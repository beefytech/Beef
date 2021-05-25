#include "ModelDef.h"
#include "BFApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/ModelInstance.h"
#include "util/Dictionary.h"
#include "util/TLSingleton.h"
#include "FileStream.h"
#include "MemStream.h"
#include "util/MathUtils.h"
#include "util/Sphere.h"
#include "util/HashSet.h"
#include "util/BeefPerf.h"

#pragma warning(disable:4190)

USING_NS_BF;

struct ModelManager
{
public:
	Dictionary<String, ModelMaterialDef*> mMaterialMap;	
};

ModelManager sModelManager;
static TLSingleton<String> gModelDef_TLStrReturn;

void Beefy::ModelAnimation::GetJointTranslation(int jointIdx, float frameNum, ModelJointTranslation* outJointTranslation)
{
	// Frame 35
	BF_ASSERT((int)frameNum < (int)mFrames.size());
	int frameNumStart = (int)frameNum;
	int frameNumEnd = (frameNumStart + 1) % (int)mFrames.size();

	float endAlpha = frameNum - frameNumStart;
	float startAlpha = 1.0f - endAlpha;

	ModelJointTranslation* jointTransStart = &(mFrames[frameNumStart].mJointTranslations[jointIdx]);
	ModelJointTranslation* jointTransEnd = &(mFrames[frameNumEnd].mJointTranslations[jointIdx]);

	//if (/*(jointIdx == 37) || (jointIdx == 36) || (jointIdx == 35) ||*/ (jointIdx == 34) /*|| (jointIdx == 12) || (jointIdx == 11) || (jointIdx == 10) || (jointIdx == 0)*/)
	{		
		outJointTranslation->mQuat = Quaternion::Slerp(endAlpha, jointTransStart->mQuat, jointTransEnd->mQuat, true);
		outJointTranslation->mScale = (jointTransStart->mScale * startAlpha) + (jointTransEnd->mScale * endAlpha);
		outJointTranslation->mTrans = (jointTransStart->mTrans * startAlpha) + (jointTransEnd->mTrans * endAlpha);
	}
	/*else
	{
		*outJointTranslation = *jointTransStart;
	}*/

	//*outJointTranslation = *jointTransStart;
}


//

BF_EXPORT ModelInstance* BF_CALLTYPE ModelDef_CreateModelInstance(ModelDef* modelDef, ModelCreateFlags flags)
{
	return gBFApp->mRenderDevice->CreateModelInstance(modelDef, flags);
}

BF_EXPORT void ModelDef_Compact(ModelDef* modelDef)
{
	modelDef->Compact();
}

BF_EXPORT void ModelDef_GetBounds(ModelDef* modelDef, Vector3& min, Vector3& max)
{
	modelDef->GetBounds(min, max);
}

BF_EXPORT void ModelDef_SetBaseDir(ModelDef* modelDef, char* baseDIr)
{
	modelDef->mLoadDir = baseDIr;
}

BF_EXPORT const char* BF_CALLTYPE ModelDef_GetInfo(ModelDef* modelDef)
{
	String& outString = *gModelDef_TLStrReturn.Get();
	outString.Clear();
	for (int meshIdx = 0; meshIdx < (int)modelDef->mMeshes.mSize; meshIdx++)	
	{
		auto mesh = modelDef->mMeshes[meshIdx];
		for (int primIdx = 0; primIdx < (int)mesh.mPrimitives.mSize; primIdx++)		
		{
			auto prims = mesh.mPrimitives[primIdx];
			outString += StrFormat("%d\t%d\t%d\t%d", meshIdx, primIdx, prims.mIndices.size(), prims.mVertices.size());
			for (auto& texPath : prims.mTexPaths)
			{
				outString += "\t";
				outString += texPath;
			}

			outString += "\n";
		}
	}
	return outString.c_str();
}

BF_EXPORT float BF_CALLTYPE ModelDef_GetFrameRate(ModelDef* modelDef)
{
	return modelDef->mFrameRate;
}

BF_EXPORT int BF_CALLTYPE ModelDef_GetJointCount(ModelDef* modelDef)
{
	return (int)modelDef->mJoints.size();
}

BF_EXPORT int BF_CALLTYPE ModelDef_GetAnimCount(ModelDef* modelDef)
{
	return (int)modelDef->mAnims.size();
}

BF_EXPORT ModelAnimation* BF_CALLTYPE ModelDef_GetAnimation(ModelDef* modelDef, int animIdx)
{
	return &modelDef->mAnims[animIdx];
}

BF_EXPORT void BF_CALLTYPE ModelDef_SetTextures(ModelDef* modelDef, int32 meshIdx, int32 primitivesIdx, char** paths, int32 pathCount)
{
	auto& prims = modelDef->mMeshes[meshIdx].mPrimitives[primitivesIdx];
	prims.mTexPaths.Clear();
	for (int i = 0; i < pathCount; i++)
		prims.mTexPaths.Add(paths[i]);
}

BF_EXPORT bool BF_CALLTYPE ModelDef_RayIntersect(ModelDef* modelDef, const Matrix4& worldMtx, const Vector3& origin, const Vector3& vec, Vector3& outIntersect, float& outDistance)
{
	return modelDef->RayIntersect(worldMtx, origin, vec, outIntersect, outDistance);
}

BF_EXPORT void BF_CALLTYPE ModelDefAnimation_GetJointTranslation(ModelAnimation* modelAnimation, int jointIdx, float frame, ModelJointTranslation* outJointTranslation)
{
	modelAnimation->GetJointTranslation(jointIdx, frame, outJointTranslation);
}

BF_EXPORT int BF_CALLTYPE ModelDefAnimation_GetFrameCount(ModelAnimation* modelAnimation)
{
	return (int)modelAnimation->mFrames.size();
}

BF_EXPORT const char* BF_CALLTYPE ModelDefAnimation_GetName(ModelAnimation* modelAnimation)
{
	return modelAnimation->mName.c_str();
}

BF_EXPORT void BF_CALLTYPE ModelDefAnimation_Clip(ModelAnimation* modelAnimation, int startFrame, int numFrames)
{	
	modelAnimation->mFrames.RemoveRange(0, startFrame);
	modelAnimation->mFrames.RemoveRange(numFrames, modelAnimation->mFrames.Count() - numFrames);
}

ModelDef::ModelDef()
{
	mFlags = Flags_None;
}

ModelDef::~ModelDef()
{
	for (auto& materialInstance : mMaterials)
	{
		materialInstance.mDef->mRefCount--;
	}
}

void ModelDef::Compact()
{
	for (auto& mesh : mMeshes)
	{
		for (auto& prims : mesh.mPrimitives)
		{
			Array<int> vtxMap;
			vtxMap.Insert(0, -1, prims.mVertices.mSize);			
			int newVtxIdx = 0;

			for (uint16 vtxIdx : prims.mIndices)
			{
				if (vtxMap[vtxIdx] == -1)				
					vtxMap[vtxIdx] = newVtxIdx++;
			}

			if (newVtxIdx >= prims.mVertices.mSize)
				continue;

			for (uint16& vtxIdx : prims.mIndices)
				vtxIdx = vtxMap[vtxIdx];

			Array<ModelVertex> oldVertices = prims.mVertices;
			prims.mVertices.Resize(newVtxIdx);
			for (int oldVtxIdx = 0; oldVtxIdx < oldVertices.mSize; oldVtxIdx++)
			{
				int newVtxIdx = vtxMap[oldVtxIdx];
				if (newVtxIdx != -1)
					prims.mVertices[newVtxIdx] = oldVertices[oldVtxIdx];
			}
		}
	}	
}

void ModelDef::CalcBounds()
{
	int vtxCount = 0;

	for (auto& mesh : mMeshes)
	{
		for (auto& prims : mesh.mPrimitives)
		{
			for (auto& vtx : prims.mVertices)
			{
				if (vtxCount == 0)
				{
					mBounds.mMin = vtx.mPosition;
					mBounds.mMax = vtx.mPosition;
				}
				else
				{
					mBounds.mMin.mX = BF_MIN(mBounds.mMin.mX, vtx.mPosition.mX);
					mBounds.mMin.mY = BF_MIN(mBounds.mMin.mY, vtx.mPosition.mY);
					mBounds.mMin.mZ = BF_MIN(mBounds.mMin.mZ, vtx.mPosition.mZ);

					mBounds.mMax.mX = BF_MAX(mBounds.mMax.mX, vtx.mPosition.mX);
					mBounds.mMax.mY = BF_MAX(mBounds.mMax.mY, vtx.mPosition.mY);
					mBounds.mMax.mZ = BF_MAX(mBounds.mMax.mZ, vtx.mPosition.mZ);
				}

				vtxCount++;
			}
		}
	}

	mFlags = (Flags)(mFlags | Flags_HasBounds);
}

void ModelDef::GetBounds(Vector3& min, Vector3& max)
{
	if ((mFlags & Flags_HasBounds) == 0)
		CalcBounds();

	min = mBounds.mMin;
	max = mBounds.mMax;	
}

#define SWAP(x, y) { auto temp = x; x = y; y = temp; }
#define N (sizeof(A)/sizeof(A[0]))

// Partition using Lomuto partition scheme
static int partition(float a[], int left, int right, int pIndex)
{
	// pick `pIndex` as a pivot from the array
	float pivot = a[pIndex];

	// Move pivot to end
	SWAP(a[pIndex], a[right]);

	// elements less than the pivot will be pushed to the left of `pIndex`;
	// elements more than the pivot will be pushed to the right of `pIndex`;
	// equal elements can go either way
	pIndex = left;

	// each time we find an element less than or equal to the pivot, `pIndex`
	// is incremented, and that element would be placed before the pivot.
	for (int i = left; i < right; i++)
	{
		if (a[i] <= pivot)
		{
			SWAP(a[i], a[pIndex]);
			pIndex++;
		}
	}

	// move pivot to its final place
	SWAP(a[pIndex], a[right]);

	// return `pIndex` (index of the pivot element)
	return pIndex;
}

// Returns the k'th smallest element in the list within `left…right`
// (i.e., `left <= k <= right`). The search space within the array is
// changing for each round – but the list is still the same size.
// Thus, `k` does not need to be updated with each round.
static float quickselect(float A[], int left, int right, int k)
{
	// If the array contains only one element, return that element
	if (left == right) {
		return A[left];
	}

	// select `pIndex` between left and right
	int pIndex = left + rand() % (right - left + 1);

	pIndex = partition(A, left, right, pIndex);

	// The pivot is in its final sorted position
	if (k == pIndex) {
		return A[k];
	}

	// if `k` is less than the pivot index
	else if (k < pIndex) {
		return quickselect(A, left, pIndex - 1, k);
	}

	// if `k` is more than the pivot index
	else {
		return quickselect(A, pIndex + 1, right, k);
	}
}

void ModelDef::GenerateCollisionData()
{
	BP_ZONE("ModelDef::GenerateCollisionData");

	mFlags = (Flags)(mFlags | Flags_HasBVH);

	int statsWorkItrs = 0;
	int statsWorkTris = 0;

	BF_ASSERT(mBVNodes.IsEmpty());
			
	struct _WorkEntry
	{
		int mParentNodeIdx;		
		int mTriWorkIdx;
		int mTriWorkCount;		
	};

	Array<int32> triWorkList;
	
	int triCount = 0;
	for (auto& mesh : mMeshes)
	{
		for (auto& prims : mesh.mPrimitives)
		{
			int startIdx = mBVIndices.mSize;
			triCount += prims.mIndices.mSize / 3;
			triWorkList.Reserve(triWorkList.mSize + triCount);
			mBVIndices.Reserve(mBVIndices.mSize + prims.mIndices.mSize);
			mBVVertices.Reserve(mBVVertices.mSize + prims.mVertices.mSize);

			for (int triIdx = 0; triIdx < triCount; triIdx++)
				triWorkList.Add(startIdx / 3 + triIdx);

			for (auto idx : prims.mIndices)
			{
				mBVIndices.Add(idx + startIdx);
			}

			for (auto& vtx : prims.mVertices)
				mBVVertices.Add(vtx.mPosition);
		}
	}

	Array<_WorkEntry> workList;
	
 	_WorkEntry workEntry;
	workEntry.mParentNodeIdx = -1; 	
	workEntry.mTriWorkIdx = 0;
	workEntry.mTriWorkCount = triWorkList.mSize;
	workList.Add(workEntry);

	Array<Vector3> points;
	points.Reserve(triWorkList.mSize);	
	Array<float> centers;
	centers.Reserve(triWorkList.mSize);
	Array<int> left;
	left.Reserve(triWorkList.mSize);
	Array<int> right;
	right.Reserve(triWorkList.mSize);	
	
	mBVTris.Reserve(triWorkList.mSize * 2);

	while (!workList.IsEmpty())
	{
		auto workEntry = workList.back();
		workList.pop_back();

		statsWorkItrs++;
		statsWorkTris += workEntry.mTriWorkCount;
		
		centers.Clear();
		left.Clear();
		right.Clear();

		int nodeIdx = mBVNodes.mSize;
		mBVNodes.Add(ModelBVNode());

		if (workEntry.mParentNodeIdx != -1)
		{
			auto& bvParent = mBVNodes[workEntry.mParentNodeIdx];
			if (bvParent.mLeft == -1)
				bvParent.mLeft = nodeIdx;
			else
			{
				BF_ASSERT(bvParent.mRight == -1);
				bvParent.mRight = nodeIdx;
			}
		}

		for (int triIdxIdx = 0; triIdxIdx < workEntry.mTriWorkCount; triIdxIdx++)
		{
			bool inLeft = false;
			bool inRight = false;

			int triIdx = triWorkList.mVals[workEntry.mTriWorkIdx + triIdxIdx];
			for (int triVtxIdx = 0; triVtxIdx < 3; triVtxIdx++)
			{
				int vtxIdx = mBVIndices.mVals[triIdx * 3 + triVtxIdx];
				bool isNewVtx = false;

				int32* valuePtr = NULL;				
				auto& vtx = mBVVertices[vtxIdx];

				auto& bvNode = mBVNodes[nodeIdx];
				if ((triIdxIdx == 0) && (triVtxIdx == 0))
				{
					bvNode.mBoundAABB.mMin = vtx;
					bvNode.mBoundAABB.mMax = vtx;
				}
				else
				{
					bvNode.mBoundAABB.mMin.mX = BF_MIN(bvNode.mBoundAABB.mMin.mX, vtx.mX);
					bvNode.mBoundAABB.mMin.mY = BF_MIN(bvNode.mBoundAABB.mMin.mY, vtx.mY);
					bvNode.mBoundAABB.mMin.mZ = BF_MIN(bvNode.mBoundAABB.mMin.mZ, vtx.mZ);

					bvNode.mBoundAABB.mMax.mX = BF_MAX(bvNode.mBoundAABB.mMax.mX, vtx.mX);
					bvNode.mBoundAABB.mMax.mY = BF_MAX(bvNode.mBoundAABB.mMax.mY, vtx.mY);
					bvNode.mBoundAABB.mMax.mZ = BF_MAX(bvNode.mBoundAABB.mMax.mZ, vtx.mZ);
				}				
			}
		}

		//mBVNodes[nodeIdx].mBoundSphere = Sphere::MiniBall(points.mVals, points.mSize);

		bool didSplit = false;

		if (workEntry.mTriWorkCount > 4)
		{
			int splitPlane = 0;
			float splitWidth = 0;

			// Split along widest AABB dimension
			for (int dimIdx = 0; dimIdx < 3; dimIdx++)
			{
				float minVal = 0;
				float maxVal = 0;

				for (int triIdxIdx = 0; triIdxIdx < workEntry.mTriWorkCount; triIdxIdx++)
				{
					int triIdx = triWorkList.mVals[workEntry.mTriWorkIdx + triIdxIdx];
					for (int triVtxIdx = 0; triVtxIdx < 3; triVtxIdx++)
					{
						int vtxIdx = mBVIndices.mVals[triIdx * 3 + triVtxIdx];
						const Vector3& vtx = mBVVertices.mVals[vtxIdx];

						float coord = ((float*)&vtx)[dimIdx];
						if ((triIdxIdx == 0) && (triVtxIdx == 0))
						{
							minVal = coord;
							maxVal = coord;
						}
						else
						{
							minVal = BF_MIN(minVal, coord);
							maxVal = BF_MAX(maxVal, coord);
						}
					}
				}

				float width = maxVal - minVal;
				if (width > splitWidth)
				{
					splitPlane = dimIdx;
					splitWidth = width;
				}
			}						

			centers.SetSize(workEntry.mTriWorkCount);
			for (int triIdxIdx = 0; triIdxIdx < workEntry.mTriWorkCount; triIdxIdx++)
			{
				int triIdx = triWorkList.mVals[workEntry.mTriWorkIdx + triIdxIdx];
				float coordAcc = 0;
				for (int triVtxIdx = 0; triVtxIdx < 3; triVtxIdx++)
				{
					int vtxIdx = mBVIndices.mVals[triIdx * 3 + triVtxIdx];
					const Vector3& vtx = mBVVertices.mVals[vtxIdx];
					float coord = ((float*)&vtx)[splitPlane];
					coordAcc += coord;
				}

				float coordAvg = coordAcc / 3;
				centers.mVals[triIdxIdx] = coordAvg;
			}

			float centerCoord = quickselect(centers.mVals, 0, centers.mSize - 1, centers.mSize / 2);

// 			centers.Sort([](float lhs, float rhs) { return lhs < rhs; });
// 			centerCoord = centers[centers.mSize / 2];						

			for (int triIdxIdx = 0; triIdxIdx < workEntry.mTriWorkCount; triIdxIdx++)
			{
				bool inLeft = false;
				bool inRight = false;

				int triIdx = triWorkList.mVals[workEntry.mTriWorkIdx + triIdxIdx];
				for (int triVtxIdx = 0; triVtxIdx < 3; triVtxIdx++)
				{
					int vtxIdx = mBVIndices.mVals[triIdx * 3 + triVtxIdx];
					const Vector3& vtx = mBVVertices.mVals[vtxIdx];
					float coord = ((float*)&vtx)[splitPlane];

					if (coord < centerCoord)
						inLeft = true;
					else
						inRight = true;
				}

				if (inLeft)
					left.Add(triIdx);
				if (inRight)
					right.Add(triIdx);
			}

			// Don't split if the split didn't significantly separate the triangles
			bool doSplit =
				(left.mSize <= workEntry.mTriWorkCount * 0.85f) &&
				(right.mSize <= workEntry.mTriWorkCount * 0.85f);

			if (doSplit)
			{				
				mBVNodes[nodeIdx].mKind = ModelBVNode::Kind_Branch;
				mBVNodes[nodeIdx].mLeft = -1;
				mBVNodes[nodeIdx].mRight = -1;
						
				_WorkEntry childWorkEntry;
								
				childWorkEntry.mParentNodeIdx = nodeIdx;
				childWorkEntry.mTriWorkIdx = triWorkList.mSize;
				childWorkEntry.mTriWorkCount = right.mSize;
				workList.Add(childWorkEntry);
				triWorkList.Insert(triWorkList.mSize, right.mVals, right.mSize);											

				childWorkEntry.mParentNodeIdx = nodeIdx;
				childWorkEntry.mTriWorkIdx = triWorkList.mSize;
				childWorkEntry.mTriWorkCount = left.mSize;
				workList.Add(childWorkEntry);
				triWorkList.Insert(triWorkList.mSize, left.mVals, left.mSize);

				continue;
			}
		}

		// Did not split	
		int triStartIdx = mBVTris.mSize;
		//mBVTris.Reserve(mBVTris.mSize + workEntry.mTriWorkCount);
		for (int triIdxIdx = 0; triIdxIdx < workEntry.mTriWorkCount; triIdxIdx++)
			mBVTris.Add(triWorkList[workEntry.mTriWorkIdx + triIdxIdx]);

		auto& bvNode = mBVNodes[nodeIdx];
		bvNode.mKind = ModelBVNode::Kind_Leaf;
		bvNode.mTriStartIdx = triStartIdx;
		bvNode.mTriCount = workEntry.mTriWorkCount;
	}

	NOP;
}

void ModelDef::RayIntersect(ModelBVNode* bvNode, const Matrix4& worldMtx, const Vector3& origin, const Vector3& vec, Vector3& outIntersect, float& outDistance)
{	
// 	if (!RayIntersectsCircle(origin, vec, bvNode->mBoundSphere, NULL, NULL, NULL))
// 		return false;
	if (!RayIntersectsAABB(origin, vec, bvNode->mBoundAABB, NULL, NULL, NULL))
		return;

	if (bvNode->mKind == ModelBVNode::Kind_Branch)
	{
		bool hadIntersect = false;
		for (int branchIdx = 0; branchIdx < 2; branchIdx++)		
			RayIntersect(&mBVNodes[(branchIdx == 0) ? bvNode->mLeft : bvNode->mRight], worldMtx, origin, vec, outIntersect, outDistance);		
		return;
	}
	
	for (int triIdxIdx = 0; triIdxIdx < bvNode->mTriCount; triIdxIdx++)
	{
		int triIdx = mBVTris[bvNode->mTriStartIdx + triIdxIdx];

		Vector3 curIntersect;
		float curDistance;
		if (RayIntersectsTriangle(origin, vec, 
			mBVVertices[mBVIndices[triIdx*3+0]], mBVVertices[mBVIndices[triIdx*3+1]], mBVVertices[mBVIndices[triIdx*3+2]],
			&curIntersect, &curDistance))
		{
			if (curDistance < outDistance)
			{
				outIntersect = curIntersect;
				outDistance = curDistance;
			}
		}
	}		
}

bool ModelDef::RayIntersect(const Matrix4& worldMtx, const Vector3& origin, const Vector3& vec, Vector3& outIntersect, float& outDistance)
{
	if ((mFlags & Flags_HasBounds) == 0)
		CalcBounds();

	if (!RayIntersectsAABB(origin, vec, mBounds, NULL, NULL, NULL))
		return false;

	if (mBVNodes.IsEmpty())
		GenerateCollisionData();

	const float maxDist = 3.40282e+038f;
	outDistance = maxDist;
	RayIntersect(&mBVNodes[0], worldMtx, origin, vec, outIntersect, outDistance);	
	return outDistance != maxDist;
}

ModelMaterialDef* ModelMaterialDef::CreateOrGet(const StringImpl& prefix, const StringImpl& path)
{	
	StringT<128> key = prefix;
	key += ":";
	key += path;
	ModelMaterialDef** modelMaterialDefPtr = NULL;
	if (sModelManager.mMaterialMap.TryAdd(key, NULL, &modelMaterialDefPtr))
		*modelMaterialDefPtr = new ModelMaterialDef();
	return *modelMaterialDefPtr;
}


///

class ModelReader
{
public:
	ModelDef* mModelDef;
	DataStream* mFS;

public:
	ModelReader(ModelDef* modelDef)
	{
		mFS = NULL;
		mModelDef = modelDef;
	}

	~ModelReader()
	{
		delete mFS;
	}

	bool ReadFile()
	{
		int sig = mFS->ReadInt32();
		if (sig != 0xBEEF0001)
			return false;

		int flags = mFS->ReadInt32();
		Array<String> texNames;
		int texNameSize = mFS->ReadInt32();
		for (int i = 0; i < texNameSize; i++)
		{
			String path = mFS->ReadAscii32SizedString();
			texNames.Add(path);
		}

		int idxCount = mFS->ReadInt32();

		mModelDef->mMeshes.Add(ModelMesh());
		auto& modelMesh = mModelDef->mMeshes[0];
		modelMesh.mPrimitives.Add(ModelPrimitives());
		auto& modelPrimitives = modelMesh.mPrimitives[0];

		modelPrimitives.mFlags = (ModelPrimitives::Flags)flags;
		modelPrimitives.mTexPaths = texNames;

		modelPrimitives.mIndices.Resize(idxCount);
		mFS->Read(modelPrimitives.mIndices.mVals, idxCount * 2);

		int vtxCount = mFS->ReadInt32();
		modelPrimitives.mVertices.Resize(vtxCount);
		for (int i = 0; i < vtxCount; i++)
		{
			if ((flags & ModelPrimitives::Flags_Vertex_Position) != 0)
				mFS->ReadT(modelPrimitives.mVertices[i].mPosition);
			if ((flags & ModelPrimitives::Flags_Vertex_Tex0) != 0)
				mFS->ReadT(modelPrimitives.mVertices[i].mTexCoords);
			if ((flags & ModelPrimitives::Flags_Vertex_Tex1) != 0)
				mFS->ReadT(modelPrimitives.mVertices[i].mBumpTexCoords);
		}

		return true;
	}

	bool ReadFile(const StringImpl& fileName, const StringImpl& baseDir)
	{	
		if (fileName.StartsWith('@'))
		{
			int colon = (int)fileName.IndexOf(':');
			String addrStr = fileName.Substring(1, colon - 1);
			String lenStr = fileName.Substring(colon + 1);
			void* addr = (void*)(intptr)strtoll(addrStr.c_str(), NULL, 16);
			int len = (int)strtol(lenStr.c_str(), NULL, 10);
			MemStream* memStream = new MemStream(addr, len, false);
			mFS = memStream;
		}
		else
		{
			FileStream* fs = new FileStream();
			mFS = fs;
			if (!fs->Open(fileName, "rb"))
				return false;
		}		
		
		return ReadFile();		
	}
};

BF_EXPORT void* BF_CALLTYPE Res_OpenModel(const char* fileName, const char* baseDir, void* vertexDefinition)
{
	ModelDef* modelDef = new ModelDef();
	modelDef->mLoadDir = baseDir;
	ModelReader reader(modelDef);
	if (!reader.ReadFile(fileName, baseDir))
	{
		delete modelDef;
		return NULL;
	}

	return modelDef;
}

BF_EXPORT StringView BF_CALLTYPE Res_SerializeModel(ModelDef* modelDef)
{
	DynMemStream ms;
	ms.Write((int)0xBEEF0001);

	for (auto& mesh : modelDef->mMeshes)
	{
		for (auto& prims : mesh.mPrimitives)
		{
			ms.Write((int)prims.mFlags);
			ms.Write(prims.mTexPaths.mSize);
			for (auto& path : prims.mTexPaths)
				ms.Write(path);

			ms.Write((int)prims.mIndices.mSize);
			ms.Write(prims.mIndices.mVals, prims.mIndices.mSize * 2);

			ms.Write((int)prims.mVertices.mSize);

			for (int i = 0; i < prims.mVertices.mSize; i++)
			{
				auto& vtx = prims.mVertices[i];
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Position) != 0)
					ms.WriteT(vtx.mPosition);
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Tex0) != 0)
					ms.WriteT(vtx.mTexCoords);
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Tex1) != 0)
					ms.WriteT(vtx.mBumpTexCoords);
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Color) != 0)
					ms.WriteT(vtx.mColor);
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Normal) != 0)
					ms.WriteT(vtx.mNormal);
				if ((prims.mFlags & ModelPrimitives::Flags_Vertex_Tangent) != 0)
					ms.WriteT(vtx.mTangent);
			}
		}
	}
	
	String& outString = *gModelDef_TLStrReturn.Get();
	outString.Clear();
	outString.Append((char*)ms.GetPtr(), ms.GetSize());
	return outString;
}
