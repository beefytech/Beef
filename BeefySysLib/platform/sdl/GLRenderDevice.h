#pragma once

#include "Common.h"

#ifdef BF_PLATFORM_OPENGL_ES2
#include "SDL_opengles2.h"
#else
#include "SDL_opengl.h"
#endif

#include "gfx/Shader.h"
#include "gfx/Texture.h"
#include "gfx/RenderDevice.h"
#include "gfx/DrawLayer.h"

struct SDL_Window;

NS_BF_BEGIN;

class BFApp;
class GLRenderDevice;

class GLTexture : public Texture
{
public:
	GLRenderDevice*			mRenderDevice;
	GLuint					mGLTexture;
	GLuint					mGLTexture2;
	//IGL10RenderTargetView*	mGLRenderTargetView;

public:
	GLTexture();
	~GLTexture();

	virtual void			PhysSetAsTarget();
};

class GLShaderParam : public ShaderParam
{
public:
	GLint					mGLVariable;

public:
	GLShaderParam();
	~GLShaderParam();

	virtual void			SetTexture(Texture* texture);
	virtual void			SetFloat4(float x, float y, float z, float w) override;
};

typedef std::map<String, GLShaderParam*> GLShaderParamMap;

class GLShader : public Shader
{
public:
	//IGL10Effect*			mGLEffect;

	GLuint mGLVertexShader;
	GLuint mGLFragmentShader;
	GLuint mGLProgram;
	GLint mAttribPosition;
	GLint mAttribTexCoord0;
	GLint mAttribColor;
	GLint mAttribTex0;
	GLint mAttribTex1;
	
	GLShaderParamMap		mParamsMap;	

public:
	GLShader();
	~GLShader();
	
	virtual ShaderParam*	GetShaderParam(const StringImpl& name) override;
};

class GLDrawBatch : public DrawBatch
{
public:
	//IGL10Buffer*			mGLBuffer;

public:
	GLDrawBatch(int minVtxSize = 0, int minIdxSize = 0);
	~GLDrawBatch();

	virtual void			Lock();
	virtual void			Draw();
};

class GLDrawLayer : public DrawLayer
{
public:
	virtual DrawBatch*		CreateDrawBatch();
	virtual DrawBatch*		AllocateBatch(int minVtxCount, int minIdxCount) override;
	virtual void			FreeBatch(DrawBatch* drawBatch) override;

public:
	GLDrawLayer();
	~GLDrawLayer();
};

class GLRenderWindow : public RenderWindow
{
public:
	SDL_Window*				mSDLWindow;
	GLRenderDevice*			mRenderDevice;
	//IGLGISwapChain*			mGLSwapChain;
	//IGL10Texture2D*		mGLBackBuffer;
	//IGL10RenderTargetView*	mGLRenderTargetView;
	bool					mResizePending;
	int						mPendingWidth;
	int						mPendingHeight;		

public:
	virtual void			PhysSetAsTarget();

public:
	GLRenderWindow(GLRenderDevice* renderDevice, SDL_Window* sdlWindow);
	~GLRenderWindow();

	void					SetAsTarget() override;
	void					Resized() override;
	virtual void			Present() override;

	void					CopyBitsTo(uint32* dest, int width, int height);
};

typedef std::vector<GLDrawBatch*> GLDrawBatchVector;

class GLRenderDevice : public RenderDevice
{
public:
	//IGLGIFactory*			mGLGIFactory;
	//IGL10Device*			mGLDevice;
	//IGL10BlendState*		mGLNormalBlendState;
	//IGL10BlendState*		mGLAdditiveBlendState;
	//IGL10RasterizerState*	mGLRasterizerStateClipped;
	//IGL10RasterizerState*	mGLRasterizerStateUnclipped;

	GLuint					mGLVertexBuffer;
	GLuint					mGLIndexBuffer;
	GLuint					mBlankTexture;

	bool					mHasVSync;

	GLDrawBatchVector		mDrawBatchPool;
    GLDrawBatch*            mFreeBatchHead;

public:
	virtual void			PhysSetAdditive(bool additive);
	virtual void			PhysSetShader(Shader* shaderPass);
	virtual void			PhysSetRenderWindow(RenderWindow* renderWindow);
	virtual void			PhysSetRenderTarget(Texture* renderTarget);

public:
	GLRenderDevice();
	virtual ~GLRenderDevice();
	bool					Init(BFApp* app) override;

	void					FrameStart() override;
	void					FrameEnd() override;

	Texture*				LoadTexture(ImageData* imageData, bool additive) override;
	Shader*					LoadShader(const StringImpl& fileName) override;
	Texture*				CreateRenderTarget(int width, int height, bool destAlpha) override;

	void					SetShader(Shader* shader) override;
	virtual void			SetClip(float x, float y, float width, float height) override;
	virtual void			DisableClip() override;
};

NS_BF_END;
