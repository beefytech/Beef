#pragma once

#include "Common.h"
#include "util/Quaternion.h"
#include "util/Vector.h"
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
	std::vector<ModelJointTranslation> mJointTranslations;
};

class ModelAnimation
{
public:
	String mName;
	std::vector<ModelAnimationFrame> mFrames;

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

class ModelMesh
{
public:
	String mName;
	std::vector<ModelVertex> mVertices;
	std::vector<uint16> mIndices;
	String mTexFileName;
	String mBumpFileName;
};

class ModelDef
{
public:
	String mLoadDir;
	float mFrameRate;
	std::vector<ModelMesh> mMeshes;
	std::vector<ModelJoint> mJoints;
	std::vector<ModelAnimation> mAnims;
};

NS_BF_END;
