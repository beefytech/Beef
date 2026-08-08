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

void Beefy::ModelInstance::ComputeSkinningJointMatrices(Matrix4* outMatrices) const
{
	for (int jointIdx = 0; jointIdx < (int)mJointTranslations.size(); jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];

		BF_ASSERT(joint->mParentIdx < jointIdx);

		const ModelJointTranslation* jointPosition = &mJointTranslations[jointIdx];

		Matrix4* mtx = &outMatrices[jointIdx];

		*mtx = Matrix4::CreateTransform(jointPosition->mTrans, jointPosition->mScale, jointPosition->mQuat);
		if (joint->mParentIdx >= 0)
		{
			 Matrix4* parentMatrix = &outMatrices[joint->mParentIdx];
			 *mtx = Matrix4::Multiply(*parentMatrix, *mtx);
		}
		else
		{
			// Root joints only carry their transform relative to whatever's above them in the FBX node
			// hierarchy (see FBXReader.cpp's ModelDef::mArmatureToWorld comment) -- fold that back in here
			// so it propagates to every descendant through the parent-chain multiply above.
			*mtx = Matrix4::Multiply(mModelDef->mArmatureToWorld, *mtx);
		}
	}

	for (int jointIdx = 0; jointIdx < (int)mModelDef->mJoints.size(); jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];
		Matrix4* mtx = &outMatrices[jointIdx];
		*mtx = Matrix4::Multiply(*mtx, joint->mPoseInvMatrix);
	}
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


