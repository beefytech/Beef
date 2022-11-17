#include "GLRenderDevice.h"
#include "SdlBFApp.h"
#include "BFWindow.h"
#include "img/ImageData.h"
#include "util/PerfTimer.h"
#include <SDL2/SDL_video.h>

USING_NS_BF;

#ifndef NOT_IMPL
#define NOT_IMPL throw "Not implemented"
#endif

//#pragma comment(lib, "SDL2.lib")

#ifdef _WIN32
#ifdef BF_PLATFORM_OPENGL_ES2
#pragma comment(lib, "libEGL.lib")
#pragma comment(lib, "libGLESv2.lib")
#else
#pragma comment(lib, "opengl32.lib")
#endif
#endif

/*#if SDL_VIDEO_DRIVER_WINDOWS
#define APIENTRYP __stdcall *
#elif defined BF_PLATFORM_OPENGL_ES2
#define APIENTRYP *
#endif*/


#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03

#if defined BF_PLATFORM_OPENGL_ES2
#define APIENTRYP BF_CALLTYPE *
#endif

extern void* (SDLCALL* bf_SDL_GL_GetProcAddress)(const char* proc);
extern void (SDLCALL* bf_SDL_GetWindowSize)(SDL_Window* window, int* w, int* h);
extern void (SDLCALL* bf_SDL_GL_SwapWindow)(SDL_Window* window);

typedef void (APIENTRYP GL_DEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);

static void (APIENTRYP bf_glDebugMessageCallback)(GL_DEBUGPROC callback, const void* userParam);
static void (APIENTRYP bf_glActiveTexture)(GLenum texture);
static void (APIENTRYP bf_glGenVertexArrays)(GLsizei n, GLuint* buffers);
static void (APIENTRYP bf_glBindVertexArray)(GLenum target);
static void (APIENTRYP bf_glGenBuffers)(GLsizei n, GLuint *buffers);
static void (APIENTRYP bf_glBindBuffer)(GLenum target, GLuint buffer);
static void (APIENTRYP bf_glBufferData)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
static void (APIENTRYP bf_glDeleteBuffers)(GLsizei n, const GLuint *buffers);
static GLvoid* (APIENTRYP bf_glMapBuffer)(GLenum target, GLenum access);
static GLboolean (APIENTRYP bf_glUnmapBuffer)(GLenum target);
static void (APIENTRYP bf_glGetBufferParameteriv)(GLenum target, GLenum pname, GLint *params);

static void (APIENTRYP bf_glColorPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glDrawArrays)(GLenum mode, GLint first, GLsizei count);
static void (APIENTRYP bf_glEdgeFlagPointer)(GLsizei stride, const GLboolean *pointer);
static void (APIENTRYP bf_glGetPointerv)(GLenum pname, GLvoid* *params);
static void (APIENTRYP bf_glIndexPointer)(GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glNormalPointer)(GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glTexCoordPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glVertexPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
static void (APIENTRYP bf_glDrawElements)(GLenum mode, GLsizei count, GLenum type, const GLvoid * indices);

static void (APIENTRYP bf_glAttachShader)(GLuint program, GLuint shader);
static void (APIENTRYP bf_glBindAttribLocation)(GLuint program, GLuint index, const GLchar *name);
static void (APIENTRYP bf_glCompileShader)(GLuint shader);
static GLuint (APIENTRYP bf_glCreateProgram)(void);
static GLuint (APIENTRYP bf_glCreateShader)(GLenum type);
static void (APIENTRYP bf_glDeleteProgram)(GLuint program);
static void (APIENTRYP bf_glDeleteShader)(GLuint shader);
static void (APIENTRYP bf_glDetachShader)(GLuint program, GLuint shader);
static void (APIENTRYP bf_glDisableVertexAttribArray)(GLuint index);
static void (APIENTRYP bf_glEnableVertexAttribArray)(GLuint index);
static void (APIENTRYP bf_glGetActiveAttrib)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
static void (APIENTRYP bf_glGetActiveUniform)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
static void (APIENTRYP bf_glGetAttachedShaders)(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *obj);
static GLint (APIENTRYP bf_glGetAttribLocation)(GLuint program, const GLchar *name);
static void (APIENTRYP bf_glGetProgramiv)(GLuint program, GLenum pname, GLint *params);
static void (APIENTRYP bf_glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (APIENTRYP bf_glGetShaderiv)(GLuint shader, GLenum pname, GLint *params);
static void (APIENTRYP bf_glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static void (APIENTRYP bf_glGetShaderSource)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
static GLint (APIENTRYP bf_glGetUniformLocation)(GLuint program, const GLchar *name);
static void (APIENTRYP bf_glGetUniformfv)(GLuint program, GLint location, GLfloat *params);
static void (APIENTRYP bf_glGetUniformiv)(GLuint program, GLint location, GLint *params);
static void (APIENTRYP bf_glGetVertexAttribfv)(GLuint index, GLenum pname, GLfloat *params);
static void (APIENTRYP bf_glGetVertexAttribiv)(GLuint index, GLenum pname, GLint *params);
static void (APIENTRYP bf_glGetVertexAttribPointerv)(GLuint index, GLenum pname, GLvoid* *pointer);
static GLboolean (APIENTRYP bf_glIsProgram)(GLuint program);
static GLboolean (APIENTRYP bf_glIsShader)(GLuint shader);
static void (APIENTRYP bf_glLinkProgram)(GLuint program);
static void (APIENTRYP bf_glShaderSource)(GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);
static void (APIENTRYP bf_glUseProgram)(GLuint program);
static void (APIENTRYP bf_glUniform1f)(GLint location, GLfloat v0);
static void (APIENTRYP bf_glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
static void (APIENTRYP bf_glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
static void (APIENTRYP bf_glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
static void (APIENTRYP bf_glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static void (APIENTRYP bf_glGetObjectParameterivARB)(GLint obj, GLenum pname, GLint *params);

static void (APIENTRYP bf_glCompressedTexImage2D)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
static  void (APIENTRYP bf_glClientActiveTexture)(GLenum texture);

#if !defined BF_PLATFORM_OPENGL_ES2
static void (APIENTRYP bf_glGetVertexAttribdv)(GLuint index, GLenum pname, GLdouble *params);
#endif

///

static int GetPowerOfTwo(int input)
{
	int value = 1;
	while (value < input)
		value <<= 1;
	return value;
}

#define GLFAILED(check) ((hr = (check)) != 0)
#define GLCHECK(check) if ((check) != 0) BF_FATAL("GL call failed")

static void CreateOrthographicOffCenter(float left, float right, float bottom, float top, float zNear, float zFar, float matrix[4][4])
{
	memset(matrix, 0, sizeof(float) * 4 * 4);

	float invRL = 1.0f / (right - left);
	float invTB = 1.0f / (top - bottom);
	float invFN = 1.0f / (zFar - zNear);

	matrix[0][0] = 2.0f * invRL;
	matrix[1][1] = 2.0f * invTB;
	matrix[2][2] = -2.0f * invFN;
	matrix[3][3] = 1.0f;

	matrix[3][0] = -(right + left) * invRL;
	matrix[3][1] = -(top + bottom) * invTB;
	matrix[3][2] = -(zFar + zNear) * invFN;
}

GLShaderParam::GLShaderParam()
{
	mGLVariable = 0;
}

GLShaderParam::~GLShaderParam()
{
}

void GLShaderParam::SetTexture(Texture* texture)
{
	NOT_IMPL;
	//GLTexture* dXTexture = (GLTexture*) texture;
	//GLCHECK(mGLVariable->AsShaderResource()->SetResource(dXTexture->mGLTexture));
}

void GLShaderParam::SetFloat4(float x, float y, float z, float w)
{
	NOT_IMPL;
	//float v[4] = {x, y, z, w};
	//GLCHECK(mGLVariable->AsVector()->SetFloatVector(v));
}

///

GLShader::GLShader()
{
}

GLShader::~GLShader()
{
	for (auto paramKV : mParamsMap)
	{
		delete paramKV.mValue;
	}
}

ShaderParam* GLShader::GetShaderParam(const StringImpl& name)
{
	NOT_IMPL;
	return NULL;
}

///

GLTexture::GLTexture()
{
	mGLTexture = 0;
	mGLTexture2 = 0;
	//mGLRenderTargetView = NULL;
	mRenderDevice = NULL;
	mImageData = NULL;
}

GLTexture::~GLTexture()
{
	if (mImageData != NULL)
		mImageData->Deref();
	//if (mGLTexture != NULL)
		//mGLTexture->Release();
}

void GLTexture::PhysSetAsTarget()
{
	NOT_IMPL;
}

void GLTexture::Blt(ImageData* imageData, int x, int y)
{
	if (mImageData != NULL)
	{
		for (int row = 0; row < imageData->mHeight; row++)
		{
			memcpy(mImageData->mBits + (y + row) * mImageData->mWidth + x,
				imageData->mBits + row * imageData->mWidth, imageData->mWidth * 4);
		}
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D, mGLTexture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, imageData->mWidth, imageData->mHeight, GL_RGBA, GL_UNSIGNED_BYTE, imageData->mBits);
	}
}

///

GLDrawBatch::GLDrawBatch() : DrawBatch()
{

}

GLDrawBatch::~GLDrawBatch()
{
}

extern int gBFDrawBatchCount;

struct GLVertex3D
{
	float x, y, z;
	float u, v;
	uint32 color;
};

void GLDrawBatch::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if (mIdxIdx == 0)
		return;

    gBFDrawBatchCount++;

	GLRenderDevice* glRenderDevice = (GLRenderDevice*) gBFApp->mRenderDevice;
	GLShader* curShader = (GLShader*)mRenderState->mShader;

	if (glRenderDevice->mGLVAO == 0)
	{
		bf_glGenVertexArrays(1, &glRenderDevice->mGLVAO);
		bf_glBindVertexArray(glRenderDevice->mGLVAO);

		bf_glGenBuffers(1, &glRenderDevice->mGLVertexBuffer);
		bf_glGenBuffers(1, &glRenderDevice->mGLIndexBuffer);
	}

	auto glVertices = (GLVertex3D*)mVertices;

	bf_glBindBuffer(GL_ARRAY_BUFFER, glRenderDevice->mGLVertexBuffer);
	bf_glBufferData(GL_ARRAY_BUFFER, mVtxIdx * sizeof(GLVertex3D), mVertices, GL_STREAM_DRAW);

	bf_glEnableVertexAttribArray(curShader->mAttribPosition);
	bf_glVertexAttribPointer(curShader->mAttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(GLVertex3D), (void*)offsetof(GLVertex3D, x));
	bf_glEnableVertexAttribArray(curShader->mAttribTexCoord0);
	bf_glVertexAttribPointer(curShader->mAttribTexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex3D), (void*)offsetof(GLVertex3D, u));
	bf_glEnableVertexAttribArray(curShader->mAttribColor);
	bf_glVertexAttribPointer(curShader->mAttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLVertex3D), (void*)offsetof(GLVertex3D, color));

	if (mRenderState != renderDevice->mPhysRenderState)
		renderDevice->PhysSetRenderState(mRenderState);

	bf_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glRenderDevice->mGLIndexBuffer);
	bf_glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIdxIdx * sizeof(int16), mIndices, GL_STREAM_DRAW);
	bf_glDrawElements(GL_TRIANGLES, mIdxIdx, GL_UNSIGNED_SHORT, NULL);

    bf_glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, NULL, GL_STREAM_DRAW);
    bf_glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STREAM_DRAW);
}

GLDrawLayer::GLDrawLayer()
{
}

GLDrawLayer::~GLDrawLayer()
{
}

DrawBatch* GLDrawLayer::CreateDrawBatch()
{
	return new GLDrawBatch();
}


RenderCmd* GLDrawLayer::CreateSetTextureCmd(int textureIdx, Texture* texture)
{
	GLSetTextureCmd* setTextureCmd = AllocRenderCmd<GLSetTextureCmd>();
	setTextureCmd->mTextureIdx = textureIdx;
	setTextureCmd->mTexture = texture;
	return setTextureCmd;
}

void GLDrawLayer::SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size)
{
}

void GLRenderDevice::PhysSetRenderWindow(RenderWindow* renderWindow)
{
	mCurRenderTarget = renderWindow;
	mPhysRenderWindow = renderWindow;
	((GLRenderWindow*)renderWindow)->PhysSetAsTarget();
}

void GLRenderDevice::PhysSetRenderTarget(Texture* renderTarget)
{
	mCurRenderTarget = renderTarget;
	renderTarget->PhysSetAsTarget();
}

///

template <typename T>
static void BFGetGLProc(T& proc, const char* name)
{
	proc = (T)bf_SDL_GL_GetProcAddress(name);
}

#define BF_GET_GLPROC(name) BFGetGLProc(bf_##name, #name)

void GL_DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	NOP;
}

GLRenderWindow::GLRenderWindow(GLRenderDevice* renderDevice, SDL_Window* sdlWindow)
{
	if (bf_glGenBuffers == NULL)
	{
		BF_GET_GLPROC(glDebugMessageCallback);
		BF_GET_GLPROC(glActiveTexture);
		BF_GET_GLPROC(glGenVertexArrays);
		BF_GET_GLPROC(glBindVertexArray);
		BF_GET_GLPROC(glGenBuffers);
		BF_GET_GLPROC(glBindBuffer);
		BF_GET_GLPROC(glBufferData);
		BF_GET_GLPROC(glDeleteBuffers);
		BF_GET_GLPROC(glMapBuffer);
		BF_GET_GLPROC(glUnmapBuffer);
		BF_GET_GLPROC(glGetBufferParameteriv);

		BF_GET_GLPROC(glColorPointer);
		BF_GET_GLPROC(glDrawArrays);
		BF_GET_GLPROC(glEdgeFlagPointer);
		BF_GET_GLPROC(glGetPointerv);
		BF_GET_GLPROC(glIndexPointer);
		BF_GET_GLPROC(glNormalPointer);
		BF_GET_GLPROC(glTexCoordPointer);
		BF_GET_GLPROC(glVertexPointer);
		BF_GET_GLPROC(glDrawElements);
		BF_GET_GLPROC(glVertexAttribPointer);

		BF_GET_GLPROC(glAttachShader);
		BF_GET_GLPROC(glBindAttribLocation);
		BF_GET_GLPROC(glCompileShader);
		BF_GET_GLPROC(glCreateProgram);
		BF_GET_GLPROC(glCreateShader);
		BF_GET_GLPROC(glDeleteProgram);
		BF_GET_GLPROC(glDeleteShader);
		BF_GET_GLPROC(glDetachShader);
		BF_GET_GLPROC(glDisableVertexAttribArray);
		BF_GET_GLPROC(glEnableVertexAttribArray);
		BF_GET_GLPROC(glGetActiveAttrib);
		BF_GET_GLPROC(glGetActiveUniform);
		BF_GET_GLPROC(glGetAttachedShaders);
		BF_GET_GLPROC(glGetAttribLocation);
		BF_GET_GLPROC(glGetProgramiv);
		BF_GET_GLPROC(glGetProgramInfoLog);
		BF_GET_GLPROC(glGetShaderiv);
		BF_GET_GLPROC(glGetShaderInfoLog);
		BF_GET_GLPROC(glGetShaderSource);
		BF_GET_GLPROC(glGetUniformLocation);
		BF_GET_GLPROC(glGetUniformfv);
		BF_GET_GLPROC(glGetUniformiv);
		BF_GET_GLPROC(glGetVertexAttribfv);
		BF_GET_GLPROC(glGetVertexAttribiv);
		BF_GET_GLPROC(glGetVertexAttribPointerv);
		BF_GET_GLPROC(glIsProgram);
		BF_GET_GLPROC(glIsShader);
		BF_GET_GLPROC(glLinkProgram);
		BF_GET_GLPROC(glShaderSource);
		BF_GET_GLPROC(glUseProgram);
		BF_GET_GLPROC(glUniform1f);
		BF_GET_GLPROC(glUniform2f);
		BF_GET_GLPROC(glUniform3f);
		BF_GET_GLPROC(glUniform4f);
		BF_GET_GLPROC(glUniformMatrix4fv);
		BF_GET_GLPROC(glGetObjectParameterivARB);
		BF_GET_GLPROC(glCompressedTexImage2D);
		BF_GET_GLPROC(glClientActiveTexture);

#if !defined BF_PLATFORM_OPENGL_ES2
        BF_GET_GLPROC(glGetVertexAttribdv);
#endif
    }

	mSDLWindow = sdlWindow;
	mRenderDevice = renderDevice;
	Resized();

	//bf_glDebugMessageCallback(GL_DebugCallback, NULL);
}

GLRenderWindow::~GLRenderWindow()
{

}

void GLRenderWindow::PhysSetAsTarget()
{
	GLfloat matrix[4][4];
	CreateOrthographicOffCenter(0.0f, (float)mWidth, (float)mHeight, 0.0f, -100.0f, 100.0f, matrix);
    glViewport(0, 0, (GLsizei)mWidth, (GLsizei)mHeight);

	mHasBeenDrawnTo = true;
}

void GLRenderWindow::SetAsTarget()
{
	//TODO: Handle this more elegantly when we actually handle draw layers properly...
	//if (mRenderDevice->mCurRenderTarget != NULL)
		//mRenderDevice->mCurDrawLayer->Flush();

	mHasBeenTargeted = true;
	mRenderDevice->mCurRenderTarget = this;
}

void GLRenderWindow::Resized()
{
	mRenderDevice->mResizeCount++;
	mResizeNum = mRenderDevice->mResizeCount;

	bf_SDL_GetWindowSize(mSDLWindow, &mWidth, &mHeight);

	//NOT_IMPL;
	/*if (mGLSwapChain != NULL)
	{
		mGLRenderTargetView->Release();
		mGLBackBuffer->Release();
		GLCHECK(mGLSwapChain->ResizeBuffers(1, mWidth, mHeight, GLGI_FORMAT_R8G8B8A8_UNORM, 0));

		GLCHECK(mGLSwapChain->GetBuffer(0, __uuidof(IGL10Texture2D), (LPVOID*)&mGLBackBuffer));
		GLCHECK(mRenderDevice->mGLDevice->CreateRenderTargetView(mGLBackBuffer, NULL, &mGLRenderTargetView));
	}*/
}

void GLRenderWindow::Present()
{
	bf_SDL_GL_SwapWindow(mSDLWindow);
	//GLCHECK(mGLSwapChain->Present((mWindow->mFlags & BFWINDOW_VSYNC) ? 1 : 0, 0));
}

void GLRenderWindow::CopyBitsTo(uint32* dest, int width, int height)
{
	mCurDrawLayer->Flush();

	NOT_IMPL;
	/*GL10_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = 0;
	texDesc.Format = GLGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = GL10_USAGE_STAGING;
	texDesc.CPUAccessFlags = GL10_CPU_ACCESS_READ;

	IGL10Texture2D *texture;
	GLCHECK(mRenderDevice->mGLDevice->CreateTexture2D(&texDesc, 0, &texture));
	mRenderDevice->mGLDevice->CopyResource(texture, mGLBackBuffer);

	GL10_MAPPED_TEXTURE2D mapTex;
	GLCHECK(texture->Map(GL10CalcSubresource(0, 0, 1), GL10_MAP_READ, 0, &mapTex));
	uint8* srcPtr = (uint8*) mapTex.pData;
	uint8* destPtr = (uint8*) dest;
	for (int y = 0; y < height; y++)
	{
	memcpy(destPtr, srcPtr, width*sizeof(uint32));
	srcPtr += mapTex.RowPitch;
	destPtr += width * 4;
	}
	texture->Unmap(0);
	texture->Release();*/
}

///

GLRenderDevice::GLRenderDevice()
{
	//mGLDevice = NULL;
	mCurShader = NULL;
	mGLVAO = 0;
	mGLVertexBuffer = 0;
	mGLIndexBuffer = 0;
	mBlankTexture = 0;
    mFreeBatchHead = NULL;
}

GLRenderDevice::~GLRenderDevice()
{
}

bool GLRenderDevice::Init(BFApp* app)
{
	SdlBFApp* winApp = (SdlBFApp*) app;

	//RenderState* glRenderState;
	if (mDefaultRenderState == NULL)
	{
		auto dxRenderState = (RenderState*)CreateRenderState(NULL);

		mDefaultRenderState = dxRenderState;
		mDefaultRenderState->mDepthFunc = DepthFunc_Less;
		mDefaultRenderState->mWriteDepthBuffer = true;

		mPhysRenderState = mDefaultRenderState;
	}
	else
	{
		//glRenderState = (DXRenderState*)mDefaultRenderState;
		//glRenderState->ReinitNative();
	}

	///

	////Use GL10_CREATE_DEVICE_DEBUG for PIX
	//GLCHECK(GL10CreateDevice(NULL, GL10_DRIVER_TYPE_HARDWARE, NULL, GL10_CREATE_DEVICE_DEBUG, GL10_SDK_VERSION, &mGLDevice));
	////GLCHECK(GL10CreateDevice(NULL, GL10_DRIVER_TYPE_HARDWARE, NULL, 0, GL10_SDK_VERSION, &mGLDevice));

	//IGLGIDevice* pGLGIDevice = NULL;
	//GLCHECK(mGLDevice->QueryInterface(__uuidof(IGLGIDevice), reinterpret_cast<void**>(&pGLGIDevice)));

	//IGLGIAdapter* pGLGIAdapter = NULL;
	//GLCHECK(pGLGIDevice->GetParent(__uuidof(IGLGIAdapter), reinterpret_cast<void**>(&pGLGIAdapter)));

	//IGLGIFactory* pGLGIFactory = NULL;
	//GLCHECK(pGLGIAdapter->GetParent(__uuidof(IGLGIFactory), reinterpret_cast<void**>(&mGLGIFactory)));

	////set rasterizer
	//GL10_RASTERIZER_DESC rasterizerState;
	//rasterizerState.CullMode = GL10_CULL_NONE;
	//rasterizerState.FillMode = GL10_FILL_SOLID;
	//rasterizerState.FrontCounterClockwise = true;
 //   rasterizerState.DepthBias = false;
 //   rasterizerState.DepthBiasClamp = 0;
 //   rasterizerState.SlopeScaledDepthBias = 0;
 //   rasterizerState.DepthClipEnable = false;
 //   rasterizerState.ScissorEnable = true;
 //   //TODO:rasterizerState.MultisampleEnable = false;
	//rasterizerState.MultisampleEnable = true;
 //   rasterizerState.AntialiasedLineEnable = true;
	//
	//mGLDevice->CreateRasterizerState( &rasterizerState, &mGLRasterizerStateClipped);
	//
	//rasterizerState.ScissorEnable = false;
	//mGLDevice->CreateRasterizerState( &rasterizerState, &mGLRasterizerStateUnclipped);
	//mGLDevice->RSSetState(mGLRasterizerStateUnclipped);
	//
	//IGL10BlendState* g_pBlendState = NULL;

	//GL10_BLEND_DESC BlendState;
	//ZeroMemory(&BlendState, sizeof(GL10_BLEND_DESC));
	//BlendState.BlendEnable[0] = TRUE;
	////BlendState.SrcBlend = GL10_BLEND_SRC_ALPHA;
	//BlendState.SrcBlend = GL10_BLEND_ONE;

	//BlendState.DestBlend = GL10_BLEND_INV_SRC_ALPHA;
	//BlendState.BlendOp = GL10_BLEND_OP_ADD;
	//BlendState.SrcBlendAlpha = GL10_BLEND_ONE;
	//BlendState.DestBlendAlpha = GL10_BLEND_ONE;
	//BlendState.BlendOpAlpha = GL10_BLEND_OP_ADD;
	//BlendState.RenderTargetWriteMask[0] = GL10_COLOR_WRITE_ENABLE_ALL;
	//mGLDevice->CreateBlendState(&BlendState, &mGLNormalBlendState);

	//BlendState.DestBlend = GL10_BLEND_ONE;
	//mGLDevice->CreateBlendState(&BlendState, &mGLAdditiveBlendState);

	//PhysSetAdditive(false);

	return true;
}

void GLRenderDevice::FrameStart()
{
	mCurRenderTarget = NULL;
	mPhysRenderWindow = NULL;

	for (auto aRenderWindow : mRenderWindowList)
	{
		aRenderWindow->mHasBeenDrawnTo = false;
		aRenderWindow->mHasBeenTargeted = false;
	}
}

void GLRenderDevice::FrameEnd()
{
	for (auto aRenderWindow : mRenderWindowList)
	{
		if (aRenderWindow->mHasBeenTargeted)
		{
			PhysSetRenderWindow(aRenderWindow);
			PhysSetRenderState(mDefaultRenderState);

			for (auto drawLayer : aRenderWindow->mDrawLayerList)
			{
				drawLayer->Flush();
			}

			aRenderWindow->Present();
		}
	}
}

Texture* GLRenderDevice::LoadTexture(ImageData* imageData, int flags)
{
	imageData->mIsAdditive = (flags & TextureFlag_Additive) != 0;
	imageData->PremultiplyAlpha();

	//int w = power_of_two(imageData->mWidth);
	//int h = power_of_two(imageData->mHeight);
	GLTexture* glTexture = new GLTexture();
	glTexture->mRenderDevice = this;
	glTexture->mWidth = imageData->mWidth;
	glTexture->mHeight = imageData->mHeight;
	glTexture->mImageData = imageData;
	glTexture->AddRef();
	imageData->AddRef();

	return glTexture;
}

Shader* GLRenderDevice::LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition)
{
	GLShader* glShader = new GLShader();

	glShader->mVertexSize = sizeof(GLVertex3D);
	glShader->mGLVertexShader = bf_glCreateShader(GL_VERTEX_SHADER);
	glShader->mGLFragmentShader = bf_glCreateShader(GL_FRAGMENT_SHADER);

	GLint vertProgramLen = 0;
	GLint fragProgramLen = 0;

#ifdef BF_PLATFORM_OPENGL_ES2
	GLchar* vertProgram = (GLchar*)LoadBinaryData(fileName + "_es.vert", &vertProgramLen);
	GLchar* fragProgram = (GLchar*)LoadBinaryData(fileName + "_es.frag", &fragProgramLen);
#else
	GLchar* vertProgram = (GLchar*)LoadBinaryData(fileName + ".vert", &vertProgramLen);
	GLchar* fragProgram = (GLchar*)LoadBinaryData(fileName + ".frag", &fragProgramLen);
#endif

	if ((vertProgram == NULL) || (fragProgram == NULL))
	{
		delete vertProgram;
		delete fragProgram;
		return NULL;
	}

	int infoLogLen = 0;
	char infoLog[2048];

	bf_glShaderSource(glShader->mGLVertexShader, 1, (const GLchar**)&vertProgram, &vertProgramLen);
	bf_glCompileShader(glShader->mGLVertexShader);
	bf_glGetShaderInfoLog(glShader->mGLVertexShader, 2048, &infoLogLen, infoLog);
	GLint compiled = 0;

	//bf_glGetObjectParameterivARB(glShader->mGLVertexShader, GL_COMPILE_STATUS, &compiled);
    bf_glGetShaderiv(glShader->mGLVertexShader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
		BF_FATAL(StrFormat("Shader error: %s", infoLog).c_str());

	bf_glShaderSource(glShader->mGLFragmentShader, 1, (const GLchar**)&fragProgram, &fragProgramLen);
	bf_glCompileShader(glShader->mGLFragmentShader);
	bf_glGetShaderInfoLog(glShader->mGLFragmentShader, 2048, &infoLogLen, infoLog);
	compiled = 0;
	//bf_glGetObjectParameterivARB(glShader->mGLFragmentShader, GL_COMPILE_STATUS, &compiled);
    bf_glGetShaderiv(glShader->mGLFragmentShader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
		BF_FATAL(StrFormat("Shader error: %s", infoLog).c_str());

	glShader->mGLProgram = bf_glCreateProgram();
	bf_glAttachShader(glShader->mGLProgram, glShader->mGLVertexShader);
	bf_glAttachShader(glShader->mGLProgram, glShader->mGLFragmentShader);

	bf_glLinkProgram(glShader->mGLProgram);

	glShader->mAttribPosition = bf_glGetAttribLocation(glShader->mGLProgram, "position");
	glShader->mAttribTexCoord0 = bf_glGetAttribLocation(glShader->mGLProgram, "texCoord0");
	glShader->mAttribColor = bf_glGetAttribLocation(glShader->mGLProgram, "color");
	glShader->mAttribTex0 = bf_glGetUniformLocation(glShader->mGLProgram, "tex");
	glShader->mAttribTex1 = bf_glGetUniformLocation(glShader->mGLProgram, "tex2");

	return glShader;
}

void GLRenderDevice::PhysSetRenderState(RenderState* renderState)
{
	mCurShader = (GLShader*)renderState->mShader;
	if (mCurShader != NULL)
	{
		bf_glUseProgram(mCurShader->mGLProgram);

		GLRenderDevice* aRenderDevice = (GLRenderDevice*)gBFApp->mRenderDevice;

		//TODO: Cache more

		GLfloat matrix[4][4];
		CreateOrthographicOffCenter(0.0f, (float)mPhysRenderWindow->mWidth, (float)mPhysRenderWindow->mHeight, 0.0f, -100.0f, 100.0f, matrix);
		GLint matrixLoc = bf_glGetUniformLocation(mCurShader->mGLProgram, "screenMatrix");
		//BF_ASSERT(matrixLoc >= 0);
		if (matrixLoc >= 0)
			bf_glUniformMatrix4fv(matrixLoc, 1, false, (float*)matrix);
	}

	if (renderState->mClipped)
	{
		glEnable(GL_SCISSOR_TEST);
 		glScissor((GLsizei)renderState->mClipRect.mX,
			mPhysRenderWindow->mHeight - (GLsizei)renderState->mClipRect.mY - (GLsizei)renderState->mClipRect.mHeight,
 			(GLsizei)renderState->mClipRect.mWidth, (GLsizei)renderState->mClipRect.mHeight);
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
	}

	mPhysRenderState = renderState;
}

Texture* GLRenderDevice::CreateRenderTarget(int width, int height, bool destAlpha)
{
	NOT_IMPL;
}

void GLRenderDevice::SetRenderState(RenderState* renderState)
{
	mCurRenderState = renderState;
}


void GLSetTextureCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
/*#ifdef BF_PLATFORM_OPENGL_ES2
	//bf_glClientActiveTexture(GL_TEXTURE0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mTexture)->mGLTexture);
	glUniform1i(curShader->mAttribTex0, 0);

	//bf_glClientActiveTexture(GL_TEXTURE1);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mTexture)->mGLTexture2);
	glUniform1i(curShader->mAttribTex1, 1);

	//glEnable(GL_TEXTURE_2D);
#else
	glActiveTexture(GL_TEXTURE0 + mTextureIdx);
	glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mTexture)->mGLTexture);
#endif*/

	auto glTexture = (GLTexture*)mTexture;
	auto glRenderDevice = (GLRenderDevice*)renderDevice;

	if (glRenderDevice->mBlankTexture == 0)
	{
		glGenTextures(1, &glRenderDevice->mBlankTexture);
		glBindTexture(GL_TEXTURE_2D, glRenderDevice->mBlankTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		/*if (bf_glCompressedTexImage2D != NULL)
		{
			uint64 hwData = 0;
			bf_glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, 4, 4, 0,
									  sizeof(hwData), (uint8*)&hwData);
		}
		else*/
		{
			uint16 color = 0;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
				GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, &color);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	}

	if (glTexture->mImageData != NULL)
	{
		glTexture->mGLTexture2 = glRenderDevice->mBlankTexture;

		int texCount = 1;
		//texCount = (imageData->mHWBitsType == HWBITS_PVRTC_2X4BPPV1) ? 2 : 1;

		for (int texNum = 0; texNum < 2/*texCount*/; texNum++)
		{
			GLuint glTextureID;
			glGenTextures(1, &glTextureID);
			glBindTexture(GL_TEXTURE_2D, glTextureID);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			//if (imageData->mHWBits != NULL)
			//{
			//	int internalFormat = (imageData->mHWBitsType == HWBITS_PVRTC_2BPPV1) ?
			//		GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG :
			//		GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;

			//	int texSize = imageData->mHWBitsLength / texCount;

			//	bf_glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, imageData->mWidth, imageData->mHeight, 0,
			//		texSize, (uint8*)imageData->mHWBits /*+ (texNum * texSize)*/);
			//}
			//else
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glTexture->mImageData->mWidth, glTexture->mImageData->mHeight, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, glTexture->mImageData->mBits);
			}

			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

			if (texNum == 0)
				glTexture->mGLTexture = glTextureID;
			else
				glTexture->mGLTexture2 = glTextureID;
		}

		glTexture->mImageData->Deref();
		glTexture->mImageData = NULL;
	}

	bf_glActiveTexture(GL_TEXTURE0 + mTextureIdx);
	//glUniform1i(curShader->mAttribTex0, 0);
	glBindTexture(GL_TEXTURE_2D, glTexture->mGLTexture);
}
