#pragma once

#include "Common.h"
#include "util/Dictionary.h"

#ifdef BF_PLATFORM_OPENGL_ES2
#include <SDL2/SDL_opengles2.h>
#else
#include <SDL2/SDL_opengl.h>
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
	ImageData*				mImageData;
	//IGL10RenderTargetView*	mGLRenderTargetView;

public:
	GLTexture();
	~GLTexture();

	virtual void			PhysSetAsTarget();
	virtual void			Blt(ImageData* imageData, int x, int y) override;
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

typedef Dictionary<String, GLShaderParam*> GLShaderParamMap;

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

class GLSetTextureCmd : public RenderCmd
{
public:
	int						mTextureIdx;
	Texture*				mTexture;

public:
	virtual void Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};


class GLDrawBatch : public DrawBatch
{
public:
	//IGL10Buffer*			mGLBuffer;

public:
	GLDrawBatch();
	~GLDrawBatch();

	//virtual void			Lock();
	virtual void			Render(RenderDevice* renderDevice, RenderWindow* renderWindow) override;
};

class GLDrawLayer : public DrawLayer
{
public:
	virtual DrawBatch*		CreateDrawBatch();
	virtual RenderCmd*		CreateSetTextureCmd(int textureIdx, Texture* texture) override;
	virtual void			SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size) override;

public:
	GLDrawLayer();
	~GLDrawLayer();
};

class GLRenderWindow : public RenderWindow
{
public:
	SDL_Window*				mSDLWindow;
	GLRenderDevice*			mRenderDevice;
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

class GLRenderDevice : public RenderDevice
{
public:
	GLuint					mGLVAO;
	GLuint					mGLVertexBuffer;
	GLuint					mGLIndexBuffer;
	GLuint					mBlankTexture;
	GLShader*				mCurShader;

	bool					mHasVSync;

    GLDrawBatch*            mFreeBatchHead;

public:
	//virtual void			PhysSetAdditive(bool additive);
	//virtual void			PhysSetShader(Shader* shaderPass);
	virtual void			PhysSetRenderWindow(RenderWindow* renderWindow);
	virtual void			PhysSetRenderState(RenderState* renderState) override;
	virtual void			PhysSetRenderTarget(Texture* renderTarget) override;

public:
	GLRenderDevice();
	virtual ~GLRenderDevice();
	bool					Init(BFApp* app) override;

	void					FrameStart() override;
	void					FrameEnd() override;

	Texture*				LoadTexture(ImageData* imageData, int flags) override;
	Shader*					LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition) override;
	Texture*				CreateRenderTarget(int width, int height, bool destAlpha) override;

	virtual Texture*		CreateDynTexture(int width, int height) override { return NULL; }
	virtual void			SetRenderState(RenderState* renderState) override;

	/*void					SetShader(Shader* shader) override;
	virtual void			SetClip(float x, float y, float width, float height) override;
	virtual void			DisableClip() override;*/
};

NS_BF_END;
