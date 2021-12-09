#include "glTF.h"
#include "gfx/ModelInstance.h"
#include "util/Json.h"
#include "gfx/RenderDevice.h"
#include "BFApp.h"

USING_NS_BF;

static bool IsWhitespace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r');
}

class GLTFPropsParser
{
public:
	enum NodeKind
	{
		NodeKind_None,
		NodeKind_End,
		NodeKind_LBrace,
		NodeKind_RBrace,
		NodeKind_LBracket,
		NodeKind_RBracket,
		NodeKind_Equals,
		NodeKind_Index,
		NodeKind_Integer,
		NodeKind_Float,
		NodeKind_String
	};

	struct Node
	{
	public:		
		NodeKind mKind;
		int mStart;
		int mEnd;
		union
		{
			int mValueInt;
			float mValueFloat;
		};

	public:
		Node()
		{
			mKind = NodeKind_None;
			mStart = -1;
			mEnd = -1;
			mValueInt = 0;
		}
	};

public:
	char* mStart;
	char* mPtr;
	char* mEnd;
	Node mNext;

public:		
	GLTFPropsParser(const StringImpl& str)
	{
		mStart = str.mPtr;
		mPtr = mStart;
		mEnd = mStart + str.mLength;
	}

// 	double ParseLiteralDouble()
// 	{
// 		char buf[256];
// 		int len = BF_MAX(mTokenEnd - mTokenStart, 255);
// 
// 		memcpy(buf, &mSrc[mTokenStart], len);
// 		char c = buf[len - 1];
// 		if ((c == 'd') || (c == 'D') || (c == 'f') || (c == 'F'))
// 			buf[len - 1] = '\0';
// 		else
// 			buf[len] = '\0';
// 
// 		return strtod(buf, NULL);
// 	}

	Node PeekNext()
	{
		if (mNext.mKind != NodeKind_None)
			return mNext;

		while (true)
		{
			if (mPtr >= mEnd)
			{
				mNext.mKind = NodeKind_End;
				return mNext;
			}
				
			char* start = mPtr;
			char c = *(mPtr++);
			
			if (c == '{')
				mNext.mKind = NodeKind_LBrace;
			else if (c == '}')
				mNext.mKind = NodeKind_RBrace;
			else if (c == '[')
				mNext.mKind = NodeKind_LBracket;
			else if (c == ']')
				mNext.mKind = NodeKind_RBracket;
			else if (c == '=')
				mNext.mKind = NodeKind_Equals;

			if (mNext.mKind != NodeKind_None)
			{
				mNext.mStart = (int)(mPtr - mStart - 1);
				mNext.mEnd = (int)(mPtr - mStart);				
				return mNext;
			}						

			if ((c >= '0') && (c <= '9'))
			{
				bool hadDot = false;
				while (mPtr < mEnd)
				{
					char c = *mPtr;
					if (c == '.')
					{
						mPtr++;
						hadDot = true;
					}
					else if ((c >= '0') && (c <= '9'))						
					{						
						mPtr++;
					}
					else
						break;
				}
				
				mNext.mStart = (int)(start - mStart);
				mNext.mEnd = (int)(mPtr - mStart);
				
				char buf[256];
				int len = BF_MIN((int)(mPtr - start), 255);

				memcpy(buf, start, len);
				char c = buf[len - 1];
				if ((c == 'd') || (c == 'D') || (c == 'f') || (c == 'F'))
					buf[len - 1] = '\0';
				else
					buf[len] = '\0';

				if (hadDot)
				{
					mNext.mKind = NodeKind_Float;
					mNext.mValueFloat = (float)strtod(buf, NULL);
				}
				else
				{
					mNext.mKind = NodeKind_Integer;
					mNext.mValueInt = atoi(buf);
				}
				return mNext;
			}

			if (!IsWhitespace(c))
			{
				char* lastCPtr = start;
				while (mPtr < mEnd)
				{
					char c = *mPtr;
					if ((c == '}') || (c == '=') || (c == '[') || (c == '\r') || (c == '\n'))
						break;		
					if (c != ' ')
						lastCPtr = mPtr;
					mPtr++;
				}
				mPtr = lastCPtr + 1;

				mNext.mStart = (int)(start - mStart);
				mNext.mEnd = (int)(mPtr - mStart);
				mNext.mKind = NodeKind_String;
				return mNext;
			}
		}
	}

	Node GetNext()
	{
		auto node = PeekNext();
		mNext = Node();
		return node;
	}

	StringView GetStringView(const Node& node)
	{
		return StringView(mStart + node.mStart, node.mEnd - node.mStart);
	}

	StringView GetStringView(const Node& node, StringView& prefix)
	{
		auto stringView = StringView(mStart + node.mStart, node.mEnd - node.mStart);
		if (!stringView.EndsWith('\''))
		{
			prefix = "";
			return stringView;
		}
		
		int strStartIdx = (int)stringView.IndexOf('\'');
		prefix = StringView(stringView, 0, strStartIdx);
		return StringView(stringView, strStartIdx + 1, (int)stringView.mLength - strStartIdx - 2);
	}

	bool GetNextStringView(StringView& prefix, StringView& value)
	{
		auto node = GetNext();
		if (node.mKind != NodeKind_String)
			return false;
		auto stringView = StringView(mStart + node.mStart, node.mEnd - node.mStart);
		if (!stringView.EndsWith('\''))
		{
			prefix = "";
			value = stringView;
			return true;
		}
		int strStartIdx = (int)stringView.IndexOf('\'');
		prefix = StringView(stringView, 0, strStartIdx);
		value = StringView(stringView, strStartIdx + 1, (int)stringView.mLength - strStartIdx - 2);
		return true;
	}
};

enum ComponentType
{
	Int8 = 5120,
	UInt8 = 5121,
	Int16 = 5122,
	UInt16 = 5123,
	UInt32 = 5125,
	Float = 5126,
};

BF_EXPORT void* BF_CALLTYPE Res_OpenGLTF(const char* fileName, const char* baseDir, void* vertexDefinition)
{
	ModelDef* modelDef = new ModelDef();
	GLTFReader reader(modelDef);
	if (!reader.ReadFile(fileName, baseDir))
	{
		delete modelDef;
		return NULL;
	}

	return modelDef;
}

GLTFReader::GLTFReader(ModelDef* modelDef)
{
	mModelDef = modelDef;
}

GLTFReader::~GLTFReader()
{
	
}

struct DataSpan
{
	uint8* mPtr;
	int mSize;
};

struct DataAccessor
{
	uint8* mPtr;
	int mSize;
	int mCount;
	ComponentType mComponentType;
};

template <typename T>
static void ReadBuffer(DataAccessor& dataAccessor, T* outPtr, int outStride)
{	
	for (int i = 0; i < dataAccessor.mCount; i++)
		*(T*)((uint8*)outPtr + i * outStride) = ((T*)dataAccessor.mPtr)[i];
}

template <typename T>
static void ReadBuffer(DataAccessor& dataAccessor, T* outPtr, int outStride, int inStride)
{
	for (int i = 0; i < dataAccessor.mCount; i++)
		*(T*)((uint8*)outPtr + i * outStride) = *(T*)(dataAccessor.mPtr + i * inStride);
}

static void TrySkipValue(GLTFPropsParser& propsParser)
{
	auto nextNode = propsParser.PeekNext();

	if (nextNode.mKind == GLTFPropsParser::NodeKind_LBracket)
	{
		propsParser.GetNext();
		propsParser.GetNext();
		propsParser.GetNext();
		nextNode = propsParser.PeekNext();
	}

	if (nextNode.mKind == GLTFPropsParser::NodeKind_Equals)
	{
		propsParser.GetNext();

		int depth = 0;
		do
		{
			auto node = propsParser.GetNext();
			if (node.mKind == GLTFPropsParser::NodeKind_End)
				return;
			if (node.mKind == GLTFPropsParser::NodeKind_LBrace)
				depth++;
			else if (node.mKind == GLTFPropsParser::NodeKind_RBrace)
				depth--;
			if (node.mKind == GLTFPropsParser::NodeKind_LBracket)
				depth++;
			if (node.mKind == GLTFPropsParser::NodeKind_LBracket)
				depth--;
		} while (depth > 0);
	}
}

static bool ExpectIndex(GLTFPropsParser& propsParser, int& idx)
{
	auto node = propsParser.GetNext();
	if (node.mKind != GLTFPropsParser::NodeKind_LBracket)
		return false;
	node = propsParser.GetNext();
	if (node.mKind != GLTFPropsParser::NodeKind_Integer)
		return false;
	idx = node.mValueInt;
	node = propsParser.GetNext();
	if (node.mKind != GLTFPropsParser::NodeKind_RBracket)
		return false;
	return true;
};

static bool  ExpectOpen(GLTFPropsParser& propsParser)
{
	if (propsParser.GetNext().mKind != GLTFPropsParser::NodeKind_LBrace)
		return false;
	return true;
};

static bool ExpectClose(GLTFPropsParser& propsParser)
{
	if (propsParser.GetNext().mKind != GLTFPropsParser::NodeKind_RBrace)
		return false;
	return true;
};

static bool ExpectEquals(GLTFPropsParser& propsParser)
{
	if (propsParser.GetNext().mKind != GLTFPropsParser::NodeKind_Equals)
		return false;
	return true;
};

bool GLTFReader::ParseMaterialDef(ModelMaterialDef* materialDef, const StringImpl& matText)
{
	GLTFPropsParser propsParser(matText);

	while (true)
	{
		auto node = propsParser.GetNext();
		if (node.mKind == GLTFPropsParser::NodeKind_End)
			break;

		if (node.mKind == GLTFPropsParser::NodeKind_String)
		{
			auto key = propsParser.GetStringView(node);
			if (key == "Parent")
			{
				if (propsParser.GetNext().mKind != GLTFPropsParser::NodeKind_Equals)
					return false;

				auto valueNode = propsParser.GetNext();
				if (valueNode.mKind != GLTFPropsParser::NodeKind_String)
					return false;

				StringView prefix;
				StringView str = propsParser.GetStringView(valueNode, prefix);				
				auto parentMaterialDef = LoadMaterial(str);
			}
			else if (key == "TextureParameterValues")
			{
				int count = 0;
				if (!ExpectIndex(propsParser, count))
					return false;
				if (!ExpectEquals(propsParser))
					return false;
				if (!ExpectOpen(propsParser))
					return false;

				while (true)
				{
					node = propsParser.GetNext();
					if (node.mKind == GLTFPropsParser::NodeKind_RBrace)
						break;
					if (node.mKind != GLTFPropsParser::NodeKind_String)
						return false;

					StringView prefix;
					StringView str = propsParser.GetStringView(node, prefix);
					if (str == "TextureParameterValues")
					{
						auto textureParamValue = materialDef->mTextureParameterValues.Alloc();

						int idx = 0;
						if (!ExpectIndex(propsParser, idx))
							return false;
						if (!ExpectEquals(propsParser))
							return false;
						if (!ExpectOpen(propsParser))
							return false;

						while (true)
						{
							node = propsParser.GetNext();
							if (node.mKind == GLTFPropsParser::NodeKind_RBrace)
								break;
							if (node.mKind != GLTFPropsParser::NodeKind_String)
								return false;
							
							str = propsParser.GetStringView(node, prefix);
							if (str == "ParameterInfo")
							{
								if (!ExpectEquals(propsParser))
									return false;
								if (!ExpectOpen(propsParser))
									return false;
								if (!propsParser.GetNextStringView(prefix, str))
									return false;
								if (!ExpectEquals(propsParser))
									return false;
								if (!propsParser.GetNextStringView(prefix, str))
									return false;
								textureParamValue->mName = str;

								if (!ExpectClose(propsParser))
									return false;
							}
							else if (str == "ParameterValue")
							{
								if (!ExpectEquals(propsParser))
									return false;
								if (!propsParser.GetNextStringView(prefix, str))
									return false;

								String path = mRootDir;
								path += str;
								int dotPos = (int)path.IndexOf('.');
								if (dotPos != -1)
									path.RemoveToEnd(dotPos);
								path += ".tga";

								textureParamValue->mTexturePath = path;

// 								Texture* texture = gBFApp->mRenderDevice->LoadTexture(path, 0);
// 								textureParamValue->mTexture = texture;
							}
							else
								TrySkipValue(propsParser);
						}
					}
					else
					{
						TrySkipValue(propsParser);
					}
				}
			}
			else
			{
				TrySkipValue(propsParser);
			}
		}
	}

	return true;
}

ModelMaterialDef* GLTFReader::LoadMaterial(const StringImpl& relPath)
{
	String propsPath;
	if (relPath.StartsWith('/'))
	{
		propsPath = mRootDir + relPath;
		int dotPos = (int)propsPath.LastIndexOf('.');
		if (dotPos > 0)
			propsPath.RemoveToEnd(dotPos);
		propsPath += ".props.txt";
	}
	else if (mBasePathName.Contains("staticmesh"))
		propsPath = GetFileDir(mBasePathName) + "/" + relPath + ".props.txt";
	else
		propsPath = GetFileDir(mBasePathName) + "/materials/" + relPath + ".props.txt";

	ModelMaterialDef* materialDef = ModelMaterialDef::CreateOrGet("GLTF", propsPath);

	if (materialDef->mInitialized)
		return materialDef;
	
	materialDef->mInitialized = true;

	String propText;
	if (LoadTextData(propsPath, propText))
	{
		if (!ParseMaterialDef(materialDef, propText))
		{
			// Had error
		}

	}		
	return materialDef;
}

bool GLTFReader::LoadModelProps(const StringImpl& propsPath)
{
	String propText;
	if (!LoadTextData(propsPath, propText))
		return false;

	GLTFPropsParser propsParser(propText);
	while (true)
	{
		auto node = propsParser.GetNext();
		if (node.mKind == GLTFPropsParser::NodeKind_End)
			break;

		if (node.mKind == GLTFPropsParser::NodeKind_String)
		{
			auto key = propsParser.GetStringView(node);
			if (key == "StaticMaterials")
			{
				int count = 0;
				if (!ExpectIndex(propsParser, count))
					return false;
				if (!ExpectEquals(propsParser))
					return false;
				if (!ExpectOpen(propsParser))
					return false;

				while (true)
				{
					node = propsParser.GetNext();
					if (node.mKind == GLTFPropsParser::NodeKind_RBrace)
						break;
					if (node.mKind != GLTFPropsParser::NodeKind_String)
						return false;

					StringView prefix;
					StringView str = propsParser.GetStringView(node, prefix);
					if (str == "StaticMaterials")
					{	
						StaticMaterial staticMaterial;

						int idx = 0;
						if (!ExpectIndex(propsParser, idx))
							return false;
						if (!ExpectEquals(propsParser))
							return false;
						if (!ExpectOpen(propsParser))
							return false;

						while (true)
						{
							node = propsParser.GetNext();
							if (node.mKind == GLTFPropsParser::NodeKind_RBrace)
								break;
							if (node.mKind != GLTFPropsParser::NodeKind_String)
								return false;

							str = propsParser.GetStringView(node, prefix);
							if (str == "MaterialSlotName")
							{
								if (!ExpectEquals(propsParser))
									return false;
								if (!propsParser.GetNextStringView(prefix, str))
									return false;
								staticMaterial.mMaterialSlotName = str;
							}
							else if (str == "MaterialInterface")
							{
								if (!ExpectEquals(propsParser))
									return false;
								if (!propsParser.GetNextStringView(prefix, str))
									return false;
								staticMaterial.mMaterialDef = LoadMaterial(str);
							}
							else
								TrySkipValue(propsParser);
						}

						mStaticMaterials.Add(staticMaterial);
					}
					else
					{
						TrySkipValue(propsParser);
					}
				}
			}
			else
			{
				TrySkipValue(propsParser);
			}
		}
	}

	return true;
}

bool GLTFReader::ReadFile(const StringImpl& filePath, const StringImpl& rootDir)
{
	String basePathName;
	int dotPos = (int)filePath.LastIndexOf('.');
	if (dotPos > 0)
		basePathName = filePath.Substring(0, dotPos);
	else
		basePathName = basePathName;
	mBasePathName = basePathName;
	mRootDir = rootDir;

	String jsonPath = basePathName + ".gltf";	
	
	char* textData = LoadTextData(jsonPath, NULL);
	if (textData == NULL)
		return false;
	defer({ delete textData; });

	Json* jRoot = Json::Parse(textData);
	if (jRoot == NULL)
		return false;
	defer({ delete jRoot; });	

	LoadModelProps(basePathName + ".props.txt");
	
	Array<Array<uint8>> buffers;
	Array<DataSpan> bufferViews;
	Array<DataAccessor> dataAccessors;
	
	if (auto jBuffers = jRoot->GetObjectItem("buffers"))
	{
		for (auto jBuffer = jBuffers->mChild; jBuffer != NULL; jBuffer = jBuffer->mNext)
		{
			Array<uint8> data;
			if (auto jName = jBuffer->GetObjectItem("uri"))
			{
				if (jName->mValueString != NULL)
				{
					String dataPath = GetFileDir(basePathName) + "/" + jName->mValueString;

					int size = 0;
					uint8* rawData = LoadBinaryData(dataPath, &size);
					if (rawData != NULL)					
						data.Insert(0, rawData, size);					
				}
			}

			buffers.Add(data);
		}
	}

	if (auto jBufferViews = jRoot->GetObjectItem("bufferViews"))
	{
		for (auto jBufferView = jBufferViews->mChild; jBufferView != NULL; jBufferView = jBufferView->mNext)
		{
			int bufferIdx = 0;
			int byteOffset = 0;
			int byteLength = 0;

			if (auto jBufferIdx = jBufferView->GetObjectItem("buffer"))
				bufferIdx = jBufferIdx->mValueInt;
			if (auto jByteOffset = jBufferView->GetObjectItem("byteOffset"))
				byteOffset = jByteOffset->mValueInt;
			if (auto jByteLength = jBufferView->GetObjectItem("byteLength"))
				byteLength = jByteLength->mValueInt;			
			bufferViews.Add(DataSpan{ buffers[bufferIdx].mVals + byteOffset, byteLength });
		}
	}	

	if (auto jAccessors = jRoot->GetObjectItem("accessors"))
	{
		for (auto jAccessor = jAccessors->mChild; jAccessor != NULL; jAccessor = jAccessor->mNext)
		{
			DataAccessor dataAccessor = { 0 };
			if (auto jBufferIdx = jAccessor->GetObjectItem("bufferView"))
			{
				DataSpan& dataSpan = bufferViews[jBufferIdx->mValueInt];
				dataAccessor.mPtr = dataSpan.mPtr;
				dataAccessor.mSize = dataSpan.mSize;
			}
			if (auto jCount = jAccessor->GetObjectItem("count"))
				dataAccessor.mCount = jCount->mValueInt;
			if (auto jCount = jAccessor->GetObjectItem("componentType"))
				dataAccessor.mComponentType = (ComponentType)jCount->mValueInt;

			dataAccessors.Add(dataAccessor);
		}
	}

	auto _GetFloat3 = [&](Json* json, Vector3& vec)
	{
		int i = 0;
		for (auto jItem = json->mChild; jItem != NULL; jItem = jItem->mNext)
		{
			if (i == 0)
				vec.mX = (float)jItem->mValueDouble;
			if (i == 1)
				vec.mY = (float)jItem->mValueDouble;
			if (i == 2)
				vec.mZ = (float)jItem->mValueDouble;
			i++;
		}
	};

	auto _GetFloat4 = [&](Json* json, Vector4& vec)
	{
		int i = 0;
		for (auto jItem = json->mChild; jItem != NULL; jItem = jItem->mNext)
		{
			if (i == 0)
				vec.mX = (float)jItem->mValueDouble;
			if (i == 1)
				vec.mY = (float)jItem->mValueDouble;
			if (i == 2)
				vec.mZ = (float)jItem->mValueDouble;
			if (i == 3)
				vec.mW = (float)jItem->mValueDouble;
			i++;
		}
	};	
	
	if (auto jMaterials = jRoot->GetObjectItem("materials"))
	{
		int materialIdx = 0;
		for (auto jMaterial = jMaterials->mChild; jMaterial != NULL; jMaterial = jMaterial->mNext)
		{
			ModelMaterialInstance modelMaterialInstance;
			if (auto jName = jMaterial->GetObjectItem("name"))
			{
				if (jName->mValueString != NULL)
				{					
					modelMaterialInstance.mName = jName->mValueString;					
					String matPath = jName->mValueString;

					if (materialIdx < mStaticMaterials.mSize)
						matPath = mStaticMaterials[materialIdx].mMaterialSlotName;					

					ModelMaterialDef* materialDef = LoadMaterial(matPath);
					modelMaterialInstance.mDef = materialDef;
				}
			}
			if (auto jPBRMetallicRoughness = jMaterial->GetObjectItem("pbrMetallicRoughness"))
			{
				
			}			

			mModelDef->mMaterials.Add(modelMaterialInstance);
			materialIdx++;
		}
	}

	if (auto jMeshes = jRoot->GetObjectItem("meshes"))
	{
		for (auto jMesh = jMeshes->mChild; jMesh != NULL; jMesh = jMesh->mNext)
		{
			ModelMesh modelMesh;

			if (auto jName = jMesh->GetObjectItem("name"))
			{
				if (jName->mValueString != NULL)
					modelMesh.mName = jName->mValueString;
			}

			if (auto jPrimitives = jMesh->GetObjectItem("primitives"))
			{
				modelMesh.mPrimitives.Resize(jPrimitives->GetArraySize());				

				int primCount = 0;
				for (auto jPrimitive = jPrimitives->mChild; jPrimitive != NULL; jPrimitive = jPrimitive->mNext)
				{
					ModelPrimitives& modelPrimitives = modelMesh.mPrimitives[primCount];

					if (auto jIndices = jPrimitive->GetObjectItem("indices"))
					{
						auto& dataAccessor = dataAccessors[jIndices->mValueInt];						
						modelPrimitives.mIndices.ResizeRaw(dataAccessor.mCount);
						for (int i = 0; i < dataAccessor.mCount; i++)
							modelPrimitives.mIndices[i] = *(uint16*)(dataAccessor.mPtr + i * 2);
					}

					if (auto jIndices = jPrimitive->GetObjectItem("material"))
						modelPrimitives.mMaterial = &mModelDef->mMaterials[jIndices->mValueInt];

					if (auto jAttributes = jPrimitive->GetObjectItem("attributes"))
					{
						if (auto jPosition = jAttributes->GetObjectItem("POSITION"))
						{
							auto& dataAccessor = dataAccessors[jPosition->mValueInt];
							modelPrimitives.mVertices.Resize(dataAccessor.mCount);
							ReadBuffer<Vector3>(dataAccessor, &modelPrimitives.mVertices[0].mPosition, sizeof(ModelVertex));
						}

						if (auto jNormal = jAttributes->GetObjectItem("NORMAL"))													
							ReadBuffer<Vector3>(dataAccessors[jNormal->mValueInt], &modelPrimitives.mVertices[0].mNormal, sizeof(ModelVertex));
						if (auto jTangent = jAttributes->GetObjectItem("TANGENT"))
							ReadBuffer<Vector3>(dataAccessors[jTangent->mValueInt], &modelPrimitives.mVertices[0].mTangent, sizeof(ModelVertex), sizeof(Vector4));
						if (auto jColor = jAttributes->GetObjectItem("COLOR_0"))
							ReadBuffer<uint32>(dataAccessors[jColor->mValueInt], &modelPrimitives.mVertices[0].mColor, sizeof(ModelVertex));
						if (auto jTexCoords = jAttributes->GetObjectItem("TEXCOORD_0"))
						{
							ReadBuffer<TexCoords>(dataAccessors[jTexCoords->mValueInt], &modelPrimitives.mVertices[0].mTexCoords, sizeof(ModelVertex));
							for (auto& vertex : modelPrimitives.mVertices)
							{
								vertex.mTexCoords.mV = 1.0f - vertex.mTexCoords.mV;
							}
						}
						if (auto jTexCoords = jAttributes->GetObjectItem("TEXCOORD_1"))
						{
							ReadBuffer<TexCoords>(dataAccessors[jTexCoords->mValueInt], &modelPrimitives.mVertices[0].mTexCoords, sizeof(ModelVertex));
							for (auto& vertex : modelPrimitives.mVertices)
							{
								//vertex.mTexCoords.mU = 1.0f - vertex.mTexCoords.mU;
								vertex.mTexCoords.mV = 1.0f - vertex.mTexCoords.mV;
							}
						}
						else
						{
							for (auto& vertex : modelPrimitives.mVertices)
								vertex.mBumpTexCoords = vertex.mTexCoords;
						}
					}

					primCount++;
				}				
			}

			mModelDef->mMeshes.Add(modelMesh);
		}
	}

	if (auto jNodes = jRoot->GetObjectItem("nodes"))
	{		
		mModelDef->mNodes.Reserve(jNodes->GetArraySize());
		for (auto jNode = jNodes->mChild; jNode != NULL; jNode = jNode->mNext)
		{
			ModelNode modelNode;
			if (auto jName = jNode->GetObjectItem("name"))
			{
				if (jName->mValueString != NULL)
					modelNode.mName = jName->mValueString;
			}
			if (auto jChildren = jNode->GetObjectItem("children"))
			{
				for (auto jChild = jChildren->mChild; jChild != NULL; jChild = jChild->mNext)
				{
					int childIdx = jChild->mValueInt;
					modelNode.mChildren.Add(mModelDef->mNodes.mVals + childIdx);
				}
			}

			if (auto jTranslation = jNode->GetObjectItem("translation"))
				_GetFloat3(jTranslation, modelNode.mTranslation);
			if (auto jTranslation = jNode->GetObjectItem("rotation"))
				_GetFloat4(jTranslation, modelNode.mRotation);
			if (auto jMesh = jNode->GetObjectItem("mesh"))
				modelNode.mMesh = mModelDef->mMeshes.mVals + jMesh->mValueInt;

			mModelDef->mNodes.Add(modelNode);
		}
	}

	return true;
}
