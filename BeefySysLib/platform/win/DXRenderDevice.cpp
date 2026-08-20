#include "WinBFApp.h"

#ifndef BF_FORCE_SDL

#include "Common.h"
#include "DXRenderDevice.h"
#include "BFWindow.h"
#include "img/ImageData.h"
#include "util/PerfTimer.h"
#include "util/BeefPerf.h"
#include "util/Hash.h"
#include "Span.h"
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

#include <dxgi1_2.h>
#include <d3d11_1.h>

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

// Halves `samples` until the hardware supports it for `format` -- always terminates at 1.
static int ValidateSampleCount(ID3D11Device* device, DXGI_FORMAT format, int samples)
{
	int useSamples = samples;
	while (useSamples > 1)
	{
		UINT qualityLevels = 0;
		device->CheckMultisampleQualityLevels(format, useSamples, &qualityLevels);
		if (qualityLevels > 0)
			break;
		useSamples /= 2;
	}
	return BF_MAX(useSamples, 1);
}

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
	mD3DInstLayout = NULL;
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
	if (mD3DInstLayout != NULL)
		mD3DInstLayout->Release();
	mD3DInstLayout = NULL;
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

extern "C" typedef HRESULT(WINAPI* Func_D3DX10Compile)(void* srcData, size_t srcSize, char* sourceName, CONST D3D10_SHADER_MACRO* pDefines, LPD3D10INCLUDE pInclude,
	LPCSTR pFunctionName, LPCSTR pProfile, UINT Flags1, UINT Flags2, ID3D10Blob** ppShader, ID3D10Blob** ppErrorMsgs);
static Func_D3DX10Compile gFunc_D3DX10Compile;

// Compiled shaders are cached next to the source as "<file>_<entry>_<profile>", keyed by a hash of
// the exact bytes handed to the compiler (plus entry/profile/flags) -- not by file times, which lie
// whenever a copy preserves mtimes or the clock/zone shifts. Layout: ShaderCacheHeader then the raw
// DXBC blob. A missing/legacy/mismatched header just means recompile.
struct ShaderCacheHeader
{
	uint32 mMagic;
	uint32 mVersion;
	uint64 mHash;
};
static const uint32 cShaderCacheMagic = 0x43534642; // 'BFSC'
static const uint32 cShaderCacheVersion = 1;
static const UINT cShaderCompileFlags = D3D10_SHADER_DEBUG | D3D10_SHADER_ENABLE_STRICTNESS;

static bool ReadShaderCache(const StringImpl& cachePath, uint64 wantHash, bool requireHashMatch, ID3D10Blob** outBuffer)
{
	FILE* fp = fopen(cachePath.c_str(), "rb");
	if (fp == NULL)
		return false;

	fseek(fp, 0, SEEK_END);
	int fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	ShaderCacheHeader header = { 0 };
	int blobOfs = 0;
	if ((fileSize >= (int)sizeof(header)) && (fread(&header, sizeof(header), 1, fp) == 1) &&
		(header.mMagic == cShaderCacheMagic) && (header.mVersion == cShaderCacheVersion))
	{
		if ((requireHashMatch) && (header.mHash != wantHash))
		{
			fclose(fp);
			return false;
		}
		blobOfs = sizeof(header);
	}
	else if (requireHashMatch)
	{
		// Legacy headerless cache (or corrupt) -- can't verify it.
		fclose(fp);
		return false;
	}

	int blobSize = fileSize - blobOfs;
	if (blobSize <= 0)
	{
		fclose(fp);
		return false;
	}
	fseek(fp, blobOfs, SEEK_SET);
	D3D10CreateBlob(blobSize, outBuffer);
	int readSize = (int)fread((*outBuffer)->GetBufferPointer(), 1, blobSize, fp);
	fclose(fp);
	return readSize == blobSize;
}

static void WriteShaderCache(const StringImpl& cachePath, uint64 hash, ID3D10Blob* blob)
{
	FILE* fp = fopen(cachePath.c_str(), "wb");
	if (fp == NULL)
		return;
	ShaderCacheHeader header = { cShaderCacheMagic, cShaderCacheVersion, hash };
	fwrite(&header, sizeof(header), 1, fp);
	fwrite(blob->GetBufferPointer(), 1, blob->GetBufferSize(), fp);
	fclose(fp);
}

static bool LoadDXShader(const StringImpl& filePath, const StringImpl& entry, const StringImpl& profile, ID3D10Blob** outBuffer)
{
	String cachePath = filePath + "_" + entry + "_" + profile;

	int srcSize = 0;
	uint8* srcData = LoadBinaryData(filePath, &srcSize);
	if (srcData == NULL)
	{
		// No source at all (eg a shipped build) -- whatever cache exists is the best we have.
		if (ReadShaderCache(cachePath, 0, false, outBuffer))
			return true;
		BF_FATAL(StrFormat("Shader source not found: %s", filePath.c_str()).c_str());
		return false;
	}

	uint64 hash = Hash64(srcData, srcSize);
	hash = Hash64(entry.c_str(), (int)entry.length(), hash);
	hash = Hash64(profile.c_str(), (int)profile.length(), hash);
	hash = Hash64(&cShaderCompileFlags, sizeof(cShaderCompileFlags), hash);

	if (ReadShaderCache(cachePath, hash, true, outBuffer))
	{
		delete [] srcData;
		return true;
	}

	if (gFunc_D3DX10Compile == NULL)
	{
		auto lib = LoadLibraryA("D3DCompiler_47.dll");
		if (lib != NULL)
			gFunc_D3DX10Compile = (Func_D3DX10Compile)::GetProcAddress(lib, "D3DCompile");
	}
	if (gFunc_D3DX10Compile == NULL)
	{
		// No compiler on this machine: a stale cache still beats nothing.
		delete [] srcData;
		if (ReadShaderCache(cachePath, hash, false, outBuffer))
			return true;
		BF_FATAL("Shader compiler unavailable and no cached shader");
		return false;
	}

	// Compiled from the in-memory bytes (the same bytes the hash covers) -- note this means #include
	// isn't supported.
	ID3D10Blob* errorMessage = NULL;
	HRESULT dxResult = gFunc_D3DX10Compile(srcData, srcSize, "Shader", NULL, NULL, entry.c_str(), profile.c_str(),
		cShaderCompileFlags, 0, outBuffer, &errorMessage);
	delete [] srcData;

	if (FAILED(dxResult))
	{
		if (errorMessage != NULL)
		{
			BF_FATAL(StrFormat("Shader compile failed (%s): %s", filePath.c_str(), (char*)errorMessage->GetBufferPointer()).c_str());
			errorMessage->Release();
		}
		else
			BF_FATAL(StrFormat("Shader compile failed: %s", filePath.c_str()).c_str());
		return false;
	}

	WriteShaderCache(cachePath, hash, *outBuffer);
	return true;
}

static bool LoadDXShader(Span<uint8> fileData, const StringImpl& entry, const StringImpl& profile, ID3D10Blob** outBuffer)
{
	HRESULT hr;
	
	if (gFunc_D3DX10Compile == NULL)
	{
		auto lib = LoadLibraryA("D3DCompiler_47.dll");
		if (lib != NULL)
			gFunc_D3DX10Compile = (Func_D3DX10Compile)::GetProcAddress(lib, "D3DCompile");
	}

	ID3D10Blob* errorMessage = NULL;
	auto dxResult = gFunc_D3DX10Compile(fileData.mVals, fileData.mSize, "ShaderSource", NULL, NULL, entry.c_str(), profile.c_str(),
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

	return true;
}

bool DXShader::Load()
{
	//HRESULT hr;

	ID3D10Blob* errorMessage = NULL;
	ID3D10Blob* vertexShaderBuffer = NULL;
	ID3D10Blob* pixelShaderBuffer = NULL;

	void* memPtr = NULL;
	int memSize = 0;
	if (ParseMemorySpan(mSrcPath, memPtr, memSize))
	{		
		int crPos = (int)mSrcPath.IndexOf('\n');
		if (crPos != -1)
		{
			void* memPtr2 = NULL;
			int memSize2 = 0;
			if (ParseMemorySpan(mSrcPath.Substring(crPos + 1), memPtr2, memSize2))
			{
				D3D10CreateBlob(memSize, &vertexShaderBuffer);
				memcpy(vertexShaderBuffer->GetBufferPointer(), memPtr, memSize);
				D3D10CreateBlob(memSize2, &pixelShaderBuffer);
				memcpy(pixelShaderBuffer->GetBufferPointer(), memPtr2, memSize2);	
			}
		}
		else
		{
			Span<uint8> span((uint8*)memPtr, memSize);
			LoadDXShader(span, "VS", "vs_4_0", &vertexShaderBuffer);
			LoadDXShader(span, "PS", "ps_4_0", &pixelShaderBuffer);
		}
	}
	else
	{
		LoadDXShader(mSrcPath + ".fx", "VS", "vs_4_0", &vertexShaderBuffer);
		LoadDXShader(mSrcPath + ".fx", "PS", "ps_4_0", &pixelShaderBuffer);
	}

	defer(
		{
			vertexShaderBuffer->Release();
			pixelShaderBuffer->Release();
		});

	mHas2DPosition = false;
	mVertexSize = 0;
	mD3DLayout = NULL;
	mD3DInstLayout = NULL;

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

	int instElemIdx = mVertexDef->mInstanceElementIdx;
	if ((instElemIdx >= 0) && (instElemIdx < mVertexDef->mNumElements))
	{
		// Same vertex layout, but the instance element comes from slot 1 (per-instance stream). Explicit
		// slot-0 offsets keep the vertex stride identical to the non-instanced layout.
		D3D11_INPUT_ELEMENT_DESC instLayout[64];
		int ofs = 0;
		for (int elementIdx = 0; elementIdx < mVertexDef->mNumElements; elementIdx++)
		{
			instLayout[elementIdx] = layout[elementIdx];
			instLayout[elementIdx].AlignedByteOffset = ofs;
			ofs += dxgiSize[mVertexDef->mElementData[elementIdx].mFormat];
		}
		instLayout[instElemIdx].InputSlot = 1;
		instLayout[instElemIdx].AlignedByteOffset = 0;
		instLayout[instElemIdx].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
		instLayout[instElemIdx].InstanceDataStepRate = 1;
		result = mRenderDevice->mD3DDevice->CreateInputLayout(instLayout, mVertexDef->mNumElements, vertexShaderBuffer->GetBufferPointer(),
			vertexShaderBuffer->GetBufferSize(), &mD3DInstLayout);
		DXCHECK(result);
		if (FAILED(result))
			return false;
	}

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
	mD3DKeyedMutex = NULL;
	mContentBits = NULL;
	mD3DFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	mSampleCount = 1;
	mStandardDepthClear = false;
	mD3DUAV = NULL;
}

DXTexture::~DXTexture()
{
	if ((!mPath.IsEmpty()) && (mRenderDevice != NULL))
		((DXRenderDevice*)mRenderDevice)->mTextureMap.Remove(mPath);

	//OutputDebugStrF("DXTexture::~DXTexture %@\n", this);
	delete mContentBits;
	if (mD3DResourceView != NULL)
		mD3DResourceView->Release();
	if (mD3DRenderTargetView != NULL)
		mD3DRenderTargetView->Release();
	if (mD3DDepthStencilView != NULL)
		mD3DDepthStencilView->Release();
	if (mD3DDepthBuffer != NULL)
		mD3DDepthBuffer->Release();
	if (mD3DKeyedMutex != NULL)
		mD3DKeyedMutex->Release();
	if (mD3DUAV != NULL)
		mD3DUAV->Release();
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
	if (mD3DRenderTargetView != NULL)
	{
		mD3DRenderTargetView->Release();
		mD3DRenderTargetView = NULL;
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
	if (mD3DKeyedMutex != NULL)
	{
		mD3DKeyedMutex->Release();
		mD3DKeyedMutex = NULL;
	}
	if (mD3DUAV != NULL)
	{
		mD3DUAV->Release();
		mD3DUAV = NULL;
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

		mRenderDevice->mCurD3DRTV = mD3DRenderTargetView;
		mRenderDevice->mCurD3DDSV = mD3DDepthStencilView;
		ID3D11RenderTargetView* rtvs[2] = { mD3DRenderTargetView, NULL };
		int rtvCount = 1;
		if (mSecondaryTarget != NULL)
		{
			rtvs[1] = ((DXTexture*)mSecondaryTarget)->mD3DRenderTargetView;
			rtvCount = 2;
		}
		mRenderDevice->mD3DDeviceContext->OMSetRenderTargets(rtvCount, rtvs, mD3DDepthStencilView);
		mRenderDevice->mD3DDeviceContext->RSSetViewports(1, &viewPort);
	}

	if (mWantsClear)
	{
		float bgColor[4] = {1, (rand() % 256) / 256.0f, 0.5, 1};
		if (mD3DRenderTargetView != NULL)
			mRenderDevice->mD3DDeviceContext->ClearRenderTargetView(mD3DRenderTargetView, bgColor);
		if (mD3DDepthStencilView != NULL)
			mRenderDevice->mD3DDeviceContext->ClearDepthStencilView(mD3DDepthStencilView, D3D11_CLEAR_DEPTH/*|D3D11_CLEAR_STENCIL*/, mStandardDepthClear ? 1.0f : 0.0f, 0);

		//mRenderDevice->mD3DDevice->ClearRenderTargetView(mD3DRenderTargetView, D3DXVECTOR4(1, 0.5, 0.5, 1));
		mHasBeenDrawnTo = true;
		if (mResetClear)
			mWantsClear = false;
	}
}

///

DXStructuredBuffer::DXStructuredBuffer()
{
	mD3DBuffer = NULL;
	mD3DStaging = NULL;
	mStride = 0;
	mGpuWritable = false;
	mDefaultUsage = false;
}

DXStructuredBuffer::~DXStructuredBuffer()
{
	if (mD3DBuffer != NULL)
		mD3DBuffer->Release();
	if (mD3DStaging != NULL)
		mD3DStaging->Release();
}

void DXStructuredBuffer::PhysSetAsTarget()
{
	BF_FATAL("Structured buffers can't be render targets");
}

void DXStructuredBuffer::UpdateBufferRange(int offset, void* data, int size)
{
	BF_ASSERT(mDefaultUsage);
	BF_ASSERT((offset >= 0) && (size > 0) && (offset + size <= mStride * mWidth));
	D3D11_BOX box = { (UINT)offset, 0, 0, (UINT)(offset + size), 1, 1 };
	mRenderDevice->mD3DDeviceContext->UpdateSubresource(mD3DBuffer, 0, &box, data, 0, 0);
}

bool DXStructuredBuffer::GetBufferData(void* outData, int size)
{
	int byteWidth = mStride * mWidth;
	if ((size <= 0) || (size > byteWidth))
		return false;
	auto ctx = mRenderDevice->mD3DDeviceContext;
	if (mD3DStaging == NULL)
	{
		D3D11_BUFFER_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Usage = D3D11_USAGE_STAGING;
		desc.ByteWidth = byteWidth;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = mStride;
		if (FAILED(mRenderDevice->mD3DDevice->CreateBuffer(&desc, NULL, &mD3DStaging)))
			return false;
	}
	ctx->CopyResource(mD3DStaging, mD3DBuffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(mD3DStaging, 0, D3D11_MAP_READ, 0, &mapped)))
		return false;
	memcpy(outData, mapped.pData, size);
	ctx->Unmap(mD3DStaging, 0);
	return true;
}

///

DXTexture3D::DXTexture3D()
{
	mD3DTexture3D = NULL;
	mD3DStaging = NULL;
	for (int i = 0; i < cMaxMips; i++)
		mD3DUAVs[i] = NULL;
	mDepth = 0;
	mMipLevels = 1;
	mBytesPerTexel = 4;
}

DXTexture3D::~DXTexture3D()
{
	for (int i = 0; i < cMaxMips; i++)
		if (mD3DUAVs[i] != NULL)
			mD3DUAVs[i]->Release();
	if (mD3DStaging != NULL)
		mD3DStaging->Release();
	if (mD3DTexture3D != NULL)
		mD3DTexture3D->Release();
}

void DXTexture3D::PhysSetAsTarget()
{
	BF_FATAL("3D textures can't be render targets");
}

void DXTexture3D::SetData3D(int mipLevel, void* data, int rowPitch, int slicePitch)
{
	if ((mipLevel < 0) || (mipLevel >= mMipLevels))
		return;
	mRenderDevice->mD3DDeviceContext->UpdateSubresource(mD3DTexture3D, mipLevel, NULL, data, rowPitch, slicePitch);
}

bool DXTexture3D::GetData3D(int mipLevel, void* outData, int outSize)
{
	if ((mipLevel < 0) || (mipLevel >= mMipLevels))
		return false;
	int w = BF_MAX(1, mWidth >> mipLevel);
	int h = BF_MAX(1, mHeight >> mipLevel);
	int d = BF_MAX(1, mDepth >> mipLevel);
	int rowBytes = w * mBytesPerTexel;
	if (outSize < rowBytes * h * d)
		return false;

	auto ctx = mRenderDevice->mD3DDeviceContext;
	if (mD3DStaging == NULL)
	{
		D3D11_TEXTURE3D_DESC desc;
		mD3DTexture3D->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		if (FAILED(mRenderDevice->mD3DDevice->CreateTexture3D(&desc, NULL, &mD3DStaging)))
			return false;
	}
	ctx->CopyResource(mD3DStaging, mD3DTexture3D);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(mD3DStaging, mipLevel, D3D11_MAP_READ, 0, &mapped)))
		return false;
	uint8* dest = (uint8*)outData;
	for (int z = 0; z < d; z++)
	{
		for (int y = 0; y < h; y++)
		{
			memcpy(dest, (uint8*)mapped.pData + z * mapped.DepthPitch + y * mapped.RowPitch, rowBytes);
			dest += rowBytes;
		}
	}
	ctx->Unmap(mD3DStaging, mipLevel);
	return true;
}

void DXTexture3D::GenerateMips()
{
	if (mMipLevels > 1)
		mRenderDevice->mD3DDeviceContext->GenerateMips(mD3DResourceView);
}

///

DXComputeShader::DXComputeShader()
{
	mRenderDevice = NULL;
	mD3DComputeShader = NULL;
}

DXComputeShader::~DXComputeShader()
{
	if (mD3DComputeShader != NULL)
		mD3DComputeShader->Release();
	if (mRenderDevice != NULL)
		mRenderDevice->mComputeShaders.Remove(this);
}

bool DXComputeShader::Load()
{
	if (mRenderDevice->mD3DDevice->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
	{
		BF_FATAL("Compute shaders need a Direct3D 11.0 feature level device");
		return false;
	}
	ID3D10Blob* blob = NULL;
	if (!LoadDXShader(mSrcPath + ".fx", mEntry, "cs_5_0", &blob))
		return false;
	HRESULT hr = mRenderDevice->mD3DDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &mD3DComputeShader);
	blob->Release();
	return SUCCEEDED(hr);
}

///

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
	if ((srcWidth <= 0) || (srcHeight <= 0))
		return;

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

// Reads back the render target's real depth buffer (R32_TYPELESS/D32_FLOAT -- see CreateRenderTarget)
// as raw float bits. D3D11 rules: a depth resource can't be mapped and can't be partially copied, so
// this CopyResource's the whole buffer into a same-desc staging texture and reads the rect from the
// map. MSAA depth can't be staging-copied at all; readback callers are 1-sample by design.
void DXTexture::GetDepthBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits)
{
	if ((srcWidth <= 0) || (srcHeight <= 0))
		return;
	if (mD3DDepthBuffer == NULL)
		return;

	D3D11_TEXTURE2D_DESC texDesc;
	mD3DDepthBuffer->GetDesc(&texDesc);
	BF_ASSERT(texDesc.SampleDesc.Count == 1);
	texDesc.BindFlags = 0;
	texDesc.MiscFlags = 0;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D *texture;
	DXCHECK(mRenderDevice->mD3DDevice->CreateTexture2D(&texDesc, 0, &texture));
	mRenderDevice->mD3DDeviceContext->CopyResource(texture, mD3DDepthBuffer);

	D3D11_MAPPED_SUBRESOURCE mapTex;
	DXCHECK(mRenderDevice->mD3DDeviceContext->Map(texture, 0, D3D11_MAP_READ, NULL, &mapTex));

	uint8* srcPtr = (uint8*) mapTex.pData + srcY * mapTex.RowPitch + srcX * sizeof(uint32);
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

// A new DXTexture sharing this render target's depth buffer, sampleable as R32_FLOAT -- lets a
// shader read the depth that filled while the color plane was being rendered (SSAO/SSR inputs).
// 1-sample only; the wrapper AddRefs the resource, so either can be deleted first.
Texture* DXTexture::CreateDepthRef()
{
	if (mD3DDepthBuffer == NULL)
		return NULL;

	D3D11_TEXTURE2D_DESC desc;
	mD3DDepthBuffer->GetDesc(&desc);
	if (desc.SampleDesc.Count > 1)
		return NULL;

	DXTexture* ref = new DXTexture();
	ref->mWidth = mWidth;
	ref->mHeight = mHeight;
	ref->mRenderDevice = mRenderDevice;
	ref->mD3DTexture = mD3DDepthBuffer;
	mD3DDepthBuffer->AddRef();
	ref->mD3DFormat = DXGI_FORMAT_R32_FLOAT;

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	ZeroMemory(&srDesc, sizeof(srDesc));
	srDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;
	DXCHECK(((DXRenderDevice*)mRenderDevice)->mD3DDevice->CreateShaderResourceView(mD3DDepthBuffer, &srDesc, &ref->mD3DResourceView));

	ref->AddRef();
	return ref;
}

void* DXTexture::GetSharedHandle()
{
	IDXGIResource* dxgiResource = NULL;
	HANDLE handle = NULL;
	if (SUCCEEDED(mD3DTexture->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiResource)))
	{
		dxgiResource->GetSharedHandle(&handle);
		dxgiResource->Release();
	}
	return handle;
}

bool DXTexture::AcquireKeyedMutex(uint64 key, uint32 timeoutMs)
{	
	if (mD3DKeyedMutex == NULL)
		return false;
	auto result = mD3DKeyedMutex->AcquireSync(key, timeoutMs);
	if ((result == WAIT_ABANDONED) || (result == WAIT_TIMEOUT))
		return false;
	return SUCCEEDED(result);
}

void DXTexture::ReleaseKeyedMutex(uint64 key)
{
	if (mD3DKeyedMutex != NULL)
		mD3DKeyedMutex->ReleaseSync(key);
}

void DXTexture::ResolveTo(Texture* dest)
{
	DXTexture* dxDest = (DXTexture*)dest;
	BF_ASSERT(mSampleCount > 1);
	BF_ASSERT(dxDest->mSampleCount == 1);
	BF_ASSERT((mWidth == dxDest->mWidth) && (mHeight == dxDest->mHeight) && (mD3DFormat == dxDest->mD3DFormat));
	((DXRenderDevice*)mRenderDevice)->mD3DDeviceContext->ResolveSubresource(dxDest->mD3DTexture, 0, mD3DTexture, 0, mD3DFormat);
}

void DXTexture::GenerateMips()
{
	((DXRenderDevice*)mRenderDevice)->mD3DDeviceContext->GenerateMips(mD3DResourceView);
}

void DXTexture::CopyToMip(int mipLevel, Texture* src, int width, int height)
{
	DXTexture* dxSrc = (DXTexture*)src;
	BF_ASSERT(dxSrc->mD3DFormat == mD3DFormat);
	D3D11_BOX box = { 0, 0, 0, (UINT)width, (UINT)height, 1 };
	((DXRenderDevice*)mRenderDevice)->mD3DDeviceContext->CopySubresourceRegion(mD3DTexture, mipLevel, 0, 0, 0, dxSrc->mD3DTexture, 0, &box);
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
		((mRenderState->mClipRect.width == 0) || (mRenderState->mClipRect.height == 0)))
		return;

	if (mRenderState->mClipped)
		BF_ASSERT((mRenderState->mClipRect.width > 0) && (mRenderState->mClipRect.height > 0));

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

DXStaticMesh::DXStaticMesh()
{
	mD3DVertexBuffer = NULL;
	mD3DIndexBuffer = NULL;
}

DXStaticMesh::~DXStaticMesh()
{
	if (mD3DVertexBuffer != NULL)
		mD3DVertexBuffer->Release();
	if (mD3DIndexBuffer != NULL)
		mD3DIndexBuffer->Release();
}

StaticMesh* DXRenderDevice::CreateStaticMesh(int vertexSize, void* vtxData, int vtxCount, void* idxData, int idxCount, bool idx32)
{
	DXStaticMesh* mesh = new DXStaticMesh();
	mesh->mVtxSize = vertexSize;
	mesh->mVtxCount = vtxCount;
	mesh->mIdxCount = idxCount;
	mesh->mIdx32 = idx32;

	D3D11_BUFFER_DESC bd = { 0 };
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.ByteWidth = vertexSize * vtxCount;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA init = { 0 };
	init.pSysMem = vtxData;
	HRESULT result = mD3DDevice->CreateBuffer(&bd, &init, &mesh->mD3DVertexBuffer);
	DXCHECK(result);

	bd.ByteWidth = (idx32 ? sizeof(uint32) : sizeof(uint16)) * idxCount;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	init.pSysMem = idxData;
	result = mD3DDevice->CreateBuffer(&bd, &init, &mesh->mD3DIndexBuffer);
	DXCHECK(result);

	if ((mesh->mD3DVertexBuffer == NULL) || (mesh->mD3DIndexBuffer == NULL))
	{
		delete mesh;
		return NULL;
	}
	return mesh;
}

void DXRenderDevice::EnsureInstIota(int count)
{
	if (count <= mInstIotaCount)
		return;
	int newCount = BF_MAX(count, BF_MAX(mInstIotaCount * 2, 65536));
	float* data = new float[newCount];
	for (int i = 0; i < newCount; i++)
		data[i] = (float)(i + 1);
	if (mInstIotaBuffer != NULL)
		mInstIotaBuffer->Release();
	mInstIotaBuffer = NULL;
	D3D11_BUFFER_DESC bd = { 0 };
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.ByteWidth = sizeof(float) * newCount;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA init = { 0 };
	init.pSysMem = data;
	DXCHECK(mD3DDevice->CreateBuffer(&bd, &init, &mInstIotaBuffer));
	delete [] data;
	mInstIotaCount = newCount;
}

void DXStaticMeshDrawCmd::CommandQueued(DrawLayer* drawLayer)
{
	mRenderState = drawLayer->mRenderDevice->mCurRenderState;
}

void DXStaticMeshDrawCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if ((mMesh == NULL) || (mInstCount <= 0))
		return;
	if ((mRenderState->mClipped) &&
		((mRenderState->mClipRect.width == 0) || (mRenderState->mClipRect.height == 0)))
		return;

	DXRenderDevice* dev = (DXRenderDevice*)renderDevice;
	if (mRenderState != dev->mPhysRenderState)
		dev->PhysSetRenderState(mRenderState);
	DXShader* shader = (DXShader*)mRenderState->mShader;
	if ((shader == NULL) || (shader->mD3DInstLayout == NULL))
		return; // the shader's vertex definition has no instance element
	dev->EnsureInstIota(mInstBase + mInstCount);

	ID3D11DeviceContext* ctx = dev->mD3DDeviceContext;
	ctx->IASetInputLayout(shader->mD3DInstLayout);
	ID3D11Buffer* bufs[2] = { mMesh->mD3DVertexBuffer, dev->mInstIotaBuffer };
	UINT strides[2] = { (UINT)mMesh->mVtxSize, sizeof(float) };
	UINT offsets[2] = { 0, (UINT)(mInstBase * sizeof(float)) };
	ctx->IASetVertexBuffers(0, 2, bufs, strides, offsets);
	ctx->IASetIndexBuffer(mMesh->mD3DIndexBuffer, mMesh->mIdx32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
	ctx->DrawIndexedInstanced(mMesh->mIdxCount, mInstCount, 0, 0, 0);
	// PhysSetRenderState only sets the layout on a shader change, so put the batch layout back for the
	// dynamic batches that follow under this same render state.
	ctx->IASetInputLayout(shader->mD3DLayout);
}

void DXDrawLayer::DrawStaticMeshInstanced(StaticMesh* mesh, int instBase, int instCount)
{
	DXStaticMeshDrawCmd* cmd = AllocRenderCmd<DXStaticMeshDrawCmd>();
	cmd->mMesh = (DXStaticMesh*)mesh;
	cmd->mInstBase = instBase;
	cmd->mInstCount = instCount;
	QueueRenderCmd(cmd);
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
	BP_ZONE("DXRenderDevice::PhysSetRenderState");
	DXRenderState* dxRenderState = (DXRenderState*)renderState;
	DXShader* dxShader = (DXShader*)renderState->mShader;

	if (renderState->mTopology != mPhysRenderState->mTopology)
	{
		D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		if (dxRenderState->mTopology == Topology3D_LineLine)
			topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		mD3DDeviceContext->IASetPrimitiveTopology(topology);
	}

	bool shaderChanged = (renderState->mShader != mPhysRenderState->mShader) && (renderState->mShader != NULL);
	bool pixelShaderDisableChanged = renderState->mDisablePixelShader != mPhysRenderState->mDisablePixelShader;
	bool samplerKindChanged = renderState->mSamplerKind != mPhysRenderState->mSamplerKind;

	if (shaderChanged || pixelShaderDisableChanged || samplerKindChanged)
	{
		if (samplerKindChanged)
		{
			ID3D11SamplerState* samplerState = mD3DDefaultSamplerState;
			if (renderState->mSamplerKind == SamplerKind_Wrap)
				samplerState = mD3DWrapSamplerState;
			else if (renderState->mSamplerKind == SamplerKind_Nearest)
				samplerState = mD3DNearestSamplerState;
			mD3DDeviceContext->PSSetSamplers(0, 1, &samplerState);
		}

		if (dxShader != NULL)
		{
			if (shaderChanged)
			{
				mD3DDeviceContext->IASetInputLayout(dxShader->mD3DLayout);
				mD3DDeviceContext->VSSetShader(dxShader->mD3DVertexShader, NULL, 0);
			}
			mD3DDeviceContext->PSSetShader(renderState->mDisablePixelShader ? NULL : dxShader->mD3DPixelShader, NULL, 0);

			if ((shaderChanged) && (dxShader->mHas2DPosition))
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
				float* dataPtr = (float*)mappedResource.pData;
				dataPtr[0] = (float)mCurRenderTarget->mWidth;
				dataPtr[1] = (float)mCurRenderTarget->mHeight;
				dataPtr[2] = 0;
				dataPtr[3] = 0;

				// Unlock the constant buffer.
				mD3DDeviceContext->Unmap(mMatrix2DBuffer, 0);

				//float params[4] = {mCurRenderTarget->mWidth, mCurRenderTarget->mHeight, 0, 0};
				mD3DDeviceContext->VSSetConstantBuffers(0, 1, &mMatrix2DBuffer);
			}
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
			rects[0].left = (int)renderState->mClipRect.x;
			rects[0].right = (int) (renderState->mClipRect.x + renderState->mClipRect.width);
			rects[0].top = (int) renderState->mClipRect.y;
			rects[0].bottom = (int) (renderState->mClipRect.y + renderState->mClipRect.height);
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

	if (renderState->mCullMode != mPhysRenderState->mCullMode)
		setRasterizerState = true;

	if (renderState->mFrontFace != mPhysRenderState->mFrontFace)
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
			rasterizerState.FrontCounterClockwise = dxRenderState->mFrontFace == FrontFace_CounterClockwise;
			rasterizerState.DepthBias = 0;
			rasterizerState.DepthBiasClamp = 0;
			rasterizerState.SlopeScaledDepthBias = 0;
			rasterizerState.DepthClipEnable = renderState->mDepthFunc != DepthFunc_Always;
			rasterizerState.ScissorEnable = renderState->mClipped;
			// Quadrilateral line rasterization on MSAA targets (ignored on single-sample ones).
			rasterizerState.MultisampleEnable = true;
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
	
	if (renderState->mDisableRenderTarget != mPhysRenderState->mDisableRenderTarget)
	{
		if (renderState->mDisableRenderTarget)
			mD3DDeviceContext->OMSetRenderTargets(0, NULL, mCurD3DDSV);
		else
			mD3DDeviceContext->OMSetRenderTargets(1, &mCurD3DRTV, mCurD3DDSV);
	}

	if (renderState->mDisableBlend != mPhysRenderState->mDisableBlend)
		mD3DDeviceContext->OMSetBlendState(renderState->mDisableBlend ? NULL : mD3DNormalBlendState, NULL, 0xffffffff);

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

void DXRenderDevice::PhysSetViewportRect(int x, int y, int width, int height, bool clear)
{
	D3D11_VIEWPORT viewPort;
	viewPort.TopLeftX = (float)x;
	viewPort.TopLeftY = (float)y;
	viewPort.Width = (float)width;
	viewPort.Height = (float)height;
	viewPort.MinDepth = 0.0f;
	viewPort.MaxDepth = 1.0f;
	mD3DDeviceContext->RSSetViewports(1, &viewPort);

	if (!clear)
		return;
	D3D11_RECT rect = { x, y, x + width, y + height };
	if (mD3DDeviceContext1 != NULL)
	{
		if (mCurD3DRTV != NULL)
		{
			float bgColor[4] = { 1, 0, 0.5f, 1 };
			mD3DDeviceContext1->ClearView(mCurD3DRTV, bgColor, &rect, 1);
		}
		if (mCurD3DDSV != NULL)
		{
			// ClearView on a depth view takes the depth from Color[0] (depth-only formats, which is
			// all our depth targets use).
			float depth[4] = { 1, 0, 0, 0 };
			mD3DDeviceContext1->ClearView(mCurD3DDSV, depth, &rect, 1);
		}
	}
	else
	{
		// 11.0 fallback: no rect clears, so the whole target goes.
		if (mCurD3DRTV != NULL)
		{
			float bgColor[4] = { 1, 0, 0.5f, 1 };
			mD3DDeviceContext->ClearRenderTargetView(mCurD3DRTV, bgColor);
		}
		if (mCurD3DDSV != NULL)
			mD3DDeviceContext->ClearDepthStencilView(mCurD3DDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	}
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

void DXRenderDevice::ReleaseRenderState(RenderState* renderState)
{
	mRenderStates.Remove((DXRenderState*)renderState);
	delete renderState;
}

struct DXModelVertex
{
	Vector3 mPosition;
	uint32 mColor;
	TexCoords mTexCoords;
	Vector3 mNormal;
	TexCoords mBumpTexCoords;
	Vector3 mTangent;
	float mInstanceIdx; // 0 = per-draw constants (see Gfx_DrawIndexedVerticesInst)
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
		{VertexElementUsage_Tangent,			0, VertexElementFormat_Vector3},
		{VertexElementUsage_TextureCoordinate,	2, VertexElementFormat_Single}
	};

	auto vertexDefinition = CreateVertexDefinition(vertexDefData, sizeof(vertexDefData) / sizeof(vertexDefData[0]));
	/*RenderState* renderState = NULL;
	if ((flags & ModelCreateFlags_NoSetRenderState) == 0)
	{
		renderState = CreateRenderState(mDefaultRenderState);
		renderState->mShader = LoadShader(gBFApp->mInstallDir + "/shaders/ModelStd", vertexDefinition);
		renderState->mTexWrap = true;
		renderState->mDepthFunc = DepthFunc_LessEqual;
		renderState->mWriteDepthBuffer = true;
	}*/
	delete vertexDefinition;

	//dxModelInstance->mRenderState = renderState;

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

				DXTexture* texture = (DXTexture*)((RenderDevice*)this)->LoadTexture(texPath, TextureFlag_NoPremult | TextureFlag_Mipmaps | TextureFlag_Srgb);
				if (texture == NULL)
					texture = (DXTexture*)((RenderDevice*)this)->LoadTexture("!white", TextureFlag_NoPremult | TextureFlag_Mipmaps | TextureFlag_Srgb);
				dxPrimitives->mTextures.Add(texture);
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
				destVtx->mInstanceIdx = 0;
			}

			mD3DDeviceContext->Unmap(dxPrimitives->mD3DVertexBuffer, 0);

			dxMeshIdx++;
		}
	}

	return dxModelInstance;
}

void DXDrawLayer::SetBufferData(Texture* buffer, void* data, int size)
{
	DXSetBufferDataCmd* cmd = AllocRenderCmd<DXSetBufferDataCmd>();
	cmd->mBuffer = (DXStructuredBuffer*)buffer;
	cmd->mSize = size;
	cmd->mData = new uint8[size];
	memcpy(cmd->mData, data, size);
	QueueRenderCmd(cmd);
}

void DXDrawLayer::SetComputeTexture(int slot, Texture* texture)
{
	BF_ASSERT((slot >= 0) && (slot < 32));
	DXSetComputeTextureCmd* cmd = AllocRenderCmd<DXSetComputeTextureCmd>();
	cmd->mSlot = slot;
	cmd->mTexture = (DXTexture*)texture;
	QueueRenderCmd(cmd);
}

void DXDrawLayer::SetComputeUAV(int slot, Texture* texture, int mipLevel)
{
	BF_ASSERT((slot >= 0) && (slot < D3D11_PS_CS_UAV_REGISTER_COUNT));
	DXSetComputeUAVCmd* cmd = AllocRenderCmd<DXSetComputeUAVCmd>();
	cmd->mSlot = slot;
	cmd->mMipLevel = mipLevel;
	cmd->mTexture = (DXTexture*)texture;
	QueueRenderCmd(cmd);
}

void DXDrawLayer::Dispatch(ComputeShader* shader, int groupsX, int groupsY, int groupsZ)
{
	DXDispatchCmd* cmd = AllocRenderCmd<DXDispatchCmd>();
	cmd->mShader = (DXComputeShader*)shader;
	cmd->mGroupsX = groupsX;
	cmd->mGroupsY = groupsY;
	cmd->mGroupsZ = groupsZ;
	QueueRenderCmd(cmd);
	// A UAV bind evicts any pixel-shader view of the same resource, so the next SetTexture must
	// re-bind even when this layer thinks the slot is current.
	for (int texIdx = 0; texIdx < MAX_TEXTURES; texIdx++)
		mCurTextures[texIdx] = (Texture*)(intptr)-1;
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

void DXRenderState::SetSamplerKind(SamplerKind samplerKind)
{
	mSamplerKind = samplerKind;
	InvalidateRasterizerState();
}

void DXRenderState::SetClipRect(const RectF& rect)
{
	BF_ASSERT((rect.width >= 0) && (rect.height >= 0));
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

void DXRenderState::SetCullMode(CullMode cullMode)
{
	mCullMode = cullMode;
	InvalidateRasterizerState();
}

void DXRenderState::SetFrontFace(FrontFace frontFace)
{
	mFrontFace = frontFace;
	InvalidateRasterizerState();
}

///

DXModelInstance::DXModelInstance(ModelDef* modelDef) : ModelInstance(modelDef)
{
}

DXModelInstance::~DXModelInstance()
{
}

void DXModelInstance::Render(RenderCmd* renderCmd, RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if (renderCmd->mRenderState != NULL)
		renderCmd->SetRenderState();

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
			{
				ID3D11ShaderResourceView* const* resView = NULL;
				if ((i < dxPrimitives->mTextures.size()) && (dxPrimitives->mTextures[i] != NULL))
				{
					resView = &dxPrimitives->mTextures[i]->mD3DResourceView;
					mD3DRenderDevice->mD3DDeviceContext->PSSetShaderResources(i, 1, resView);
				}
			}

			// Set vertex buffer
			UINT stride = sizeof(DXModelVertex);
			UINT offset = 0;
			mD3DRenderDevice->mD3DDeviceContext->IASetVertexBuffers(0, 1, &dxPrimitives->mD3DVertexBuffer, &stride, &offset);
			mD3DRenderDevice->mD3DDeviceContext->IASetIndexBuffer(dxPrimitives->mD3DIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
			mD3DRenderDevice->mD3DDeviceContext->DrawIndexed(dxPrimitives->mNumIndices, 0, 0);
		}
	}
}

void Beefy::DXModelInstance::CommandQueued(RenderCmd* renderCmd, DrawLayer* drawLayer)
{
	renderCmd->mRenderState = drawLayer->mRenderDevice->mCurRenderState;
	BF_ASSERT(renderCmd->mRenderState->mShader->mVertexSize == sizeof(DXModelVertex));
	//RenderState* layerState = drawLayer->mRenderDevice->mCurRenderState;
	//BF_ASSERT(layerState->mShader->mVertexSize == sizeof(DXModelVertex));
	//if (mRenderState != NULL)
	//{
	//	// Keep our depth/write settings from model creation; only adopt the shader.
	//	// The draw layer's current state is a 2D state with depth disabled by default.
	//	mRenderState->mShader = layerState->mShader;
	//}
	//else
	//{
	//	mRenderState = layerState;
	//}

	drawLayer->mCurTextures[0] = NULL;

	if (!mDirty)
		return;
	mDirty = false;

#ifndef BF_NO_FBX
	Matrix4 jointsMatrices[BF_MAX_NUM_BONES];
	ComputeSkinningJointMatrices(jointsMatrices);

	for (int meshIdx = 0; meshIdx < (int) mModelDef->mMeshes.size(); meshIdx++)
	{
		if (!mMeshesVisible[meshIdx])
			continue;

		ModelMesh* mesh = &mModelDef->mMeshes[meshIdx];
		DXModelMesh* dxMesh = &mDXModelMeshs[meshIdx];

		for (int primsIdx = 0; primsIdx < (int)dxMesh->mPrimitives.size(); primsIdx++)
		{
			ModelPrimitives* modelPrims = &mesh->mPrimitives[primsIdx];
			DXModelPrimitives* dxPrims = &dxMesh->mPrimitives[primsIdx];

			D3D11_MAPPED_SUBRESOURCE mappedSubResource;
			DXRenderDevice* dxRenderDevice = (DXRenderDevice*)drawLayer->mRenderDevice;
			DXCHECK(dxRenderDevice->mD3DDeviceContext->Map(dxPrims->mD3DVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubResource));
			DXModelVertex* dxVtxData = (DXModelVertex*)mappedSubResource.pData;
			for (int vtxIdx = 0; vtxIdx < (int)modelPrims->mVertices.size(); vtxIdx++)
			{
				ModelVertex* srcVtxData = &modelPrims->mVertices[vtxIdx];

				Vector3 vtx(0, 0, 0);
				Vector3 normal(0, 0, 0);
				Vector3 tangent(0, 0, 0);

				float totalWeight = 0;

				if (srcVtxData->mNumBoneWeights > 0)
				{
					for (int weightIdx = 0; weightIdx < srcVtxData->mNumBoneWeights; weightIdx++)
					{
						int jointIdx = srcVtxData->mBoneIndices[weightIdx];
						float boneWeight = srcVtxData->mBoneWeights[weightIdx];

						Matrix4* mtx = &jointsMatrices[jointIdx];

						vtx = vtx + Vector3::Transform(srcVtxData->mPosition, *mtx) * boneWeight;

						Vector3 origNormal = srcVtxData->mNormal;
						normal = normal + Vector3(
							mtx->m00 * origNormal.mX + mtx->m01 * origNormal.mY + mtx->m02 * origNormal.mZ,
							mtx->m10 * origNormal.mX + mtx->m11 * origNormal.mY + mtx->m12 * origNormal.mZ,
							mtx->m20 * origNormal.mX + mtx->m21 * origNormal.mY + mtx->m22 * origNormal.mZ) * boneWeight;

						Vector3 origTangent = srcVtxData->mTangent;
						tangent = tangent + Vector3(
							mtx->m00 * origTangent.mX + mtx->m01 * origTangent.mY + mtx->m02 * origTangent.mZ,
							mtx->m10 * origTangent.mX + mtx->m11 * origTangent.mY + mtx->m12 * origTangent.mZ,
							mtx->m20 * origTangent.mX + mtx->m21 * origTangent.mY + mtx->m22 * origTangent.mZ) * boneWeight;

						totalWeight += boneWeight;
					}
					BF_ASSERT(fabs(totalWeight - 1.0) < 0.1f);
				}
				else
				{
					vtx = srcVtxData->mPosition;
					normal = srcVtxData->mNormal;
					tangent = srcVtxData->mTangent;
				}

				DXModelVertex* destVtx = dxVtxData + vtxIdx;

				destVtx->mPosition = vtx;
				destVtx->mNormal = Vector3::Normalize(normal);
				destVtx->mTangent = Vector3::Normalize(tangent);
				destVtx->mTexCoords = srcVtxData->mTexCoords;
				destVtx->mBumpTexCoords = srcVtxData->mBumpTexCoords;
				destVtx->mColor = 0xFFFFFFFF; //TODO: Color
				destVtx->mInstanceIdx = 0;
			}

			dxRenderDevice->mD3DDeviceContext->Unmap(dxPrims->mD3DVertexBuffer, 0);
		}
	}
#endif
}

///


void DXSetTextureCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	ID3D11ShaderResourceView* srv = ((DXTexture*)mTexture)->mD3DResourceView;
	dxRenderDevice->mD3DDeviceContext->PSSetShaderResources(mTextureIdx, 1, &srv);
	// Slots from DX_VS_TEXTURE_SLOT up are readable by the vertex stage too (per-draw instance records).
	if (mTextureIdx >= DX_VS_TEXTURE_SLOT)
		dxRenderDevice->mD3DDeviceContext->VSSetShaderResources(mTextureIdx, 1, &srv);
}

///

void DXSetBufferDataCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	int byteWidth = mBuffer->mStride * mBuffer->mWidth;
	BF_ASSERT(mSize <= byteWidth);

	if (mBuffer->mDefaultUsage)
	{
		// USAGE_DEFAULT can't be mapped; a partial upload keeps the tail.
		D3D11_BOX box = { 0, 0, 0, (UINT)mSize, 1, 1 };
		dxRenderDevice->mD3DDeviceContext->UpdateSubresource(mBuffer->mD3DBuffer, 0, &box, mData, 0, 0);
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	if (FAILED(dxRenderDevice->mD3DDeviceContext->Map(mBuffer->mD3DBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)))
		return;
	memcpy(mappedResource.pData, mData, mSize);
	dxRenderDevice->mD3DDeviceContext->Unmap(mBuffer->mD3DBuffer, 0);
}

void DXSetBufferDataCmd::Free()
{
	delete[] mData;
	mData = NULL;
	RenderCmd::Free();
}

///

void DXSetConstantData::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
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
		dxRenderDevice->mD3DDeviceContext->VSSetConstantBuffers(mSlotIdx, 1, &buffer);
	else if (mUsageIdx == 2)
		dxRenderDevice->mD3DDeviceContext->CSSetConstantBuffers(mSlotIdx, 1, &buffer);
	else
		dxRenderDevice->mD3DDeviceContext->PSSetConstantBuffers(mSlotIdx, 1, &buffer);
}

///

void DXSetComputeTextureCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	ID3D11ShaderResourceView* srv = (mTexture != NULL) ? mTexture->mD3DResourceView : NULL;
	dxRenderDevice->mD3DDeviceContext->CSSetShaderResources(mSlot, 1, &srv);
	if (srv != NULL)
		dxRenderDevice->mCSBoundSRVs |= 1u << mSlot;
}

void DXSetComputeUAVCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	ID3D11UnorderedAccessView* uav = (mTexture != NULL) ? mTexture->GetUAV(mMipLevel) : NULL;
	UINT initialCount = (UINT)-1;
	dxRenderDevice->mD3DDeviceContext->CSSetUnorderedAccessViews(mSlot, 1, &uav, &initialCount);
	if (uav != NULL)
		dxRenderDevice->mCSBoundUAVs |= 1u << mSlot;
}

void DXDispatchCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	auto ctx = dxRenderDevice->mD3DDeviceContext;
	ctx->CSSetShader(mShader->mD3DComputeShader, NULL, 0);
	ctx->Dispatch(mGroupsX, mGroupsY, mGroupsZ);
	ctx->CSSetShader(NULL, NULL, 0);

	// Leave nothing bound: the same resources are typically sampled by the draws that follow.
	if (dxRenderDevice->mCSBoundUAVs != 0)
	{
		ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = { NULL };
		UINT counts[D3D11_PS_CS_UAV_REGISTER_COUNT];
		for (int i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; i++)
			counts[i] = (UINT)-1;
		int maxSlot = 0;
		for (int i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; i++)
			if ((dxRenderDevice->mCSBoundUAVs & (1u << i)) != 0)
				maxSlot = i;
		ctx->CSSetUnorderedAccessViews(0, maxSlot + 1, nullUAVs, counts);
		dxRenderDevice->mCSBoundUAVs = 0;
	}
	if (dxRenderDevice->mCSBoundSRVs != 0)
	{
		ID3D11ShaderResourceView* nullSRVs[32] = { NULL };
		int maxSlot = 0;
		for (int i = 0; i < 32; i++)
			if ((dxRenderDevice->mCSBoundSRVs & (1u << i)) != 0)
				maxSlot = i;
		ctx->CSSetShaderResources(0, maxSlot + 1, nullSRVs);
		dxRenderDevice->mCSBoundSRVs = 0;
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
	// A multisampled backbuffer only works with the blt-model DISCARD swap effect (Present resolves
	// it implicitly) -- a FLIP_DISCARD migration would need an explicit offscreen MSAA target +
	// ResolveTo instead.
	int msaaSamples = ValidateSampleCount(mDXRenderDevice->mD3DDevice, DXGI_FORMAT_R8G8B8A8_UNORM,
		mDXRenderDevice->mWindowMsaaSampleCount);

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = mWidth;
	swapChainDesc.BufferDesc.Height = mHeight;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = mHWnd;
	swapChainDesc.SampleDesc.Count = msaaSamples;
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
	descDepth.Format = DXGI_FORMAT_D32_FLOAT;
	descDepth.SampleDesc.Count = msaaSamples;
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

		mDXRenderDevice->mCurD3DRTV = mD3DRenderTargetView;
		mDXRenderDevice->mCurD3DDSV = mD3DDepthStencilView;
		mDXRenderDevice->mD3DDeviceContext->OMSetRenderTargets(1, &mD3DRenderTargetView, mD3DDepthStencilView);
		mDXRenderDevice->mD3DDeviceContext->RSSetViewports(1, &viewPort);
	}

	if (!mHasBeenDrawnTo)
	{
		//mRenderDevice->mD3DDevice->ClearRenderTargetView(mD3DRenderTargetView, D3DXVECTOR4(rand() / (float) RAND_MAX, 0, 1, 0));
		float bgColor[4] = {0, 0, 0, 0};
		mDXRenderDevice->mD3DDeviceContext->ClearRenderTargetView(mD3DRenderTargetView, bgColor);
		// Reverse-Z: the window's scene depth clears to the far plane at 0.
		mDXRenderDevice->mD3DDeviceContext->ClearDepthStencilView(mD3DDepthStencilView, D3D11_CLEAR_DEPTH/*|D3D11_CLEAR_STENCIL*/, 0.0f, 0);
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
	
	if ((rect.right <= rect.left) || (rect.bottom <= rect.top))
	{
		if (mWidth <= 0)
		{
			// Defaults to avoid DX init failure
			mWidth = 16;
			mHeight = 16;
		}
		return;
	}

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

		// ResizeBuffers keeps the swapchain's original SampleDesc; the depth buffer has to match it.
		int msaaSamples = ValidateSampleCount(mDXRenderDevice->mD3DDevice, DXGI_FORMAT_R8G8B8A8_UNORM,
			mDXRenderDevice->mWindowMsaaSampleCount);

		D3D11_TEXTURE2D_DESC descDepth;
		ZeroMemory(&descDepth, sizeof(descDepth));
		descDepth.Width = mWidth;
		descDepth.Height = mHeight;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D32_FLOAT;
		descDepth.SampleDesc.Count = msaaSamples;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		descDepth.CPUAccessFlags = 0;
		descDepth.MiscFlags = 0;
		CheckDXResult(mDXRenderDevice->mD3DDevice->CreateTexture2D(&descDepth, NULL, &mD3DDepthBuffer));

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
	BP_ZONE("DXRenderWindow::Present");
	// Under external pacing our own vblank must never block the paced loop
	bool useVSync = (mWindow->mFlags & BFWINDOW_VSYNC) && (gBFApp != NULL) && (!gBFApp->mExternalPacingActive);
	HRESULT hr = mDXSwapChain->Present(useVSync ? 1 : 0, 0);

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
	mD3DDeviceContext1 = NULL;
	mNeedsReinitNative = false;
	mMatrix2DBuffer = NULL;
	mCurD3DRTV = NULL;
	mCurD3DDSV = NULL;
	mCSBoundSRVs = 0;
	mCSBoundUAVs = 0;
	mInstIotaBuffer = NULL;
	mInstIotaCount = 0;
	mGpuTimerWriteIdx = 0;
	mGpuTimerCurTag = 0;
	mGpuTimerEnabled = false;
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
	mD3DDeviceContext1 = NULL;
	mD3DDeviceContext->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&mD3DDeviceContext1);

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
	rasterizerState.MultisampleEnable = true;
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

	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DXCHECK(mD3DDevice->CreateSamplerState(&sampDesc, &mD3DNearestSamplerState));

	// Shadow-map comparison sampler (SampleCmp in HLSL): each fetch compares the reference depth
	// against the 4 neighboring texels and bilinearly blends the pass/fail results -- hardware PCF.
	// LESS_EQUAL passes ("lit") where ref <= stored depth. Permanently bound at sampler slot 1;
	// slot 0 stays the per-RenderState sampler (see PhysSetRenderState).
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DXCHECK(mD3DDevice->CreateSamplerState(&sampDesc, &mD3DShadowSamplerState));
	mD3DDeviceContext->PSSetSamplers(1, 1, &mD3DShadowSamplerState);

	// Trilinear (mip-interpolating) sampler, permanently at slot 2 -- for mipped atlases sampled with
	// explicit gradients (decals).
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DXCHECK(mD3DDevice->CreateSamplerState(&sampDesc, &mD3DTrilinearSamplerState));
	mD3DDeviceContext->PSSetSamplers(2, 1, &mD3DTrilinearSamplerState);

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

DXGpuTimerFrame::DXGpuTimerFrame()
{
	mDisjoint = NULL;
	mSpanCount = 0;
	mFrameId = 0;
	mOpen = false;
	mPending = false;
}

DXGpuTimerFrame::~DXGpuTimerFrame()
{
	ReleaseNative();
}

void DXGpuTimerFrame::ReleaseNative()
{
	if (mDisjoint != NULL)
		mDisjoint->Release();
	mDisjoint = NULL;
	for (auto query : mBeginQueries)
		query->Release();
	for (auto query : mEndQueries)
		query->Release();
	mBeginQueries.Clear();
	mEndQueries.Clear();
	mTags.Clear();
	mSpanCount = 0;
	mOpen = false;
	mPending = false;
}

void DXRenderDevice::GpuTimerSetEnabled(bool enabled)
{
	if (mGpuTimerEnabled == enabled)
		return;
	mGpuTimerEnabled = enabled;
	if (!enabled)
	{
		// In-flight frames would never be collected; drop them (and their queries) outright.
		for (int i = 0; i < DX_GPUTIMER_FRAMES; i++)
			mGpuTimerFrames[i].ReleaseNative();
		mGpuTimerWriteIdx = 0;
	}
}

ID3D11Query* DXRenderDevice::GetTimestampQuery(Array<ID3D11Query*>& queries, int idx)
{
	while ((int)queries.size() <= idx)
	{
		D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP, 0 };
		ID3D11Query* query = NULL;
		if (FAILED(mD3DDevice->CreateQuery(&desc, &query)))
			return NULL;
		queries.push_back(query);
	}
	return queries[idx];
}

bool DXRenderDevice::GpuTimerBeginFrame(int64 frameId)
{
	if (!mGpuTimerEnabled)
		return false;
	DXGpuTimerFrame& frame = mGpuTimerFrames[mGpuTimerWriteIdx];
	if (frame.mPending)
		return false; // the ring is full of frames the GPU hasn't finished -- skip timing this one
	if (frame.mDisjoint == NULL)
	{
		D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		if (FAILED(mD3DDevice->CreateQuery(&desc, &frame.mDisjoint)))
			return false;
	}
	frame.mFrameId = frameId;
	frame.mSpanCount = 0;
	frame.mOpen = true;
	mD3DDeviceContext->Begin(frame.mDisjoint);
	return true;
}

void DXRenderDevice::GpuTimerSetTag(int tag)
{
	mGpuTimerCurTag = tag;
}

int DXRenderDevice::GpuTimerSpanBegin()
{
	if (!mGpuTimerEnabled)
		return -1;
	DXGpuTimerFrame& frame = mGpuTimerFrames[mGpuTimerWriteIdx];
	if ((!frame.mOpen) || (frame.mSpanCount >= DX_GPUTIMER_MAX_SPANS))
		return -1;
	int idx = frame.mSpanCount;
	ID3D11Query* beginQuery = GetTimestampQuery(frame.mBeginQueries, idx);
	if ((beginQuery == NULL) || (GetTimestampQuery(frame.mEndQueries, idx) == NULL))
		return -1;
	while ((int)frame.mTags.size() <= idx)
		frame.mTags.push_back(0);
	frame.mTags[idx] = mGpuTimerCurTag;
	frame.mSpanCount = idx + 1;
	mD3DDeviceContext->End(beginQuery);
	return idx;
}

void DXRenderDevice::GpuTimerSpanEnd(int spanId)
{
	DXGpuTimerFrame& frame = mGpuTimerFrames[mGpuTimerWriteIdx];
	if ((!frame.mOpen) || (spanId < 0) || (spanId >= (int)frame.mEndQueries.size()))
		return;
	mD3DDeviceContext->End(frame.mEndQueries[spanId]);
}

void DXRenderDevice::GpuTimerEndFrame()
{
	DXGpuTimerFrame& frame = mGpuTimerFrames[mGpuTimerWriteIdx];
	if (!frame.mOpen)
		return;
	mD3DDeviceContext->End(frame.mDisjoint);
	frame.mOpen = false;
	frame.mPending = true;
	mGpuTimerWriteIdx = (mGpuTimerWriteIdx + 1) % DX_GPUTIMER_FRAMES;
}

int DXRenderDevice::GpuTimerFetch(int64* outFrameId, GpuTimerSpan* outSpans, int maxSpans)
{
	// Oldest first, so results come back in frame order.
	for (int i = 1; i <= DX_GPUTIMER_FRAMES; i++)
	{
		DXGpuTimerFrame& frame = mGpuTimerFrames[(mGpuTimerWriteIdx + i) % DX_GPUTIMER_FRAMES];
		if (!frame.mPending)
			continue;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
		HRESULT hr = mD3DDeviceContext->GetData(frame.mDisjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr != S_OK)
			return -1; // not ready; a later frame can't be ready before this one either

		frame.mPending = false;
		*outFrameId = frame.mFrameId;
		if ((disjoint.Disjoint) || (disjoint.Frequency == 0))
			return 0; // the clock jumped (power state change) -- this frame's timings are meaningless

		int count = 0;
		for (int spanIdx = 0; (spanIdx < frame.mSpanCount) && (count < maxSpans); spanIdx++)
		{
			uint64 beginTick = 0;
			uint64 endTick = 0;
			if (mD3DDeviceContext->GetData(frame.mBeginQueries[spanIdx], &beginTick, sizeof(beginTick), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			if (mD3DDeviceContext->GetData(frame.mEndQueries[spanIdx], &endTick, sizeof(endTick), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			if (endTick <= beginTick)
				continue;
			outSpans[count].mTag = frame.mTags[spanIdx];
			outSpans[count].mNanos = (int64)((endTick - beginTick) * 1000000000ULL / disjoint.Frequency);
			count++;
		}
		return count;
	}
	return -1;
}

void DXRenderDevice::ReleaseNative()
{
	for (int i = 0; i < DX_GPUTIMER_FRAMES; i++)
		mGpuTimerFrames[i].ReleaseNative();
	mD3DVertexBuffer->Release();
	mD3DVertexBuffer = NULL;
	if (mInstIotaBuffer != NULL)
		mInstIotaBuffer->Release();
	mInstIotaBuffer = NULL;
	mInstIotaCount = 0;
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
	mD3DNearestSamplerState->Release();
	mD3DNearestSamplerState = NULL;
	mD3DShadowSamplerState->Release();
	mD3DShadowSamplerState = NULL;
	mD3DTrilinearSamplerState->Release();
	mD3DTrilinearSamplerState = NULL;
	if (mD3DDeviceContext1 != NULL)
		mD3DDeviceContext1->Release();
	mD3DDeviceContext1 = NULL;
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

	String pathEx = fileName;
	if ((flags & TextureFlag_Additive) != 0)
		pathEx += ":add";

	DXTexture* aTexture = NULL;
	if ((!fileName.StartsWith('@')) && (mTextureMap.TryGetValue(pathEx, &aTexture)))
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
		aTexture->mPath = pathEx;
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

	bool wantMipmaps = (flags & TextureFlag_Mipmaps) != 0;

	int aWidth = imageData->mWidth;
	int aHeight = imageData->mHeight;

	// Create the target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = aWidth;
	desc.Height = aHeight;
	desc.ArraySize = 1;
	desc.Format = ((flags & TextureFlag_Srgb) != 0) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;

	//OutputDebugStrF("Creating texture\n");

	ID3D11Texture2D* d3DTexture = NULL;

	if (wantMipmaps)
	{
		// GenerateMips requires the texture be created empty (MipLevels=0 for a full chain, bound
		// as both SRV and RT) and populated afterward -- can't supply initial subresource data here.
		desc.MipLevels = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		DXCHECK(mD3DDevice->CreateTexture2D(&desc, NULL, &d3DTexture));
		mD3DDeviceContext->UpdateSubresource(d3DTexture, 0, NULL, imageData->mBits, aWidth * 4, 0);
	}
	else
	{
		D3D11_SUBRESOURCE_DATA resData;
		resData.pSysMem = imageData->mBits;
		resData.SysMemPitch = aWidth * 4;
		resData.SysMemSlicePitch = aWidth * aHeight * 4;

		desc.MipLevels = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		DXCHECK(mD3DDevice->CreateTexture2D(&desc, &resData, &d3DTexture));
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = wantMipmaps ? -1 : 1;

	DXCHECK(mD3DDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

	if (wantMipmaps)
		mD3DDeviceContext->GenerateMips(d3DShaderResourceView);

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

void DXRenderDevice::ReleaseShader(Shader* shader)
{
	mShaders.Remove((DXShader*)shader);
	delete shader;
}

ComputeShader* DXRenderDevice::LoadComputeShader(const StringImpl& fileName, const StringImpl& entry)
{
	DXComputeShader* shader = new DXComputeShader();
	shader->mRenderDevice = this;
	shader->mSrcPath = fileName;
	shader->mEntry = entry;
	if (!shader->Load())
	{
		shader->mRenderDevice = NULL;
		delete shader;
		return NULL;
	}
	mComputeShaders.Add(shader);
	return shader;
}

void DXRenderDevice::ReleaseComputeShader(ComputeShader* shader)
{
	delete shader;
}

void DXRenderDevice::SetRenderState(RenderState* renderState)
{
	mCurRenderState = renderState;
}

Texture* DXRenderDevice::CreateRenderTarget(int width, int height, int flags, int sampleCount)
{
	bool destAlpha = (flags & 1) != 0;
	bool makeShared = (flags & 2) != 0;
	bool highPrecision = (flags & 4) != 0;
	bool r8 = (flags & 8) != 0;
	bool f16 = (flags & 0x10) != 0;
	bool mipmaps = (flags & 0x20) != 0;
	bool rg8 = (flags & 0x40) != 0;
	bool r16f = (flags & 0x80) != 0;
	bool r32u = (flags & 0x100) != 0;
	bool unorderedAccess = (flags & 0x200) != 0;

	// D3D11 shared resources can't be multisampled -- render into a private MSAA target and
	// ResolveTo a shared one instead.
	BF_ASSERT(!(makeShared && (sampleCount > 1)));
	BF_ASSERT(!(mipmaps && ((sampleCount > 1) || makeShared)));
	BF_ASSERT(!(unorderedAccess && ((sampleCount > 1) || makeShared)));

	ID3D11ShaderResourceView* d3DShaderResourceView = NULL;

	DXGI_FORMAT format = highPrecision ? DXGI_FORMAT_R32_FLOAT : r8 ? DXGI_FORMAT_R8_UNORM :
		f16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : rg8 ? DXGI_FORMAT_R8G8_UNORM :
		r16f ? DXGI_FORMAT_R16_FLOAT : r32u ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
	int samples = ValidateSampleCount(mD3DDevice, format, sampleCount);

	// Create the render target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = mipmaps ? 0 : 1; // 0 = full chain, filled on demand by GenerateMips
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = samples;
	desc.SampleDesc.Quality = 0;

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0; //D3D11_CPU_ACCESS_WRITE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	if (unorderedAccess)
		desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

	if (makeShared)
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	if (mipmaps)
		desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

	ID3D11Texture2D* d3DTexture = NULL;
	DXCHECK(mD3DDevice->CreateTexture2D(&desc, NULL, &d3DTexture));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = mipmaps ? -1 : 1;

	// An MSAA texture can't be sampled as a plain Texture2D -- callers never should (ResolveTo a
	// single-sample target first), but the view still has to be creatable.
	if (samples > 1)
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
	aRenderTarget->mD3DFormat = format;
	aRenderTarget->mSampleCount = samples;
	if (makeShared)
		d3DTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&aRenderTarget->mD3DKeyedMutex);
	if (unorderedAccess)
		DXCHECK(mD3DDevice->CreateUnorderedAccessView(d3DTexture, NULL, &aRenderTarget->mD3DUAV));
	aRenderTarget->AddRef();

	// Typeless so GetDepthBits can staging-copy it and CreateDepthRef can view it; stencil is
	// unused engine-wide.
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_R32_TYPELESS;
	descDepth.SampleDesc.Count = samples;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL | ((samples == 1) ? D3D11_BIND_SHADER_RESOURCE : 0);
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	mD3DDevice->CreateTexture2D(&descDepth, NULL, &aRenderTarget->mD3DDepthBuffer);

	// A typeless resource can't take a NULL-desc view.
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	ZeroMemory(&dsvDesc, sizeof(dsvDesc));
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = (samples > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
	DXCHECK(mD3DDevice->CreateDepthStencilView(aRenderTarget->mD3DDepthBuffer, &dsvDesc, &aRenderTarget->mD3DDepthStencilView));

	return aRenderTarget;
}

// Depth-only target (shadow maps): the depth buffer is the only plane -- mD3DTexture and
// mD3DRenderTargetView stay NULL, mD3DResourceView views the depth itself, so SetTexture binds it
// for sampling (incl. comparison/PCF) unchanged. Draw into it with a DisableRenderTarget +
// DisablePixelShader render state.
Texture* DXRenderDevice::CreateDepthTarget(int width, int height, bool is16Bit)
{
	DXTexture* aRenderTarget = new DXTexture();
	aRenderTarget->mWidth = width;
	aRenderTarget->mHeight = height;
	aRenderTarget->mRenderDevice = this;
	aRenderTarget->mD3DFormat = is16Bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R32_FLOAT;
	aRenderTarget->mStandardDepthClear = true;
	aRenderTarget->AddRef();

	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = is16Bit ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	DXCHECK(mD3DDevice->CreateTexture2D(&descDepth, NULL, &aRenderTarget->mD3DDepthBuffer));

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	ZeroMemory(&dsvDesc, sizeof(dsvDesc));
	dsvDesc.Format = is16Bit ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DXCHECK(mD3DDevice->CreateDepthStencilView(aRenderTarget->mD3DDepthBuffer, &dsvDesc, &aRenderTarget->mD3DDepthStencilView));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	ZeroMemory(&srDesc, sizeof(srDesc));
	srDesc.Format = is16Bit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R32_FLOAT;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;
	DXCHECK(mD3DDevice->CreateShaderResourceView(aRenderTarget->mD3DDepthBuffer, &srDesc, &aRenderTarget->mD3DResourceView));

	return aRenderTarget;
}

Texture* DXRenderDevice::CreateStructuredBuffer(int stride, int count, int flags)
{
	BF_ASSERT((stride > 0) && (stride % 4 == 0) && (count > 0));
	bool gpuWritable = (flags & 1) != 0;
	bool defaultUsage = gpuWritable || ((flags & 2) != 0);

	DXStructuredBuffer* buffer = new DXStructuredBuffer();
	buffer->mWidth = count;
	buffer->mHeight = 1;
	buffer->mStride = stride;
	buffer->mGpuWritable = gpuWritable;
	buffer->mDefaultUsage = defaultUsage;
	buffer->mRenderDevice = this;
	buffer->AddRef();

	D3D11_BUFFER_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Usage = defaultUsage ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;
	desc.ByteWidth = stride * count;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (gpuWritable ? D3D11_BIND_UNORDERED_ACCESS : 0);
	desc.CPUAccessFlags = defaultUsage ? 0 : D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = stride;
	DXCHECK(mD3DDevice->CreateBuffer(&desc, NULL, &buffer->mD3DBuffer));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	ZeroMemory(&srDesc, sizeof(srDesc));
	srDesc.Format = DXGI_FORMAT_UNKNOWN;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srDesc.Buffer.FirstElement = 0;
	srDesc.Buffer.NumElements = count;
	DXCHECK(mD3DDevice->CreateShaderResourceView(buffer->mD3DBuffer, &srDesc, &buffer->mD3DResourceView));

	if (gpuWritable)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		ZeroMemory(&uavDesc, sizeof(uavDesc));
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = count;
		DXCHECK(mD3DDevice->CreateUnorderedAccessView(buffer->mD3DBuffer, &uavDesc, &buffer->mD3DUAV));
	}

	return buffer;
}

// Format flags as CreateRenderTarget (4 R32F, 8 R8, 0x10 RGBA16F, 0x40 RG8, 0x80 R16F, 0x100 R32_UINT,
// else RGBA8); 0x20 = full mip chain (GenerateMips-able, so also render-target bindable).
Texture* DXRenderDevice::CreateTexture3D(int width, int height, int depth, int flags)
{
	BF_ASSERT((width > 0) && (height > 0) && (depth > 0));
	bool highPrecision = (flags & 4) != 0;
	bool r8 = (flags & 8) != 0;
	bool f16 = (flags & 0x10) != 0;
	bool mipmaps = (flags & 0x20) != 0;
	bool rg8 = (flags & 0x40) != 0;
	bool r16f = (flags & 0x80) != 0;
	bool r32u = (flags & 0x100) != 0;
	BF_ASSERT(!(mipmaps && r32u)); // integer formats can't be mip-filtered

	DXGI_FORMAT format = highPrecision ? DXGI_FORMAT_R32_FLOAT : r8 ? DXGI_FORMAT_R8_UNORM :
		f16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : rg8 ? DXGI_FORMAT_R8G8_UNORM :
		r16f ? DXGI_FORMAT_R16_FLOAT : r32u ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
	int bytesPerTexel = highPrecision ? 4 : r8 ? 1 : f16 ? 8 : rg8 ? 2 : r16f ? 2 : r32u ? 4 : 4;

	int mipLevels = 1;
	if (mipmaps)
	{
		int size = BF_MAX(BF_MAX(width, height), depth);
		while (((size >> mipLevels) >= 1) && (mipLevels < DXTexture3D::cMaxMips))
			mipLevels++;
	}

	D3D11_TEXTURE3D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	desc.MipLevels = mipLevels;
	desc.Format = format;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	if (mipmaps)
	{
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	}

	DXTexture3D* tex = new DXTexture3D();
	tex->mWidth = width;
	tex->mHeight = height;
	tex->mDepth = depth;
	tex->mMipLevels = mipLevels;
	tex->mBytesPerTexel = bytesPerTexel;
	tex->mD3DFormat = format;
	tex->mRenderDevice = this;
	tex->AddRef();
	DXCHECK(mD3DDevice->CreateTexture3D(&desc, NULL, &tex->mD3DTexture3D));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	ZeroMemory(&srDesc, sizeof(srDesc));
	srDesc.Format = format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
	srDesc.Texture3D.MostDetailedMip = 0;
	srDesc.Texture3D.MipLevels = mipLevels;
	DXCHECK(mD3DDevice->CreateShaderResourceView(tex->mD3DTexture3D, &srDesc, &tex->mD3DResourceView));

	for (int mip = 0; mip < mipLevels; mip++)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		ZeroMemory(&uavDesc, sizeof(uavDesc));
		uavDesc.Format = format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
		uavDesc.Texture3D.MipSlice = mip;
		uavDesc.Texture3D.FirstWSlice = 0;
		uavDesc.Texture3D.WSize = -1;
		DXCHECK(mD3DDevice->CreateUnorderedAccessView(tex->mD3DTexture3D, &uavDesc, &tex->mD3DUAVs[mip]));
	}

	return tex;
}

Texture* DXRenderDevice::OpenSharedRenderTarget(void* handle, int width, int height)
{
	ID3D11Texture2D* sharedTex = NULL;
	HRESULT hr = mD3DDevice->OpenSharedResource((HANDLE)handle, __uuidof(ID3D11Texture2D), (void**)&sharedTex);
	if (FAILED(hr))
		return NULL;

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	ID3D11ShaderResourceView* resourceView = NULL;
	DXCHECK(mD3DDevice->CreateShaderResourceView(sharedTex, &srDesc, &resourceView));

	ID3D11RenderTargetView* rtView = NULL;
	DXCHECK(mD3DDevice->CreateRenderTargetView(sharedTex, NULL, &rtView));

	IDXGIKeyedMutex* keyedMutex = NULL;
	sharedTex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&keyedMutex);

	DXTexture* texture = new DXTexture();
	texture->mWidth = width;
	texture->mHeight = height;
	texture->mRenderDevice = this;
	texture->mD3DTexture = sharedTex;
	texture->mD3DResourceView = resourceView;
	texture->mD3DRenderTargetView = rtView;
	texture->mD3DKeyedMutex = keyedMutex;
	texture->AddRef();

	int sampleQuality = 0;

	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.SampleDesc.Quality = sampleQuality;
	descDepth.Format = DXGI_FORMAT_D32_FLOAT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	mD3DDevice->CreateTexture2D(&descDepth, NULL, &texture->mD3DDepthBuffer);

	DXCHECK(mD3DDevice->CreateDepthStencilView(texture->mD3DDepthBuffer, NULL, &texture->mD3DDepthStencilView));

	return texture;
}

//#include <dxgi1_2.h>
//#include <d3d11_1.h>
//#include "gfx/Texture.h"
//#include "BFApp.h"

#endif