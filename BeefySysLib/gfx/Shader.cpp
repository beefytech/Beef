#include "Shader.h"

USING_NS_BF;

Shader::Shader()
{
	mLastResizeCount = -1;
}

Shader::~Shader()
{
	mTextureParam = NULL;
}

void Shader::Init()
{
	mTextureParam = GetShaderParam("tex2D");
}