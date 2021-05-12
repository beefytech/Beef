#pragma once

#pragma once

#include "Common.h"
#include "util/Quaternion.h"
#include "util/Vector.h"
#include "util/Array.h"
#include "FileStream.h"
#include <vector>

NS_BF_BEGIN;

class ModelDef;
class ModelMaterialDef;

class GLTFReader
{
public:
	class StaticMaterial
	{
	public:
		ModelMaterialDef* mMaterialDef;
		String mMaterialSlotName;
	};

public:
	String mBasePathName;
	String mRootDir;
	ModelDef* mModelDef;
	Array<StaticMaterial> mStaticMaterials;

public:
	GLTFReader(ModelDef* modelDef);
	~GLTFReader();

	bool ParseMaterialDef(ModelMaterialDef* materialDef, const StringImpl& matText);
	ModelMaterialDef* LoadMaterial(const StringImpl& path);
	bool LoadModelProps(const StringImpl& relPath);
	bool ReadFile(const StringImpl& filePath, const StringImpl& rootDir);
};

NS_BF_END;