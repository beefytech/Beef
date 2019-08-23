#include "BlCvTypeSource.h"
#include "BlContext.h"
#include "codeview/cvinfo.h"
#include "BlCvParser.h"
#include "BlPdbParser.h"
#include "BlCodeView.h"

#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define GET_INTO(T, name) T name = GET(T)
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

USING_NS_BF;

static int sIdx = 0;

BlCvTypeContainer::BlCvTypeContainer()
{
	mSectionData = NULL;
	mSectionSize = 0;
	mCvMinTag = -1;
	mCvMaxTag = -1;
	mCodeView = NULL;
	mOutTypeInfo = NULL;
	mIdx = sIdx++;
}

BlCvTypeContainer::~BlCvTypeContainer()
{
	
}

void BlCvTypeContainer::Fail(const StringImpl& err)
{
	BF_FATAL("Err");
}

const char* BlCvTypeContainer::CvParseAndDupString(uint8*& data, bool hasUniqueName)
{
	const char* strStart = (const char*)data;
	int strLen = (int)strlen((const char*)data);
	if (strLen == 0)
		return NULL;
	data += strLen + 1;
	if (hasUniqueName)
	{
		strLen = (int)strlen((const char*)data);
		data += strLen + 1;
	}
	char* retVal = (char*)mCodeView->mAlloc.AllocBytes((int)(data - (uint8*)strStart));
	memcpy(retVal, strStart, data - (uint8*)strStart);
	return retVal;
}

int BlCvTypeContainer::CreateMasterTag(int tagId, BlTypeInfo* outTypeInfo)
{
#define DECL_LEAF(leafType, leafName) \
		leafType& leafName = *(leafType*)&outBuffer[2];
#define FIXTPI(varName) \
		varName = GetMasterTPITag(varName);
#define FIXIPI(varName) \
		varName = GetMasterIPITag(varName);
	
	uint8* sectionData = mSectionData; //mCvTypeSectionData;

	uint8* srcDataStart = sectionData + mCvTagStartMap[tagId - mCvMinTag];	
	int16 tagLen = *(int16*)(srcDataStart - 2);
	uint8* srcDataEnd = srcDataStart + tagLen;
	
	int outSize = (int)(srcDataEnd - srcDataStart) + 2;
	uint8 outBuffer[0x10000];
	memcpy(outBuffer, srcDataStart - 2, outSize);
	uint8* data = (uint8*)outBuffer + 2;
	uint8* dataEnd = data + tagLen;

	int16 trLeafType = GET(uint16);

	BF_ASSERT(((intptr)data) % 4 == 0);

	switch (trLeafType)
	{
	case LF_VTSHAPE:
		break;
	case LF_LABEL:
		break;
	case LF_STRUCTURE:
	case LF_CLASS:
		{
			DECL_LEAF(lfClass, classInfo);
			FIXTPI(classInfo.derived);
			FIXTPI(classInfo.field);		
			FIXTPI(classInfo.vshape);		
		}
		break;
	case LF_ENUM:
		{
			DECL_LEAF(lfEnum, enumInfo);
			FIXTPI(enumInfo.field);		
		}
		break;
	case LF_UNION:
		{
			DECL_LEAF(lfUnion, unionInfo);
			FIXTPI(unionInfo.field);
		}
		break;
	case LF_ARRAY:
		{
			DECL_LEAF(lfArray, arrayInfo);
			FIXTPI(arrayInfo.elemtype);
			FIXTPI(arrayInfo.idxtype);
		}
		break;
	case LF_POINTER:
		{
			DECL_LEAF(lfPointer, pointerInfo);
			FIXTPI(pointerInfo.utype);
			if ((pointerInfo.attr.ptrmode == CV_PTR_MODE_PMEM) || (pointerInfo.attr.ptrmode == CV_PTR_MODE_PMFUNC))
			{
				FIXTPI(pointerInfo.pbase.pm.pmclass);
			}
			else if (pointerInfo.attr.ptrmode == CV_PTR_BASE_TYPE)
			{
				FIXTPI(pointerInfo.pbase.btype.index);
			}
		}
		break;
	case LF_MODIFIER:
		{
			DECL_LEAF(lfModifier, modifierInfo);
			FIXTPI(modifierInfo.type);
		}
		break;
	case LF_MFUNCTION:
		{
			DECL_LEAF(lfMFunc, func);
			FIXTPI(func.rvtype);
			FIXTPI(func.classtype);
			FIXTPI(func.thistype);
			FIXTPI(func.arglist);			
		}
		break;
	case LF_PROCEDURE:
		{
			DECL_LEAF(lfProc, proc);
			FIXTPI(proc.rvtype);
			FIXTPI(proc.arglist);
		}
		break;
	case LF_FIELDLIST:		
		{
			while (data < dataEnd)
			{
				static int itrIdx = 0;
				itrIdx++;

				uint8* leafDataStart = data;
				int leafType = (int)GET(uint16);
				switch (leafType)
				{
				case LF_VFUNCTAB:
					{
						lfVFuncTab& vfuncTab = *(lfVFuncTab*)leafDataStart;
						FIXTPI(vfuncTab.type);
						data = (uint8*)(&vfuncTab + 1);
					}
					break;
				case LF_BCLASS:
					{
						lfBClass& baseClassInfo = *(lfBClass*)leafDataStart;
						data = (uint8*)&baseClassInfo.offset;
						int thisOffset = (int)BlCvParser::CvParseConstant(data);
						FIXTPI(baseClassInfo.index);
					}
					break;
				case LF_VBCLASS:
				case LF_IVBCLASS:
					{
						lfVBClass& baseClassInfo = *(lfVBClass*)leafDataStart;
						data = (uint8*)&baseClassInfo.vbpoff;
						int thisOffset = (int)BlCvParser::CvParseConstant(data);
						int vTableOffset = (int)BlCvParser::CvParseConstant(data);
						FIXTPI(baseClassInfo.index);
						FIXTPI(baseClassInfo.vbptr);
					}
					break;
				case LF_ENUMERATE:
					{
						CV_fldattr_t fieldAttr = GET(CV_fldattr_t);
						int64 fieldVal = BlCvParser::CvParseConstant(data);
						const char* fieldName = BlCvParser::CvParseString(data);						
					}
					break;
				case LF_NESTTYPE:
					{
						lfNestType& nestType = *(lfNestType*)leafDataStart;
						FIXTPI(nestType.index);
						data = (uint8*)&nestType.Name;
						const char* typeName = BlCvParser::CvParseString(data);
					}
					break;
				case LF_ONEMETHOD:
					{
						lfOneMethod& oneMethod = *(lfOneMethod*)leafDataStart;
						FIXTPI(oneMethod.index);
						data = (uint8*)&oneMethod.vbaseoff;						
						int virtOffset = -1;
						if ((oneMethod.attr.mprop == CV_MTintro) || (oneMethod.attr.mprop == CV_MTpureintro))
						{
							virtOffset = GET(int32);							
						}
						const char* methodName = BlCvParser::CvParseString(data);						
					}
					break;
				case LF_METHOD:
					{
						lfMethod& method = *(lfMethod*)leafDataStart;
						FIXTPI(method.mList);
						data = (uint8*)&method.Name;						
						const char* methodName = BlCvParser::CvParseString(data);						
					}
					break;
				case LF_MEMBER:
				case LF_STMEMBER:
					{
						lfMember& member = *(lfMember*)leafDataStart;
						FIXTPI(member.index);
						bool isStatic = leafType == LF_STMEMBER;						
						data = (uint8*)&member.offset;
						int memberOffset = -1;
						if (!isStatic)
						{
							memberOffset = (int)BlCvParser::CvParseConstant(data);
							//
						}
						char* fieldName = (char*)BlCvParser::CvParseString(data);						
					}
				break;
				default:
					BF_FATAL("Unhandled");
				}

				PTR_ALIGN(data, outBuffer, 4);
				//BF_ASSERT(((intptr)data) % 4 == 0);
			}
		}
		break;
	case LF_ARGLIST:
		{
			int argCount = GET(int32);
			for (int argIdx = 0; argIdx < argCount; argIdx++)
			{
				CV_typ_t& argTypeId = GET(CV_typ_t);
				FIXTPI(argTypeId);
			}
		}
		break;
	case LF_METHODLIST:
		{
			while (data < dataEnd)
			{
				mlMethod& method = *(mlMethod*)data;
				FIXTPI(method.index);
				data = (uint8*)method.vbaseoff;
				if ((method.attr.mprop == CV_MTintro) || (method.attr.mprop == CV_MTpureintro))
				{
					int virtOffset = GET(int32);
				}
			}
		}
		break;
	case LF_STRING_ID:		
		break;
	case LF_SUBSTR_LIST:
		{
			DECL_LEAF(lfArgList, argList);
			data = (uint8*)&argList.arg;
			for (int idx = 0; idx < (int)argList.count; idx++)
			{
				CV_ItemId& itemId = GET(CV_ItemId);
				FIXIPI(itemId);
			}
		}
		break;
	case LF_UDT_SRC_LINE:
		{
			DECL_LEAF(lfUdtSrcLine, udtSrcLine);
			udtSrcLine.leaf = LF_UDT_MOD_SRC_LINE;
			FIXTPI(udtSrcLine.type);
			FIXIPI(udtSrcLine.src);

			lfUdtModSrcLine& udtModSrcLine = *(lfUdtModSrcLine*)&udtSrcLine;
			udtModSrcLine.imod = 1;			
			outSize += 4;
			*(int16*)(outBuffer) += 4;
		}
		break;
	case LF_FUNC_ID:
		{			
			DECL_LEAF(lfFuncId, funcId);
			FIXIPI(funcId.scopeId);
			FIXTPI(funcId.type);
		}
		break;
	case LF_MFUNC_ID:
		{			
			DECL_LEAF(lfMFuncId, mfuncId);
			FIXTPI(mfuncId.parentType);
			FIXTPI(mfuncId.type);
		}
		break;
	case LF_BUILDINFO:
		{
			DECL_LEAF(lfBuildInfo, buildInfo);
			data = (uint8*)&buildInfo.arg;
			for (int idx = 0; idx < buildInfo.count; idx++)
			{
				CV_ItemId& itemId = GET(CV_ItemId);
				FIXIPI(itemId);
			}
		}
		break;
	case LF_BITFIELD:
		{
			DECL_LEAF(lfBitfield, bitfield);
			FIXTPI(bitfield.type);
		}
		break;
	case LF_VFTABLE:
		{
			DECL_LEAF(lfVftable, vtTable);
			FIXTPI(vtTable.type);
			FIXTPI(vtTable.baseVftable);
		}
		break;	
	default:
		NotImpl();
		break;
	}

	BF_ASSERT(*(int16*)(outBuffer) == outSize - 2);
	outTypeInfo->mData.Write(outBuffer, outSize);
	return outTypeInfo->mCurTagId++;
}

void BlCvTypeContainer::HashName(const char* namePtr, bool hasMangledName, HashContext& hashContext)
{
	int hashSize = (int)strlen(namePtr);
	if (hasMangledName)
		hashSize += 1 + (int)strlen(namePtr + hashSize + 1);
	hashContext.Mixin(namePtr, hashSize);
}

int BlCvTypeContainer::GetMasterTPITag(int tagId)
{
	if (tagId == 0)
		return 0;
	if (tagId < mCvMinTag)
		return tagId;	
	int masterTag = (*mTPIMap)[tagId - mCvMinTag];
	if (masterTag == -1)
	{
		// Ensure we're TPI
		BF_ASSERT(&mTagMap == mTPIMap);
		ParseTag(tagId);
		masterTag = (*mTPIMap)[tagId - mCvMinTag];
		BF_ASSERT(masterTag != -1);
		return masterTag;
	}

	BF_ASSERT(masterTag != -1);
	return masterTag;
}

int BlCvTypeContainer::GetMasterIPITag(int tagId)
{
	if (tagId == 0)
		return 0;
	// Ensure we're IPI
	BF_ASSERT(&mTagMap == mIPIMap);	
	if (tagId < mCvMinTag)
		return tagId;
	int typeIdx = mTagMap[tagId - mCvMinTag];
	BF_ASSERT(typeIdx != -1);
	return typeIdx;
}

void BlCvTypeContainer::NotImpl()
{
	BF_FATAL("NotImpl");
}

void BlCvTypeContainer::ScanTypeData()
{
	uint8* sectionData = mSectionData;
	int sectionSize = mSectionSize;

	uint8* data = (uint8*)sectionData;
	uint8* dataEnd = data + sectionSize;

	if (mCvMinTag == -1)
	{
		mCvMinTag = 0x1000;

		int tagCountGuess = sectionSize / 128;
		mCvTagStartMap.reserve(tagCountGuess);

		int tagIdx = mCvMinTag;
		while (data < dataEnd)
		{
			int tagLen = GET(uint16);
			uint8* dataStart = data;
			uint8* dataTagEnd = data + tagLen;

			mCvTagStartMap.push_back((int)(data - sectionData));

			data = dataTagEnd;
			tagIdx++;
		}
		mCvMaxTag = tagIdx;
	}
	else
	{
		int tagIdx = mCvMinTag;
		while (data < dataEnd)
		{
			int tagLen = GET(uint16);
			uint8* dataStart = data;
			uint8* dataTagEnd = data + tagLen;

			mCvTagStartMap[tagIdx - mCvMinTag] = (int)(data - sectionData);

			data = dataTagEnd;
			tagIdx++;
		}
	}
}

void BlCvTypeContainer::HashTPITag(int tagId, HashContext& hashContext)
{
	int masterTagId = GetMasterTPITag(tagId);
	hashContext.Mixin(masterTagId);
}

void BlCvTypeContainer::HashIPITag(int tagId, HashContext& hashContext)
{
	int masterTagId = GetMasterIPITag(tagId);
	hashContext.Mixin(masterTagId);
}

void BlCvTypeContainer::HashTagContent(int tagId, HashContext& hashContext, bool& isIPI)
{
	uint8* sectionData = mSectionData; //mCvTypeSectionData;

	uint8* data = sectionData + mCvTagStartMap[tagId - mCvMinTag];
	uint8* dataStart = data;
	int16 tagLen = *(int16*)(data - 2);
	uint8* dataEnd = data + tagLen;
	int16 trLeafType = GET(uint16);

	hashContext.Mixin(trLeafType);

	switch (trLeafType)
	{
	case LF_VTSHAPE:
		{
			lfVTShape& vtShape = *(lfVTShape*)dataStart;
			data = (uint8*)&vtShape.desc;
			int shapeBytes = (vtShape.count + 1) / 2;
			hashContext.Mixin(data, shapeBytes);			
		}
		break;
	case LF_LABEL:
		{
			lfLabel& label = *(lfLabel*)dataStart;
			hashContext.Mixin(label.mode);
		}
		break;
	case LF_ARGLIST:
		{
			int argCount = (int)GET(int32);
			hashContext.Mixin(argCount);
			for (int argIdx = 0; argIdx < (int)argCount; argIdx++)
			{
				HashTPITag(GET(CV_typ_t), hashContext);
			}
		}
		break;
	case LF_FIELDLIST:
		{
			while (data < dataEnd)
			{
				uint8* leafDataStart = data;
				int leafType = (int)GET(uint16);
				hashContext.Mixin(leafType);
				switch (leafType)
				{
				case LF_VFUNCTAB:
					{
						lfVFuncTab& vfuncTab = *(lfVFuncTab*)leafDataStart;
						HashTPITag(vfuncTab.type, hashContext);
						data = (uint8*)(&vfuncTab + 1);
					}
					break;
				case LF_BCLASS:
					{
						lfBClass& baseClassInfo = *(lfBClass*)leafDataStart;
						data = (uint8*)&baseClassInfo.offset;
						int thisOffset = (int)BlCvParser::CvParseConstant(data);
						hashContext.Mixin(thisOffset);
						HashTPITag(baseClassInfo.index, hashContext);
					}
					break;
				case LF_VBCLASS:
				case LF_IVBCLASS:
					{
						lfVBClass& baseClassInfo = *(lfVBClass*)leafDataStart;
						data = (uint8*)&baseClassInfo.vbpoff;
						int thisOffset = (int)BlCvParser::CvParseConstant(data);
						int vTableOffset = (int)BlCvParser::CvParseConstant(data);
						hashContext.Mixin(baseClassInfo.attr);
						HashTPITag(baseClassInfo.index, hashContext);
						HashTPITag(baseClassInfo.vbptr, hashContext);
						HashTPITag(thisOffset, hashContext);
						HashTPITag(vTableOffset, hashContext);
					}
					break;
				case LF_ENUMERATE:
					{
						CV_fldattr_t fieldAttr = GET(CV_fldattr_t);
						int64 fieldVal = BlCvParser::CvParseConstant(data);
						const char* fieldName = BlCvParser::CvParseString(data);
						hashContext.Mixin(fieldAttr);
						hashContext.Mixin(fieldVal);
						hashContext.MixinStr(fieldName);
					}
					break;
				case LF_NESTTYPE:
					{
						int16 pad = GET(int16);
						int32 nestedTypeId = GET(int32);
						//int64 nestedSize = BlCvParser::CvParseConstant(data);
						const char* typeName = BlCvParser::CvParseString(data);
						HashTPITag(nestedTypeId, hashContext);
					}
					break;
				case LF_ONEMETHOD:
					{
						CV_fldattr_t attr = GET(CV_fldattr_t);
						CV_typ_t methodTypeId = GET(CV_typ_t);
						hashContext.Mixin(attr);
						HashTPITag(methodTypeId, hashContext);

						int virtOffset = -1;
						if ((attr.mprop == CV_MTintro) || (attr.mprop == CV_MTpureintro))
						{
							virtOffset = GET(int32);
							hashContext.Mixin(virtOffset);
						}						
						const char* name = BlCvParser::CvParseString(data);
						hashContext.MixinStr(name);
					}
					break;
				case LF_METHOD:
					{
						int count = (int)GET(uint16);
						int32 methodList = GET(int32);
						const char* name = BlCvParser::CvParseString(data);
						hashContext.MixinStr(name);
					}
					break;
				case LF_MEMBER:
				case LF_STMEMBER:
					{
						bool isStatic = leafType == LF_STMEMBER;
						bool isConst = false;
						CV_fldattr_t attr = GET(CV_fldattr_t);
						CV_typ_t fieldTypeId = GET(CV_typ_t);
						hashContext.Mixin(attr);

						int memberOffset = -1;
						if (!isStatic)
						{
							memberOffset = (int)BlCvParser::CvParseConstant(data);
							hashContext.Mixin(memberOffset);
						}
						const char* name = BlCvParser::CvParseString(data);
						hashContext.MixinStr(name);
						HashTPITag(fieldTypeId, hashContext);
					}
					break;
				default:
					BF_FATAL("Unhandled");
				}

				PTR_ALIGN(data, sectionData, 4);
			}
		}
		break;
	case LF_METHODLIST:
		{
			while (data < dataEnd)
			{
				mlMethod& method = *(mlMethod*)data;
				bool tempBool = false;
				HashTagContent(method.index, hashContext, tempBool);
				data = (uint8*)method.vbaseoff;
				if ((method.attr.mprop == CV_MTintro) || (method.attr.mprop == CV_MTpureintro))
				{
					int virtOffset = GET(int32);
					hashContext.Mixin(virtOffset);
				}
			}
		}
		break;
	case LF_STRUCTURE:
	case LF_CLASS:
		{			
			lfClass& classInfo = *(lfClass*)dataStart;
			data = (uint8*)&classInfo.data;
			int dataSize = (int)BlCvParser::CvParseConstant(data);
			HashName((char*)data, classInfo.property.hasuniquename, hashContext);
			hashContext.Mixin(dataSize);
			hashContext.Mixin(classInfo.property);			
			HashTPITag(classInfo.field, hashContext);
			HashTPITag(classInfo.derived, hashContext);
			HashTPITag(classInfo.vshape, hashContext);
		}
		break;
	case LF_ENUM:
		{
			lfEnum& enumInfo = *(lfEnum*)dataStart;
			data = (uint8*)enumInfo.Name;
			HashName((char*)data, enumInfo.property.hasuniquename, hashContext);
			hashContext.Mixin(enumInfo.property);
			HashTPITag(enumInfo.utype, hashContext);
			HashTPITag(enumInfo.field, hashContext);			
		}
		break;
	case LF_UNION:
		{
			lfUnion& unionInfo = *(lfUnion*)dataStart;
			data = (uint8*)unionInfo.data;
			int dataSize = (int)BlCvParser::CvParseConstant(data);
			HashName((char*)data, unionInfo.property.hasuniquename, hashContext);
			hashContext.Mixin(dataSize);
			hashContext.Mixin(unionInfo.property);
			HashTPITag(unionInfo.field, hashContext);			
		}
		break;
	case LF_ARRAY:
		{
			lfArray& arrayInfo = *(lfArray*)dataStart;
			data = (uint8*)arrayInfo.data;
			int dataSize = (int)BlCvParser::CvParseConstant(data);
			HashName((char*)data, false, hashContext);			
			HashTPITag(arrayInfo.elemtype, hashContext);
			HashTPITag(arrayInfo.idxtype, hashContext);
		}
		break;
	case LF_MFUNCTION:
		{
			lfMFunc& func = *(lfMFunc*)dataStart;
			HashTPITag(func.thistype, hashContext);
			HashTPITag(func.classtype, hashContext);
			HashTPITag(func.rvtype, hashContext);
			HashTPITag(func.arglist, hashContext);
			hashContext.Mixin(func.funcattr);
			hashContext.Mixin(func.calltype);
			hashContext.Mixin(func.parmcount);
			hashContext.Mixin(func.thisadjust);
		}
		break;
	case LF_PROCEDURE:
		{
			lfProc& proc = *(lfProc*)dataStart;
			HashTPITag(proc.rvtype, hashContext);
			HashTPITag(proc.arglist, hashContext);
			hashContext.Mixin(proc.funcattr);
			hashContext.Mixin(proc.calltype);
			hashContext.Mixin(proc.parmcount);
		}
		break;	
	case LF_POINTER:
		{
			lfPointer& pointerInfo = *(lfPointer*)dataStart;
			HashTPITag(pointerInfo.utype, hashContext);
			hashContext.Mixin(pointerInfo.attr);			
		}
		break;
	case LF_MODIFIER:
		{
			lfModifier& modifierInfo = *(lfModifier*)dataStart;
			HashTPITag(modifierInfo.type, hashContext);
			hashContext.Mixin(modifierInfo.attr);
		}
		break;
	case LF_STRING_ID:
		{
			isIPI = true;
			lfStringId& stringId = *(lfStringId*)dataStart;
			HashIPITag(stringId.id, hashContext);
			HashName((char*)stringId.name, false, hashContext);			
		}
		break;
	case LF_SUBSTR_LIST:
		{
			lfArgList& argList = *(lfArgList*)dataStart;
			data = (uint8*)&argList.arg;
			for (int idx = 0; idx < (int)argList.count; idx++)
			{
				CV_ItemId& itemId = GET(CV_ItemId);
				HashIPITag(itemId, hashContext);
			}
		}	
		break;
	case LF_UDT_SRC_LINE:
		{
			lfUdtSrcLine& srcLine = *(lfUdtSrcLine*)dataStart;
			HashTPITag(srcLine.type, hashContext);
			HashIPITag(srcLine.src, hashContext);
		}
		break;
	case LF_FUNC_ID:
		{
			isIPI = true;
			lfFuncId& funcId = *(lfFuncId*)dataStart;
			HashIPITag(funcId.scopeId, hashContext);
			HashTPITag(funcId.type, hashContext);
			HashName((char*)funcId.name, false, hashContext);
		}
		break;
	case LF_MFUNC_ID:
		{
			isIPI = true;
			lfMFuncId& mfuncId = *(lfMFuncId*)dataStart;
			HashTPITag(mfuncId.parentType, hashContext);
			HashTPITag(mfuncId.type, hashContext);
			HashName((char*)mfuncId.name, false, hashContext);
		}
		break;
	case LF_BUILDINFO:
		{
			lfBuildInfo& buildInfo = *(lfBuildInfo*)dataStart;
			data = (uint8*)&buildInfo.arg;
			for (int idx = 0; idx < (int)buildInfo.count; idx++)
			{
				CV_ItemId& itemId = GET(CV_ItemId);
				HashIPITag(itemId, hashContext);
			}
		}
		break;
	case LF_BITFIELD:
		{
			lfBitfield& bitfield = *(lfBitfield*)dataStart;
			HashTPITag(bitfield.type, hashContext);
			hashContext.Mixin(bitfield.length);
			hashContext.Mixin(bitfield.position);
		}
		break;
	case LF_VFTABLE:
		{
			lfVftable& vtTable = *(lfVftable*)dataStart;
			HashTPITag(vtTable.type, hashContext);
			HashTPITag(vtTable.baseVftable, hashContext);
			hashContext.Mixin(vtTable.offsetInObjectLayout);
			data = (uint8*)&vtTable.Names;
			hashContext.Mixin(data, vtTable.len);
		}
		break;
	default:
		NotImpl();
		break;
	}
}

void BlCvTypeContainer::ParseTag(int tagId, bool forceFull)
{
	uint8* data = mSectionData + mCvTagStartMap[tagId - mCvMinTag];
	uint8* dataStart = data;
	int16 tagLen = *(int16*)(data - 2);
	uint8* dataEnd = data + tagLen;
	int16 trLeafType = GET(uint16);

	//TODO: Bad? Good?
	//forceFull = true;

	bool wantsExt = false;
	int memberMasterTag = 0;
	switch (trLeafType)
	{
	case LF_FUNC_ID:
		{
			mCodeView->mStat_ParseTagFuncs++;

			wantsExt = true;
			lfFuncId& funcId = *(lfFuncId*)dataStart;			
			int memberTagId = GetMasterTPITag(funcId.type);
			mElementMap[tagId - mCvMinTag] = memberTagId;

			/*if (!forceFull)
			{
				mTagMap[tagId - mCvMinTag] = memberMasterTag | BlTypeMapFlag_InfoExt_ProcId_TypeOnly;
				return;
			}*/			

			//return;
		}
		break;
	case LF_MFUNC_ID:
		{
			mCodeView->mStat_ParseTagFuncs++;

			wantsExt = true;
			lfMFuncId& funcId = *(lfMFuncId*)dataStart;

			int memberTagId = GetMasterTPITag(funcId.type);
			mElementMap[tagId - mCvMinTag] = memberTagId;
			/*if (!forceFull)
			{
				mTagMap[tagId - mCvMinTag] = memberMasterTag | BlTypeMapFlag_InfoExt_ProcId_TypeOnly;
				return;
			}*/

			//return;

		}
		break;
	}
	
	HashContext hashContext;
	bool isIPI = false;
	HashTagContent(tagId, hashContext, isIPI);
	auto outTypeInfo = mOutTypeInfo;
	if (isIPI)
		outTypeInfo = &mCodeView->mIPI;
	
	mCodeView->mStat_TypeMapInserts++;

	Val128 hashVal = hashContext.Finish128();

	int masterTagId;
	auto insertPair = outTypeInfo->mTypeMap.insert(std::make_pair(hashVal, -1));
	if (insertPair.second)
	{
		masterTagId = CreateMasterTag(tagId, outTypeInfo);
		insertPair.first->second = masterTagId;		
	}
	else
	{
		masterTagId = insertPair.first->second;
	}

	/*if (wantsExt)
	{
		mTagMap[tagId - mCvMinTag] = (int)mInfoExts.size() | BlTypeMapFlag_InfoExt_ProcId_Resolved;

		BlCvTypeInfoExt infoExt;
		infoExt.mMasterTag = masterTagId;
		infoExt.mMemberMasterTag = memberMasterTag;
		mInfoExts.push_back(infoExt);
	}
	else*/
		mTagMap[tagId - mCvMinTag] = masterTagId;
}

void BlCvTypeContainer::ParseTypeData()
{
	mTagMap.insert(mTagMap.begin(), mCvTagStartMap.size(), -1);
	mElementMap.insert(mElementMap.begin(), mCvTagStartMap.size(), -1);
	for (int tagId = mCvMinTag; tagId < mCvMaxTag; tagId++)
	{
		if (mTagMap[tagId - mCvMinTag] == -1)
			ParseTag(tagId);
	}
}

//////////////////////////////////////////////////////////////////////////

BlCvTypeSource::BlCvTypeSource()
{
	mTypeServerLib = NULL;
	mIPI = NULL;
	mIsDone = false;
	mObjectData = NULL;
}

BlCvTypeSource::~BlCvTypeSource()
{
	delete mTypeServerLib;
	if (mIPI != &mTPI)
		delete mIPI;
}

void BlCvTypeSource::CreateIPI()
{
	auto codeView = mTPI.mCodeView;

	BF_ASSERT(mIPI == &mTPI);
	mIPI = new BlCvTypeContainer();	
	mIPI->mCodeView = codeView;
	mIPI->mOutTypeInfo = &codeView->mIPI;
	mIPI->mTPIMap = &mTPI.mTagMap;
	mIPI->mIPIMap = &mIPI->mTagMap;
}

void BlCvTypeSource::Init(BlCodeView* codeView)
{	
	mTPI.mCodeView = codeView;
	mTPI.mOutTypeInfo = &codeView->mTPI;
	mTPI.mTPIMap = &mTPI.mTagMap;
	mTPI.mIPIMap = &mTPI.mTagMap;
	mIPI = &mTPI;
}
