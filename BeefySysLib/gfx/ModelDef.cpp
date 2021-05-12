#include "ModelDef.h"
#include "BFApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/ModelInstance.h"
#include "util/Dictionary.h"
#include "util/TLSingleton.h"
#include "FileStream.h"
#include "MemStream.h"

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

BF_EXPORT ModelInstance* BF_CALLTYPE ModelDef_CreateModelInstance(ModelDef* modelDef)
{
	return gBFApp->mRenderDevice->CreateModelInstance(modelDef);
}

BF_EXPORT const char* BF_CALLTYPE ModelDef_GetInfo(ModelDef* modelDef)
{
	String& outString = *gModelDef_TLStrReturn.Get();
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

ModelDef::~ModelDef()
{
	for (auto& materialInstance : mMaterials)
	{
		materialInstance.mDef->mRefCount--;
	}
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
		if (fileName.Contains(':'))
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
	return outString;
}
