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
	// The final skinning palette, one matrix per joint: model-space joint pose times the joint's
	// mPoseInvMatrix. Fed whole by the engine (ModelInstance_SetJointMatrices) -- all sampling,
	// blending and hierarchy composition happen engine-side. Initialized to the bind pose.
	Array<Matrix4> mJointMatrices;
	Array<bool> mMeshesVisible;
	// Set whenever the palette or mesh visibility change; cleared once the skinned vertex
	// buffers have been recomputed. Lets CommandQueued skip re-skinning when nothing has changed,
	// including across the multiple times a single instance may be queued within the same frame.
	bool mDirty;

public:
	ModelInstance(ModelDef* modelDef);

	void SetBindPose();
};

NS_BF_END;
