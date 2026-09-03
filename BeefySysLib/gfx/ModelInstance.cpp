#include "ModelInstance.h"

USING_NS_BF;

ModelInstance::ModelInstance(ModelDef* modelDef)
{
	mModelDef = modelDef;
	SetBindPose();
	mMeshesVisible.Insert(0, true, mModelDef->mMeshes.size());
	mDirty = true;
}

// The same walk the engine does when composing a pose (parent-chain multiply, with
// mArmatureToWorld folded into root joints, then each joint's mPoseInvMatrix) -- run once here on
// the bind-pose locals, so an instance no one animates still skins to the authored bind pose.
void Beefy::ModelInstance::SetBindPose()
{
	int jointCount = (int)mModelDef->mJoints.size();
	mJointMatrices.Resize(jointCount);
	for (int jointIdx = 0; jointIdx < jointCount; jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];

		BF_ASSERT(joint->mParentIdx < jointIdx);

		const ModelJointTranslation* jointPosition = &joint->mBindPoseLocal;

		Matrix4* mtx = &mJointMatrices[jointIdx];

		*mtx = Matrix4::CreateTransform(jointPosition->mTrans, jointPosition->mScale, jointPosition->mQuat);
		if (joint->mParentIdx >= 0)
		{
			Matrix4* parentMatrix = &mJointMatrices[joint->mParentIdx];
			*mtx = Matrix4::Multiply(*parentMatrix, *mtx);
		}
		else
		{
			// Root joints only carry their transform relative to whatever's above them in the FBX node
			// hierarchy (see FBXReader.cpp's ModelDef::mArmatureToWorld comment).
			*mtx = Matrix4::Multiply(mModelDef->mArmatureToWorld, *mtx);
		}
	}

	for (int jointIdx = 0; jointIdx < jointCount; jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];
		Matrix4* mtx = &mJointMatrices[jointIdx];
		*mtx = Matrix4::Multiply(*mtx, joint->mPoseInvMatrix);
	}

	mDirty = true;
}

///

BF_EXPORT void BF_CALLTYPE ModelInstance_SetJointMatrices(ModelInstance* modelInstance, Matrix4* matrices, int32 count)
{
	BF_ASSERT(count == modelInstance->mJointMatrices.mSize);
	memcpy(modelInstance->mJointMatrices.mVals, matrices, count * sizeof(Matrix4));
	modelInstance->mDirty = true;
}

BF_EXPORT void BF_CALLTYPE ModelInstance_SetMeshVisibility(ModelInstance* modelInstance, int meshIdx, int visible)
{
	modelInstance->mMeshesVisible[meshIdx] = visible != 0;
	modelInstance->mDirty = true;
}
