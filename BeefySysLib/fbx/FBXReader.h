#pragma once

#ifndef BF_NO_FBX

#include "Common.h"
#include "util/Dictionary.h"
#include "util/Vector.h"
#include "util/Matrix4.h"
#include "util/Quaternion.h"
#include "util/Hash.h"
#include <vector>
#include <map>
#include <unordered_map>

NS_BF_BEGIN;

#define BF_MAX_NUM_BONES 256

#define BF_MODEL_VERSION 1

struct FBXBoneWeight
{
public:
	int mBoneIdx;
	float mBoneWeight;
};

typedef Array<FBXBoneWeight> BoneWeightVector;

class FBXVertexData
{
public:
	Vector3 mCoords;
	uint32 mColor;
	Array<TexCoords> mTexCoords;
	Vector3 mNormal;
	Array<TexCoords> mBumpTexCoords;
	Vector3 mTangent;
	BoneWeightVector mBoneWeights;

	bool operator==(const FBXVertexData& check) const
	{
		if (mCoords != check.mCoords)
			return false;
		if (mNormal != check.mNormal)
			return false;

		if (mTexCoords.size() != check.mTexCoords.size())
			return false;

		for (int i = 0; i < (int)mTexCoords.size(); i++)
			if ((mTexCoords[i].mU != check.mTexCoords[i].mU) ||
				(mTexCoords[i].mV != check.mTexCoords[i].mV))
				return false;

		return true;
	}
};

class FBXMaterial
{
public:
	String mTexFileName;
	String mBumpFileName;
};

class FBXMesh
{
public:
	FBXMaterial mMaterial;
	String mName;
	Array<FBXVertexData> mVertexData;
	Array<int> mIndexData;
};

struct FBXJoint
{
	String name;
	int id;
	int parentIndex;
	String parentName;

	// Global bind pose and its inverse (stored as Matrix4)
	Matrix4 mGlobalBindPoseInv;

	// Local bind pose TRS
	double posx, posy, posz;
	double quatw, quatx, quaty, quatz;
	float scalex, scaley, scalez;
	bool bInheritScale;

	float mBoneLength;
};

struct FBXSkeletonKeyframe
{
	float time;
	double tx, ty, tz;
	double quat_w, quat_x, quat_y, quat_z;
	float sx, sy, sz;
};

class FBXTrack
{
public:
	String mBone;
	Array<FBXSkeletonKeyframe> mSkeletonKeyframes;
};

class FBXAnimation
{
public:
	String mName;
	float mLength;
	std::vector<FBXTrack> mTracks;
};

class ModelDef;

class FBXReader
{
public:
	ModelDef* mModelDef;

	Array<FBXMesh*> mMeshes;
	Dictionary<String, int> mJointIndexMap;
	Array<FBXJoint> mFBXJoints;
	Array<FBXAnimation> mAnimations;

	float mFPS;

public:
	FBXReader(ModelDef* modelDef);
	~FBXReader();

	bool WriteBFFile(const StringImpl& fileName, const StringImpl& checkFile, const StringImpl& checkFile2);
	bool ReadBFFile(const StringImpl& fileName);
	bool ReadFile(const StringImpl& fileName, bool loadAnims = true);
};

NS_BF_END;

namespace std
{
	template <>
	struct hash<Beefy::FBXVertexData>
	{
		size_t operator()(const Beefy::FBXVertexData& val) const
		{
			auto hash = Beefy::Hash64(&val.mCoords, 3 * sizeof(float));
			hash = Beefy::Hash64(&val.mNormal, 3 * sizeof(float), hash);
			return hash;
		}
	};
}

#endif
