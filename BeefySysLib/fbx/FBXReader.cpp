#include "FBXReader.h"
#include "BFApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/DrawLayer.h"
#include "gfx/ModelInstance.h"
#include "FileStream.h"

#ifndef BF_NO_FBX

#include "boost/unordered_map.hpp"

#pragma warning(disable:4996)

USING_NS_BF;

namespace boost
{
	static inline std::size_t hash_value(const FBXVertexData& vtxData)
	{
		uint32* data = (uint32*) &vtxData.mCoords;
		return (data[0] ^ data[1] ^ data[2]);
	}
}

#pragma comment(lib, "libfbxsdk.lib")

#ifdef IOS_REF
#undef  IOS_REF
#define IOS_REF (*(pManager->GetIOSettings()))
#endif


bool FBXIsValidMatrix(const FbxAMatrix& mat)
{
	// Check that scales aren't zero and quaternions don't have //1.#QNAN0 values
	if (mat.GetQ()[0] != mat.GetQ()[0] ||
		mat.GetS()[0] == 0)
	{
		return false;
	}
	return true;
}

FbxAMatrix FBXCorrectMatrix(const FbxAMatrix& mat)
{
	if (!FBXIsValidMatrix(mat))
	{
		/*FxOgreFBXLog("Detected invalid matrix:\n");
		logMatrix(mat);
		FxOgreFBXLog("Returning identity matrix:\n");*/
		return FbxAMatrix();
	}
	return mat;
}

static void InitializeSdkObjects(FbxManager*& pManager, FbxScene*& pScene)
{
	//The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
	pManager = FbxManager::Create();
	if (!pManager)
	{
		FBXSDK_printf("Error: Unable to create FBX Manager!\n");
		exit(1);
	}
	else FBXSDK_printf("Autodesk FBX SDK version %s\n", pManager->GetVersion());

	//Create an IOSettings object. This object holds all import/export settings.
	FbxIOSettings* ios = FbxIOSettings::Create(pManager, IOSROOT);
	pManager->SetIOSettings(ios);

	//Load plugins from the executable directory (optional)
	FbxString lPath = FbxGetApplicationDirectory();
	pManager->LoadPluginsDirectory(lPath.Buffer());

	//Create an FBX scene. This object holds most objects imported/exported from/to files.
	pScene = FbxScene::Create(pManager, "My Scene");
	if (!pScene)
	{
		FBXSDK_printf("Error: Unable to create FBX scene!\n");
		exit(1);
	}
}

static void DestroySdkObjects(FbxManager* pManager, bool pExitStatus)
{
	//Delete the FBX Manager. All the objects that have been allocated using the FBX Manager and that haven't been explicitly destroyed are also automatically destroyed.
	if (pManager) pManager->Destroy();
	if (pExitStatus) FBXSDK_printf("Program Success!\n");
}

static bool LoadScene(FbxManager* pManager, FbxDocument* pScene, const char* pFilename, FbxImporter* pImporter)
{
	int lFileMajor, lFileMinor, lFileRevision;
	int lSDKMajor, lSDKMinor, lSDKRevision;
	//int lFileFormat = -1;
	int i, lAnimStackCount;
	bool lStatus;
	char lPassword[1024];

	// Get the file version number generate by the FBX SDK.
	FbxManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

	// Create an importer.
	//FbxImporter* pImporter = FbxImporter::Create(pManager, "");

	// Initialize the importer by providing a filename.
	const bool lImportStatus = pImporter->Initialize(pFilename, -1, pManager->GetIOSettings());
	pImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

	if (!lImportStatus)
	{
		FbxString error = pImporter->GetStatus().GetErrorString();
		FBXSDK_printf("Call to FbxImporter::Initialize() failed.\n");
		FBXSDK_printf("Error returned: %s\n\n", error.Buffer());

		if (pImporter->GetStatus().GetCode() == FbxStatus::eInvalidFileVersion)
		{
			FBXSDK_printf("FBX file format version for this FBX SDK is %d.%d.%d\n", lSDKMajor, lSDKMinor, lSDKRevision);
			FBXSDK_printf("FBX file format version for file '%s' is %d.%d.%d\n\n", pFilename, lFileMajor, lFileMinor, lFileRevision);
		}

		return false;
	}

	FBXSDK_printf("FBX file format version for this FBX SDK is %d.%d.%d\n", lSDKMajor, lSDKMinor, lSDKRevision);

	if (pImporter->IsFBX())
	{
		FBXSDK_printf("FBX file format version for file '%s' is %d.%d.%d\n\n", pFilename, lFileMajor, lFileMinor, lFileRevision);

		// From this point, it is possible to access animation stack information without
		// the expense of loading the entire file.

		FBXSDK_printf("Animation Stack Information\n");

		lAnimStackCount = pImporter->GetAnimStackCount();

		FBXSDK_printf("    Number of Animation Stacks: %d\n", lAnimStackCount);
		FBXSDK_printf("    Current Animation Stack: \"%s\"\n", pImporter->GetActiveAnimStackName().Buffer());
		FBXSDK_printf("\n");

		for (i = 0; i < lAnimStackCount; i++)
		{
			FbxTakeInfo* lTakeInfo = pImporter->GetTakeInfo(i);

			FBXSDK_printf("    Animation Stack %d\n", i);
			FBXSDK_printf("         Name: \"%s\"\n", lTakeInfo->mName.Buffer());
			FBXSDK_printf("         Description: \"%s\"\n", lTakeInfo->mDescription.Buffer());

			// Change the value of the import name if the animation stack should be imported 
			// under a different name.
			FBXSDK_printf("         Import Name: \"%s\"\n", lTakeInfo->mImportName.Buffer());

			// Set the value of the import state to false if the animation stack should be not
			// be imported. 
			FBXSDK_printf("         Import State: %s\n", lTakeInfo->mSelect ? "true" : "false");
			FBXSDK_printf("\n");
		}

		// Set the import states. By default, the import states are always set to 
		// true. The code below shows how to change these states.
		IOS_REF.SetBoolProp(IMP_FBX_MATERIAL, true);
		IOS_REF.SetBoolProp(IMP_FBX_TEXTURE, true);
		IOS_REF.SetBoolProp(IMP_FBX_LINK, true);
		IOS_REF.SetBoolProp(IMP_FBX_SHAPE, true);
		IOS_REF.SetBoolProp(IMP_FBX_GOBO, true);
		IOS_REF.SetBoolProp(IMP_FBX_ANIMATION, true);
		IOS_REF.SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
	}

	// Import the scene.
	lStatus = pImporter->Import(pScene);

	if (lStatus == false && pImporter->GetStatus().GetCode() == FbxStatus::ePasswordError)
	{
		FBXSDK_printf("Please enter password: ");

		lPassword[0] = '\0';

		FBXSDK_CRT_SECURE_NO_WARNING_BEGIN
			scanf("%s", lPassword);
		FBXSDK_CRT_SECURE_NO_WARNING_END

			FbxString lString(lPassword);

		IOS_REF.SetStringProp(IMP_FBX_PASSWORD, lString);
		IOS_REF.SetBoolProp(IMP_FBX_PASSWORD_ENABLE, true);

		lStatus = pImporter->Import(pScene);

		if (lStatus == false && pImporter->GetStatus().GetCode() == FbxStatus::ePasswordError)
		{
			FBXSDK_printf("\nPassword is wrong, import aborted.\n");
		}
	}

	// Destroy the importer.
	//pImporter->Destroy();

	return lStatus;
}

FBXReader::FBXReader(ModelDef* modelDef)
{
	mModelDef = modelDef;
	mParamLum = 1.0f;
	mParamBindframe = 0;
}

FBXReader::~FBXReader()
{
	while (mMeshes.size() > 0)
	{
		delete mMeshes.back();
		mMeshes.pop_back();
	}
}

//void FBXReader::TranslateNode

FBXMesh* FBXReader::LoadMesh(FbxNode* fbxNode, FbxMesh* fbxMesh)
{	
	FBXMesh* mesh = new FBXMesh();
	mesh->mName = fbxNode->GetName();

	FbxAMatrix nodeTransform = GetBindPose(fbxNode, fbxMesh);
	nodeTransform = FBXCorrectMatrix(nodeTransform);

	int numSrcVertices = fbxMesh->GetControlPointsCount();
	int numFaces = fbxMesh->GetPolygonCount();
	int numVertices = numFaces * 3;

	int elementMatCount = fbxMesh->GetElementMaterialCount();

	int numMaterials = fbxNode->GetMaterialCount();

	FBXMaterial* material = &mesh->mMaterial;

	for (int matIdx = 0; matIdx < numMaterials; matIdx++)
	{
		FbxSurfaceMaterial* fbxMaterial = fbxNode->GetMaterial(matIdx);
		
		const char* matName = fbxMaterial->GetName();

		for (int channelIdx = 0; true; channelIdx++)
		{
			FbxProperty prop = fbxMaterial->FindProperty(FbxLayerElement::sTextureChannelNames[channelIdx]);
			if (!prop.IsValid())
				break;
			int texCount = prop.GetSrcObjectCount(FbxTexture::ClassId);

			for (int texIdx = 0; texIdx < texCount; texIdx++)
			{
				FbxTexture* texture = FbxCast<FbxTexture>(prop.GetSrcObject(FbxTexture::ClassId, texIdx));								
				if (texture != NULL)
				{
					FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);

					if (fileTexture != NULL)
					{
						String mediaName = fileTexture->GetMediaName();
						String fileExt = "";

						int parenPos = (int)mediaName.find(" (");
						String origFileName = fileTexture->GetFileName();
						int dotPos = (int)origFileName.rfind('.');
						if (dotPos != -1)
							fileExt = origFileName.substr(dotPos);
						//material->mTexFileName = fileName;

						/*if (parenPos != -1) 
						{
							String suffix = mediaName.substr(parenPos + 1);
							String fileName = mediaName.substr(0, parenPos);

							fileName += fileExt;

							if (suffix == "(Map)")
								material->mTexFileName = fileName;
							if (suffix == "(Normal Map Translator)")
								material->mBumpFileName = fileName;
						}*/

						String fileName = fileTexture->GetFileName();

						if (material->mTexFileName.length() == 0)
						{
							int slashPos = std::max((int)fileName.rfind('\\'), (int)fileName.rfind('/'));
							if (slashPos > 0)
								fileName = fileName.substr(slashPos + 1);
							material->mTexFileName = fileName;
						}
					}
				}
			}
		}

		//material->GetTex
		int b = 0;
	}

	/*for (int matIdx = 0; matIdx < matCount; matIdx++)
	{
		fbxMesh->GetElementMaterial(matIdx);
	}*/
	
	std::vector<FBXVertexData> unpackedVtxData;
	unpackedVtxData.resize(numVertices);

	FbxVector4* controlPoints = fbxMesh->GetControlPoints();

	/*std::vector<VertexData> srcVtxData;
	srcVtxData.resize(numSrcVertices);
	FbxVector4* controlPoints = fbxMesh->GetControlPoints();
	for (int i = 0; i < numSrcVertices; i++)
	{
		VertexData* vtxData = &srcVtxData[i];
		FbxVector4 controlPoint = controlPoints[i];
		vtxData->mCoords = Vector3((float)controlPoint[0], (float)controlPoint[1], (float)controlPoint[2]);
	}*/
	
	for (int uvSetIdx = 0; uvSetIdx < fbxMesh->GetElementUVCount(); uvSetIdx++)
	{
		FbxGeometryElementUV* elementUV = fbxMesh->GetElementUV(uvSetIdx);		
		FbxLayerElement::EMappingMode mappingMode = elementUV->GetMappingMode();
		if (mappingMode == FbxLayerElement::eByControlPoint)			
		{						
			auto directArray = elementUV->GetDirectArray();
			int uvLen = directArray.GetCount();
			int maxIdx = 0;
			/*for (int i = 0; i < uvLen; i++)
			{
				FBXVertexData* vtxData = &unpackedVtxData[i];				
				auto texCoords = directArray.GetAt(i);
				vtxData->mTexCoords.push_back(TexCoords((float) texCoords[0], (float) texCoords[1]));
			}*/

			int vtxIdx = 0;
			for (int faceIdx = 0; faceIdx < numFaces; faceIdx++)
			{
				for (int faceVtxIdx = 0; faceVtxIdx < 3; faceVtxIdx++)
				{					
					int controlIdx = fbxMesh->GetPolygonVertex(faceIdx, faceVtxIdx);					
					FBXVertexData* vtxData = &unpackedVtxData[vtxIdx];				
					auto texCoords = directArray.GetAt(controlIdx);
					vtxData->mTexCoords.push_back(TexCoords((float) texCoords[0], (float) texCoords[1]));
					vtxIdx++;
				}
			}

			OutputDebugStrF("Max: %d\n", maxIdx);
		}
		else if (mappingMode == FbxLayerElement::eByPolygonVertex)
		{
			auto idxArray = elementUV->GetIndexArray();
			int idxLen = idxArray.GetCount();
			auto directArray = elementUV->GetDirectArray();
			int uvLen = directArray.GetCount();
			int maxIdx = 0;
			for (int i = 0; i < idxLen; i++)
			{	
				FBXVertexData* vtxData = &unpackedVtxData[i];
				int directIdx = idxArray.GetAt(i);
				maxIdx = std::max(maxIdx, directIdx);
				auto texCoords = directArray.GetAt(directIdx);
				vtxData->mTexCoords.push_back(TexCoords((float)texCoords[0], (float)texCoords[1]));
			}
			OutputDebugStrF("Max: %d\n", maxIdx);
		}
		else
		{
			BF_ASSERT("Unsupported UV" == 0);
		}
	}
	
	if (fbxMesh->GetLayer(0)->GetNormals() == NULL)
	{		
		fbxMesh->InitNormals();
		fbxMesh->ComputeVertexNormals();
	}
	
	//fbxMesh->InitTangents();

	int layerCount = fbxMesh->GetLayerCount();

	//auto tangents = fbxMesh->GetLayer(0)->GetTangents();
	//auto bionarmals = fbxMesh->GetLayer(0)->GetBinormals();

	FbxLayerElementArrayTemplate<FbxVector4>* tangentArray = NULL;
	fbxMesh->GetTangents(&tangentArray);

	FbxLayerElementArrayTemplate<int>* tangentIndexArray = NULL;
	fbxMesh->GetTangentsIndices(&tangentIndexArray);

	tangentIndexArray[0];

	std::vector<BoneWeightVector> boneWeightsVector;
	boneWeightsVector.resize(numSrcVertices);
	GetVertexBoneWeights(fbxNode, fbxMesh, boneWeightsVector);

	int vtxIdx = 0;
	for (int faceIdx = 0; faceIdx < numFaces; faceIdx++)
	{
		for (int faceVtxIdx = 0; faceVtxIdx < 3; faceVtxIdx++)
		{
			FbxVector4 normal;
			fbxMesh->GetPolygonVertexNormal(faceIdx, faceVtxIdx, normal);
			//TODO: Rotate to the bind pose
			
			int controlIdx = fbxMesh->GetPolygonVertex(faceIdx, faceVtxIdx);

			FbxVector4 controlPt = controlPoints[controlIdx];

			controlPt = nodeTransform.MultT(controlPt);

			FBXVertexData* vtxData = &unpackedVtxData[vtxIdx];
			vtxData->mNormal = Vector3((float)normal[0], (float)normal[1], (float)normal[2]);
			//vtxData->mCoords = Vector3((float)controlPt[0] * 50 + 50, (float)controlPt[1] * 50 + 50, (float)controlPt[2] * 50 + 50);
			vtxData->mCoords = Vector3((float)controlPt[0], (float)controlPt[1], (float)controlPt[2]);
			vtxData->mBoneWeights = boneWeightsVector[controlIdx];
			vtxIdx++;
		}
	}	

	mesh->mVertexData.reserve(numVertices);

	typedef boost::unordered_map<FBXVertexData, int> VertexDataMap;
	 VertexDataMap usedVertexData;
	int vertexDataSize = numVertices;
	for (int vtxIdx = 0; vtxIdx < vertexDataSize; vtxIdx++)
	{
		//VertexData* vtxData = hash
		FBXVertexData* vtxData = &unpackedVtxData[vtxIdx];

		/*bool hasValidIdx = false;
		for (int boneIdx = 0; boneIdx < (int)vtxData->mBoneWeights.size(); boneIdx++)
		{
			BoneWeight boneWeight = vtxData->mBoneWeights[boneIdx];
			hasValidIdx |= boneWeight.mBoneIdx <= 45;
		}

		if (!hasValidIdx)
		{
			vtxData->mCoords.mX = 0;
			vtxData->mCoords.mY = 0;
			vtxData->mCoords.mZ = 0;
		}*/

		auto itr = usedVertexData.find(*vtxData);
		if (itr != usedVertexData.end())
		{
			mesh->mIndexData.push_back(itr->second);
		}
		else
		{
			int idx = (int)mesh->mVertexData.size();
			usedVertexData.insert(VertexDataMap::value_type(*vtxData, idx));
			mesh->mVertexData.push_back(*vtxData);
			mesh->mIndexData.push_back(idx);
		}		
	}

	return mesh;
}

static bool IsNodeVisible(FbxNode *pNode)
{
	bool bIsVisible = false;
	FbxNode* pParentNode = pNode;
	while (pParentNode != NULL)
	{
		bIsVisible = pParentNode->GetVisibility();
		if (!bIsVisible)
		{
			break;
		}
		pParentNode = pParentNode->GetParent();
	}

	return bIsVisible;
}

void FBXReader::TranslateNode(FbxNode* fbxNode)
{
	if (fbxNode == NULL)
		return;

	switch (fbxNode->GetNodeAttribute()->GetAttributeType())
	{
	case FbxNodeAttribute::eMesh:
		if (IsNodeVisible(fbxNode))
		{			
			FbxGeometryConverter geometryConverter(mFBXManager);
			for (int attrIdx = 0; attrIdx < fbxNode->GetNodeAttributeCount(); attrIdx++)
			{
				if (fbxNode->GetNodeAttributeByIndex(attrIdx)->GetAttributeType() == FbxNodeAttribute::eMesh)
				{
					FbxMesh* fbxMesh = (FbxMesh*) fbxNode->GetNodeAttributeByIndex(attrIdx);
					FbxMesh* triMesh = geometryConverter.TriangulateMesh(fbxMesh);
					FBXMesh* mesh = LoadMesh(fbxNode, triMesh);
					mMeshes.push_back(mesh);
				}
			}			
		}
	}

	for (int childIdx = 0; childIdx < fbxNode->GetChildCount(); childIdx++)
	{
		// Call recursive translateNode once for each child of the root node
		FbxNode* childNode = fbxNode->GetChild(childIdx);
		TranslateNode(childNode);
	}
}

void FBXReader::FindJoints(FbxNode* fbxNode)
{
	if (fbxNode == NULL)
		return;

	if (fbxNode->GetParent() != NULL)
	{
		switch (fbxNode->GetNodeAttribute()->GetAttributeType())
		{
		case FbxNodeAttribute::eSkeleton:
			if (IsNodeVisible(fbxNode))
			{
				String jointName = fbxNode->GetName();
				int idx = mJointIndexMap[jointName];
				mFBXJoints[idx].pNode = fbxNode;
			}
		}
	}

	for (int childIdx = 0; childIdx < fbxNode->GetChildCount(); childIdx++)
	{
		// Call recursive translateNode once for each child of the root node
		FbxNode* childNode = fbxNode->GetChild(childIdx);
		FindJoints(childNode);
	}
}

static FbxAMatrix FBXConvertMatrix(const FbxMatrix& mat)
{
	FbxVector4 trans, shear, scale;
	FbxQuaternion rot;
	double sign;
	mat.GetElements(trans, rot, shear, scale, sign);
	FbxAMatrix ret;
	ret.SetT(trans);
	ret.SetQ(rot);
	ret.SetS(scale);
	return ret;
}

int FBXReader::FBXGetJointIndex(FbxNode* pNode)
{
	if (pNode)
	{
		if (mJointIndexMap.find(pNode->GetName()) != mJointIndexMap.end())
		{
			return mJointIndexMap[pNode->GetName()];
		}
	}
	return -1;
}

FbxAMatrix FBXReader::GetBindPose(FbxNode *pNode, FbxMesh *pMesh)
{
	//if (!params.useanimframebind)
	{
		int lSkinCount = pMesh->GetDeformerCount(FbxDeformer::eSkin);
		for (int i = 0; i != lSkinCount; ++i)
		{
			int lClusterCount = ((FbxSkin *) pMesh->GetDeformer(i, FbxDeformer::eSkin))->GetClusterCount();
			for (int j = 0; j < lClusterCount; ++j)
			{

				FbxCluster *pCluster = ((FbxSkin *) pMesh->GetDeformer(i, FbxDeformer::eSkin))->GetCluster(j);

				FbxAMatrix geometryBind;
				pCluster->GetTransformMatrix(geometryBind);
				if (FBXIsValidMatrix(geometryBind))
					return geometryBind;
			}
		}
		// It can be difficult to calculate the correct bind position for the mesh because
		// the FBX Global evaluate functions do not seem to take the inheritance type into account.
		// eInherit_Rrs nodes can mess things up.
		//FxOgreFBXLog("Warning: can not find bind pose for mesh %s.  Attempting to calculate it\n", pNode->GetName());
	}

	FbxAMatrix globalTransform;
	//if (getSkeleton())
	{
		globalTransform = CalculateGlobalTransformWithBind(pNode, FbxTime(mParamBindframe));
		//globalTransform = getSkeleton()->CalculateGlobalTransformWithBind(pNode, FbxTime(params.bindframe), params);
	}
	//else
	{
		//globalTransform = CalculateGlobalTransform(pNode, FbxTime(params.bindframe));
	}

	return globalTransform;
}

bool FBXReader::FBXLoadJoint(FbxNode* pNode, FbxAMatrix globalBindPose)
{
	if (!pNode)
	{
		//FxOgreFBXLog("Failed to load joint.\n");
		return false;
	}
	if (mJointIndexMap.find(pNode->GetName()) != mJointIndexMap.end())
	{
		// We have already exported this joint.
		return false;
	}

	if (!pNode->GetParent())
	{
		// Ignore the FBX root node here.
		return false;
	}

	// Protect against nodes that have parents with the same name (to avoid infinite recursion)
	String nodename(pNode->GetName());
	FbxNode *pParent = pNode->GetParent();
	String parentName;
	if (pParent)
		parentName = pParent->GetName();
	while (pParent)
	{
		String parentname = pParent->GetName();
		if (parentname.compare(nodename) == 0)
		{
			//FxOgreFBXLog("Warning! %s joint has a parent by the same name.  Joint names must be unique!  Output file may render incorrectly.", pNode->GetName());
			pNode = pParent;
			break;
		}
		pParent = pParent->GetParent();
	}

	globalBindPose = FBXCorrectMatrix(globalBindPose);

	FBXJoint newJoint;
	newJoint.mBoneLength = 0;
	newJoint.parentIndex = -1;
	mFBXJoints.push_back(newJoint);
	int index = mFBXJoints.size() - 1;
	mJointIndexMap[pNode->GetName()] = index;

	mFBXJoints[index].pNode = pNode;
	mFBXJoints[index].name = pNode->GetName();
	mFBXJoints[index].id = index;
	mFBXJoints[index].parentName = parentName;

	//FxOgreFBXLog("%s joint added at index %i with global transform:", pNode->GetName(), index);
	//logMatrix(globalBindPose);

	mFBXJoints[index].globalBindPose = globalBindPose;

	return true;
}


static FbxAMatrix CalculateGlobalTransform(FbxNode* pNode, FbxTime time) 
{
    // There seems to be some variance between calculating the global transform and
    // using the FBX calculate it with EvaluateGlobalTransform.  
    /*
    FbxAMatrix lTM = pNode->EvaluateLocalTransform(time);

    if( pNode->GetParent() )
    {
        FbxAMatrix parentMatrix = CalculateGlobalTransform(pNode->GetParent(), time);
        return parentMatrix * lTM;
    }
    // root node
    // params.exportWorldCoords?
    return lTM;
    */
    return pNode->EvaluateGlobalTransform(time);
}

FbxAMatrix FBXReader::CalculateGlobalTransformWithBind(FbxNode* pNode, FbxTime time)
{
	/*if (params.useanimframebind)
	{
		return CalculateGlobalTransform(pNode, time);
	}
	else*/
	{
		FbxAMatrix lTM = pNode->EvaluateLocalTransform(time);
		if (pNode->GetParent())
		{
			FbxAMatrix parentMatrix;
			if (mJointIndexMap.find(pNode->GetParent()->GetName()) == mJointIndexMap.end())
			{
				parentMatrix = CalculateGlobalTransform(pNode->GetParent(), time);
			}
			else
			{
				int index = mJointIndexMap[pNode->GetParent()->GetName()];
				parentMatrix = mFBXJoints[index].globalBindPose;
			}
			return parentMatrix * lTM;
		}
		return lTM;
	}
}

#define PRECISION 0.00000001f

static int gKeyFrameCount = 0;

FBXSkeletonKeyframe FBXReader::FBXLoadKeyframe(FBXJoint& j, float time)
{
	gKeyFrameCount++;

	FbxTime FbxTime;
	FbxTime.SetSecondDouble(time);
	//create keyframe
	FBXSkeletonKeyframe key;
	key.time = time;


	FbxAMatrix localTMOrig = j.localBindPose;
	FbxAMatrix localTMAtTime = localTMOrig;	

	FbxAMatrix nodeTM, parentTM;
	if (j.pNode)
	{

		//nodeTM = CalculateGlobalTransformWithBind(j.pNode, FbxTime);

		/*if (params.skelBB)
		{
			// Bounding box info is output to the MESH file, so this will not impact the
			// mesh if called from AddFBXAnimationToExisting.
			m_bbox.merge(Point3(nodeTM.GetT()[0], nodeTM.GetT()[1], nodeTM.GetT()[2]));
		}
		else*/
		{
			localTMAtTime = j.pNode->EvaluateLocalTransform(FbxTime);			
		}
	}

	//FbxAMatrix deltaMat = localTMOrig.Inverse() * localTMAtTime;
	//FbxVector4 diff = localTMAtTime.GetT() - localTMOrig.GetT();

	//FbxAMatrix deltaMat = localTMAtTime * localTMOrig.Inverse();
	//FbxAMatrix deltaGlobalMat = nodeTM * j.bindPose.Inverse();

	FbxAMatrix deltaMat = localTMAtTime;
	FbxVector4 diff = localTMAtTime.GetT();

	//FbxVector4 diff = deltaGlobalMat.GetT();

	// @todo why does calculating the diff vector like below break UTRef?
	// FbxVector4 diff = deltaMat.GetT();

	Vector3 translation((float)diff[0], (float)diff[1], (float)diff[2]);
	if (fabs(translation.mX) < PRECISION)
		translation.mX = 0;
	if (fabs(translation.mY) < PRECISION)
		translation.mY = 0;
	if (fabs(translation.mZ) < PRECISION)
		translation.mZ = 0;


	Vector3 scale((float)deltaMat.GetS()[0], (float)deltaMat.GetS()[1], (float)deltaMat.GetS()[2]);
	if (fabs(scale.mX) < PRECISION)
		scale.mX = 0;
	if (fabs(scale.mY) < PRECISION)
		scale.mY = 0;
	if (fabs(scale.mZ) < PRECISION)
		scale.mZ = 0;

	key.tx = translation.mX * mParamLum;
	key.ty = translation.mY * mParamLum;
	key.tz = translation.mZ * mParamLum;

	FbxQuaternion quat = deltaMat.GetQ();	
	//FbxQuaternion quat = deltaGlobalMat.GetQ();	
	key.quat_x = quat[0];
	key.quat_y = quat[1];
	key.quat_z = quat[2];
	key.quat_w = quat[3];

	Quaternion quatTest((float)quat[0], (float)quat[1], (float)quat[2], (float)quat[3]);

	Vector3 vecOut = Vector3::Transform(Vector3(0, 1, 0), quatTest);

	key.sx = static_cast<float>(scale.mX);
	key.sy = static_cast<float>(scale.mY);
	key.sz = static_cast<float>(scale.mZ);

	return key;
}

bool FBXReader::FBXLoadClipAnim(String clipName, float start, float stop, float rate, FBXAnimation& a)
{
	size_t i, j;
	std::vector<float> times;
	times.clear();
	if (rate <= 0)
	{
		//FxOgreFBXLog("invalid sample rate for the clip (must be >0), we skip it\n");
		return false;
	}
	else
	{
		for (float t = start; t < stop - 0.00001f; t += rate)
			times.push_back(t);
		//times.push_back(stop);
	}
	// get animation length
	float length = 0;
	if (times.size() >= 0)
		length = times[times.size() - 1] - times[0];
	if (length < 0)
	{
		//FxOgreFBXLog("invalid time range for the clip, we skip it\n");
		return false;
	}
	a.mName = clipName.c_str();
	a.mTracks.clear();
	a.mLength = length;


	// create a track for current clip for all joints
	std::vector<FBXTrack> animTracks;
	for (i = 0; i < mFBXJoints.size(); i++)
	{
		FBXTrack t;
		t.mType = TT_SKELETON;
		t.mBone = mFBXJoints[i].name;
		t.mSkeletonKeyframes.clear();
		animTracks.push_back(t);
	}

	// evaluate animation curves at selected times
	for (i = 0; i < times.size(); i++)
	{
		//load a keyframe for every joint at current time
		for (j = 0; j < mFBXJoints.size(); j++)
		{
			FBXSkeletonKeyframe key = FBXLoadKeyframe(mFBXJoints[j], times[i]);
			key.time = key.time - times[0];
			//add keyframe to joint track
			animTracks[j].mSkeletonKeyframes.push_back(key);
		}		
	}
	// add created tracks to current clip
	for (i = 0; i < animTracks.size(); i++)
	{
		a.mTracks.push_back(animTracks[i]);
	}
	if (animTracks.size() > 0)
	{
		// display info
		//FxOgreFBXLog("length: %f\n", a.m_length);
		//FxOgreFBXLog("num keyframes: %d\n", animTracks[0].m_skeletonKeyframes.size());
	}

	return true;
}

bool FBXReader::FBXLoadClip(String clipName, float start, float stop, float rate)
{
	FBXAnimation a;
	mAnimations.push_back(a);
	bool stat = FBXLoadClipAnim(clipName, start, stop, rate, mAnimations[mAnimations.size() - 1]);
	if (!stat)
	{
		mAnimations.pop_back();
		return false;
	}
	return true;
}

void FBXReader::GetAnimationBounds()
{	
	mAnimStart = FLT_MAX;
	mAnimStop = -FLT_MAX;

	FbxTime kStop = FBXSDK_TIME_MINUS_INFINITE;
	FbxTime kStart = FBXSDK_TIME_INFINITE;

	// Iterate through all curves to find the start and end time of the animation. Works
	// for bones and morph targets.
	for (int i = 0; i < mFBXScene->GetSrcObjectCount(FbxAnimCurve::ClassId); ++i)
	{
		FbxAnimCurve* pCurve = (FbxAnimCurve*) mFBXScene->GetSrcObject(FbxAnimCurve::ClassId, i);
		if (pCurve)
		{
			int numKeys = pCurve->KeyGetCount();
			if (numKeys > 0)
			{
				float first = (float) pCurve->KeyGet(0).GetTime().GetSecondDouble();
				float last = first;

				if (numKeys > 1)
				{
					last = (float) pCurve->KeyGet(numKeys - 1).GetTime().GetSecondDouble();
				}
				if (first < mAnimStart)				
					mAnimStart = first;				
				if (last > mAnimStop)				
					mAnimStop = last;				
			}
		}
	}	
}

void FBXReader::ComputeBindPoseBoundingBox()
{
	// Make sure the bind
	for (size_t i = 0; i < mFBXJoints.size(); ++i)
	{
		//m_bbox.merge(Point3(mJoints[i].globalBindPose.GetT()[0], mJoints[i].globalBindPose.GetT()[1], mJoints[i].globalBindPose.GetT()[2]));
	}
}

void FBXReader::SetParentIndexes()
{
	// Set the parent indexes, so we know what to sort.
	for (size_t i = 0; i < mFBXJoints.size(); ++i)
	{
		FbxNode *pNode = mFBXJoints[i].pNode;
		int parentIndex = -1;
		if (pNode->GetParent())
		{
			if (mJointIndexMap.find(pNode->GetParent()->GetName()) != mJointIndexMap.end())
			{
				parentIndex = mJointIndexMap[pNode->GetParent()->GetName()];
			}
		}
		mFBXJoints[i].parentIndex = parentIndex;
	}
}

void FBXReader::AddParentsOfExistingJoints()
{
	// If the parent for this node doesn't exist, add it here.
	for (size_t i = 0; i < mFBXJoints.size(); ++i)
	{
		FbxNode *pNode = mFBXJoints[i].pNode;
		if (pNode->GetParent())
		{
			if (mJointIndexMap.find(pNode->GetParent()->GetName()) == mJointIndexMap.end())
			{
				FbxAMatrix global = CalculateGlobalTransformWithBind(pNode->GetParent(), FbxTime(mParamBindframe));

				bool bLoaded = FBXLoadJoint(pNode->GetParent(), global);

				//FxOgreFBXLog("Warning joint %s created with no bind pose information.\n", pNode->GetParent()->GetName());
				//logMatrix(global);
			}
		}
	}
}

// Ensure that parents are before their children in mJoints
void FBXReader::SortAndPruneJoints()
{
	// Set the indexes to sort by
	SetParentIndexes();

	std::map<String, int> sortedJointIndexMap;
	std::vector<FBXJoint> sorted_joints;

	for (size_t i = 0; i < mFBXJoints.size(); ++i)
	{
		SortJoint(mFBXJoints[i], sortedJointIndexMap, sorted_joints);
	}

	// Update the joint list.
	mFBXJoints.clear();
	mFBXJoints = sorted_joints;
	mJointIndexMap.clear();

	mJointIndexMap = sortedJointIndexMap;
}

void FBXReader::SortJoint(FBXJoint j, std::map<String, int> &sortedJointIndexMap, std::vector<FBXJoint>& sorted_joints)
{
	// Only export if we haven't already.
	if (sortedJointIndexMap.find(j.name) == sortedJointIndexMap.end())
	{
		int newParentIndex = j.parentIndex;
		if (j.parentIndex != -1)
		{
			// Export parents first. 
			SortJoint(mFBXJoints[j.parentIndex], sortedJointIndexMap, sorted_joints);
			newParentIndex = sortedJointIndexMap[j.pNode->GetParent()->GetName()];
		}
		if (sorted_joints.size() >= BF_MAX_NUM_BONES)
		{
			//FxOgreFBXLog("Warning: pruned joint - %s.  Too many bones!\n", j.name.c_str());
		}
		else
		{
			int id = sorted_joints.size();

			sorted_joints.push_back(j);
			sortedJointIndexMap[j.name] = id;

			sorted_joints[id].id = id;
			sorted_joints[id].parentIndex = newParentIndex;
			//FxOgreFBXLog("%i - %s - parent index: %i.\n", sorted_joints[id].id, sorted_joints[id].name.c_str(), sorted_joints[id].parentIndex);
		}
	}
}

void FBXReader::CalculateLocalTransforms(FbxNode* pRootNode)
{
	//FxOgreFBXLog("Calculating local transforms for skeleton bones.\n");
	// Calculate global transforms.
	for (size_t i = 0; i < mFBXJoints.size(); ++i)
	{
		FbxNode* pNode = mFBXJoints[i].pNode;
		FbxAMatrix localTM, parentTM;

		localTM = mFBXJoints[i].globalBindPose;

		Quaternion quatOrig(
			(float)localTM.GetQ()[0],
			(float)localTM.GetQ()[1],
			(float)localTM.GetQ()[2],
			(float)localTM.GetQ()[3]);

		/*if (params.useanimframebind)
		{
			// We could calculate the local transforms from the global ones, but
			// in some circumstances (blake), this produces incorrect values, possibly as
			// a result of gimble lock.  With useanimframebind, we are relying on the 
			// FBX SDK to calculate everything, so we rely on the EvaluateLocalTransform
			// function as opposed to calculating our own values.
			localTM = mJoints[i].pNode->EvaluateLocalTransform(FbxTime((params.bindframe)));
		}
		else*/ if (mFBXJoints[i].parentIndex != -1)
		{
			parentTM = mFBXJoints[mFBXJoints[i].parentIndex].globalBindPose;
			localTM = parentTM.Inverse() * localTM;
			//localTM = localTM * parentTM.Inverse();
		}

		mFBXJoints[i].localBindPose = localTM;
		mFBXJoints[i].bindPose = localTM;
		
		Quaternion quat(
			(float)localTM.GetQ()[0],
			(float)localTM.GetQ()[1],
			(float)localTM.GetQ()[2],
			(float)localTM.GetQ()[3]);

		Matrix4 quatOrigMat = quatOrig.ToMatrix();
		Matrix4 quatMat = quat.ToMatrix();

		Vector3 translation((float)localTM.GetT()[0], (float)localTM.GetT()[1], (float)localTM.GetT()[2]);
		if (fabs(translation.mX) < PRECISION)
			translation.mX = 0;
		if (fabs(translation.mY) < PRECISION)
			translation.mY = 0;
		if (fabs(translation.mZ) < PRECISION)
			translation.mZ = 0;

		Vector3 scale((float)localTM.GetS()[0], (float)localTM.GetS()[1], (float)localTM.GetS()[2]);
		if (fabs(scale.mX) < PRECISION)
			scale.mX = 0;
		if (fabs(scale.mY) < PRECISION)
			scale.mY = 0;
		if (fabs(scale.mZ) < PRECISION)
			scale.mZ = 0;

		mFBXJoints[i].posx = translation.mX * mParamLum;
		mFBXJoints[i].posy = translation.mY * mParamLum;
		mFBXJoints[i].posz = translation.mZ * mParamLum;
		
		mFBXJoints[i].quatx = localTM.GetQ()[0];
		mFBXJoints[i].quaty = localTM.GetQ()[1];
		mFBXJoints[i].quatz = localTM.GetQ()[2];
		mFBXJoints[i].quatw = localTM.GetQ()[3];

		mFBXJoints[i].scalex = static_cast<float>(scale.mX);
		mFBXJoints[i].scaley = static_cast<float>(scale.mY);
		mFBXJoints[i].scalez = static_cast<float>(scale.mZ);

		mFBXJoints[i].bInheritScale = true;

		if (mFBXJoints[i].parentIndex >= 0)
		{
			FBXJoint* parentJoint = &mFBXJoints[mFBXJoints[i].parentIndex];

			mFBXJoints[i].bindPose = parentJoint->bindPose * mFBXJoints[i].bindPose;

			float dx = (float)(mFBXJoints[i].posx - parentJoint->posx);
			float dy = (float)(mFBXJoints[i].posy - parentJoint->posy);
			float dz = (float)(mFBXJoints[i].posz - parentJoint->posz);

			float len = sqrt(dx*dx + dy*dy + dz*dz);
			mFBXJoints[i].mBoneLength = len;
		}		

		//FxOgreFBXLog("%s - Local Trans:( %f,%f,%f) Quat( %f,%f,%f,%f), Scale(%f,%f,%f).\n", pNode->GetName(), translation.mX, translation.mY, translation.mZ, localTM.GetQ()[3], localTM.GetQ()[0], localTM.GetQ()[1], localTM.GetQ()[2], scale.mX, scale.mY, scale.mZ);

	}
	ComputeBindPoseBoundingBox();
}

void FBXReader::LoadBindPose()
{
	//if (!params.useanimframebind)
	{
		int poseCount = mFBXScene->GetPoseCount();
		for (int poseIdx = 0; poseIdx < poseCount; poseIdx++)
		{
			FbxPose* pPose = mFBXScene->GetPose(poseIdx);
			if (pPose->IsBindPose())
			{
				for (int j = 0; j < pPose->GetCount(); ++j)
				{
					FbxNode *pNode = pPose->GetNode(j);
					if (!IsNodeVisible(pNode))
					{
						continue;
					}
					// Only export actual joints here.  Otherwise meshes will create joints unnecessarily.
					if (pNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
					{
						FbxAMatrix globalBindPose;
						globalBindPose = FBXConvertMatrix(pPose->GetMatrix(j));

						int jointIndex = FBXGetJointIndex(pNode);
						if (-1 == jointIndex)
						{
							if (FBXIsValidMatrix(globalBindPose))
							{
								FBXLoadJoint(pNode, globalBindPose);
							}
						}
						else if (globalBindPose != mFBXJoints[jointIndex].globalBindPose)
						{
							//FxOgreFBXLog("Warning: Multiple bind poses found for joint %s.  Setting bind pose to frame 0 and ignoring all stored bind poses.\n", pNode->GetName());
							//params.useanimframebind = true;
							break;
						}
					}
				}
			}
		}
	}

	// Nodes to export using bind pose information calculated from the animation.
	std::vector<FbxNode*> time0skeletonNodes;

	// Iterate through geometry and look for bones that were not incuded in the bind pose
	// but the mesh is weighted to.
	for (int iGeom = 0; iGeom < mFBXScene->GetGeometryCount(); ++iGeom)
	{
		FbxGeometry *pGeom = mFBXScene->GetGeometry(iGeom);
		if (!IsNodeVisible(pGeom->GetNode()))
		{
			continue;
		}
		bool bIsMeshWeighted = false;
		int lSkinCount = pGeom->GetDeformerCount(FbxDeformer::eSkin);
		for (int i = 0; i != lSkinCount; ++i)
		{
			int lClusterCount = ((FbxSkin *) pGeom->GetDeformer(i, FbxDeformer::eSkin))->GetClusterCount();
			for (int j = 0; j < lClusterCount; ++j)
			{
				FbxCluster *pCluster = ((FbxSkin *) pGeom->GetDeformer(i, FbxDeformer::eSkin))->GetCluster(j);
				if (pCluster)
				{
					bIsMeshWeighted = true;
					FbxNode* pNode = pCluster->GetLink();
					FbxAMatrix globalMatrix;
					int jointIndex = FBXGetJointIndex(pNode);

					// Add this joint to the sk
					time0skeletonNodes.push_back(pNode);

					//if (!params.useanimframebind)
					{
						pCluster->GetTransformLinkMatrix(globalMatrix);
						if (-1 == jointIndex)
						{
							//If a bind pose was properly set in the FBX file, this is not needed, but just in case.
							FBXLoadJoint(pNode, globalMatrix);
						}
						else if (globalMatrix != mFBXJoints[jointIndex].globalBindPose)
						{
							//FxOgreFBXLog("Warning: Multiple bind poses found for joint %s on mesh %s.  Setting bind pose to frame 0 and ignoring all stored bind poses.\n", pNode->GetName(), pGeom->GetName());
							//params.useanimframebind = true;
						}
					}
				}
			}
		}
	}
	/*if (params.useanimframebind)
	{
		if (m_joints.size() > 0)
		{
			FxOgreFBXLog("Recalculating transforms for joints based on bindframe.\n");
		}
		// Delete bind pose information stored thus far.
		clear();
		for (size_t i = 0; i < time0skeletonNodes.size(); ++i)
		{
			FbxAMatrix bindTransform = CalculateGlobalTransform(time0skeletonNodes[i], FbxTime(params.bindframe));
			loadJoint(time0skeletonNodes[i], params, bindTransform);
		}
	}*/
	// loop through the geometry again and make sure that parents of unweighted meshes are treated as bones.
	for (int iGeom = 0; iGeom < mFBXScene->GetGeometryCount(); ++iGeom)
	{
		FbxGeometry *pGeom = mFBXScene->GetGeometry(iGeom);
		if (!IsNodeVisible(pGeom->GetNode()))
		{
			continue;
		}
		bool bIsMeshWeighted = false;
		int lSkinCount = pGeom->GetDeformerCount(FbxDeformer::eSkin);
		for (int i = 0; i != lSkinCount; ++i)
		{
			int lClusterCount = ((FbxSkin*)pGeom->GetDeformer(i, FbxDeformer::eSkin))->GetClusterCount();
			for (int j = 0; j < lClusterCount; ++j)
			{
				FbxCluster *pCluster = ((FbxSkin*)pGeom->GetDeformer(i, FbxDeformer::eSkin))->GetCluster(j);
				if (pCluster)
				{
					bIsMeshWeighted = true;
				}
			}
		}
		// If the mesh is unweighted, export it's parent as a bone.
		if (!bIsMeshWeighted)
		{
			FbxNode *pNode = pGeom->GetNode();
			if (pNode && pNode->GetParent())
			{
				// If the mesh is unweighted, export its parent as a bone so we can rigidly skin the mesh.
				FbxAMatrix bindTransform = CalculateGlobalTransformWithBind(pNode, FbxTime(mParamBindframe));

				if (FBXLoadJoint(pNode->GetParent(), bindTransform))
				{
					//FxOgreFBXLog("Unskinned mesh %s found. Added parent %s to the skeleton.\n", pNode->GetName(), pNode->GetParent()->GetName());
				}
			}
		}
	}
}

bool FBXReader::GetVertexBoneWeights(FbxNode* pNode, FbxMesh *pMesh, std::vector<BoneWeightVector>& boneWeightsVector)
{	
	int lSkinCount = pMesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int i = 0; i != lSkinCount; ++i)
	{
		int lClusterCount = ((FbxSkin*)pMesh->GetDeformer(i, FbxDeformer::eSkin))->GetClusterCount();
		for (int j = 0; j < lClusterCount; ++j)
		{
			FbxCluster *pCluster = ((FbxSkin*)pMesh->GetDeformer(i, FbxDeformer::eSkin))->GetCluster(j);
			if (pCluster /*&& m_pSkeleton*/)
			{
				int boneIndex = FBXGetJointIndex(pCluster->GetLink());
				int k, lIndexCount = pCluster->GetControlPointIndicesCount();
				int* lIndices = pCluster->GetControlPointIndices();
				double* lWeights = pCluster->GetControlPointWeights();

				for (k = 0; k < lIndexCount; k++)
				{
					if (boneIndex >= 0)
					{
						FBXBoneWeight boneWeight;
						boneWeight.mBoneIdx = boneIndex;
						boneWeight.mBoneWeight = (float)lWeights[k];
						boneWeightsVector[lIndices[k]].push_back(boneWeight);

						//newweights[lIndices[k]].push_back(static_cast<float>(lWeights[k]));
						//newjointIds[lIndices[k]].push_back(boneIndex);
					}
				}
			}
		}
	}
	return true;
}

bool FBXReader::ReadFile(const StringImpl& fileName, bool loadAnims)
{	
	bool loadDefData = true;

	mModelDef->mLoadDir = GetFileDir(fileName);

	String bfModelFileName = fileName + ".bfmodel";
	if (ReadBFFile(bfModelFileName))
		return true;

	String checkFileName2;

	int atPos = (int)fileName.find('@');
	if (atPos != -1)
	{
		loadDefData = false;
		checkFileName2 = fileName.substr(0, atPos) + ".fbx";

		if (!ReadFile(checkFileName2, false))
			return false;

		for (int jointIdx = 0; jointIdx < (int) mFBXJoints.size(); jointIdx++)
		{
			FBXJoint* fbxJoint = &mFBXJoints[jointIdx];
			fbxJoint->pNode = NULL;
		}
	}

	FbxManager* sdkManager = NULL;
	FbxScene* scene = NULL;	
	
	// Prepare the FBX SDK.
	InitializeSdkObjects(sdkManager, scene);

	mFBXManager = sdkManager;
	mFBXScene = scene;

	FbxImporter* lImporter = FbxImporter::Create(sdkManager, "");

	if (!LoadScene(sdkManager, scene, fileName.c_str(), lImporter))
	{
		lImporter->Destroy();
		return false;
	}
	
	FbxGlobalSettings& globalSettings = scene->GetGlobalSettings();

	FbxTime::EMode timeMode = globalSettings.GetTimeMode();
	mFrameRate = (float) FbxTime::GetFrameRate(timeMode);	
	
	if (!loadDefData)
	{
		// Hook joints back up		
		FindJoints(scene->GetRootNode());		
	}

	/*clipInfo clip;
	clip.name = clipName;
	clip.start = 0;
	clip.stop = 0;
	clip.rate = 1;
	m_params.skelClipList.push_back(clip);
	return true;*/

	
	FbxGlobalSettings& lGlobalSettings = mFBXScene->GetGlobalSettings();
	FbxTime::EMode lTimeMode = lGlobalSettings.GetTimeMode();
	mFPS = (float) FbxTime::GetFrameRate(lTimeMode);
	
	GetAnimationBounds();

	// Loading anims
	//bool stat;
	//size_t i;
	// clear animations list
	mAnimations.clear();

	LoadBindPose();
	AddParentsOfExistingJoints();
	SortAndPruneJoints();
	CalculateLocalTransforms(mFBXScene->GetRootNode());

	//AddFBXAnimationToExisting("Test", mAnimStart, mAnimStop);

	//stat = FBXLoadClip("Test", 0, 10000, mFPS);

	//FBXLoadClip("Test", mAnimStart, mAnimStop, 1.0f/mFPS);

	if (loadAnims)
	{
		int lAnimStackCount = lImporter->GetAnimStackCount();
		for (int i = 0; i < lAnimStackCount; i++)
		{
			FbxTakeInfo* lTakeInfo = lImporter->GetTakeInfo(i);

			FbxTime startTime = lTakeInfo->mLocalTimeSpan.GetStart();
			FbxTime endTime = lTakeInfo->mLocalTimeSpan.GetStop();

			FbxAnimStack* lAnimStack = scene->GetSrcObject<FbxAnimStack>(i);
			scene->SetCurrentAnimationStack(lAnimStack);
			//scene->SetCurrentTake();

			FBXLoadClip(lTakeInfo->mName.Buffer(), (float) startTime.GetSecondDouble(), (float) endTime.GetSecondDouble(), 1.0f / mFPS);
		}
	}

	//FxOgreFBXLog("Loading skeleton animations...\n");
	// load skeleton animation clips for the whole skeleton
	//for (i = 0; i < mParamsSkelClipList.size(); i++)
	{
		//FxOgreFBXLog("Loading clip %s.\n", params.skelClipList[i].name.c_str());
		/*stat = FBXLoadClip(mParamsSkelClipList[i].name, params.skelClipList[i].start,
			params.skelClipList[i].stop, params.skelClipList[i].rate);*/
		/*if (stat == true)
		{
			FxOgreFBXLog("Clip successfully loaded\n");
		}
		else
		{
			FxOgreFBXLog("Failed loading clip\n");
		}*/
	}

	FbxNode* rootNode = scene->GetRootNode();
	if (rootNode != NULL)
	{
		for (int childIdx = 0; childIdx < rootNode->GetChildCount(); childIdx++)
		{
			// Call recursive translateNode once for each child of the root node
			FbxNode* childNode = rootNode->GetChild(childIdx);
			TranslateNode(childNode);
		}
	}	

	mModelDef->mFrameRate = mFPS;

	if (loadDefData)
	{
		mModelDef->mMeshes.resize(mMeshes.size());
		mModelDef->mJoints.resize(mFBXJoints.size());

		for (int meshIdx = 0; meshIdx < (int) mMeshes.size(); meshIdx++)
		{
			FBXMesh* fbxMesh = mMeshes[meshIdx];
			ModelMesh* mesh = &mModelDef->mMeshes[meshIdx];

			mesh->mName = fbxMesh->mName;
			mesh->mTexFileName = fbxMesh->mMaterial.mTexFileName;
			mesh->mBumpFileName = fbxMesh->mMaterial.mBumpFileName;

			mesh->mIndices.resize(fbxMesh->mIndexData.size());
			for (int idxIdx = 0; idxIdx < (int) fbxMesh->mIndexData.size(); idxIdx++)
				mesh->mIndices[idxIdx] = (uint16) fbxMesh->mIndexData[idxIdx];

			BF_ASSERT(fbxMesh->mVertexData.size() < 0x10000);
			mesh->mVertices.resize(fbxMesh->mVertexData.size());
			for (int vtxIdx = 0; vtxIdx < (int) fbxMesh->mVertexData.size(); vtxIdx++)
			{
				FBXVertexData* fbxVertex = &fbxMesh->mVertexData[vtxIdx];
				ModelVertex* vertex = &mesh->mVertices[vtxIdx];

				vertex->mPosition = fbxVertex->mCoords;
				vertex->mColor = fbxVertex->mColor;
				if (fbxVertex->mTexCoords.size() != 0)
					vertex->mTexCoords = TexCoords::FlipV(fbxVertex->mTexCoords[0]);
				if (fbxVertex->mBumpTexCoords.size() != 0)
					vertex->mBumpTexCoords = TexCoords::FlipV(fbxVertex->mBumpTexCoords[0]);
				vertex->mNormal = fbxVertex->mNormal;
				vertex->mTangent = fbxVertex->mTangent;
				vertex->mNumBoneWeights = (int) fbxVertex->mBoneWeights.size();
				BF_ASSERT(vertex->mNumBoneWeights <= MODEL_MAX_BONE_WEIGHTS);
				for (int boneWeightIdx = 0; boneWeightIdx < vertex->mNumBoneWeights; boneWeightIdx++)
				{
					vertex->mBoneIndices[boneWeightIdx] = fbxVertex->mBoneWeights[boneWeightIdx].mBoneIdx;
					vertex->mBoneWeights[boneWeightIdx] = fbxVertex->mBoneWeights[boneWeightIdx].mBoneWeight;
				}
			}
		}

		for (int jointIdx = 0; jointIdx < (int) mFBXJoints.size(); jointIdx++)
		{
			FBXJoint* fbxJoint = &mFBXJoints[jointIdx];
			ModelJoint* joint = &mModelDef->mJoints[jointIdx];

			joint->mName = fbxJoint->name;
			joint->mParentIdx = fbxJoint->parentIndex;
			auto invGlobalMtx = fbxJoint->globalBindPose.Inverse();
			for (int row = 0; row < 4; row++)
				for (int col = 0; col < 4; col++)
					joint->mPoseInvMatrix.mMat[row][col] = (float) invGlobalMtx.Get(col, row);
		}
	}

	mModelDef->mAnims.resize(mAnimations.size());
	for (int animIdx = 0; animIdx < (int)mAnimations.size(); animIdx++)
	{
		FBXAnimation* fbxAnimation = &mAnimations[animIdx];		
		ModelAnimation* animation = &mModelDef->mAnims[animIdx];

		animation->mName = fbxAnimation->mName;

		animation->mFrames.resize((int)fbxAnimation->mTracks[0].mSkeletonKeyframes.size());
		for (int frameIdx = 0; frameIdx < (int)animation->mFrames.size(); frameIdx++)
			animation->mFrames[frameIdx].mJointTranslations.resize(mFBXJoints.size());

		for (int trackIdx = 0; trackIdx < (int)fbxAnimation->mTracks.size(); trackIdx++)
		{
			FBXTrack* fbxTrack = &fbxAnimation->mTracks[trackIdx];

			for (int frameIdx = 0; frameIdx < (int)fbxTrack->mSkeletonKeyframes.size(); frameIdx++)
			{
				FBXSkeletonKeyframe* fbxSkeletonKeyframe = &fbxTrack->mSkeletonKeyframes[frameIdx];
				ModelAnimationFrame* animFrame = &animation->mFrames[frameIdx];				
				ModelJointTranslation* jointPosition = &animFrame->mJointTranslations[trackIdx];

				jointPosition->mQuat = Quaternion((float)fbxSkeletonKeyframe->quat_x, (float)fbxSkeletonKeyframe->quat_y, (float)fbxSkeletonKeyframe->quat_z, (float)fbxSkeletonKeyframe->quat_w);
				jointPosition->mScale = Vector3((float)fbxSkeletonKeyframe->sx, (float)fbxSkeletonKeyframe->sy, (float)fbxSkeletonKeyframe->sz);
				jointPosition->mTrans = Vector3((float)fbxSkeletonKeyframe->tx, (float)fbxSkeletonKeyframe->ty, (float)fbxSkeletonKeyframe->tz);
			}
		}		
	}
	//mModelDef->mAnims

	lImporter->Destroy();

	if (loadAnims)	
		WriteBFFile(bfModelFileName, fileName, checkFileName2);	

	return true;
}

bool Beefy::FBXReader::WriteBFFile(const StringImpl& fileName, const StringImpl& checkFile, const StringImpl& checkFile2)
{
	FILE* fp = fopen(fileName.c_str(), "wb");
	if (fp == NULL)
		return false;

	FileStream fs;
	fs.mFP = fp;

	String fileDir = GetFileDir(checkFile);

	fs.Write(BF_MODEL_VERSION);
	fs.Write(GetFileName(checkFile));
	fs.Write(GetFileTimeWrite(checkFile));
	fs.Write(GetFileName(checkFile2));
	fs.Write(GetFileTimeWrite(checkFile2));
	
	fs.Write(mModelDef->mFrameRate);

	fs.Write((int)mModelDef->mMeshes.size());
	for (int meshIdx = 0; meshIdx < (int)mModelDef->mMeshes.size(); meshIdx++)
	{
		ModelMesh* modelMesh = &mModelDef->mMeshes[meshIdx];
		fs.Write(modelMesh->mName);

		fs.Write((int)modelMesh->mIndices.size());
		fs.Write((void*)&modelMesh->mIndices[0], modelMesh->mIndices.size() * sizeof(modelMesh->mIndices[0]));

		fs.Write((int) modelMesh->mVertices.size());
		fs.Write((void*)&modelMesh->mVertices[0], modelMesh->mVertices.size() * sizeof(modelMesh->mVertices[0]));

		fs.Write(modelMesh->mTexFileName);
		fs.Write(modelMesh->mBumpFileName);		
	}

	fs.Write((int)mModelDef->mJoints.size());
	for (int jointIdx = 0; jointIdx < (int)mModelDef->mJoints.size(); jointIdx++)
	{
		ModelJoint* modelJoint = &mModelDef->mJoints[jointIdx];
		fs.Write(modelJoint->mName);
		fs.Write(modelJoint->mParentIdx);
		fs.WriteT(modelJoint->mPoseInvMatrix);
	}

	fs.Write((int)mModelDef->mAnims.size());
	for (int animIdx = 0; animIdx < (int)mModelDef->mAnims.size(); animIdx++)
	{
		ModelAnimation* modelAnim = &mModelDef->mAnims[animIdx];
		fs.Write(modelAnim->mName);
		
		fs.Write((int)modelAnim->mFrames.size());
		for (int frameIdx = 0; frameIdx < (int)modelAnim->mFrames.size(); frameIdx++)
		{
			ModelAnimationFrame* frame = &modelAnim->mFrames[frameIdx];
			BF_ASSERT(mModelDef->mJoints.size() == frame->mJointTranslations.size());
			fs.Write((void*)&frame->mJointTranslations[0], (int)frame->mJointTranslations.size() * sizeof(frame->mJointTranslations[0]));
		}
	}
	
	return true;
}

bool Beefy::FBXReader::ReadBFFile(const StringImpl& fileName)
{
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (fp == NULL)
		return false;

	FileStream fs;
	fs.mFP = fp;

	int version = fs.ReadInt32();
	if (version != BF_MODEL_VERSION)
		return false;

	String fileDir = GetFileDir(fileName);

	String checkFileName = fs.ReadAscii32SizedString();
	int64 storedTime = fs.ReadInt64();
	int64 localTime = GetFileTimeWrite(fileDir + checkFileName);	
	if (storedTime != localTime)
		return false;

	checkFileName = fs.ReadAscii32SizedString();	
	storedTime = fs.ReadInt64();
	if (checkFileName.length() != 0)
	{
		localTime = GetFileTimeWrite(fileDir + checkFileName);
		if (storedTime != localTime)
			return false;
	}

	mModelDef->mFrameRate = fs.ReadFloat();

	mModelDef->mMeshes.resize(fs.ReadInt32());
	for (int meshIdx = 0; meshIdx < (int) mModelDef->mMeshes.size(); meshIdx++)
	{
		ModelMesh* modelMesh = &mModelDef->mMeshes[meshIdx];
		modelMesh->mName = fs.ReadAscii32SizedString();

		modelMesh->mIndices.resize(fs.ReadInt32());
		fs.Read((void*)&modelMesh->mIndices[0], modelMesh->mIndices.size() * sizeof(modelMesh->mIndices[0]));

		modelMesh->mVertices.resize(fs.ReadInt32());
		fs.Read((void*)&modelMesh->mVertices[0], modelMesh->mVertices.size() * sizeof(modelMesh->mVertices[0]));

		modelMesh->mTexFileName = fs.ReadAscii32SizedString();
		modelMesh->mBumpFileName = fs.ReadAscii32SizedString();
	}

	mModelDef->mJoints.resize(fs.ReadInt32());
	for (int jointIdx = 0; jointIdx < (int)mModelDef->mJoints.size(); jointIdx++)
	{
		ModelJoint* modelJoint = &mModelDef->mJoints[jointIdx];
		modelJoint->mName = fs.ReadAscii32SizedString();
		modelJoint->mParentIdx = fs.ReadInt32();
		fs.ReadT(modelJoint->mPoseInvMatrix);
	}

	mModelDef->mAnims.resize(fs.ReadInt32());
	for (int animIdx = 0; animIdx < (int)mModelDef->mAnims.size(); animIdx++)
	{
		ModelAnimation* modelAnim = &mModelDef->mAnims[animIdx];
		modelAnim->mName = fs.ReadAscii32SizedString();

		modelAnim->mFrames.resize(fs.ReadInt32());
		for (int frameIdx = 0; frameIdx < (int)modelAnim->mFrames.size(); frameIdx++)
		{
			ModelAnimationFrame* frame = &modelAnim->mFrames[frameIdx];
			frame->mJointTranslations.resize(mModelDef->mJoints.size());
			fs.Read((void*)&frame->mJointTranslations[0], (int)frame->mJointTranslations.size() * sizeof(frame->mJointTranslations[0]));
		}
	}

	return true;
}

////

BF_EXPORT ModelDef* BF_CALLTYPE Res_OpenFBX(const char* fileName, VertexDefinition* vertexDefinition)
{
	ModelDef* modelDef = new ModelDef();
	FBXReader fbxReader(modelDef);
	if (!fbxReader.ReadFile(fileName))
	{
		delete modelDef;
		return NULL;
	}

	return modelDef;
}

#else

BF_EXPORT void* BF_CALLTYPE Res_OpenFBX(const char* fileName, void* vertexDefinition)
{
	return NULL;
}

#endif