#include "COFF.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/MemStream.h"
#include "codeview/cvinfo.h"
#include "DebugTarget.h"
#include "DebugManager.h"
#include "DWARFInfo.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/ZipFile.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/platform/PlatformHelper.h"
#include "WinDebugger.h"
#include "MiniDumpDebugger.h"
#include "Linker/BlHash.h"
#include "Backend/BeLibManger.h"
#include "Compiler/BfUtil.h"
#include <shlobj.h>

#include "BeefySysLib/util/AllocDebug.h"

#define LF_CLASS_EX 0x1608
#define LF_STRUCTURE_EX 0x1609

USING_NS_BF_DBG;

#define MSF_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\032DS\0\0"
static const int NUM_ROOT_DIRS = 73;

#pragma warning(default:4800)

#define ADDR_FLAG_ABS 0x8000000000000000L

#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define GET_INTO(T, name) T name = GET(T)
#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

static const char* DataGetString(uint8*& data)
{
	const char* prevVal = (const char*)data;
	while (*data != 0)
		data++;
	data++;
	return prevVal;
}

#define CREATE_PRIMITIVE(pdbTypeCode, dwTypeCode, typeName, type) \
	BP_ALLOC_T(DbgType); \
	dbgType = mAlloc.Alloc<DbgType>(); \
	dbgType->mCompileUnit = mMasterCompileUnit; \
	dbgType->mTypeName = typeName; \
	dbgType->mName = typeName; \
	dbgType->mTypeCode = dwTypeCode; \
	dbgType->mSize = sizeof(type); \
	dbgType->mAlign = sizeof(type); \
	mCvSystemTypes[(int)pdbTypeCode] = dbgType; \
	\
	BP_ALLOC_T(DbgType); \
	ptrType = mAlloc.Alloc<DbgType>(); \
	ptrType->mCompileUnit = mMasterCompileUnit; \
	ptrType->mTypeCode = DbgType_Ptr; \
	ptrType->mSize = sizeof(addr_target); \
	ptrType->mAlign = sizeof(addr_target); \
	ptrType->mTypeParam = dbgType; \
	dbgType->mPtrType = ptrType; \
	mCvSystemTypes[(int)pdbTypeCode | ptrMask] = ptrType;

#define REGISTER_PRIMITIVE() mTypeMap.Insert(dbgType)

static const char* GetLastDoubleColon(const char* name)
{
	int templateDepth = 0;
	int parenDepth = 0;
	const char* lastDblColon = NULL;
	for (const char* checkPtr = name; *checkPtr != '\0'; checkPtr++)
	{
		char c = checkPtr[0];
		if (c == '<')
			templateDepth++;
		else if (c == '>')
			templateDepth--;
		else if (c == '(')
			parenDepth++;
		else if (c == ')')
			parenDepth--;
		if ((templateDepth == 0) && (parenDepth == 0) && (c == ':') && (checkPtr[1] == ':'))
			lastDblColon = checkPtr;
		if ((templateDepth == 0) && (parenDepth == 0) && (c == ' '))
		{
			// This catches cases like "Beefy::BfMethodRef::operator Beefy::BfMethodInstance*", where we want to match the :: right before "operator"
			break;
		}
	}
	return lastDblColon;
}

static const char* GetNamespaceEnd(const char* name)
{
	int templateDepth = 0;
	const char* lastDblColon = NULL;
	for (const char* checkPtr = name; *checkPtr != '\0'; checkPtr++)
	{
		char c = checkPtr[0];
		if ((c == '<') || (c == ' '))
			return NULL;
		if ((c == ':') && (checkPtr[1] == ':'))
			lastDblColon = checkPtr;
	}
	return lastDblColon;
}

// This version inserts namespaces that contain specialized methods, but since we don't (at the time of this comment) support
//  calling specialized methods, we don't bother doing this -- it increases the scan time
#if 0
static const char* GetNamespaceEnd(const char* name)
{
	bool hadTemplate = false;
	int templateDepth = 0;
	const char* lastDblColon = NULL;
	for (const char* checkPtr = name; *checkPtr != '\0'; checkPtr++)
	{
		char c = checkPtr[0];
		if (c == '<')
		{
			hadTemplate = true;
			templateDepth++;
		}
		else if (c == '>')
			templateDepth--;
		if ((templateDepth == 0) && (c == ':') && (checkPtr[1] == ':'))
		{
			if (hadTemplate)
			{
				// This is to reject cases like NameSpace::TypeName<int>::sField - since we know that there's a typename attached and we just want 'loose' namespaces,
				// But this still allows NameSpace::Method<int>, where "NameSpace" is indeed a loose namespace we need to catch
				return NULL;
			}
			lastDblColon = checkPtr;
		}
		if ((templateDepth == 0) && (c == ' '))
		{
			// This catches cases like "Beefy::BfMethodRef::operator Beefy::BfMethodInstance*", where we want to match the :: right before "operator"
			//  In these cases we do know this is an operator method name so it's not in a loose namespace
			return NULL;
		}
	}
	return lastDblColon;
}
#endif

//////////////////////////////////////////////////////////////////////////

uint8* CvStreamReader::GetTempPtr(int offset, int size, bool mayRecurse, bool* madeCopy)
{
	int pageStart = offset >> mPageBits;
	int pageEnd = (offset + size - 1) >> mPageBits;

	if (pageStart >= pageEnd)
		return mStreamPtrs.mVals[pageStart] + (offset & ((1<<mPageBits) - 1));

	// Handle the relatively-rare case of spanning multiple pages
	if (madeCopy != NULL)
		*madeCopy = true;

	uint8* destPtr;
	if (mayRecurse)
	{
		if (mCOFF->mTempBufIdx >= mCOFF->mTempBufs.size())
			mCOFF->mTempBufs.push_back(Array<uint8>());
		auto& arr = mCOFF->mTempBufs[mCOFF->mTempBufIdx++];
		if (arr.size() < size)
			arr.Resize(size);
		destPtr = arr.mVals;
	}
	else
	{
		if (mTempData.size() < size)
			mTempData.Resize(size);
		destPtr = mTempData.mVals;
	}

	uint8* dest = destPtr;
	while (size > 0)
	{
		int copyPage = offset >> mPageBits;
		int pageOffset = offset & ((1 << mPageBits) - 1);
		int copyBytes = BF_MIN(mCOFF->mCvPageSize - pageOffset, size);
		memcpy(destPtr, mStreamPtrs.mVals[copyPage] + pageOffset, copyBytes);
		destPtr += copyBytes;
		offset += copyBytes;
		size -= copyBytes;
	}
	return dest;
}

uint8* CvStreamReader::GetPermanentPtr(int offset, int size, bool* madeCopy)
{
	int pageStart = offset >> mPageBits;
	int pageEnd = (offset + size - 1) >> mPageBits;

	if (pageStart == pageEnd)
		return mStreamPtrs.mVals[pageStart] + (offset & ((1 << mPageBits) - 1));

	// Handle the relatively-rare case of spanning multiple pages
	BP_ALLOC("GetPermanentPtr", size);
	uint8* destData = mCOFF->mAlloc.AllocBytes(size, "GetPermanentPtr");
	if (madeCopy != NULL)
		*madeCopy = true;

	uint8* destPtr = destData;
	while (size > 0)
	{
		int copyPage = offset >> mPageBits;
		int pageOffset = offset & ((1 << mPageBits) - 1);
		int copyBytes = BF_MIN(mCOFF->mCvPageSize - pageOffset, size);
		memcpy(destPtr, mStreamPtrs.mVals[copyPage] + pageOffset, copyBytes);
		destPtr += copyBytes;
		offset += copyBytes;
		size -= copyBytes;
	}
	return destData;
}

//////////////////////////////////////////////////////////////////////////

COFF::COFF(DebugTarget* debugTarget) : DbgModule(debugTarget)
{
	mParseKind = ParseKind_Full;
	memset(mWantPDBGuid, 0, 16);
	memset(mPDBGuid, 0, 16);
	mWantAge = -1;
	mDebugAge = -1;
	mFileAge = -1;
	mCvMinTag = -1;
	mCvMaxTag = -1;
	mCvIPIMinTag = -1;
	mCvIPIMaxTag = -1;
	mMasterCompileUnit = NULL;
	mTempBufIdx = 0;
	mIs64Bit = false;

	mCvPageSize = 0;
	mCvPageBits = 31;
	mCvDataStream = NULL;
	mCvHeaderData = NULL;
	mCvStrTableData = NULL;
	mCvPublicSymbolData = NULL;
	mCvGlobalSymbolData = NULL;
	mNewFPOData = NULL;
	mCvGlobalSymbolInfoStream = 0;
	mCvPublicSymbolInfoStream = 0;
	mCvSymbolRecordStream = 0;
	mCvNewFPOStream = 0;
	mCvMappedFile = INVALID_HANDLE_VALUE;
	mCvMappedFileMapping = INVALID_HANDLE_VALUE;
	mCvMappedViewOfFile = NULL;
	mCvMappedFileSize = 0;
	//mParsedProcRecs = false;

	mGlobalsTargetType = NULL;
	mPrevScanName = NULL;
	mProcSymCount = 0;
	mCvSrcSrvStream = -1;
	mCvEmitStream = -1;
	mIsFastLink = false;
	mHotThunkCurAddr = 0;
	mHotThunkDataLeft = 0;

	mTriedSymSrv = false;
	mDbgSymRequest = NULL;
	mWantsAutoLoadDebugInfo = false;
	mPDBLoaded = false;
	mEmitSourceFile = NULL;
}

COFF::~COFF()
{
	BF_ASSERT(mTempBufIdx == 0);
	ClosePDB();
	mDebugger->mDbgSymSrv.ReleaseRequest(mDbgSymRequest);
}

const char* COFF::CvCheckTargetMatch(const char* name, bool& wasBeef)
{
	if (mGlobalsTargetType == NULL)
		return NULL;

	/*if (mGlobalsTargetType->IsGlobalsContainer())
	{
		wasBeef = true;
	}
	else*/ if (mGlobalsTargetType->mLanguage == DbgLanguage_Beef)
	{
		if (strncmp(name, "_bf::", 5) != 0)
			return NULL;
		name += 5;
		wasBeef = true;
	}
	else if (mGlobalsTargetType->mTypeCode == DbgType_Root)
	{
		if (strncmp(name, "_bf::", 5) == 0)
		{
			wasBeef = true;
			name += 5;
		}
	}

	if (mGlobalsTargetType->mName == NULL)
	{
		for (const char* cPtr = name; *cPtr != '\0'; cPtr++)
		{
			if (*cPtr == ':')
				return NULL;
			if (*cPtr == '<')
				return NULL;
		}

		return name;
	}

	const char* memberNamePtr = name;
	const char* typeName = mGlobalsTargetType->mName;

	while (true)
	{
		char cm = *(memberNamePtr++);
		char ct = *(typeName++);
		if (ct == 0)
		{
			if (cm == ':')
			{
				const char* memberName = memberNamePtr + 1;
				for (const char* cPtr = memberName; *cPtr != '\0'; cPtr++)
				{
					if (*cPtr == ':')
						return NULL;
					if (*cPtr == '<')
						return NULL;
				}
				return memberName;
			}
			return NULL;
		}
		if (cm != ct)
		{
			if ((cm == ':') && (ct == '.') && (memberNamePtr[0] == ':'))
			{
				memberNamePtr++;
				continue;
			}
            return NULL;
		}
	}
}

int COFF::CvGetStringHash(const char* str)
{
	if (str == NULL)
		return 0;

	int curHash = 0;
	const char* curHashPtr = str;
	while (*curHashPtr != 0)
	{
		char c = *curHashPtr;
		curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
		curHashPtr++;
	}
	return curHash & 0x3FFFFFFF;
}

// Remove "__ptr64"
void COFF::CvFixupName(char* name)
{
	char* cPtrOut = NULL;
	for (char* cPtr = name; *cPtr != '\0'; cPtr++)
	{
		if ((cPtr[0] == '_') && (cPtr[1] == '_'))
		{
			if (strncmp(cPtr + 2, "ptr64", 5) == 0)
			{
				if (cPtrOut == NULL)
					cPtrOut = cPtr;
				cPtr += 6;
			}
		}
		else if (cPtrOut != NULL)
		{
			*(cPtrOut++) = *cPtr;
		}
	}

	if (cPtrOut != NULL)
		*(cPtrOut++) = '\0';
}

void COFF::InitCvTypes()
{
	if (mMasterCompileUnit != NULL)
	{
		BF_ASSERT(mCompileUnits.size() == 1);
		return;
	}

	mDbgFlavor = DbgFlavor_MS;
	mCvSystemTypes.Resize(0x700);

	DbgType* dbgType;
	DbgType* ptrType;

#ifdef BF_DBG_32
	const int ptrMask = 0x0400; // T_32*
#else
	const int ptrMask = 0x0600; // T_64*
#endif
	mMasterCompileUnit = new DbgCompileUnit(this);
	mMasterCompileUnit->mDbgModule = this;
	mMasterCompileUnit->mIsMaster = true;
	mCompileUnits.push_back(mMasterCompileUnit);

	CREATE_PRIMITIVE(T_NOTTRANS, DbgType_Void, "void", void*);
	mCvSystemTypes[T_NOTTRANS]->mSize = 0;
	mCvSystemTypes[T_NOTTRANS]->mAlign = 0;
	CREATE_PRIMITIVE(T_NOTYPE, DbgType_Void, "void", void*);
	mCvSystemTypes[T_NOTYPE]->mSize = 0;
	mCvSystemTypes[T_NOTYPE]->mAlign = 0;
	CREATE_PRIMITIVE(T_VOID, DbgType_Void, "void", void*);
	mCvSystemTypes[T_VOID]->mSize = 0;
	mCvSystemTypes[T_VOID]->mAlign = 0;
	mCvSystemTypes[T_PVOID] = ptrType;

#ifdef BF_DBG_32
	BP_ALLOC_T(DbgType);
	ptrType = mAlloc.Alloc<DbgType>();
	ptrType->mCompileUnit = mMasterCompileUnit;
	ptrType->mTypeCode = DbgType_Ptr;
	ptrType->mSize = 8;
	ptrType->mAlign = 8;
	ptrType->mTypeParam = dbgType;
	dbgType->mPtrType = ptrType;
	mCvSystemTypes[(int)T_VOID | 0x0600] = ptrType;
#endif

#ifdef BF_DBG_32
	CREATE_PRIMITIVE(T_HRESULT, DbgType_u32, "HRESULT", addr_target);
#else
	CREATE_PRIMITIVE(T_HRESULT, DbgType_u64, "HRESULT", addr_target);
#endif
	CREATE_PRIMITIVE(T_CHAR, DbgType_SChar, "char", char);
	CREATE_PRIMITIVE(T_RCHAR, DbgType_SChar, "char", char); REGISTER_PRIMITIVE();
	CREATE_PRIMITIVE(T_UCHAR, DbgType_UChar, "char8", uint8); REGISTER_PRIMITIVE();
	CREATE_PRIMITIVE(T_WCHAR, DbgType_SChar16, "wchar", uint16); REGISTER_PRIMITIVE();
	CREATE_PRIMITIVE(T_CHAR16, DbgType_UChar16, "char16", uint16);
	CREATE_PRIMITIVE(T_CHAR32, DbgType_UChar32, "char32", uint32);
	CREATE_PRIMITIVE(T_INT1, DbgType_i8, "sbyte", int8);
	CREATE_PRIMITIVE(T_UINT1, DbgType_u8, "byte", uint8);
	CREATE_PRIMITIVE(T_SHORT, DbgType_i16, "short", int16);
	CREATE_PRIMITIVE(T_INT2, DbgType_i16, "short", int16);
	CREATE_PRIMITIVE(T_USHORT, DbgType_u16, "ushort", uint16);
	CREATE_PRIMITIVE(T_UINT2, DbgType_u16, "ushort", uint16);
	CREATE_PRIMITIVE(T_LONG, DbgType_i32, "int", int32);
	CREATE_PRIMITIVE(T_INT4, DbgType_i32, "int", int32);
	CREATE_PRIMITIVE(T_ULONG, DbgType_i32, "uint", uint32);
	CREATE_PRIMITIVE(T_UINT4, DbgType_u32, "uint", uint32);
	CREATE_PRIMITIVE(T_QUAD, DbgType_i64, "int64", int64);
	CREATE_PRIMITIVE(T_INT8, DbgType_i64, "int64", int64); REGISTER_PRIMITIVE();
	CREATE_PRIMITIVE(T_UQUAD, DbgType_u64, "uint64", uint64);
	CREATE_PRIMITIVE(T_UINT8, DbgType_u64, "uint64", uint64); REGISTER_PRIMITIVE();
	CREATE_PRIMITIVE(T_OCT, DbgType_i128, "int128", uint128);
	CREATE_PRIMITIVE(T_INT16, DbgType_i128, "int128", uint128);
	CREATE_PRIMITIVE(T_UOCT, DbgType_u128, "uint128", uint128);
	CREATE_PRIMITIVE(T_UINT16, DbgType_u128, "uint128", uint128);
	CREATE_PRIMITIVE(T_BOOL08, DbgType_Bool, "bool", bool);
	CREATE_PRIMITIVE(T_BOOL32, DbgType_Bool, "bool", int32);
	CREATE_PRIMITIVE(T_REAL32, DbgType_Single, "float", float);
	CREATE_PRIMITIVE(T_REAL64, DbgType_Double, "double", double);
	CREATE_PRIMITIVE(T_REAL80, DbgType_Double, "double80", double); // This isn't correct
}

addr_target COFF::GetSectionAddr(uint16 section, uint32 offset)
{
	if (section == 0)
		return offset;

	if ((section & 0x8000) != 0)
	{
		auto linkedModule = GetLinkedModule();
		addr_target hiBase = linkedModule->mSecRelEncodingVec[section & 0x7FFF];
		return hiBase + offset;
	}

	int rva = mSectionRVAs[section - 1];
	if (rva == 0)
		return ADDR_FLAG_ABS + offset;

	return (addr_target)mImageBase + rva + offset;
}

DbgType* COFF::CvGetType(int typeId)
{
	//TODO: How do we handle types that have the high bit set?
	if (typeId < 0)
		return NULL;

	/*if (typeId == 0)
		return NULL;*/
	if (typeId < 0x1000)
	{
		TYPE_ENUM_e typeEnum = (TYPE_ENUM_e)typeId;
		DbgType* type = mCvSystemTypes[typeId];
		return type;
	}

	DbgType* type = mCvTypeMap[typeId - mCvMinTag];
	if (type == NULL)
		type = CvParseType(typeId);

	/*if ((!allowNull) || (type != NULL))
	{
		BF_ASSERT(type->mCompileUnit->mDbgModule == this);
	}*/

	return type;
}

DbgType* COFF::CvGetTypeSafe(int typeId)
{
	DbgType* type = CvGetType(typeId);
	if (type != NULL)
		return type;
	return CvGetType(T_VOID);
}

DbgType* COFF::CvGetType(int typeId, CvCompileUnit* compileUnit)
{
	if ((typeId & 0x80000000) != 0)
	{
		return CvGetType(MapImport(compileUnit, typeId));
	}
	return CvGetType(typeId);
}

int COFF::CvGetTagStart(int tagIdx, bool ipi)
{
	if (tagIdx == 0)
		return NULL;
	if (ipi)
		return mCvIPITagStartMap[tagIdx - mCvMinTag];
	else
		return mCvTagStartMap[tagIdx - mCvMinTag];
}

int COFF::CvGetTagSize(int tagIdx, bool ipi)
{
	if (tagIdx == 0)
		return 0;
	if (ipi)
		return mCvIPITagStartMap[tagIdx - mCvMinTag + 1] - mCvIPITagStartMap[tagIdx - mCvMinTag];
	else
		return mCvTagStartMap[tagIdx - mCvMinTag + 1] - mCvTagStartMap[tagIdx - mCvMinTag];
}

uint8* COFF::CvGetTagData(int tagIdx, bool ipi, int* outDataSize)
{
	if (tagIdx == 0)
		return NULL;
	if ((ipi) && (tagIdx < mCvIPIMinTag))
		return NULL;
	if ((!ipi) && (tagIdx < mCvMinTag))
		return NULL;

	auto& reader = ipi ? mCvIPIReader : mCvTypeSectionReader;
	int offset = ipi ? mCvIPITagStartMap[tagIdx - mCvIPIMinTag] : mCvTagStartMap[tagIdx - mCvMinTag];

	uint8* data = reader.GetTempPtr(offset, 4);
	uint16 trLength = *(uint16*)data;
	data = reader.GetTempPtr(offset + 2, trLength, true);

	if (outDataSize != NULL)
		*outDataSize = trLength;

	return data;
}

int64 COFF::CvParseConstant(uint16 constVal, uint8*& data)
{
	if (constVal < LF_NUMERIC) // 0x8000
		return constVal;
	switch (constVal)
	{
	case LF_CHAR:
		return GET(int8);
	case LF_SHORT:
		return GET(int16);
	case LF_USHORT:
		return GET(uint16);
	case LF_LONG:
		return GET(int32);
	case LF_ULONG:
		return GET(uint32);
	case LF_QUADWORD:
		return GET(int64);
	case LF_UQUADWORD:
		return (int64)GET(uint64);
	default:
		BF_FATAL("Not handled");
	}
	return 0;
}

int64 COFF::CvParseConstant(uint8*& data)
{
	uint16 val = GET(uint16);
	return CvParseConstant(val, data);
}

const char* COFF::CvParseString(uint8*& data)
{
	const char* strStart = (const char*)data;
	int strLen = strlen((const char*)data);
	if (strLen == 0)
		return NULL;
	data += strLen + 1;
	return strStart;
}

const char* COFF::CvParseAndDupString(uint8*& data)
{
	int strLen = strlen((const char*)data);
	if (strLen == 0)
	{
		data++;
		return NULL;
	}

	BP_ALLOC("CvParseAndDupString", strLen + 1);
	char* dupStr = (char*)mAlloc.AllocBytes(strLen + 1, "CvParseAndDupString");
	memcpy(dupStr, data, strLen);
	data += strLen + 1;
	return dupStr;
}

const char* COFF::CvDupString(const char* str, int strLen)
{
	BP_ALLOC("CvDupString", strLen + 1);
	char* dupStr = (char*)mAlloc.AllocBytes(strLen + 1, "CvParseAndDupString");
	memcpy(dupStr, str, strLen);
	dupStr[strLen] = 0;
	return dupStr;
}

void COFF::CvParseArgList(DbgSubprogram* subprogram, int tagIdx, bool ipi)
{
	uint8* data = CvGetTagData(tagIdx, ipi);
	CvAutoReleaseTempData releaseTempData(this, data);

	int16 trLeafType = GET(int16);

	BF_ASSERT(trLeafType == LF_ARGLIST);

	int argCount = GET(int32);
	for (int argIdx = 0; argIdx < argCount; argIdx++)
	{
		CV_typ_t argTypeId = GET(CV_typ_t);
		DbgType* argType = CvGetType(argTypeId);

		BP_ALLOC_T(DbgVariable);
		DbgVariable* arg = mAlloc.Alloc<DbgVariable>();
		arg->mType = argType;
		arg->mIsParam = true;
		subprogram->mParams.PushBack(arg);
	}

	ReleaseTempBuf(data);
}

DbgSubprogram* COFF::CvParseMethod(DbgType* parentType, const char* methodName, int tagIdx, bool ipi, DbgSubprogram* subprogram)
{
	BP_ZONE("COFF::CvParseMethod");

	uint8* data = CvGetTagData(tagIdx, ipi);
	if (data == NULL)
		return NULL;
	CvAutoReleaseTempData releaseTempData(this, data);

	bool fromTypeSection = methodName != NULL;

	uint8* dataStart = data;
	int16 trLeafType = GET(int16);

	if (trLeafType == LF_FUNC_ID)
	{
		lfFuncId* funcData = (lfFuncId*)dataStart;
		subprogram = CvParseMethod(NULL, (const char*)funcData->name, funcData->type, false, subprogram);
		return subprogram;
	}

	if (trLeafType == LF_MFUNC_ID)
	{
		lfMFuncId* funcData = (lfMFuncId*)dataStart;
		auto parentType = CvGetType(funcData->parentType);
		//subprogram = CvParseMethod(parentType, (const char*)funcData->name, funcData->type, false, subprogram);

		// We shouldn't pass parentType in there, because that would be the declType and not the actual primary type (ie: definition type)
		subprogram = CvParseMethod(NULL, (const char*)funcData->name, funcData->type, false, subprogram);
		return subprogram;
	}

	//DbgSubprogram* subprogram = mAlloc.Alloc<DbgSubprogram>();
	if (subprogram == NULL)
	{
		BP_ALLOC_T(DbgSubprogram);
		subprogram = mAlloc.Alloc<DbgSubprogram>();
	}

	subprogram->mName = methodName;

	int argListId = 0;
	if (trLeafType == LF_MFUNCTION)
	{
		static int gMFuncIdx = 0;
		gMFuncIdx++;

		lfMFunc* funcData = (lfMFunc*)dataStart;
		argListId = funcData->arglist;
		subprogram->mReturnType = CvGetType(funcData->rvtype);

		//TODO:
		if (parentType == NULL)
			subprogram->mParentType = CvGetType(funcData->classtype);

		DbgType* thisType = CvGetType(funcData->thistype);
		if ((thisType != NULL) && (!thisType->IsVoid()))
		{
			BP_ALLOC_T(DbgVariable);
			DbgVariable* arg = mAlloc.Alloc<DbgVariable>();
			arg->mType = thisType;
			arg->mIsParam = true;
			arg->mTagIdx = gMFuncIdx;
			subprogram->mParams.PushBack(arg);

			//TODO: Why did we have this "fromTypeSection" check? It caused non-static methods from S_GPROC32_ID to look like static methods in the autocomplete list
			//if (fromTypeSection)
			{
				arg->mType = thisType;
				subprogram->mHasThis = true;
				arg->mName = "this";
			}
		}
	}
	else if (trLeafType == LF_METHODLIST)
	{
		int32 flags = GET(int32);
		int methodIndex = GET(int32);
		return CvParseMethod(parentType, methodName, methodIndex, ipi, subprogram);
	}
	else if (trLeafType == LF_PROCEDURE)
	{
		lfProc* funcData = (lfProc*)dataStart;
		subprogram->mReturnType = CvGetType(funcData->rvtype);
		argListId = funcData->arglist;
	}
	else if (trLeafType == LF_MFUNC_ID)
	{
		//
	}
	else
	{
		SoftFail(StrFormat("Unhandled func type at tagId %d ipi %d", tagIdx, ipi));
	}

	if ((parentType != NULL) && (!IsObjectFile()))
	{
		subprogram->mCompileUnit = parentType->mCompileUnit;
		parentType->mMethodList.PushBack(subprogram);
	}

	mSubprograms.push_back(subprogram);
	CvParseArgList(subprogram, argListId, ipi);
	return subprogram;
}

void COFF::CvParseMethodList(DbgType* parentType, const char* methodName, int tagIdx, bool ipi)
{
	int dataSize = 0;
	uint8* data = CvGetTagData(tagIdx, ipi, &dataSize);
	CvAutoReleaseTempData releaseTempData(this, data);

	uint8* dataEnd = data + dataSize;
	int16 trLeafType = GET(int16);

	BF_ASSERT(trLeafType == LF_METHODLIST);

	while (data < dataEnd)
	{
		int32 flags = GET(int32);
		int32 methodIndex = GET(int32);
		CvParseMethod(parentType, methodName, methodIndex, ipi);
	}
}

void COFF::CvParseMembers(DbgType* parentType, int tagIdx, bool ipi)
{
	if (tagIdx == NULL)
		return;

	auto& reader = ipi ? mCvIPIReader : mCvTypeSectionReader;
	int offset = ipi ? mCvIPITagStartMap[tagIdx - mCvIPIMinTag] : mCvTagStartMap[tagIdx - mCvMinTag];

	uint8* data = reader.GetTempPtr(offset, 4);
	uint16 trLength = *(uint16*)data;
	offset += 2;
	data = reader.GetTempPtr(offset, trLength, true);
	CvAutoReleaseTempData releaseTempData(this, data);

	uint8* dataStart = data;
	uint8* dataEnd = data + trLength;
	int16 trLeafType = GET(int16);
	bool strMadeCopy;

	auto _ParseString = [&]()
	{
		strMadeCopy = false;
		int nameLen = strlen((const char*)data);
		int nameOfs = data - dataStart;
		const char* name = (const char*)reader.GetPermanentPtr(offset + nameOfs, nameLen + 1, &strMadeCopy);
		data += nameLen + 1;
		return name;
	};

	if (trLeafType == 0)
		return;
	switch (trLeafType)
	{
	case LF_FIELDLIST:
		{
			uint8* sectionStart = data;

			while (data < dataEnd)
			{
				uint8* leafDataStart = data;
				int leafType = (int)GET(uint16);
				switch (leafType)
				{
				case LF_VFUNCTAB:
					{
						lfVFuncTab& vfuncTab = *(lfVFuncTab*)leafDataStart;

						DbgType* vtableType = CvGetType(vfuncTab.type);

						BP_ALLOC_T(DbgVariable);
						DbgVariable* vtableMember = mAlloc.Alloc<DbgVariable>();
						vtableMember->mType = vtableType;
						parentType->mMemberList.PushFront(vtableMember);

						data = (uint8*)(&vfuncTab + 1);
					}
					break;
				case LF_BCLASS:
					{
						lfBClass& baseClassInfo = *(lfBClass*)leafDataStart;

						BP_ALLOC_T(DbgBaseTypeEntry);
						DbgBaseTypeEntry* baseTypeEntry = mAlloc.Alloc<DbgBaseTypeEntry>();
						data = (uint8*)&baseClassInfo.offset;
						baseTypeEntry->mThisOffset = (int)CvParseConstant(data);
						if (baseClassInfo.index != 0)
						{
							baseTypeEntry->mBaseType = CvGetType(baseClassInfo.index);

// 							if (parentType->mLanguage == DbgLanguage_Beef)
// 							{
// 								if (!parentType->mBaseTypes.IsEmpty())
// 									parentType->mTypeParam = baseTypeEntry->mBaseType;
// 							}

							parentType->mBaseTypes.PushBack(baseTypeEntry);
							parentType->mAlign = std::max(parentType->mAlign, baseTypeEntry->mBaseType->GetAlign());

							if (!parentType->mSizeCalculated)
							{
								if ((baseTypeEntry->mBaseType->GetByteCount() == 0) && (baseTypeEntry->mBaseType->IsBfObject()))
								{
									parentType->mExtType = DbgExtType_Interface;
								}

								parentType->mSize = BF_MAX(parentType->mSize, baseTypeEntry->mBaseType->GetByteCount());
							}
						}
					}
					break;
				case LF_VBCLASS:
				case LF_IVBCLASS:
					{
						lfVBClass& baseClassInfo = *(lfVBClass*)leafDataStart;

						BP_ALLOC_T(DbgBaseTypeEntry);
						DbgBaseTypeEntry* baseTypeEntry = mAlloc.Alloc<DbgBaseTypeEntry>();
						baseTypeEntry->mBaseType = CvGetType(baseClassInfo.index);

						data = (uint8*)&baseClassInfo.vbpoff;
						baseTypeEntry->mThisOffset = (int)CvParseConstant(data);
						baseTypeEntry->mVTableOffset = (int)CvParseConstant(data);
						parentType->mBaseTypes.PushBack(baseTypeEntry);
					}
					break;
				case LF_ENUMERATE:
					{
						CV_fldattr_t fieldAttr = GET(CV_fldattr_t);
						int64 fieldVal = CvParseConstant(data);

						const char* fieldName = _ParseString();

						BP_ALLOC_T(DbgVariable);
						DbgVariable* member = mAlloc.Alloc<DbgVariable>();
						member->mCompileUnit = parentType->mCompileUnit;
						member->mConstValue = fieldVal;
						member->mName = fieldName;
						member->mIsStatic = true;
						member->mIsConst = true;
						member->mType = parentType->mTypeParam;
						FixConstant(member);

						parentType->mMemberList.PushBack(member);
					}
					break;
				case LF_NESTTYPE:
					{
						// Resolve nested types at end through 'primary type'
						//  Nested types also include nested TypeDefs, so the embedded type index may even be a primitive type here

						// Why did we have this check to avoid unnamed nested types?
						//  it seems we added this and removed it a couple times.
						//  Disallowing NULL nested types here breaks the "_BUF_SIZE" in String, which is
						//  an anonymous enum
						/*int16 pad = GET(int16);
						int32 nestedTypeId = GET(int32);
						const char* typeName = CvParseAndDupString(data);
						if ((typeName != NULL) && (typeName[0] != '<'))
						{
							DbgType* nestedType = CvCreateType();
							nestedType->mTypeParam = CvGetType(nestedTypeId);
							nestedType->mTypeCode = DbgType_TypeDef;
							nestedType->mName = typeName;
							nestedType->mTypeName = typeName;
							nestedType->mParent = parentType;
							parentType->mSubTypeList.PushBack(nestedType);
						}*/

						int16 pad = GET(int16);
						int32 nestedTypeId = GET(int32);

						const char* typeName = _ParseString();

						DbgType* nestedType = CvCreateType();
						nestedType->mTypeParam = CvGetType(nestedTypeId);
						nestedType->mTypeCode = DbgType_TypeDef;
						if ((typeName != NULL) && (typeName[0] != '<'))
						{
							nestedType->mName = typeName;
							nestedType->mTypeName = typeName;
						}
						nestedType->mParent = parentType;
						parentType->mSubTypeList.PushBack(nestedType);
					}
					break;
				case LF_ONEMETHOD:
					{
						CV_fldattr_t attr = GET(CV_fldattr_t);
						CV_typ_t methodTypeId = GET(CV_typ_t);

						int virtOffset = -1;
						if ((attr.mprop == CV_MTintro) || (attr.mprop == CV_MTpureintro))
							virtOffset = GET(int32);
						const char* methodName = _ParseString();
						DbgSubprogram* subProgram = CvParseMethod(parentType, methodName, methodTypeId, ipi);
						subProgram->mVirtual = (attr.mprop == CV_MTintro) || (attr.mprop == CV_MTpureintro) || (attr.mprop == CV_MTvirtual);
						subProgram->mVTableLoc = virtOffset;
					}
					break;
				case LF_METHOD:
					{
						int count = (int)GET(uint16);
						int32 methodList = GET(int32);
						const char* methodName = _ParseString();

						uint8* listData = CvGetTagData(methodList, ipi);
						CvAutoReleaseTempData releaseTempData(this, listData);

						int16 trLeafType = GET_FROM(listData, int16);
						if (trLeafType != LF_METHODLIST)
						{
							SoftFail("Invalid LF_METHOD member");
							return;
						}

						BF_ASSERT(trLeafType == LF_METHODLIST);

						for (int methodIdx = 0; methodIdx < count; methodIdx++)
						{
							CV_fldattr_t attr = GET_FROM(listData, CV_fldattr_t);
							int16 unused = GET_FROM(listData, int16);
							CV_typ_t methodTypeId = GET_FROM(listData, CV_typ_t);
							int virtOffset = -1;
							if ((attr.mprop == CV_MTintro) || (attr.mprop == CV_MTpureintro))
								virtOffset = GET_FROM(listData, int32);

							DbgSubprogram* subProgram = CvParseMethod(parentType, methodName, methodTypeId, ipi);
							subProgram->mVirtual = (attr.mprop == CV_MTintro) || (attr.mprop == CV_MTpureintro) || (attr.mprop == CV_MTvirtual);
							subProgram->mVTableLoc = virtOffset;
						}
					}
					break;
				case LF_MEMBER:
				case LF_STMEMBER:
					{
						bool isStatic = leafType == LF_STMEMBER;
						bool isConst = false;
						CV_fldattr_t attr = GET(CV_fldattr_t);
						CV_typ_t fieldTypeId = GET(CV_typ_t);

						if (parentType->mTagIdx == 29184)
						{
							NOP;
						}

						int memberOffset = -1;
						if (isStatic)
						{
							//?
						}
						else
						{
							memberOffset = (int)CvParseConstant(data);
						}
						char* fieldName = (char*)_ParseString();

						if ((fieldName != NULL) && (parentType->mLanguage == DbgLanguage_Beef))
						{
							if (strcmp(fieldName, "$prim") == 0)
							{
								parentType->mTypeParam = CvGetType(fieldTypeId);
								parentType->mTypeParam = GetPrimitiveType(parentType->mTypeParam->mTypeCode, DbgLanguage_Beef);

								parentType->mSize = parentType->mTypeParam->mSize;
								parentType->mAlign = parentType->mTypeParam->mAlign;
								if ((parentType->mBaseTypes.mHead != NULL) && (strcmp(parentType->mBaseTypes.mHead->mBaseType->mName, "System.Enum") == 0))
									parentType->mTypeCode = DbgType_Enum;
								break;
							}

							if (strncmp(fieldName, "$using$", 7) == 0)
							{
								fieldName = NULL;
							}
						}

						int64 constVal = 0;
						if ((fieldName != NULL) && (parentType->mLanguage == DbgLanguage_Beef) && (isStatic))
						{
							for (char* cPtr = fieldName; true; cPtr++)
							{
								char c = *cPtr;
								if (c == 0)
									break;
								if (c == '$')
								{
									if (!strMadeCopy)
									{
										int ofs = cPtr - fieldName;
										fieldName = DbgDupString(fieldName, "CvParseMembers.LF_MEMBER");
										cPtr = fieldName + ofs;
									}

									isConst = true;
									if (cPtr[1] == '_')
									{
										cPtr[1] = '-';
										constVal = atoll(cPtr + 1);
									}
									else
										constVal = atoll(cPtr + 1);
									*cPtr = 0;
									break;
								}
							}
						}

						if ((isStatic) && (!isConst) && (IsObjectFile()))
						{
							// Already has statics filled in
							break;
						}

						BP_ALLOC_T(DbgVariable);
						DbgVariable* member = mAlloc.Alloc<DbgVariable>();
						member->mIsStatic = isStatic;

						DbgType* fieldType = CvGetType(fieldTypeId);
// 						if (fieldType == NULL)
// 						{
// 							uint8* fieldTypeData = CvGetTagData(fieldTypeId, ipi);
// 							CvAutoReleaseTempData releaseTempData(this, fieldTypeData);
//
// 							// It's a section data ptr
// 							int16 memberLeafType = *((int16*)fieldTypeData);
// 							switch (memberLeafType)
// 							{
// 							case LF_BITFIELD:
// 								{
// 									lfBitfield& bitfield = *(lfBitfield*)fieldTypeData;
// 									fieldType = CvGetType(bitfield.type);
//
// 									// Bit offset is expressed in MSB form
// 									member->mBitOffset = (fieldType->mSize * 8) - bitfield.position - bitfield.length;
// 									member->mBitSize = bitfield.length;
// 								}
// 								break;
// 							default:
// 								BF_FATAL("Unhandled");
// 							}
// 						}

						if (fieldType == NULL)
							fieldType = CvGetType(fieldTypeId);

						if ((fieldType->mTypeCode == DbgType_Enum) && (fieldType->GetByteCount() == 0))
							fieldType = fieldType->GetPrimaryType();

// 						if (fieldType->mTypeCode == DbgType_Bitfield)
// 						{
// 							auto bitfieldType = (DbgBitfieldType*)fieldType;
// 							member->mBitOffset = bitfieldType->mPosition;
// 							member->mBitSize = bitfieldType->mLength;
// 							fieldType = fieldType->mTypeParam;
// 						}

						if (isConst)
						{
							member->mConstValue = constVal;
							member->mIsConst = true;
						}
						else if (isStatic)
						{
							// NOP
						}
						else
							member->mMemberOffset = memberOffset;
						member->mType = fieldType;
						member->mCompileUnit = parentType->mCompileUnit;
						member->mName = fieldName;

						// Size should already be set, right?  It gets set on 'dataSize' in the LF_STRUCT/LF_CLASS
						//parentType->mSize = std::max(memberOffset + fieldType->mSize, parentType->mSize);
						if ((!isStatic) && (member->mMemberOffset >= 0))
						{
							if (!parentType->mIsPacked)
								parentType->mAlign = std::max(parentType->mAlign, fieldType->GetAlign());

							if (!parentType->mSizeCalculated)
							{
								parentType->mSize = std::max(memberOffset + fieldType->GetByteCount(), parentType->mSize);
							}
						}

						//if (parentType->mAlign > 1)
							//parentType->mSize = (parentType->mSize + (parentType->mAlign - 1)) & ~(parentType->mAlign - 1);
						//parentType->mSizeCalculated = true;

						parentType->mMemberList.PushBack(member);

						if (isStatic)
							parentType->mNeedsGlobalsPopulated = true;
					}
					break;
				case LF_INDEX:
					{
						int _pad = (int)GET(uint16);
						int32 indexType = GET(int32);
						CvParseMembers(parentType, indexType, ipi);
					}
					break;
				default:
					SoftFail(StrFormat("Unhandled leaf id 0x%X", leafType));
					return;
				}

				PTR_ALIGN(data, sectionStart, 4);
			}
		}
	break;
	}
}

int COFF::CvConvRegNum(int regNum, int* outBits)
{
	if ((regNum >= CV_AMD64_R8B) && (regNum <= CV_AMD64_R15B))
	{
		if (outBits != NULL)
			*outBits = 8;
		regNum -= CV_AMD64_R8B - CV_AMD64_R8;
	}
	else if ((regNum >= CV_AMD64_R8W) && (regNum <= CV_AMD64_R15W))
	{
		if (outBits != NULL)
			*outBits = 16;
		regNum -= CV_AMD64_R8W - CV_AMD64_R8;
	}
	else if ((regNum >= CV_AMD64_R8D) && (regNum <= CV_AMD64_R15D))
	{
		if (outBits != NULL)
			*outBits = 32;
		regNum -= CV_AMD64_R8D - CV_AMD64_R8;
	}

	switch (regNum)
	{
#ifdef BF_DBG_64
	case CV_AMD64_RAX: return X64Reg_RAX;
	case CV_AMD64_RDX: return X64Reg_RDX;
	case CV_AMD64_RCX: return X64Reg_RCX;
	case CV_AMD64_RBX: return X64Reg_RBX;
	case CV_AMD64_RSI: return X64Reg_RSI;
	case CV_AMD64_RDI: return X64Reg_RDI;
	case CV_AMD64_RBP: return X64Reg_RBP;
	case CV_AMD64_RSP: return X64Reg_RSP;
	case CV_AMD64_R8: return X64Reg_R8;
	case CV_AMD64_R9: return X64Reg_R9;
	case CV_AMD64_R10: return X64Reg_R10;
	case CV_AMD64_R11: return X64Reg_R11;
	case CV_AMD64_R12: return X64Reg_R12;
	case CV_AMD64_R13: return X64Reg_R13;
	case CV_AMD64_R14: return X64Reg_R14;
	case CV_AMD64_R15: return X64Reg_R15;
	case CV_AMD64_RIP: return X64Reg_RIP;

	case CV_REG_AL: if (outBits != NULL) *outBits = 8; return X64Reg_RAX;
	case CV_REG_AX: if (outBits != NULL) *outBits = 16; return X64Reg_RAX;
	case CV_REG_EAX: if (outBits != NULL) *outBits = 32; return X64Reg_RAX;
	case CV_REG_CL: if (outBits != NULL) *outBits = 8; return X64Reg_RCX;
	case CV_REG_CX: if (outBits != NULL) *outBits = 16; return X64Reg_RCX;
	case CV_REG_ECX: if (outBits != NULL) *outBits = 32; return X64Reg_RCX;
	case CV_REG_DL: if (outBits != NULL) *outBits = 8; return X64Reg_RDX;
	case CV_REG_DX: if (outBits != NULL) *outBits = 16; return X64Reg_RDX;
	case CV_REG_EDX: if (outBits != NULL) *outBits = 32; return X64Reg_RDX;
	case CV_REG_BL: if (outBits != NULL) *outBits = 8; return X64Reg_RBX;
	case CV_REG_BX: if (outBits != NULL) *outBits = 16; return X64Reg_RBX;
	case CV_REG_EBX: if (outBits != NULL) *outBits = 32; return X64Reg_RBX;
	case CV_REG_ESP: if (outBits != NULL) *outBits = 32; return X64Reg_RSP;
	case CV_REG_EBP: if (outBits != NULL) *outBits = 32; return X64Reg_RBP;
	case CV_AMD64_SIL: if (outBits != NULL) *outBits = 8; return X64Reg_RSI;
	case CV_REG_SI: if (outBits != NULL) *outBits = 16; return X64Reg_RSI;
	case CV_REG_ESI: if (outBits != NULL) *outBits = 32; return X64Reg_RSI;
	case CV_AMD64_DIL: if (outBits != NULL) *outBits = 8; return X64Reg_RDI;
	case CV_REG_DI: if (outBits != NULL) *outBits = 16; return X64Reg_RDI;
	case CV_REG_EDI: if (outBits != NULL) *outBits = 32; return X64Reg_RDI;

	case CV_AMD64_R8B: *outBits = 8; return X64Reg_R8;
	case CV_AMD64_R8W: *outBits = 16; return X64Reg_R8;
	case CV_AMD64_R8D: *outBits = 32; return X64Reg_R8;
	case CV_AMD64_R9B: *outBits = 8; return X64Reg_R9;
	case CV_AMD64_R9W: *outBits = 16; return X64Reg_R9;
	case CV_AMD64_R9D: *outBits = 32; return X64Reg_R9;
	case CV_AMD64_R10B: *outBits = 8; return X64Reg_R10;
	case CV_AMD64_R10W: *outBits = 16; return X64Reg_R10;
	case CV_AMD64_R10D: *outBits = 32; return X64Reg_R10;
	case CV_AMD64_R11B: *outBits = 8; return X64Reg_R11;
	case CV_AMD64_R11W: *outBits = 16; return X64Reg_R11;
	case CV_AMD64_R11D: *outBits = 32; return X64Reg_R11;
	case CV_AMD64_R12B: *outBits = 8; return X64Reg_R12;
	case CV_AMD64_R12W: *outBits = 16; return X64Reg_R12;
	case CV_AMD64_R12D: *outBits = 32; return X64Reg_R12;
	case CV_AMD64_R13B: *outBits = 8; return X64Reg_R13;
	case CV_AMD64_R13W: *outBits = 16; return X64Reg_R13;
	case CV_AMD64_R13D: *outBits = 32; return X64Reg_R13;
	case CV_AMD64_R14B: *outBits = 8; return X64Reg_R14;
	case CV_AMD64_R14W: *outBits = 16; return X64Reg_R14;
	case CV_AMD64_R14D: *outBits = 32; return X64Reg_R14;
	case CV_AMD64_R15B: *outBits = 8; return X64Reg_R15;
	case CV_AMD64_R15W: *outBits = 16; return X64Reg_R15;
	case CV_AMD64_R15D: *outBits = 32; return X64Reg_R15;

	case CV_AMD64_XMM0_0: return X64Reg_XMM00;
	case CV_AMD64_XMM0_1: return X64Reg_XMM01;
	case CV_AMD64_XMM0_2: return X64Reg_XMM02;
	case CV_AMD64_XMM0_3: return X64Reg_XMM03;
	case CV_AMD64_XMM1_0: return X64Reg_XMM10;
	case CV_AMD64_XMM1_1: return X64Reg_XMM11;
	case CV_AMD64_XMM1_2: return X64Reg_XMM12;
	case CV_AMD64_XMM1_3: return X64Reg_XMM13;
	case CV_AMD64_XMM2_0: return X64Reg_XMM20;
	case CV_AMD64_XMM2_1: return X64Reg_XMM21;
	case CV_AMD64_XMM2_2: return X64Reg_XMM22;
	case CV_AMD64_XMM2_3: return X64Reg_XMM23;
	case CV_AMD64_XMM3_0: return X64Reg_XMM30;
	case CV_AMD64_XMM3_1: return X64Reg_XMM31;
	case CV_AMD64_XMM3_2: return X64Reg_XMM32;
	case CV_AMD64_XMM3_3: return X64Reg_XMM33;
	case CV_AMD64_XMM4_0: return X64Reg_XMM40;
	case CV_AMD64_XMM4_1: return X64Reg_XMM41;
	case CV_AMD64_XMM4_2: return X64Reg_XMM42;
	case CV_AMD64_XMM4_3: return X64Reg_XMM43;
	case CV_AMD64_XMM5_0: return X64Reg_XMM50;
	case CV_AMD64_XMM5_1: return X64Reg_XMM51;
	case CV_AMD64_XMM5_2: return X64Reg_XMM52;
	case CV_AMD64_XMM5_3: return X64Reg_XMM53;
	case CV_AMD64_XMM6_0: return X64Reg_XMM60;
	case CV_AMD64_XMM6_1: return X64Reg_XMM61;
	case CV_AMD64_XMM6_2: return X64Reg_XMM62;
	case CV_AMD64_XMM6_3: return X64Reg_XMM63;
	case CV_AMD64_XMM7_0: return X64Reg_XMM70;
	case CV_AMD64_XMM7_1: return X64Reg_XMM71;
	case CV_AMD64_XMM7_2: return X64Reg_XMM72;
	case CV_AMD64_XMM7_3: return X64Reg_XMM73;
	case CV_AMD64_XMM8_0: return X64Reg_XMM80;
	case CV_AMD64_XMM8_1: return X64Reg_XMM81;
	case CV_AMD64_XMM8_2: return X64Reg_XMM82;
	case CV_AMD64_XMM8_3: return X64Reg_XMM83;
	case CV_AMD64_XMM9_0: return X64Reg_XMM90;
	case CV_AMD64_XMM9_1: return X64Reg_XMM91;
	case CV_AMD64_XMM9_2: return X64Reg_XMM92;
	case CV_AMD64_XMM9_3: return X64Reg_XMM93;
	case CV_AMD64_XMM10_0: return X64Reg_XMM10_0;
	case CV_AMD64_XMM10_1: return X64Reg_XMM10_1;
	case CV_AMD64_XMM10_2: return X64Reg_XMM10_2;
	case CV_AMD64_XMM10_3: return X64Reg_XMM10_3;
	case CV_AMD64_XMM11_0: return X64Reg_XMM11_0;
	case CV_AMD64_XMM11_1: return X64Reg_XMM11_1;
	case CV_AMD64_XMM11_2: return X64Reg_XMM11_2;
	case CV_AMD64_XMM11_3: return X64Reg_XMM11_3;
	case CV_AMD64_XMM12_0: return X64Reg_XMM12_0;
	case CV_AMD64_XMM12_1: return X64Reg_XMM12_1;
	case CV_AMD64_XMM12_2: return X64Reg_XMM12_2;
	case CV_AMD64_XMM12_3: return X64Reg_XMM12_3;
	case CV_AMD64_XMM13_0: return X64Reg_XMM13_0;
	case CV_AMD64_XMM13_1: return X64Reg_XMM13_1;
	case CV_AMD64_XMM13_2: return X64Reg_XMM13_2;
	case CV_AMD64_XMM13_3: return X64Reg_XMM13_3;
	case CV_AMD64_XMM14_0: return X64Reg_XMM14_0;
	case CV_AMD64_XMM14_1: return X64Reg_XMM14_1;
	case CV_AMD64_XMM14_2: return X64Reg_XMM14_2;
	case CV_AMD64_XMM14_3: return X64Reg_XMM14_3;
	case CV_AMD64_XMM15_0: return X64Reg_XMM15_0;
	case CV_AMD64_XMM15_1: return X64Reg_XMM15_1;
	case CV_AMD64_XMM15_2: return X64Reg_XMM15_2;
	case CV_AMD64_XMM15_3: return X64Reg_XMM15_3;
#else
	case CV_REG_AL: return X86Reg_EAX;
	case CV_REG_CL: return X86Reg_ECX;
	case CV_REG_DL: return X86Reg_EDX;
	case CV_REG_BL: return X86Reg_EBX;
	case CV_REG_AH: return X86Reg_EAX;
	case CV_REG_AX: return X86Reg_EAX;
	case CV_REG_CX: return X86Reg_ECX;
	case CV_REG_DX: return X86Reg_EDX;
	case CV_REG_BX: return X86Reg_EBX;
	case CV_REG_SP: return X86Reg_ESP;
	case CV_REG_BP: return X86Reg_EBP;
	case CV_REG_SI: return X86Reg_ESI;
	case CV_REG_DI: return X86Reg_EDI;
	case CV_REG_EAX: return X86Reg_EAX;
	case CV_REG_ECX: return X86Reg_ECX;
	case CV_REG_EDX: return X86Reg_EDX;
	case CV_REG_EBX: return X86Reg_EBX;
	case CV_REG_ESP: return X86Reg_ESP;
	case CV_REG_EBP: return X86Reg_EBP;
	case CV_REG_ESI: return X86Reg_ESI;
	case CV_REG_EDI: return X86Reg_EDI;
	case CV_REG_IP: return X86Reg_EIP;
	// 	case CV_REG_XMM0: return X86Reg_XMM00;
	// 	case CV_REG_XMM1: return X86Reg_XMM01;
	// 	case CV_REG_XMM2: return X86Reg_XMM02;
	// 	case CV_REG_XMM3: return X86Reg_XMM03;
	// 	case CV_REG_XMM4: return X86Reg_XMM04;
	// 	case CV_REG_XMM5: return X86Reg_XMM05;
	// 	case CV_REG_XMM6: return X86Reg_XMM06;
	// 	case CV_REG_XMM7: return X86Reg_XMM07;
#endif
	}

	return 0; // Nope

	BF_FATAL("Invalid register");
	return 0;
}

DbgType* COFF::CvCreateType()
{
	DbgModule* linkedModule = GetLinkedModule();
	BP_ALLOC_T(DbgType);
	DbgType* dbgType = mAlloc.Alloc<DbgType>();
	dbgType->mCompileUnit = mMasterCompileUnit;
	dbgType->mTypeIdx = (int)linkedModule->mTypes.size();
	linkedModule->mTypes.push_back(dbgType);

	return dbgType;
}

DbgType* COFF::CvParseType(int tagIdx, bool ipi)
{
	auto& reader = ipi ? mCvIPIReader : mCvTypeSectionReader;
	int offset = ipi ? mCvIPITagStartMap[tagIdx - mCvIPIMinTag] : mCvTagStartMap[tagIdx - mCvMinTag];

	uint8* data = reader.GetTempPtr(offset, 4);
	uint16 trLength = *(uint16*)data;
	offset += 2;
	data = reader.GetTempPtr(offset, trLength, true);
	CvAutoReleaseTempData releaseTempData(this, data);

	uint8* dataStart = data;
	uint8* dataEnd = data + trLength;
	int16 trLeafType = GET(int16);
	bool strMadeCopy;

	auto _ParseString = [&]()
	{
		strMadeCopy = false;
		int nameLen = strlen((const char*)data);
		int nameOfs = data - dataStart;
		const char* name = (const char*)reader.GetPermanentPtr(offset + nameOfs, nameLen + 1, &strMadeCopy);
		data += nameLen + 1;
		return name;
	};

	DbgType* dbgType = NULL;

	switch (trLeafType)
	{
	case LF_ENUM:
		{
			int elemCount = (int)GET(uint16);
			int prop = (int)GET(uint16);
			int underlyingType = GET(int32);
			int typeIndex = GET(int32);
			const char* name = _ParseString();

			dbgType = CvCreateType();
			dbgType->mCompileUnit = mMasterCompileUnit;
			dbgType->mName = name;
			dbgType->mTypeName = name;
			//SplitName(dbgType->mName, dbgType->mTypeName, dbgType->mTemplateParams);
			dbgType->mTypeCode = DbgType_Enum;
			dbgType->mTypeParam = CvGetTypeSafe(underlyingType);
			dbgType->mIsIncomplete = true;

			if (dbgType->mTypeParam->GetByteCount() == 0)
				dbgType->mIsDeclaration = true;

			dbgType->mSize = dbgType->mTypeParam->mSize;
			dbgType->mAlign = dbgType->mTypeParam->mAlign;

			/*if (dbgType->mTypeParam->GetByteCount() == 0)
			{
				// Appears to be a bug where byte-sized enums come out as void (sometimes?)
				dbgType->mTypeParam = mBfPrimitiveTypes[DbgType_u8];
			}*/

			//dbgType->mIsDeclaration = typeIndex == 0;

			//CvParseMembers(dbgType, typeIndex, sectionData);
		}
		break;
	case LF_VTSHAPE:
		{
			lfVTShape& vtShape = *(lfVTShape*)dataStart;

			dbgType = CvCreateType();
			dbgType->mTypeCode = DbgType_VTable;
			dbgType->mSize = vtShape.count * sizeof(addr_target);
			dbgType->mSizeCalculated = true;
		}
		break;
	case LF_BITFIELD:
		{
			//DbgType
			lfBitfield& bitfield = *(lfBitfield*)dataStart;
			//dbgType = CvCreateType();

			DbgBitfieldType* bitfieldType = mAlloc.Alloc<DbgBitfieldType>();
			dbgType = bitfieldType;
			dbgType->mTypeParam = CvGetTypeSafe(bitfield.type);
			dbgType->mTypeCode = DbgType_Bitfield;
			//bitfieldType->mPosition = (dbgType->mTypeParam->mSize * 8) - bitfield.position - bitfield.length;
			bitfieldType->mPosition = bitfield.position;
			bitfieldType->mLength = bitfield.length;
			bitfieldType->mSize = dbgType->mTypeParam->mSize;
			bitfieldType->mAlign = dbgType->mTypeParam->mAlign;
			dbgType->mSizeCalculated = true;
			dbgType->mPriority = DbgTypePriority_Unique;
		}
		break;

	case LF_CLASS:
	case LF_STRUCTURE:
	case LF_CLASS_EX:
	case LF_STRUCTURE_EX:
		{
			unsigned short  count;          // count of number of elements in class
			CV_prop_t       property;       // property attribute field (prop_t)
			CV_typ_t        field;          // type index of LF_FIELD descriptor list
			CV_typ_t        derived;        // type index of derived from list if not zero
			CV_typ_t        vshape;         // type index of vshape table for this class
			int dataSize;

			int16 extra = 0;
			if ((trLeafType == 0x1608) || (trLeafType == 0x1609))
			{
				property = GET(CV_prop_t);
				extra = GET(int16);
				field = GET(CV_typ_t);
				derived = GET(CV_typ_t);
				vshape = GET(CV_typ_t);
				count = GET(unsigned short);
				dataSize = (int)CvParseConstant(data);
			}
			else
			{
				count = GET(unsigned short);
				property = GET(CV_prop_t);
				field = GET(CV_typ_t);
				derived = GET(CV_typ_t);
				vshape = GET(CV_typ_t);
				dataSize = (int)CvParseConstant(data);
			}

			const char* name = _ParseString();

// 			if (strstr(name, "TestStruct") != NULL)
// 			{
// 				NOP;
// 			}

// 			if ((strstr(name, "`") != NULL) || (strstr(name, "::__l") != NULL))
// 			{
// 				OutputDebugStrF("Local type: %s\n", name);
// 			}

			// Remove "enum " from type names

			bool isPartialDef = false;

			for (int i = 0; true; i++)
			{
				char c = name[i];

				if (c == 0)
					break;

				if (c == '$')
				{
					if ((i >= 5) && (strncmp(&name[i - 5], "$part$", 6) == 0))
					{
						if (!strMadeCopy)
							name = DbgDupString(name, "CvParseType.LF_CLASS");
						strcpy((char*)&name[i - 5], &name[i + 1]);
						if (name[i - 5] == '\0')
						{
							isPartialDef = true;
							break;
						}
					}
				}

				if ((c == ' ') && (i >= 5))
				{
					if ((name[i - 4] == 'e') &&
						(name[i - 3] == 'n') &&
						(name[i - 2] == 'u') &&
						(name[i - 1] == 'm'))
					{
						char prevC = name[i - 5];
						if ((!isalnum(prevC)) && (prevC != '_'))
						{
							if (!strMadeCopy)
								name = DbgDupString(name, "CvParseType.LF_CLASS");

							memmove((char*)name + i - 4, name + i + 1, strlen(name + i + 1) + 1);
							i -= 5;
							continue;
						}
					}
				}
			}

			dbgType = CvCreateType();
			dbgType->mCompileUnit = mMasterCompileUnit;
			dbgType->mIsDeclaration = property.fwdref;
			if (isPartialDef)
				dbgType->mIsDeclaration = true;
			dbgType->mName = name;
			dbgType->mTypeName = name;
			//SplitName(dbgType->mName, dbgType->mTypeName, dbgType->mTemplateParams);
			if ((trLeafType == LF_CLASS) || (trLeafType == LF_CLASS_EX))
				dbgType->mTypeCode = DbgType_Class;
			else
				dbgType->mTypeCode = DbgType_Struct;

			DbgType* baseType = NULL;
			if (derived != 0)
			{
				baseType = CvGetTypeSafe(derived);
				BP_ALLOC_T(DbgBaseTypeEntry);
				DbgBaseTypeEntry* baseTypeEntry = mAlloc.Alloc<DbgBaseTypeEntry>();
				baseTypeEntry->mBaseType = baseType;
				dbgType->mBaseTypes.PushBack(baseTypeEntry);
			}

			if (property.packed)
			{
				dbgType->mAlign = 1;
				dbgType->mIsPacked = true;
			}

			if (!dbgType->mIsDeclaration)
			{
				dbgType->mSizeCalculated = true;
				dbgType->mSize = dataSize;

				if (strncmp(name, "_bf:", 4) == 0)
				{
					// Beef types have different strides from size. Note we MUST set size to zero here, because there are
					// some BF_MAX calculations we perform on it
					dbgType->mSizeCalculated = false;
					dbgType->mSize = 0;
				}
			}

			if (vshape != 0)
			{
				CvGetTypeSafe(vshape);
				dbgType->mHasVTable = true;
			}

			dbgType->mIsIncomplete = true;

// 			if (classInfo.field != 0)
// 				dbgType->mDefinedMembersSize = CvGetTagSize(classInfo.field, ipi);

			//CvParseMembers(dbgType, classInfo.field, sectionData);
		}
		break;
	case LF_UNION:
		{
			lfUnion& classInfo = *(lfUnion*)dataStart;
			data = (uint8*)&classInfo.data;
			int dataSize = (int)CvParseConstant(data);
			const char* name = _ParseString();

			dbgType = CvCreateType();
			dbgType->mCompileUnit = mMasterCompileUnit;
			dbgType->mIsDeclaration = classInfo.property.fwdref;
			dbgType->mName = name;
			dbgType->mTypeName = name;
			//SplitName(dbgType->mName, dbgType->mTypeName, dbgType->mTemplateParams);
			dbgType->mTypeCode = DbgType_Union;
			dbgType->mSize = dataSize;
			dbgType->mAlign = 1;
			dbgType->mSizeCalculated = !dbgType->mIsDeclaration;

			dbgType->mIsIncomplete = true;
			//CvParseMembers(dbgType, classInfo.field, sectionData);
		}
		break;

	case LF_MODIFIER:
		{
			lfModifier& modifier = *(lfModifier*)dataStart;
			DbgType* outerType = CvGetTypeSafe(modifier.type);
			dbgType = outerType;

			if (modifier.attr.MOD_const)
			{
				DbgType* innerType = dbgType;
				dbgType = CvCreateType();
				dbgType->mTypeParam = innerType;
				dbgType->mTypeCode = DbgType_Const;
				dbgType->mLanguage = innerType->mLanguage;
			}

			if (modifier.attr.MOD_volatile)
			{
				DbgType* innerType = dbgType;
				dbgType = CvCreateType();
				dbgType->mTypeParam = innerType;
				dbgType->mTypeCode = DbgType_Volatile;
				dbgType->mLanguage = innerType->mLanguage;
			}

			if (modifier.attr.MOD_unaligned)
			{
				DbgType* innerType = dbgType;
				dbgType = CvCreateType();
				dbgType->mTypeParam = innerType;
				dbgType->mTypeCode = DbgType_Unaligned;
				dbgType->mLanguage = innerType->mLanguage;
			}
		}
		break;
	case LF_POINTER:
		{
			lfPointer* pointerInfo = (lfPointer*)dataStart;

			dbgType = CvCreateType();
			dbgType->mTypeParam = CvGetTypeSafe(pointerInfo->utype);
			if (pointerInfo->attr.ptrmode == CV_PTR_MODE_RVREF)
				dbgType->mTypeCode = DbgType_RValueReference;
			else if (pointerInfo->attr.ptrmode == CV_PTR_MODE_LVREF)
				dbgType->mTypeCode = DbgType_Ref;
			else
			{
				dbgType->mTypeCode = DbgType_Ptr;
				if ((dbgType->mTypeParam != NULL) && (dbgType->mTypeParam->mPtrType == NULL))
					dbgType->mTypeParam->mPtrType = dbgType;
			}
			//dbgType->mSize = pointerInfo->attr.size;
			dbgType->mSize = sizeof(addr_target);
			dbgType->mAlign = dbgType->mSize;
			dbgType->mSizeCalculated = true;
			dbgType->mLanguage = dbgType->mTypeParam->mLanguage;

			/*if (prop.isconst)
			{
			DbgType* innerType = dbgType;
			dbgType = CvCreateType();
			dbgType->mTypeParam = innerType;
			dbgType->mTypeCode = DbgType_Const;
			dbgType->mSize = innerType->mSize;
			}*/

			//TODO: Handle const, volatile, ref, restrict, etc
		}
		break;
	case LF_DIMARRAY:
		{
		}
		break;
	case LF_ARRAY:
		{
			lfArray* array = (lfArray*)dataStart;

			DbgType* indexType = CvGetTypeSafe(array->idxtype);

			dbgType = CvCreateType();
			dbgType->mTypeParam = CvGetTypeSafe(array->elemtype);
			dbgType->mTypeCode = DbgType_SizedArray;
			dbgType->mLanguage = dbgType->mTypeParam->mLanguage;
			data = (uint8*)&array->data;

			int size = (int)CvParseConstant(data);
			const char* name = _ParseString();
			dbgType->mName = name;
			dbgType->mSize = size;
			dbgType->mAlign = dbgType->mTypeParam->GetAlign();
			// Beef arrays do not necessarily have aligned sizes
			dbgType->mSizeCalculated = false;
			dbgType->mLanguage = dbgType->mTypeParam->mLanguage;
		}
		break;
	case LF_PROCEDURE:
		{
			dbgType = CvCreateType();

			//TODO: Calling convetion, etc

			lfProc* proc = (lfProc*)dataStart;

			dbgType->mTypeCode = DbgType_Subroutine;
			dbgType->mTypeParam = CvGetTypeSafe(proc->rvtype);
			BP_ALLOC_T(DbgBlock);
			dbgType->mBlockParam = mAlloc.Alloc<DbgBlock>();

			uint8* argData = CvGetTagData(proc->arglist, ipi);
			CvAutoReleaseTempData releaseTempData(this, argData);

			int16 argLeafType = GET_FROM(argData, int16);
			BF_ASSERT(argLeafType == LF_ARGLIST);
			int argCount = GET_FROM(argData, int32);
			CV_typ_t* argTypes = (CV_typ_t*)argData;
			for (int paramIdx = 0; paramIdx < proc->parmcount; paramIdx++)
			{
				BP_ALLOC_T(DbgVariable);
				DbgVariable* arg = mAlloc.Alloc<DbgVariable>();
				arg->mIsParam = true;
				arg->mType = CvGetTypeSafe(argTypes[paramIdx]);
				arg->mName = "$arg";
				dbgType->mBlockParam->mVariables.PushBack(arg);
			}
		}
		break;
	case LF_MFUNCTION:
		{
			dbgType = CvCreateType();

			//TODO: Calling convetion, etc

			lfMFunc* proc = (lfMFunc*)dataStart;

			dbgType->mTypeCode = DbgType_Subroutine;
			dbgType->mTypeParam = CvGetTypeSafe(proc->rvtype);
			BP_ALLOC_T(DbgBlock);
			dbgType->mBlockParam = mAlloc.Alloc<DbgBlock>();

			uint8* argData = CvGetTagData(proc->arglist, ipi);
			CvAutoReleaseTempData releaseTempData(this, argData);

			int16 argLeafType = GET_FROM(argData, int16);
			BF_ASSERT(argLeafType == LF_ARGLIST);
			int argCount = GET_FROM(argData, int32);
			CV_typ_t* argTypes = (CV_typ_t*)argData;
			for (int paramIdx = 0; paramIdx < proc->parmcount; paramIdx++)
			{
				BP_ALLOC_T(DbgVariable);
				DbgVariable* arg = mAlloc.Alloc<DbgVariable>();
				arg->mIsParam = true;
				arg->mType = CvGetTypeSafe(argTypes[paramIdx]);
				arg->mName = "$arg";
				dbgType->mBlockParam->mVariables.PushBack(arg);
			}
		}
		break;
// 	case 0x1609:
// 		NOP;
// 		break;
	default:
		NOP;
		break;
	}

	if (dbgType != NULL)
	{
		if (tagIdx == 14709)
		{
			NOP;
		}
		dbgType->mTagIdx = tagIdx;
		int wantIdx = tagIdx - mCvMinTag;
		mCvTypeMap[wantIdx] = dbgType;
	}

	return dbgType;
}

void COFF::ParseTypeData(CvStreamReader& reader, int dataOffset)
{
	BP_ZONE("COFF::ParseTypeData");

	//uint8* sectionEnd = sectionData + sectionSize;
	//uint8* data = sectionData;

	if (mCvMinTag == -1)
		mCvMinTag = 0x1000;

	int offset = dataOffset;

	bool isTagMapEmpty = mCvTagStartMap.empty();
	for (int tagIdx = mCvMinTag; true; tagIdx++)
	{
		if (tagIdx == mCvMaxTag)
			break;
		if (offset >= reader.mSize)
			break;

		//OutputDebugStrF("%X %X\n", tagIdx, (int)(data - sectionData));

		BF_ASSERT(((offset) & 3) == 0);

		//PTR_ALIGN(data, sectionData, 4);

		uint8* data = reader.GetTempPtr(offset, 4);
		uint16 trLength = *(uint16*)data;
		uint16 trLeafType = *(uint16*)(data + 2);

// 		uint16 trLength = GET(uint16);
// 		uint8* dataStart = data;
// 		uint16 trLeafType = GET(uint16);
// 		uint8* dataEnd = dataStart + trLength;

		if (isTagMapEmpty)
		{
			mCvTagStartMap.push_back(offset);
			mCvTypeMap.push_back(NULL);
		}
		else
			mCvTagStartMap[tagIdx - mCvMinTag] = offset;

		offset += trLength + 2;

		DbgType* dbgType = NULL;

		if (trLeafType == 0)
			continue;

		if ((trLeafType == LF_ENUM) || (trLeafType == LF_CLASS) || (trLeafType == LF_STRUCTURE) || (trLeafType == LF_UNION) ||
			(trLeafType == LF_CLASS_EX) || (trLeafType == LF_STRUCTURE_EX))
		{
			CvParseType(tagIdx);
		}
	}

	if (isTagMapEmpty)
		mCvTagStartMap.Add(offset);
}

void COFF::ParseTypeData(int sectionNum, CvStreamReader& reader, int& sectionSize, int& dataOfs, int& hashStream, int& hashAdjOffset, int& hashAdjSize, int& minVal, int& maxVal)
{
	BP_ZONE("COFF::ParseTypeData");

	sectionSize = 0;
// 	sectionData = CvReadStream(sectionNum, &sectionSize);
// 	if (sectionData == NULL)
// 		return;
// 	uint8* data = sectionData;

	CvReadStream(sectionNum, reader);
	uint8* data = reader.GetTempPtr(0, 0);
	uint8* sectionData = data;

	int32 ver = GET(int32);
	int32 headerSize = GET(int32);
	minVal = GET(int32);
	maxVal = GET(int32);
	int32 followSize = GET(int32);

	hashStream = GET(int16);
	int16 hashStreamPadding = GET(int16); // -1
	int32 hashKeySize = GET(int32); // 4
	int32 hashBucketsSize = GET(int32); // 0x3ffff
	int32 hashValsOffset = GET(int32); // 0
	int32 hashValsSize = GET(int32);
	int32 hashTypeIndexOffset = GET(int32);
	int32 hashTypeIndexSize = GET(int32);
	hashAdjOffset = GET(int32);
	hashAdjSize = GET(int32);

	dataOfs = 14 * 4;

	uint8* typeDataHead = data;

	bool validate = false;

	// Validate hash info - not necessary, just for debugging
	if (validate)
	{
		BF_FATAL("Fix validate stuff to work with new CvReader");

		int hashStreamSize = 0;
		uint8* hashDataHead = CvReadStream(hashStream, &hashStreamSize);

		int tagId = minVal;

		//////// Validate Type indices
		int typeIndexCount = hashTypeIndexSize / 8;
		// We expect a TypeIndexOffset for every 8k of data, plus the zero at the start
		int expectedTypeIndices = (sectionSize - (int)(data - sectionData)) / 8192 + 1;
		// Assert it's in some acceptable range...

		BF_ASSERT((typeIndexCount > expectedTypeIndices / 2) && (typeIndexCount < expectedTypeIndices * 2));
		for (int entryIdx = 0; entryIdx < typeIndexCount; entryIdx++)
		{
			data = hashDataHead + hashTypeIndexOffset + entryIdx * 8;
			GET_INTO(int, typeIdx);
			GET_INTO(int, ofs);

			uint8* dataEnd;
			int endTagId;
			if (entryIdx < typeIndexCount - 1)
			{
				endTagId = GET(int);
				int endOfs = GET(int);
				dataEnd = typeDataHead + endOfs;
			}
			else
			{
				endTagId = maxVal;
				dataEnd = sectionData + sectionSize;
			}

			data = typeDataHead + ofs;
			while (data < dataEnd)
			{
				int tagSize = GET(uint16);
				data += tagSize;
				tagId++;
			}
			BF_ASSERT(data == dataEnd);
			BF_ASSERT(tagId == endTagId);
		}

		BF_ASSERT(tagId == maxVal);

		////// Validate hashes
		int hashCount = hashValsSize / 4;
		BF_ASSERT(hashCount == maxVal - minVal);
		tagId = minVal;
		int32* hashVals = (int32*)(hashDataHead + hashValsOffset);
		data = typeDataHead;
		Array<int> tagStarts;
		tagStarts.Reserve(maxVal - minVal);
		for (int hashIdx = 0; hashIdx < hashCount; hashIdx++)
		{
			int32 expectHashVal = hashVals[hashIdx];

			PTR_ALIGN(data, sectionData, 4);
			tagStarts.push_back((int)(data - sectionData));
			int32 hashVal = BlHash::GetTypeHash(data) % hashBucketsSize;
			BF_ASSERT(hashVal == expectHashVal);

			tagId++;
		}

		//////// Validate hash adjusters
		if (hashAdjSize > 0)
		{
			data = hashDataHead + hashAdjOffset;
			uint8* dataEnd = data + hashAdjSize;

			GET_INTO(int32, adjustCount);
			GET_INTO(int32, adjustCapacity);

			GET_INTO(int32, usedLen);
			uint32* usedBitsArr = (uint32*)data;
			data += usedLen * sizeof(int32);
			GET_INTO(int32, deletedLen);
			uint32* deletedBitsArr = (uint32*)data;
			data += deletedLen * sizeof(int32);

			int curAdjIdx = 0;
			for (int usedIdx = 0; usedIdx < usedLen; usedIdx++)
			{
				int usedBits = usedBitsArr[usedIdx];
				for (int i = 0; i < 32; i++)
				{
					if ((usedBits & (1 << i)) != 0)
					{
						GET_INTO(int32, adjVal);
						GET_INTO(CV_typ_t, typeId);

						uint8* typeData = sectionData + tagStarts[typeId - minVal];
						int32 hashVal = BlHash::GetTypeHash(typeData) % hashBucketsSize;

						const char* strVal = mStringTable.mStrTable + adjVal;
						int32 strHashVal = BlHash::HashStr_PdbV1(strVal) % hashBucketsSize;

						BF_ASSERT(hashVal == strHashVal);

						curAdjIdx++;
					}
				}
			}

			BF_ASSERT(curAdjIdx == adjustCount);
		}

		data = typeDataHead;
	}
}

void COFF::ParseTypeData()
{
	if (!mPDBLoaded)
		return;
	if (mParsedTypeData)
		return;
	mParsedTypeData = true;

	auto linkedModule = GetLinkedModule();
	int startingTypeIdx = (int)linkedModule->mTypes.size();

	//uint8* sectionData;
	int sectionSize = 0;
	int hashAdjOffset = 0;
	int32 hashStream = -1;
	int32 hashAdjSize = 0;
	int32 dataOffset = 0;

	ParseTypeData(2, mCvTypeSectionReader, sectionSize, dataOffset, hashStream, hashAdjOffset, hashAdjSize, mCvMinTag, mCvMaxTag);
	//mCvTypeSectionData = sectionData;

	mCvTypeMap.Clear();
	mCvTagStartMap.Clear();
	mCvTypeMap.Resize(mCvMaxTag - mCvMinTag);
	mCvTagStartMap.Resize(mCvMaxTag - mCvMinTag);

	//DbgDataMap dataMap(minVal, maxVal);

	//uint8* data = sectionData + dataOffset;
	ParseTypeData(mCvTypeSectionReader, dataOffset);

	if (hashAdjSize > 0)
	{
		int sectionSize = 0;
		uint8* data = CvReadStream(hashStream, &sectionSize);
		uint8* sectionData = data;

		data = sectionData + hashAdjOffset;

		GET_INTO(int32, adjustCount);
		GET_INTO(int32, unk0);
		GET_INTO(int32, unkCount);
		for (int i = 0; i < unkCount; i++)
		{
			GET_INTO(int32, unk2);
		}
		GET_INTO(int32, unkCount2);
		for (int i = 0; i < unkCount2; i++)
		{
			GET_INTO(int32, unk3);
		}

		// Types listed in the adjustment table are always primary types,
		//  they should override any "old types" with the same name
		for (int adjIdx = 0; adjIdx < adjustCount; adjIdx++)
		{
			GET_INTO(int32, adjVal);
			GET_INTO(CV_typ_t, typeId);
			DbgType* dbgType = CvGetType(typeId);
			if (dbgType != NULL)
			{
				dbgType->mPriority = DbgTypePriority_Primary_Explicit;
			}
		}

		delete [] sectionData;
	}

	FixTypes(startingTypeIdx);
	MapTypes(startingTypeIdx);

	BF_ASSERT(mTempBufIdx == 0);
}

void COFF::FixConstant(DbgVariable* constVar)
{
	if (constVar->mType->mTypeCode == DbgType_u8)
		constVar->mConstValue &= 0xFF;
	else if (constVar->mType->mTypeCode == DbgType_u16)
		constVar->mConstValue &= 0xFFFF;
	else if (constVar->mType->mTypeCode == DbgType_u32)
		constVar->mConstValue &= 0xFFFFFFFF;
}

void COFF::MapRanges(DbgVariable* variable, CV_LVAR_ADDR_RANGE* range, CV_LVAR_ADDR_GAP* gaps)
{
	auto rangeStart = GetSectionAddr(range->isectStart, range->offStart);

	if (variable->mRangeStart == 0)
	{
		variable->mRangeStart = rangeStart;
		variable->mRangeLen = (rangeStart + range->cbRange) - variable->mRangeStart;
	}
	else
	{
		addr_target maxEnd = BF_MAX(variable->mRangeStart + variable->mRangeLen, rangeStart + range->cbRange);
		variable->mRangeStart = BF_MIN(variable->mRangeStart, rangeStart);
		variable->mRangeLen = maxEnd - variable->mRangeStart;
	}
}

void COFF::MakeThis(DbgSubprogram* curSubprogram, DbgVariable*& curParam)
{
	if ((curParam != NULL) || (curSubprogram->mParams.mHead != NULL))
		return;

	BP_ALLOC_T(DbgVariable);
	curParam = mAlloc.Alloc<DbgVariable>();
	curParam->mIsParam = true;
	curParam->mName = "this";
	curSubprogram->mParams.mHead = curParam;
}

static int gSymCount = 0;

void COFF::ParseCompileUnit_Symbols(DbgCompileUnit* compileUnit, uint8* sectionData, uint8* data, uint8* symDataEnd, CvInlineInfoVec& inlineDataVec, bool wantDeferInternals, DbgSubprogram* useSubprogram)
{
	bool isOptimized = false;

	uint8* dataHead = data;

	CvCompileUnit* cvCompileUnit = (CvCompileUnit*)compileUnit;

	uint8* symDataStart = data;
	DbgSubprogram* curSubprogram = NULL;
	DbgVariable* curRegRelVariable = NULL;
	DbgVariable* curParam = NULL;
	DbgVariable* localVar = NULL;
	bool inLocalVarRanged = false;
	SizedArray<DbgBlock*, 16> blockStack;
	blockStack.push_back(compileUnit->mGlobalBlock);
	int deferBlockDepth = 0;
	int deferInlineDepth = 0;

	uint8* newLocationDataStart = NULL;
	uint8* locationDataStart = NULL;
	uint8* locationDataEnd = NULL;
	int locationDataCount = 0;

	struct _DeferredVariableLocation
	{
		DbgVariable* mVariable;
		uint8* mRangedStart;
		int mRangedLength;

		_DeferredVariableLocation()
		{
			mVariable = NULL;
			mRangedStart = NULL;
			mRangedLength = 0;
		}
	};
	SizedArray<_DeferredVariableLocation, 16> deferredVariableLocations;
	int unrangedIdx = -1;

	auto _NextUnrangedLocalVar = [&](const char* name, DbgType* varType)
	{
		inLocalVarRanged = false;

		localVar = NULL;

		unrangedIdx++;
		while (unrangedIdx < deferredVariableLocations.size())
		{
			localVar = deferredVariableLocations[unrangedIdx].mVariable;
			if (strcmp(localVar->mName, name) == 0)
				break;
			localVar = NULL;
			unrangedIdx++;
		}

		bool isParam = false;
		if (localVar == NULL)
		{
			if (curParam != NULL)
			{
				isParam = true;
				if (curParam->mType != varType)
				{
					// Type was different, probably from a 'ref' being added when we are passing composites
					curParam->mName = name;
					curParam = curParam->mNext;
				}
				else
				{
					localVar = curParam;
					curParam = curParam->mNext;
				}
			}
		}

		if (localVar == NULL)
		{
			BP_ALLOC_T(DbgVariable);
			localVar = mAlloc.Alloc<DbgVariable>();
			if (name != NULL)
				blockStack.back()->mVariables.PushBack(localVar);
			//return;
		}

		localVar->mName = name;
		if (isParam)
			localVar->mIsParam = true;
		if (strcmp(name, "this") == 0)
		{
			MakeThis(curSubprogram, localVar);
			//BF_ASSERT(varType->mTypeCode == DbgType_Ptr);
			curSubprogram->mParentType = varType->mTypeParam;
			curSubprogram->mHasThis = true;
		}
	};

	auto _FinishLocationData = [&]()
	{
		if (locationDataStart == NULL)
			return;

		if (inLocalVarRanged)
		{
			auto& deferredVariableLocation = deferredVariableLocations.back();
			BF_ASSERT(deferredVariableLocation.mRangedLength == 0);
			deferredVariableLocation.mRangedStart = locationDataStart;
			deferredVariableLocation.mRangedLength = (int)(locationDataEnd - locationDataStart);
		}
		else
		{
			if (localVar->mLocationData != NULL)
			{
				// Already handled
				NOP;
			}
			else if (unrangedIdx >= deferredVariableLocations.size())
			{
				// There was no ranged data, commit just the unranged entry
				BF_ASSERT(locationDataCount == localVar->mLocationLen);
				BP_ALLOC("DeferredVarLoc0", locationDataEnd - locationDataStart);
				localVar->mLocationData = mAlloc.AllocBytes(locationDataEnd - locationDataStart, "DeferredVarLoc0");
				memcpy((uint8*)localVar->mLocationData, locationDataStart, locationDataEnd - locationDataStart);
			}
			else
			{
				// We have both ranged an unranged data. Merge them!
				auto& deferredVariableLocation = deferredVariableLocations[unrangedIdx];
				auto deferredVar = deferredVariableLocation.mVariable;
				BF_ASSERT(deferredVar == localVar);

				if (deferredVariableLocation.mRangedLength == 0)
					BF_ASSERT(locationDataCount == localVar->mLocationLen);
				else
					BF_ASSERT(locationDataCount < localVar->mLocationLen);
				BP_ALLOC("DeferredVarLoc1", deferredVariableLocation.mRangedLength + (locationDataEnd - locationDataStart));
				localVar->mLocationData = mAlloc.AllocBytes(deferredVariableLocation.mRangedLength + (locationDataEnd - locationDataStart), "DeferredVarLoc1");
				memcpy((uint8*)localVar->mLocationData, deferredVariableLocation.mRangedStart, deferredVariableLocation.mRangedLength);
				memcpy((uint8*)localVar->mLocationData + deferredVariableLocation.mRangedLength, locationDataStart, locationDataEnd - locationDataStart);

				deferredVariableLocation.mRangedLength = -1; // Mark as 'handled'
			}
		}

		locationDataStart = NULL;
		locationDataEnd = NULL;
		locationDataCount = 0;
	};

	auto _FlushDeferredVariableLocations = [&]()
	{
		for (auto& deferredVariableLocation : deferredVariableLocations)
		{
			if (deferredVariableLocation.mRangedLength == -1)
				continue;
			auto deferredVar = deferredVariableLocation.mVariable;
			if (deferredVar->mLocationData != NULL)
			{
				// Already handled
				continue;
			}
			BP_ALLOC("DeferredVarLoc2", deferredVariableLocation.mRangedLength);
			deferredVar->mLocationData = mAlloc.AllocBytes(deferredVariableLocation.mRangedLength, "DeferredVarLoc2");
			memcpy((uint8*)deferredVar->mLocationData, deferredVariableLocation.mRangedStart, deferredVariableLocation.mRangedLength);
		}
		deferredVariableLocations.clear();
		unrangedIdx = -1;
	};

	bool inlineDebugDump = false;

	if (useSubprogram != NULL)
	{
		curSubprogram = useSubprogram;
		blockStack.push_back(&curSubprogram->mBlock);

		curParam = curSubprogram->mParams.mHead;
		curRegRelVariable = curParam;
	}

	while (data < symDataEnd)
	{
		bool deferInternals = (wantDeferInternals) || (deferInlineDepth > 0);

		gSymCount++;

		uint8* dataStart = data;
		GET_INTO(uint16, symLen);
		uint8* dataEnd = data + symLen;
		GET_INTO(uint16, symType);

		if (!IsObjectFile())
			BF_ASSERT(symLen % 4 == 2);

		bool newLocalVarHasLocData = false;
		DbgVariable* prevLocalVar = localVar;

		switch (symType)
		{
		case S_LOCAL:
		case S_BPREL32:
		case S_REGISTER:
		case S_REGREL32:
		case S_GPROC32:
		case S_LPROC32:
		case S_GPROC32_ID:
		case S_LPROC32_ID:
			// Starting a new var or method
			if ((!deferInternals) && (locationDataStart != NULL))
			{
				_FinishLocationData();
			}
			break;
		}

		/*if (handled)
		{
			data = dataEnd;
			continue;
		}*/

		if (inlineDebugDump)
			BfLogCv("SYMTYPE: %X\n", symType);

		switch (symType)
		{
		case S_UDT:
			{
				UDTSYM& udtSym = *(UDTSYM*)dataStart;

				DbgType* typeDefType = CvCreateType();
				typeDefType->mTypeCode = DbgType_TypeDef;
				typeDefType->mTypeParam = CvGetTypeSafe(udtSym.typind);
				typeDefType->mName = DbgDupString((const char*)udtSym.name, "DbgDupString.S_UDT");
				if (strncmp(typeDefType->mName, "_bf::", 5) == 0)
				{
					typeDefType->mLanguage = DbgLanguage_Beef;
					typeDefType->mName += 5;
				}
				else
				{
					typeDefType->mLanguage = DbgLanguage_C;
				}
				typeDefType->mTypeName = typeDefType->mName;
			}
			break;
		case S_OBJNAME:
			{
				GET_INTO(int32, objSig);
				const char* objName = CvParseAndDupString(data);
			}
			break;
		case S_CONSTANT:
			{
				CONSTSYM& constSym = *(CONSTSYM*)dataStart;

				BP_ALLOC_T(DbgVariable);
				DbgVariable* constVar = mAlloc.Alloc<DbgVariable>();
				constVar->mName = DbgDupString((const char*)constSym.name, "DbgDupString.S_CONSTANT");
				constVar->mType = CvGetTypeSafe(constSym.typind);
				constVar->mIsConst = true;
				constVar->mIsStatic = true;
				constVar->mConstValue = constSym.value;

				data = constSym.name + strlen((const char*)constSym.name) + 1;
				constVar->mConstValue = CvParseConstant(constSym.value, data);
				FixConstant(constVar);

				if (constVar->mName != NULL)
					blockStack.back()->mVariables.PushBack(constVar);
			}
			break;
		case S_EXPORT:
			{
				GET_INTO(int16, ord);
				struct ExpFlags
				{
					unsigned short  fConstant : 1;      // CONSTANT
					unsigned short  fData : 1;          // DATA
					unsigned short  fPrivate : 1;       // PRIVATE
					unsigned short  fNoName : 1;        // NONAME
					unsigned short  fOrdinal : 1;       // Ordinal was explicitly assigned
					unsigned short  fForwarder : 1;     // This is a forwarder
					unsigned short  reserved : 10;      // Reserved. Must be zero.
				};
				GET_INTO(ExpFlags, flags);
				const char* symName = CvParseAndDupString(data);
			}
			break;
		case S_GDATA32:
		case S_LDATA32:
			// In PDB reads we get this from the symbol stream
			if ((IsObjectFile()) || (curSubprogram != NULL))
			{
				auto linkedModule = GetLinkedModule();
				DATASYM32& dataSym = *(DATASYM32*)dataStart;

				char* name = (char*)dataSym.name;
				char* targetName = NULL;
				DbgType* targetType = mMasterCompileUnit->mGlobalType;
				bool overrideBeef = false;
				char* lastDblColon = (char*)GetLastDoubleColon(name);
				if (lastDblColon != NULL)
				{
					targetName = name;
					name = lastDblColon + 2;
					*lastDblColon = 0;

					if (strcmp(targetName, "_bf") == 0)
					{
						overrideBeef = true;
					}
					else
					{
						targetType = CvGetTypeOrNamespace(targetName);
					}
				}

				DbgType* dbgType = CvGetTypeSafe(dataSym.typind);

				BP_ALLOC_T(DbgVariable);
				DbgVariable* variable = mAlloc.Alloc<DbgVariable>();
				variable->mType = dbgType;
				variable->mLocationData = dataStart;
				variable->mLocationLen = 1;
				variable->mIsStatic = true;
				variable->mCompileUnit = mMasterCompileUnit;
				variable->mName = name;
				if (targetType == mMasterCompileUnit->mGlobalType)
					variable->mLinkName = name;
				variable->mIsExtern = symType == S_GDATA32;

				if (curSubprogram != NULL)
				{
					if (!IsObjectFile())
					{
						// Copy this, we free the source data
						variable->mName = DbgDupString(name, "DbgDupString.S_GDATA32");
						BP_ALLOC_T(DATASYM32)
						auto newPtr = mAlloc.Alloc<DATASYM32>();
						memcpy(newPtr, variable->mLocationData, sizeof(DATASYM32));
						variable->mLocationData = (uint8*)newPtr;
					}
					if (variable->mName != NULL)
						blockStack.back()->mVariables.PushBack(variable);
					break;
				}

				//variable->mCompileUnit = m;
				// Push front so we will find before the original static declaration (that has no memory associated with it)
				if (targetType != NULL)
					targetType->mMemberList.PushFront(variable);

				if ((variable->mIsExtern) && (variable->mLinkName != NULL))
					mStaticVariables.push_back(variable);
			}
			break;
		case S_GTHREAD32:
		case S_LTHREAD32:
			{
				if ((IsObjectFile()) || (curSubprogram != NULL))
				{
					auto linkedModule = GetLinkedModule();
					THREADSYM32& dataSym = *(THREADSYM32*)dataStart;

					char* name = (char*)dataSym.name;
					char* targetName = NULL;
					DbgType* targetType = mMasterCompileUnit->mGlobalType;
					bool overrideBeef = false;
					char* lastDblColon = (char*)GetLastDoubleColon(name);
					if (lastDblColon != NULL)
					{
						targetName = name;
						name = lastDblColon + 2;
						*lastDblColon = 0;

						if (strcmp(targetName, "_bf") == 0)
						{
							overrideBeef = true;
						}
						else
						{
							targetType = CvGetTypeOrNamespace(targetName);
						}
					}

					DbgType* dbgType = CvGetTypeSafe(dataSym.typind);

					BP_ALLOC_T(DbgVariable);
					DbgVariable* variable = mAlloc.Alloc<DbgVariable>();
					variable->mType = dbgType;
					variable->mLocationData = dataStart;
					variable->mLocationLen = 1;
					variable->mIsStatic = true;
					variable->mCompileUnit = mMasterCompileUnit;
					variable->mName = name;
					if (targetType == mMasterCompileUnit->mGlobalType)
						variable->mLinkName = name;
					variable->mIsExtern = symType == S_GTHREAD32;

					if (curSubprogram != NULL)
					{
						if (!IsObjectFile())
						{
							// Copy this, we free the source data
							variable->mName = DbgDupString(name, "DbgDupString.S_GTHREAD32");
							BP_ALLOC_T(DATASYM32)
								auto newPtr = mAlloc.Alloc<DATASYM32>();
							memcpy(newPtr, variable->mLocationData, sizeof(DATASYM32));
							variable->mLocationData = (uint8*)newPtr;
						}
						if (variable->mName != NULL)
							blockStack.back()->mVariables.PushBack(variable);
						break;
					}

					//variable->mCompileUnit = m;
					// Push front so we will find before the original static declaration (that has no memory associated with it)
					targetType->mMemberList.PushFront(variable);

					if ((variable->mIsExtern) && (variable->mLinkName != NULL))
						mStaticVariables.push_back(variable);
				}
			}
			break;
		case S_GPROC32:
		case S_LPROC32:
		case S_GPROC32_ID:
		case S_LPROC32_ID:
			{
				BF_ASSERT(curSubprogram == NULL);

				PROCSYM32* procSym = (PROCSYM32*)dataStart;

				DbgType* parentType = NULL;

				auto addr = GetSectionAddr(procSym->seg, procSym->off);

				DbgSubprogram* subprogram = NULL;
				if (procSym->typind != 0)
				{
					bool ipi = false;
					if ((!IsObjectFile()) &&
						((symType == S_GPROC32_ID) || (symType == S_LPROC32_ID)))
					{
						if (!mCvIPIReader.IsSetup())
							CvParseIPI();
						ipi = true;
					}
					subprogram = CvParseMethod(parentType, NULL, procSym->typind, ipi);
				}

				if (subprogram == NULL)
				{
					BP_ALLOC_T(DbgSubprogram);
					subprogram = mAlloc.Alloc<DbgSubprogram>();
				}

				subprogram->mTagIdx = (int)(dataEnd - sectionData); // Position for method data
				subprogram->mIsOptimized = isOptimized;
				subprogram->mCompileUnit = compileUnit;
				char* name = DbgDupString((const char*)procSym->name, "DbgDupString.S_GPROC32");

				if (procSym->flags.CV_PFLAG_OPTDBGINFO)
				{
					subprogram->mIsOptimized = true;
				}

				if ((name[0] == '_') && (name[1] == '_'))
				{
					// This matches things like "__chkstk" or other system (supposedly) functions
					subprogram->mIsStepFilteredDefault = true;
				}

				if (strncmp(name, "_bf::", 5) == 0)
				{
					compileUnit->mLanguage = DbgLanguage_Beef;
				}

				localVar = NULL;
				bool hasColon = false;
				for (char* cPtr = name; *cPtr != 0; cPtr++)
				{
					char c = *cPtr;
					hasColon |= c == ':';
					if (c == '$')
					{
						if (strcmp(cPtr, "$CHK") == 0)
						{
							subprogram->mCheckedKind = BfCheckedKind_Checked;
							*cPtr = NULL;
							break;
						}
						else if (strcmp(cPtr, "$UCHK") == 0)
						{
							subprogram->mCheckedKind = BfCheckedKind_Unchecked;
							*cPtr = NULL;
							break;
						}
					}
				}

				subprogram->mPrologueSize = procSym->DbgStart;
				subprogram->mBlock.mLowPC = addr;
				subprogram->mBlock.mHighPC = subprogram->mBlock.mLowPC + (int32)procSym->len;
				BF_ASSERT(procSym->len >= 0);

				MapSubprogram(subprogram);

				curSubprogram = subprogram;
				curParam = subprogram->mParams.mHead;
				curRegRelVariable = curParam;
				blockStack.push_back(&subprogram->mBlock);
				//OutputDebugStrF("Func: %s\n", subprogram->mName);

				if (hasColon)
				{
					subprogram->mName = name;
					subprogram->mHasQualifiedName = true;
					compileUnit->mOrphanMethods.PushBack(curSubprogram);
				}
				else
				{
					subprogram->mName = name;
					compileUnit->mGlobalType->mMethodList.PushBack(curSubprogram);
				}

				BF_ASSERT(unrangedIdx == -1);
			}
			break;
		case S_THUNK32:
			{
				THUNKSYM32& thunkSym = *(THUNKSYM32*)dataStart;

				DbgType* parentType = NULL;

				DbgSubprogram* subprogram;
				BP_ALLOC_T(DbgSubprogram);
				subprogram = mAlloc.Alloc<DbgSubprogram>();
				subprogram->mCompileUnit = compileUnit;
				subprogram->mHasQualifiedName = true;
				subprogram->mName = DbgDupString((const char*)thunkSym.name, "DbgDupString.S_THUNK32");
				subprogram->mTagIdx = (int)(dataEnd - sectionData); // Position for method data

				subprogram->mBlock.mLowPC = GetSectionAddr(thunkSym.seg, thunkSym.off);
				subprogram->mBlock.mHighPC = subprogram->mBlock.mLowPC + thunkSym.len;
				BF_ASSERT(thunkSym.len >= 0);

				MapSubprogram(subprogram);

				curSubprogram = subprogram;
				curParam = subprogram->mParams.mHead;
				blockStack.push_back(&subprogram->mBlock);

				BF_ASSERT(unrangedIdx == -1);

				//OutputDebugStrF("Func: %s\n", subprogram->mName);
			}
			break;
		case S_BLOCK32:
			{
				if (deferInternals)
				{
					deferBlockDepth++;
					break;
				}

				BP_ALLOC_T(DbgBlock);
				DbgBlock* block = mAlloc.Alloc<DbgBlock>();
				blockStack.back()->mSubBlocks.PushBack(block);
				blockStack.push_back(block);

				BLOCKSYM32* blockSym = (BLOCKSYM32*)dataStart;

				block->mLowPC = GetSectionAddr(blockSym->seg, blockSym->off);
				block->mHighPC = block->mLowPC + blockSym->len;
			}
			break;
		case S_FRAMEPROC:
			{
				FRAMEPROCSYM* frameProc = (FRAMEPROCSYM*)dataStart;
				if (curSubprogram != NULL)
				{
// 					if ((curSubprogram->mName != NULL) && (strcmp(curSubprogram->mName, "_bf::IDETest::HotTester::TestFuncs") == 0))
// 					{
// 						NOP;
// 					}

					curSubprogram->mFrameBaseLen = frameProc->cbFrame + frameProc->cbSaveRegs;
					curSubprogram->mParamBaseReg = (DbgSubprogram::LocalBaseRegKind)(uint8)frameProc->flags.encodedParamBasePointer;
					curSubprogram->mLocalBaseReg = (DbgSubprogram::LocalBaseRegKind)(uint8)frameProc->flags.encodedLocalBasePointer;
				}
			}
			break;
		case S_BPREL32:
			{
				if (deferInternals)
					break;
				BPRELSYM32* bpRel32 = (BPRELSYM32*)dataStart;

				const char* name = DbgDupString((const char*)bpRel32->name, "DbgDupString.S_BPREL32");
				DbgType* varType = CvGetTypeSafe(bpRel32->typind);

				_NextUnrangedLocalVar(name, varType);

				localVar->mName = name;
				localVar->mCompileUnit = compileUnit;
				localVar->mType = varType;
				// This is location data now, not just a S_LOCAL opener
				//prevLocalVar = localVar;
				newLocalVarHasLocData = true;
			}
			break;
		case S_REGISTER:
			{
				if (deferInternals)
					break;
				REGSYM* regSym = (REGSYM*)dataStart;
				const char* name = DbgDupString((const char*)regSym->name);
				DbgType* varType = CvGetTypeSafe(regSym->typind);

				_NextUnrangedLocalVar(name, varType);

				localVar->mName = name;
				localVar->mCompileUnit = compileUnit;
				localVar->mType = varType;
				// This is location data now, not just a S_LOCAL opener
				//prevLocalVar = localVar;
				newLocalVarHasLocData = true;
			}
			break;
		case S_REGREL32:
			{
				if (deferInternals)
					break;
				REGREL32* regRel32 = (REGREL32*)dataStart;
				const char* name = DbgDupString((const char*)regRel32->name);
				DbgType* varType = CvGetTypeSafe(regRel32->typind);

				_NextUnrangedLocalVar(name, varType);

				localVar->mName = name;
				localVar->mCompileUnit = compileUnit;
				if ((localVar->mType != NULL) && (!localVar->mType->IsPointer()) && (varType != NULL) && (varType->IsPointer()))
					localVar->mSigNoPointer = true;
				localVar->mType = varType;
				// This is location data now, not just a S_LOCAL opener
				//prevLocalVar = localVar;
				newLocalVarHasLocData = true;
			}
			break;
		case S_LOCAL:
			{
				if (deferInternals)
					break;

				inLocalVarRanged = true;

				LOCALSYM& localSym = *(LOCALSYM*)dataStart;
				char* name = DbgDupString((const char*)localSym.name);

				bool isConst = false;
				int64 constVal = 0;
				if ((compileUnit->mLanguage == DbgLanguage_Beef) && (name != NULL) && (name[0] != '$'))
				{
					for (char* cPtr = name + 1; true; cPtr++)
					{
						char c = *cPtr;
						if (c == 0)
							break;
						if (c == '$')
						{
							if (cPtr[1] == '_')
							{
								cPtr[1] = '-';
								isConst = true;
								constVal = atoll(cPtr + 1);
								*cPtr = 0;
							}
							else if ((cPtr[1] >= '0') && (cPtr[1] <= '9'))
							{
								isConst = true;
								constVal = atoll(cPtr + 1);
								*cPtr = 0;
							}
						}
					}
				}

				if ((name != NULL) && (name[0] == '#'))
				{
					if (strcmp(name + 1, "StepOver") == 0)
					{
						curSubprogram->mIsStepFilteredDefault = true;
					}

					localVar = NULL;
					break;
				}

				DbgType* varType = CvGetType(localSym.typind, cvCompileUnit);
				if (varType == NULL)
					varType = CvGetType(T_VOID);

				if (name != NULL)
				{
					if ((localSym.flags.fIsOptimizedOut) && (name[0] != '$'))
					{
						if (varType->mSize > 0)
							curSubprogram->mIsOptimized = true;
					}

					if ((strcmp(name, "this") == 0) && (varType != NULL))
					{
						//BF_ASSERT(varType->mTypeCode == DbgType_Ptr);
						MakeThis(curSubprogram, curParam);
						curSubprogram->mParentType = varType->mTypeParam;
						curSubprogram->mHasThis = true;
					}
				}

				bool handledLocalVar = false;
				if (curParam != NULL)
				{
					if ((name != NULL) && (name[0] == '$'))
					{
						int strLen = strlen(name);

						// Splat head
						const char* dollarPos = strchr(name + 1, '$');
						const char* nameStr = name + 1;
						int nameLen = dollarPos - name - 1;

						if ((dollarPos != NULL) &&
							((localVar == NULL) || (strncmp(localVar->mName, name, nameLen + 1) != 0)))
						{
							BP_ALLOC("ParamName", nameLen + 1);
							char* dupStr = (char*)mAlloc.AllocBytes(nameLen + 1, "ParamName");
							memcpy(dupStr, nameStr, nameLen);
							curParam->mName = dupStr;
							curParam->mIsConst = true;
							curParam = curParam->mNext;

							if (strcmp(dupStr, "this") == 0)
								curSubprogram->mHasThis = true;
						}
					}
					else if ((curParam->mType != varType) && (varType->mTypeCode != DbgType_Void))
					{
						// Type was different, probably from a 'ref' being added when we are passing composites
						curParam->mName = name;
						curParam = curParam->mNext;
					}
					else
					{
						localVar = curParam;
						curParam = curParam->mNext;
						handledLocalVar = true;
					}
				}

				if (!handledLocalVar)
				{
					BP_ALLOC_T(DbgVariable);
					localVar = mAlloc.Alloc<DbgVariable>();
					if (name != NULL)
						blockStack.back()->mVariables.PushBack(localVar);
				}
				localVar->mName = name;

				localVar->mCompileUnit = compileUnit;
				localVar->mType = varType;
				localVar->mIsConst = isConst;
				localVar->mConstValue = constVal;

				_DeferredVariableLocation deferredLocation;
				deferredLocation.mVariable = localVar;
				deferredVariableLocations.Add(deferredLocation);
			}
			break;
		case S_DEFRANGE_REGISTER:
			{
				if (deferInternals)
					break;
				DEFRANGESYMREGISTER& defRangeReg = *(DEFRANGESYMREGISTER*)dataStart;
				if (localVar != NULL)
					MapRanges(localVar, &defRangeReg.range, defRangeReg.gaps);
			}
			break;
		case S_DEFRANGE_FRAMEPOINTER_REL:
			{
				if (deferInternals)
					break;
				DEFRANGESYMFRAMEPOINTERREL& defRangeFPRel = *(DEFRANGESYMFRAMEPOINTERREL*)dataStart;
				MapRanges(localVar, &defRangeFPRel.range, defRangeFPRel.gaps);
			}
			break;
		case S_DEFRANGE_SUBFIELD_REGISTER:
			{
				if (deferInternals)
					break;
				DEFRANGESYMSUBFIELDREGISTER& defRangeSubFieldReg = *(DEFRANGESYMSUBFIELDREGISTER*)dataStart;
			}
			break;
		case S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
			{
				if (deferInternals)
					break;
				DEFRANGESYMFRAMEPOINTERREL_FULL_SCOPE& defRangeFPRel = *(DEFRANGESYMFRAMEPOINTERREL_FULL_SCOPE*)dataStart;
			}
			break;
		case S_DEFRANGE_REGISTER_REL:
			{
				if (deferInternals)
					break;
				DEFRANGESYMREGISTERREL& defRangeRegRel = *(DEFRANGESYMREGISTERREL*)dataStart;
				if (localVar != NULL)
					MapRanges(localVar, &defRangeRegRel.range, defRangeRegRel.gaps);
			}
			break;
		case S_ENDARG:
			if (deferInternals)
				break;
			BF_ASSERT(curParam == NULL);
			break;
		case S_END:
		case S_PROC_ID_END:
			if (deferBlockDepth > 0)
				--deferBlockDepth;
			else
				blockStack.pop_back();
			BF_ASSERT(blockStack.size() > 0);
			if (blockStack.size() == 1)
			{
				_FinishLocationData();
				_FlushDeferredVariableLocations();

				// Done with forced subprogram?
				if (useSubprogram != NULL)
				{
					return;
				}
				else if (deferInternals)
				{
					if (curSubprogram->mTagIdx != -1)
					{
						int endOffset = (int)(dataEnd - sectionData);
						curSubprogram->mDeferredInternalsSize = endOffset - curSubprogram->mTagIdx;
					}
				}

				inlineDebugDump = false;
				curSubprogram = NULL;
				curParam = NULL;
			}
			break;
		case S_COMPILE2:
			{
				COMPILESYM* compileSym = (COMPILESYM*)dataStart;
			}
			break;
		case S_COMPILE3:
			{
				COMPILESYM3* compileSym = (COMPILESYM3*)dataStart;
			}
			break;
		case S_ENVBLOCK:
			{
				GET_INTO(uint8, flags);
				while (true)
				{
					const char* envKey = CvParseAndDupString(data);
					if (envKey == NULL)
						break;
					const char* envValue = CvParseAndDupString(data);
				}
			}
			break;
		case S_BUILDINFO:
			{
				CV_ItemId buildInfoId = GET(CV_ItemId);
			}
			break;
		case S_TRAMPOLINE:
			break;
		case S_COFFGROUP:
			break;
		case S_SECTION:
			break;
		case S_FRAMECOOKIE:
			{
				FRAMECOOKIE& frameCookie = *(FRAMECOOKIE*)dataStart;
			}
			break;
		case S_LABEL32:
			{
				LABELSYM32& labelSym = *(LABELSYM32*)dataStart;
			}
			break;
		case S_CALLSITEINFO:
			{
				CALLSITEINFO& callSiteInfo = *(CALLSITEINFO*)dataStart;
			}
			break;
		case S_HEAPALLOCSITE:
			{
				HEAPALLOCSITE& heapAllocSite = *(HEAPALLOCSITE*)dataStart;
			}
			break;
		case S_FILESTATIC:
			{
				FILESTATICSYM& fileStaticSym = *(FILESTATICSYM*)dataStart;
			}
			break;
		case S_CALLEES:
			{
				FUNCTIONLIST& calleeList = *(FUNCTIONLIST*)dataStart;
			}
			break;
		case S_CALLERS:
			{
				FUNCTIONLIST& calleeList = *(FUNCTIONLIST*)dataStart;
			}
			break;
		case S_POGODATA:
			{
				POGOINFO& pogoInfo = *(POGOINFO*)dataStart;
			}
			break;

		case S_INLINESITE:
		case S_INLINESITE2:
			{
				if (useSubprogram != NULL)
				{
					deferInlineDepth++;
					break; // Already handled this
				}

				uint32 inlinee;
				uint8* binaryAnnotations;

				if (symType == S_INLINESITE)
				{
					INLINESITESYM& inlineSite = *(INLINESITESYM*)dataStart;
					inlinee = inlineSite.inlinee;
					binaryAnnotations = (uint8*)&inlineSite.binaryAnnotations;
				}
				else // S_INLINESITE2
				{
					INLINESITESYM2& inlineSite = *(INLINESITESYM2*)dataStart;
					inlinee = inlineSite.inlinee;
					binaryAnnotations = (uint8*)&inlineSite.binaryAnnotations;
				}

				DbgSubprogram* inlineParent = curSubprogram;
				DbgSubprogram* subprogram = NULL;
				if (IsObjectFile())
				{
					subprogram = CvParseMethod(NULL, NULL, inlinee, false);
					subprogram->mCompileUnit = compileUnit;
					curSubprogram = subprogram;
				}
				else if ((inlinee & 0x80000000) != 0)
				{
					BP_ALLOC_T(DbgSubprogram);
					subprogram = mAlloc.Alloc<DbgSubprogram>(); // ??
					subprogram->mCompileUnit = compileUnit;

					if (!mCvIPIReader.IsSetup())
						CvParseIPI();

					curSubprogram = subprogram;
				}
				else
				{
					BP_ALLOC_T(DbgSubprogram);
					subprogram = mAlloc.Alloc<DbgSubprogram>();

					subprogram->mCompileUnit = compileUnit;
					curSubprogram = subprogram;

					FixupInlinee(curSubprogram, inlinee);
				}

				BF_ASSERT(subprogram == curSubprogram);

				curSubprogram->mTagIdx = (int)(dataEnd - sectionData); // Position for method data
				BP_ALLOC_T(DbgInlineeInfo);
				curSubprogram->mInlineeInfo = mAlloc.Alloc<DbgInlineeInfo>();
				curSubprogram->mInlineeInfo->mInlineParent = inlineParent;
				if (inlineParent->mInlineeInfo != NULL)
					curSubprogram->mInlineeInfo->mInlineDepth = inlineParent->mInlineeInfo->mInlineDepth + 1;
				else
					curSubprogram->mInlineeInfo->mInlineDepth = 1;
				curSubprogram->mInlineeInfo->mRootInliner = inlineParent->GetRootInlineParent();
				curSubprogram->mInlineeInfo->mInlineeId = 0;
				curSubprogram->mFrameBaseData = inlineParent->mFrameBaseData;
				curSubprogram->mFrameBaseLen = inlineParent->mFrameBaseLen;
				curSubprogram->mLocalBaseReg = inlineParent->mLocalBaseReg;

				if (curSubprogram->mName == NULL)
				{
					/*curSubprogram->mName = "<Inline>";

					String name = StrFormat("<Inline>@%@", curSubprogram);
					curSubprogram->mName = DbgDupString(name.c_str());*/

					String name = StrFormat("<Inline>@%@", curSubprogram);
					curSubprogram->mName = DbgDupString(name.c_str());

					curSubprogram->mInlineeInfo->mInlineeId = inlinee;
				}

				if (inlineDebugDump)
				{
					int depth = curSubprogram->GetInlineDepth();
					BfLogCv("S_INLINESITE Ofs:%d Depth:%d Inlinee:%X Subprogram:%@ %s\n  Parent:%@ %s\n", (dataStart - symDataStart), depth, inlinee,
						curSubprogram, curSubprogram->mName, curSubprogram->mInlineeInfo->mInlineParent, curSubprogram->mInlineeInfo->mInlineParent->mName);
				}

				CvInlineInfo inlineInfo;
				inlineInfo.mNext = NULL;
				inlineInfo.mTail = NULL;
				inlineInfo.mInlinee = inlinee;
				inlineInfo.mSubprogram = NULL;
				inlineInfo.mSubprogram = curSubprogram;
				inlineInfo.mData = binaryAnnotations;
				inlineInfo.mDataLen = dataEnd - inlineInfo.mData;
				inlineInfo.mDebugDump = inlineDebugDump;
				inlineDataVec.push_back(inlineInfo);
				blockStack.push_back(&curSubprogram->mBlock);
			}
			break;
		case S_INLINESITE_END:
			{
				if (inlineDebugDump)
					BfLogCv("S_INLINESITE_END\n");

				if (deferInlineDepth > 0)
				{
					deferInlineDepth--;
				}
				else
				{
					if (deferInternals)
					{
						int endOffset = (int)(dataEnd - sectionData);
						curSubprogram->mDeferredInternalsSize = endOffset - curSubprogram->mTagIdx;
					}

					curSubprogram = curSubprogram->mInlineeInfo->mInlineParent;
					blockStack.pop_back();
				}
			}
			break;
		case S_SSEARCH:
			{
				SEARCHSYM* searchSym = (SEARCHSYM*)dataStart;
			}
			break;
		case S_SEPCODE:
			{
				BP_ALLOC_T(DbgSubprogram);
				curSubprogram = mAlloc.Alloc<DbgSubprogram>();
				curSubprogram->mCompileUnit = compileUnit;
				blockStack.push_back(&curSubprogram->mBlock);

				SEPCODESYM* sepCodeSym = (SEPCODESYM*)dataStart;

				auto addr = GetSectionAddr(sepCodeSym->sect, sepCodeSym->off);
				auto parentAddr = GetSectionAddr(sepCodeSym->sectParent, sepCodeSym->offParent);

				curSubprogram->mBlock.mLowPC = addr;
				curSubprogram->mBlock.mHighPC = addr + sepCodeSym->length;

				String name = StrFormat("SEPCODE@%p", parentAddr);
				curSubprogram->mName = DbgDupString(name.c_str());
				curSubprogram->mTagIdx = (int)(dataEnd - sectionData); // Position for method data

				MapSubprogram(curSubprogram);
			}
			break;
		case S_COMPILE:
			break;
		case S_ANNOTATION:
			break;
		case S_UNAMESPACE:
			break;
		case /*S_FASTLINK*/0x1167:
			break;
		case /*S_INLINEES*/0x1168:
			break;
		case 0x1176:
			break;
		case 0x1178:
			break;
		case 0x1179:
			break;
		case 7:
			// Unknown
			break;
		default:
			BF_DBG_FATAL("Unhandled");
			break;
		}

		bool hasNewLocalVar = (localVar != prevLocalVar) && (localVar != NULL);
		if (newLocalVarHasLocData)
		{
			prevLocalVar = localVar;
		}

		if (deferInlineDepth == 0)
		{
			switch (symType)
			{
			case S_BPREL32:
			case S_REGISTER:
			case S_REGREL32:
			case S_DEFRANGE_REGISTER:
			case S_DEFRANGE_FRAMEPOINTER_REL:
			case S_DEFRANGE_SUBFIELD_REGISTER:
			case S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
			case S_DEFRANGE_REGISTER_REL:
				{
					if (prevLocalVar != NULL)
					{
						/*if (localVar->mLocationData == NULL)
						localVar->mLocationData = dataStart;
						localVar->mLocationLen++;*/
						if (locationDataStart == NULL)
							locationDataStart = dataStart;
						locationDataEnd = dataEnd;
						locationDataCount++;
						if (localVar->mLocationData == NULL)
							localVar->mLocationLen++;
					}
				}
				break;
			}
		}

		if (hasNewLocalVar)
		{
			if ((localVar->mName != NULL) && (localVar->mName[0] == '$'))
			{
				char* aliasPos = (char*)strstr(localVar->mName, "$alias$");

				// This $alias$ hack is unfortunate, but LLVM gets confused when we attempt to tie multiple debug variables
				//  to the same memory location, which can happen during mixin injection.  The result is that the mixin gets
				//  some premature instructions attributed to it from the variable declaration.  This fixes that.
				if ((aliasPos != NULL) && (aliasPos > localVar->mName))
				{
					String findName = String(aliasPos + 7);
					localVar->mName = CvDupString(localVar->mName + 1, aliasPos - localVar->mName - 1);

					auto curBlock = blockStack.back();
					localVar->mLocationData = (uint8*)CvDupString(aliasPos + 7, strlen(aliasPos + 7));
					localVar->mRangeStart = curBlock->mLowPC;
					localVar->mRangeLen = curBlock->mHighPC - curBlock->mLowPC;

					/*bool found = false;

					auto _CheckBlock = [&](DbgBlock* block)
					{
						for (auto checkVar : block->mVariables)
						{
							if (checkVar->mName == NULL)
								continue;

							if (findName == checkVar->mName)
							{
								localVar->mConstValue = checkVar->mConstValue;
								localVar->mType = checkVar->mType;
								localVar->mLocationData = checkVar->mLocationData;
								localVar->mLocationLen = checkVar->mLocationLen;
								found = true;
								return true;
							}
						}

						return false;
					};

					for (int blockIdx = (int)blockStack.size() - 1; blockIdx >= 0; blockIdx--)
					{
						DbgBlock* block = blockStack[blockIdx];
						if (_CheckBlock(block))
							break;
					}

					if (!found)
					{
						addr_target checkAddr = 0;
						DbgSubprogram* checkSubprogram = curSubprogram;

						while (checkSubprogram != NULL)
						{
							auto checkAddr = checkSubprogram->mBlock.mLowPC;
							if (checkSubprogram->mInlineeInfo == NULL)
								break;
							checkSubprogram = checkSubprogram->mInlineeInfo->mInlineParent;

							std::function<void(DbgBlock*)> _RecurseBlock = [&](DbgBlock* block)
							{
								if ((checkAddr < block->mLowPC) || (checkAddr >= block->mHighPC))
									return;

								if (_CheckBlock(block))
									return;

								for (auto block : block->mSubBlocks)
									_RecurseBlock(block);
							};

							_RecurseBlock(&checkSubprogram->mBlock);
						}
					}*/
				}
			}

			if ((compileUnit->mLanguage != DbgLanguage_Beef) && (localVar->mName != NULL))
			{
				for (char* cPtr = (char*)localVar->mName; true; cPtr++)
				{
					char c = *cPtr;
					if (c == 0)
						break;
					if ((c == '<') || (c == '>'))
						*cPtr = '$';
				}
			}
		}

		/*if (prevLocalVar != NULL)
		{
			BF_ASSERT(locationDataCount == prevLocalVar->mLocationLen);
		}*/

		data = dataEnd;
		//PTR_ALIGN(data, sectionData, 4);
	}

	if (localVar != NULL)
	{
		if (locationDataStart != NULL)
		{
			_FinishLocationData();
		}
	}

	_FlushDeferredVariableLocations();
}

CvCompileUnit* COFF::ParseCompileUnit(CvModuleInfo* moduleInfo, CvCompileUnit* compileUnit, uint8* sectionData, int sectionSize)
{
	BP_ZONE("COFF::ParseCompileUnit");

	CvInlineInfoVec inlineDataVec;
	Dictionary<uint32, CvInlineInfo*> inlineDataDict;

	if (moduleInfo != NULL)
	{
		BfLogDbg("ParseCompileUnit %s %s\n", mPDBPath.c_str(), moduleInfo->mModuleName);
	}
	else
	{
		BfLogDbg("ParseCompileUnit %s NULL\n", mPDBPath.c_str());
	}

	int allocSizeStart = mAlloc.GetAllocSize();

	if (compileUnit == NULL)
	{
		compileUnit = new CvCompileUnit(this);
		mCompileUnits.push_back(compileUnit);
	}
	compileUnit->mDbgModule = this;
	if (moduleInfo != NULL)
	{
		compileUnit->mModuleIdx = moduleInfo->mIdx;
        compileUnit->mName = moduleInfo->mModuleName;
		moduleInfo->mCompileUnit = compileUnit;
	}
	else
	{
		compileUnit->mModuleIdx = NULL;
		compileUnit->mName = mFilePath.c_str();
	}

	uint8* data = sectionData;
	uint8* dataEnd = NULL;

	GET_INTO(uint32, infoType);
	BF_ASSERT(infoType == CV_SIGNATURE_C13);

	int taggedSize = sectionSize;

	uint8* debugSubSectionsStart = data;
	if (moduleInfo != NULL)
	{
		taggedSize = moduleInfo->mLinesBytes;
		debugSubSectionsStart = data + moduleInfo->mSymbolBytes - sizeof(uint32);
	}
	else
	{
		taggedSize = sectionSize - sizeof(uint32);
	}

	if (mStringTable.mStrTable == NULL)
	{
		data = debugSubSectionsStart;
		dataEnd = data + taggedSize;
		while (data < dataEnd)
		{
			PTR_ALIGN(data, sectionData, 4);
			GET_INTO(int32, lineInfoType);
			GET_INTO(int32, lineInfoLength);
			uint8* dataStart = data;
			uint8* dataEnd = data + lineInfoLength;
			if (lineInfoType == DEBUG_S_STRINGTABLE)
			{
				if (mStringTable.mStrTable == NULL)
					mStringTable.mStrTable = (const char*)data;
			}
			data = dataEnd;
		}
	}

	//
	{
		data = debugSubSectionsStart;
		dataEnd = data + taggedSize;
		while (data < dataEnd)
		{
			PTR_ALIGN(data, sectionData, 4);
			GET_INTO(int32, lineInfoType);
			GET_INTO(int32, lineInfoLength);
			uint8* dataStart = data;
			uint8* dataEnd = data + lineInfoLength;
			if (lineInfoType == DEBUG_S_CROSSSCOPEIMPORTS)
			{
				while (data < dataEnd)
				{
					compileUnit->mImports.Add(CvCrossScopeImport());
					auto& crossScopeImport = compileUnit->mImports.back();
					crossScopeImport.mScopeCompileUnit = NULL;

					GET_INTO(uint32, externalScope);

					const char* fileName = mStringTable.mStrTable + externalScope;
					BfLogCv("DEBUG_S_CROSSSCOPEIMPORTS Scope:%s  ", fileName);
					crossScopeImport.mScopeName = fileName;

					//
					/*for (int checkIdx = 0; checkIdx < (int)mCvModuleInfo.size(); checkIdx++)
					{
						auto checkModuleInfo = mCvModuleInfo[checkIdx];
						if ((checkModuleInfo != NULL) && (checkModuleInfo->mModuleName != NULL) && (stricmp(checkModuleInfo->mModuleName, fileName) == 0))
						{
							ParseCompileUnit(checkIdx);
						}
					}*/

					GET_INTO(int32, numRefs);

					for (int i = 0; i < numRefs; i++)
					{
						GET_INTO(uint32, refId);
						BfLogCv(" %X", refId);

						crossScopeImport.mImports.Add(refId);
					}

					BfLogCv("\n");
				}
			}
			else if (lineInfoType == DEBUG_S_CROSSSCOPEEXPORTS)
			{
				BfLogCv("DEBUG_S_CROSSSCOPEEXPORTS");
				while (data < dataEnd)
				{
					GET_INTO(int32, localId);
					GET_INTO(int32, globalId);
					BfLogCv(" %X=%X", globalId, localId);

					CvCrossScopeExportEntry entry;
					entry.mLocalId = localId;
					entry.mGlobalId = globalId;
					compileUnit->mExports.Add(entry);
				}
				BfLogCv("\n");
			}
			else if (lineInfoType == DEBUG_S_FRAMEDATA)
			{
				uint8* dataPtr = data;
				addr_target baseAddr = 0;
				int relAddr = GET_FROM(dataPtr, int32);
				ParseFrameDescriptors(dataPtr, dataEnd - dataPtr, GetLinkedModule()->mImageBase + relAddr);
			}
			data = dataEnd;
		}
	}

	if (moduleInfo != NULL)
	{
		data = sectionData + 4;
		dataEnd = data + moduleInfo->mSymbolBytes - sizeof(uint32);
		bool wantsDeferInternals = true;
		ParseCompileUnit_Symbols(compileUnit, sectionData, data, dataEnd, inlineDataVec, wantsDeferInternals, NULL);
		//ParseCompileUnit_Symbols(compileUnit, sectionData, data, dataEnd, inlineDataVec, false, NULL);
		data = dataEnd;
	}

	// Scan debug subsections to find file checksum table, and to load symbol data for hotloads
	Dictionary<int, int> checksumFileRefs;
	{
		BP_ZONE("ParseCompileUnit_SrcFiles");

		data = debugSubSectionsStart;
		dataEnd = data + taggedSize;
		while (true)
		{
			PTR_ALIGN(data, sectionData, 4);
			if (data >= dataEnd)
				break;
			GET_INTO(int32, lineInfoType);
			GET_INTO(int32, lineInfoLength);
			uint8* dataStart = data;
			uint8* dataEnd = data + lineInfoLength;

			if (lineInfoType < 0)
			{
				// Ignore
			}
			else if (lineInfoType == DEBUG_S_SYMBOLS)
			{
				ParseCompileUnit_Symbols(compileUnit, sectionData, data, dataEnd, inlineDataVec, false, NULL);
			}
			else if (lineInfoType == DEBUG_S_FILECHKSMS)
			{
				BF_ASSERT(checksumFileRefs.size() == 0);

				while (data < dataEnd)
				{
					int dataOfs = (int)(data - dataStart);

					GET_INTO(uint, fileTableOfs);

					const char* fileName = mStringTable.mStrTable + fileTableOfs;

					if ((fileName[0] == '\\') && (fileName[1] == '$'))
						fileName++;

					DbgSrcFile* srcFile = NULL;

					if (fileName[0] == '$')
					{
						srcFile = AddSrcFile(compileUnit, fileName);
					}
					else if ((fileName[0] == '/') || (fileName[0] == '\\') ||
						((fileName[0] != 0) && (fileName[1] == ':')))
					{
						srcFile = AddSrcFile(compileUnit, fileName);
					}
					else
					{
						String fullDir = GetFileDir(mFilePath);
						fullDir.Append("\\");
						fullDir.Append(fileName);
						srcFile = AddSrcFile(compileUnit, fullDir);
					}
					if (srcFile->IsBeef())
						compileUnit->mLanguage = DbgLanguage_Beef;

					GET_INTO(uint8, hashLen);
					GET_INTO(uint8, hashType);
					if ((hashType == 1) && (hashLen == 16))
					{
						srcFile->mHashKind = DbgHashKind_MD5;
						memcpy(srcFile->mHash, data, 16);
					}
					else if ((hashType == 3) && (hashLen == 32))
					{
						srcFile->mHashKind = DbgHashKind_SHA256;
						memcpy(srcFile->mHash, data, 32);
					}

					data += hashLen;
					checksumFileRefs.TryAdd(dataOfs, (int)compileUnit->mSrcFileRefs.size() - 1);
					PTR_ALIGN(data, sectionData, 4);
				}
			}
			else
			{
				BF_ASSERT((lineInfoType >= DEBUG_S_SYMBOLS) && (lineInfoType <= DEBUG_S_COFF_SYMBOL_RVA));
			}

			data = dataEnd;
		}
	}

	if (!inlineDataVec.IsEmpty())
	{
		for (auto& inlineData : inlineDataVec)
		{
			uint32* keyPtr;
			CvInlineInfo** valuePtr;
			if (inlineDataDict.TryAdd(inlineData.mInlinee, &keyPtr, &valuePtr))
			{
				inlineData.mTail = &inlineData;
				*valuePtr = &inlineData;
			}
			else
			{
				(*valuePtr)->mTail->mNext = &inlineData;
				(*valuePtr)->mTail = &inlineData;
			}
		}
	}
	int inlineDataIdx = 0;

	DbgLineDataBuilder lineBuilder(this);

	//
	{
		BP_ZONE("ParseCompileUnit_LineInfo");

		//int totalLineCount = 0;
		//int targetLineCount = 0;

		int inlineIdx = 0;

// 		struct _InlinerData
// 		{
// 			bool mWantSummaryDump;
// 			Array<DbgInlinee> mInlinees;
// 			Array<DbgInlineLineData> mInlineLineData;
//
// 			_InlinerData()
// 			{
// 				mWantSummaryDump = false;
// 			}
// 		};
//
// 		Dictionary<DbgSubprogram*, _InlinerData> deferredInlinerDatas;

		// Line info
		data = debugSubSectionsStart;
		dataEnd = data + taggedSize;
		while (data < dataEnd)
		{
			PTR_ALIGN(data, sectionData, 4);
			GET_INTO(int32, lineInfoType);
			GET_INTO(int32, lineInfoLength);
			uint8* dataStart = data;
			uint8* dataEnd = data + lineInfoLength;

			if (lineInfoType == DEBUG_S_FILECHKSMS)
			{
				// Already handled
			}
			else if (lineInfoType == DEBUG_S_LINES)
			{
				CV_DebugSLinesHeader_t& lineSec = GET(CV_DebugSLinesHeader_t);

				Array<DbgLineData> lineDataVec;

				addr_target contribStartAddr = GetSectionAddr(lineSec.segCon, lineSec.offCon);

				while (data < dataEnd)
				{
					lineDataVec.Clear();

					CV_DebugSLinesFileBlockHeader_t& linesFileHeader = GET(CV_DebugSLinesFileBlockHeader_t);
					DbgSrcFileReference* srcFileRef = &compileUnit->mSrcFileRefs[checksumFileRefs[linesFileHeader.offFile]];
					bool isBeef = srcFileRef->mSrcFile->IsBeef();

					int expectSize = (int)sizeof(CV_DebugSLinesFileBlockHeader_t) + (int)(linesFileHeader.nLines * sizeof(CV_Line_t));
					if ((lineSec.flags & CV_LINES_HAVE_COLUMNS) != 0)
						expectSize += (int)(linesFileHeader.nLines * sizeof(int16) * 2);
					BF_ASSERT(expectSize == linesFileHeader.cbBlock);

					lineDataVec.Resize(linesFileHeader.nLines);
					for (int lineIdx = 0; lineIdx < linesFileHeader.nLines; lineIdx++)
					{
						CV_Line_t& srcLineData = GET(CV_Line_t);

						DbgLineData& lineData = lineDataVec[lineIdx];
						lineData.mRelAddress = (uint32)((contribStartAddr + srcLineData.offset) - mImageBase);

						if (srcLineData.linenumStart == 0xf00f00) // Never step into
						{
							if (lineIdx > 0)
								lineData.mLine = lineDataVec[lineIdx - 1].mLine;
							lineData.mColumn = -2; // Never step into marker
						}
						else if (srcLineData.linenumStart == 0xfeefee) // Always step into
						{
							if (lineIdx > 0)
								lineData.mLine = lineDataVec[lineIdx - 1].mLine;
							lineData.mColumn = -1; // Always step into marker
						}
						else
							lineData.mLine = srcLineData.linenumStart - 1;

						// In Beef we always set the column, so a section without CV_LINES_HAVE_COLUMNS indicates a block of invalid
						//  positions that we want to skip over
						if (isBeef)
							lineData.mColumn = -1;
					}

					if ((lineSec.flags & CV_LINES_HAVE_COLUMNS) != 0)
					{
						for (int lineIdx = 0; lineIdx < linesFileHeader.nLines; lineIdx++)
						{
							CV_Column_t& srcColumnData = GET(CV_Column_t);

							DbgLineData& lineData = lineDataVec[lineIdx];
							lineData.mColumn = (int)srcColumnData.offColumnStart - 1;
						}
					}

					DbgLineData* lastLineData = NULL;
					for (int lineIdx = 0; lineIdx < linesFileHeader.nLines; lineIdx++)
					{
						DbgLineData& lineData = lineDataVec[lineIdx];
						lastLineData = lineBuilder.Add(compileUnit, lineData, srcFileRef->mSrcFile, NULL);
					}
				}
			}
			else if (lineInfoType == DEBUG_S_INLINEELINES)
			{
				int linesType = GET(int);
				while (data < dataEnd)
				{
					int inlinee = 0;
					int srcFileIdx = -1;
					int startLine;

					DbgSrcFileReference* startSrcFileRef = NULL;
					if (linesType == 0)
					{
						CodeViewInfo::InlineeSourceLine& lineInfo = GET(CodeViewInfo::InlineeSourceLine);
						srcFileIdx = checksumFileRefs[lineInfo.fileId];
						startSrcFileRef = &compileUnit->mSrcFileRefs[srcFileIdx];
						startLine = lineInfo.sourceLineNum;
						inlinee = lineInfo.inlinee;
					}
					else if (linesType == 1)
					{
						CodeViewInfo::InlineeSourceLineEx& lineInfo = *(CodeViewInfo::InlineeSourceLineEx*)data;
						srcFileIdx = checksumFileRefs[lineInfo.fileId];
						startSrcFileRef = &compileUnit->mSrcFileRefs[srcFileIdx];
						startLine = lineInfo.sourceLineNum;
						inlinee = lineInfo.inlinee;

						data += sizeof(CodeViewInfo::InlineeSourceLineEx);
						data += (int)sizeof(CV_off32_t) * lineInfo.countOfExtraFiles;
					}
					else
						BF_FATAL("Invalid DEBUG_S_INLINEELINES lines type");

					CvInlineInfo* inlineData = NULL;
					inlineDataDict.TryGetValue(inlinee, &inlineData);

					while (inlineData != NULL)
					{
						bool inlineDebugDump = inlineData->mDebugDump;

						if (inlineData->mInlinee == -1)
						{
							if (inlineDebugDump)
								BfLogCv("Duplicate data: %X\n", inlinee);
							break;
						}

						// Mark as done
						inlineData->mInlinee = -1;

						int chunkNum = 0;
						int curLine = startLine;
						int curValidLine = curLine;
						bool flushOnLineOffset = false;
						addr_target lastLineAddr = 0;

						DbgSrcFileReference* srcFileRef = startSrcFileRef;
						DbgSubprogram* curSubprogram = inlineData->mSubprogram;

						DbgSubprogram* inlineParent = inlineData->mSubprogram->GetRootInlineParent();

						addr_target curAddr = inlineParent->mBlock.mLowPC;
						lastLineAddr = 0;

						if (inlineDebugDump)
						{
							//inlinerData->mWantSummaryDump = true;
							BfLogCv("------------------------------------------------\nINLINE DATA:%X Idx:%d CurAddr:%@ Line:%d\n SubProgram:%@ %s\n File:%s\n", inlinee, inlineIdx, curAddr, curLine,
								curSubprogram, curSubprogram->mName, srcFileRef->mSrcFile->mFilePath.c_str());
						}

						uint8* annData = inlineData->mData;
						uint8* annDataEnd = inlineData->mData + inlineData->mDataLen;

						DbgLineData* curLineData = NULL;

						auto _AddLine = [&]()
						{
							if (chunkNum != 0)
							{
								curLineData = NULL;
								return;
							}

							DbgLineData lineData;
							lineData.mLine = curValidLine - 1;
							lineData.mRelAddress = (uint32)(curAddr - mImageBase);
							lineData.mColumn = 0;
							if (curLine <= 0) // Negative lines mean "invalid position" for inlining
								lineData.mColumn = -1;
							lineData.mContribSize = 0;

							if ((curSubprogram->mBlock.mLowPC == 0) && (curLineData != NULL))
								curSubprogram->mBlock.mLowPC = mImageBase + curLineData->mRelAddress;

							if (inlineDebugDump)
								BfLogCv(" Adding Line:%d Addr:%@\n", lineData.mLine + 1, lineData.mRelAddress + mImageBase);

							curLineData = lineBuilder.Add(compileUnit, lineData, srcFileRef->mSrcFile, curSubprogram);
						};

						int codeIdx = 0;
						while (annData < annDataEnd)
						{
							auto annVal = CodeViewInfo::CVUncompressData(annData);

							switch (annVal)
							{
							case CodeViewInfo::BA_OP_Invalid:               // link time pdb contains PADDINGs
								break;
							case CodeViewInfo::BA_OP_CodeOffset:            // param : start offset
								{
									int32 offset = (int32)CodeViewInfo::CVUncompressData(annData);
									curAddr = inlineParent->mBlock.mLowPC + offset;
									if (inlineDebugDump)
										BfLogCv(" BA_OP_CodeOffset(%d) CurAddr:%@ Line:%d\n", offset, curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeCodeOffsetBase:  // param : nth separated code chunk (main code chunk == 0)
								{
									chunkNum = (int32)CodeViewInfo::CVUncompressData(annData);

									if (inlineDebugDump)
										BfLogCv("xxxxxxxxx BA_OP_ChangeCodeOffsetBase(%d) CurAddr:%@ Line:%d\n", chunkNum, curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeCodeOffset:      // param : delta of offset
								{
									int32 offset = (int32)CodeViewInfo::CVUncompressData(annData);

									curAddr += offset;
									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeCodeOffset(%d) CurAddr:%@ Line:%d\n", offset, curAddr, curLine);

									if ((curLineData != NULL) && (curLineData->mContribSize == 0))
									{
										curLineData->mRelAddress = (uint32)(curAddr - mImageBase);
										if (inlineDebugDump)
											BfLogCv(" Setting addr on previous entry\n");
									}
									else
										_AddLine();

									flushOnLineOffset = true;
								}
								break;
							case CodeViewInfo::BA_OP_ChangeCodeLength:      // param : length of code, default next start
								{
									int newCodeLen = (int32)CodeViewInfo::CVUncompressData(annData);
									curAddr += newCodeLen;

									if (curLineData != NULL)
										curLineData->mContribSize = newCodeLen;

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeCodeLength(%d) Addr:%@ EndAddr:%@\n", newCodeLen, curLineData->mRelAddress + mImageBase, curLineData->mRelAddress + mImageBase + curLineData->mContribSize);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeFile:            // param : fileId
								{
									uint32 newFileId = CodeViewInfo::CVUncompressData(annData);
									srcFileRef = &compileUnit->mSrcFileRefs[checksumFileRefs[newFileId]];

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeFile(%s) CurAddr:%@ Line:%d\n", srcFileRef->mSrcFile->mFilePath.c_str(), curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeLineOffset:      // param : line offset (signed)
								{
									int32 lineOfs = (int32)CodeViewInfo::DecodeSignedInt32(CodeViewInfo::CVUncompressData(annData));
									curLine += lineOfs;
									if (curLine > 0)
										curValidLine = curLine;

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeLineOffset(%d) CurAddr:%@ Line:%d\n", lineOfs, curAddr, curLine);

									_AddLine();
								}
								break;
							case CodeViewInfo::BA_OP_ChangeLineEndDelta:    // param : how many lines, default 1
								{
									int numLines = (int32)CodeViewInfo::CVUncompressData(annData);

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeLineEndDelta(%d) CurAddr:%@ Line:%d\n", numLines, curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeRangeKind:       // param : either 1 (default, for statement) or 0 (for expression)
								{
									int32 readKind = (int32)CodeViewInfo::CVUncompressData(annData);
									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeRangeKind(%d) CurAddr:%@ Line:%d\n", readKind, curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeColumnStart:     // param : start column number, 0 means no column info
								{
									int column = (int32)CodeViewInfo::CVUncompressData(annData);
									if (curLineData != NULL)
										curLineData->mColumn = column;
									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeColumnStart(%d) CurAddr:%@ Line:%d\n", column, curAddr, curLine);
								}
								break;
							case CodeViewInfo::BA_OP_ChangeColumnEndDelta:  // param : end column number delta (signed)
								{
									int columnEndDelta = (int32)CodeViewInfo::DecodeSignedInt32(CodeViewInfo::CVUncompressData(annData));
									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeColumnEndDelta(%d) CurAddr:%@ Line:%d\n", columnEndDelta, curAddr, curLine);
								}
								break;
								// Combo opcodes for smaller encoding size.
							case CodeViewInfo::BA_OP_ChangeCodeOffsetAndLineOffset:  // param : ((sourceDelta << 4) | CodeDelta)
								{
									int offsetData = (int32)CodeViewInfo::CVUncompressData(annData);
									int codeDelta = offsetData & 0xF;
									int sourceDelta = CodeViewInfo::DecodeSignedInt32(offsetData >> 4);

									curAddr += codeDelta;
									curLine += sourceDelta;
									if (curLine > 0)
										curValidLine = curLine;

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeCodeOffsetAndLineOffset(%d, %d) CurAddr:%@ Line:%d\n", codeDelta, sourceDelta,  curAddr, curLine);

									_AddLine();
								}
								break;
							case CodeViewInfo::BA_OP_ChangeCodeLengthAndCodeOffset:  // param : codeLength, codeOffset
								{
									curValidLine = curLine;
									int codeLen = (int32)CodeViewInfo::CVUncompressData(annData);
									int codeOffset = (int32)CodeViewInfo::CVUncompressData(annData);

									curAddr += codeOffset;

									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeCodeLengthAndCodeOffset(%d, %d) CurAddr:%@ Line:%d\n", codeLen, codeOffset, curAddr, curLine);

									_AddLine();

									if (curLineData != NULL)
									{
										if (inlineDebugDump)
											BfLogCv(" ContribSize:%d\n", codeLen);
										curLineData->mContribSize = codeLen;
									}
								}
								break;
							case CodeViewInfo::BA_OP_ChangeColumnEnd:       // param : end column number
								{
									int columnEnd = (int32)CodeViewInfo::CVUncompressData(annData);
									if (inlineDebugDump)
										BfLogCv(" BA_OP_ChangeColumnEnd(%d) CurAddr:%@ Line:%d\n", columnEnd, curAddr, curLine);
								}
								break;
							}

							codeIdx++;
						}

						if (curLineData != NULL)
						{
							if (curSubprogram->mBlock.mLowPC == 0)
								curSubprogram->mBlock.mLowPC = mImageBase + curLineData->mRelAddress;
							curSubprogram->mBlock.mHighPC = mImageBase + curLineData->mRelAddress + curLineData->mContribSize;
						}

						inlineData = inlineData->mNext;
					}
				}
			}

			data = dataEnd;
		}

// 		for (auto& deferredInlinerDataKV : deferredInlinerDatas)
// 		{
// 			auto inlinerParent = deferredInlinerDataKV.mKey;
// 			auto deferredInlinerData = &deferredInlinerDataKV.mValue;
//
// 			inlinerParent->mInlinerData = mAlloc.Alloc<DbgInlinerData>();
// 			inlinerParent->mInlinerData->mInlinees.CopyFrom(deferredInlinerData->mInlinees.mVals, deferredInlinerData->mInlinees.mSize, mAlloc);
// 			inlinerParent->mInlinerData->mInlineLineData.CopyFrom(deferredInlinerData->mInlineLineData.mVals, deferredInlinerData->mInlineLineData.mSize, mAlloc);
// 			//deferredInlinerData->
//
// 			if (deferredInlinerData->mWantSummaryDump)
// 			{
// 				BfLogCv("------------------------------\nSUMMARY FOR: %s\n", inlinerParent->mName);
//
// 				for (auto& inlineLine : inlinerParent->mInlinerData->mInlineLineData)
// 				{
// 					BfLogCv("Addr:%@ EndAddr:%@ Line:%d Column:%d Inlinee:%d\n", inlineLine.mAddress, inlineLine.mAddress + inlineLine.mContribSize,
// 						inlineLine.mLine + 1, inlineLine.mColumn + 1, inlineLine.mInlineIdx);
// 				}
// 			}
// 		}
	}

	lineBuilder.Commit();

	//OutputDebugStrF("Module loaded, AllocSize added: %d\n", (mAlloc.GetAllocSize() - allocSizeStart) / 1024);
	return compileUnit;
}

CvCompileUnit* COFF::ParseCompileUnit(int compileUnitId)
{
	CvModuleInfo* moduleInfo = mCvModuleInfo[compileUnitId];
	if (moduleInfo->mCompileUnit != NULL)
		return moduleInfo->mCompileUnit;

	BP_ZONE("ParseCompileUnit");

	ParseTypeData();

	if (moduleInfo->mStream == -1)
		return NULL;

	int sectionSize = 0;
	uint8* sectionData = CvReadStream(moduleInfo->mStream, &sectionSize);
	if (sectionData == NULL)
		return NULL;
	ParseCompileUnit(moduleInfo, NULL, sectionData, sectionSize);
	delete sectionData;
	return moduleInfo->mCompileUnit;
}

void COFF::ParseCompileUnits()
{
	for (int i = 0; i < (int)mCvModuleInfo.size(); i++)
		ParseCompileUnit(i);
}

DbgType* COFF::CvGetTypeOrNamespace(char* name, DbgLanguage language)
{
	if (language == DbgLanguage_Unknown)
	{
		if (strncmp(name, "_bf::", 5) == 0)
		{
			language = DbgLanguage_Beef;
			name += 5;
		}
		else if ((name[0] == 'G') && (name[1] == '$'))
		{
			language = DbgLanguage_Beef;
		}
		else
		{
			language = DbgLanguage_C;

			// Check for a primitive array type like 'double[]'
			for (int i = 0; i < 10; i++)
			{
				char c = name[i];
				if (c == 0)
					break;
				if (c == '[')
					language = DbgLanguage_Beef;
			}
		}
	}

	auto linkedModule = GetLinkedModule();

	auto dbgTypeEntry = linkedModule->mTypeMap.Find(name, language);
	if (dbgTypeEntry != NULL)
	{
		auto dbgType = dbgTypeEntry->mValue;
		if (dbgType->mHotNewType != NULL)
			return dbgType->mHotNewType;
		return dbgType;
	}

	// It's possible that we get a reference to a template name that isn't actually in our type database
	//  These may indicate VS compiler bugs or anonymous namespaces
	bool isValidName = true;
	for (const char* cPtr = name; *cPtr != '\0'; cPtr++)
		if ((*cPtr == '<') || (*cPtr == '`') || (*cPtr == '$'))
			isValidName = false;
	if (!isValidName)
		return NULL;

	//OutputDebugStrF("CvGetTypeOrNamespace Creating: %s\n", name);

	char* lastDblColon = (char*)GetLastDoubleColon(name);

	DbgType* dbgType = CvCreateType();

	DbgType* parentType = mMasterCompileUnit->mGlobalType;
	if (lastDblColon != NULL)
	{
		*lastDblColon = 0;
		parentType = CvGetTypeOrNamespace(name, language);
		dbgType->mTypeName = DbgDupString(lastDblColon + 2, "DbgDupString.TypeOrNamespace0");
		*lastDblColon = ':';
		dbgType->mName = DbgDupString(name, "DbgDupString.TypeOrNamespace1");
	}
	else
	{
		dbgType->mTypeName = DbgDupString(name, "DbgDupString.TypeOrNamespace2");
		dbgType->mName = dbgType->mTypeName;
	}

	parentType->mSubTypeList.PushBack(dbgType);
	dbgType->mTypeCode = DbgType_Namespace;
	dbgType->mLanguage = language;
	linkedModule->mTypeMap.Insert(dbgType);

	return dbgType;
}

void COFF::MapCompileUnitMethods(DbgCompileUnit* compileUnit)
{
	bool addHotTypes = (IsObjectFile()) && (mHotPrimaryTypes.size() == 0);

	DbgSubprogram* prevDbgMethod = NULL;
	DbgSubprogram* dbgMethod = compileUnit->mOrphanMethods.mHead;
	while (dbgMethod != NULL)
	{
		auto nextDbgMethod = dbgMethod->mNext;
		bool movedMethod = false;
		if (dbgMethod->mHasQualifiedName)
		{
			char* name = (char*)dbgMethod->mName;
			char* lastDblColon = (char*)GetLastDoubleColon(name);

			if (lastDblColon != NULL)
			{
				dbgMethod->mName = lastDblColon + 2;
				dbgMethod->mHasQualifiedName = false;
				const char* typeName = name;
				*lastDblColon = 0;

				DbgType* parentType = CvGetTypeOrNamespace(name);
				if (parentType != NULL)
				{
					if (addHotTypes)
					{
						mHotPrimaryTypes.Add(parentType);
					}

					compileUnit->mOrphanMethods.Remove(dbgMethod, prevDbgMethod);
					parentType->mMethodList.PushBack(dbgMethod);
					dbgMethod->mParentType = parentType;
					movedMethod = true;
				}
			}
		}

		if (!movedMethod)
			prevDbgMethod = dbgMethod;
		dbgMethod = nextDbgMethod;
	}
}

void COFF::MapCompileUnitMethods(int compileUnitId)
{
	CvModuleInfo* moduleInfo = mCvModuleInfo[compileUnitId];
	if (moduleInfo->mHasMappedMethods)
		return;
	ParseCompileUnit(compileUnitId);

	auto compileUnit = moduleInfo->mCompileUnit;
	//for (auto dbgMethod : compileUnit->mGlobalType.mMethodList)

	MapCompileUnitMethods(compileUnit);

	moduleInfo->mHasMappedMethods = true;
}

void COFF::PopulateType(DbgType* dbgType)
{
	uint8* data = CvGetTagData(dbgType->mTagIdx, false);
	CvAutoReleaseTempData releaseTempData(this, data);

	uint8* dataStart = data;
	uint16 trLeafType = GET(uint16);

	switch (trLeafType)
	{
	case LF_ENUM:
		{
			lfEnum& enumInfo = *(lfEnum*)dataStart;
			CvParseMembers(dbgType, enumInfo.field, false);
		}
		break;
	case LF_CLASS:
	case LF_STRUCTURE:
		{
			lfClass& classInfo = *(lfClass*)dataStart;
			CvParseMembers(dbgType, classInfo.field, false);
		}
		break;

	case LF_CLASS_EX:
	case LF_STRUCTURE_EX:
		{
			auto property = GET(CV_prop_t);
			auto extra = GET(int16);
			auto field = GET(CV_typ_t);
			CvParseMembers(dbgType, field, false);
		}
		break;

	case LF_UNION:
		{
			lfUnion& classInfo = *(lfUnion*)dataStart;
			CvParseMembers(dbgType, classInfo.field, false);
		}
		break;
	default:
		BF_FATAL("Invalid type");
	}
}

void COFF::PopulateTypeGlobals(DbgType* dbgType)
{
	//gDbgPerfManager->StartRecording();

	{
		BP_ZONE_F("COFF::PopulateTypeGlobals %s", (dbgType->mName != NULL) ? dbgType->mName : "?");

		ParseTypeData();
		dbgType->mNeedsGlobalsPopulated = false;
		dbgType->mHasGlobalsPopulated = true;
		mGlobalsTargetType = dbgType;
		ParseSymbolStream(CvSymStreamType_Globals_Targeted);
		mGlobalsTargetType = NULL;
	}

	//gDbgPerfManager->StopRecording(true);
}

void COFF::PopulateSubprogram(DbgSubprogram* dbgSubprogram)
{
	if (dbgSubprogram->mDeferredInternalsSize == 0)
		return;

	BP_ZONE("COFF::PopulateSubprogram");

	auto compileUnit = (CvCompileUnit*)dbgSubprogram->mCompileUnit;
	CvModuleInfo* moduleInfo = mCvModuleInfo[compileUnit->mModuleIdx];

	int sectionSize = 0;
	uint8* sectionData = CvReadStreamSegment(moduleInfo->mStream, dbgSubprogram->mTagIdx, dbgSubprogram->mDeferredInternalsSize);
	CvInlineInfoVec inlineDataVec;
	ParseCompileUnit_Symbols(compileUnit, NULL, sectionData, sectionData + dbgSubprogram->mDeferredInternalsSize, inlineDataVec, false, dbgSubprogram);
	delete sectionData;

	dbgSubprogram->mDeferredInternalsSize = 0;
}

void COFF::FixSubprogramName(DbgSubprogram* dbgSubprogram)
{
	const char* name = dbgSubprogram->mName;

	for (const char* cPtr = name; *cPtr != 0; cPtr++)
	{
		char c = *cPtr;
		if (c == '$')
		{
			bool isChecked = strcmp(cPtr, "$CHK") == 0;
			bool isUnchecked = strcmp(cPtr, "$UCHK") == 0;
			if (isChecked || isUnchecked)
			{
				dbgSubprogram->mCheckedKind = isChecked ? BfCheckedKind_Checked : BfCheckedKind_Unchecked;
				dbgSubprogram->mName = CvDupString(name, cPtr - name);
				return;
			}
		}
	}
}

void COFF::FixupInlinee(DbgSubprogram* dbgSubprogram, uint32 ipiTag)
{
	if (!mCvIPIReader.IsSetup())
		CvParseIPI();

	//int idx = (ipiTag & 0x7FFFFFFF) - mCvIPIMinTag;
	//uint8* dataStart = mCvIPIData + mCvIPITagStartMap[idx];

	uint8* dataStart = CvGetTagData(ipiTag & 0x7FFFFFFF, true);
	CvAutoReleaseTempData releaseTempData(this, dataStart);

	lfEasy& funcLeaf = *(lfEasy*)dataStart;
	switch (funcLeaf.leaf)
	{
	case LF_FUNC_ID:
		{
			lfFuncId* funcData = (lfFuncId*)dataStart;
			CvParseMethod(NULL, NULL, funcData->type, false, dbgSubprogram);
			dbgSubprogram->mName = (const char*)funcData->name;

			bool doScope = true;

			if ((funcData->scopeId != 0) && (doScope))
			{
				uint8* data = CvGetTagData(funcData->scopeId, true);
				CvAutoReleaseTempData releaseTempData(this, data);

				lfEasy& leaf = *(lfEasy*)data;
				switch (leaf.leaf)
				{
				case LF_STRING_ID:
					{
						lfStringId& str = *(lfStringId*)data;
						const char* parentStr = (const char*)str.name;

						int allocLen = strlen(parentStr) + 2 + strlen(dbgSubprogram->mName) + 1;
						BP_ALLOC("String", allocLen);
						char* dupStr = (char*)mAlloc.AllocBytes(allocLen, "String");

						strcpy(dupStr, parentStr);
						strcat(dupStr, "::");
						strcat(dupStr, dbgSubprogram->mName);
						dbgSubprogram->mName = dupStr;
					}
					break;
				case LF_UDT_MOD_SRC_LINE:
					{
						/*auto udtModSrcLine = *(lfUdtModSrcLine*)data;
						auto parentType = CvGetType(udtModSrcLine.type);
						curSubprogram->mParentType = parentType;*/
					}
					break;
				}
			}
			break;
		}
	case LF_MFUNC_ID:
		{
			lfMFuncId* funcData = (lfMFuncId*)dataStart;
			CvParseMethod(NULL, NULL, funcData->type, false, dbgSubprogram);
			dbgSubprogram->mName = (const char*)funcData->name;

			if (dbgSubprogram->mName != NULL)
			{
				for (const char* cPtr = dbgSubprogram->mName + 1; true; cPtr++)
				{
					char c = *cPtr;
					if (c == 0)
						break;
					// For removing the mangled name from the mixins
					if (c == '?')
					{
						int nameLen = cPtr - dbgSubprogram->mName;
						char* dupStr = (char*)mAlloc.AllocBytes(nameLen + 1);
						memcpy(dupStr, dbgSubprogram->mName, nameLen);
						dupStr[nameLen] = 0;
						dbgSubprogram->mName = dupStr;
						break;
					}
				}
			}

			FixSubprogramName(dbgSubprogram);

			dbgSubprogram->mParentType = CvGetType(funcData->parentType);
		}
		break;
	}
}

int COFF::MapImport(CvCompileUnit* compileUnit, int id)
{
	BF_ASSERT((id & 0x80000000) != 0);

	int scopeIdx = (id & 0x7FFF0000) >> 20;
	int itemId = (id & 0x0000FFFF);

	if (mModuleNameSet.IsEmpty())
	{
		for (auto moduleInfo : mCvModuleInfo)
		{
			String name = moduleInfo->mModuleName;
			name = ToUpper(name);

			CvModuleInfoNameEntry entry;
			entry.mModuleInfo = moduleInfo;
			mModuleNameSet.Add(entry);
		}
	}

	if (scopeIdx < (int)compileUnit->mImports.size())
	{
		auto& importSection = compileUnit->mImports[scopeIdx];
		if (itemId < (int)importSection.mImports.size())
		{
			auto importId = importSection.mImports[itemId];

			if (importSection.mScopeCompileUnit == NULL)
			{
				CvModuleInfo* moduleInfo = NULL;
				size_t nameHash = CvModuleInfoNameEntry::GetHashCode(importSection.mScopeName);

				for (auto& nameEntry : mModuleNameSet.SelectHashes(nameHash))
				{
					String checkModuleName = nameEntry.mModuleInfo->mModuleName;
					checkModuleName.Replace("\\", "/");
					checkModuleName = ToUpper(checkModuleName);

					String checkScopeName = importSection.mScopeName;
					checkScopeName.Replace("\\", "/");
					checkScopeName = ToUpper(checkScopeName);

					bool matches = false;
					if (checkModuleName == checkScopeName)
						matches = true;
					else if (checkScopeName.EndsWith("/" + checkModuleName))
						matches = true;

					if (matches)
					{
						ParseCompileUnit(nameEntry.mModuleInfo->mIdx);

						auto externCompileUnit = nameEntry.mModuleInfo->mCompileUnit;
						if (externCompileUnit->mExportMap.IsEmpty())
						{
							for (auto& exportEntry : externCompileUnit->mExports)
								externCompileUnit->mExportMap[exportEntry.mLocalId] = exportEntry.mGlobalId;
						}

						if (externCompileUnit->mExportMap.ContainsKey(importId))
						{
							importSection.mScopeCompileUnit = nameEntry.mModuleInfo->mCompileUnit;
							break;
						}
					}
				}
			}

			if (importSection.mScopeCompileUnit != NULL)
			{
				uint32 exportId;
				if (importSection.mScopeCompileUnit->mExportMap.TryGetValue(importId, &exportId))
				{
					if ((exportId & 0x80000000) != 0)
					{
						// Does this mean a recursive lookup?
						return exportId & 0x7FFFFFFF;
					}
					else
					{
						return exportId;
					}
				}
			}
		}
	}

	return 0;
}

void COFF::FixupInlinee(DbgSubprogram* dbgSubprogram)
{
	BF_ASSERT(dbgSubprogram->mInlineeInfo != NULL);
	BF_ASSERT((dbgSubprogram->mInlineeInfo->mInlineeId & 0x80000000) != 0);

	int scopeIdx = (dbgSubprogram->mInlineeInfo->mInlineeId & 0x7FFF0000) >> 20;
	int itemId = (dbgSubprogram->mInlineeInfo->mInlineeId & 0x0000FFFF);

	if (mModuleNameMap.IsEmpty())
	{
		for (auto moduleInfo : mCvModuleInfo)
		{
			mModuleNameMap[CaseInsensitiveString(moduleInfo->mModuleName)] = moduleInfo;
		}
	}

	auto compileUnit = (CvCompileUnit*)dbgSubprogram->mCompileUnit;

	if (scopeIdx < (int)compileUnit->mImports.size())
	{
		auto& importSection = compileUnit->mImports[scopeIdx];
		if (itemId < (int)importSection.mImports.size())
		{
			auto importId = importSection.mImports[itemId];

			if (importSection.mScopeCompileUnit == NULL)
			{
				CvModuleInfo* moduleInfo = NULL;
				mModuleNameMap.TryGetValue(CaseInsensitiveString(importSection.mScopeName), &moduleInfo);
				if (moduleInfo != NULL)
				{
					ParseCompileUnit(moduleInfo->mIdx);
					importSection.mScopeCompileUnit = moduleInfo->mCompileUnit;
				}

				if (importSection.mScopeCompileUnit != NULL)
				{
					for (auto& exportEntry : importSection.mScopeCompileUnit->mExports)
					{
						if (exportEntry.mLocalId == importId)
						{
							if (exportEntry.mGlobalId < mCvIPITagStartMap.size())
							{
								FixupInlinee(dbgSubprogram, exportEntry.mGlobalId);
							}
						}
					}
				}
			}
		}
	}

	dbgSubprogram->mInlineeInfo->mInlineeId = 0;
}

void COFF::PopulateStaticVariableMap()
{
	if (mPopulatedStaticVariables)
		return;

	BP_ZONE("COFF::PopulateStaticVariableMap");

	if (mMasterCompileUnit != NULL)
	{
		PopulateTypeGlobals(mMasterCompileUnit->mGlobalType);
		for (auto variable : mMasterCompileUnit->mGlobalType->mMemberList)
		{
			if ((variable->mIsExtern) && (variable->mLinkName != NULL))
				mStaticVariables.push_back(variable);
		}
	}

	DbgModule::PopulateStaticVariableMap();
}

void COFF::ScanCompileUnit(int compileUnitId)
{
	BP_ZONE("COFF::ScanCompileUnit");

	CvModuleInfo* moduleInfo = mCvModuleInfo[compileUnitId];

	if (moduleInfo->mStream < 0)
		return;

	BF_ASSERT(moduleInfo->mSymbolBytes % 4 == 0);
	uint8* sectionData = CvReadStreamSegment(moduleInfo->mStream, moduleInfo->mSymbolBytes, moduleInfo->mLinesBytes);
	uint8* data = sectionData;
	uint8* dataEnd = sectionData + moduleInfo->mLinesBytes;

	while (data < dataEnd)
	{
		GET_INTO(int32, lineInfoType);
		GET_INTO(int32, lineInfoLength);
		uint8* dataEnd = data + lineInfoLength;

		if (lineInfoType == DEBUG_S_FILECHKSMS)
		{
			while (data < dataEnd)
			{
				GET_INTO(uint, fileTableOfs);

				DbgSrcFile* srcFile;

				DbgSrcFile** srcFilePtr = NULL;
				if (!mCVSrcFileRefCache.TryAdd(fileTableOfs, NULL, &srcFilePtr))
				{
					srcFile = *srcFilePtr;
				}
				else
				{
					const char* fileName = mStringTable.mStrTable + fileTableOfs;
					if ((fileName[0] == '\\') && (fileName[1] == '$'))
						fileName++;
					srcFile = AddSrcFile(NULL, fileName);
					mSrcFileDeferredRefs.Add(srcFile);
					*srcFilePtr = srcFile;
					//mCVSrcFileRefCache[fileTableOfs] = srcFile;
				}

				DbgDeferredSrcFileReference dbgDeferredSrcFileReference;
				dbgDeferredSrcFileReference.mDbgModule = this;
				dbgDeferredSrcFileReference.mCompileUnitId = compileUnitId;
				srcFile->mDeferredRefs.push_back(dbgDeferredSrcFileReference);
				srcFile->mHadLineData = true;

				GET_INTO(uint8, hashLen);
				GET_INTO(uint8, hashType);
				data += hashLen;

				PTR_ALIGN(data, sectionData, 4);
			}

			break; // Stop once we handle the file checksums
		}

		data = dataEnd;
	}

	delete [] sectionData;
}

void COFF::FixTypes(int startingIdx)
{
	BP_ZONE("COFF::FixTypes");

	//std::unordered_map<String, DbgType*> namespaceMap;

	auto linkedModule = GetLinkedModule();

	String wholeNamespace;
	for (int typeIdx = startingIdx; typeIdx < (int)linkedModule->mTypes.size(); typeIdx++)
	{
		DbgType* dbgType = linkedModule->mTypes[typeIdx];

		DbgType* prevNamespaceType = NULL;
		if (dbgType->mName == NULL)
		{
			// Internal typedef
			continue;
		}

		if (dbgType->mLanguage == DbgLanguage_Unknown)
		{
			if (strncmp(dbgType->mName, "_bf::", 5) == 0)
			{
				dbgType->mLanguage = DbgLanguage_Beef;
				dbgType->mName += 5;
				dbgType->mTypeName += 5;
			}
			else if ((dbgType->mName[0] == 'G') && (dbgType->mName[1] == '$'))
			{
				dbgType->mLanguage = DbgLanguage_Beef;
			}
			else
			{
				dbgType->mLanguage = DbgLanguage_C;

				// Check for a primitive array type like 'double[]'
				for (int i = 0; i < 10; i++)
				{
					char c = dbgType->mName[i];
					if (c == 0)
						break;
					if (c == '[')
						dbgType->mLanguage = DbgLanguage_Beef;
				}
			}
		}

		if (dbgType->mIsDeclaration)
			continue;

		if (IsObjectFile())
		{
			auto entry = linkedModule->mTypeMap.Find(dbgType->mName, dbgType->mLanguage);
			if (entry != NULL)
			{
				// Just do hot replacing unless this type is new
				continue;
			}
		}

		// This never happens anymore - nested types are always handled as "internal typedefs"
		//TODO: Check the 'isnested' flag on the leaf instead of counting on parent to be specified?
		bool hadParent = dbgType->mParent != NULL;

		int chevronDepth = 0;

		if ((dbgType->mTypeCode == DbgType_Class) || (dbgType->mTypeCode == DbgType_Struct) || (dbgType->mTypeCode == DbgType_Union) ||
			(dbgType->mTypeCode == DbgType_Enum) || (dbgType->mTypeCode == DbgType_TypeDef))
		{
			int startIdx = -1;
			const char* name = dbgType->mTypeName;

			for (int i = 0; true; i++)
			{
				char c = name[i];
				if (c == '<')
					chevronDepth++;
				else if (c == '>')
					chevronDepth--;

				if ((chevronDepth == 0) && ((c == ':') || (c == '.')))
				{
					if ((!hadParent) && (i - startIdx > 1))
					{
						//String wholeNamespace = String(name, name + i);
						wholeNamespace.clear();
						wholeNamespace.Insert(0, name, i);

						auto entry = linkedModule->mTypeMap.Find(wholeNamespace.c_str(), dbgType->mLanguage);
						if (entry == NULL)
						{
							DbgType* namespaceType = CvCreateType();
							namespaceType->mTypeCode = DbgType_Namespace;
							namespaceType->mName = DbgDupString(wholeNamespace.c_str(), "DbgDupString.FixTypes");
							namespaceType->mTypeName = namespaceType->mName + startIdx + 1;
							namespaceType->mPriority = DbgTypePriority_Primary_Implicit;
							namespaceType->mLanguage = dbgType->mLanguage;
							linkedModule->mTypeMap.Insert(namespaceType);

							if (prevNamespaceType != NULL)
							{
								namespaceType->mParent = prevNamespaceType;
								prevNamespaceType->mSubTypeList.PushBack(namespaceType);
							}

							prevNamespaceType = namespaceType;
						}
						else
						{
							prevNamespaceType = entry->mValue;
						}
					}

					startIdx = i;
				}
				else if ((c == 0) /*|| (c == '<')*/ || (c == '('))
					break;
			}

			if (prevNamespaceType != NULL)
			{
				dbgType->mParent = prevNamespaceType;
				prevNamespaceType->mSubTypeList.PushBack(dbgType);
			}
			dbgType->mTypeName = dbgType->mTypeName + startIdx + 1; // Just take last part of name
		}

		if (dbgType->mParent == NULL)
			dbgType->mCompileUnit->mGlobalType->mSubTypeList.PushBack(dbgType);
	}
}

void COFF::CvReadStream(int streamIdx, CvStreamReader& streamReader)
{
	BF_ASSERT(streamReader.mCOFF == NULL);
	streamReader.mCOFF = this;
	streamReader.mPageBits = mCvPageBits;

	int streamSize = mCvStreamSizes[streamIdx];
	int streamPageCount = (streamSize + mCvPageSize - 1) / mCvPageSize;
	streamReader.mStreamPtrs.Resize(streamPageCount);
	streamReader.mSize = streamSize;

	int streamPtrIdx = mCvStreamPtrStartIdxs[streamIdx];
	for (int streamPageIdx = 0; streamPageIdx < streamPageCount; streamPageIdx++)
	{
		streamReader.mStreamPtrs[streamPageIdx] = (uint8*)mCvMappedViewOfFile + mCvStreamPtrs[streamPtrIdx] * mCvPageSize;
		streamPtrIdx++;
	}
}

void COFF::CvInitStreamRaw(CvStreamReader & streamReader, uint8 * data, int size)
{
	BF_ASSERT(streamReader.mCOFF == NULL);
	streamReader.mCOFF = this;
	streamReader.mPageBits = 31;
	streamReader.mStreamPtrs.Add(data);
	streamReader.mSize = size;
}

uint8* COFF::CvReadStream(int streamIdx, int* outSize)
{
	BP_ZONE("ReadCvSection");

	if (streamIdx >= mCvStreamSizes.size())
		return NULL;

	if ((streamIdx < 0) || (streamIdx >= mCvStreamSizes.mSize))
	{
		return NULL;
	}

	int streamSize = mCvStreamSizes[streamIdx];
	if (outSize != NULL)
		*outSize = streamSize;
	if (streamSize <= 0)
		return NULL;
	int streamPageCount = (streamSize + mCvPageSize - 1) / mCvPageSize;

	uint8* sectionData = new uint8[streamSize];
	bool deferDeleteSectionData = false;

	int streamPtrIdx = mCvStreamPtrStartIdxs[streamIdx];
	for (int streamPageIdx = 0; streamPageIdx < streamPageCount; streamPageIdx++)
	{
		mCvDataStream->SetPos(mCvStreamPtrs[streamPtrIdx] * mCvPageSize);
		mCvDataStream->Read(sectionData + streamPageIdx * mCvPageSize, std::min(streamSize - (streamPageIdx * mCvPageSize), mCvPageSize));
		streamPtrIdx++;
	}

	return sectionData;
}

uint8* COFF::CvReadStreamSegment(int streamIdx, int offset, int size)
{
	BP_ZONE("ReadCvSection");

	//int streamSize = mCvStreamSizes[streamIdx];
	//int streamPageCount = (streamSize + mCvPageSize - 1) / mCvPageSize;

	uint8* sectionData = new uint8[size];
	//delete [] sectionData;

	bool deferDeleteSectionData = false;

	uint8* data = sectionData;

	int curOffset = offset;
	int sizeLeft = size;

	int streamPtrIdx = mCvStreamPtrStartIdxs[streamIdx];
	streamPtrIdx += offset / mCvPageSize;
	curOffset = offset - (offset / mCvPageSize) * mCvPageSize;

	//for (int streamPageIdx = 0; streamPageIdx < streamPageCount; streamPageIdx++)
	while (sizeLeft > 0)
	{
		mCvDataStream->SetPos(mCvStreamPtrs[streamPtrIdx] * mCvPageSize + curOffset);
		int readSize = std::min(mCvPageSize - curOffset, sizeLeft);
		mCvDataStream->Read(data, readSize);
		data += readSize;
		sizeLeft -= readSize;
		curOffset = 0;
		streamPtrIdx++;
	}

	return sectionData;
}

void COFF::ReleaseTempBuf(uint8* buf)
{
	if ((mTempBufIdx > 0) && (mTempBufs.mVals[mTempBufIdx - 1].mVals == buf))
		mTempBufIdx--;
}

bool COFF::CvParseHeader(uint8 wantGuid[16], int32 wantAge)
{
	int streamSize = 0;
	uint8* data = CvReadStream(1, &streamSize);
	mCvHeaderData = data;

	int32 pdbVersion = GET(int32);
	int32 timestamp = GET(int32);
	int32 pdbAge = GET(int32);
	for (int i = 0; i < 16; i++)
		mPDBGuid[i] = GET(int8);

	if ((wantAge != -1) &&
		(/*(pdbAge != wantAge) ||*/ (memcmp(mPDBGuid, wantGuid, 16) != 0)))
	{
		if (mDebugger != NULL)
		{
			String msg = "PDB GUID did not match requested version\n";

			msg += StrFormat(" Module GUID: ");
			for (int i = 0; i < 16; i++)
				msg += StrFormat("%02X", (uint8)wantGuid[i]);
			msg += "\n";

			msg += StrFormat(" PDB GUID   : ");
			for (int i = 0; i < 16; i++)
				msg += StrFormat("%02X", (uint8)mPDBGuid[i]);
			Fail(msg);
		}
		return false;
	}

	if (mParseKind == ParseKind_Header)
		return true;

	int nameTableIdx = -1;

	GET_INTO(int32, strTabLen);
	const char* strTab = (const char*)data;
	data += strTabLen;

	GET_INTO(int32, numStrItems);
	GET_INTO(int32, strItemMax);

	GET_INTO(int32, usedLen);
	data += usedLen * sizeof(int32);
	GET_INTO(int32, deletedLen);
	data += deletedLen * sizeof(int32);

	for (int tableIdx = 0; tableIdx < numStrItems; tableIdx++)
	{
		GET_INTO(int32, strOfs);
		GET_INTO(int32, streamNum);

		const char* tableName = strTab + strOfs;
		if (strcmp(tableName, "/names") == 0)
			mStringTable.mStream = streamNum;
		if (strcmp(tableName, "srcsrv") == 0)
			mCvSrcSrvStream = streamNum;
		if (strcmp(tableName, "emit") == 0)
			mCvEmitStream = streamNum;

		/*if (tableIdx == nameTableIdx)
		{
		nameStringTable.mStream = streamNum;
		nameStringTable.mStreamOffset = strOfs;
		}*/
	}

	return true;
}

bool COFF::CvParseDBI(int wantAge)
{
	BP_ZONE("CvParseDBI");

	uint8* data = CvReadStream(3);
	if (data == NULL)
		return false;

	uint8* sectionData = data;
	defer
	(
		delete sectionData;
	);

	// Header
	GET_INTO(int32, signature);
	GET_INTO(uint32, version);
	GET_INTO(uint32, age);
	mDebugAge = (int32)age;

	if ((wantAge != -1) && (wantAge != age))
	{
		if (mDebugger != NULL)
		{
			String msg = "PDB age did not match requested age\n";
			msg += StrFormat(" Module Age: %d  PDB Age: %d", wantAge, age);
			Fail(msg);
		}
		return false;
	}

	if (mParseKind == ParseKind_Header)
		return true;

	mCvGlobalSymbolInfoStream = GET(int16); // hash1_file, snGSSyms
	GET_INTO(uint16, pdb_dll_version);
	mCvPublicSymbolInfoStream = GET(int16); // hash2_file, snPSSyms
	GET_INTO(uint16, pdb_dll_build_major);
	mCvSymbolRecordStream = GET(int16); // gsym_file, snSymRecs
	GET_INTO(uint16, pdb_dll_build_minor);
	GET_INTO(uint32, gp_modi_size); //module_size
	GET_INTO(uint32, section_contribution_size); //offset_size
	GET_INTO(uint32, section_map_size); //hash_size
	GET_INTO(uint32, file_info_size); //srcmodule_size
	GET_INTO(uint32, ts_map_size); //pdb_import_size
	GET_INTO(uint32, mfc_index); //resvd0
	GET_INTO(uint32, dbg_header_size); // stream_index_size
	GET_INTO(uint32, ec_info_size); //unknown_size
	GET_INTO(uint16, flags); //resvd3
	GET_INTO(uint16, machine);
	GET_INTO(uint32, reserved); //resvd4

	if (machine == 0x8664)
		mIs64Bit = true;
	else if (machine == 0x014C)
		mIs64Bit = false;
	else // Unknown machine
		return false;

	uint8* headerEnd = data;

	// Skip to debug header
	data += gp_modi_size;
	data += section_contribution_size;
	data += section_map_size;
	data += file_info_size;
	data += ts_map_size;
	data += ec_info_size;

	// Debug header
	GET_INTO(int16, fpo);
	GET_INTO(int16, exception);
	GET_INTO(int16, fixup);
	GET_INTO(int16, omap_to_src);
	GET_INTO(int16, omap_from_src);
	GET_INTO(int16, section_header); // 0x0a, all others are -1 (segments)
	GET_INTO(int16, token_rid_map);
	GET_INTO(int16, x_data);
	GET_INTO(int16, p_data);
	mCvNewFPOStream = GET(int16);
	GET_INTO(int16, section_header_origin);

	if (section_header != -1)
		ParseSectionHeader(section_header);

	// Module info
	data = headerEnd;
	uint8* dataEnd = data + gp_modi_size;

	while (data < dataEnd)
	{
		BP_ALLOC_T(CvModuleInfo);
		CvModuleInfo* moduleInfo = mAlloc.Alloc<CvModuleInfo>();
		memcpy(moduleInfo, data, sizeof(CvModuleInfoBase));
		data += sizeof(CvModuleInfoBase);

		moduleInfo->mModuleName = CvParseAndDupString(data);//DataGetString(data);
		moduleInfo->mObjectName = CvParseAndDupString(data);//DataGetString(data);
		moduleInfo->mIdx = (int)mCvModuleInfo.size();

		//BfLogCv("Module %d: %s  Obj: %s\n", mCvModuleInfo.size(), moduleInfo->mModuleName, moduleInfo->mObjectName);

		mCvModuleInfo.push_back(moduleInfo);

		/*if (moduleInfo->mStream != -1)
		{
			if (moduleInfo->mStream >= mDeferredModuleInfo.size())
				mDeferredModuleInfo.resize(moduleInfo->mStream + 1);
			mDeferredModuleInfo[moduleInfo->mStream].push_back(moduleInfo);
		}*/

		PTR_ALIGN(data, sectionData, 4);
	}

	// Section contrib
	{
		BP_ZONE("CvParseDBI_Contrib");

		dataEnd = data + section_contribution_size;
		int32 sig = GET(int32);
		BF_ASSERT((sig == 0xF12EBA2D) || (sig == 0xf13151e4));

		bool isV2 = sig == 0xf13151e4;
		if (isV2)
		{
			Fail("FastLink PDBs are not supported. Consider adding full debug info (/DEBUG:FULL).");
			mIsFastLink = true;
		}

		while (data < dataEnd)
		{
			CvSectionContrib& contrib = GET(CvSectionContrib);

			if (isV2)
			{
				uint32_t isectCoff = GET(uint32_t);
			}

			for (int contribOffset = 0; true; contribOffset += DBG_MAX_LOOKBACK)
			{
				int curSize = contrib.mSize - contribOffset;
				if (curSize <= 0)
					break;

				BP_ALLOC_T(DbgCompileUnitContrib);
				auto contribEntry = mAlloc.Alloc<DbgCompileUnitContrib>();
				contribEntry->mAddress = GetSectionAddr(contrib.mSection, contrib.mOffset) + contribOffset;

				if ((contribEntry->mAddress & 0xFFFF0000'00000000) != 0)
					continue;

				contribEntry->mLength = curSize;
				contribEntry->mDbgModule = this;
				contribEntry->mCompileUnitId = contrib.mModule;
				mDebugTarget->mContribMap.Insert(contribEntry);
			}
		}
	}

	// Dbi section map
	dataEnd = data + section_map_size;
	GET_INTO(int16, numSections);
	GET_INTO(int16, numSectionsCopy);
	while (data < dataEnd)
	{
		GET_INTO(uint8, flags);
		GET_INTO(uint8, section_type);
		// This field hasn't been deciphered but it is always 0x00000000 or 0xFFFFFFFF
		// and modifying it doesn't seem to invalidate the Cv.
		GET_INTO(int32, unknownData);
		GET_INTO(uint16, section_number);
		// Same thing as for unknown_data_1.
		GET_INTO(int32, unknownData2);
		// Value added to the address offset when calculating the RVA.
		GET_INTO(uint32, rva_offset);
		GET_INTO(uint32, section_length);
	}

	// Dbi File info
	dataEnd = data + file_info_size;
	GET_INTO(uint16, fileInfoNumModules);
	GET_INTO(uint16, bad_fileInfoNumSourceFiles);

	int fileInfoNumSourceFiles = 0;

	uint16* modIndices = (uint16*)data;
	data += fileInfoNumModules * sizeof(uint16);
	uint16* modFileCounts = (uint16*)data;
	data += fileInfoNumModules * sizeof(uint16);

	for (int moduleIdx = 0; moduleIdx < fileInfoNumModules; moduleIdx++)
		fileInfoNumSourceFiles += modFileCounts[moduleIdx];

	int32* fileNameOffsets = (int32*)data;
	data += fileInfoNumSourceFiles * sizeof(int32);

	char* nameBuffer = (char*)data;
	char* namePtr = nameBuffer;

	int curFileOfs = 0;
	for (int moduleIdx = 0; moduleIdx < fileInfoNumModules; moduleIdx++)
	{
		//TODO: LLD-LINK does not emit this properly
		//BF_ASSERT((modIndices[moduleIdx] == curFileOfs) || (moduleIdx >= 32));

		for (int fileIdx = 0; fileIdx < modFileCounts[moduleIdx]; fileIdx++)
		{
			char* fileName = namePtr + *fileNameOffsets;
			curFileOfs++;
			fileNameOffsets++;
		}
		//int blockStart = fileBlockTable[i];
		//int blockLength = fileBlockTable[fileInfoNumModules + i];

		//int offset = offsetTable[blockStart]; through blockLength...
	}

	// File name table
	/*uint8* dataStart = data;
	while (data < dataEnd)
	{
		int offset = (int)(data - dataStart);
		const char* fileName = DataGetString(data);
		//DbgSrcFile* srcFile = AddSrcFile(mMasterCompileUnit, fileName);
		//fileTable.insert(std::make_pair(offset, srcFile));
	}*/
	data = dataEnd;

	BF_ASSERT(ts_map_size == 0);

	// EC Info
	dataEnd = data + ec_info_size;
	GET_INTO(uint32, strTabSig);
	GET_INTO(uint32, strTabVer);

	BF_ASSERT(strTabSig == 0xEFFEEFFE);
	GET_INTO(int32, strTabSize);

	uint8* strTabData = data;
	data += strTabSize;

	GET_INTO(int32, strTabEntryCount);
	for (int entryIdx = 0; entryIdx < strTabEntryCount; entryIdx++)
	{
		GET_INTO(int32, strOffset);
		const char* str = (const char*)strTabData + strOffset;
	}

	return true;
}

void COFF::ParseSectionHeader(int sectionIdx)
{
	bool fakeRVAS = mSectionRVAs.empty();

	int sectionSize = 0;
	uint8* sectionData = CvReadStream(sectionIdx, &sectionSize);
	uint8* data = sectionData;

	uint8* dataEnd = data + sectionSize;
	while (data < dataEnd)
	{
		auto& sectionHeader = GET(PESectionHeader);
		if (fakeRVAS)
		{
			mSectionRVAs.push_back(sectionHeader.mVirtualAddress);
		}
	}

	if (fakeRVAS)
		mSectionRVAs.push_back(0);

	delete sectionData;
}

void COFF::CvParseIPI()
{
	BF_ASSERT(!mCvIPIReader.IsSetup());

	//uint8* sectionData;
	int sectionSize = 0;
	int hashAdjOffset = 0;
	int32 hashStream = -1;
	int32 hashAdjSize = 0;
	int32 dataOffset = 0;
	ParseTypeData(4, mCvIPIReader, sectionSize, dataOffset, hashStream, hashAdjOffset, hashAdjSize, mCvIPIMinTag, mCvIPIMaxTag);
	//mCvIPIData = sectionData;

	int recordCount = mCvIPIMaxTag - mCvIPIMinTag;
	mCvIPITagStartMap.Resize(recordCount + 1);

	//uint8* data = sectionData + dataOffset;
	int offset = dataOffset;
	for (int idx = 0; idx < recordCount; idx++)
	{
		//BF_ASSERT(((offset) & 3) == 0);
		uint8* data = mCvIPIReader.GetTempPtr(offset, 4);
		uint16 trLength = GET(uint16);
		int offsetStart = offset;
		offset += 2;
		data = mCvIPIReader.GetTempPtr(offset, trLength);
		uint8* dataStart = data;
		uint16 trLeafType = GET(uint16);
		//uint8* dataEnd = dataStart + trLength;
		offset += trLength;

		/*
		*/

		lfEasy& funcLeaf = *(lfEasy*)dataStart;
		switch (funcLeaf.leaf)
		{
		case LF_FUNC_ID:
			{
				lfFuncId* funcData = (lfFuncId*)dataStart;
				//subprogram->mName = (const char*)funcData->name;

				bool doScope = true;

				/*if (strcmp(curSubprogram->mName, "DispatchToMethod") == 0)
				{
				doScope = true;
				}*/

				if ((funcData->scopeId != 0) && (doScope))
				{
					uint8* data = CvGetTagData(funcData->scopeId, true);
					CvAutoReleaseTempData releaseTempData(this, data);

					lfEasy& leaf = *(lfEasy*)data;
					switch (leaf.leaf)
					{
					case LF_STRING_ID:
						{
							lfStringId& str = *(lfStringId*)data;
							const char* parentStr = (const char*)str.name;
						}
						break;
					case LF_UDT_MOD_SRC_LINE:
						{
							/*auto udtModSrcLine = *(lfUdtModSrcLine*)data;
							auto parentType = CvGetType(udtModSrcLine.type);
							curSubprogram->mParentType = parentType;*/
						}
						break;
					}
				}
				break;
			}
		case LF_MFUNC_ID:
			{
				lfMFuncId* funcData = (lfMFuncId*)dataStart;
				auto parentType = CvGetType(funcData->parentType);
			}
			break;
		}

		mCvIPITagStartMap[idx] = offsetStart;
	}

	mCvIPITagStartMap[recordCount] = offset;
}

const char* COFF::CvParseSymbol(int offset, CvSymStreamType symStreamType, addr_target& outAddr)
{
	const char* retName = NULL;

	int offsetStart = offset;
	uint8* data = mCvSymbolRecordReader.GetTempPtr(offset, 4);
	uint16 symLen = *(uint16*)data;
	bool madeCopy = false;
	data = mCvSymbolRecordReader.GetTempPtr(offset, symLen + 2, false, &madeCopy);
	uint8* dataStart = data;
	uint16 symType = *(uint16*)(data + 2);

	BF_ASSERT(symLen % 4 == 2);
	BF_ASSERT((intptr)data % 4 == 0);

	char* scanName = NULL;

	switch (symType)
	{
	case S_UDT:
		{
			UDTSYM& udt = *(UDTSYM*)dataStart;
			const char* name = (const char*)udt.name;

#ifdef _DEBUG
			ParseTypeData();
			auto type = CvGetType(udt.typind);
#endif
			retName = name;
		}
		break;
	case S_PUB32:
		{
			PUBSYM32& pubSym = *(PUBSYM32*)dataStart;

			if (symStreamType != CvSymStreamType_Symbols)
				break;

			BP_ALLOC_T(DbgSymbol);
			DbgSymbol* dbgSymbol = mAlloc.Alloc<DbgSymbol>();

			const char* name = (const char*)mCvSymbolRecordReader.GetPermanentPtr(offsetStart + offsetof(PUBSYM32, name), symLen - offsetof(PUBSYM32, name) + 2); // pubSym.name;
			//OutputDebugStrF("Sym: %s\n", name);
#ifdef BF_DBG_32
			if ((name != NULL) && (name[0] == '_'))
				name++;
#endif

			dbgSymbol->mName = name;
			dbgSymbol->mAddress = GetSectionAddr(pubSym.seg, pubSym.off);
			dbgSymbol->mDbgModule = this;

			if ((dbgSymbol->mAddress >= mImageBase) && (dbgSymbol->mAddress < mImageBase + mImageSize))
			{
				if ((dbgSymbol->mAddress & 0xFFFF'0000'0000'0000) == 0)
					mDebugTarget->mSymbolMap.Insert(dbgSymbol);
			}
			else
			{
				NOP;
			}
			mSymbolNameMap.Insert(dbgSymbol);
			outAddr = dbgSymbol->mAddress;

			retName = name;
		}
		break;
	case S_CONSTANT:
		{
			CONSTSYM& constSym = *(CONSTSYM*)dataStart;
		}
		break;
	case S_GDATA32:
	case S_LDATA32:
		{
			uint8* permDataPtr = madeCopy ? mCvSymbolRecordReader.GetPermanentPtr(offsetStart, symLen + 2) : dataStart;
			DATASYM32& dataSym = *(DATASYM32*)permDataPtr;

			if (strcmp((const char*)dataSym.name, "_tls_index") == 0)
			{
				mTLSIndexAddr = GetSectionAddr(dataSym.seg, dataSym.off);
			}

			char* name = (char*)dataSym.name;
			retName = name;

			if (symStreamType == CvSymStreamType_Globals_Scan)
			{
				scanName = (char*)dataSym.name;
				break;
			}

			bool wasBeef = false;
			const char* memberName = CvCheckTargetMatch(name, wasBeef);
			if (memberName == NULL)
				break;

			DbgType* dbgType = CvGetType(dataSym.typind);

			BP_ALLOC_T(DbgVariable);
			DbgVariable* variable = mAlloc.Alloc<DbgVariable>();
			variable->mType = dbgType;
			variable->mLocationData = permDataPtr;
			variable->mLocationLen = 1;

			variable->mIsStatic = true;
			variable->mCompileUnit = mMasterCompileUnit;
			variable->mName = memberName;
			variable->mLinkName = name;

			if (strcmp((const char*)dataSym.name, "__ImageBase") == 0)
				variable->mLinkName = NULL; // Avoid adding to map

			variable->mIsExtern = symType == S_GDATA32;
			variable->mCompileUnit = mGlobalsTargetType->mCompileUnit;
			// Push front so we will find before the original static declaration (that has no memory associated with it)
			mGlobalsTargetType->mMemberList.PushFront(variable);

			if (IsObjectFile())
			{
				if ((variable->mIsExtern) && (variable->mLinkName != NULL))
					mStaticVariables.push_back(variable);
			}
		}
		break;
	case S_GTHREAD32:
	case S_LTHREAD32:
		{
			uint8* permDataPtr = madeCopy ? mCvSymbolRecordReader.GetPermanentPtr(offsetStart, symLen + 2) : dataStart;
			THREADSYM32& threadSym = *(THREADSYM32*)permDataPtr;

			if (symStreamType == CvSymStreamType_Globals_Scan)
			{
				scanName = (char*)threadSym.name;
				break;
			}

			char* name = (char*)threadSym.name;
			retName = name;
			bool wasBeef = false;
			const char* memberName = CvCheckTargetMatch(name, wasBeef);
			if (memberName == NULL)
				break;

			addr_target addr = GetSectionAddr(threadSym.seg, threadSym.off);
			DbgType* dbgType = CvGetType(threadSym.typind);

			BP_ALLOC_T(DbgVariable);
			DbgVariable* variable = mAlloc.Alloc<DbgVariable>();
			variable->mType = dbgType;
			variable->mLocationData = permDataPtr;
			variable->mLocationLen = 1;
			variable->mIsStatic = true;
			variable->mCompileUnit = mMasterCompileUnit;
			variable->mName = memberName;
			variable->mLinkName = name;
			variable->mIsExtern = symType == S_GTHREAD32;
			variable->mCompileUnit = mGlobalsTargetType->mCompileUnit;
			// Push front so we will find before the original static declaration (that has no memory associated with it)
			mGlobalsTargetType->mMemberList.PushFront(variable);
		}
		break;
	case S_PROCREF:
		{
			REFSYM2& refSym = *(REFSYM2*)dataStart;
			if (refSym.imod == 0)
				break;

			char* name = (char*)refSym.name;
			retName = name;

			if (symStreamType == CvSymStreamType_Globals_Scan)
			{
				scanName = (char*)refSym.name;
				break;
			}

			int moduleId = refSym.imod - 1;

			bool wasBeef = false;
			char* memberName = (char*)CvCheckTargetMatch(name, wasBeef);
			if (memberName == NULL)
				break;

			bool wantsCopy = madeCopy;

			bool isIllegal = false;
			for (const char* cPtr = memberName; *cPtr != 0; cPtr++)
			{
				char c = *cPtr;
				if (c == '$')
				{
					if ((strcmp(cPtr, "$CHK") == 0) || (strcmp(cPtr, "$UCHK") == 0))
					{
						auto prevName = memberName;
						memberName = DbgDupString(prevName);
						memberName[cPtr - prevName] = 0;
						wantsCopy = false;
						break;
					}
					else
						isIllegal = true;
				}

				if ((c == '<') || (c == '`'))
					isIllegal = true;
			}

			if (!isIllegal)
			{
				BP_ALLOC_T(DbgMethodNameEntry);
				auto methodNameEntry = mAlloc.Alloc<DbgMethodNameEntry>();
				methodNameEntry->mCompileUnitId = moduleId;
				methodNameEntry->mName = wantsCopy ? DbgDupString(memberName) : memberName;
				mGlobalsTargetType->mMethodNameList.PushBack(methodNameEntry);
			}
		}
		break;
	default:
		//BF_FATAL("Unhandled");
		break;
	}

	if (scanName != NULL)
	{
		//TODO: Remove
		//String origName = name;

		//CvFixupName(name);
		char* lastDblColon = (char*)GetNamespaceEnd(scanName);
		if (lastDblColon != NULL)
		{
			if (mPrevScanName != NULL)
			{
				int namespaceLen = lastDblColon - scanName;
				if (strncmp(scanName, mPrevScanName, namespaceLen + 2) == 0) // +2 includes the ending '::'
				{
					// We've already inserted this namespace
					return retName;
				}
			}

			StringT<256> tempName;
			tempName = String(scanName, lastDblColon - scanName);

			DbgType* dbgType = CvGetTypeOrNamespace((char*)tempName.c_str());

// 			*lastDblColon = '\0';
// 			DbgType* dbgType = CvGetTypeOrNamespace(scanName);
// 			*lastDblColon = ':';

			/*if (strcmp(lastDblColon, "::this") == 0)
			{
				dbgType->mLanguage = DbgLanguage_Beef;
			}*/
			if (dbgType != NULL)
				dbgType->mNeedsGlobalsPopulated = true;

			mPrevScanName = scanName;

			/*const char* methodName = NULL;

			DbgType* dbgParent = NULL;
			if (lastDblColon != NULL)
			{
			*lastDblColon = 0;
			const char* namespaceName = name;
			dbgParent = CvGetTypeOrNamespace(namespaceName);
			methodName = lastDblColon + 2;
			}
			else
			{
			dbgParent = &mMasterCompileUnit->mGlobalType;
			//mStaticVariables.push_back(variable);
			methodName = name;
			}

			if (dbgParent != NULL)
			{
			auto methodNameEntry = mAlloc.Alloc<DbgMethodNameEntry>();
			methodNameEntry->mCompileUnitId = moduleId;
			methodNameEntry->mName = methodName;
			dbgParent->mMethodNameList.PushBack(methodNameEntry);
			}*/
		}
		else
		{
			mMasterCompileUnit->mGlobalType->mNeedsGlobalsPopulated = true;
		}
	}

	return retName;
}

uint8* COFF::HandleSymStreamEntries(CvSymStreamType symStreamType, uint8* data, uint8* addrMap)
{
	uint32 tickStart = BFTickCount();

	GET_INTO(uint32, verSig);
	GET_INTO(uint32, verHdr);
	GET_INTO(int32, sizeHr);
	GET_INTO(int32, sizeBuckets);

	BF_ASSERT(verSig == 0xffffffff);
	BF_ASSERT(verHdr == 0xf12f091a);

	uint8* dataEnd = data + sizeHr;
	uint8* refDataHead = data;
	int entryIdxMax = sizeHr / 8;

	data = dataEnd;

	dataEnd = data + sizeBuckets ;

	int32* hashBucketHead = (int32*)(data + 0x81 * 4);
	int32* hashBucketPtr = hashBucketHead;

	int numRecordsRead = 0;

#ifdef _DEBUG
	bool verify = false;
#endif

	if (mIsFastLink)
		return dataEnd; // Format changed

	if (sizeBuckets == 0)
		return dataEnd; // No hash

	std::multimap<addr_target, int> checkAddrMap;

	int bitCount = 0;
	for (int blockIdx = 0; blockIdx < 0x81; blockIdx++)
	{
		GET_INTO(uint32, bits);
		for (int i = 0; i < 32; i++)
		{
			if ((bits & (1 << i)) != 0)
			{
				bitCount++;
				int32 entryStart = *(hashBucketPtr++) / 12;
				int32 entryEnd;
				if ((uint8*)hashBucketPtr != (uint8*)dataEnd)
					entryEnd = *(hashBucketPtr) / 12;
				else
					entryEnd = entryIdxMax;

				BF_ASSERT(entryStart <= entryEnd);

				for (int entryIdx = entryStart; entryIdx < entryEnd; entryIdx++)
				{
					int32 symDataPtr = *(int32*)(refDataHead + entryIdx * 8);
					if (symDataPtr == 0)
						continue;

					addr_target addr = 0;
					const char* symName = CvParseSymbol(symDataPtr - 1, symStreamType, addr);
#ifdef _DEBUG
					if ((verify) && (addrMap != NULL))
					{
						checkAddrMap.insert(std::make_pair(addr, symDataPtr - 1));
					}

					if ((verify) && (symName != NULL))
					{
						int hashIdx = (blockIdx * 32) + i;
						int wantHash = BlHash::HashStr_PdbV1(symName) % 4096;
						BF_ASSERT(hashIdx == wantHash);
					}
#endif
					numRecordsRead++;
				}
			}
		}
	}

#ifdef _DEBUG
	if ((verify) && (addrMap != NULL))
	{
		auto itr = checkAddrMap.begin();

		data = addrMap;
		for (int entryIdx = 0; entryIdx < numRecordsRead; entryIdx++)
		{
			int verifyIdx = GET(int32);
			int actualIdx = itr->second;
			if (actualIdx != verifyIdx)
			{
				OutputDebugStrF("Mismatch #%X %X %X %08X\n", entryIdx, verifyIdx, actualIdx, itr->first);
			}
			++itr;
		}
	}
#endif

	BF_ASSERT(numRecordsRead == entryIdxMax);

	//OutputDebugStrF("HandleSymStreamEntries Ticks: %d\n", BFTickCount() - tickStart);

	return dataEnd;
}

void COFF::ParseSymbolStream(CvSymStreamType symStreamType)
{
	if (mCvSymbolRecordStream == 0)
		return;

	BP_ZONE("COFF::ParseSymbolStream");

	if (!mCvSymbolRecordReader.IsSetup())
		CvReadStream(mCvSymbolRecordStream, mCvSymbolRecordReader);

	if ((symStreamType == CvSymStreamType_Globals_Scan) || (symStreamType == CvSymStreamType_Globals_Targeted))
	{
		int streamSize = 0;
		uint8* data;
		if (mCvGlobalSymbolData == NULL)
			mCvGlobalSymbolData = CvReadStream(mCvGlobalSymbolInfoStream, &streamSize);
		data = mCvGlobalSymbolData;

		HandleSymStreamEntries(symStreamType, data, NULL);
	}
	else
	{
		int streamSize = 0;
		uint8* data;
		if (mCvPublicSymbolData == NULL)
			mCvPublicSymbolData = CvReadStream(mCvPublicSymbolInfoStream, &streamSize); // psgsi
		data = mCvPublicSymbolData;
		uint8* sectionData = data;

		GET_INTO(int32, symHashSize);
		GET_INTO(int32, addrMapSize);
		GET_INTO(int32, thunkCount);
		GET_INTO(int32, thunkSize);
		GET_INTO(int32, thunkTableStream);
		GET_INTO(int32, thunkTableOfs);
		GET_INTO(int32, thunkSectCount);

		data = HandleSymStreamEntries(symStreamType, data, data + symHashSize);

		//TODO: Hm, not quite right?
		/*int addrMapEntryCount = addrMapSize / 4;
		for (int addrIdx = 0; addrIdx < addrMapEntryCount; addrIdx++)
		{
			GET_INTO(int32, addrVal);
		}

		for (int thunkIdx = 0; thunkIdx < thunkCount; thunkIdx++)
		{
			GET_INTO(int32, thunkVal);
		}

		for (int sectionIdx = 0; sectionIdx < thunkSectCount; sectionIdx++)
		{
			GET_INTO(int32, ofs);
			GET_INTO(int32, sectNum);
		}*/
	}
}

void COFF::Fail(const StringImpl& error)
{
	DbgModule::Fail(StrFormat("%s in %s", error.c_str(), mPDBPath.c_str()));
}

void COFF::SoftFail(const StringImpl& error)
{
	DbgModule::SoftFail(StrFormat("%s in %s", error.c_str(), mPDBPath.c_str()));
}

void COFF::HardFail(const StringImpl& error)
{
	DbgModule::HardFail(StrFormat("%s in %s", error.c_str(), mPDBPath.c_str()));
}

void COFF::ParseGlobalsData()
{
	if (!mPDBLoaded)
		return;
	if (mParsedGlobalsData)
		return;
	ParseTypeData();
	mParsedGlobalsData = true;

	//gDbgPerfManager->StartRecording();

	int startTypeIdx = (int)mTypes.size();

	ParseSymbolStream(CvSymStreamType_Globals_Scan);

	//gDbgPerfManager->StopRecording(true);

	//int addedTypes = (int)mTypes.size() - startTypeIdx;
	//OutputDebugStrF("Types Added: %d ProcSymCount: %d\n", addedTypes, mProcSymCount);
}

void COFF::ParseSymbolData()
{
	if (!mPDBLoaded)
		return;
	if (mParsedSymbolData)
		return;
	ParseGlobalsData();
	mParsedSymbolData = true;
	ParseSymbolStream(CvSymStreamType_Symbols);
}

bool COFF::ParseCv(DataStream& pdbFS, uint8* rootDirData, int pageSize, uint8 wantGuid[16], int32 wantAge)
{
	BP_ZONE("ParseCv");

	mParsedGlobalsData = false;
	mParsedSymbolData = false;
	mParsedTypeData = false;
	mPopulatedStaticVariables = false;

	InitCvTypes();

	int startingTypeIdx = mTypes.size();

	uint8* data = rootDirData;

	bool failed = false;
	int numStreams = GET(int32);
	if (numStreams == 0)
		return true;

	mCvStreamSizes.Resize(numStreams);
	mCvStreamPtrStartIdxs.Resize(numStreams);

	int streamPages = 0;
	for (int i = 0; i < (int)mCvStreamSizes.size(); i++)
		mCvStreamSizes[i] = GET(int32);
	for (int streamIdx = 0; streamIdx < numStreams; streamIdx++)
	{
		mCvStreamPtrStartIdxs[streamIdx] = streamPages;
		if (mCvStreamSizes[streamIdx] > 0)
			streamPages += (mCvStreamSizes[streamIdx] + pageSize - 1) / pageSize;
	}
	mCvStreamPtrs.Resize(streamPages);
	for (int i = 0; i < (int)mCvStreamPtrs.size(); i++)
		mCvStreamPtrs[i] = GET(int32);

	//////////////////////////////////////////////////////////////////////////

	if (!CvParseHeader(wantGuid, wantAge))
		return false;

	if (!CvParseDBI(wantAge))
		return false;

	if (mParseKind == ParseKind_Header)
		return true;

	///
	{
		BP_ZONE("COFF::ParseCv_ReadStrTable");
		if (mStringTable.mStream != -1)
		{
			mCvStrTableData = CvReadStream(mStringTable.mStream);
			mStringTable.mStrTable = (const char*)mCvStrTableData + 12;
		}
	}

	for (int compileUnitId = 0; compileUnitId < (int)mCvModuleInfo.size(); compileUnitId++)
	{
		ScanCompileUnit(compileUnitId);
	}

	//////////////////////////////////////////////////////////////////////////

	return true;
}

void COFF::ReportMemory(MemReporter* memReporter)
{
	DbgModule::ReportMemory(memReporter);

	memReporter->AddVec("mCvTypeMap", mCvTypeMap);
	memReporter->AddVec("mCvTagStartMap", mCvTagStartMap);
	memReporter->AddVec("mCvIPITagStartMap", mCvIPITagStartMap);
	memReporter->AddVec(mCvStreamSizes);
	memReporter->AddVec(mCvStreamPtrStartIdxs);
	memReporter->AddVec(mCvStreamPtrs);
	memReporter->AddVec(mCvModuleInfo);
	memReporter->AddVec(mCVSrcFileRefCache);

	if (mCvHeaderData != NULL)
		memReporter->Add("mCvHeaderData", mCvStreamSizes[1]);
	for (auto& entry : mCvTypeSectionData)
	{
		if (entry.mSize != -1)
			memReporter->Add("mCvTypeSectionData", entry.mSize);
		else
			memReporter->Add("mCvTypeSectionData", mCvStreamSizes[2]);
	}
	if (mCvStrTableData != NULL)
		memReporter->Add("mCvStrTableData", mCvStreamSizes[mStringTable.mStream]);
	if (mCvPublicSymbolData != NULL)
		memReporter->Add("mCvPublicSymbolData", mCvStreamSizes[mCvPublicSymbolInfoStream]);
	if (mCvGlobalSymbolData != NULL)
		memReporter->Add("mCvGlobalSymbolData", mCvStreamSizes[mCvGlobalSymbolInfoStream]);

	//memReporter->AddVec(mCvNamespaceStaticEntries);
}

bool COFF::TryLoadPDB(const String& pdbPath, uint8 wantGuid[16], int32 wantAge)
{
	bool isVerifyOnly = mDebugger == NULL;

	if (!mPDBPath.IsEmpty())
		return false; // Already have a PDB loaded

	mCvMappedFile = CreateFileA(pdbPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (mCvMappedFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	BP_ZONE("COFF::TryLoadPDB");

	if (!isVerifyOnly)
		mDebugger->OutputMessage(StrFormat("Loading PDB %s\n", pdbPath.c_str()));

	DWORD highFileSize = 0;
	int fileSize = (int)GetFileSize(mCvMappedFile, &highFileSize);
	mCvMappedFileMapping = CreateFileMapping(mCvMappedFile, NULL, PAGE_READONLY, 0, fileSize, NULL);
	mCvMappedViewOfFile = MapViewOfFile(mCvMappedFileMapping, FILE_MAP_READ, 0, 0, fileSize);
	mCvMappedFileSize = fileSize;

	//FileStream pdbFS;
	//if (!pdbFS.Open(pdbPath, "rb"))
		//return false;

	mPDBPath = pdbPath;
	mCvDataStream = new SafeMemStream(mCvMappedViewOfFile, fileSize, false);

	//Cv_HEADER
	//pdbFS.Read

	char sig[32];
	mCvDataStream->Read(sig, 32);
	if (memcmp(sig, MSF_SIGNATURE_700, 32) != 0)
	{
		if (!isVerifyOnly)
			Fail("PDB signature error");
		return false;
	}

	BfLogDbg("Loading PDB %s\n", pdbPath.c_str());

	BP_ZONE_F("LoadCv %s", pdbPath.c_str());

	int pageSize = mCvDataStream->ReadInt32();
	int fpmPageNum = mCvDataStream->ReadInt32();
	int totalPageCount = mCvDataStream->ReadInt32();
	int rootDirSize = mCvDataStream->ReadInt32();
	int unknown = mCvDataStream->ReadInt32();

	int rootDirPtrs[NUM_ROOT_DIRS];
	mCvDataStream->ReadT(rootDirPtrs);

	bool failed = false;

	mCvPageSize = pageSize;
	for (int i = 0; i < 31; i++)
	{
		if (mCvPageSize < (1 << i))
			break;
		mCvPageBits = i;
	}

	int rootPageCount = (rootDirSize + pageSize - 1) / pageSize;
	int rootPointersPages = (rootPageCount*sizeof(int32) + pageSize - 1) / pageSize;
	int rootPageIdx = 0;

	Array<uint8> rootDirData;
	rootDirData.Resize(rootPageCount * pageSize);

	for (int rootDirIdx = 0; rootDirIdx < rootPointersPages; rootDirIdx++)
	{
		int rootDirPtr = rootDirPtrs[rootDirIdx];
		if (rootDirPtr == 0)
			continue;
		mCvDataStream->SetPos(rootDirPtr * pageSize);

		int32 rootPages[1024];
		mCvDataStream->Read(rootPages, pageSize);

		for (int subRootPageIdx = 0; subRootPageIdx < pageSize / 4; subRootPageIdx++, rootPageIdx++)
		{
			if (rootPageIdx >= rootPageCount)
				break;

			int rootPagePtr = rootPages[subRootPageIdx];
			if (rootPagePtr == 0)
				break;
			mCvDataStream->SetPos(rootPagePtr * pageSize);
			mCvDataStream->Read(&rootDirData[rootPageIdx * pageSize], pageSize);
		}
	}

	int startingTypeIdx = mTypes.size();
	if (!ParseCv(*mCvDataStream, &rootDirData[0], pageSize, wantGuid, wantAge))
	{
		//mDebugger->OutputMessage("Failed to parse PDB\n");
		return false;
	}

	if (mCvDataStream->mFailed)
		return false;

	//OutputDebugStrF("COFF::TryLoadPDB %s\n", pdbPath.c_str());
	if (!isVerifyOnly)
		mDebugger->ModuleChanged(this);

	mPDBLoaded = true;

	return true;
}

void COFF::ClosePDB()
{
	delete mCvDataStream;
	mCvDataStream = NULL;
	delete mCvHeaderData;
	mCvHeaderData = NULL;
	delete mCvStrTableData;
	mCvStrTableData = NULL;
	for (auto& entry : mCvTypeSectionData)
		delete entry.mData;
	mCvTypeSectionData.Clear();
	for (auto& entry : mCvCompileUnitData)
		delete entry.mData;
	mCvCompileUnitData.Clear();
	delete mCvPublicSymbolData;
	mCvPublicSymbolData = NULL;
	delete mCvGlobalSymbolData;
	mCvGlobalSymbolData = NULL;
	delete mNewFPOData;
	mNewFPOData = NULL;
	if (mCvMappedViewOfFile != NULL)
		::UnmapViewOfFile(mCvMappedViewOfFile);
	mCvMappedViewOfFile = NULL;
	if (mCvMappedFileMapping != INVALID_HANDLE_VALUE)
		::CloseHandle(mCvMappedFileMapping);
	mCvMappedFileMapping = NULL;
	if (mCvMappedFile != INVALID_HANDLE_VALUE)
		::CloseHandle(mCvMappedFile);
	mCvMappedFile = NULL;

	for (auto kv : mHotLibMap)
		delete kv.mValue;
	mHotLibMap.Clear();
	mHotLibSymMap.Clear();

	delete mEmitSourceFile;
	mEmitSourceFile = NULL;
}

bool COFF::LoadPDB(const String& pdbPath, uint8 wantGuid[16], int32 wantAge)
{
	if (mDebugTarget->mTargetBinary == this)
	{
		// If we don't have to load the debug symbols from a remote source or the cache
		//  then we assume we were locally built, meaning we don't attempt to auto-load
		//  any symbols since we assume we already have what we built for all referenced
		//  dlls
		mDebugTarget->mWasLocallyBuilt = FileExists(pdbPath);
	}

	memcpy(mWantPDBGuid, wantGuid, 16);
	mWantAge = wantAge;

	mDbgSymRequest = mDebugger->mDbgSymSrv.CreateRequest(mFilePath, pdbPath, wantGuid, wantAge);
	mDbgSymRequest->SearchLocal();

	if (!mDbgSymRequest->mFinalPDBPath.IsEmpty())
	{
		TryLoadPDB(mDbgSymRequest->mFinalPDBPath, wantGuid, wantAge);
		mDebugger->mDbgSymSrv.ReleaseRequest(mDbgSymRequest);
		mDbgSymRequest = NULL;
	}
	else
		mMayBeOld = true;

	String fileName = GetFileName(mFilePath);
	mWantsAutoLoadDebugInfo = !mDebugTarget->mWasLocallyBuilt;

	if ((fileName.Equals("KERNEL32.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("KERNELBASE.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("USER32.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("SHELL32.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("GDI32.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("GDI32FULL.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
		(fileName.Equals("NTDLL.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("COMDLG32.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("MSVCRT.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("COMBASE.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("UCRTBASE.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("RPCRT4.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("BCRYPTPRIMITIVES.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("SHCORE.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("WIN32U.DLL", String::CompareKind_OrdinalIgnoreCase)) ||
        (fileName.Equals("MSWSOCK.DLL", String::CompareKind_OrdinalIgnoreCase)))
	{
		mWantsAutoLoadDebugInfo = false;
	}

	/*if (pdbPath.IndexOf('\\') != -1) // Do we have an absolute path at all?
	{
		if (TryLoadPDB(pdbPath, wantGuid, wantAge))
			return true;
		ClosePDB();
	}*/

	//char exePath[MAX_PATH];
	//GetModuleFileNameA(NULL, exePath, sizeof(exePath));

	/*String checkPath = ::GetFileDir(mFilePath);
	checkPath += "/";
	checkPath += GetFileName(pdbPath);

	if (!FileNameEquals(checkPath, pdbPath))
	{
		if (TryLoadPDB(checkPath, wantGuid, wantAge))
			return true;
	}*/

	return false;
}

bool COFF::CheckSection(const char* name, uint8* sectionData, int sectionSize)
{
	if (strcmp(name, ".debug$T") == 0)
	{
		mDbgFlavor = DbgFlavor_MS;
		DbgSectionData entry;
		entry.mData = sectionData;
		entry.mSize = sectionSize;
		mCvTypeSectionData.Add(entry);
		return true;
	}

	if (strcmp(name, ".debug$S") == 0)
	{
		DbgSectionData entry;
		entry.mData = sectionData;
		entry.mSize = sectionSize;
		mCvCompileUnitData.Add(entry);
		return true;
	}

	return false;
}

void COFF::ProcessDebugInfo()
{
	BP_ZONE("COFF::ProcessDebugInfo");

	if ((!mCvTypeSectionData.IsEmpty()) && (!mCvCompileUnitData.IsEmpty()))
	{
		auto linkedModule = (COFF*)GetLinkedModule();
		int startingTypeIdx = (int)linkedModule->mTypes.size();

		InitCvTypes();

		for (auto entry : mCvTypeSectionData)
		{
			uint8* data = entry.mData;
			GET_INTO(uint32, infoType);
			BF_ASSERT(infoType == CV_SIGNATURE_C13);

			CvInitStreamRaw(mCvTypeSectionReader, entry.mData + 4, entry.mSize - 4);
			ParseTypeData(mCvTypeSectionReader, 0);
		}

		FixTypes(startingTypeIdx);
		linkedModule->MapTypes(startingTypeIdx);

		CvCompileUnit* compileUnit = NULL;
		for (auto entry : mCvCompileUnitData)
		{
			compileUnit = ParseCompileUnit(NULL, compileUnit, entry.mData, entry.mSize);
		}
		if (compileUnit != NULL)
		{
			compileUnit->mLanguage = DbgLanguage_Beef;
			mMasterCompileUnit->mLanguage = DbgLanguage_Beef;
			MapCompileUnitMethods(compileUnit);
			mEndTypeIdx = (int)linkedModule->mTypes.size();
		}
	}
}

void COFF::FinishHotSwap()
{
	DbgModule::FinishHotSwap();
	mTypeMap.Clear();
}

intptr COFF::EvaluateLocation(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, DbgAddrType* outAddrType, DbgEvalLocFlags flags)
{
	if (mDbgFlavor == DbgFlavor_GNU)
		return DbgModule::EvaluateLocation(dwSubprogram, locData, locDataLen, stackFrame, outAddrType, flags);

	if ((locDataLen == 0) && (locData != NULL))
	{
		// locData is actually a string, the aliased name
		*outAddrType = DbgAddrType_Alias;
		return (intptr)locData;
	}

	addr_target pc = 0;
	if (stackFrame != NULL)
	{
		// Use 'GetSourcePC', which will offset the RSP when we're not at the top position of the call stack, since RSP will be the
		//  return address in those cases
		pc = stackFrame->GetSourcePC();
	}

	int inlineDepth = 0;
	uint8* data = (uint8*)locData;
	for (int locIdx = 0; locIdx < locDataLen; locIdx++)
	{
		uint8* dataStart = data;
		GET_INTO(uint16, symLen);
		uint8* dataEnd = data + symLen;
		GET_INTO(uint16, symType);

		CV_LVAR_ADDR_RANGE* rangeInfo = NULL;
		CV_LVAR_ADDR_GAP* gapsInfo = NULL;
		addr_target result = 0;

		if (inlineDepth > 0)
		{
			if (symType == S_INLINESITE)
				inlineDepth++;
			else if (symType == S_INLINESITE_END)
				inlineDepth--;
			data = dataEnd;
			continue;
		}

		bool handled = false;
		switch (symType)
		{
		case S_GDATA32:
		case S_LDATA32:
			{
				DATASYM32& dataSym = *(DATASYM32*)dataStart;
				*outAddrType = DbgAddrType_Target;
				return GetSectionAddr(dataSym.seg, dataSym.off);
			}
			break;
		case S_BPREL32:
			{
				BPRELSYM32* bpRel32 = (BPRELSYM32*)dataStart;
#if BF_DBG_32
				*outAddrType = DbgAddrType_Target;
				return stackFrame->mRegisters.mIntRegs.ebp + (int)bpRel32->off;
#else

				*outAddrType = DbgAddrType_Target;
				return stackFrame->mRegisters.mIntRegs.rbp + (int)bpRel32->off;
#endif
			}
			break;
		case S_LTHREAD32:
		case S_GTHREAD32:
			{
				THREADSYM32& threadSym = *(THREADSYM32*)dataStart;

				int tlsIndex = mDebugger->ReadMemory<int>(mTLSIndexAddr);
				addr_target tlsEntry = mDebugger->GetTLSOffset(tlsIndex);

				*outAddrType = DbgAddrType_Target;
				return threadSym.off + tlsEntry;
			}
			break;
		case S_REGISTER:
			{
				REGSYM* regSym = (REGSYM*)dataStart;
				*outAddrType = DbgAddrType_Register;
				int regNum = CvConvRegNum(regSym->reg);
				return regNum;
			}
			break;
		case S_REGREL32:
			{
				REGREL32* regRel32 = (REGREL32*)dataStart;
				*outAddrType = DbgAddrType_Target;
				int regNum = CvConvRegNum(regRel32->reg);
				return stackFrame->mRegisters.mIntRegsArray[regNum] + regRel32->off;
			}
			break;
		case S_DEFRANGE_REGISTER:
			{
				DEFRANGESYMREGISTER& defRangeReg = *(DEFRANGESYMREGISTER*)dataStart;
				*outAddrType = DbgAddrType_Register;
				result = CvConvRegNum(defRangeReg.reg);
				rangeInfo = &defRangeReg.range;
				gapsInfo = &defRangeReg.gaps[0];
			}
			break;
		case S_DEFRANGE_FRAMEPOINTER_REL:
			{
				DEFRANGESYMFRAMEPOINTERREL& defRangeFPRel = *(DEFRANGESYMFRAMEPOINTERREL*)dataStart;

				DbgSubprogram::LocalBaseRegKind baseReg = ((flags & DbgEvalLocFlag_IsParam) != 0) ? dwSubprogram->mParamBaseReg : dwSubprogram->mLocalBaseReg;

				*outAddrType = DbgAddrType_Target;
#ifdef BF_DBG_64
				if (baseReg == DbgSubprogram::LocalBaseRegKind_RSP)
					result = stackFrame->mRegisters.mIntRegsArray[X64Reg_RSP] + defRangeFPRel.offFramePointer;
				else if (baseReg == DbgSubprogram::LocalBaseRegKind_R13)
					result = stackFrame->mRegisters.mIntRegsArray[X64Reg_R13] + defRangeFPRel.offFramePointer;
				else
					result = stackFrame->mRegisters.mIntRegsArray[X64Reg_RBP] + defRangeFPRel.offFramePointer;
#else
				if (baseReg == DbgSubprogram::LocalBaseRegKind_VFRAME)
					result = stackFrame->mRegisters.mIntRegsArray[X86Reg_ESP] + dwSubprogram->mFrameBaseLen + defRangeFPRel.offFramePointer;
				else if (baseReg == DbgSubprogram::LocalBaseRegKind_EBX)
					result = stackFrame->mRegisters.mIntRegsArray[X86Reg_EBX] + defRangeFPRel.offFramePointer;
				else
					result = stackFrame->mRegisters.mIntRegsArray[X86Reg_EBP] + defRangeFPRel.offFramePointer;
#endif

				rangeInfo = &defRangeFPRel.range;
				gapsInfo = &defRangeFPRel.gaps[0];
			}
			break;
		case S_DEFRANGE_SUBFIELD_REGISTER:
			{
				DEFRANGESYMSUBFIELDREGISTER& defRangeSubfieldReg = *(DEFRANGESYMSUBFIELDREGISTER*)dataStart;

				*outAddrType = DbgAddrType_Target;
				int regNum = CvConvRegNum(defRangeSubfieldReg.reg);
				result = stackFrame->mRegisters.mIntRegsArray[regNum] + defRangeSubfieldReg.offParent;

				rangeInfo = &defRangeSubfieldReg.range;
				gapsInfo = &defRangeSubfieldReg.gaps[0];
			}
			break;
		case S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
			{
				DEFRANGESYMFRAMEPOINTERREL_FULL_SCOPE& defFPRel = *(DEFRANGESYMFRAMEPOINTERREL_FULL_SCOPE*)dataStart;

#ifdef BF_DBG_64
				int regNum = X64Reg_RSP;
#else
				int regNum = X86Reg_ESP;
#endif
				*outAddrType = DbgAddrType_Target;
				result = stackFrame->mRegisters.mIntRegsArray[regNum] + defFPRel.offFramePointer;
				return result;
			}
			break;
		case S_DEFRANGE_REGISTER_REL:
			{
				DEFRANGESYMREGISTERREL& defRangeRegRel = *(DEFRANGESYMREGISTERREL*)dataStart;
				*outAddrType = DbgAddrType_Target;
				int regNum = CvConvRegNum(defRangeRegRel.baseReg);
				result = stackFrame->mRegisters.mIntRegsArray[regNum] + defRangeRegRel.offBasePointer;
				rangeInfo = &defRangeRegRel.range;
				gapsInfo = &defRangeRegRel.gaps[0];
			}
			break;
		case S_PROCREF:
			{
				// Not currently handled.
			}
			break;
		case S_INLINESITE:
			{
				inlineDepth++;
			}
			break;
		case S_FILESTATIC:
			{
				FILESTATICSYM& fileStaticSym = *(FILESTATICSYM*)dataStart;
			}
			break;
		default:
			if (!mFailed)
				SoftFail(StrFormat("Unknown symbol type '0x%X' in EvaluateLocation", symType));
			return 0;
		}

		if (rangeInfo != NULL)
		{
			auto rangeStart = GetSectionAddr(rangeInfo->isectStart, rangeInfo->offStart);

			if ((pc >= rangeStart) && (pc < rangeStart + rangeInfo->cbRange))
			{
				bool inRange = true;
				while ((uint8*)gapsInfo < (uint8*)dataEnd)
				{
					if ((pc >= rangeStart + gapsInfo->gapStartOffset) && (pc < rangeStart + gapsInfo->gapStartOffset + gapsInfo->cbRange))
					{
						inRange = false;
						break;
					}
					gapsInfo++;
				}

				if (inRange)
					return result;
			}
		}

		data = dataEnd;
	}

	if ((dwSubprogram != NULL) && (dwSubprogram->mIsOptimized))
		*outAddrType = DbgAddrType_OptimizedOut;
	else
		*outAddrType = DbgAddrType_NoValue;
	return 0;
}

bool COFF::CanGetOldSource()
{
	return mCvSrcSrvStream != -1;
}

String COFF::GetOldSourceCommand(const StringImpl& path)
{
	if (mCvSrcSrvStream == -1)
		return "";

	int outSize;
	uint8* data = CvReadStream(mCvSrcSrvStream, &outSize);
	String cmdBlock = String((char*)data, outSize);
	delete data;

	bool inFileSection = false;
	Dictionary<String, String> defs;

	enum _SectType
	{
		_SectType_None,
		_SectType_Ini,
		_SectType_Variables,
		_SectType_SourceFiles
	};
	_SectType sectType = _SectType_None;

	//
	{
		AutoCrit autoCrit(gDebugManager->mCritSect);
		defs["TARG"] = gDebugManager->mSymSrvOptions.mSourceServerCacheDir;
	}

	std::function<void (String&)> _Expand = [&](String& str)
	{
		for (int i = 0; i < str.length(); i++)
		{
			if (str[i] == '%')
			{
				int endIdx = str.IndexOf('%', i + 1);
				if (endIdx != -1)
				{
					String varName = ToUpper(str.Substring(i + 1, endIdx - i - 1));
					if (((endIdx < str.length() - 1) && (str[endIdx + 1] == '(')) &&
						((varName == "FNVAR") || (varName == "FNBKSL") || (varName == "FNFILE")))
					{
						int closePos = str.IndexOf(')', endIdx + 2);
						if (closePos != -1)
						{
							String paramStr = str.Substring(endIdx + 2, closePos - endIdx - 2);
							_Expand(paramStr);

							if (varName == "FNVAR")
							{
								paramStr = defs[paramStr];
							}
							else
							{
								if (varName == "FNBKSL")
								{
									paramStr.Replace("/", "\\");
								}
								else if (varName == "FNFILE")
								{
									paramStr = GetFileName(paramStr);
								}
							}

							str.Remove(i, closePos - i + 1);
							str.Insert(i, paramStr);
							i--;
						}
					}
					else
					{
						auto replaceStr = defs[varName];
						if (!replaceStr.IsEmpty())
						{
							str.Remove(i, endIdx - i + 1);
							str.Insert(i, replaceStr);
							i--;
						}
					}
				}
			}
		}
	};

	int linePos = 0;
	while (true)
	{
		int crPos = cmdBlock.IndexOf('\n', linePos);
		if (crPos == -1)
			break;
		int lineEndPos = crPos;
		if (cmdBlock[lineEndPos - 1] == '\r')
			lineEndPos--;

		String line = cmdBlock.Substring(linePos, lineEndPos - linePos);

		if (line.StartsWith("SRCSRV: ini -"))
		{
			sectType = _SectType_Ini;
		}
		else if (line.StartsWith("SRCSRV: variables -"))
		{
			sectType = _SectType_Variables;
		}
		else if (line.StartsWith("SRCSRV: source files -"))
		{
			sectType = _SectType_SourceFiles;
		}
		else if (line.StartsWith("SRCSRV: end -"))
		{
			break;
		}
		else
		{
			switch (sectType)
			{
			case _SectType_Ini:
			case _SectType_Variables:
				{
					int eqPos = line.IndexOf('=');
					if (eqPos != -1)
					{
						defs[ToUpper(line.Substring(0, eqPos))] = line.Substring(eqPos + 1);
					}
					break;
				}
			case _SectType_SourceFiles:
				{
					int curStrIdx = 0;
					int curStrPos = 0;

					StringT<256> var;

					bool matches = false;
					while (curStrPos < (int)line.length())
					{
						int starPos = line.IndexOf('*', curStrPos);
						if (starPos == -1)
							starPos = line.length();

						//var = line.Substring(curStrPos, starPos - curStrPos);
						var.Clear();
						var.Insert(0, line.c_str() + curStrPos, starPos - curStrPos);

						if (curStrIdx == 0)
						{
							matches = FileNameEquals(var, path);
							if (!matches)
								break;
						}

						defs[StrFormat("VAR%d", curStrIdx + 1)] = var;

						curStrIdx++;
						curStrPos = starPos + 1;
					}

					if (!matches)
						break;

					String target = defs["SRCSRVTRG"];
					String cmd = defs["SRCSRVCMD"];
					String env = defs["SRCSRVENV"];

					_Expand(target);
					_Expand(cmd);
					_Expand(env);

					String retVal;
					if ((cmd.IsEmpty()) && (target.StartsWith("HTTP", StringImpl::CompareKind_OrdinalIgnoreCase)))
					{
						String localFile;

						BfpFileResult result;
						BFP_GETSTR_HELPER(localFile, result, BfpFile_GetTempPath(__STR, __STRLEN, &result));
						int dotPos = target.IndexOf("://");
						if (dotPos != -1)
						{
							localFile.Append("SymbolCache\\src\\");
							localFile.Append(StringView(target, dotPos + 3));
							localFile.Replace("/", "\\");
						}

						retVal = localFile;
						retVal += "\n";
						retVal += target;
						retVal += "\n";
						retVal += env;
					}
					else if (!cmd.IsWhitespace())
					{
						retVal = target;
						retVal += "\n";
						retVal += cmd;
						retVal += "\n";
						retVal += env;
					}

					return retVal;
				}
				break;
			}
		}

		linePos = crPos + 1;
	}

	return "";
}

bool COFF::GetEmitSource(const StringImpl& filePath, String& outText)
{
	if (!filePath.StartsWith("$Emit"))
		return false;

	if (mEmitSourceFile == NULL)
	{
		mEmitSourceFile = new ZipFile();

		String zipPath = mPDBPath;
		int dotPos = zipPath.LastIndexOf('.');
		zipPath.RemoveToEnd(dotPos);
		zipPath.Append("__emit.zip");
		if (!mEmitSourceFile->Open(zipPath))
		{
			if (mCvEmitStream == -1)
				return "";

			int outSize;
			uint8* data = CvReadStream(mCvEmitStream, &outSize);

			FileStream fileStream;
			fileStream.Open(zipPath, "wb");
			fileStream.Write(data, outSize);
			fileStream.Close();

			delete data;

			mEmitSourceFile->Open(zipPath);
		}
	}

	if (mEmitSourceFile->IsOpen())
	{
		String usePath = filePath;
		if (usePath.StartsWith("$Emit"))
		{
			int dollarPos = usePath.IndexOf('$', 1);
			usePath.Remove(0, dollarPos + 1);
		}
		usePath = EncodeFileName(usePath);
		usePath.Append(".bf");

		Array<uint8> data;
		if (mEmitSourceFile->Get(usePath, data))
		{
			outText.Insert(outText.mLength, (char*)data.mVals, data.mSize);
			return true;
		}
	}

	return false;
}

bool COFF::HasPendingDebugInfo()
{
	if (mDbgSymRequest == NULL)
		return false;

	return true;
}

void COFF::PreCacheImage()
{
	if (!mDebugger->IsMiniDumpDebugger())
		return;

	if (mLoadState != DbgModuleLoadState_NotLoaded)
		return;

	auto miniDumpDebugger = (MiniDumpDebugger*)mDebugger;

	if (mOrigImageData != NULL)
		return;

	mDebugger->mDbgSymSrv.PreCacheImage(mFilePath, mTimeStamp, mImageSize);
}

void COFF::PreCacheDebugInfo()
{
	if (mDbgSymRequest == NULL)
		return;

	if (mDbgSymRequest->mInProcess)
		return;

	bool hasDebugInfo = false;
	mDbgSymRequest->SearchCache();
	if (mDbgSymRequest->mFinalPDBPath.IsEmpty())
	{
		if (GetCurrentThreadId() == mDebugger->mDebuggerThreadId)
		{
			mDbgSymRequest->mIsPreCache = true;
			mDbgSymRequest->SearchSymSrv();
			mDbgSymRequest->mIsPreCache = false;
			mDbgSymRequest->mFailed = false;
		}
	}
}

bool COFF::LoadModuleImage(const StringImpl& imagePath)
{
	if (!mDebugger->IsMiniDumpDebugger())
		return false;

	auto miniDumpDebugger = (MiniDumpDebugger*)mDebugger;

	if (!imagePath.IsEmpty())
	{
		MappedFile* mappedFile = miniDumpDebugger->MapModule(this, imagePath);
		mMappedImageFile = mappedFile;

		if (mappedFile != NULL)
		{
			MemStream memStream(mappedFile->mData, mappedFile->mFileSize, false);
			ReadCOFF(&memStream, DbgModuleKind_Module);

			mOrigImageData = new DbgModuleMemoryCache(mImageBase, mImageSize);
		}
	}

	if (mOrigImageData == NULL) // Failed?
	{
		mLoadState = DbgModuleLoadState_Failed;
		return false;
	}

	return true;
}

bool COFF::RequestImage()
{
	if (!mDebugger->IsMiniDumpDebugger())
		return false;

	if (mLoadState != DbgModuleLoadState_NotLoaded)
		return false;

	auto miniDumpDebugger = (MiniDumpDebugger*)mDebugger;

	if (mOrigImageData != NULL)
		return false;

	if (GetCurrentThreadId() == mDebugger->mDebuggerThreadId)
	{
		auto prevRunState = mDebugger->mRunState;
		mDebugger->mRunState = RunState_SearchingSymSrv;
		mDebugger->mDebugManager->mOutMessages.push_back(StrFormat("symsrv Searching for image '%s'", mFilePath.c_str()));

		auto dbgSymRequest = mDebugger->mDbgSymSrv.CreateRequest();
		mDebugger->mActiveSymSrvRequest = dbgSymRequest;
		BF_ASSERT(mDebugger->mDebugManager->mCritSect.mLockCount == 1);
		mDebugger->mDebugManager->mCritSect.Unlock();
		// We unlock to allow the IDE to continue updating while we search
		String imagePath = dbgSymRequest->SearchForImage(mFilePath, mTimeStamp, mImageSize);
		mDebugger->mDebugManager->mCritSect.Lock();
		delete mDebugger->mActiveSymSrvRequest;
		mDebugger->mActiveSymSrvRequest = NULL;

		mDebugger->mRunState = prevRunState;

		return LoadModuleImage(imagePath);
	}
	else
	{
		return false;
	}

	return false;
}

bool COFF::RequestDebugInfo(bool allowRemote)
{
	if (mDbgSymRequest == NULL)
		return false;

	if (mDbgSymRequest->mInProcess)
		return false;

	bool hasDebugInfo = false;
	mDbgSymRequest->SearchCache();
	if (mDbgSymRequest->mFinalPDBPath.IsEmpty())
	{
		if (!allowRemote)
			return false;

		if (GetCurrentThreadId() == mDebugger->mDebuggerThreadId)
		{
			auto prevRunState = mDebugger->mRunState;
			mDebugger->mRunState = RunState_SearchingSymSrv;

			mDbgSymRequest->mInProcess = true;
			mDebugger->mActiveSymSrvRequest = mDbgSymRequest;
			BF_ASSERT(mDebugger->mDebugManager->mCritSect.mLockCount == 1);
			mDebugger->mDebugManager->mCritSect.Unlock();
			mDbgSymRequest->SearchSymSrv();
			mDebugger->mDebugManager->mCritSect.Lock();
			mDebugger->mActiveSymSrvRequest = NULL;
			mDbgSymRequest->mInProcess = false;

			mDebugger->mRunState = prevRunState;
		}
		else
		{
			return false;
		}
	}
	if (!mDbgSymRequest->mFinalPDBPath.IsEmpty())
		hasDebugInfo = TryLoadPDB(mDbgSymRequest->mFinalPDBPath, mDbgSymRequest->mWantGuid, mDbgSymRequest->mWantAge);
	mDebugger->mDbgSymSrv.ReleaseRequest(mDbgSymRequest);
	mDbgSymRequest = NULL;
	return hasDebugInfo;
}

bool COFF::WantsAutoLoadDebugInfo()
{
	return mWantsAutoLoadDebugInfo;
}

bool COFF::DbgIsStrMutable(const char* str)
{
	if (IsObjectFile())
	{
		return GetLinkedModule()->DbgIsStrMutable(str);
	}

	if (mCvMappedViewOfFile == NULL)
		return true;

	if ((str >= (const char*)mCvMappedViewOfFile) && (str < (const char*)mCvMappedViewOfFile + mCvMappedFileSize))
		return false;
	return true;
}

// We don't need this for Beef linkining because of "FORCELINK_", but we DO need it for
//  libraries we linked to, either in the DLL or in the LIB
addr_target COFF::LocateSymbol(const StringImpl& name)
{
	BP_ZONE("COFF::LocateSymbol");

	BfLogDbg("COFF:LocateSymbol '%s'\n", name.c_str());

	if (mHotLibMap.IsEmpty())
	{
		for (auto moduleInfo : mCvModuleInfo)
		{
			if (moduleInfo->mObjectName == NULL)
				continue;
			String libName = moduleInfo->mObjectName;
			if (!libName.EndsWith(".lib", StringImpl::CompareKind_OrdinalIgnoreCase))
				continue;

			CvLibInfo** libInfoPtr;
			if (!mHotLibMap.TryAdd(libName, NULL, &libInfoPtr))
				continue;

			CvLibInfo* libInfo = new CvLibInfo();
			*libInfoPtr = libInfo;

			if (!libInfo->mLibFile.Init(libName, false))
				continue;

			for (auto kv : libInfo->mLibFile.mOldEntries)
			{
				auto libEntry = kv.mValue;
				while (libEntry != NULL)
				{
					for (auto sym : libEntry->mSymbols)
					{
#ifdef BF_DBG_32
						if (sym.StartsWith('_'))
							mHotLibSymMap[sym.Substring(1)] = libEntry;
						else
							// Fallthrough
#endif
						mHotLibSymMap[sym] = libEntry;
					}
					libEntry = libEntry->mNextWithSameName;
				}
			}
		}
	}

	BeLibEntry* libEntry;
	if (!mHotLibSymMap.TryGetValue(name, &libEntry))
		return 0;

	if (libEntry->mLength == -1)
	{
		BfLogDbg("Already loaded obj '%s' in '%s'\n", libEntry->mName.c_str(), libEntry->mLibFile->mFilePath.c_str());

		// We already tried to load this
		return 0;
	}

	auto fileExt = GetFileExtension(libEntry->mName);
	if (String::Equals(fileExt, ".dll", StringImpl::CompareKind_OrdinalIgnoreCase))
	{
		for (auto dbgModule : mDebugTarget->mDbgModules)
		{
			if (String::Equals(libEntry->mName, dbgModule->mDisplayName))
			{
				dbgModule->ParseSymbolData();
				auto entry = dbgModule->mSymbolNameMap.Find(name.c_str());
				if (entry == NULL)
					continue;

				addr_target callTarget;

#ifdef BF_DBG_32
				callTarget = entry->mValue->mAddress;
#else

				BfLogDbg("Setting up DLL thunk for '%s' in '%s'\n", name.c_str(), dbgModule->mFilePath.c_str());

				int wantHotSize = 16;
				if (mHotThunkDataLeft < wantHotSize)
				{
					int allocSize = 4096;
					mDebugger->ReserveHotTargetMemory(allocSize);
					int outAllocSize = 0;
					mHotThunkCurAddr = mDebugger->AllocHotTargetMemory(allocSize, true, false, &outAllocSize);
					mHotThunkDataLeft = outAllocSize;
				}

				int outAllocSize = 0;
				addr_target hotAddr = mHotThunkCurAddr;
				mHotThunkCurAddr += wantHotSize;
				mHotThunkDataLeft -= wantHotSize;

#pragma pack(push, 1)
				struct HotLongJmpOp
				{
					uint8 mOpCode0;
					uint8 mOpCode1;
					int32 mRIPRel;
					uint64 mTarget;
				};
#pragma pack(pop)

				HotLongJmpOp jumpOp;
				jumpOp.mOpCode0 = 0xFF;
				jumpOp.mOpCode1 = 0x25;
				jumpOp.mRIPRel = 0;
				jumpOp.mTarget = entry->mValue->mAddress;
				mDebugger->WriteMemory(hotAddr, jumpOp);

				callTarget = hotAddr;
#endif
				char* dupStr = (char*)mAlloc.AllocBytes(name.length() + 1);
				memcpy(dupStr, name.c_str(), name.length());

				DbgSymbol* dwSymbol = mAlloc.Alloc<DbgSymbol>();
				dwSymbol->mDbgModule = this;
				dwSymbol->mName = dupStr;
				dwSymbol->mAddress = callTarget;
				mSymbolNameMap.Insert(dwSymbol);
				return callTarget;
			}
		}

		return 0;
	}
	BfLogDbg("Loading obj '%s' in '%s'\n", libEntry->mName.c_str(), libEntry->mLibFile->mFilePath.c_str());

// #ifdef _DEBUG
// 	FILE* fpTest = fopen("c:\\temp\\locateSym.obj", "wb");
//
// 	uint8* data = new uint8[libEntry->mLength];
//
// 	fseek(libEntry->mLibFile->mOldFileStream.mFP, libEntry->mOldDataPos + sizeof(BeLibMemberHeader), SEEK_SET);
// 	fread(data, 1, libEntry->mLength, libEntry->mLibFile->mOldFileStream.mFP);
// 	fwrite(data, 1, libEntry->mLength, fpTest);
// 	fclose(fpTest);
// 	delete data;
// #endif

	FileSubStream fileStream;
	fileStream.mFP = libEntry->mLibFile->mOldFileStream.mFP;
	fileStream.mOffset = libEntry->mOldDataPos + sizeof(BeLibMemberHeader);
	fileStream.mSize = libEntry->mLength;
	fileStream.SetPos(0);

	DbgModule* dbgModule = new COFF(mDebugger->mDebugTarget);
	dbgModule->mHotIdx = mDebugger->mActiveHotIdx;
	dbgModule->mFilePath = libEntry->mName + "@" + libEntry->mLibFile->mFilePath;
	bool success = dbgModule->ReadCOFF(&fileStream, DbgModuleKind_FromLocateSymbol);
	fileStream.mFP = NULL;

	// Mark as loaded
	libEntry->mLength = -1;

	if (!success)
	{
		BfLogDbg("Failed\n");

		Fail(StrFormat("Debugger failed to read binary '%s' in '%s'", libEntry->mName.c_str(), libEntry->mLibFile->mFilePath.c_str()));
		delete dbgModule;
		return 0;
	}
	mDebugger->mDebugTarget->AddDbgModule(dbgModule);

	auto symbolEntry = mSymbolNameMap.Find(name.c_str());
	if (symbolEntry != NULL)
		return symbolEntry->mValue->mAddress;

	return 0;
}

void COFF::ParseFrameDescriptors(uint8* data, int size, addr_target baseAddr)
{
	uint8* ptr = data;
	uint8* endPtr = data + size;

	Dictionary<int, COFFFrameProgram> programMap;
	Array<COFFFrameProgram::Command> cmds;

	while (ptr < endPtr)
	{
		COFFFrameDescriptor* frame = (COFFFrameDescriptor*)ptr;
		ptr += sizeof(COFFFrameDescriptor);

		COFFFrameDescriptorEntry entry;
		entry.mFrameDescriptor = frame;

		bool failed = false;

		COFFFrameProgram* programPtr = NULL;
		if (programMap.TryAdd(frame->mFrameFunc, NULL, &programPtr))
		{
			COFFFrameProgram program;

			cmds.Clear();
			String curCmd;
			const char* ptr = mStringTable.mStrTable + frame->mFrameFunc;
			while (true)
			{
				char c = *(ptr++);
				if ((c == ' ') || (c == 0))
				{
					if (curCmd == "$eip")
						cmds.Add(COFFFrameProgram::Command_EIP);
					else if ((curCmd == "$esp") || (curCmd == "$21"))
						cmds.Add(COFFFrameProgram::Command_ESP);
					else if ((curCmd == "$ebp") || (curCmd == "$22"))
						cmds.Add(COFFFrameProgram::Command_EBP);
					else if ((curCmd == "$eax") || (curCmd == "$17"))
						cmds.Add(COFFFrameProgram::Command_EAX);
					else if ((curCmd == "$ebx") || (curCmd == "$20"))
						cmds.Add(COFFFrameProgram::Command_EBX);
					else if ((curCmd == "$ecx") || (curCmd == "$18"))
						cmds.Add(COFFFrameProgram::Command_ECX);
					else if ((curCmd == "$edx") || (curCmd == "$19"))
						cmds.Add(COFFFrameProgram::Command_EDX);
					else if ((curCmd == "$esi") || (curCmd == "$23"))
						cmds.Add(COFFFrameProgram::Command_ESI);
					else if ((curCmd == "$edi") || (curCmd == "$24"))
						cmds.Add(COFFFrameProgram::Command_EDI);
					else if (curCmd == "$T0")
						cmds.Add(COFFFrameProgram::Command_T0);
					else if (curCmd == "$T1")
						cmds.Add(COFFFrameProgram::Command_T1);
					else if (curCmd == "$T2")
						cmds.Add(COFFFrameProgram::Command_T2);
					else if (curCmd == "$T3")
						cmds.Add(COFFFrameProgram::Command_T3);
					else if ((curCmd == ".raSearch") || (curCmd == ".raSearchStart"))
						cmds.Add(COFFFrameProgram::Command_RASearch);
					else if (curCmd == "+")
						cmds.Add(COFFFrameProgram::Command_Add);
					else if (curCmd == "-")
						cmds.Add(COFFFrameProgram::Command_Subtract);
					else if (curCmd == "@")
						cmds.Add(COFFFrameProgram::Command_Align);
					else if (curCmd == "=")
						cmds.Add(COFFFrameProgram::Command_Set);
					else if (curCmd == "^")
						cmds.Add(COFFFrameProgram::Command_Deref);
					else if (!curCmd.IsEmpty())
					{
						if ((curCmd[0] >= '0') && (curCmd[0] <= '9'))
						{
							int val = atoi(curCmd.c_str());
							if ((val >= 0) && (val <= 255))
							{
								cmds.Add(COFFFrameProgram::Command_Value8);
								cmds.Add((COFFFrameProgram::Command)val);
							}
							else
							{
								cmds.Add(COFFFrameProgram::Command_Value);
								cmds.Insert(cmds.size(), (COFFFrameProgram::Command*)&val, 4);
							}
						}
						else
						{
							failed = true;
							SoftFail(StrFormat("Invalid COFF frame program: %s", curCmd.c_str()));
						}
					}
					if (c == 0)
						break;
					curCmd.Clear();
				}
				else
					curCmd.Append(c);
			}
			if (failed)
				cmds.Clear();
			cmds.Add(COFFFrameProgram::Command_None);
			program.mCommands = (COFFFrameProgram::Command*)mAlloc.AllocBytes(cmds.size());
			memcpy(program.mCommands, &cmds[0], cmds.size());

			*programPtr = program;
		}

		entry.mProgram = *programPtr;
		mDebugTarget->mCOFFFrameDescriptorMap.insert(std::make_pair(baseAddr + frame->mRvaStart, entry));
	}

	mParsedFrameDescriptors = true;
}

void COFF::ParseFrameDescriptors()
{
	if (mParsedFrameDescriptors)
		return;
	if (mCvNewFPOStream == 0)
		return;

	int streamSize = 0;
	mNewFPOData = CvReadStream(mCvNewFPOStream, &streamSize);
	ParseFrameDescriptors(mNewFPOData, streamSize, mImageBase);
}

NS_BF_DBG_BEGIN
// void TestCoff(void* tdata, int tdataSize, void* cuData, int cuDataSize)
// {
// 	DebugTarget* debugTarget = new DebugTarget(NULL);
// 	{
// 		COFF coff(debugTarget);
// 		coff.mCvTypeSectionData = (uint8*)tdata;
// 		coff.mCvTypeSectionDataSize = tdataSize;
//
// 		coff.mCvCompileUnitData = (uint8*)cuData;
// 		coff.mCvCompileUnitDataSize = cuDataSize;
//
// 		coff.ProcessDebugInfo();
// 	}
// 	delete debugTarget;
// }

void TestPDB(const StringImpl& fileName, WinDebugger* debugger)
{
	DebugTarget* debugTarget = new DebugTarget(NULL);
	COFF coff(debugTarget);
	coff.mDebugger = debugger;
	uint8 wantGuid[16];
	coff.TryLoadPDB(fileName, wantGuid, -1);
	coff.ParseTypeData();
	coff.CvParseIPI();
	coff.ParseGlobalsData();
	coff.ParseSymbolData();
	for (int compileUnitId = 0; compileUnitId < (int)coff.mCvModuleInfo.size(); compileUnitId++)
		coff.ParseCompileUnit(compileUnitId);
}

NS_BF_DBG_END