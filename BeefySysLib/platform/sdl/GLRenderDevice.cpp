#include "GLRenderDevice.h"
#include "SdlBFApp.h"
#include "BFWindow.h"
#include "img/ImageData.h"
#include "util/PerfTimer.h"
#include "SDL_video.h"

USING_NS_BF;

#define NOT_IMPL throw "Not implemented"

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
	mGLVariable = NULL;
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
	GLShaderParamMap::iterator itr = mParamsMap.begin();
	while (itr != mParamsMap.end())
	{
		delete itr->second;
		++itr;
	}

	//if (mGLEffect != NULL)
		//mGLEffect->Release();	
}

ShaderParam* GLShader::GetShaderParam(const std::wstring& name)
{
	/*GLShaderParamMap::iterator itr = mParamsMap.find(name);
	if (itr != mParamsMap.end())
		return itr->second;

	IGL10EffectVariable* d3DVariable = mGLEffect->GetVariableByName(ToString(name).c_str());
	if (d3DVariable == NULL)
		return NULL;

	GLShaderParam* shaderParam = new GLShaderParam();
	shaderParam->mGLVariable = d3DVariable;
	mParamsMap[name] = shaderParam;

	return shaderParam;*/
	NOT_IMPL;
	return NULL;
}

///

GLTexture::GLTexture()
{
	mGLTexture = NULL;
	//mGLRenderTargetView = NULL;	
	mRenderDevice = NULL;
}

GLTexture::~GLTexture()
{
	//if (mGLTexture != NULL)
		//mGLTexture->Release();
}

void GLTexture::PhysSetAsTarget()
{	
	NOT_IMPL;
	//{
	//	GL10_VIEWPORT viewPort;
	//	viewPort.Width = mWidth;
	//	viewPort.Height = mHeight;
	//	viewPort.MinDepth = 0.0f;
	//	viewPort.MaxDepth = 1.0f;
	//	viewPort.TopLeftX = 0;
	//	viewPort.TopLeftY = 0;

	//	mRenderDevice->mGLDevice->RSSetViewports(1, &viewPort);
	//	mRenderDevice->mGLDevice->OMSetRenderTargets(1, &mGLRenderTargetView, NULL);		
	//}

	//if (!mHasBeenDrawnTo)
	//{
	//	//mRenderDevice->mGLDevice->ClearRenderTargetView(mGLRenderTargetView, D3GLVECTOR4(1, 0.5, 0.5, 1));				
	//	mHasBeenDrawnTo = true;
	//}
}

///

GLDrawBatch::GLDrawBatch(int minVtxSize, int minIdxSize) : DrawBatch()
{	
	mAllocatedVertices = 16*1024;
	// Alloc more indices than vertices. Worst case is really long tri strip.
	mAllocatedIndices = mAllocatedVertices * 3;

	if (minVtxSize > mAllocatedVertices)
		mAllocatedVertices = GetPowerOfTwo(minVtxSize);
	if (minIdxSize > mAllocatedIndices)
		mAllocatedIndices = GetPowerOfTwo(minIdxSize);

	mVertices = new Vertex3D[mAllocatedVertices];	
	mIndices = new uint16[mAllocatedIndices];
    mNext = NULL;
}

GLDrawBatch::~GLDrawBatch()
{
	delete mVertices;
	delete mIndices;
	//mGLBuffer->Release();
}

void GLDrawBatch::Lock()
{		
	//mGLBuffer->Map(GL10_MAP_WRITE_DISCARD, 0, (void**) &mVertices);
}

extern int gBFDrawBatchCount;

void GLDrawBatch::Draw()
{
	if (mIdxIdx == 0)
		return;

    gBFDrawBatchCount++;
    
	GLRenderDevice* glRenderDevice = (GLRenderDevice*) gBFApp->mRenderDevice;
	if (glRenderDevice->mPhysRenderWindow != mDrawLayer->mRenderWindow)
		glRenderDevice->PhysSetRenderWindow(mDrawLayer->mRenderWindow);
	
	//// Flip BGRA to RGBA
	//for (int i = 0; i < mIdx; i++)
	//{
	//	uint32 aColor = mVertices[i].color;
	//	aColor = 
	//		(aColor & 0xFF00FF00) |
	//		((aColor & 0x00FF0000) >> 16) |
	//		((aColor & 0x000000FF) << 16);
	//	mVertices[i].color = aColor;
	//}

	//mGLBuffer->Unmap();
	
	
	//GLShader* curShader = (GLShader*) aRenderDevice->mCurShader;
	GLShader* curShader = (GLShader*) mCurShader;
		
	//if (curShader->mTextureParam != NULL)
		//curShader->mTextureParam->SetTexture(mCurTexture);

#ifdef BF_PLATFORM_OPENGL_ES2
	//bf_glClientActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mCurTexture)->mGLTexture);
	glUniform1i(curShader->mAttribTex0, 0);
    
	//bf_glClientActiveTexture(GL_TEXTURE1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mCurTexture)->mGLTexture2);
	glUniform1i(curShader->mAttribTex1, 1);
	//glEnable(GL_TEXTURE_2D);
#else
	glBindTexture(GL_TEXTURE_2D, ((GLTexture*)mCurTexture)->mGLTexture);
#endif

    
	//if (mIsAdditive != glRenderDevice->mCurAdditive)
		//glRenderDevice->PhysSetAdditive(mIsAdditive);

	//TODO: Just do 'apply', we don't have to do full PhysSetShaderPass
	if (curShader != glRenderDevice->mPhysShader)
		glRenderDevice->PhysSetShader(mCurShader);		

	// Set vertex buffer
	
	if (glRenderDevice->mGLVertexBuffer == 0)
	{
		bf_glGenBuffers(1, &glRenderDevice->mGLVertexBuffer);		
		bf_glGenBuffers(1, &glRenderDevice->mGLIndexBuffer);
	}	

	bf_glBindBuffer(GL_ARRAY_BUFFER, glRenderDevice->mGLVertexBuffer);
	bf_glBufferData(GL_ARRAY_BUFFER, mVtxIdx * sizeof(Vertex3D), mVertices, GL_STREAM_DRAW);
	
	bf_glVertexAttribPointer(curShader->mAttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, x));	
	bf_glVertexAttribPointer(curShader->mAttribTexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, u));	
	bf_glVertexAttribPointer(curShader->mAttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));

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

DrawBatch* GLDrawLayer::AllocateBatch(int minVtxCount, int minIdxCount)
{
	AutoPerf autoPerf("GLDrawLayer::AllocateBatch");

	//GLDrawBatchVector* pool = &((GLRenderDevice*) gBFApp->mRenderDevice)->mDrawBatchPool;

    GLRenderDevice* glRenderDevice = (GLRenderDevice*)gBFApp->mRenderDevice;
    
	GLDrawBatch* newBatch = NULL;
	
    //TODO: Search
    GLDrawBatch** prevRefPtr = &glRenderDevice->mFreeBatchHead;
    GLDrawBatch* checkBatch = glRenderDevice->mFreeBatchHead;
    
    while (checkBatch != NULL)
    {
        if ((checkBatch->mAllocatedVertices >= minVtxCount) &&
			(checkBatch->mAllocatedIndices >= minIdxCount))
		{
			newBatch = checkBatch;
			*prevRefPtr = (GLDrawBatch*)checkBatch->mNext;
            checkBatch->mNext = NULL;
			break;
		}
        
        checkBatch = (GLDrawBatch*)checkBatch->mNext;
        prevRefPtr = (GLDrawBatch**)&checkBatch->mNext;
    }
    
	/*for (int i = pool->size() -1; i >= 0; i--)
	{
		GLDrawBatch* checkBatch = (*pool)[i];
		
		if ((checkBatch->mAllocatedVertices >= minVtxCount) &&
			(checkBatch->mAllocatedIndices >= minIdxCount))
		{
			newBatch = checkBatch;
			pool->erase(pool->begin() + i);
			break;
		}
	}*/

	if (newBatch == NULL)
		newBatch = new GLDrawBatch(minVtxCount, minIdxCount);	
	
	newBatch->mDrawLayer = this;
	return newBatch;
}

void GLDrawLayer::FreeBatch(DrawBatch* drawBatch)
{
	//delete drawBatch;

	GLDrawBatch* batch = (GLDrawBatch*) drawBatch;
	batch->Clear();

	//GLDrawBatchVector* pool = &((GLRenderDevice*) gBFApp->mRenderDevice)->mDrawBatchPool;
	//pool->push_back(batch);
    GLRenderDevice* glRenderDevice = (GLRenderDevice*)gBFApp->mRenderDevice;
    drawBatch->mNext = glRenderDevice->mFreeBatchHead;
    glRenderDevice->mFreeBatchHead = batch;
}

void GLRenderDevice::PhysSetShader(Shader* shader)
{	
	GLRenderDevice* aRenderDevice = (GLRenderDevice*) gBFApp->mRenderDevice;
	
	//TODO: Cache more

	GLShader* glShader = (GLShader*)shader;

	GLfloat matrix[4][4];
	CreateOrthographicOffCenter(0.0f, (float)mPhysRenderWindow->mWidth, (float)mPhysRenderWindow->mHeight, 0.0f, -100.0f, 100.0f, matrix);
	GLint matrixLoc = bf_glGetUniformLocation(glShader->mGLProgram, "screenMatrix");	
	//BF_ASSERT(matrixLoc >= 0);	
	if (matrixLoc >= 0)
        bf_glUniformMatrix4fv(matrixLoc, 1, false, (float*)matrix);

	/*mPhysShaderPass = shaderPass;
	GLShaderPass* dXShaderPass = (GLShaderPass*) mPhysShaderPass;
	mGLDevice->IASetInputLayout(dXShaderPass->mGLLayout);
	
	if (mCurShader->mLastResizeCount != mCurRenderTarget->mResizeNum)
	{
		ShaderParam* shaderParam = mCurShader->GetShaderParam(L"WindowSize");
		if (shaderParam != NULL)
		{
			shaderParam->SetFloat2((float) mCurRenderTarget->mWidth, (float) mCurRenderTarget->mHeight);
		}

		mCurShader->mLastResizeCount = mCurRenderTarget->mResizeNum;
	}

	GLCHECK(dXShaderPass->mGLEffectPass->Apply(0));*/

	/*GLfloat matrix[4][4];
	CreateOrthographicOffCenter(0.0f, (float)mPhysRenderWindow->mWidth, (float)mPhysRenderWindow->mHeight, 0.0f, -100.0f, 100.0f, matrix);
	GLint uniformLocation = bf_glGetUniformLocation(((GLShader*)shaderPass->mTechnique->mShader)->mGLProgram, "screenMatrix");
	if (uniformLocation != -1)
		bf_glUniformMatrix4fv(uniformLocation, 1, false, (GLfloat*)matrix);*/
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
	proc = (T)SDL_GL_GetProcAddress(name);
}

#define BF_GET_GLPROC(name) BFGetGLProc(bf_##name, #name) 

GLRenderWindow::GLRenderWindow(GLRenderDevice* renderDevice, SDL_Window* sdlWindow)
{	
	if (bf_glGenBuffers == NULL)
	{
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
	
	//mGLSwapChain = NULL;
	//mGLBackBuffer = NULL;
	//mGLRenderTargetView = NULL;	

	//mRenderDevice = renderDevice;
	//mHWnd = hWnd;

	//HRESULT hr = S_OK;	

	//Resized();

	//GLGI_SWAP_CHAIN_DESC swapChainDesc;
	//ZeroMemory( &swapChainDesc, sizeof(swapChainDesc) );
	//swapChainDesc.BufferCount = 1;
	//swapChainDesc.BufferDesc.Width = mWidth;
	//swapChainDesc.BufferDesc.Height = mHeight;
	//swapChainDesc.BufferDesc.Format = GLGI_FORMAT_R8G8B8A8_UNORM;
	//swapChainDesc.BufferUsage = GLGI_USAGE_RENDER_TARGET_OUTPUT;
	//swapChainDesc.OutputWindow = mHWnd;
	//swapChainDesc.SampleDesc.Count = 1;
	//swapChainDesc.SampleDesc.Quality = 0;
	//swapChainDesc.Windowed = TRUE;

	////swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	////swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	////swapChainDesc.Flags = GLGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

 //   IGLGIDevice* pGLGIDevice = NULL;
 //   mRenderDevice->mGLDevice->QueryInterface(__uuidof(IGLGIDevice), (void**) &pGLGIDevice );

 //   GLCHECK(mRenderDevice->mGLGIFactory->CreateSwapChain(pGLGIDevice, &swapChainDesc, &mGLSwapChain));
 //   pGLGIDevice->Release();
 //   pGLGIDevice = NULL;
	//
	//GLCHECK(mGLSwapChain->GetBuffer(0, __uuidof(IGL10Texture2D), (LPVOID*)&mGLBackBuffer));	
	//GLCHECK(mRenderDevice->mGLDevice->CreateRenderTargetView(mGLBackBuffer, NULL, &mGLRenderTargetView));		
}

GLRenderWindow::~GLRenderWindow()
{	
	/*if (mGLRenderTargetView != NULL)
		mGLRenderTargetView->Release();
		if (mGLBackBuffer != NULL)
		mGLBackBuffer->Release();
		if (mGLSwapChain != NULL)
		mGLSwapChain->Release();	*/
}

void GLRenderWindow::PhysSetAsTarget()
{
	GLfloat matrix[4][4];
	CreateOrthographicOffCenter(0.0f, (float)mWidth, (float)mHeight, 0.0f, -100.0f, 100.0f, matrix);
    
    glViewport(0, 0, (GLsizei)mWidth, (GLsizei)mHeight);
    
    //TODO: Set matrix variable
	//glMatrixMode(GL_MODELVIEW);
	//glLoadMatrixf((const GLfloat*)matrix);

	

    
	//NOT_IMPL;
	////if (mRenderDevice->mCurRenderTarget != this)
	//{
	//	GL10_VIEWPORT viewPort;
	//	viewPort.Width = mWidth;
	//	viewPort.Height = mHeight;
	//	viewPort.MinDepth = 0.0f;
	//	viewPort.MaxDepth = 1.0f;
	//	viewPort.TopLeftX = 0;
	//	viewPort.TopLeftY = 0;
	//	
	//	mRenderDevice->mGLDevice->OMSetRenderTargets(1, &mGLRenderTargetView, NULL);		
	//	mRenderDevice->mGLDevice->RSSetViewports(1, &viewPort);
	//}

	//if (!mHasBeenDrawnTo)
	//{		
	//	//mRenderDevice->mGLDevice->ClearRenderTargetView(mGLRenderTargetView, D3GLVECTOR4(rand() / (float) RAND_MAX, 0, 1, 0));				
	//	mRenderDevice->mGLDevice->ClearRenderTargetView(mGLRenderTargetView, D3GLVECTOR4(0, 0, 0, 0));				
	//}

	//mHasBeenDrawnTo = true;
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

	SDL_GetWindowSize(mSDLWindow, &mWidth, &mHeight);

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
	SDL_GL_SwapWindow(mSDLWindow);	
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
	mPhysShader = NULL;
	RenderWindowList::iterator itr = mRenderWindowList.begin();
	while (itr != mRenderWindowList.end())
	{
		(*itr)->mHasBeenDrawnTo = false;
		(*itr)->mHasBeenTargeted = false;
		++itr;
	}	
}

void GLRenderDevice::FrameEnd()
{	
	RenderWindowList::iterator itr = mRenderWindowList.begin();
	while (itr != mRenderWindowList.end())
	{
		RenderWindow* aRenderWindow = *itr;		
		if (aRenderWindow->mHasBeenTargeted)
		{
			//aRenderWindow->mCurDrawLayer->Flush();

			DrawLayerList::iterator drawLayerItr = aRenderWindow->mDrawLayerList.begin();
			while (drawLayerItr != aRenderWindow->mDrawLayerList.end())
			{
				DrawLayer* drawLayer = *drawLayerItr;
				drawLayer->Flush();
				++drawLayerItr;
			}

			aRenderWindow->Present();
		}
		++itr;
	}
}

Texture* GLRenderDevice::LoadTexture(ImageData* imageData, bool additive)
{
	imageData->mIsAdditive = additive;	
	imageData->PremultiplyAlpha();

	//int w = power_of_two(imageData->mWidth);
	//int h = power_of_two(imageData->mHeight);

	if (mBlankTexture == 0)
	{
		glGenTextures(1, &mBlankTexture);
		glBindTexture(GL_TEXTURE_2D, mBlankTexture);
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

	GLTexture* glTexture = new GLTexture();
	glTexture->mGLTexture2 = mBlankTexture;

	int texCount = 0;
	texCount = (imageData->mHWBitsType == HWBITS_PVRTC_2X4BPPV1) ? 2 : 1;
    
	for (int texNum = 0; texNum < 2/*texCount*/; texNum++)
	{
		GLuint glTextureID;
		glGenTextures(1, &glTextureID);
		glBindTexture(GL_TEXTURE_2D, glTextureID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
		if (imageData->mHWBits != NULL)
		{
			int internalFormat = (imageData->mHWBitsType == HWBITS_PVRTC_2BPPV1) ?
				GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG :
				GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;

			int texSize = imageData->mHWBitsLength / texCount;

			bf_glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, imageData->mWidth, imageData->mHeight, 0,
				texSize, (uint8*)imageData->mHWBits /*+ (texNum * texSize)*/);
		}
		else
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageData->mWidth, imageData->mHeight, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, imageData->mBits);
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		if (texNum == 0)
			glTexture->mGLTexture = glTextureID;
		else
			glTexture->mGLTexture2 = glTextureID;
	}

	glTexture->mWidth = imageData->mWidth;
	glTexture->mHeight = imageData->mHeight;
	glTexture->AddRef();

	return glTexture;

	//IGL10ShaderResourceView* d3DShaderResourceView = NULL;

	//imageData->PremultiplyAlpha();

	//int aWidth = 0;
	//int aHeight = 0;
	//
	//// Create the render target texture
	//GL10_TEXTURE2D_DESC desc;
	//ZeroMemory(&desc, sizeof(desc));
	//desc.Width = imageData->mWidth;
	//desc.Height = imageData->mHeight;
	//desc.MipLevels = 1;
	//desc.ArraySize = 1;
	//desc.Format = GLGI_FORMAT_R8G8B8A8_UNORM;
	//desc.SampleDesc.Count = 1;
	//desc.Usage = GL10_USAGE_DYNAMIC;
	//desc.CPUAccessFlags = GL10_CPU_ACCESS_WRITE;
	//desc.BindFlags = GL10_BIND_SHADER_RESOURCE;

	//IGL10Texture2D* d3DTexture = NULL;
	//GLCHECK(mGLDevice->CreateTexture2D(&desc, NULL, &d3DTexture));

	//aWidth = imageData->mWidth;
	//aHeight = imageData->mHeight;

	//GL10_MAPPED_TEXTURE2D mapTex;
	//GLCHECK(d3DTexture->Map(GL10CalcSubresource(0, 0, 1), GL10_MAP_WRITE_DISCARD, 0, &mapTex));
	//uint8* destPtr = (uint8*) mapTex.pData;
	//uint8* srcPtr = (uint8*) imageData->mBits;
	//for (int y = 0; y < imageData->mHeight; y++)
	//{
	//	memcpy(destPtr, srcPtr, aWidth*sizeof(uint32));
	//	srcPtr += aWidth*sizeof(uint32);
	//	destPtr += mapTex.RowPitch;
	//}
	//d3DTexture->Unmap(0);			

	//GL10_SHADER_RESOURCE_VIEW_DESC srDesc;
	//srDesc.Format = desc.Format;
	//srDesc.ViewDimension = GL10_SRV_DIMENSION_TEXTURE2D;
	//srDesc.Texture2D.MostDetailedMip = 0;
	//srDesc.Texture2D.MipLevels = 1;	
	//	
	//GLCHECK(mGLDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

	//GLTexture* aTexture = new GLTexture();
	//aTexture->mWidth = aWidth;
	//aTexture->mHeight = aHeight;
	//aTexture->mGLTexture = d3DShaderResourceView;
	//aTexture->AddRef();
	//return aTexture;
}

Shader* GLRenderDevice::LoadShader(const StringImpl& fileName)
{
	GLShader* glShader = new GLShader();

	glShader->mGLVertexShader = bf_glCreateShader(GL_VERTEX_SHADER);
	glShader->mGLFragmentShader = bf_glCreateShader(GL_FRAGMENT_SHADER);
    
	GLint vertProgramLen = 0;
	GLint fragProgramLen = 0;
    
#ifdef BF_PLATFORM_OPENGL_ES2
	GLchar* vertProgram = (GLchar*)LoadBinaryData(fileName + L"_es.vert", &vertProgramLen);
	GLchar* fragProgram = (GLchar*)LoadBinaryData(fileName + L"_es.frag", &fragProgramLen);
#else
	GLchar* vertProgram = (GLchar*)LoadBinaryData(fileName + L".vert", &vertProgramLen);
	GLchar* fragProgram = (GLchar*)LoadBinaryData(fileName + L".frag", &fragProgramLen);
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
	bf_glUseProgram(glShader->mGLProgram);
	
	glShader->mAttribPosition = bf_glGetAttribLocation(glShader->mGLProgram, "position");	
	glShader->mAttribTexCoord0 = bf_glGetAttribLocation(glShader->mGLProgram, "texCoord0");	
	glShader->mAttribColor = bf_glGetAttribLocation(glShader->mGLProgram, "color");	
	glShader->mAttribTex0 = bf_glGetUniformLocation(glShader->mGLProgram, "tex");	
	glShader->mAttribTex1 = bf_glGetUniformLocation(glShader->mGLProgram, "tex2");	

	if (glShader->mAttribPosition >= 0)
		bf_glEnableVertexAttribArray(glShader->mAttribPosition);
	if (glShader->mAttribTexCoord0 >= 0)
		bf_glEnableVertexAttribArray(glShader->mAttribTexCoord0);
	if (glShader->mAttribColor >= 0)
		bf_glEnableVertexAttribArray(glShader->mAttribColor);

	return glShader;
}

void GLRenderDevice::SetShader(Shader* shader)
{
	mShaderChanged = true;
	mCurShader = shader;
}

void GLRenderDevice::PhysSetAdditive(bool additive)
{
	if (additive)		
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);	
	else
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);	
	mCurAdditive = additive;
}

void GLRenderDevice::SetClip(float x, float y, float width, float height)
{
	//TODO: Store state in draw batcher
	mCurDrawLayer->Flush();
	
	NOT_IMPL;

	/*GL10_RECT rects[1];
	rects[0].left = (int) x;
	rects[0].right = (int) (x + width);
	rects[0].top = (int) y;
	rects[0].bottom = (int) (y + height);

	mGLDevice->RSSetScissorRects(1, rects);
	mGLDevice->RSSetState(mGLRasterizerStateClipped);*/
}

void GLRenderDevice::DisableClip()
{
	mCurDrawLayer->Flush();
	NOT_IMPL;
	//mGLDevice->RSSetState(mGLRasterizerStateUnclipped);
}

Texture* GLRenderDevice::CreateRenderTarget(int width, int height, bool destAlpha)
{
	NOT_IMPL;

	//IGL10ShaderResourceView* d3DShaderResourceView = NULL;
	//
	//int aWidth = 0;
	//int aHeight = 0;
	//
	//// Create the render target texture
	//GL10_TEXTURE2D_DESC desc;
	//ZeroMemory(&desc, sizeof(desc));
	//desc.Width = width;
	//desc.Height = height;
	//desc.MipLevels = 1;
	//desc.ArraySize = 1;
	//desc.Format = GLGI_FORMAT_R8G8B8A8_UNORM;
	//desc.SampleDesc.Count = 1;	
	//UINT qualityLevels = 0;

	//int samples = 1;
	//GLCHECK(mGLDevice->CheckMultisampleQualityLevels(GLGI_FORMAT_R8G8B8A8_UNORM, samples, &qualityLevels));

	//desc.SampleDesc.Count = samples;
	//desc.SampleDesc.Quality = qualityLevels-1;

	//desc.Usage = GL10_USAGE_DEFAULT;
	//desc.CPUAccessFlags = 0; //GL10_CPU_ACCESS_WRITE;
	//desc.BindFlags = GL10_BIND_SHADER_RESOURCE | GL10_BIND_RENDER_TARGET;

	//IGL10Texture2D* d3DTexture = NULL;
	//GLCHECK(mGLDevice->CreateTexture2D(&desc, NULL, &d3DTexture));

	//aWidth = width;
	//aHeight = height;

	//GL10_SHADER_RESOURCE_VIEW_DESC srDesc;
	//srDesc.Format = desc.Format;
	//srDesc.ViewDimension = GL10_SRV_DIMENSION_TEXTURE2D;
	//srDesc.Texture2D.MostDetailedMip = 0;
	//srDesc.Texture2D.MipLevels = 1;	
	//
	//if (qualityLevels != 0)
	//{
	//	srDesc.ViewDimension = GL10_SRV_DIMENSION_TEXTURE2DMS;
	//}

	//GLCHECK(mGLDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));
	//
	//IGL10RenderTargetView*	d3DRenderTargetView;	
	//GLCHECK(mGLDevice->CreateRenderTargetView(d3DTexture, NULL, &d3DRenderTargetView));

	//GLTexture* aRenderTarget = new GLTexture();
	//aRenderTarget->mWidth = width;
	//aRenderTarget->mHeight = height;
	//aRenderTarget->mRenderDevice = this;
	//aRenderTarget->mGLTexture = d3DShaderResourceView;
	//aRenderTarget->mGLRenderTargetView = d3DRenderTargetView;
	//aRenderTarget->AddRef();
	//return aRenderTarget;	
}
