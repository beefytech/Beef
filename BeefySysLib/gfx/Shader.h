#pragma once

#include "Common.h"

NS_BF_BEGIN;

class Texture;
class Shader;

class ShaderParam
{
public:
	virtual ~ShaderParam() {}

	virtual void			SetTexture(Texture* texture) = 0;	
	virtual void			SetFloat2(float x, float y) { SetFloat4(x, y, 0, 1); }
	virtual void			SetFloat3(float x, float y, float z) { SetFloat4(x, y, z, 1); }
	virtual void			SetFloat4(float x, float y, float z, float w) = 0;
};

class Shader
{
public:
	ShaderParam*			mTextureParam;
	int						mLastResizeCount;	
	int						mVertexSize;

public:
	virtual void			Init();

public:
	Shader();
	virtual ~Shader();
	
	virtual ShaderParam*	GetShaderParam(const StringImpl& name) = 0;
};

NS_BF_END;
