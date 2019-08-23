#include "ModelDef.h"
#include "BFApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/ModelInstance.h"

USING_NS_BF;

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
	modelAnimation->mFrames.erase(modelAnimation->mFrames.begin(), modelAnimation->mFrames.begin() + startFrame);
	modelAnimation->mFrames.erase(modelAnimation->mFrames.begin() + numFrames, modelAnimation->mFrames.end());
}



