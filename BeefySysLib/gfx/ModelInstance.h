#pragma once

#include "Common.h"
#include "gfx/ModelDef.h"
#include "gfx/Renderable.h"
#include "util/Matrix4.h"

NS_BF_BEGIN;

// A ModelInstance is Renderable, not a RenderCmd: it's a long-lived, app-owned object (unlike the
// pooled, per-frame RenderCmds), and must be queueable more than once at a time -- eg drawn into
// the main scene and into an offscreen selection mask in the same frame. See DrawLayer::QueueRenderable.
class ModelInstance : public Renderable
{
public:
	ModelDef* mModelDef;
	Array<ModelJointTranslation> mJointTranslations;
	Array<bool> mMeshesVisible;

public:
	ModelInstance(ModelDef* modelDef);

	virtual void SetJointPosition(int jointIdx, const ModelJointTranslation& jointTranslation);
};

NS_BF_END;