#pragma once

#ifndef BF_NO_FBX

#define FBXSDK_SHARED

#include "Common.h"
#include "fbxsdk.h"
#include "Util/Vector.h"
#include "Util/Matrix4.h"
#include "Util/Quaternion.h"

NS_BF_BEGIN;

#define BF_MAX_NUM_BONES 256

#define BF_MODEL_VERSION 1

struct FBXBoneWeight
{
public:
	int mBoneIdx;
	float mBoneWeight;
};

typedef std::vector<FBXBoneWeight> BoneWeightVector;

class FBXVertexData
{
public:
	Vector3 mCoords;
	uint32 mColor;
	std::vector<TexCoords> mTexCoords;
	Vector3 mNormal;
	std::vector<TexCoords> mBumpTexCoords;
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
	std::vector<FBXVertexData> mVertexData;
	std::vector<int> mIndexData;
};

struct FBXJoint
{
	String name;
	int id;
	FbxNode *pNode;
	FbxAMatrix globalBindPose, localBindPose;
	FbxAMatrix bindPose;

	Matrix4 mCurMatrix;

	float mBoneLength; // Length to parent

	int parentIndex;
	double posx, posy, posz;
	//double angle;
	//double axisx,axisy,axisz;
	double quatw, quatx, quaty, quatz;
	float scalex, scaley, scalez;
	bool bInheritScale;

	// Used when loading from an Ogre Skeleton.
	String parentName;
};

enum FBXTrackType
{ 
	TT_SKELETON, 
	TT_MORPH, 
	TT_POSE 
};

enum FBXTarget
{ 
	T_MESH, 
	T_SUBMESH 
};

struct FBXVertexPosition
{
	float x, y, z;
};

struct FBXVertexPoseRef
{
	int poseIndex;
	float poseWeight;
};

struct FBXVertexKeyframe
{
	float time;
	std::vector<FBXVertexPosition> positions;
	std::vector<FBXVertexPoseRef> poserefs;
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
	FBXTrackType mType;
	FBXTarget mTarget;
	int mIndex;
	String mBone;
	std::vector<FBXVertexKeyframe> mVertexKeyframes;
	std::vector<FBXSkeletonKeyframe> mSkeletonKeyframes;

public:
	FBXTrack()
	{
		Clear();
	}

	void Clear()
	{
		mType = TT_SKELETON;
		mTarget = T_MESH;
		mIndex = 0;
		mBone = "";
		mVertexKeyframes.clear();
		mSkeletonKeyframes.clear();
	}
};

class FBXAnimation
{
public:
	//public members
	String mName;
	float mLength;
	std::vector<FBXTrack> mTracks;
};

class ModelDef;

class FBXReader
{
public:
	ModelDef* mModelDef;

	FbxManager* mFBXManager;
	FbxScene* mFBXScene;
	std::vector<FBXMesh*> mMeshes;
	std::map<String, int> mJointIndexMap;
	std::vector<FBXJoint> mFBXJoints;	
	std::vector<FBXAnimation> mAnimations;

	int mParamBindframe;
	float mParamLum;
	float mFPS;
	
	float mFrameRate;
	float mAnimStart;
	float mAnimStop;	

protected:
	FBXMesh* LoadMesh(FbxNode* fbxNode, FbxMesh* fbxMesh);
	void TranslateNode(FbxNode* fbxNode);

public:
	FBXReader(ModelDef* modelDef);
	~FBXReader();

	bool WriteBFFile(const StringImpl& fileName, const StringImpl& checkFile, const StringImpl& checkFile2);
	bool ReadBFFile(const StringImpl& fileName);

	bool ReadFile(const StringImpl& fileName, bool loadAnims = true);
	bool FBXLoadJoint(FbxNode* pNode, FbxAMatrix globalBindPose);
	int FBXGetJointIndex(FbxNode* pNode);
	bool FBXLoadClip(String clipName, float start, float stop, float rate);
	bool FBXLoadClipAnim(String clipName, float start, float stop, float rate, FBXAnimation& a);
	FBXSkeletonKeyframe FBXLoadKeyframe(FBXJoint& j, float time);
	FbxAMatrix CalculateGlobalTransformWithBind(FbxNode* pNode, FbxTime time);
	void GetAnimationBounds();	
	void CalculateLocalTransforms(FbxNode* pRootNode);
	void ComputeBindPoseBoundingBox();
	void SetParentIndexes();
	void SortAndPruneJoints();
	void SortJoint(FBXJoint j, std::map<String, int> &sortedJointIndexMap, std::vector<FBXJoint>& sorted_joints);
	void AddParentsOfExistingJoints();
	void LoadBindPose();
	bool GetVertexBoneWeights(FbxNode* pNode, FbxMesh *pMesh, std::vector<BoneWeightVector>& boneWeightsVector);
	FbxAMatrix GetBindPose(FbxNode *pNode, FbxMesh *pMesh);
	void FindJoints(FbxNode* fbxNode);
};

NS_BF_END;

#endif