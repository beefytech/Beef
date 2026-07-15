#include "FBXReader.h"
#include "BFApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/DrawLayer.h"
#include "gfx/ModelInstance.h"
#include "FileStream.h"
#include "util/Dictionary.h"

#ifndef BF_NO_FBX

#include "ufbx/ufbx.c"

#pragma warning(disable:4996)

USING_NS_BF;

namespace boost
{
	static inline std::size_t hash_value(const FBXVertexData& vtxData)
	{
		uint32* data = (uint32*)&vtxData.mCoords;
		return (data[0] ^ data[1] ^ data[2]);
	}
}

static Matrix4 UfbxToMatrix4(const ufbx_matrix& m)
{
	Matrix4 r;
	r.mMat[0][0] = (float)m.m00; r.mMat[0][1] = (float)m.m01; r.mMat[0][2] = (float)m.m02; r.mMat[0][3] = (float)m.m03;
	r.mMat[1][0] = (float)m.m10; r.mMat[1][1] = (float)m.m11; r.mMat[1][2] = (float)m.m12; r.mMat[1][3] = (float)m.m13;
	r.mMat[2][0] = (float)m.m20; r.mMat[2][1] = (float)m.m21; r.mMat[2][2] = (float)m.m22; r.mMat[2][3] = (float)m.m23;
	r.mMat[3][0] = 0;             r.mMat[3][1] = 0;             r.mMat[3][2] = 0;             r.mMat[3][3] = 1;
	return r;
}

FBXReader::FBXReader(ModelDef* modelDef)
{
	mModelDef = modelDef;
	mFPS = 30.0f;
}

FBXReader::~FBXReader()
{
	for (int i = 0; i < (int)mMeshes.size(); i++)
		delete mMeshes[i];
	mMeshes.Clear();
}

bool FBXReader::ReadFile(const StringImpl& fileName, bool loadAnims)
{
	bool loadDefData = true;

	if (mModelDef->mLoadDir.IsEmpty())
		mModelDef->mLoadDir = GetFileDir(fileName);

	String bfModelFileName = fileName + ".bfmodel";
	if (ReadBFFile(bfModelFileName))
		return true;

	String checkFileName2;

	int atPos = (int)fileName.IndexOf('@');
	if (atPos != -1)
	{
		loadDefData = false;
		checkFileName2 = fileName.Substring(0, atPos) + ".fbx";
		if (!ReadFile(checkFileName2, false))
			return false;
	}

	ufbx_load_opts opts = {};
	opts.generate_missing_normals = true;

	ufbx_error error;
	ufbx_scene* scene = ufbx_load_file(fileName.c_str(), &opts, &error);
	if (!scene)
		return false;

	mFPS = (float)scene->settings.frames_per_second;
	if (mFPS <= 0.0f)
		mFPS = 30.0f;
	mModelDef->mFrameRate = mFPS;

	// --- Collect joints from skin clusters ---
	if (loadDefData)
	{
		mFBXJoints.Clear();
		mJointIndexMap.Clear();

		// Mark which nodes are bones (referenced by any skin cluster)
		Dictionary<uint32_t, bool> boneNodeSet;
		for (size_t si = 0; si < scene->skin_clusters.count; si++)
		{
			ufbx_skin_cluster* cluster = scene->skin_clusters.data[si];
			if (!cluster->bone_node) continue;
			// Walk up from this bone to mark ancestors too
			ufbx_node* n = cluster->bone_node;
			while (n && (n != scene->root_node))
			{
				uint32_t id = n->element.typed_id;
				if (boneNodeSet.Find(id) != boneNodeSet.end())
					break;
				boneNodeSet[id] = true;
				n = n->parent;
			}
		}

		// Add joints in parent-first order via recursive helper
		std::function<void(ufbx_node*)> addJoint = [&](ufbx_node* node)
		{
			if (!node || (node == scene->root_node)) return;
			uint32_t id = node->element.typed_id;
			//if (boneNodeSet.find(id) == boneNodeSet.end()) return;
			if (!boneNodeSet.ContainsKey(id)) return;
			String name = node->name.data;
			//if (mJointIndexMap.find(name) != mJointIndexMap.end()) return;
			if (mJointIndexMap.ContainsKey(name)) return;

			// Add parent first
			if (node->parent && (node->parent != scene->root_node))
				addJoint(node->parent);

			FBXJoint joint = {};
			joint.name = node->name.data;
			joint.parentIndex = -1;
			joint.mBoneLength = 0;
			joint.bInheritScale = true;

			if (node->parent && (node->parent != scene->root_node))
			{
				joint.parentName = node->parent->name.data;
				auto it = mJointIndexMap.Find(joint.parentName);
				if (it != mJointIndexMap.end())
					joint.parentIndex = it->mValue;
			}

			// Inverse global bind pose
			ufbx_matrix invGlobal = ufbx_matrix_invert(&node->node_to_world);
			joint.mGlobalBindPoseInv = UfbxToMatrix4(invGlobal);

			// Local bind pose TRS from node's rest transform
			ufbx_transform lt = node->local_transform;
			joint.posx = lt.translation.x;
			joint.posy = lt.translation.y;
			joint.posz = lt.translation.z;
			joint.quatx = lt.rotation.x;
			joint.quaty = lt.rotation.y;
			joint.quatz = lt.rotation.z;
			joint.quatw = lt.rotation.w;
			joint.scalex = (float)lt.scale.x;
			joint.scaley = (float)lt.scale.y;
			joint.scalez = (float)lt.scale.z;

			// Bone length = distance from parent translation
			if (joint.parentIndex >= 0)
			{
				float dx = (float)joint.posx;
				float dy = (float)joint.posy;
				float dz = (float)joint.posz;
				joint.mBoneLength = sqrtf(dx*dx + dy*dy + dz*dz);
			}

			if ((int)mFBXJoints.size() < BF_MAX_NUM_BONES)
			{
				joint.id = (int)mFBXJoints.size();
				mJointIndexMap[joint.name] = joint.id;
				mFBXJoints.Add(joint);
			}
		};

		for (size_t si = 0; si < scene->skin_clusters.count; si++)
		{
			ufbx_skin_cluster* cluster = scene->skin_clusters.data[si];
			if (cluster->bone_node)
				addJoint(cluster->bone_node);
		}
	}

	// --- Load meshes ---
	if (loadDefData)
	{
		uint32_t triIndicesBuf[1024];

		for (size_t mi = 0; mi < scene->meshes.count; mi++)
		{
			ufbx_mesh* mesh = scene->meshes.data[mi];
			if (mesh->instances.count == 0) continue;
			ufbx_node* meshNode = mesh->instances.data[0];

			FBXMesh* fbxMesh = new FBXMesh();
			fbxMesh->mName = meshNode->name.data;

			// Texture from material
			if (mesh->materials.count > 0)
			{
				ufbx_material* mat = mesh->materials.data[0];
				if (mat)
				{
					ufbx_texture* diffTex = mat->fbx.diffuse_color.texture;
					if (!diffTex && mat->textures.count > 0)
						diffTex = mat->textures.data[0].texture;
					if (diffTex)
					{
						String fn = diffTex->filename.data;
						int slashPos = BF_MAX((int)fn.LastIndexOf('\\'), (int)fn.LastIndexOf('/'));
						if (slashPos >= 0)
							fn = fn.Substring(slashPos + 1);
						fbxMesh->mMaterial.mTexFileName = fn;
					}
				}
			}

			// Build per-position bone weight vectors
			size_t numPositions = mesh->vertex_position.values.count;
			std::vector<BoneWeightVector> boneWeights(numPositions);

			if (mesh->skin_deformers.count > 0)
			{
				ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
				for (size_t ci = 0; ci < skin->clusters.count; ci++)
				{
					ufbx_skin_cluster* cluster = skin->clusters.data[ci];
					if (!cluster->bone_node) continue;
					String boneName = cluster->bone_node->name.data;
					auto it = mJointIndexMap.Find(boneName);
					if (it == mJointIndexMap.end()) continue;
					int boneIdx = it->mValue;

					for (size_t wi = 0; wi < cluster->vertices.count; wi++)
					{
						uint32_t posIdx = cluster->vertices.data[wi];
						float weight = (float)cluster->weights.data[wi];
						if ((posIdx < numPositions) && (weight > 0.0f))
						{
							FBXBoneWeight bw;
							bw.mBoneIdx = boneIdx;
							bw.mBoneWeight = weight;
							boneWeights[posIdx].Add(bw);
						}
					}
				}
			}

			// Triangulate faces and collect vertex data
			std::vector<FBXVertexData> unpackedVtx;
			unpackedVtx.reserve(mesh->num_indices);

			for (size_t fi = 0; fi < mesh->faces.count; fi++)
			{
				ufbx_face face = mesh->faces.data[fi];
				if (face.num_indices < 3) continue;

				uint32_t numTris = ufbx_triangulate_face(triIndicesBuf, 1024, mesh, face);
				for (uint32_t ti = 0; ti < numTris; ti++)
				{
					for (int vi = 0; vi < 3; vi++)
					{
						uint32_t cornerIdx = triIndicesBuf[ti * 3 + vi];

						FBXVertexData vd = {};
						vd.mColor = 0xFFFFFFFF;

						// Position
						uint32_t posIdx = mesh->vertex_position.indices.data[cornerIdx];
						ufbx_vec3 pos = mesh->vertex_position.values.data[posIdx];
						vd.mCoords = Vector3((float)pos.x, (float)pos.y, (float)pos.z);

						// Normal
						if (mesh->vertex_normal.exists)
						{
							uint32_t normIdx = mesh->vertex_normal.indices.data[cornerIdx];
							ufbx_vec3 norm = mesh->vertex_normal.values.data[normIdx];
							vd.mNormal = Vector3((float)norm.x, (float)norm.y, (float)norm.z);
						}

						// UV
						if (mesh->vertex_uv.exists)
						{
							uint32_t uvIdx = mesh->vertex_uv.indices.data[cornerIdx];
							ufbx_vec2 uv = mesh->vertex_uv.values.data[uvIdx];
							vd.mTexCoords.push_back(TexCoords((float)uv.x, (float)uv.y));
						}

						// Tangent
						if (mesh->vertex_tangent.exists)
						{
							uint32_t tanIdx = mesh->vertex_tangent.indices.data[cornerIdx];
							ufbx_vec3 tan = mesh->vertex_tangent.values.data[tanIdx];
							vd.mTangent = Vector3((float)tan.x, (float)tan.y, (float)tan.z);
						}

						// Bone weights (keyed by position index)
						vd.mBoneWeights = boneWeights[posIdx];

						unpackedVtx.push_back(vd);
					}
				}
			}

			// Deduplicate vertices
			typedef Dictionary<FBXVertexData, int> VertexDataMap;
			VertexDataMap usedVerts;
			for (int vi = 0; vi < (int)unpackedVtx.size(); vi++)
			{
				FBXVertexData* vd = &unpackedVtx[vi];
				auto itr = usedVerts.Find(*vd);
				if (itr != usedVerts.end())
				{
					fbxMesh->mIndexData.push_back(itr->mValue);
				}
				else
				{
					int idx = (int)fbxMesh->mVertexData.size();
					usedVerts[*vd] = idx;
					fbxMesh->mVertexData.push_back(*vd);
					fbxMesh->mIndexData.push_back(idx);
				}
			}

			mMeshes.push_back(fbxMesh);
		}
	}

	// --- Load animations ---
	if (loadAnims)
	{
		mAnimations.Clear();

		for (size_t ai = 0; ai < scene->anim_stacks.count; ai++)
		{
			ufbx_anim_stack* stack = scene->anim_stacks.data[ai];
			double startTime = stack->time_begin;
			double endTime = stack->time_end;
			if (endTime <= startTime) continue;

			ufbx_bake_opts bakeOpts = {};
			bakeOpts.resample_rate = mFPS;
			bakeOpts.minimum_sample_rate = mFPS;

			ufbx_error bakeErr;
			ufbx_baked_anim* bake = ufbx_bake_anim(scene, stack->anim, &bakeOpts, &bakeErr);
			if (!bake) continue;

			// Map joint name -> baked node index
			std::map<int, int> jointToBakeIdx;
			for (size_t bni = 0; bni < bake->nodes.count; bni++)
			{
				ufbx_baked_node& bn = bake->nodes.data[bni];
				if (bn.typed_id >= scene->nodes.count) continue;
				String nodeName = scene->nodes.data[bn.typed_id]->name.data;
				auto it = mJointIndexMap.Find(nodeName);
				if (it != mJointIndexMap.end())
					jointToBakeIdx[it->mValue] = (int)bni;
			}

			// Determine frame count from any baked joint
			int numFrames = 0;
			for (auto& kv : jointToBakeIdx)
			{
				ufbx_baked_node& bn = bake->nodes.data[kv.second];
				int tc = (int)bn.translation_keys.count;
				if (tc > numFrames) numFrames = tc;
			}

			if (numFrames == 0)
			{
				ufbx_free_baked_anim(bake);
				continue;
			}

			FBXAnimation anim;
			anim.mName = stack->name.data;
			anim.mLength = (float)(endTime - startTime);

			for (int ji = 0; ji < (int)mFBXJoints.size(); ji++)
			{
				FBXTrack track;
				track.mBone = mFBXJoints[ji].name;

				auto it = jointToBakeIdx.find(ji);
				if (it != jointToBakeIdx.end())
				{
					ufbx_baked_node& bn = bake->nodes.data[it->second];
					int frameCount = (int)bn.translation_keys.count;

					for (int fi = 0; fi < frameCount; fi++)
					{
						FBXSkeletonKeyframe key = {};
						key.time = (float)(bn.translation_keys.data[fi].time - startTime);

						ufbx_vec3 t = bn.translation_keys.data[fi].value;
						key.tx = t.x; key.ty = t.y; key.tz = t.z;

						if (fi < (int)bn.rotation_keys.count)
						{
							ufbx_quat q = bn.rotation_keys.data[fi].value;
							key.quat_x = q.x; key.quat_y = q.y;
							key.quat_z = q.z; key.quat_w = q.w;
						}
						else if (bn.rotation_keys.count > 0)
						{
							ufbx_quat q = bn.rotation_keys.data[bn.rotation_keys.count - 1].value;
							key.quat_x = q.x; key.quat_y = q.y;
							key.quat_z = q.z; key.quat_w = q.w;
						}
						else
						{
							key.quat_x = 0; key.quat_y = 0; key.quat_z = 0; key.quat_w = 1;
						}

						if (fi < (int)bn.scale_keys.count)
						{
							ufbx_vec3 s = bn.scale_keys.data[fi].value;
							key.sx = (float)s.x; key.sy = (float)s.y; key.sz = (float)s.z;
						}
						else
						{
							key.sx = 1.0f; key.sy = 1.0f; key.sz = 1.0f;
						}

						track.mSkeletonKeyframes.push_back(key);
					}
				}

				// Fill missing joint keys with bind-pose TRS
				if (track.mSkeletonKeyframes.empty())
				{
					FBXJoint& fj = mFBXJoints[ji];
					for (int fi = 0; fi < numFrames; fi++)
					{
						FBXSkeletonKeyframe key = {};
						key.time = fi / mFPS;
						key.tx = fj.posx; key.ty = fj.posy; key.tz = fj.posz;
						key.quat_x = fj.quatx; key.quat_y = fj.quaty;
						key.quat_z = fj.quatz; key.quat_w = fj.quatw;
						key.sx = fj.scalex; key.sy = fj.scaley; key.sz = fj.scalez;
						track.mSkeletonKeyframes.push_back(key);
					}
				}

				anim.mTracks.push_back(track);
			}

			if (!anim.mTracks.empty())
				mAnimations.push_back(anim);

			ufbx_free_baked_anim(bake);
		}
	}

	// --- Build ModelDef ---
	if (loadDefData)
	{
		// Collect inverse bind matrices from skin clusters (prefer cluster data over computed)
		std::map<int, ufbx_matrix> jointInvBindMap;
		for (size_t si = 0; si < scene->skin_clusters.count; si++)
		{
			ufbx_skin_cluster* cluster = scene->skin_clusters.data[si];
			if (!cluster->bone_node) continue;
			String boneName = cluster->bone_node->name.data;
			auto it = mJointIndexMap.Find(boneName);
			if (it == mJointIndexMap.end()) continue;
			int ji = it->mValue;
			if (jointInvBindMap.find(ji) == jointInvBindMap.end())
				jointInvBindMap[ji] = cluster->geometry_to_bone;
		}

		mModelDef->mMeshes.Resize(mMeshes.size());
		mModelDef->mJoints.Resize(mFBXJoints.size());

		for (int meshIdx = 0; meshIdx < (int)mMeshes.size(); meshIdx++)
		{
			FBXMesh* fbxMesh = mMeshes[meshIdx];
			ModelMesh* mesh = &mModelDef->mMeshes[meshIdx];
			mesh->mName = fbxMesh->mName;

			mesh->mPrimitives.Add(ModelPrimitives());
			ModelPrimitives* prims = &mesh->mPrimitives[0];
			prims->mFlags = (ModelPrimitives::Flags)(
				ModelPrimitives::Flags_Vertex_Position |
				ModelPrimitives::Flags_Vertex_Tex0 |
				ModelPrimitives::Flags_Vertex_Tex1 |
				ModelPrimitives::Flags_Vertex_Color |
				ModelPrimitives::Flags_Vertex_Normal |
				ModelPrimitives::Flags_Vertex_Tangent);

			prims->mTexPaths.Add(fbxMesh->mMaterial.mTexFileName);
			if (!fbxMesh->mMaterial.mBumpFileName.IsEmpty())
				prims->mTexPaths.Add(fbxMesh->mMaterial.mBumpFileName);

			prims->mIndices.Resize(fbxMesh->mIndexData.size());
			for (int ii = 0; ii < (int)fbxMesh->mIndexData.size(); ii++)
				prims->mIndices[ii] = (uint16)fbxMesh->mIndexData[ii];

			BF_ASSERT(fbxMesh->mVertexData.size() < 0x10000);
			prims->mVertices.Resize(fbxMesh->mVertexData.size());
			for (int vi = 0; vi < (int)fbxMesh->mVertexData.size(); vi++)
			{
				FBXVertexData* fv = &fbxMesh->mVertexData[vi];
				ModelVertex* mv = &prims->mVertices[vi];
				mv->mPosition = fv->mCoords;
				mv->mColor = fv->mColor;
				if (!fv->mTexCoords.empty())
					mv->mTexCoords = TexCoords::FlipV(fv->mTexCoords[0]);
				if (!fv->mBumpTexCoords.empty())
					mv->mBumpTexCoords = TexCoords::FlipV(fv->mBumpTexCoords[0]);
				mv->mNormal = fv->mNormal;
				mv->mTangent = fv->mTangent;
				mv->mNumBoneWeights = (int)fv->mBoneWeights.size();
				BF_ASSERT(mv->mNumBoneWeights <= MODEL_MAX_BONE_WEIGHTS);
				for (int bi = 0; bi < mv->mNumBoneWeights; bi++)
				{
					mv->mBoneIndices[bi] = fv->mBoneWeights[bi].mBoneIdx;
					mv->mBoneWeights[bi] = fv->mBoneWeights[bi].mBoneWeight;
				}
			}
		}

		for (int ji = 0; ji < (int)mFBXJoints.size(); ji++)
		{
			FBXJoint* fj = &mFBXJoints[ji];
			ModelJoint* joint = &mModelDef->mJoints[ji];

			joint->mName = fj->name;
			joint->mParentIdx = fj->parentIndex;

			auto invIt = jointInvBindMap.find(ji);
			if (invIt != jointInvBindMap.end())
				joint->mPoseInvMatrix = UfbxToMatrix4(invIt->second);
			else
				joint->mPoseInvMatrix = fj->mGlobalBindPoseInv;

			joint->mBindPoseLocal.mTrans = Vector3(
				(float)fj->posx, (float)fj->posy, (float)fj->posz);
			joint->mBindPoseLocal.mScale = Vector3(
				fj->scalex, fj->scaley, fj->scalez);
			joint->mBindPoseLocal.mQuat = Quaternion(
				(float)fj->quatx, (float)fj->quaty,
				(float)fj->quatz, (float)fj->quatw);
		}
	}

	// Animations
	mModelDef->mAnims.Resize(mAnimations.size());
	for (int ai = 0; ai < (int)mAnimations.size(); ai++)
	{
		FBXAnimation* fa = &mAnimations[ai];
		ModelAnimation* ma = &mModelDef->mAnims[ai];
		ma->mName = fa->mName;

		if (fa->mTracks.empty() || fa->mTracks[0].mSkeletonKeyframes.empty())
			continue;

		int numFrames = (int)fa->mTracks[0].mSkeletonKeyframes.size();
		int numJoints = (int)mFBXJoints.size();

		ma->mFrames.Resize(numFrames);
		for (int fi = 0; fi < numFrames; fi++)
			ma->mFrames[fi].mJointTranslations.Resize(numJoints);

		for (int ti = 0; ti < (int)fa->mTracks.size(); ti++)
		{
			FBXTrack* track = &fa->mTracks[ti];
			int frameCount = (int)track->mSkeletonKeyframes.size();
			for (int fi = 0; fi < BF_MIN(frameCount, numFrames); fi++)
			{
				FBXSkeletonKeyframe* key = &track->mSkeletonKeyframes[fi];
				ModelJointTranslation* jt = &ma->mFrames[fi].mJointTranslations[ti];
				jt->mQuat = Quaternion(
					(float)key->quat_x, (float)key->quat_y,
					(float)key->quat_z, (float)key->quat_w);
				jt->mScale = Vector3((float)key->sx, (float)key->sy, (float)key->sz);
				jt->mTrans = Vector3((float)key->tx, (float)key->ty, (float)key->tz);
			}
		}
	}

	ufbx_free_scene(scene);

	if (loadAnims)
		WriteBFFile(bfModelFileName, fileName, checkFileName2);

	return true;
}

bool Beefy::FBXReader::WriteBFFile(const StringImpl& fileName, const StringImpl& checkFile, const StringImpl& checkFile2)
{
	return false;

	/*FILE* fp = fopen(fileName.c_str(), "wb");
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

	return true;*/	
}

bool Beefy::FBXReader::ReadBFFile(const StringImpl& fileName)
{
	return false;

	/*FILE* fp = fopen(fileName.c_str(), "rb");
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

	return true;*/
}

////

BF_EXPORT ModelDef* BF_CALLTYPE Res_OpenFBX(const char* fileName, const char* baseDir, VertexDefinition* vertexDefinition)
{
	ModelDef* modelDef = new ModelDef();
	modelDef->mLoadDir = baseDir;
	FBXReader fbxReader(modelDef);
	if (!fbxReader.ReadFile(fileName))
	{
		delete modelDef;
		return NULL;
	}

	return modelDef;
}

#else

BF_EXPORT void* BF_CALLTYPE Res_OpenFBX(const char* fileName, const char* baseDir, void* vertexDefinition)
{
	return NULL;
}

#endif
