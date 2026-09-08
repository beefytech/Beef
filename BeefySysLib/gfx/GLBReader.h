#pragma once

#include "ModelDef.h"
#include "util/Json.h"
#include "util/Dictionary.h"
#include <cmath>

NS_BF_BEGIN;

// Static GLB scenes; unsupported skinning/compression fails instead of producing corrupt meshes.
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

	bool Mesh(int meshIndex, const Matrix4& transform)
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
			String materialName;
			int material = Int(primitive, "material", -1);
			if (material >= 0)
			{
				if (material >= mMaterials.mSize) return false;
				if (auto name = mMaterials[material]->GetObjectItem("name"))
					if (name->mValueString != NULL) materialName = name->mValueString;
				if (auto pbr = mMaterials[material]->GetObjectItem("pbrMetallicRoughness"))
				{
					if (pbr->GetObjectItem("baseColorTexture") != NULL) return false;
					if (auto factor = pbr->GetObjectItem("baseColorFactor"))
					{
						if (factor->GetArraySize() != 4) return false;
						for (int i = 0; i < 4; i++) tint[i] = factor->GetArrayItem(i)->mValueDouble;
					}
				}
			}
			ModelPrimitives* prims = NULL;
			Dictionary<int, uint16> remap;
			for (int triangle = 0; triangle < count; triangle += 3)
			{
				if ((prims == NULL) || (prims->mVertices.mSize > 65532))
				{
					mesh.mPrimitives.Add(ModelPrimitives());
					prims = &mesh.mPrimitives.back();
					prims->mFlags = (ModelPrimitives::Flags)(1 | 2 | 4 | 0x10 | 0x20 | 0x40);
					prims->mMaterialName = materialName;
					prims->mTexPaths.Add(String());
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
					double color[4] = { tint[0], tint[1], tint[2], tint[3] };
					if (colors != NULL)
						for (int i = 0; i < colors->mComponents; i++) color[i] *= colors->Read(vertexIndex, i);
					v.mColor = Color(color[0], color[1], color[2], color[3]);
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
		}
		mModel->mMeshes.Add(mesh);
		return true;
	}

	bool Node(int index, const Matrix4& parent, int depth)
	{
		if ((index < 0) || (index >= mNodes.mSize) || (depth > 256) || (mActive[index] != 0)) return false;
		mActive[index] = 1;
		auto node = mNodes[index];
		if (node->GetObjectItem("skin") != NULL) return false;
		Matrix4 local = Matrix4::sIdentity;
		if (auto matrix = node->GetObjectItem("matrix"))
		{
			if (matrix->GetArraySize() != 16) return false;
			for (int i = 0; i < 16; i++) local.mMat[i % 4][i / 4] = (float)matrix->GetArrayItem(i)->mValueDouble;
		}
		else
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
			local = Matrix4::CreateTransform(Vector3(t[0], t[1], t[2]), Vector3(s[0], s[1], s[2]), Quaternion(q[0], q[1], q[2], q[3]));
		}
		auto world = Matrix4::Multiply(parent, local);
		if (auto mesh = node->GetObjectItem("mesh"))
			if (!Mesh(mesh->mValueInt, world)) return false;
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
			a.mComponents = typeName == "SCALAR" ? 1 : typeName == "VEC2" ? 2 : typeName == "VEC3" ? 3 : typeName == "VEC4" ? 4 : 0;
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
		for (auto node = nodes->mChild; node != NULL; node = node->mNext)
			if (!Node(node->mValueInt, axes, 0)) return false;
		return !mModel->mMeshes.IsEmpty();
	}
};

NS_BF_END;
