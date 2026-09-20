#pragma once

#include "ModelDef.h"
#include "util/Json.h"
#include "util/Dictionary.h"
#include <cmath>

NS_BF_BEGIN;

// GLB scenes, static or with one skin and its joint animations; anything unsupported (compression,
// several skins, sparse accessors) fails instead of producing corrupt meshes.
class GLBReader
{
	struct Accessor
	{
		const uint8* mData = NULL;
		int mCount = 0;
		int mStride = 0;
		int mType = 0;
		int mComponents = 0;
		bool mNormalized = false;

		double Read(int index, int component = 0) const
		{
			int size = mType == 5126 || mType == 5125 ? 4 : mType == 5123 ? 2 : 1;
			const uint8* ptr = mData + (size_t)index * mStride + component * size;
			if (mType == 5126) { float v; memcpy(&v, ptr, 4); return v; }
			uint32 v = 0;
			memcpy(&v, ptr, size);
			return mNormalized ? v / (size == 1 ? 255.0 : 65535.0) : v;
		}
	};

	ModelDef* mModel;
	Array<Accessor> mAccessors;
	Array<Json*> mMeshes, mNodes, mMaterials;
	Array<int> mActive;
	// Per glTF texture, its mTexPaths spelling: "*N" is embedded image N (ModelDef::mEmbeddedImages),
	// anything else a file beside the model. Empty when it has no usable image.
	Array<String> mTexturePaths;
	Array<int> mNodeParents; // -1 at a scene root
	Array<int> mJointOfNode; // -1 for a node that isn't a joint
	// A JOINTS_n value is a position in skin.joints; ModelDef joints are reordered parents-first.
	Array<int> mJointOfSkinSlot;

	struct Track
	{
		int mJoint;
		int mPath; // 0 translation, 1 rotation, 2 scale
		int mMode; // 0 linear, 1 step, 2 cubic spline
		const Accessor* mInput;
		const Accessor* mOutput;
	};

	static int Int(Json* obj, const char* name, int fallback = 0)
	{
		auto value = obj == NULL ? NULL : obj->GetObjectItem(name);
		return value == NULL ? fallback : value->mValueInt;
	}

	static void Items(Json* array, Array<Json*>& items)
	{
		if (array != NULL)
			for (auto item = array->mChild; item != NULL; item = item->mNext)
				items.Add(item);
	}

	static uint32 Color(double r, double g, double b, double a)
	{
		auto srgb = [](double v) { v = BF_MAX(0.0, BF_MIN(1.0, v)); return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055; };
		return ((uint32)(BF_MAX(0.0, BF_MIN(1.0, a)) * 255 + 0.5) << 24) |
			((uint32)(srgb(r) * 255 + 0.5) << 16) | ((uint32)(srgb(g) * 255 + 0.5) << 8) | (uint32)(srgb(b) * 255 + 0.5);
	}

	const Accessor* Attribute(Json* attrs, const char* name, int count, int components)
	{
		int index = Int(attrs, name, -1);
		if ((index < 0) || (index >= mAccessors.mSize)) return NULL;
		auto& a = mAccessors[index];
		return ((a.mCount == count) && (a.mComponents == components)) ? &a : NULL;
	}

	// A material's texture reference as a path. A map that needs a second UV set or a texture transform
	// is dropped rather than failing the model: the mesh is still right, only that map is missing.
	String TexturePath(Json* ref)
	{
		if ((ref == NULL) || (Int(ref, "texCoord") != 0) || (ref->GetObjectItem("extensions") != NULL)) return String();
		int index = Int(ref, "index", -1);
		return ((index >= 0) && (index < mTexturePaths.mSize)) ? mTexturePaths[index] : String();
	}

	static String DecodeUri(const char* uri)
	{
		String path;
		for (const char* c = uri; *c != 0; c++)
		{
			if ((c[0] == '%') && isxdigit((uint8)c[1]) && isxdigit((uint8)c[2]))
			{
				char hex[3] = { c[1], c[2], 0 };
				path.Append((char)strtol(hex, NULL, 16));
				c += 2;
			}
			else
				path.Append(*c);
		}
		return path;
	}

	static bool ReadTRS(Json* node, ModelJointTranslation& out)
	{
		float t[3] = {}, s[3] = { 1, 1, 1 }, q[4] = { 0, 0, 0, 1 };
		auto read = [&](const char* name, float* dst, int count) {
			auto value = node->GetObjectItem(name);
			if (value == NULL) return true;
			if (value->GetArraySize() != count) return false;
			for (int i = 0; i < count; i++) dst[i] = (float)value->GetArrayItem(i)->mValueDouble;
			return true;
		};
		if ((!read("translation", t, 3)) || (!read("scale", s, 3)) || (!read("rotation", q, 4))) return false;
		out.mTrans = Vector3(t[0], t[1], t[2]);
		out.mScale = Vector3(s[0], s[1], s[2]);
		out.mQuat = Quaternion(q[0], q[1], q[2], q[3]);
		return true;
	}

	static bool LocalMatrix(Json* node, Matrix4& out)
	{
		if (auto matrix = node->GetObjectItem("matrix"))
		{
			if (matrix->GetArraySize() != 16) return false;
			out = Matrix4::sIdentity;
			for (int i = 0; i < 16; i++) out.mMat[i % 4][i / 4] = (float)matrix->GetArrayItem(i)->mValueDouble;
			return true;
		}
		ModelJointTranslation trs;
		if (!ReadTRS(node, trs)) return false;
		out = Matrix4::CreateTransform(trs.mTrans, trs.mScale, trs.mQuat);
		return true;
	}

	// The skin as ModelDef joints, parents before children since poses compose in index order. Joint
	// transforms stay in glTF space and the axis change rides mArmatureToWorld, so skinned vertices
	// stay raw for the inverse bind matrices to act on.
	bool BuildSkeleton(Json* skin, const Matrix4& axes)
	{
		auto jointList = skin->GetObjectItem("joints");
		if (jointList == NULL) return false;
		Array<int> skinNodes;
		Array<bool> isJoint;
		isJoint.Resize(mNodes.mSize);
		for (auto& flag : isJoint) flag = false;
		for (auto item = jointList->mChild; item != NULL; item = item->mNext)
		{
			int node = item->mValueInt;
			if ((node < 0) || (node >= mNodes.mSize) || isJoint[node]) return false;
			isJoint[node] = true;
			skinNodes.Add(node);
		}
		if (skinNodes.IsEmpty() || (skinNodes.mSize > 256)) return false;

		mJointOfSkinSlot.Resize(skinNodes.mSize);
		Array<int> order;
		int armature = -2;
		while (order.mSize < skinNodes.mSize)
		{
			bool progress = false;
			for (int slot = 0; slot < skinNodes.mSize; slot++)
			{
				int node = skinNodes[slot];
				if (mJointOfNode[node] >= 0) continue;
				int parent = mNodeParents[node];
				if ((parent >= 0) && isJoint[parent])
				{
					if (mJointOfNode[parent] < 0) continue;
				}
				else
				{
					// A root joint: whatever sits above it must be joint-free and shared by every root,
					// for one mArmatureToWorld to describe it.
					for (int up = parent; up >= 0; up = mNodeParents[up])
						if (isJoint[up]) return false;
					if ((armature != -2) && (armature != parent)) return false;
					armature = parent;
				}
				mJointOfNode[node] = (int)order.mSize;
				mJointOfSkinSlot[slot] = (int)order.mSize;
				order.Add(slot);
				progress = true;
			}
			if (!progress) return false;
		}

		Matrix4 armatureWorld = Matrix4::sIdentity;
		for (int up = armature; up >= 0; up = mNodeParents[up])
		{
			Matrix4 local;
			if (!LocalMatrix(mNodes[up], local)) return false;
			armatureWorld = Matrix4::Multiply(local, armatureWorld);
		}
		mModel->mArmatureToWorld = Matrix4::Multiply(axes, armatureWorld);

		const Accessor* inverseBinds = NULL;
		int ibm = Int(skin, "inverseBindMatrices", -1);
		if (ibm >= 0)
		{
			if ((ibm >= mAccessors.mSize) || (mAccessors[ibm].mComponents != 16) || (mAccessors[ibm].mType != 5126) || (mAccessors[ibm].mCount < skinNodes.mSize)) return false;
			inverseBinds = &mAccessors[ibm];
		}
		mModel->mJoints.Resize(order.mSize);
		for (int jointIdx = 0; jointIdx < order.mSize; jointIdx++)
		{
			int slot = order[jointIdx];
			auto node = mNodes[skinNodes[slot]];
			// Animation targets TRS, which a matrix-authored joint doesn't have.
			if (node->GetObjectItem("matrix") != NULL) return false;
			auto& joint = mModel->mJoints[jointIdx];
			if (auto name = node->GetObjectItem("name"))
				if (name->mValueString != NULL) joint.mName = name->mValueString;
			if (joint.mName.IsEmpty()) joint.mName = StrFormat("Joint%d", jointIdx);
			int parent = mNodeParents[skinNodes[slot]];
			joint.mParentIdx = ((parent >= 0) && isJoint[parent]) ? mJointOfNode[parent] : -1;
			if (!ReadTRS(node, joint.mBindPoseLocal)) return false;
			joint.mPoseInvMatrix = Matrix4::sIdentity;
			if (inverseBinds != NULL)
				for (int i = 0; i < 16; i++) joint.mPoseInvMatrix.mMat[i % 4][i / 4] = (float)inverseBinds->Read(slot, i);
		}
		return true;
	}

	static void Sample(const Track& track, double time, double* out)
	{
		int components = (track.mPath == 1) ? 4 : 3;
		auto& input = *track.mInput;
		auto& output = *track.mOutput;
		int keys = input.mCount;
		// A cubic spline key is in-tangent, value, out-tangent.
		int stride = (track.mMode == 2) ? 3 : 1;
		int offset = (track.mMode == 2) ? 1 : 0;
		auto value = [&](int key, int c) { return output.Read(key * stride + offset, c); };
		if ((keys == 1) || (time <= input.Read(0)) || (time >= input.Read(keys - 1)))
		{
			int key = ((keys == 1) || (time <= input.Read(0))) ? 0 : keys - 1;
			for (int c = 0; c < components; c++) out[c] = value(key, c);
			return;
		}
		int lo = 0, hi = keys - 1;
		while (hi - lo > 1)
		{
			int mid = (lo + hi) / 2;
			if (input.Read(mid) <= time) lo = mid; else hi = mid;
		}
		double t0 = input.Read(lo), dt = input.Read(lo + 1) - t0;
		double u = (dt > 0) ? (time - t0) / dt : 0;
		if (track.mMode == 1)
		{
			for (int c = 0; c < components; c++) out[c] = value(lo, c);
			return;
		}
		if (track.mMode == 2)
		{
			double u2 = u * u, u3 = u2 * u;
			double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
			for (int c = 0; c < components; c++)
				out[c] = h00 * value(lo, c) + h10 * dt * output.Read(lo * 3 + 2, c) + h01 * value(lo + 1, c) + h11 * dt * output.Read((lo + 1) * 3, c);
			return;
		}
		if (track.mPath == 1)
		{
			auto q = Quaternion::Slerp((float)u, Quaternion((float)value(lo, 0), (float)value(lo, 1), (float)value(lo, 2), (float)value(lo, 3)),
				Quaternion((float)value(lo + 1, 0), (float)value(lo + 1, 1), (float)value(lo + 1, 2), (float)value(lo + 1, 3)), true);
			out[0] = q.mX; out[1] = q.mY; out[2] = q.mZ; out[3] = q.mW;
			return;
		}
		for (int c = 0; c < components; c++) out[c] = value(lo, c) + (value(lo + 1, c) - value(lo, c)) * u;
	}

	// Joint channels baked to frames at the def's frame rate, which is what playback samples. A joint a
	// clip doesn't animate holds its bind pose; channels on non-joint nodes (or morph weights) are dropped.
	bool BakeAnimations(Json* root)
	{
		Array<Json*> anims;
		Items(root->GetObjectItem("animations"), anims);
		for (auto anim : anims)
		{
			Array<Json*> channels, samplers;
			Items(anim->GetObjectItem("channels"), channels);
			Items(anim->GetObjectItem("samplers"), samplers);
			Array<Track> tracks;
			double start = 1e300, end = -1e300;
			for (auto channel : channels)
			{
				auto target = channel->GetObjectItem("target");
				int node = Int(target, "node", -1);
				if ((node < 0) || (node >= mNodes.mSize) || (mJointOfNode[node] < 0)) continue;
				auto pathItem = target->GetObjectItem("path");
				String path = ((pathItem != NULL) && (pathItem->mValueString != NULL)) ? pathItem->mValueString : "";
				int pathKind = (path == "translation") ? 0 : (path == "rotation") ? 1 : (path == "scale") ? 2 : -1;
				if (pathKind < 0) continue;
				int samplerIdx = Int(channel, "sampler", -1);
				if ((samplerIdx < 0) || (samplerIdx >= samplers.mSize)) return false;
				auto sampler = samplers[samplerIdx];
				int input = Int(sampler, "input", -1), output = Int(sampler, "output", -1);
				if ((input < 0) || (input >= mAccessors.mSize) || (output < 0) || (output >= mAccessors.mSize)) return false;
				auto interpItem = sampler->GetObjectItem("interpolation");
				String interp = ((interpItem != NULL) && (interpItem->mValueString != NULL)) ? interpItem->mValueString : "LINEAR";
				Track track = { mJointOfNode[node], pathKind, (interp == "STEP") ? 1 : (interp == "CUBICSPLINE") ? 2 : 0, &mAccessors[input], &mAccessors[output] };
				int keys = track.mInput->mCount;
				if ((track.mInput->mComponents != 1) || (track.mInput->mType != 5126) || (track.mOutput->mComponents != ((pathKind == 1) ? 4 : 3)) ||
					(track.mOutput->mCount != keys * ((track.mMode == 2) ? 3 : 1)))
					return false;
				start = BF_MIN(start, track.mInput->Read(0));
				end = BF_MAX(end, track.mInput->Read(keys - 1));
				tracks.Add(track);
			}
			if (tracks.IsEmpty())
				continue;
			int frameCount = (int)floor((end - start) * mModel->mFrameRate + 0.5) + 1;
			if ((frameCount < 1) || (frameCount > 1000000)) return false;

			mModel->mAnims.Add(ModelAnimation());
			auto& clip = mModel->mAnims.back();
			if (auto name = anim->GetObjectItem("name"))
				if (name->mValueString != NULL) clip.mName = name->mValueString;
			if (clip.mName.IsEmpty()) clip.mName = StrFormat("Animation%d", (int)mModel->mAnims.mSize - 1);
			clip.mFrames.Resize(frameCount);
			for (auto& frame : clip.mFrames)
			{
				frame.mJointTranslations.Resize(mModel->mJoints.mSize);
				for (int j = 0; j < mModel->mJoints.mSize; j++)
					frame.mJointTranslations[j] = mModel->mJoints[j].mBindPoseLocal;
			}
			for (auto& track : tracks)
			{
				for (int frameIdx = 0; frameIdx < frameCount; frameIdx++)
				{
					double v[4];
					Sample(track, start + frameIdx / (double)mModel->mFrameRate, v);
					auto& pose = clip.mFrames[frameIdx].mJointTranslations[track.mJoint];
					if (track.mPath == 0) pose.mTrans = Vector3((float)v[0], (float)v[1], (float)v[2]);
					else if (track.mPath == 1) pose.mQuat = Quaternion::Normalise(Quaternion((float)v[0], (float)v[1], (float)v[2], (float)v[3]));
					else pose.mScale = Vector3((float)v[0], (float)v[1], (float)v[2]);
				}
			}
		}
		return true;
	}

	bool Mesh(int meshIndex, const Matrix4& transform, bool skinned)
	{
		if ((meshIndex < 0) || (meshIndex >= mMeshes.mSize)) return false;
		auto source = mMeshes[meshIndex];
		auto primitives = source->GetObjectItem("primitives");
		if (primitives == NULL) return false;
		ModelMesh mesh;
		if (auto name = source->GetObjectItem("name"))
			if (name->mValueString != NULL) mesh.mName = name->mValueString;
		Vector3 x(transform.m00, transform.m10, transform.m20);
		Vector3 y(transform.m01, transform.m11, transform.m21);
		Vector3 z(transform.m02, transform.m12, transform.m22);
		Vector3 nx = Vector3::CrossProduct(y, z), ny = Vector3::CrossProduct(z, x), nz = Vector3::CrossProduct(x, y);
		float determinant = Vector3::Dot(x, nx);
		if (fabs(determinant) < 1e-12f) return false;
		for (auto primitive = primitives->mChild; primitive != NULL; primitive = primitive->mNext)
		{
			if ((Int(primitive, "mode", 4) != 4) || (primitive->GetObjectItem("extensions") != NULL)) return false;
			auto attrs = primitive->GetObjectItem("attributes");
			int posIndex = Int(attrs, "POSITION", -1);
			if ((posIndex < 0) || (posIndex >= mAccessors.mSize)) return false;
			auto& positions = mAccessors[posIndex];
			if ((positions.mComponents != 3) || (positions.mType != 5126)) return false;
			auto normals = Attribute(attrs, "NORMAL", positions.mCount, 3);
			auto uv = Attribute(attrs, "TEXCOORD_0", positions.mCount, 2);
			auto colors = Attribute(attrs, "COLOR_0", positions.mCount, 4);
			if (colors == NULL) colors = Attribute(attrs, "COLOR_0", positions.mCount, 3);
			auto tangents = Attribute(attrs, "TANGENT", positions.mCount, 4);
			const Accessor* skinJoints[2] = { Attribute(attrs, "JOINTS_0", positions.mCount, 4), Attribute(attrs, "JOINTS_1", positions.mCount, 4) };
			const Accessor* skinWeights[2] = { Attribute(attrs, "WEIGHTS_0", positions.mCount, 4), Attribute(attrs, "WEIGHTS_1", positions.mCount, 4) };
			const Accessor* indices = NULL;
			int index = Int(primitive, "indices", -1);
			if (index != -1)
			{
				if ((index < 0) || (index >= mAccessors.mSize)) return false;
				indices = &mAccessors[index];
				if ((indices->mComponents != 1) || (indices->mType == 5126) || (indices->mNormalized)) return false;
			}
			int count = indices == NULL ? positions.mCount : indices->mCount;
			if ((count % 3) != 0) return false;
			double tint[4] = { 1, 1, 1, 1 };
			float roughness = 1.0f, metallic = 1.0f;
			Vector3 emissive(0, 0, 0);
			String materialName;
			String albedoPath, normalPath, ormPath, emissionPath;
			bool ormHasOcclusion = false;
			int material = Int(primitive, "material", -1);
			if (material >= 0)
			{
				if (material >= mMaterials.mSize) return false;
				if (auto name = mMaterials[material]->GetObjectItem("name"))
					if (name->mValueString != NULL) materialName = name->mValueString;
				if (auto pbr = mMaterials[material]->GetObjectItem("pbrMetallicRoughness"))
				{
					if (auto value = pbr->GetObjectItem("roughnessFactor")) roughness = (float)value->mValueDouble;
					if (auto value = pbr->GetObjectItem("metallicFactor")) metallic = (float)value->mValueDouble;
					albedoPath = TexturePath(pbr->GetObjectItem("baseColorTexture"));
					ormPath = TexturePath(pbr->GetObjectItem("metallicRoughnessTexture"));
					if (auto factor = pbr->GetObjectItem("baseColorFactor"))
					{
						if (factor->GetArraySize() != 4) return false;
						for (int i = 0; i < 4; i++) tint[i] = factor->GetArrayItem(i)->mValueDouble;
					}
				}
			}
			if (material >= 0)
			{
				normalPath = TexturePath(mMaterials[material]->GetObjectItem("normalTexture"));
				emissionPath = TexturePath(mMaterials[material]->GetObjectItem("emissiveTexture"));
				// Occlusion rides the ORM's red channel only when it samples that same image.
				ormHasOcclusion = (!ormPath.IsEmpty()) && (TexturePath(mMaterials[material]->GetObjectItem("occlusionTexture")) == ormPath);
				if (auto value = mMaterials[material]->GetObjectItem("emissiveFactor"))
				{
					if (value->GetArraySize() != 3) return false;
					emissive = Vector3((float)value->GetArrayItem(0)->mValueDouble, (float)value->GetArrayItem(1)->mValueDouble, (float)value->GetArrayItem(2)->mValueDouble);
				}
				if (auto extensions = mMaterials[material]->GetObjectItem("extensions"))
					if (auto extension = extensions->GetObjectItem("KHR_materials_emissive_strength"))
						if (auto strength = extension->GetObjectItem("emissiveStrength")) emissive = emissive * (float)strength->mValueDouble;
			}
			ModelPrimitives* prims = NULL;
			Dictionary<int, uint16> remap;
			int firstPrim = (int)mesh.mPrimitives.mSize;
			for (int triangle = 0; triangle < count; triangle += 3)
			{
				if ((prims == NULL) || (prims->mVertices.mSize > 65532))
				{
					mesh.mPrimitives.Add(ModelPrimitives());
					prims = &mesh.mPrimitives.back();
					prims->mFlags = (ModelPrimitives::Flags)(1 | 2 | 4 | 0x10 | 0x20 | 0x40);
					prims->mMaterialName = materialName;
					prims->mHasSurfaceMaterial = true;
					auto sidedness = material >= 0 ? mMaterials[material]->GetObjectItem("doubleSided") : NULL;
					prims->mTwoSided = (sidedness != NULL) && (sidedness->mType == Json::Type_True);
					prims->mRoughness = roughness;
					prims->mMetallic = metallic;
					prims->mEmissive = emissive;
					prims->mTexPaths.Add(albedoPath);
					prims->mTexRoles.Add("albedo");
					auto addMap = [&](const String& path, const char* role) {
						if (path.IsEmpty()) return;
						prims->mTexPaths.Add(path);
						prims->mTexRoles.Add(role);
					};
					addMap(normalPath, "normal");
					addMap(emissionPath, "emission");
					// "metallicRoughness": the same green/blue channels, but red is not occlusion.
					addMap(ormPath, ormHasOcclusion ? "orm" : "metallicRoughness");
					remap.Clear();
				}
				for (int corner = 0; corner < 3; corner++)
				{
					int c = determinant < 0 ? 2 - corner : corner;
					double raw = indices == NULL ? triangle + c : indices->Read(triangle + c);
					if ((raw < 0) || (raw >= positions.mCount)) return false;
					int vertexIndex = (int)raw;
					auto found = remap.Find(vertexIndex);
					if (found != remap.end()) { prims->mIndices.Add(found->mValue); continue; }
					ModelVertex v = {};
					Vector3 p((float)positions.Read(vertexIndex, 0), (float)positions.Read(vertexIndex, 1), (float)positions.Read(vertexIndex, 2));
					v.mPosition = x * p.mX + y * p.mY + z * p.mZ + Vector3(transform.m03, transform.m13, transform.m23);
					if (normals != NULL)
						v.mNormal = Vector3::Normalize((nx * (float)normals->Read(vertexIndex, 0) + ny * (float)normals->Read(vertexIndex, 1) + nz * (float)normals->Read(vertexIndex, 2)) * (1.0f / determinant));
					if (uv != NULL) v.mTexCoords = TexCoords((float)uv->Read(vertexIndex, 0), (float)uv->Read(vertexIndex, 1));
					v.mBumpTexCoords = v.mTexCoords;
					if (tangents != NULL)
						v.mTangent = Vector3::Normalize(x * (float)tangents->Read(vertexIndex, 0) + y * (float)tangents->Read(vertexIndex, 1) + z * (float)tangents->Read(vertexIndex, 2));
					double color[4] = { tint[0], tint[1], tint[2], tint[3] };
					if (colors != NULL)
						for (int i = 0; i < colors->mComponents; i++) color[i] *= colors->Read(vertexIndex, i);
					v.mColor = Color(color[0], color[1], color[2], color[3]);
					if (skinned)
					{
						for (int set = 0; set < 2; set++)
						{
							if ((skinJoints[set] == NULL) || (skinWeights[set] == NULL)) continue;
							for (int c = 0; c < 4; c++)
							{
								double weight = skinWeights[set]->Read(vertexIndex, c);
								int slot = (int)skinJoints[set]->Read(vertexIndex, c);
								if ((weight <= 0) || (slot < 0) || (slot >= mJointOfSkinSlot.mSize) || (v.mNumBoneWeights >= MODEL_MAX_BONE_WEIGHTS)) continue;
								v.mBoneIndices[v.mNumBoneWeights] = mJointOfSkinSlot[slot];
								v.mBoneWeights[v.mNumBoneWeights++] = (float)weight;
							}
						}
					}
					uint16 mapped = (uint16)prims->mVertices.mSize;
					remap[vertexIndex] = mapped;
					prims->mVertices.Add(v);
					prims->mIndices.Add(mapped);
				}
				if (normals == NULL)
				{
					int end = prims->mIndices.mSize;
					auto& a = prims->mVertices[prims->mIndices[end - 3]];
					auto& b = prims->mVertices[prims->mIndices[end - 2]];
					auto& c = prims->mVertices[prims->mIndices[end - 1]];
					auto normal = Vector3::CrossProduct(b.mPosition - a.mPosition, c.mPosition - a.mPosition);
					a.mNormal += normal; b.mNormal += normal; c.mNormal += normal;
				}
			}
			if ((tangents == NULL) && (!normalPath.IsEmpty()) && (uv != NULL))
				for (int p = firstPrim; p < (int)mesh.mPrimitives.mSize; p++)
					mesh.mPrimitives[p].GenerateTangents();
		}
		mModel->mMeshes.Add(mesh);
		return true;
	}

	bool Node(int index, const Matrix4& parent, int depth)
	{
		if ((index < 0) || (index >= mNodes.mSize) || (depth > 256) || (mActive[index] != 0)) return false;
		mActive[index] = 1;
		auto node = mNodes[index];
		Matrix4 local;
		if (!LocalMatrix(node, local)) return false;
		auto world = Matrix4::Multiply(parent, local);
		if (auto mesh = node->GetObjectItem("mesh"))
		{
			// A skinned mesh's own node transform is ignored -- the joints place it -- and its vertices
			// stay raw for the inverse bind matrices.
			bool skinned = node->GetObjectItem("skin") != NULL;
			if (!Mesh(mesh->mValueInt, skinned ? Matrix4::sIdentity : world, skinned)) return false;
		}
		if (auto children = node->GetObjectItem("children"))
			for (auto child = children->mChild; child != NULL; child = child->mNext)
				if (!Node(child->mValueInt, world, depth + 1)) return false;
		mActive[index] = 0;
		return true;
	}

public:
	GLBReader(ModelDef* model) : mModel(model) { mModel->mFrameRate = 30; }

	bool Read(const StringImpl& path)
	{
		int length = 0;
		uint8* data = LoadBinaryData(path, &length);
		if (data == NULL) return false;
		defer({ delete [] data; });
		auto word = [&](int offset) { uint32 v; memcpy(&v, data + offset, 4); return v; };
		if ((length < 20) || (word(0) != 0x46546C67) || (word(4) != 2) || (word(8) != (uint32)length)) return false;
		String json;
		const uint8* binary = NULL;
		int binaryLength = 0;
		for (int offset = 12; offset < length;)
		{
			if (length - offset < 8) return false;
			uint32 size = word(offset), kind = word(offset + 4);
			offset += 8;
			if ((size > (uint32)(length - offset)) || ((size % 4) != 0)) return false;
			if (kind == 0x4E4F534A) { if (!json.IsEmpty()) return false; json.Append((char*)data + offset, size); }
			if (kind == 0x004E4942) { if (binary != NULL) return false; binary = data + offset; binaryLength = size; }
			offset += size;
		}
		auto root = Json::Parse(json.c_str());
		if (root == NULL) return false;
		defer({ delete root; });
		if ((binary == NULL) || (root->GetObjectItem("extensionsRequired") != NULL)) return false;
		auto buffers = root->GetObjectItem("buffers");
		if ((buffers == NULL) || (buffers->GetArraySize() != 1)) return false;
		if ((Int(buffers->mChild, "byteLength", -1) < 0) || (Int(buffers->mChild, "byteLength") > binaryLength) || (buffers->mChild->GetObjectItem("uri") != NULL)) return false;
		Array<Json*> views, accessors;
		Items(root->GetObjectItem("bufferViews"), views);
		Items(root->GetObjectItem("accessors"), accessors);
		for (auto source : accessors)
		{
			Accessor a;
			int view = Int(source, "bufferView", -1);
			if ((view < 0) || (view >= views.mSize) || (source->GetObjectItem("sparse") != NULL)) return false;
			auto v = views[view];
			if (Int(v, "buffer") != 0) return false;
			a.mCount = Int(source, "count"); a.mType = Int(source, "componentType");
			auto type = source->GetObjectItem("type");
			if ((type == NULL) || (type->mValueString == NULL)) return false;
			String typeName = type->mValueString;
			a.mComponents = typeName == "SCALAR" ? 1 : typeName == "VEC2" ? 2 : typeName == "VEC3" ? 3 : typeName == "VEC4" ? 4 : typeName == "MAT4" ? 16 : 0;
			int size = (a.mType == 5126) || (a.mType == 5125) ? 4 : a.mType == 5123 ? 2 : a.mType == 5121 ? 1 : 0;
			if ((size == 0) || (a.mComponents == 0) || (a.mCount <= 0)) return false;
			a.mStride = Int(v, "byteStride", size * a.mComponents);
			auto normalized = source->GetObjectItem("normalized");
			a.mNormalized = (normalized != NULL) && (normalized->mType == Json::Type_True);
			int start = Int(v, "byteOffset"), span = Int(v, "byteLength"), offset = Int(source, "byteOffset");
			if ((start < 0) || (span < 0) || (offset < 0) || ((int64)start + span > binaryLength) || (a.mStride < size * a.mComponents) ||
				((int64)offset + (int64)(a.mCount - 1) * a.mStride + size * a.mComponents > span)) return false;
			a.mData = binary + start + offset;
			mAccessors.Add(a);
		}
		// Embedded images are copied onto the def now; the file buffer does not outlive this call.
		Array<Json*> images, textures;
		Items(root->GetObjectItem("images"), images);
		Items(root->GetObjectItem("textures"), textures);
		Array<String> imagePaths;
		for (auto image : images)
		{
			String imagePath;
			int view = Int(image, "bufferView", -1);
			auto uri = image->GetObjectItem("uri");
			if ((view >= 0) && (view < views.mSize) && (Int(views[view], "buffer") == 0))
			{
				int start = Int(views[view], "byteOffset"), span = Int(views[view], "byteLength");
				if ((start >= 0) && (span > 0) && ((int64)start + span <= binaryLength))
				{
					imagePath = StrFormat("*%d", (int)mModel->mEmbeddedImages.mSize);
					mModel->mEmbeddedImages.Add(Array<uint8>());
					mModel->mEmbeddedImages.back().Insert(0, binary + start, span);
				}
			}
			else if ((uri != NULL) && (uri->mValueString != NULL) && (strncmp(uri->mValueString, "data:", 5) != 0))
				imagePath = DecodeUri(uri->mValueString);
			imagePaths.Add(imagePath);
		}
		for (auto texture : textures)
		{
			int source = Int(texture, "source", -1);
			mTexturePaths.Add(((source >= 0) && (source < imagePaths.mSize)) ? imagePaths[source] : String());
		}
		Items(root->GetObjectItem("meshes"), mMeshes);
		Items(root->GetObjectItem("nodes"), mNodes);
		Items(root->GetObjectItem("materials"), mMaterials);
		mActive.Resize(mNodes.mSize);
		for (auto& active : mActive) active = 0;
		auto scenes = root->GetObjectItem("scenes");
		auto scene = scenes == NULL ? NULL : scenes->GetArrayItem(Int(root, "scene"));
		auto nodes = scene == NULL ? NULL : scene->GetObjectItem("nodes");
		if (nodes == NULL) return false;
		Matrix4 axes = Matrix4::sIdentity;
		axes.m00 = -1; axes.m22 = -1;

		mNodeParents.Resize(mNodes.mSize);
		mJointOfNode.Resize(mNodes.mSize);
		for (int i = 0; i < mNodes.mSize; i++)
		{
			mNodeParents[i] = -1;
			mJointOfNode[i] = -1;
		}
		for (int i = 0; i < mNodes.mSize; i++)
			if (auto children = mNodes[i]->GetObjectItem("children"))
				for (auto child = children->mChild; child != NULL; child = child->mNext)
				{
					int childIdx = child->mValueInt;
					if ((childIdx < 0) || (childIdx >= mNodes.mSize) || (mNodeParents[childIdx] != -1)) return false;
					mNodeParents[childIdx] = i;
				}
		// One skeleton per model: every skinned mesh has to share the skin.
		int skinIdx = -1;
		for (auto node : mNodes)
			if ((node->GetObjectItem("mesh") != NULL) && (node->GetObjectItem("skin") != NULL))
			{
				int used = Int(node, "skin", -1);
				if ((used < 0) || ((skinIdx >= 0) && (used != skinIdx))) return false;
				skinIdx = used;
			}
		if (skinIdx >= 0)
		{
			Array<Json*> skins;
			Items(root->GetObjectItem("skins"), skins);
			if ((skinIdx >= skins.mSize) || (!BuildSkeleton(skins[skinIdx], axes)) || (!BakeAnimations(root))) return false;
		}

		for (auto node = nodes->mChild; node != NULL; node = node->mNext)
			if (!Node(node->mValueInt, axes, 0)) return false;
		return !mModel->mMeshes.IsEmpty();
	}
};

NS_BF_END;
