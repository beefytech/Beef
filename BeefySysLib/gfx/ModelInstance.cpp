#include "ModelInstance.h"

USING_NS_BF;

ModelInstance::ModelInstance(ModelDef* modelDef)
{
	mModelDef = modelDef;
	mJointTranslations.Resize(mModelDef->mJoints.size());
	for (int i = 0; i < (int)mModelDef->mJoints.size(); i++)
		mJointTranslations[i] = mModelDef->mJoints[i].mBindPoseLocal;
	mMeshesVisible.Insert(0, true, mModelDef->mMeshes.size());
	mDirty = true;
}

void Beefy::ModelInstance::SetJointPosition(int jointIdx, const ModelJointTranslation& jointTranslation)
{
	mJointTranslations[jointIdx] = jointTranslation;
	mDirty = true;
}

///

BF_EXPORT void BF_CALLTYPE ModelInstance_SetJointTranslation(ModelInstance* modelInstance, int jointIdx, const ModelJointTranslation& jointTranslation)
{
	modelInstance->SetJointPosition(jointIdx, jointTranslation);
}

BF_EXPORT void BF_CALLTYPE ModelInstance_SetMeshVisibility(ModelInstance* modelInstance, int meshIdx, int visible)
{
	modelInstance->mMeshesVisible[meshIdx] = visible != 0;
	modelInstance->mDirty = true;
}


