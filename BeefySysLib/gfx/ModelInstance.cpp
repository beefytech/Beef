#include "ModelInstance.h"

USING_NS_BF;

ModelInstance::ModelInstance(ModelDef* modelDef)
{
	mNext = NULL;
	mModelDef = modelDef;
	mJointTranslations.Resize(mModelDef->mJoints.size());	
	mMeshesVisible.Insert(0, mModelDef->mMeshes.size(), true);
}

void Beefy::ModelInstance::SetJointPosition(int jointIdx, const ModelJointTranslation& jointTranslation)
{
	mJointTranslations[jointIdx] = jointTranslation;
}

///

BF_EXPORT void BF_CALLTYPE ModelInstance_SetJointTranslation(ModelInstance* modelInstance, int jointIdx, const ModelJointTranslation& jointTranslation)
{
	modelInstance->SetJointPosition(jointIdx, jointTranslation);
}

BF_EXPORT void BF_CALLTYPE ModelInstance_SetMeshVisibility(ModelInstance* modelInstance, int meshIdx, int visible)
{
	modelInstance->mMeshesVisible[meshIdx] = visible != 0;
}


