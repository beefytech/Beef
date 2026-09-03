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

class ComputeShader
{
public:
	virtual ~ComputeShader() {}
};

enum ShaderFlags
{
	ShaderFlags_None = 0,
	ShaderFlags_NoOptimization = 1, // maps to the backend's skip-optimization compile flag
};

class Shader
{
public:
	ShaderParam*			mTextureParam;
	int						mLastResizeCount;
	int						mVertexSize;
	// Non-empty = compilation failed and this shader must not be drawn with; LoadShader returns
	// the object anyway so the caller can read the error (Gfx_GetShaderError).
	String					mCompileError;

public:
	virtual void			Init();

public:
	Shader();
	virtual ~Shader();
	
	virtual ShaderParam*	GetShaderParam(const StringImpl& name) = 0;
};

NS_BF_END;
