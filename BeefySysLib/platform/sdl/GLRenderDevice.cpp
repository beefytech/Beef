#include "GLRenderDevice.h"
#include "BFApp.h"
#include "SdlBFApp.h"
#include "BFWindow.h"
#include "img/ImageData.h"
#include "util/PerfTimer.h"
#include <SDL3/SDL_video.h>

USING_NS_BF;

#ifndef NOT_IMPL
#define NOT_IMPL throw "Not implemented"
#endif

//#pragma comment(lib, "SDL3.lib")

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
extern bool (SDLCALL* bf_SDL_GetWindowSizeInPixels)(SDL_Window* window, int* w, int* h);
extern bool (SDLCALL* bf_SDL_GL_SwapWindow)(SDL_Window* window);
extern bool (SDLCALL* bf_SDL_GL_MakeCurrent)(SDL_Window* window, SDL_GLContext context);
extern SDL_GLContext (SDLCALL* bf_SDL_GL_GetCurrentContext)();
extern const char* (SDLCALL* bf_SDL_GetError)(void);

extern SDL_DisplayID (SDLCALL* bf_SDL_GetDisplayForWindow)(SDL_Window *window);
extern const SDL_DisplayMode* (SDLCALL* bf_SDL_GetCurrentDisplayMode)(SDL_DisplayID displayID);

static GLenum (APIENTRYP bf_glGetError)();
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

static void (APIENTRYP bf_glGenTextures)(GLsizei n, GLuint *textures);
static void (APIENTRYP bf_glBindTexture)(GLenum target, GLuint texture);
static void (APIENTRYP bf_glPixelStorei)(GLenum pname, GLint param);
static void (APIENTRYP bf_glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
static void (APIENTRYP bf_glTexParameteri)(GLenum target, GLenum pname, GLint param);
static void (APIENTRYP bf_glDisable)(GLenum cap);
static void (APIENTRYP bf_glEnable)(GLenum cap);
static void (APIENTRYP bf_glCullFace)(GLenum mode);
static void (APIENTRYP bf_glDepthFunc)(GLenum mode);
static void (APIENTRYP bf_glDepthMask)(GLboolean flag);
static void (APIENTRYP bf_glPolygonMode)(GLenum face, GLenum mode);
static void (APIENTRYP bf_glBlendFunc)(GLenum sfactor, GLenum dfactor);
static void (APIENTRYP bf_glScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
static void (APIENTRYP bf_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
static void (APIENTRYP bf_glFramebufferTexture)(GLenum target, GLenum attachment, GLuint texture, GLint level);
static void (APIENTRYP bf_glDrawBuffers)(GLsizei n, const GLenum *bufs);
static GLenum (APIENTRYP bf_glCheckFramebufferStatus)(GLenum target);
static void (APIENTRYP bf_glTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
static void (APIENTRYP bf_glBindFramebuffer)(GLenum target, GLuint framebuffer);
static void (APIENTRYP bf_glClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
static void (APIENTRYP bf_glClearDepth)(GLdouble depth);
static void (APIENTRYP bf_glClear)(GLbitfield mask);

#if !defined BF_PLATFORM_OPENGL_ES2
static void (APIENTRYP bf_glGetVertexAttribdv)(GLuint index, GLenum pname, GLdouble *params);
#endif

enum class DebugSource : uint32
{
	API = 0x8246,
	WindowSystem = 0x8247,
	ShaderCompiler = 0x8248,
	ThirdParty = 0x8249,
	Application = 0x824A,
	Other = 0x824B
};

enum class DebugType : uint32
{
	Error = 0x824C,
	DeprecatedBehavior = 0x824D,
	UndefinedBehavior = 0x824E,
	Portability = 0x824F,
	Performance = 0x8250,
	Other = 0x8251,
	Marker = 0x8268,
};

enum class DebugSeverity : uint32
{
	High = 0x9146,
	Medium = 0x9147,
	Low = 0x9148,
	Notification = 0x826B
};

typedef void(*DEBUGPROC)(DebugSource source, DebugType type, uint32 id, DebugSeverity severity, uint32 length, char* message, void* userParam);
static  void (APIENTRYP bf_glDebugMessageCallback)(DEBUGPROC fn, void* userdata);

static void APIENTRY DebugMessageCallback(DebugSource source, DebugType type, uint32 id, DebugSeverity severity, uint32 length, char* message, void* userParam)
{
	if (type == DebugType::Marker || severity == DebugSeverity::Notification)
		return;

	switch (type)
	{
		case DebugType::Error:
			OutputDebugStr("[Error] ");
			break;
		case DebugType::DeprecatedBehavior:
			OutputDebugStr("[DeprecatedBehavior] ");
			break;

		case DebugType::UndefinedBehavior:
			OutputDebugStr("[UndefinedBehavior] ");
			break;

		case DebugType::Portability:
			OutputDebugStr("[Portability] ");
			break;

		case DebugType::Performance:
			OutputDebugStr("[Performance] ");
			break;

		case DebugType::Other:
			OutputDebugStr("[Other] ");
			break;

		case DebugType::Marker:
			OutputDebugStr("[Marker] ");
			break;
	}

    OutputDebugStr(message);
	OutputDebugStrF(" %d\n", id);
}

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
		bf_glBindTexture(GL_TEXTURE_2D, mGLTexture);
		bf_glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, imageData->mWidth, imageData->mHeight, GL_RGBA, GL_UNSIGNED_BYTE, imageData->mBits);
	}
}

void GLTexture::SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32 *bits)
{
	bf_glBindTexture(GL_TEXTURE_2D, mGLTexture);
	bf_glTexSubImage2D(GL_TEXTURE_2D, 0, destX, destY, destWidth, destHeight, GL_RGBA, GL_UNSIGNED_BYTE, bits);
}

///

GLDrawBatch::GLDrawBatch() : DrawBatch()
{

}

GLDrawBatch::~GLDrawBatch()
{
}

extern int gBFDrawBatchCount;

void GLDrawBatch::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if (mIdxIdx == 0)
		return;

    gBFDrawBatchCount++;

	GLRenderDevice* glRenderDevice = (GLRenderDevice*) gBFApp->mRenderDevice;
	GLShader* curShader = (GLShader*)mRenderState->mShader;

	if (glRenderDevice->mGLVAO == 0)
	{
		if (glRenderDevice->mGLVertexBuffer == 0)
		{
			bf_glGenBuffers(1, &glRenderDevice->mGLVertexBuffer);
			bf_glGenBuffers(1, &glRenderDevice->mGLIndexBuffer);
		}

		bf_glBindBuffer(GL_ARRAY_BUFFER, glRenderDevice->mGLVertexBuffer);
		bf_glGenVertexArrays(1, &glRenderDevice->mGLVAO);
		bf_glBindVertexArray(glRenderDevice->mGLVAO);

		bf_glEnableVertexAttribArray(curShader->mAttribPosition);
		bf_glVertexAttribPointer(curShader->mAttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(DefaultVertex3D), (void*)offsetof(DefaultVertex3D, x));
		bf_glEnableVertexAttribArray(curShader->mAttribTexCoord0);
		bf_glVertexAttribPointer(curShader->mAttribTexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(DefaultVertex3D), (void*)offsetof(DefaultVertex3D, u));
		bf_glEnableVertexAttribArray(curShader->mAttribColor);
		bf_glVertexAttribPointer(curShader->mAttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(DefaultVertex3D), (void*)offsetof(DefaultVertex3D, color));
	}
    bf_glBindVertexArray(glRenderDevice->mGLVAO);
	auto glVertices = (DefaultVertex3D*)mVertices;

	bf_glBindBuffer(GL_ARRAY_BUFFER, glRenderDevice->mGLVertexBuffer);
	bf_glBufferData(GL_ARRAY_BUFFER, mVtxIdx * sizeof(DefaultVertex3D), mVertices, GL_STREAM_DRAW);
	bf_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glRenderDevice->mGLIndexBuffer);
	bf_glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIdxIdx * sizeof(int16), mIndices, GL_STREAM_DRAW);

	if (mRenderState != renderDevice->mPhysRenderState)
		renderDevice->PhysSetRenderState(mRenderState);

	bf_glDrawElements(GL_TRIANGLES, mIdxIdx, GL_UNSIGNED_SHORT, NULL);
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

GLRenderWindow::GLRenderWindow(GLRenderDevice* renderDevice, SDL_Window* sdlWindow)
{
	if (bf_glGenBuffers == NULL)
	{
		BF_GET_GLPROC(glGetError);
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
		BF_GET_GLPROC(glGenTextures);
		BF_GET_GLPROC(glBindTexture);
		BF_GET_GLPROC(glPixelStorei);
		BF_GET_GLPROC(glTexImage2D);
		BF_GET_GLPROC(glTexParameteri);
		BF_GET_GLPROC(glDisable);
		BF_GET_GLPROC(glEnable);
		BF_GET_GLPROC(glCullFace);
		BF_GET_GLPROC(glDepthMask);
		BF_GET_GLPROC(glPolygonMode);
		BF_GET_GLPROC(glDepthFunc);
		BF_GET_GLPROC(glBlendFunc);
		BF_GET_GLPROC(glScissor);
		BF_GET_GLPROC(glViewport);
		BF_GET_GLPROC(glFramebufferTexture);
		BF_GET_GLPROC(glDrawBuffers);
		BF_GET_GLPROC(glCheckFramebufferStatus);
		BF_GET_GLPROC(glTexSubImage2D);
		BF_GET_GLPROC(glBindFramebuffer);
		BF_GET_GLPROC(glClearColor);
		BF_GET_GLPROC(glClearDepth);
		BF_GET_GLPROC(glClear);

#if !defined BF_PLATFORM_OPENGL_ES2
        BF_GET_GLPROC(glGetVertexAttribdv);
#endif

#if defined(_DEBUG) && !defined(BF_PLATFORM_OPENGL_ES2)

		BF_GET_GLPROC(glDebugMessageCallback);
		if (bf_glDebugMessageCallback != NULL)
		{
			bf_glEnable(GL_DEBUG_OUTPUT);
			bf_glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			bf_glDebugMessageCallback(&DebugMessageCallback, NULL);
		}
#endif

	}

#ifndef BF_PLATFORM_OPENGL_ES2
	//glEnableClientState(GL_INDEX_ARRAY);
#endif

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
    bf_SDL_GL_MakeCurrent(mSDLWindow, ((SdlBFApp*)gBFApp)->mGLContext);

	bf_glEnable(GL_BLEND);
	bf_glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	if (mResizePending)
	{
		Resized();
	}

	bf_glViewport(0, 0, (GLsizei)mWidth, (GLsizei)mHeight);
	bf_glScissor(0, 0, (GLsizei)mWidth, (GLsizei)mHeight);

	if (!mHasBeenDrawnTo)
	{
		bf_glClearColor(0, 0, 0, 0);
		bf_glClearDepth(1);
		bf_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
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

	bf_SDL_GetWindowSizeInPixels(mSDLWindow, &mWidth, &mHeight);
	mResizePending = false;
}

void GLRenderWindow::Present()
{
	bf_SDL_GL_SwapWindow(mSDLWindow);
}

void GLRenderWindow::CopyBitsTo(uint32* dest, int width, int height)
{
	mCurDrawLayer->Flush();
	NOT_IMPL;
}

float GLRenderWindow::GetRefreshRate()
{
	SDL_DisplayID displayID = bf_SDL_GetDisplayForWindow(mSDLWindow);
	const SDL_DisplayMode *mode = bf_SDL_GetCurrentDisplayMode(displayID);

	if (mode != NULL)
		return mode->refresh_rate;

	return RenderWindow::GetRefreshRate();
}

///

GLRenderDevice::GLRenderDevice()
{
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

	BF_ASSERT(mDefaultRenderState == NULL);
	auto renderState = (RenderState*)CreateRenderState(NULL);

	mDefaultRenderState = renderState;
	mDefaultRenderState->mDepthFunc = DepthFunc_Less;
	mDefaultRenderState->mWriteDepthBuffer = true;
	mPhysRenderState = mDefaultRenderState;

	return true;
}

void GLRenderDevice::FrameStart()
{
	if (mRenderWindowList.IsEmpty())
		return;

	mCurRenderTarget = NULL;
	mPhysRenderWindow = NULL;

	for (auto aRenderWindow : mRenderWindowList)
	{
		aRenderWindow->mHasBeenDrawnTo = false;
		aRenderWindow->mHasBeenTargeted = false;
	}

	bf_glDisable(GL_SCISSOR_TEST);
	bf_glDisable(GL_CULL_FACE);
	bf_glDisable(GL_DEPTH_TEST);
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

	glShader->mVertexSize = sizeof(DefaultVertex3D);
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
	glShader->mScreenUniformLoc = bf_glGetUniformLocation(glShader->mGLProgram, "screenMatrix");

	return glShader;
}

void GLRenderDevice::PhysSetRenderState(RenderState* renderState)
{
	mCurShader = (GLShader*)renderState->mShader;
	if (mCurShader != NULL)
	{
		bf_glUseProgram(mCurShader->mGLProgram);
		if (mCurShader->mScreenUniformLoc >= 0)
		{
			GLfloat matrix[4][4];
			CreateOrthographicOffCenter(0.0f, (float)mPhysRenderWindow->mWidth, (float)mPhysRenderWindow->mHeight, 0.0f, -100.0f, 100.0f, matrix);
			bf_glUniformMatrix4fv(mCurShader->mScreenUniformLoc, 1, false, (float*)matrix);
		}
	}

	if (renderState->mClipped)
	{
		bf_glEnable(GL_SCISSOR_TEST);
		bf_glScissor((GLsizei)renderState->mClipRect.x,
		   mPhysRenderWindow->mHeight - (GLsizei)renderState->mClipRect.y - (GLsizei)renderState->mClipRect.height,
			(GLsizei)renderState->mClipRect.width, (GLsizei)renderState->mClipRect.height);
	}
	else
	{
		bf_glDisable(GL_SCISSOR_TEST);
	}

	if (renderState->mCullMode != mPhysRenderState->mCullMode)
	{
		switch (renderState->mCullMode)
		{
			case CullMode_None:
				bf_glDisable(GL_CULL_FACE);
				break;
			case CullMode_Front:
				bf_glEnable(GL_CULL_FACE);
				bf_glCullFace(GL_FRONT);
				break;
			case CullMode_Back:
				bf_glEnable(GL_CULL_FACE);
				bf_glCullFace(GL_BACK);
				break;
		}
	}

	if (renderState->mDepthFunc != mPhysRenderState->mDepthFunc)
	{
		bf_glDepthMask(renderState->mWriteDepthBuffer ? GL_TRUE : GL_FALSE);

		switch (renderState->mDepthFunc)
		{
			case DepthFunc_Never:
				bf_glDisable(GL_DEPTH_TEST);
				break;
			case DepthFunc_Less:
				bf_glDepthFunc(GL_LESS);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_LessEqual:
				bf_glDepthFunc(GL_LEQUAL);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_Equal:
				bf_glDepthFunc(GL_EQUAL);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_Greater:
				bf_glDepthFunc(GL_GREATER);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_NotEqual:
				bf_glDepthFunc(GL_NOTEQUAL);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_GreaterEqual:
				bf_glDepthFunc(GL_GEQUAL);
				bf_glEnable(GL_DEPTH_TEST);
				break;
			case DepthFunc_Always:
				bf_glDepthFunc(GL_ALWAYS);
				bf_glEnable(GL_DEPTH_TEST);
				break;
		}
	}

	if ((renderState->mWireframe != mPhysRenderState->mWireframe))
	{
		if (renderState->mWireframe)
			bf_glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		else
			bf_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	mPhysRenderState = renderState;
}

Texture* GLRenderDevice::CreateRenderTarget(int width, int height, bool destAlpha)
{
	// GLTexture* texture = new GLTexture();
	// GLuint id;
	// bf_glGenTextures(1, &id);
	// bf_glBindTexture(GL_TEXTURE_2D, id);
	// bf_glTexImage2D(GL_TEXTURE_2D, 0, (destAlpha ? GL_RGBA : GL_RGB), width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	// bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//
	// GLuint depthrenderbuffer;
	// bf_glGenRenderbuffers(1, &depthrenderbuffer);
	// bf_glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
	// bf_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	// bf_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);
	// texture->mGLTexture = id;
	// texture->mGLTexture2 = depthrenderbuffer;
	// texture->AddRef();
	// return texture;
	NOT_IMPL;
}

Texture* GLRenderDevice::CreateDynTexture(int width, int height)
{
	auto context = bf_SDL_GL_GetCurrentContext();
	auto err = bf_SDL_GetError();
	GLuint texture = 0;
	bf_glGenTextures(1, &texture);
	auto result = bf_glGetError();

	bf_glBindTexture(GL_TEXTURE_2D, texture);
	bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	bf_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	GLTexture* glTexture = new GLTexture();
	glTexture->mGLTexture = texture;
	glTexture->mGLTexture2 = 0;
	glTexture->mRenderDevice = this;
	glTexture->mWidth = width;
	glTexture->mHeight = height;
	glTexture->AddRef();
	return glTexture;
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
		bf_glGenTextures(1, &glRenderDevice->mBlankTexture);
		bf_glBindTexture(GL_TEXTURE_2D, glRenderDevice->mBlankTexture);
		bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		/*if (bf_glCompressedTexImage2D != NULL)
		{
			uint64 hwData = 0;
			bf_glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, 4, 4, 0,
									  sizeof(hwData), (uint8*)&hwData);
		}
		else*/
		{
			uint16 color = 0;
			bf_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
				GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, &color);
		}
		bf_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	}

	if (glTexture->mImageData != NULL)
	{
		glTexture->mGLTexture2 = glRenderDevice->mBlankTexture;

		int texCount = 1;
		//texCount = (imageData->mHWBitsType == HWBITS_PVRTC_2X4BPPV1) ? 2 : 1;

		for (int texNum = 0; texNum < 2/*texCount*/; texNum++)
		{
			GLuint glTextureID;
			bf_glGenTextures(1, &glTextureID);
			bf_glBindTexture(GL_TEXTURE_2D, glTextureID);
			bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			bf_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
				bf_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glTexture->mImageData->mWidth, glTexture->mImageData->mHeight, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, glTexture->mImageData->mBits);
			}

			bf_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

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
	bf_glBindTexture(GL_TEXTURE_2D, glTexture->mGLTexture);
}
