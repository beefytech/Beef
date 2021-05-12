#pragma once

#include "Common.h"
#include "gfx/ModelDef.h"
#include "gfx/RenderCmd.h"
#include "util/Matrix4.h"

NS_BF_BEGIN;

class ModelInstance : public RenderCmd
{
public:
	ModelDef* mModelDef;	
	Array<ModelJointTranslation> mJointTranslations;
	Array<bool> mMeshesVisible;

public:
	ModelInstance(ModelDef* modelDef);

	virtual void Free() override {}	
	virtual void SetJointPosition(int jointIdx, const ModelJointTranslation& jointTranslation);
};

NS_BF_END;