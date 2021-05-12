#pragma once

#include "Common.h"
#include "util/Quaternion.h"
#include "util/Vector.h"
#include "util/Array.h"
#include "gfx/Texture.h"
#include <vector>

NS_BF_BEGIN;

class ModelJointTranslation
{
public:
	Quaternion mQuat;
	Vector3 mScale;
	Vector3 mTrans;
};

class ModelAnimationFrame
{
public:
	Array<ModelJointTranslation> mJointTranslations;
};

class ModelAnimation
{
public:
	String mName;
	Array<ModelAnimationFrame> mFrames;

public:
	void GetJointTranslation(int jointIdx, float frameNum, ModelJointTranslation* outJointTranslation);
};

#define MODEL_MAX_BONE_WEIGHTS 8

class ModelVertex
{
public:
	Vector3 mPosition;
	uint32 mColor;
	TexCoords mTexCoords;
	TexCoords mBumpTexCoords;
	Vector3 mNormal;
	Vector3 mTangent;
	int mNumBoneWeights;
	int mBoneIndices[MODEL_MAX_BONE_WEIGHTS];
	float mBoneWeights[MODEL_MAX_BONE_WEIGHTS];
};

class ModelJoint
{
public:
	String mName;
	int mParentIdx;
	Matrix4 mPoseInvMatrix;
};

class ModelMetalicRoughness
{
public:
	Vector3 mBaseColorFactor;
	float mMetallicFactor;
	float mRoughnessFactor;

public:
	ModelMetalicRoughness()
	{
		mMetallicFactor = 0;
		mRoughnessFactor = 0;
	}
};

class ModelMaterialDef
{
public:
	class TextureParameterValue	
	{
	public:
		String mName;
		String mTexturePath;

	public:
		TextureParameterValue()
		{
		
		}

		~TextureParameterValue()
		{
			
		}
	};

public:
	String mName;
	int mRefCount;
	bool mInitialized;
	OwnedArray<TextureParameterValue> mTextureParameterValues;

public:
	ModelMaterialDef()
	{
		mRefCount = 0;
		mInitialized = false;
	}

	static ModelMaterialDef* CreateOrGet(const StringImpl& prefix, const StringImpl& path);
};

class ModelMaterialInstance
{
public:
	ModelMaterialDef* mDef;
	String mName;
	ModelMetalicRoughness mModelMetalicRoughness;
};

class ModelPrimitives
{
public:
	enum Flags
	{
		Flags_None = 0,
		Flags_Vertex_Position = 1,	
		Flags_Vertex_Tex0 = 2,
		Flags_Vertex_Tex1 = 4,
		Flags_Vertex_Tex2 = 8,
		Flags_Vertex_Color = 0x10,
		Flags_Vertex_Normal = 0x20,
		Flags_Vertex_Tangent = 0x40,
	};

public:
	Array<ModelVertex> mVertices;
	Array<uint16> mIndices;
	ModelMaterialInstance* mMaterial;
	Array<String> mTexPaths;	
	Flags mFlags;

public:
	ModelPrimitives()
	{
		mMaterial = NULL;
		mFlags = Flags_None;
	}
};

class ModelMesh
{
public:
	String mName;	
	//String mTexFileName;
	//String mBumpFileName;	
	Array<ModelPrimitives> mPrimitives;
};

class ModelNode
{
public:
	String mName;
	Vector3 mTranslation;
	Vector4 mRotation;
	ModelMesh* mMesh;
	Array<ModelNode*> mChildren;
};

class ModelDef
{
public:
	String mLoadDir;
	float mFrameRate;
	Array<ModelMesh> mMeshes;
	Array<ModelJoint> mJoints;
	Array<ModelAnimation> mAnims;
	Array<ModelNode> mNodes;
	Array<ModelMaterialInstance> mMaterials;

public:
	~ModelDef();
};

NS_BF_END;
