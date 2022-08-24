//#include <direct.h>
#include "BfSystem.h"
#include "BfParser.h"
#include "BfCompiler.h"
#include "BfDefBuilder.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/UTF8.h"
#include "BfAutoComplete.h"
#include "BfResolvePass.h"
#include "MemReporter.h"
#include "BfIRCodeGen.h"
#include "BfIRBuilder.h"

#include "BeefySysLib/util/AllocDebug.h"

#define STB_SPRINTF_DECORATE(name) BF_stbsp_##name
#include "../../third_party/stb/stb_sprintf.h"

USING_NS_BF;
using namespace llvm;

#pragma warning(disable:4996)

void Beefy::DoBfLog(int fileIdx, const char* fmt ...)
{
	static int entryNum = 0;
	static bool onNewLine[10];
	entryNum++;

	static BfpFile* fp[10] = { NULL };
	static bool openedLog[10] = { false };
	static int64 logSize[10] = { 0 };
	static int logCount[10] = { 0 };

	if (logSize[fileIdx] >= 1 * 1024 * 1024 * 1024)
	{
		BfpFile_Release(fp[fileIdx]);
		openedLog[fileIdx] = false;
	}

	if (!openedLog[fileIdx])
	{
		openedLog[fileIdx] = true;
		logSize[fileIdx] = 0;

		char exeName[512];
		int len = 512;
		BfpSystem_GetExecutablePath(exeName, &len, NULL);

		String dbgName = exeName;
		int dotPos = (int)dbgName.IndexOf('.');
		if (dotPos != -1)
			dbgName.RemoveToEnd(dotPos);
		dbgName += StrFormat("_%d", fileIdx);

		if (logCount[fileIdx] > 0)
			dbgName += 'B';

		dbgName += ".txt";

		fp[fileIdx] = BfpFile_Create(dbgName.c_str(), BfpFileCreateKind_CreateAlways, (BfpFileCreateFlags)(BfpFileCreateFlag_Write | BfpFileCreateFlag_NoBuffering | BfpFileCreateFlag_ShareRead), BfpFileAttribute_Normal, NULL);
		onNewLine[fileIdx] = true;
		logCount[fileIdx]++;
	}
	if (fp[fileIdx] == NULL)
		return;

	char lineStr[4096];
	int strOfs;
	int maxChars;

	if (onNewLine[fileIdx])
	{
		strOfs = sprintf(lineStr, "%d", entryNum) + 1;
		lineStr[strOfs - 1] = ' ';
		maxChars = 4095 - strOfs;
	}
	else
	{
		strOfs = 0;
		maxChars = 4095;
	}

	va_list argList;
	va_start(argList, fmt);

	int numChars = BF_stbsp_vsnprintf(lineStr + strOfs, maxChars, fmt, argList);
	if (numChars <= maxChars)
	{
		if (strOfs + numChars > 0)
		{
			logSize[fileIdx] += strOfs + numChars;

			BfpFile_Write(fp[fileIdx], lineStr, strOfs + numChars, -1, NULL);
			if (lineStr[strOfs + numChars - 1] == '\n')
				onNewLine[fileIdx] = true;
			else
				onNewLine[fileIdx] = false;
		}
		else
			onNewLine[fileIdx] = false;
		return;
	}

	String aResult = vformat(fmt, argList);
	va_end(argList);

	if (onNewLine[fileIdx])
	{
		aResult = StrFormat("%d ", entryNum) + aResult;
	}

	if (aResult.EndsWith('\n'))
		onNewLine[fileIdx] = true;
	else
		onNewLine[fileIdx] = false;

	logSize[fileIdx] += aResult.length();
	BfpFile_Write(fp[fileIdx], aResult.c_str(), aResult.length(), -1, NULL);
}

BfAtom::~BfAtom()
{
	BF_ASSERT(mPrevNamesMap.IsEmpty());
	BF_ASSERT(mRefCount == 0);
	BF_ASSERT(mPendingDerefCount == 0);
}

void BfAtom::Ref()
{
	mRefCount++;
}

// Val128 BfAtom::GetTypesHash()
// {
// 	if (mTypeData == NULL)
// 		return 0;
// 	if (mTypeData->mTypesHash.IsZero())
// 	{
// 		for (auto typeDef : mTypeData->mTypeDefs)
// 		{
// 			if (typeDef->mNextRevision != NULL)
// 			{
// 				HASH128_MIXIN(mTypeData->mTypesHash, typeDef->mNextRevision->mHash);
// 				continue;
// 			}
//
// 			// Use the typeDef's 'mHash' here - we don't want our hash to change when
// 			//  the internals of a typeDef changes, we just want it to change when
// 			//  we add or remove typeDefs of this given name
// 			HASH128_MIXIN(mTypeData->mTypesHash, typeDef->mHash);
// 		}
// 	}
// 	return mTypeData->mTypesHash;
// }

BfAtomComposite::BfAtomComposite()
{
	mParts = NULL;
	mSize = 0;
	mAllocSize = 0;
	mOwns = false;
}

BfAtomComposite::BfAtomComposite(BfAtom* atom)
{
	Set(&atom, 1, NULL, 0);
}

BfAtomComposite::BfAtomComposite(const BfAtomComposite& rhs)
{
	mParts = NULL;
	mSize = 0;
	mAllocSize = 0;
	mOwns = false;
	if (rhs.mParts != NULL)
		Set(rhs.mParts, rhs.mSize, NULL, 0);
}

BfAtomComposite::BfAtomComposite(BfAtomComposite&& rhs)
{
	if ((rhs.mOwns) || (rhs.mParts == NULL))
	{
		mParts = rhs.mParts;
		mSize = rhs.mSize;
		mAllocSize = rhs.mAllocSize;
		mOwns = rhs.mOwns;

		rhs.mParts = NULL;
		rhs.mSize = 0;
		rhs.mAllocSize = 0;
		rhs.mOwns = false;
		return;
	}

	mParts = NULL;
	mSize = 0;
	mAllocSize = 0;
	mOwns = false;
	if (rhs.mParts != NULL)
		Set(rhs.mParts, rhs.mSize, NULL, 0);
}

BfAtomComposite::BfAtomComposite(const BfAtomComposite& left, const BfAtomComposite& right)
{
	mParts = NULL;
	mSize = 0;
	mAllocSize = 0;
	mOwns = false;
	Set(left.mParts, left.mSize, right.mParts, right.mSize);
}

BfAtomComposite::BfAtomComposite(const BfAtomComposite& left, BfAtom* right)
{
	mParts = NULL;
	mSize = 0;
	mAllocSize = 0;
	mOwns = false;
	Set(left.mParts, left.mSize, &right, 1);
}

void BfAtomComposite::Set(const BfAtomComposite& left, const BfAtomComposite& right)
{
	Set(left.mParts, left.mSize, right.mParts, right.mSize);
}

void BfAtomComposite::Set(BfAtom** atomsA, int countA, BfAtom** atomsB, int countB)
{
	BfAtom** freeParts = NULL;

	if (countA + countB > mAllocSize)
	{
		if (mOwns) // Defer freeing incase we are referring to ourselves
			freeParts = mParts;
		mAllocSize = countA + countB;
		mParts = (BfAtom**)malloc(sizeof(BfAtom*) * mAllocSize);
		mOwns = true;
	}

	if (countA > 0)
		memcpy(mParts, atomsA, sizeof(BfAtom*) * countA);
	if (countB > 0)
		memcpy(mParts + countA, atomsB, sizeof(BfAtom*) * countB);
	mSize = countA + countB;

	if (freeParts != NULL)
		free(freeParts);
}

BfAtomComposite::~BfAtomComposite()
{
	if (mOwns)
		free(mParts);
}

BfAtomComposite& BfAtomComposite::operator=(const BfAtomComposite& rhs)
{
	Set(rhs.mParts, rhs.mSize, NULL, 0);
	return *this;
}

bool BfAtomComposite::operator==(const BfAtomComposite& other) const
{
	if (mSize != other.mSize)
		return false;
	for (int i = 0; i < other.mSize; i++)
		if (mParts[i] != other.mParts[i])
			return false;
	return true;
}

bool BfAtomComposite::operator!=(const BfAtomComposite& other) const
{
	return !(*this == other);
}

bool BfAtomComposite::IsValid() const
{
	for (int i = 0; i < mSize; i++)
		if (mParts[i] == NULL)
			return false;
	return true;
}

bool BfAtomComposite::IsEmpty() const
{
	return mSize == 0;
}

int BfAtomComposite::GetPartsCount() const
{
	return mSize;
}

String BfAtomComposite::ToString() const
{
	if (mSize == 0)
		return "";
	if (mSize == 1)
		return String(mParts[0]->mString);
	String retStr;
	for (int i = 0; i < mSize; i++)
	{
		if (i > 0)
			retStr += ".";
		retStr += mParts[i]->mString;
	}
	return retStr;
}

void BfAtomComposite::ToString(StringImpl& str) const
{
	for (int i = 0; i < mSize; i++)
	{
		if (i > 0)
			str += ".";
		str += mParts[i]->mString;
	}
}

bool BfAtomComposite::StartsWith(const BfAtomComposite& other) const
{
	if (mSize < other.mSize)
		return false;
	for (int i = 0; i < other.mSize; i++)
		if (mParts[i] != other.mParts[i])
			return false;
	return true;
}

bool BfAtomComposite::EndsWith(const BfAtomComposite& other) const
{
	int ofs = mSize - other.mSize;
	if (ofs < 0)
		return false;

	for (int i = 0; i < other.mSize; i++)
		if (mParts[i + ofs] != other.mParts[i])
			return false;
	return true;
}

BfAtomComposite BfAtomComposite::GetSub(int start, int len) const
{
	BfAtomComposite atomComposite;
	atomComposite.Set(mParts + start, len, NULL, 0);
	return atomComposite;
}

void BfAtomComposite::Reference(const BfAtomComposite & other)
{
	if (!mOwns)
	{
		mParts = other.mParts;
		mSize = other.mSize;
		return;
	}
	Set(other.mParts, other.mSize, NULL, 0);
}

// Val128 BfAtomComposite::GetTypesHash()
// {
// 	Val128 hash;
// 	for (int i = 0; i < mSize; i++)
// 	{
// 		auto atom = mParts[i];
// 		if (atom->mRefCount == 0)
// 			return 0; // 0 is our "error condition" when we're looking at a graveyard'ed atom
// 		Val128 hashPart = atom->GetTypesHash();
// 		if (hash.IsZero())
// 			hash = hashPart;
// 		else
// 			HASH128_MIXIN(hash, hashPart);
// 	}
// 	return hash;
// }

uint32 BfAtomComposite::GetAtomUpdateIdx()
{
	uint32 updateIdx = 0;
	for (int i = 0; i < mSize; i++)
	{
		auto atom = mParts[i];
		if ((atom->mRefCount - atom->mPendingDerefCount) == 0)
			return 0; // 0 is our "error condition" when we're looking at a graveyard'ed atom
		updateIdx = BF_MAX(updateIdx, atom->mAtomUpdateIdx);
	}
	return updateIdx;
}

BfSizedAtomComposite::BfSizedAtomComposite()
{
	mAllocSize = BF_ARRAY_COUNT(mInitialAlloc);
	mParts = mInitialAlloc;
}

BfSizedAtomComposite::~BfSizedAtomComposite()
{
	if (mParts == mInitialAlloc)
		mParts = NULL;
}

//////////////////////////////////////////////////////////////////////////

void BfMemberDef::SetName(BfAstNode* nameNode)
{
	StringView sv = nameNode->ToStringView();
	while ((!sv.IsEmpty()) && (sv[0] == '@'))
	{
		sv.RemoveFromStart(1);
		mNamePrefixCount++;
	}
	mName = sv;
}

void BfParameterDef::SetName(BfAstNode* nameNode)
{
	StringView sv = nameNode->ToStringView();
	while ((!sv.IsEmpty()) && (sv[0] == '@'))
	{
		sv.RemoveFromStart(1);
		mNamePrefixCount++;
	}
	mName = sv;
}

//////////////////////////////////////////////////////////////////////////

bool BfPropertyDef::IsVirtual()
{
	if (((BfPropertyDeclaration*)mFieldDeclaration)->mVirtualSpecifier)
		return true;
	return false;
}

bool BfPropertyDef::HasExplicitInterface()
{
	for (auto methodDef : mMethods)
	{
		if (methodDef->mExplicitInterface != NULL)
			return true;
	}
	return false;
}

bool BfPropertyDef::IsExpressionBodied()
{
	auto propertyDeclaration = (BfPropertyDeclaration*)mFieldDeclaration;
	if (auto expr = BfNodeDynCast<BfPropertyBodyExpression>(propertyDeclaration->mDefinitionBlock))
		return true;
	return false;
}

BfAstNode * BfPropertyDef::GetRefNode()
{
	BfPropertyDeclaration* propDecl = (BfPropertyDeclaration*)mFieldDeclaration;
	if ((propDecl != NULL) && (propDecl->mNameNode != NULL))
		return propDecl->mNameNode;
	return propDecl;
}

///

BfAstNode* BfMethodDef::GetRefNode()
{
	if (mMethodType == BfMethodType_Operator)
	{
		BfOperatorDef* operatorDef = (BfOperatorDef*)this;
		if (operatorDef->mOperatorDeclaration->mOpTypeToken != NULL)
			return operatorDef->mOperatorDeclaration->mOpTypeToken;
		return operatorDef->mOperatorDeclaration->mOperatorToken;
	}
	if (auto methodDeclaration = GetMethodDeclaration())
	{
		if (auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
			return ctorDecl->mThisToken;
		if (methodDeclaration->mNameNode != NULL)
			return methodDeclaration->mNameNode;
		return methodDeclaration;
	}
	if (auto methodDeclaration = GetPropertyMethodDeclaration())
	{
		return methodDeclaration->mNameNode;
	}
	if (mDeclaringType != NULL)
		return mDeclaringType->GetRefNode();
	return NULL;
}

BfTokenNode* BfMethodDef::GetMutNode()
{
	if (auto methodDeclaration = GetMethodDeclaration())
		return methodDeclaration->mMutSpecifier;
	if (auto propertyMethodDeclaration = GetMethodDeclaration())
		return propertyMethodDeclaration->mMutSpecifier;
	return NULL;
}

bool BfMethodDef::HasBody()
{
	if (auto methodDeclaration = GetMethodDeclaration())
		return methodDeclaration->mBody != NULL;
	if (auto methodDeclaration = GetPropertyMethodDeclaration())
	{
		auto body = methodDeclaration->mBody;
		return (body != NULL) && (!BfNodeIsA<BfTokenNode>(body));
	}
	return false;
}

BfMethodDef::~BfMethodDef()
{
	FreeMembers();
}

BfImportKind BfMethodDef::GetImportKindFromPath(const StringImpl& filePath)
{
	String fileExt = GetFileExtension(filePath);

	if ((fileExt.Equals(".DYLIB", StringImpl::CompareKind_OrdinalIgnoreCase)) ||
		(fileExt.Equals(".SO", StringImpl::CompareKind_OrdinalIgnoreCase)) ||
		(fileExt.Equals(".DLL", StringImpl::CompareKind_OrdinalIgnoreCase)) ||
		(fileExt.Equals(".EXE", StringImpl::CompareKind_OrdinalIgnoreCase)))
	{
		return BfImportKind_Import_Dynamic;
	}

	return BfImportKind_Import_Static;
}

void BfMethodDef::Reset()
{
	FreeMembers();
}

void BfMethodDef::FreeMembers()
{
	for (auto param : mParams)
		delete param;
	mParams.Clear();
	for (auto genericParam : mGenericParams)
		delete genericParam;
	mGenericParams.Clear();
	delete mParamNameMap;
}

BfMethodDeclaration* BfMethodDef::GetMethodDeclaration()
{
	return BfNodeDynCast<BfMethodDeclaration>(mMethodDeclaration);
}

BfPropertyMethodDeclaration* BfMethodDef::GetPropertyMethodDeclaration()
{
	return BfNodeDynCast<BfPropertyMethodDeclaration>(mMethodDeclaration);
}

BfPropertyDeclaration* BfMethodDef::GetPropertyDeclaration()
{
	auto propertyMethodDeclaration = BfNodeDynCast<BfPropertyMethodDeclaration>(mMethodDeclaration);
	if (propertyMethodDeclaration == NULL)
		return NULL;
	return propertyMethodDeclaration->mPropertyDeclaration;
}

bool BfMethodDef::IsEmptyPartial()
{
	return mIsPartial && (mBody == NULL);
}

bool BfMethodDef::IsDefaultCtor()
{
	if ((mMethodType == BfMethodType_Ctor) || (mMethodType == BfMethodType_CtorNoBody))
	{
		return ((mParams.IsEmpty()) ||
			((mParams.mSize == 1) && (mParams[0]->mParamKind == BfParamKind_AppendIdx)));
	}
	else if (mMethodType == BfMethodType_CtorCalcAppend)
		return mParams.IsEmpty();
	return false;
}

bool BfMethodDef::IsCtorOrInit()
{
	return (mMethodType >= BfMethodType_CtorCalcAppend) && (mMethodType <= BfMethodType_Init);
}

String BfMethodDef::ToString()
{
	String methodText;
	if (mName.empty())
	{
		if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(mMethodDeclaration))
		{
			methodText += "operator";
			if (operatorDecl->mIsConvOperator)
			{
				methodText += " ";
				GetMethodDeclaration()->mReturnType->ToString(methodText);
			}
			else if (operatorDecl->mOpTypeToken != NULL)
				operatorDecl->mOpTypeToken->ToString(methodText);
		}
	}
	else if (mMethodType == BfMethodType_Ctor)
		methodText += "this";
	else if (mMethodType == BfMethodType_Dtor)
		methodText += "~this";
	else
		methodText += mName;

	if (mMethodType == BfMethodType_Mixin)
		methodText += "!";

	if (mGenericParams.size() != 0)
	{
		methodText += "<";
		for (int genericParamIdx = 0; genericParamIdx < (int)mGenericParams.size(); genericParamIdx++)
		{
			if (genericParamIdx != 0)
				methodText += ", ";
			methodText += mGenericParams[genericParamIdx]->mName;
		}
		methodText += ">";
	}
	int visParamIdx = 0;
	methodText += "(";
	for (int paramIdx = 0; paramIdx < (int)mParams.size(); paramIdx++)
	{
		BfParameterDef* paramDef = mParams[paramIdx];
		if ((paramDef->mParamKind == BfParamKind_AppendIdx) || (paramDef->mParamKind == BfParamKind_ImplicitCapture))
			continue;
		if (visParamIdx > 0)
			methodText += ", ";
		if (paramDef->mParamKind == BfParamKind_Params)
			methodText += "params ";
		if (paramDef->mTypeRef != NULL)
			paramDef->mTypeRef->ToString(methodText);
		methodText += " ";
		methodText += paramDef->mName;
		if ((paramDef->mParamDeclaration != NULL) && (paramDef->mParamDeclaration->mInitializer != NULL))
		{
			methodText += " = " + paramDef->mParamDeclaration->mInitializer->ToString();
		}
		visParamIdx++;
	}
	methodText += ")";
	return methodText;
}

int BfMethodDef::GetExplicitParamCount()
{
	for (int i = 0; i < (int)mParams.size(); i++)
	{
		auto param = mParams[i];
		if ((param->mParamKind != BfParamKind_AppendIdx) &&
			(param->mParamKind != BfParamKind_ImplicitCapture))
			return (int)mParams.size() - i;
	}

	return (int)mParams.size();
}

void BfMethodDef::BuildParamNameMap()
{
	if (mParamNameMap != NULL)
		return;

	int startIdx = 0;
	if (mMethodType == BfMethodType_Extension)
		startIdx = 1;

	mParamNameMap = new Dictionary<StringView, int>();
	for (int i = startIdx; i < mParams.mSize; i++)
		(*mParamNameMap)[mParams[i]->mName] = i - startIdx;
}

///

void BfTypeDef::Reset()
{
	FreeMembers();
	Init();
}

void BfTypeDef::FreeMembers()
{
	if ((!mIsCombinedPartial) && (mEmitParent == NULL))
		mSystem->RemoveNamespaceUsage(mNamespace, mProject);

	if (mName != NULL)
	{
		if (mName != mSystem->mEmptyAtom)
		{
			if (!mIsNextRevision)
				mSystem->UntrackName(this);
			if (mInDeleteQueue)
			{
				BF_ASSERT(mName->mPendingDerefCount > 0);
				if (mName->mPendingDerefCount > 0)
					mName->mPendingDerefCount--;
			}
			mSystem->ReleaseAtom(mName);
		}
		mName = NULL;
	}

	if (mNameEx != NULL)
	{
		if (mInDeleteQueue)
		{
			BF_ASSERT(mNameEx->mPendingDerefCount > 0);
			if (mNameEx->mPendingDerefCount > 0)
				mNameEx->mPendingDerefCount--;
		}
		mSystem->ReleaseAtom(mNameEx);
		mNameEx = NULL;
	}

	for (auto genericParam : mGenericParamDefs)
	{
// 		auto genericParamCopy = *genericParam;
// 		BF_ASSERT(genericParam->mOwner != NULL);
//
// 		if (genericParam->mOwner == this)
		delete genericParam;
	}
	mGenericParamDefs.Clear();

	for (auto field : mFields)
		delete field;
	mFields.Clear();
	for (auto prop : mProperties)
		delete prop;
	mProperties.Clear();
	for (auto method : mMethods)
		delete method;
	mMethods.Clear();
	mNestedTypes.Clear();
	// mOperators are also in mMethods so we don't need to delete those specifically
	mOperators.Clear();

	for (auto& searchName : mNamespaceSearch)
		mSystem->ReleaseAtomComposite(searchName);
	mNamespaceSearch.Clear();

	mStaticSearch.Clear();
	mInternalAccessSet.Clear();

	for (auto allocNode : mDirectAllocNodes)
		delete allocNode;
	mDirectAllocNodes.Clear();
	mIsNextRevision = false;
}

void BfTypeDef::PopulateMemberSets()
{
	while (mMethodSet.mSourceSize < mMethods.mSize)
	{
		auto methodDef = mMethods[mMethodSet.mSourceSize++];
		BF_ASSERT(methodDef->mNextWithSameName == NULL);

		BfMemberSetEntry* entry;
		if (!mMethodSet.TryAdd(methodDef, &entry))
		{
			methodDef->mNextWithSameName = (BfMethodDef*)entry->mMemberDef;
			entry->mMemberDef = methodDef;
		}
	}

	while (mFieldSet.mSourceSize < mFields.mSize)
	{
		auto fieldDef = mFields[mFieldSet.mSourceSize++];
		BF_ASSERT(fieldDef->mNextWithSameName == NULL);

		BfMemberSetEntry* entry;
		if (!mFieldSet.TryAdd(fieldDef, &entry))
		{
 			fieldDef->mNextWithSameName = (BfFieldDef*)entry->mMemberDef;
 			entry->mMemberDef = fieldDef;
		}
	}

	while (mPropertySet.mSourceSize < mProperties.mSize)
	{
		auto propDef = mProperties[mPropertySet.mSourceSize++];
		BF_ASSERT(propDef->mNextWithSameName == NULL);

		BfMemberSetEntry* entry;
		if (!mPropertySet.TryAdd(propDef, &entry))
		{
 			propDef->mNextWithSameName = (BfPropertyDef*)entry->mMemberDef;
 			entry->mMemberDef = propDef;
		}
	}
}

void BfTypeDef::ClearMemberSets()
{
	for (auto entry : mMethodSet)
		((BfMethodDef*)entry.mMemberDef)->mNextWithSameName = NULL;
	mMethodSet.Clear();

	for (auto entry : mFieldSet)
		((BfFieldDef*)entry.mMemberDef)->mNextWithSameName = NULL;
	mFieldSet.Clear();

	for (auto entry : mPropertySet)
		((BfPropertyDef*)entry.mMemberDef)->mNextWithSameName = NULL;
	mPropertySet.Clear();
}

BfTypeDef::~BfTypeDef()
{
	BfLogSysM("BfTypeDef::~BfTypeDef %p\n", this);

	delete mNextRevision;
	FreeMembers();

	if (mSource != NULL)
	{
		mSource->mRefCount--;
		BF_ASSERT(mSource->mRefCount >= 0);
	}
}

BfSource* BfTypeDef::GetLastSource()
{
	if (mNextRevision != NULL)
		return mNextRevision->mSource;
	return mSource;
}

bool BfTypeDef::IsGlobalsContainer()
{
	return (mIsStatic) && (mName == mSystem->mGlobalsAtom);
}

void BfTypeDef::RemoveGenericParamDef(BfGenericParamDef* genericParamDef)
{
	BF_FATAL("Not used anymore");

	if (mGenericParamDefs.size() == 0)
		return;

	for (auto innerType : mNestedTypes)
		innerType->RemoveGenericParamDef(genericParamDef);

	if (mGenericParamDefs[0] == genericParamDef)
	{
		mGenericParamDefs.erase(mGenericParamDefs.begin());
		//if (genericParamDef->mOwner == this)
		delete genericParamDef;
	}
}

int BfTypeDef::GetSelfGenericParamCount()
{
	if (mOuterType != NULL)
		return (int)mGenericParamDefs.size() - (int)mOuterType->mGenericParamDefs.size();
	return (int)mGenericParamDefs.size();
}

BfMethodDef* BfTypeDef::GetMethodByName(const StringImpl& name, int paramCount)
{
	PopulateMemberSets();
	BfMemberSetEntry* entry = NULL;
	if (!mMethodSet.TryGetWith(name, &entry))
		return NULL;

	BfMethodDef* bestMethodDef = NULL;
	auto methodDef = (BfMethodDef*)entry->mMemberDef;
	while (methodDef != NULL)
	{
		if (((paramCount == -1) || (paramCount == (int)methodDef->mParams.size())))
		{
			if ((bestMethodDef == NULL) ||
				((bestMethodDef->mDeclaringType->IsExtension()) && (!methodDef->mDeclaringType->IsExtension())))
				bestMethodDef = methodDef;
		}

		methodDef = methodDef->mNextWithSameName;
	}

	return bestMethodDef;
}

BfFieldDef* BfTypeDef::GetFieldByName(const StringImpl& name)
{
	PopulateMemberSets();
	BfFieldDef* nextField = NULL;
	BfMemberSetEntry* entry;
	if (mFieldSet.TryGetWith(name, &entry))
		return (BfFieldDef*)entry->mMemberDef;
	return NULL;
}

String BfTypeDef::ToString()
{
	String typeName(mName->ToString());
	auto checkOuterTypeDef = mOuterType;
	while (checkOuterTypeDef != NULL)
	{
		typeName = checkOuterTypeDef->mName->ToString() + "." + typeName;
		checkOuterTypeDef = checkOuterTypeDef->mOuterType;
	}

	if (mGenericParamDefs.size() != 0)
	{
		typeName += "<";
		for (int genericParamIdx = 0; genericParamIdx < (int)mGenericParamDefs.size(); genericParamIdx++)
		{
			if (genericParamIdx > 0)
				typeName += ", ";
			typeName += mGenericParamDefs[genericParamIdx]->mName;
		}
		typeName += ">";
	}

	return typeName;
}

bool BfTypeDef::HasAutoProperty(BfPropertyDeclaration* propertyDeclaration)
{
	if (mTypeCode == BfTypeCode_Interface)
		return false;
	if (propertyDeclaration->mTypeRef == NULL)
		return false;
	if ((propertyDeclaration->mVirtualSpecifier != NULL) && (propertyDeclaration->mVirtualSpecifier->GetToken() == BfToken_Abstract))
		return false;
	if (propertyDeclaration->mExternSpecifier != NULL)
		return false;

	for (auto methodDeclaration : propertyDeclaration->mMethods)
	{
		if (methodDeclaration->mBody == NULL)
			return true;
	}
	return false;
}

String BfTypeDef::GetAutoPropertyName(BfPropertyDeclaration* propertyDeclaration)
{
	String name = "prop__";
	if (propertyDeclaration->IsA<BfIndexerDeclaration>())
		name += "indexer__";
	else if (propertyDeclaration->mNameNode != NULL)
		name += propertyDeclaration->mNameNode->ToString();
	return name;
}

BfAstNode* BfTypeDef::GetRefNode()
{
	if ((mTypeDeclaration != NULL) && (mTypeDeclaration->mNameNode != NULL))
		return mTypeDeclaration->mNameNode;
	return mTypeDeclaration;
}

void BfTypeDef::ReportMemory(MemReporter* memReporter)
{
	memReporter->Add(sizeof(BfTypeDef));
	memReporter->AddVec(mNamespaceSearch, false);
	memReporter->AddVec(mStaticSearch, false);
	memReporter->AddVec(mInternalAccessSet, false);
	memReporter->AddVecPtr("Fields", mFields, false);
	memReporter->AddVecPtr("Properties", mProperties, false);

	memReporter->BeginSection("Methods");
	memReporter->AddVecPtr(mMethods, false);
	for (auto methodDef : mMethods)
	{
		memReporter->AddVecPtr("Params", methodDef->mParams, false);
		memReporter->AddVecPtr(methodDef->mGenericParams, false);
	}
	memReporter->EndSection();

	memReporter->AddVecPtr(mOperators, false);
	memReporter->AddVecPtr(mGenericParamDefs, false);
	memReporter->AddHashSet(mMethodSet, false);
	memReporter->AddHashSet(mFieldSet, false);
	memReporter->AddHashSet(mPropertySet, false);
	memReporter->AddVec(mBaseTypes, false);
	memReporter->AddVec(mNestedTypes, false);
	memReporter->AddVec(mDirectAllocNodes, false);
}

bool BfTypeDef::ContainsPartial(BfTypeDef* partialTypeDef)
{
	if (partialTypeDef->mPartialIdx < 0)
		return false;
	if (partialTypeDef->mPartialIdx >= mPartials.mSize)
		return false;
	return mPartials[partialTypeDef->mPartialIdx] == partialTypeDef;
}

bool BfTypeDef::NameEquals(BfTypeDef* otherTypeDef)
{
 	if (mName != otherTypeDef->mName)
 		return false;
	// We can't just check mFullnames, because a namespace of "A" with a type named "B.C" would match
	// a namespace of "A.B" with a type named "C"
	if (mNamespace.mSize != otherTypeDef->mNamespace.mSize)
		return false;
	return mFullName == otherTypeDef->mFullName;
}

bool BfTypeDef::HasSource(BfSource* source)
{
	if (mNextRevision != NULL)
		return mNextRevision->HasSource(source);
	if (mSource == source)
		return true;
	if ((mSource != NULL) && (mSource->mNextRevision != NULL) && (mSource->mNextRevision == source))
		return true;
	for (auto partial : mPartials)
		if (partial->mSource == source)
			return true;
	return false;
}

bool BfTypeDef::HasCustomAttributes()
{
	if ((mTypeDeclaration != NULL) && (mTypeDeclaration->mAttributes != NULL))
		return true;
	for (auto& partial : mPartials)
		if ((partial->mTypeDeclaration != NULL) && (partial->mTypeDeclaration->mAttributes != NULL))
			return true;
	return false;
}

bool BfTypeDef::HasParsingFailed()
{
	auto parser = mTypeDeclaration->GetParser();
	if ((parser != NULL) && (parser->mParsingFailed))
		return true;

	for (auto partial : mPartials)
	{
		parser = partial->mTypeDeclaration->GetParser();
		if ((parser != NULL) && (parser->mParsingFailed))
			return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

BfProject::BfProject()
{
	mDisabled = false;
	mSingleModule = false;
	mTargetType = BfTargetType_BeefConsoleApplication;
	mBuildConfigChanged = false;
	mSingleModule = false;
	mAlwaysIncludeAll = false;
	mDeleteStage = BfProject::DeleteStage_None;
	mSystem = NULL;
	mIdx = -1;
}

BfProject::~BfProject()
{
	BF_ASSERT(mNamespaces.size() == 0);
	BfLogSysM("Deleting project %p %s\n", this, mName.c_str());
}

bool BfProject::ContainsReference(BfProject* refProject)
{
	if (refProject->mDisabled)
		return false;
	if (refProject == this)
		return true;
	for (int i = 0; i < (int)mDependencies.size(); i++)
		if (mDependencies[i] == refProject)
			return true;
	return false;
}

bool BfProject::ReferencesOrReferencedBy(BfProject* refProject)
{
	return ContainsReference(refProject) || refProject->ContainsReference(this);
}

bool BfProject::IsTestProject()
{
	return mTargetType == BfTargetType_BeefTest;
}

//////////////////////////////////////////////////////////////////////////

BfErrorBase::~BfErrorBase()
{
	delete mLocation;
}

void BfErrorBase::SetSource(BfPassInstance* passInstance, BfSourceData* source)
{
	mSource = source;

	if (mSource != NULL)
	{
		auto parserData = mSource->ToParserData();
		if (parserData != NULL)
		{
			passInstance->mSourceFileNameMap.TryAdd(mSource, parserData->mFileName);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

size_t BfErrorEntry::GetHashCode() const
{
	HashContext hashCtx;
	hashCtx.Mixin(mError->mSrcStart);
	hashCtx.Mixin(mError->mSrcEnd);
	hashCtx.Mixin(mError->mSource);
	hashCtx.Mixin(mError->mIsWarning);
	hashCtx.Mixin(mError->mIsDeferred);
	return (size_t)hashCtx.Finish64();
}

bool BfErrorEntry::operator==(const BfErrorEntry& other) const
{
	return (mError->mSrcStart == other.mError->mSrcStart) &&
		(mError->mSrcEnd == other.mError->mSrcEnd) &&
		(mError->mSource == other.mError->mSource) &&
		(mError->mIsWarning == other.mError->mIsWarning) &&
		(mError->mIsDeferred == other.mError->mIsDeferred);
}

//////////////////////////////////////////////////////////////////////////

BfPassInstance::~BfPassInstance()
{
	for (auto bfError : mErrors)
		delete bfError;
}

void BfPassInstance::ClearErrors()
{
	mFailedIdx = 0;
	for (auto bfError : mErrors)
		delete bfError;
	mErrors.Clear();
	mOutStream.Clear();
	mLastWasDisplayed = false;
	mLastWasAdded = false;
	mIgnoreCount = 0;
	mWarningCount = 0;
	mDeferredErrorCount = 0;
}

bool BfPassInstance::HasFailed()
{
	return mFailedIdx != 0;
}

bool BfPassInstance::HasMessages()
{
	return !mErrors.IsEmpty();
}

void BfPassInstance::OutputLine(const StringImpl& str)
{
	//OutputDebugStrF("%s\n", str.c_str());
	mOutStream.push_back(str);
}

bool BfPassInstance::PopOutString(String* outString)
{
	if (mOutStream.size() == 0)
		return false;
	*outString = mOutStream.front();
	mOutStream.RemoveAt(0);
	return true;
}

bool BfPassInstance::WantsRangeRecorded(BfSourceData* bfSource, int srcIdx, int srcLen, bool isWarning, bool isDeferred)
{
	if ((!mFilterErrorsTo.IsEmpty()) && (!mFilterErrorsTo.Contains(bfSource)))
		return false;

	if (bfSource == NULL)
		return true;

	//TODO: Was this useful, or does 'var' resolution do this better?
// 	if (!mErrors.IsEmpty())
// 	{
// 		// If the last error had a range that was a subset of this one, then just keep the first error
// 		//  This helps reduce cascading errors to their root cause
// 		auto lastError = mErrors.back();
// 		if ((lastError->mSource == bfSource) && (isWarning == lastError->mIsWarning) && (isDeferred == lastError->mIsDeferred) &&
// 			(lastError->mSrcStart >= srcIdx) && (lastError->mSrcEnd <= srcIdx + srcLen))
// 			return false;
// 	}

	// Don't record errors that have already occurred at this location
	BfErrorBase checkError;
	checkError.mIsWarning = isWarning;
	checkError.mIsDeferred = isDeferred;
	checkError.mSource = bfSource;
	checkError.mSrcStart = srcIdx;
	checkError.mSrcEnd = srcIdx + srcLen;
	if (mErrorSet.Contains(BfErrorEntry(&checkError)))
		return false;

	int prevCount = (int)mErrors.size();
	if (!isWarning)
		prevCount -= mWarningCount;
	if (!isDeferred)
		prevCount -= mDeferredErrorCount;
	if (prevCount > sMaxErrors)
		return false;

	return true;
}

bool BfPassInstance::WantsRangeDisplayed(BfSourceData* bfSource, int srcIdx, int srcLen, bool isWarning, bool isDeferred)
{
	int prevDispCount = (int)mErrors.size();
	if (!isWarning)
		prevDispCount -= mWarningCount;
	if (!isDeferred)
		prevDispCount -= mDeferredErrorCount;
	if (prevDispCount > sMaxDisplayErrors)
		return false;
	auto bfParser = (bfSource == NULL) ? NULL : bfSource->ToParser();
	if (bfParser == NULL)
		return true;
	if (bfParser->mCursorIdx == -1)
		return !mTrimMessagesToCursor;
	if ((bfParser->mCursorIdx >= srcIdx) && (bfParser->mCursorIdx < srcIdx + srcLen))
		return true;
	return false;
}

void BfPassInstance::TrimSourceRange(BfSourceData* source, int startIdx, int& srcLen)
{
	int prevEnd = startIdx + srcLen;
	int newEnd = startIdx;

	// End at a newline once we've found some non-whitespace characters
	bool foundNonWS = false;
	while (newEnd < prevEnd)
	{
		char c = source->mSrc[newEnd];
		if ((c == '\r') || (c == '\n'))
		{
			if (foundNonWS)
				break;
		}
		if ((!foundNonWS) && (!::iswspace((uint8)c)))
		{
			foundNonWS = true;
		}
		newEnd++;
	}

	srcLen = newEnd - startIdx;
}

bool BfPassInstance::HasLastFailedAt(BfAstNode* astNode)
{
	if (mErrors.size() == 0)
		return false;
	auto lastError = mErrors.back();
	return (astNode != NULL) && (lastError->mSrcStart == astNode->GetSrcStart());
}

static void VisibleAdvance(const char* str, int strLength, int& idx)
{
	while (true)
	{
		char c = str[idx];
		if ((uint8)c < 0xC0)
		{
			idx++;
			break;
		}

		int cLen = 0;
		uint32 c32 = u8_toucs(str + idx, strLength - idx, &cLen);
		idx += cLen;
		if (!UTF8IsCombiningMark(c32))
			break;
	}
}

void BfPassInstance::MessageAt(const StringImpl& msgPrefix, const StringImpl& error, BfSourceData* bfSource, int srcIdx, int srcLen, BfFailFlags flags)
{
	BP_ZONE("BfPassInstance::MessageAt");

	auto bfParser = bfSource->ToParserData();
	if (bfParser == NULL)
	{
		OutputLine(error);
		return;
	}

  	if (srcIdx == 0x7FFFFFFF)
	{
		OutputLine(error);
		return;
	}

	bool atEnd = false;
	if (srcIdx >= bfParser->mSrcLength)
	{
		srcIdx = bfParser->mSrcLength - 1;
		atEnd = true;
	}

 	if (srcIdx < 0)
 	{
 		String lineStr = StrFormat("%s %s in %s", msgPrefix.c_str(), error.c_str(), bfParser->mFileName.c_str());
 		OutputLine(lineStr);
 		lineStr = msgPrefix + "    \"" + String(bfParser->mSrc + srcIdx, srcLen) + "\"";
 		OutputLine(lineStr);
 		return;
 	}

	int origSrcIdx = srcIdx;
	if (bfParser->mSrc[srcIdx] == '\n')
		srcIdx--;

	int lineNum = 0;
	int lineStart = 0;
	for (int i = 0; i < srcIdx; i++)
	{
		if (bfParser->mSrc[i] == '\n')
		{
			lineStart = i + 1;
			lineNum++;
		}
	}

	int lineChar = origSrcIdx - lineStart;

	bool endsWithPunctuation = false;
	int lastChar = error[(int)error.length() - 1];
	String formatStr;
	if ((lastChar == '.') || (lastChar == '?') || (lastChar == '!'))
		formatStr = "%s %s Line %d:%d in %s";
	else
		formatStr = "%s %s at line %d:%d in %s";

	OutputLine(StrFormat(formatStr.c_str(), msgPrefix.c_str(), error.c_str(), lineNum + 1, lineChar + 1, bfParser->mFileName.c_str()));

	StringT<256> lineStr = msgPrefix;
	lineStr.Append(' ');

	int spaceCount = 0;
	int tabCount = 0;
	bool showSpaces = (flags & BfFailFlag_ShowSpaceChars) != 0;

	auto _FlushSpacing = [&]
	{
		if (spaceCount > 1)
			lineStr += StrFormat("<%d SPACES>", spaceCount);
		else if (spaceCount == 1)
			lineStr.Append("<SPACE>");
		spaceCount = 0;

		if (tabCount > 1)
			lineStr += StrFormat("<%d TABS>", tabCount);
		else if (tabCount == 1)
			lineStr.Append("<TAB>");
		tabCount = 0;
	};

	for (int i = 0; i < 255; i++)
	{
		char c = bfParser->mSrc[lineStart + i];
		if ((c == '\0') || (c == '\n') || (c == '\r'))
		{
			break;
		}
		else if (c == '\t')
		{
			if (showSpaces)
			{
				if (spaceCount > 0)
					_FlushSpacing();
				tabCount++;
				//lineStr.Append("\xe2\x86\x92"); // Arrow \u2192
			}
			else
				lineStr.Append(' ');
		}
		else if (c == ' ')
		{
			if (showSpaces)
			{
				if (tabCount > 0)
					_FlushSpacing();
				spaceCount++;
				//lineStr.Append("\xc2\xb7"); // Dot \u00B7
			}
			else
				lineStr.Append(' ');
		}
		else
		{
			_FlushSpacing();
			showSpaces = false;
			lineStr.Append(c);
		}
	}
	_FlushSpacing();
	OutputLine(lineStr);

	/*char lineStr[256] = { 0 };
	for (int i = 0; i < 255; i++)
	{
		char c = bfParser->mSrc[lineStart + i];
		if ((c == '\0') || (c == '\n') || (c == '\r'))
		{
			lineStr[i] = 0;
			break;
		}
		else if (c == '\t')
			lineStr[i] = ' ';
		else
			lineStr[i] = c;
	}
	OutputLine(lineStr);*/

	// Don't show '^^^^^^^^^' under the entire line
	bool isFullUnderline = true;
	for (int i = lineStart; i < srcIdx; i++)
	{
		char c = bfParser->mSrc[i];
		if (!::isspace((uint8)c))
			isFullUnderline = false;
	}

	if (isFullUnderline)
	{
		isFullUnderline = false;
		for (int i = srcIdx; i <= srcIdx + srcLen; VisibleAdvance(bfParser->mSrc, bfParser->mSrcLength, i))
		{
			char c = bfParser->mSrc[i];
			if ((c == '\n') || (c == '\0'))
			{
				isFullUnderline = true;
				break;
			}
		}
	}

	if (!isFullUnderline)
	{
		String pointerStr = msgPrefix;
		pointerStr.Append(' ');
		for (int i = lineStart; i < origSrcIdx; VisibleAdvance(bfParser->mSrc, bfParser->mSrcLength, i))
			pointerStr += " ";

		for (int i = srcIdx; i < srcIdx + srcLen; VisibleAdvance(bfParser->mSrc, bfParser->mSrcLength, i))
		{
			char c = bfParser->mSrc[i];
			pointerStr += "^";
			if (c == '\n')
				break;
		}
		OutputLine(pointerStr);
	}
}

BfError* BfPassInstance::FailAt(const StringImpl& error, BfSourceData* bfSource, int srcIdx, int srcLen, BfFailFlags flags)
{
	BP_ZONE("BfPassInstance::FailAt");

	mLastWasAdded = false;
	mFailedIdx++;
	if ((int) mErrors.size() >= sMaxErrors)
		return NULL;

	if (!WantsRangeRecorded(bfSource, srcIdx, srcLen, false))
		return NULL;

	TrimSourceRange(bfSource, srcIdx, srcLen);

	BfError* errorVal = new BfError();
	errorVal->mIsWarning = false;
	errorVal->SetSource(this, bfSource);
	errorVal->mIsAfter = false;
	errorVal->mError = error;
	errorVal->mSrcStart = srcIdx;
	errorVal->mSrcEnd = srcIdx + srcLen;
	//int checkEnd = srcIdx + srcLen;
	for (int i = srcIdx; i < srcIdx + srcLen; i++)
	{
		char c = bfSource->mSrc[i];
		if ((c == '\r') || (c == '\n'))
			break;
		errorVal->mSrcEnd = i + 1;
	}
	//errorVal->mSrcEnd = srcIdx + srcLen;
	FixSrcStartAndEnd(bfSource, errorVal->mSrcStart, errorVal->mSrcEnd);
	mErrorSet.Add(BfErrorEntry(errorVal));
	mErrors.push_back(errorVal);
	mLastWasAdded = true;

	mLastWasDisplayed = WantsRangeDisplayed(bfSource, srcIdx, srcLen, false);
	if (mLastWasDisplayed)
	{
		String errorStart = "ERROR";
		/*if ((int)mErrors.size() > 1)
			errorStart += StrFormat(" #%d", mErrors.size());*/
		MessageAt(":error", errorStart + ": " + error, bfSource, srcIdx, srcLen, flags);
	}
	return errorVal;
}

void BfPassInstance::FixSrcStartAndEnd(BfSourceData* bfSource, int& startIdx, int& endIdx)
{
	auto bfParser = bfSource->ToParserData();
	if (bfParser == NULL)
		return;
	int spanLength = 0;
	UTF8GetGraphemeClusterSpan(bfParser->mSrc, bfParser->mSrcLength, startIdx, startIdx, spanLength);
	endIdx = BF_MAX(endIdx, startIdx + spanLength);
}

BfError* BfPassInstance::FailAfterAt(const StringImpl& error, BfSourceData* bfSource, int srcIdx)
{
	BP_ZONE("BfPassInstance::FailAfterAt");

	mFailedIdx++;
	if ((int)mErrors.size() >= sMaxErrors)
		return NULL;

	auto bfParser = bfSource->ToParserData();
	if (!WantsRangeRecorded(bfParser, srcIdx, 1, false))
		return NULL;

	// Go to start of UTF8 chunk
// 	int startIdx = srcIdx;
// 	int spanLenth = 0;
// 	UTF8GetGraphemeClusterSpan(bfParser->mSrc, bfParser->mOrigSrcLength, srcIdx, startIdx, spanLenth);

	BfError* errorVal = new BfError();
	errorVal->mIsWarning = false;
	errorVal->SetSource(this, bfSource);
	errorVal->mIsAfter = true;
	errorVal->mError = error;
	errorVal->mSrcStart = srcIdx;
	errorVal->mSrcEnd = srcIdx + 1;
	FixSrcStartAndEnd(bfSource, errorVal->mSrcStart, errorVal->mSrcEnd);
	mErrorSet.Add(BfErrorEntry(errorVal));
	mErrors.push_back(errorVal);

	mLastWasDisplayed = WantsRangeDisplayed(bfParser, srcIdx - 1, 2, false);
	if (mLastWasDisplayed)
	{
		String errorStart = "ERROR";
		/*if ((int)mErrors.size() > 1)
			errorStart += StrFormat(" #%d", mErrors.size());*/
		MessageAt(":error", errorStart + ": " + error, bfParser, srcIdx + 1, 1);
	}
	return errorVal;
}

BfError* BfPassInstance::Fail(const StringImpl& error)
{
	mFailedIdx++;
	if ((int) mErrors.size() >= sMaxErrors)
		return NULL;

	BfError* errorVal = new BfError();
	errorVal->mIsWarning = false;
	errorVal->mSource = NULL;
	errorVal->mIsAfter = false;
	errorVal->mError = error;
	errorVal->mSrcStart = 0;
	errorVal->mSrcEnd = 0;
	mErrors.push_back(errorVal);

	mLastWasDisplayed = (int)mErrors.size() - mWarningCount - mDeferredErrorCount <= sMaxDisplayErrors;
	if (mLastWasDisplayed)
	{
		String errorStart = "ERROR";
		/*if ((int)mErrors.size() > 1)
			errorStart += StrFormat(" #%d", mErrors.size());*/
		OutputLine(errorStart + ": " + error);
	}
	return mErrors.back();
}

BfError* BfPassInstance::Fail(const StringImpl& errorStr, BfAstNode* refNode)
{
	BP_ZONE("BfPassInstance::Fail");

	BfError* error = NULL;

	mFailedIdx++;
	if ((refNode == NULL) || (refNode->IsTemporary()))
		error = Fail(errorStr);
	else if (refNode->IsA<BfBlock>())
		error = FailAt(errorStr, refNode->GetSourceData(), refNode->GetSrcStart(), 1);
	else
		error = FailAt(errorStr, refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength());

	return error;
}

BfError* BfPassInstance::FailAfter(const StringImpl& error, BfAstNode* refNode)
{
	BP_ZONE("BfPassInstance::FailAfter");

	mFailedIdx++;
	if ((refNode == NULL) || (refNode->IsTemporary()))
		return Fail(error);
	/*if (refNode->mNext != NULL)
	{
		for (int checkIdx = refNode->mSrcEnd; checkIdx < refNode->mNext->mSrcStart; checkIdx++)
		{
			if (refNode->mSource->mSrc[checkIdx] == '\n')
			{
				// Don't show a 'fail after' if it's on a new line
				return FailAfterAt(error, refNode->mSource, refNode->mSrcEnd - 1);
			}
		}

		return FailAt(error, refNode->mSource, refNode->mNext->mSrcStart);
	}
	else*/
		return FailAfterAt(error, refNode->GetSourceData(), refNode->GetSrcEnd() - 1);
}

BfError* BfPassInstance::DeferFail(const StringImpl& error, BfAstNode* refNode)
{
	mLastWasAdded = false;
	mFailedIdx++;
	if ((int)mErrors.size() >= sMaxErrors)
		return NULL;

	if (refNode == NULL)
	{
		return Fail(error);
	}

	if (!WantsRangeRecorded(refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength(), false, true))
		return NULL;

	++mDeferredErrorCount;
	BfError* errorVal = new BfError();
	errorVal->mIsWarning = false;
	errorVal->mIsDeferred = true;
	errorVal->SetSource(this, refNode->GetSourceData());
	errorVal->mIsAfter = false;
	errorVal->mError = error;
	errorVal->mSrcStart = refNode->GetSrcStart();
	errorVal->mSrcEnd = refNode->GetSrcEnd();
	mErrors.push_back(errorVal);
	mErrorSet.Add(BfErrorEntry(errorVal));
	mLastWasAdded = true;

	BF_ASSERT(!refNode->IsTemporary());
	auto parser = errorVal->mSource->ToParserData();

	mLastWasDisplayed = false;
	return errorVal;
}

void BfPassInstance::SilentFail()
{
	mFailedIdx++;
}

BfError* BfPassInstance::WarnAt(int warningNumber, const StringImpl& warning, BfSourceData* bfSource, int srcIdx, int srcLen, bool isDeferred)
{
	mLastWasAdded = false;
	if ((int) mErrors.size() >= sMaxErrors)
		return NULL;

	auto bfParser = bfSource->ToParserData();
	if ((bfParser != NULL) && (warningNumber > 0) && (!bfParser->IsWarningEnabledAtSrcIndex(warningNumber, srcIdx)))
		return NULL;

	if (!WantsRangeRecorded(bfParser, srcIdx, srcLen, true, isDeferred))
		return NULL;

	mWarnIdx++;

	TrimSourceRange(bfSource, srcIdx, srcLen);

	BfError* errorVal = new BfError();
	errorVal->mIsWarning = true;
	errorVal->mWarningNumber = warningNumber;
	errorVal->SetSource(this, bfSource);
	errorVal->mIsAfter = false;
	errorVal->mError = warning;
	errorVal->mSrcStart = srcIdx;
	errorVal->mSrcEnd = srcIdx + srcLen;
	errorVal->mIsDeferred = isDeferred;
	FixSrcStartAndEnd(bfSource, errorVal->mSrcStart, errorVal->mSrcEnd);
	mErrorSet.Add(BfErrorEntry(errorVal));
	mErrors.push_back(errorVal);
	++mWarningCount;
	mLastWasAdded = true;

	if (!isDeferred)
	{
		mLastWasDisplayed = WantsRangeDisplayed(bfParser, srcIdx, srcLen, true);
		if (mLastWasDisplayed)
		{
			String errorStart = "WARNING";
			if ((int)mErrors.size() > 1)
				errorStart += StrFormat("(%d)", mErrors.size());
			if (warningNumber > 0)
				errorStart += StrFormat(": BF%04d", warningNumber);
			MessageAt(":warn", errorStart + ": " + warning, bfParser, srcIdx);
		}
	}
	return errorVal;
}

BfError* BfPassInstance::Warn(int warningNumber, const StringImpl& warning)
{
	mWarnIdx++;
	mLastWasAdded = false;
	mLastWasDisplayed = (int)mErrors.size() <= sMaxDisplayErrors;
	if (!mLastWasDisplayed)
		return NULL;
	(void)warningNumber;//CDH TODO is warningNumber meaningful here w/o context? n/a for now
	OutputLine((":warn WARNING: " + warning).c_str());
	return NULL;
}

BfError* BfPassInstance::Warn(int warningNumber, const StringImpl& warning, BfAstNode* refNode, bool isDeferred)
{
	BP_ZONE("BfPassInstance::Warn");

	mWarnIdx++;
	mLastWasAdded = false;
	mLastWasDisplayed = (int)mErrors.size() <= sMaxErrors;
	if (!mLastWasDisplayed)
		return NULL;

	auto parser = refNode->GetSourceData()->ToParserData();
	if (parser != NULL)
	{
		if (parser->IsUnwarnedAt(refNode))
		{
			mLastWasDisplayed = false;
			return NULL;
		}
	}

	if (refNode != NULL)
		return WarnAt(warningNumber, warning, refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength(), isDeferred);
	else
		return Warn(warningNumber, warning);
}

BfError* BfPassInstance::WarnAfter(int warningNumber, const StringImpl& warning, BfAstNode* refNode)
{
	auto parser = refNode->GetSourceData()->ToParserData();
	if (parser != NULL)
	{
		if (parser->IsUnwarnedAt(refNode))
		{
			mLastWasDisplayed = false;
			return NULL;
		}
	}

	return WarnAt(warningNumber, warning, refNode->GetSourceData(), refNode->GetSrcEnd());
}

BfError* BfPassInstance::WarnAfterAt(int warningNumber, const StringImpl& error, BfSourceData* bfSource, int srcIdx)
{
	BP_ZONE("BfPassInstance::FailAfterAt");

	mFailedIdx++;
	if ((int)mErrors.size() >= sMaxErrors)
		return NULL;

	auto bfParser = bfSource->ToParserData();
	if ((bfParser != NULL) && (warningNumber > 0) && (!bfParser->IsWarningEnabledAtSrcIndex(warningNumber, srcIdx)))
		return NULL;

	if (!WantsRangeRecorded(bfParser, srcIdx, 1, false))
		return NULL;

	// Go to start of UTF8 chunk
// 	int startIdx = srcIdx;
// 	int spanLenth = 0;
// 	UTF8GetGraphemeClusterSpan(bfParser->mSrc, bfParser->mOrigSrcLength, srcIdx, startIdx, spanLenth);

	BfError* errorVal = new BfError();
	errorVal->mIsWarning = true;
	errorVal->SetSource(this, bfSource);
	errorVal->mIsAfter = true;
	errorVal->mError = error;
	errorVal->mSrcStart = srcIdx;
	errorVal->mSrcEnd = srcIdx + 1;
	FixSrcStartAndEnd(bfSource, errorVal->mSrcStart, errorVal->mSrcEnd);
	mErrorSet.Add(BfErrorEntry(errorVal));
	mErrors.push_back(errorVal);

	mLastWasDisplayed = WantsRangeDisplayed(bfParser, srcIdx - 1, 2, false);
	if (mLastWasDisplayed)
	{
		String errorStart = "WARNING";
		/*if ((int)mErrors.size() > 1)
			errorStart += StrFormat(" #%d", mErrors.size());*/
		MessageAt(":warn", errorStart + ": " + error, bfParser, srcIdx + 1, 1);
	}
	return errorVal;
}

BfMoreInfo* BfPassInstance::MoreInfoAt(const StringImpl& info, BfSourceData* bfSource, int srcIdx, int srcLen, BfFailFlags flags)
{
	if (!mLastWasDisplayed)
	{
		if (mLastWasAdded)
		{
			auto lastError = mErrors.back();
			BfMoreInfo* moreInfo = new BfMoreInfo();
			moreInfo->mInfo = info;
			moreInfo->SetSource(this, bfSource);
			moreInfo->mSrcStart = srcIdx;
			moreInfo->mSrcEnd = srcIdx + srcLen;

			lastError->mMoreInfo.push_back(moreInfo);
			return moreInfo;
		}

		return NULL;
	}

	String msgPrefix;
	MessageAt(msgPrefix, " > " + info, bfSource, srcIdx, srcLen, flags);
	return NULL;
}

BfMoreInfo* BfPassInstance::MoreInfo(const StringImpl& info, bool forceQueue)
{
	if ((!mLastWasDisplayed) || (forceQueue))
	{
		if (mLastWasAdded)
		{
			auto lastError = mErrors.back();
			BfMoreInfo* moreInfo = new BfMoreInfo();
			moreInfo->mInfo = info;
			moreInfo->mSource = NULL;
			moreInfo->mSrcStart = -1;
			moreInfo->mSrcEnd = -1;

			lastError->mMoreInfo.push_back(moreInfo);
			return moreInfo;
		}

		return NULL;
	}

	String outText;
	outText += "  > ";
	outText += info;
	OutputLine(outText);
	return NULL;
}

BfMoreInfo* BfPassInstance::MoreInfo(const StringImpl& info, BfAstNode* refNode)
{
	if (refNode == NULL)
		return MoreInfo(info);
	else
		return MoreInfoAt(info, refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength());
}

BfMoreInfo* BfPassInstance::MoreInfoAfter(const StringImpl& info, BfAstNode* refNode)
{
	return MoreInfoAt(info, refNode->GetSourceData(), refNode->GetSrcEnd(), 1);
}

void BfPassInstance::TryFlushDeferredError()
{
	// This can happen in the case of an internal compiler error, where we believe we've satisfied
	//   generic constraints but we generate an error on the specialization but not the unspecialized version
	bool hasDisplayedError = false;
	bool hasDisplayedWarning = false;
	for (int pass = 0; pass < 2; pass++)
	{
		for (auto& error : mErrors)
		{
			if (error->mIsWarning)
			{
				if (!error->mIsDeferred)
					hasDisplayedWarning = true;
				else if ((pass == 1) && (!hasDisplayedWarning))
				{
					String errorText = "WARNING ";
					if (error->mWarningNumber > 0)
						errorText += StrFormat(": BF%04d", error->mWarningNumber);
					errorText += ": ";
					errorText += error->mError;

					MessageAt(":warning", errorText, error->mSource, error->mSrcStart, error->mSrcEnd - error->mSrcStart);

					for (auto moreInfo : error->mMoreInfo)
					{
						if (moreInfo->mSource != NULL)
							MessageAt(":warning", " > " + moreInfo->mInfo, moreInfo->mSource, moreInfo->mSrcStart, moreInfo->mSrcEnd - moreInfo->mSrcStart);
						else
							OutputLine(":warning " + moreInfo->mInfo);
					}
				}
			}
			else
			{
				if (!error->mIsDeferred)
					hasDisplayedError = true;
				else if ((pass == 1) && (!hasDisplayedError))
				{
					MessageAt(":error", "ERROR: " + error->mError, error->mSource, error->mSrcStart, error->mSrcEnd - error->mSrcStart);

					for (auto moreInfo : error->mMoreInfo)
					{
						if (moreInfo->mSource != NULL)
							MessageAt(":error", " > " + moreInfo->mInfo, moreInfo->mSource, moreInfo->mSrcStart, moreInfo->mSrcEnd - moreInfo->mSrcStart);
						else
							OutputLine(":error " + moreInfo->mInfo);
					}
				}
			}
		}
	}
}

void BfPassInstance::WriteErrorSummary()
{
	if (mErrors.size() > 0)
	{
		String msg = StrFormat(":med Errors: %d.", mErrors.size() - mWarningCount - mIgnoreCount);
		if (mWarningCount > 0)
			msg += StrFormat(" Warnings: %d.", mWarningCount);
		if ((int)mErrors.size() > sMaxDisplayErrors)
			msg += StrFormat(" Only the first %d are displayed.", sMaxDisplayErrors);
		OutputLine(msg);
	}
}

//////////////////////////////////////////////////////////////////////////

void BfReportMemory();

BfSystem::BfSystem()
{
	BP_ZONE("BfSystem::BfSystem");

	mUpdateCnt = 0;
	if (gPerfManager == NULL)
		gPerfManager = new PerfManager();
	//gPerfManager->StartRecording();

	mAtomUpdateIdx = 0;
	mAtomCreateIdx = 0;
	mTypeMapVersion = 1;
	CreateBasicTypes();
	mPtrSize = 4;
	mCurSystemLockPri = -1;
	mYieldDisallowCount = 0;
	mPendingSystemLockPri = -1;
	mCurSystemLockThreadId = 0;
	mYieldTickCount = 0;
	mHighestYieldTime = 0;
	mNeedsTypesHandledByCompiler = false;
	mWorkspaceConfigChanged = false;
	mIsResolveOnly = false;
	mEmptyAtom = GetAtom("");
	mBfAtom = GetAtom("bf");
	mGlobalsAtom = GetAtom("@");
	mTypeDot = NULL;

	if (gBfParserCache == NULL)
		gBfParserCache = new BfParserCache();
	gBfParserCache->mRefCount++;

	BfAstTypeInfo::Init();
	mDirectVoidTypeRef = mDirectTypeRefs.Alloc();
	mDirectVoidTypeRef->Init("void");
	mDirectBoolTypeRef = mDirectTypeRefs.Alloc();
	mDirectBoolTypeRef->Init("bool");
	mDirectSelfTypeRef = mDirectTypeRefs.Alloc();
	mDirectSelfTypeRef->Init("Self");
	mDirectSelfBaseTypeRef = mDirectTypeRefs.Alloc();
	mDirectSelfBaseTypeRef->Init("SelfBase");
	mDirectRefSelfBaseTypeRef = mRefTypeRefs.Alloc();
	mDirectRefSelfBaseTypeRef->mElementType = mDirectSelfBaseTypeRef;
	mDirectRefSelfBaseTypeRef->mRefToken = NULL;
	mDirectObjectTypeRef = mDirectTypeRefs.Alloc();
	mDirectObjectTypeRef->Init("System.Object");
	mDirectStringTypeRef = mDirectTypeRefs.Alloc();
	mDirectStringTypeRef->Init("System.String");
	mDirectIntTypeRef = mDirectTypeRefs.Alloc();
	mDirectIntTypeRef->Init("int");
	mDirectRefIntTypeRef = mRefTypeRefs.Alloc();
	mDirectRefIntTypeRef->mElementType = mDirectIntTypeRef;
	mDirectRefIntTypeRef->mRefToken = NULL;
	mDirectInt32TypeRef = mDirectTypeRefs.Alloc();
	mDirectInt32TypeRef->Init("int32");
}

BfSystem::~BfSystem()
{
	BP_ZONE("BfSystem::~BfSystem");

	BfLogSys(this, "Deleting BfSystem...\n");
	BfReportMemory();

	//gPerfManager->StopRecording();
	//gPerfManager->DbgPrint();

	for (auto& typeItr : mSystemTypeDefs)
		delete typeItr.mValue;

	for (auto typeDef : mTypeDefs)
		delete typeDef;
	mTypeDefs.Clear();

	for (auto typeDef : mTypeDefDeleteQueue)
		delete typeDef;

	{
		BP_ZONE("Deleting parsers");
		for (auto parser : mParsers)
		{
			delete parser;
		}
	}

	for (auto project : mProjects)
		delete project;

	for (auto project : mProjectDeleteQueue)
		delete project;

	ReleaseAtom(mGlobalsAtom);
	ReleaseAtom(mBfAtom);
	ReleaseAtom(mEmptyAtom);
	ProcessAtomGraveyard();

	BF_ASSERT(mAtomMap.size() == 0);

	gBfParserCache->mRefCount--;
	if (gBfParserCache->mRefCount == 0)
	{
		delete gBfParserCache;
		gBfParserCache = NULL;
	}

	BfLogSys(this, "After ~BfSystem\n");
	BfReportMemory();
}

#define SYSTEM_TYPE(typeVar, name, typeCode) \
	typeVar = typeDef = new BfTypeDef(); \
	typeDef->mSystem = this; \
	typeDef->mName = GetAtom(name); \
	typeDef->mName->mIsSystemType = true; \
	TrackName(typeDef); \
	typeDef->mTypeCode = typeCode; \
	typeDef->mHash = typeCode + 1000; \
	mSystemTypeDefs[name] = typeDef;

BfAtom* BfSystem::GetAtom(const StringImpl& string)
{
	StringView* stringPtr = NULL;
	BfAtom* atom = NULL;
	BfAtom** atomPtr = NULL;
	if (mAtomMap.TryAdd(string, &stringPtr, &atomPtr))
	{
		atom = new BfAtom();
		*atomPtr = atom;
		stringPtr->mPtr = strdup(string.c_str());
#ifdef _DEBUG
		for (int i = 0; i < (int)string.length(); i++)
		{
			BF_ASSERT(string[i] != '.'); // Should be a composite
		}
#endif
		mAtomCreateIdx++;
		atom->mIsSystemType = false;
		atom->mAtomUpdateIdx = ++mAtomUpdateIdx;
		atom->mString = *stringPtr;
		atom->mRefCount = 1;
		atom->mPendingDerefCount = 0;

		atom->mHash = 0;
		for (char c : string)
			atom->mHash = ((atom->mHash ^ c) << 5) - atom->mHash;

		BfLogSys(this, "Atom Allocated %p %s\n", atom, string.c_str());

		return atom;
	}
	else
		atom = *atomPtr;

	atom->Ref();
	return atom;
}

BfAtom* BfSystem::FindAtom(const StringImpl& string)
{
	BfAtom** atomPtr = NULL;
	if (mAtomMap.TryGetValueWith(string, &atomPtr))
		return *atomPtr;
	return NULL;
}

BfAtom* BfSystem::FindAtom(const StringView& string)
{
	BfAtom** atomPtr = NULL;
	if (mAtomMap.TryGetValue(string, &atomPtr))
		return *atomPtr;
	return NULL;
}

void BfSystem::ReleaseAtom(BfAtom* atom)
{
	if (--atom->mRefCount == 0)
	{
		mAtomGraveyard.push_back(atom);
		return;
	}

	BF_ASSERT(atom->mRefCount > 0);
	// Sanity check
	BF_ASSERT(atom->mRefCount < 1000000);
}

void BfSystem::ProcessAtomGraveyard()
{
	// We need this set, as it's possible to have multiple of the same entry in the graveyard
    //  if we ref and then deref again
	HashSet<BfAtom*> deletedAtoms;

	for (auto atom : mAtomGraveyard)
	{
		if (deletedAtoms.Contains(atom))
			continue;

		BF_ASSERT(atom->mRefCount >= 0);
		if (atom->mRefCount == 0)
		{
			deletedAtoms.Add(atom);
			auto itr = mAtomMap.Remove(atom->mString);
			free((void*)atom->mString.mPtr);
			delete atom;
		}
	}
	mAtomGraveyard.Clear();
}

bool BfSystem::ParseAtomComposite(const StringView& name, BfAtomComposite& composite, bool addRefs)
{
	if (name.mLength == 0)
		return true;

	bool isValid = true;

	SizedArray<BfAtom*, 6> parts;

	BF_ASSERT(composite.mSize == 0);
	int lastDot = -1;
	for (int i = 0; i <= (int)name.mLength; i++)
	{
		if ((i == (int)name.mLength) || (name[i] == '.'))
		{
			BfAtom* atom;
			if (addRefs)
				atom = GetAtom(String(name.mPtr + lastDot + 1, i - lastDot - 1));
			else
				atom = FindAtom(StringView(name.mPtr + lastDot + 1, i - lastDot - 1));
			if (atom == NULL)
				isValid = false;
			parts.push_back(atom);
			lastDot = i;
		}
	}
	if (!parts.IsEmpty())
		composite.Set(&parts[0], (int)parts.size(), NULL, 0);
	return isValid;
}

void BfSystem::RefAtomComposite(const BfAtomComposite& atomComposite)
{
	for (int i = 0; i < atomComposite.mSize; i++)
	{
		auto part = atomComposite.mParts[i];
		if (part != NULL)
			part->Ref();
	}
}

void BfSystem::ReleaseAtomComposite(const BfAtomComposite& atomComposite)
{
	for (int i = 0; i < atomComposite.mSize; i++)
	{
		auto part = atomComposite.mParts[i];
		if (part != NULL)
			ReleaseAtom(part);
	}
}

void BfSystem::SanityCheckAtomComposite(const BfAtomComposite& atomComposite)
{
	for (int i = 0; i < atomComposite.mSize; i++)
	{
		auto part = atomComposite.mParts[i];
		BF_ASSERT(part != NULL);
		BF_ASSERT(part->mRefCount > 0);
		BF_ASSERT(part->mRefCount < 1000000);
	}
}

void BfSystem::TrackName(BfTypeDef* typeDef)
{
	if (!typeDef->IsEmitted())
	{
		for (int i = 0; i < (int)typeDef->mFullName.mSize - 1; i++)
		{
			auto prevAtom = typeDef->mFullName.mParts[i];
			auto atom = typeDef->mFullName.mParts[i + 1];
			int* countPtr;
			if (atom->mPrevNamesMap.TryAdd(prevAtom, NULL, &countPtr))
			{
				*countPtr = 1;
			}
			else
			{
				(*countPtr)++;
			}
		}
	}
}

void BfSystem::UntrackName(BfTypeDef* typeDef)
{
	BfAtom* nameAtom = typeDef->mName;
	if (nameAtom != mEmptyAtom)
	{
		nameAtom->mAtomUpdateIdx = ++mAtomUpdateIdx;
	}

	if ((!typeDef->mIsCombinedPartial) && (!typeDef->IsEmitted()))
	{
		for (int i = 0; i < (int)typeDef->mFullName.mSize - 1; i++)
		{
			auto prevAtom = typeDef->mFullName.mParts[i];
			auto atom = typeDef->mFullName.mParts[i + 1];

			auto itr = atom->mPrevNamesMap.Find(prevAtom);
			if (itr != atom->mPrevNamesMap.end())
			{
				int& count = itr->mValue;
				if (--count == 0)
				{
					atom->mPrevNamesMap.Remove(itr);
				}
			}
			else
			{
				BF_DBG_FATAL("Unable to untrack name");
			}
		}
	}
}

void BfSystem::CreateBasicTypes()
{
	BfTypeDef* typeDef;

	SYSTEM_TYPE(mTypeVoid, "void", BfTypeCode_None);
	SYSTEM_TYPE(mTypeNullPtr, "null", BfTypeCode_NullPtr);
	SYSTEM_TYPE(mTypeSelf, "Self", BfTypeCode_Self);
	SYSTEM_TYPE(mTypeVar, "var", BfTypeCode_Var);
	SYSTEM_TYPE(mTypeLet, "let", BfTypeCode_Let);
	SYSTEM_TYPE(mTypeBool, "bool", BfTypeCode_Boolean);
	SYSTEM_TYPE(mTypeInt8, "int8", BfTypeCode_Int8);
	SYSTEM_TYPE(mTypeUInt8, "uint8", BfTypeCode_UInt8);
	SYSTEM_TYPE(mTypeInt16, "int16", BfTypeCode_Int16);
	SYSTEM_TYPE(mTypeUInt16, "uint16", BfTypeCode_UInt16);
	SYSTEM_TYPE(mTypeInt32, "int32", BfTypeCode_Int32);
	SYSTEM_TYPE(mTypeUInt32, "uint32", BfTypeCode_UInt32);
	SYSTEM_TYPE(mTypeInt64, "int64", BfTypeCode_Int64);
	SYSTEM_TYPE(mTypeUInt64, "uint64", BfTypeCode_UInt64);
	SYSTEM_TYPE(mTypeIntPtr, "int", BfTypeCode_IntPtr);
	SYSTEM_TYPE(mTypeUIntPtr, "uint", BfTypeCode_UIntPtr);
	SYSTEM_TYPE(mTypeIntUnknown, "int literal", BfTypeCode_IntUnknown);
	SYSTEM_TYPE(mTypeUIntUnknown, "uint literal", BfTypeCode_UIntUnknown);
	SYSTEM_TYPE(mTypeChar8, "char8", BfTypeCode_Char8);
	SYSTEM_TYPE(mTypeChar16, "char16", BfTypeCode_Char16);
	SYSTEM_TYPE(mTypeChar32, "char32", BfTypeCode_Char32);
	SYSTEM_TYPE(mTypeSingle, "float", BfTypeCode_Float);
	SYSTEM_TYPE(mTypeDouble, "double", BfTypeCode_Double);
}

bool BfSystem::DoesLiteralFit(BfTypeCode typeCode, int64 value)
{
	if (typeCode == BfTypeCode_IntPtr)
		typeCode = (mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
	if (typeCode == BfTypeCode_UIntPtr)
		typeCode = (mPtrSize == 4) ? BfTypeCode_UInt32 : BfTypeCode_UInt64;

	switch (typeCode)
	{
	case BfTypeCode_Boolean:
		return (value >= 0) && (value < 1);
	case BfTypeCode_Int8:
		return (value >= -0x80) && (value < 0x80);
	case BfTypeCode_Int16:
		return (value >= -0x8000) && (value < 0x8000);
	case BfTypeCode_Int32:
		return (value >= -0x80000000LL) && (value < 0x80000000LL);
	case BfTypeCode_Int64:
		return true;

	case BfTypeCode_UInt8:
	case BfTypeCode_Char8:
		return (value >= 0) && (value < 0x100);
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		return (value >= 0) && (value < 0x10000);
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		return (value >= 0) && (value < 0x100000000LL);
	case BfTypeCode_UInt64:
		return (value >= 0);
	default: break;
	}

	return false;
}

bool BfSystem::DoesLiteralFit(BfTypeCode typeCode, uint64 value)
{
	if (typeCode == BfTypeCode_IntPtr)
		typeCode = (mPtrSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64;
	if (typeCode == BfTypeCode_UIntPtr)
		typeCode = (mPtrSize == 4) ? BfTypeCode_UInt32 : BfTypeCode_UInt64;

	if (value >= 0x8000000000000000)
		return typeCode == BfTypeCode_UInt64;

	switch (typeCode)
	{
	case BfTypeCode_Boolean:
		return (value < 1);
	case BfTypeCode_Int8:
		return (value < 0x80);
	case BfTypeCode_Int16:
		return (value < 0x8000);
	case BfTypeCode_Int32:
		return (value < 0x80000000LL);
	case BfTypeCode_Int64:
		return true;

	case BfTypeCode_UInt8:
	case BfTypeCode_Char8:
		return (value < 0x100);
	case BfTypeCode_UInt16:
	case BfTypeCode_Char16:
		return (value < 0x10000);
	case BfTypeCode_UInt32:
	case BfTypeCode_Char32:
		return (value < 0x100000000LL);
	case BfTypeCode_UInt64:
		return true;
	default: break;
	}

	return false;
}

bool BfSystem::DoesLiteralFit(BfTypeCode typeCode, const BfVariant& variant)
{
	if ((BfIRConstHolder::IsIntable(typeCode)) && (BfIRConstHolder::IsIntable(variant.mTypeCode)))
	{
		if (BfIRConstHolder::IsSigned(variant.mTypeCode))
			return DoesLiteralFit(typeCode, variant.mInt64);
		else
			return DoesLiteralFit(typeCode, variant.mUInt64);
	}
	if ((BfIRConstHolder::IsFloat(typeCode)) && (BfIRConstHolder::IsFloat(variant.mTypeCode)))
		return true;
	return typeCode == variant.mTypeCode;
}

BfParser* BfSystem::CreateParser(BfProject* bfProject)
{
	AutoCrit crit(mDataLock);
	auto parser = new BfParser(this, bfProject);
	mParsers.push_back(parser);

	BfLogSys(this, "CreateParser: %p\n", parser);

	return parser;
}

BfCompiler* BfSystem::CreateCompiler(bool isResolveOnly)
{
	auto compiler = new BfCompiler(this, isResolveOnly);
	mCompilers.push_back(compiler);
	if (mIsResolveOnly)
		BF_ASSERT(isResolveOnly);
	if (isResolveOnly)
		mIsResolveOnly = true;
	return compiler;
}

BfProject* BfSystem::GetProject(const StringImpl& projName)
{
	for (auto project : mProjects)
		if (project->mName == projName)
			return project;

	return NULL;
}

BfTypeReference* BfSystem::GetTypeRefElement(BfTypeReference* typeRef)
{
	if (auto elementedType = BfNodeDynCast<BfElementedTypeRef>(typeRef))
		return GetTypeRefElement(elementedType->mElementType);
	return (BfTypeReference*)typeRef;
}

void BfSystem::AddNamespaceUsage(const BfAtomComposite& namespaceStr, BfProject* bfProject)
{
	if (namespaceStr.IsEmpty())
		return;

	if (namespaceStr.GetPartsCount() > 1)
	{
		BfAtomComposite subComposite;
		subComposite.Set(namespaceStr.mParts, namespaceStr.mSize - 1, NULL, 0);
		AddNamespaceUsage(subComposite, bfProject);
	}

	int* valuePtr = NULL;
	if (bfProject->mNamespaces.TryAdd(namespaceStr, NULL, &valuePtr))
	{
		BfLogSys(this, "BfSystem::AddNamespaceUsage created %s in project: %p\n", namespaceStr.ToString().c_str(), bfProject);
		*valuePtr = 1;
		mTypeMapVersion++;
	}
	else
		(*valuePtr)++;
}

void BfSystem::RemoveNamespaceUsage(const BfAtomComposite& namespaceStr, BfProject* bfProject)
{
	if (namespaceStr.IsEmpty())
		return;

	if (namespaceStr.GetPartsCount() > 1)
	{
		BfAtomComposite subComposite;
		subComposite.Set(namespaceStr.mParts, namespaceStr.mSize - 1, NULL, 0);
		RemoveNamespaceUsage(subComposite, bfProject);
	}

	int* valuePtr = NULL;
	bfProject->mNamespaces.TryGetValue(namespaceStr, &valuePtr);
	BF_ASSERT(valuePtr != NULL);
	(*valuePtr)--;
	if (*valuePtr == 0)
	{
		BfLogSys(this, "BfSystem::RemoveNamespaceUsage removed %s in project: %p\n", namespaceStr.ToString().c_str(), bfProject);
		bfProject->mNamespaces.Remove(namespaceStr);
		mTypeMapVersion++;
	}
}

bool BfSystem::ContainsNamespace(const BfAtomComposite& namespaceStr, BfProject* bfProject)
{
	if (bfProject == NULL)
	{
		for (auto checkProject : mProjects)
		{
			if (checkProject->mNamespaces.ContainsKey(namespaceStr))
				return true;
		}
		return false;
	}

	if (bfProject->mNamespaces.ContainsKey(namespaceStr))
		return true;
	for (auto depProject : bfProject->mDependencies)
		if (depProject->mNamespaces.ContainsKey(namespaceStr))
			return true;
	return false;
}

BfTypeDef* BfSystem::FilterDeletedTypeDef(BfTypeDef* typeDef)
{
	if ((typeDef != NULL) && (typeDef->mDefState == BfTypeDef::DefState_Deleted))
		return NULL;
	return typeDef;
}

bool BfSystem::CheckTypeDefReference(BfTypeDef* typeDef, BfProject* project)
{
	if (project == NULL)
		return !typeDef->mProject->mDisabled;
	if (typeDef->mProject == NULL)
		return true;
	return project->ContainsReference(typeDef->mProject);
}

BfTypeDef* BfSystem::FindTypeDef(const BfAtomComposite& findName, int numGenericArgs, BfProject* project, const Array<BfAtomComposite>& namespaceSearch, BfTypeDef** ambiguousTypeDef, BfFindTypeDefFlags flags)
{
	if (findName.GetPartsCount() == 1)
	{
		BfTypeDef** typeDefPtr = NULL;
		if (mSystemTypeDefs.TryGetValueWith(findName.mParts[0]->mString, &typeDefPtr))
			return FilterDeletedTypeDef(*typeDefPtr);
	}

	// This searched globals, but we were already doing that down below at the LAST step.  Right?
	BfTypeDef* foundTypeDef = NULL;
	BfAtomCompositeT<16> qualifiedFindName;

	bool allowGlobal = (flags & BfFindTypeDefFlag_AllowGlobal) != 0;

	int foundPri = (int)0x80000000;
	for (int namespaceIdx = 0; namespaceIdx <= (int) namespaceSearch.size(); namespaceIdx++)
	{
		int curNamespacePri = 0;
		if (namespaceIdx < (int)namespaceSearch.size())
		{
			auto& namespaceDeclaration = namespaceSearch[namespaceIdx];
			qualifiedFindName.Set(namespaceDeclaration, findName);
		}
		else
		{
			qualifiedFindName = findName;
		}

		int partialStartEntryIdx = -1;

		auto itr = mTypeDefs.TryGet(qualifiedFindName);
		while (itr)
		{
			BfTypeDef* typeDef = *itr;

			if ((typeDef->mIsPartial) ||
				((typeDef->IsGlobalsContainer()) && ((flags & BfFindTypeDefFlag_AllowGlobal) == 0)))
			{
				bool handled = false;
				if ((itr.mCurEntry < mTypeDefs.mPartialSkipCache.mSize) && (!allowGlobal))
				{
					auto& entry = mTypeDefs.mPartialSkipCache[itr.mCurEntry];
					if (entry.mRevision == mTypeDefs.mRevision)
					{
						if (entry.mIndex == -1)
						{
							// No non-partial here
							break;
						}

						itr.mCurEntry = entry.mIndex;
						typeDef = *itr;
						handled = true;
					}
				}

				if (!handled)
				{
					if (partialStartEntryIdx == -1)
						partialStartEntryIdx = itr.mCurEntry;
					itr.MoveToNextHashMatch();
					continue;
				}
			}

			if ((partialStartEntryIdx != -1) && (!allowGlobal))
			{
				mTypeDefs.SetPartialSkipCache(partialStartEntryIdx, itr.mCurEntry);
				partialStartEntryIdx = -1;
			}

			if ((typeDef->mFullName == qualifiedFindName) && (CheckTypeDefReference(typeDef, project)))
			{
				int curPri = curNamespacePri;
				if (typeDef->mGenericParamDefs.size() != numGenericArgs)
				{
					// Still allow SOME match even if we put in the wrong number of generic args
					curPri -= 10000;
				}

				if ((curPri > foundPri) || (foundTypeDef == NULL))
				{
					foundTypeDef = typeDef;
					if (ambiguousTypeDef != NULL)
						*ambiguousTypeDef = NULL;
					foundPri = curPri;
				}
				else if (curPri == foundPri)
				{
					if ((ambiguousTypeDef != NULL) && (!typeDef->mIsPartial))
						*ambiguousTypeDef = typeDef;
				}
			}
			itr.MoveToNextHashMatch();
		}

		if ((partialStartEntryIdx != -1) && (!allowGlobal))
			mTypeDefs.SetPartialSkipCache(partialStartEntryIdx, -1);
	}

	// Didn't match the correct number of generic params, but let the compiler complain
	return FilterDeletedTypeDef(foundTypeDef);
}

bool BfSystem::FindTypeDef(const BfAtomComposite& findName, int numGenericArgs, BfProject* project, const BfAtomComposite& checkNamespace, bool allowPrivate, BfTypeDefLookupContext* ctx)
{
	BfAtomComposite const* qualifiedFindNamePtr;
	BfAtomComposite qualifiedFindName;
	BfAtom* tempData[16];

	if (checkNamespace.IsEmpty())
	{
		if ((findName.mSize == 1) && (findName.mParts[0]->mIsSystemType))
		{
			BfTypeDef** typeDefPtr = NULL;
			if (mSystemTypeDefs.TryGetValueWith(findName.mParts[0]->mString, &typeDefPtr))
			{
				ctx->mBestPri = 0x7FFFFFFF;
				ctx->mBestTypeDef = FilterDeletedTypeDef(*typeDefPtr);
			}
			return true;
		}
		qualifiedFindNamePtr = &findName;
	}
	else
	{
		qualifiedFindName.mAllocSize = 16;
		qualifiedFindName.mParts = tempData;
		qualifiedFindName.Set(checkNamespace, findName);
		qualifiedFindNamePtr = &qualifiedFindName;
	}

	BfProtection minProtection = allowPrivate ? BfProtection_Private : BfProtection_Protected;

	bool hadMatch = false;

	auto itr = mTypeDefs.TryGet(*qualifiedFindNamePtr);
	while (itr)
	{
		BfTypeDef* typeDef = *itr;

		if ((typeDef->mIsPartial) ||
			(typeDef->mDefState == BfTypeDef::DefState_Deleted))
		{
			itr.MoveToNextHashMatch();
			continue;
		}

		if ((typeDef->mFullName == *qualifiedFindNamePtr) && (CheckTypeDefReference(typeDef, project)))
		{
			int curPri = 0;
			if (typeDef->mProtection < minProtection)
				curPri -= 1;
			if (typeDef->IsGlobalsContainer())
				curPri -= 2;
			if (typeDef->mGenericParamDefs.size() != numGenericArgs)
			{
				// Still allow SOME match even if we put in the wrong number of generic args
				curPri -= 4;
			}
			if ((typeDef->mPartials.mSize > 0) && (typeDef->mPartials[0]->IsExtension()))
			{
				// Is a failed extension
				curPri -= 8;
			}

			if ((curPri > ctx->mBestPri) || (ctx->mBestTypeDef == NULL))
			{
				ctx->mBestTypeDef = typeDef;
				ctx->mAmbiguousTypeDef = NULL;
				ctx->mBestPri = curPri;
				hadMatch = true;
			}
			else if (curPri == ctx->mBestPri)
			{
				ctx->mAmbiguousTypeDef = typeDef;
			}
		}
		itr.MoveToNextHashMatch();
	}

	if (qualifiedFindName.mParts == tempData)
		qualifiedFindName.mParts = NULL;

	return hadMatch;
}

BfTypeDef* BfSystem::FindTypeDef(const StringImpl& typeName, int numGenericArgs, BfProject* project, const Array<BfAtomComposite>& namespaceSearch, BfTypeDef** ambiguousTypeDef, BfFindTypeDefFlags flags)
{
	BfAtomCompositeT<16> qualifiedFindName;
	BfTypeDef* result = NULL;
	if (ParseAtomComposite(typeName, qualifiedFindName))
		result = FindTypeDef(qualifiedFindName, numGenericArgs, project, namespaceSearch, ambiguousTypeDef, flags);
	return result;
}

BfTypeDef * BfSystem::FindTypeDef(const StringImpl& typeName, BfProject* project)
{
	String findName;

	int firstChevIdx = -1;
	int chevDepth = 0;
	int numGenericArgs = 0;
	for (int i = 0; i < (int)typeName.length(); i++)
	{
		char c = typeName[i];
		if (c == '<')
		{
			if (firstChevIdx == -1)
				firstChevIdx = i;
			chevDepth++;
		}
		else if (c == '>')
		{
			chevDepth--;
		}
		else if (c == ',')
		{
			if (chevDepth == 1)
				numGenericArgs++;
		}
	}

	if (firstChevIdx != -1)
		findName = typeName.Substring(0, firstChevIdx);
	else
		findName = typeName;

	return FindTypeDef(typeName, numGenericArgs, project);
}

BfTypeDef* BfSystem::FindTypeDefEx(const StringImpl& fullTypeName)
{
	int colonPos = (int)fullTypeName.IndexOf(':');
	if (colonPos == -1)
		return NULL;

	auto project = GetProject(fullTypeName.Substring(0, colonPos));
	if (project == NULL)
		return NULL;

	int numGenericArgs = 0;
	String typeName = fullTypeName.Substring(colonPos + 1);
	int tildePos = (int)typeName.LastIndexOf('`');
	if (tildePos != -1)
	{
		BF_ASSERT(tildePos > (int)typeName.LastIndexOf('.'));
		numGenericArgs = atoi(typeName.c_str() + tildePos + 1);
		typeName.RemoveToEnd(tildePos);
	}

	BfAtomComposite qualifiedFindName;
	BfAtom* tempData[16];
	qualifiedFindName.mAllocSize = 16;
	qualifiedFindName.mParts = tempData;

	BfTypeDef* result = NULL;
	if (ParseAtomComposite(typeName, qualifiedFindName))
	{
		auto itr = mTypeDefs.TryGet(qualifiedFindName);
		while (itr)
		{
			BfTypeDef* typeDef = *itr;
			if ((typeDef->mFullName == qualifiedFindName) && (CheckTypeDefReference(typeDef, project)) &&
				(!typeDef->mIsPartial))
			{
				if (typeDef->mGenericParamDefs.size() == numGenericArgs)
					return typeDef;
			}
			itr.MoveToNextHashMatch();
		}
	}
	if (qualifiedFindName.mParts == tempData)
		qualifiedFindName.mParts = NULL;
	return result;
}

void BfSystem::FindFixitNamespaces(const StringImpl& typeName, int numGenericArgs, BfProject* project, std::set<String>& fixitNamespaces)
{
	BfAtomComposite findName;
	if (!ParseAtomComposite(typeName, findName))
		return;

	// The algorithm assumes the first (or only) part of the BfAtomComposite is a type name, and finds a type with that matching
	//  name and then adds its namespace to the fixitNamespaces

	for (auto typeDef : mTypeDefs)
	{
		if ((typeDef->mName == findName.mParts[0]) &&
			(CheckTypeDefReference(typeDef, project)) &&
			((numGenericArgs == -1) || (typeDef->mGenericParamDefs.size() == numGenericArgs)))
		{
			String outerName;
			if (typeDef->mOuterType != NULL)
			{
				if (!typeDef->mGenericParamDefs.IsEmpty())
					continue;
				outerName += "static ";
				outerName += typeDef->mOuterType->mFullName.ToString();
			}
			else
				outerName = typeDef->mNamespace.ToString();
			fixitNamespaces.insert(outerName);
		}
	}
}

void BfSystem::RemoveTypeDef(BfTypeDef* typeDef)
{
	BF_ASSERT(typeDef->mDefState == BfTypeDef::DefState_Deleted);
	// mTypeDef is already locked by the system lock
	mTypeDefs.Remove(typeDef);
	AutoCrit autoCrit(mDataLock);

	if (typeDef->mOuterType != NULL)
	{
		// We are in the outer type's mNestedTypes list
		BfLogSys(this, "Setting mForceUseNextRevision on outer type %p from %p\n", typeDef->mOuterType, typeDef);
		typeDef->mOuterType->mForceUseNextRevision = true;
	}

	// This will get properly handled in UntrackName when we process the mTypeDefDeleteQueue, but this
	//  mAtomUpdateIdx increment will trigger lookup changes in BfContext::VerifyTypeLookups
	if (typeDef->mName != mEmptyAtom)
	{
		typeDef->mName->mAtomUpdateIdx = ++mAtomUpdateIdx;
		typeDef->mName->mPendingDerefCount++;
	}
	if (typeDef->mNameEx != mEmptyAtom)
	{
		typeDef->mNameEx->mAtomUpdateIdx = ++mAtomUpdateIdx;
		typeDef->mNameEx->mPendingDerefCount++;
	}
	typeDef->mInDeleteQueue = true;

	mTypeDefDeleteQueue.push_back(typeDef);
	mTypeMapVersion++;
}

void BfSystem::InjectNewRevision(BfTypeDef* typeDef)
{
	BfLogSys(this, "InjectNewRevision from %p (decl:%p) into %p (decl:%p)\n", typeDef->mNextRevision, typeDef->mNextRevision->mTypeDeclaration, typeDef, typeDef->mTypeDeclaration);

	bool setDeclaringType = !typeDef->mIsCombinedPartial;

	auto nextTypeDef = typeDef->mNextRevision;

	for (auto prevProperty : typeDef->mProperties)
		delete prevProperty;
	typeDef->mProperties = nextTypeDef->mProperties;
	if (setDeclaringType)
		for (auto prop : typeDef->mProperties)
			prop->mDeclaringType = typeDef;
	nextTypeDef->mProperties.Clear();

	if ((typeDef->mDefState != BfTypeDef::DefState_Signature_Changed) &&
		(typeDef->mDefState != BfTypeDef::DefState_New))
	{
		BF_ASSERT(typeDef->mMethods.size() == nextTypeDef->mMethods.size());

		for (auto prop : typeDef->mProperties)
		{
			for (int methodIdx = 0; methodIdx < (int)prop->mMethods.size(); methodIdx++)
				prop->mMethods[methodIdx] = typeDef->mMethods[prop->mMethods[methodIdx]->mIdx];
		}

		for (int opIdx = 0; opIdx < (int)typeDef->mOperators.size(); opIdx++)
		{
			typeDef->mOperators[opIdx] = (BfOperatorDef*)typeDef->mMethods[typeDef->mOperators[opIdx]->mIdx];
		}

		// Remap methods in-place to previous revision's method list
		for (int methodIdx = 0; methodIdx < (int)typeDef->mMethods.size(); methodIdx++)
		{
			auto methodDef = typeDef->mMethods[methodIdx];
			auto nextMethodDef = nextTypeDef->mMethods[methodIdx];
			bool codeChanged = nextMethodDef->mFullHash != methodDef->mFullHash;

			for (auto genericParam : methodDef->mGenericParams)
				delete genericParam;
			for (auto param : methodDef->mParams)
				delete param;

			if (nextMethodDef->mMethodType == BfMethodType_Operator)
			{
				auto operatorDef = (BfOperatorDef*)methodDef;
				auto nextOperatorDef = (BfOperatorDef*)nextMethodDef;
				*operatorDef = *nextOperatorDef;
				if (setDeclaringType)
					operatorDef->mDeclaringType = typeDef;
			}
			else
			{
				*methodDef = *nextMethodDef;
				if (setDeclaringType)
					methodDef->mDeclaringType = typeDef;
			}

			if (codeChanged)
				methodDef->mCodeChanged = true;
			nextMethodDef->mParams.Clear();
			nextMethodDef->mGenericParams.Clear();
		}
	}
	else
	{
		typeDef->mOperators = nextTypeDef->mOperators;
		nextTypeDef->mOperators.Clear();

		for (auto prevMethod : typeDef->mMethods)
		{
			delete prevMethod;
		}
		typeDef->mMethods = nextTypeDef->mMethods;
		if (setDeclaringType)
			for (auto method : typeDef->mMethods)
				method->mDeclaringType = typeDef;
		nextTypeDef->mMethods.Clear();
	}

	for (auto fieldDef : typeDef->mFields)
		fieldDef->mNextWithSameName = NULL;
	for (auto propDef : typeDef->mProperties)
		propDef->mNextWithSameName = NULL;
	for (auto methodDef : typeDef->mMethods)
		methodDef->mNextWithSameName = NULL;

	if (typeDef->mSource != NULL)
		typeDef->mSource->mRefCount--;
	typeDef->mSource = nextTypeDef->mSource;
	typeDef->mSource->mRefCount++;

	typeDef->mPartialIdx = nextTypeDef->mPartialIdx;
	typeDef->mTypeDeclaration = nextTypeDef->mTypeDeclaration;
	typeDef->mHash = nextTypeDef->mHash;
	typeDef->mSignatureHash = nextTypeDef->mSignatureHash;
	typeDef->mFullHash = nextTypeDef->mFullHash;
	typeDef->mInlineHash = nextTypeDef->mInlineHash;
	typeDef->mNestDepth = nextTypeDef->mNestDepth;

	typeDef->mOuterType = nextTypeDef->mOuterType;
	//typeDef->mOuterType = nextTypeDef->mOuterType;
	typeDef->mNamespace = nextTypeDef->mNamespace;
	BF_ASSERT(typeDef->mName == nextTypeDef->mName);
	//typeDef->mName = nextTypeDef->mName;
	BF_ASSERT(typeDef->mNameEx == nextTypeDef->mNameEx);
	//typeDef->mNameEx = nextTypeDef->mNameEx;
	//typeDef->mFullName = nextTypeDef->mFullName;

	BF_ASSERT(typeDef->mFullNameEx == nextTypeDef->mFullNameEx);

	typeDef->mProtection = nextTypeDef->mProtection;

	BF_ASSERT(typeDef->mTypeCode == nextTypeDef->mTypeCode);

	typeDef->mTypeCode = nextTypeDef->mTypeCode;
	typeDef->mShow = nextTypeDef->mShow;
	typeDef->mIsAlwaysInclude = nextTypeDef->mIsAlwaysInclude;
	typeDef->mIsNoDiscard = nextTypeDef->mIsNoDiscard;
	typeDef->mIsPartial = nextTypeDef->mIsPartial;
	typeDef->mIsExplicitPartial = nextTypeDef->mIsExplicitPartial;
	//mPartialUsed
	typeDef->mIsCombinedPartial = nextTypeDef->mIsCombinedPartial;
	typeDef->mIsDelegate = nextTypeDef->mIsDelegate;
	typeDef->mIsFunction = nextTypeDef->mIsFunction;
	typeDef->mIsClosure = nextTypeDef->mIsClosure;
	typeDef->mIsAbstract = nextTypeDef->mIsAbstract;
	typeDef->mIsStatic = nextTypeDef->mIsStatic;
	typeDef->mHasAppendCtor = nextTypeDef->mHasAppendCtor;
	typeDef->mHasCEOnCompile = nextTypeDef->mHasCEOnCompile;
	typeDef->mHasCtorNoBody = nextTypeDef->mHasCtorNoBody;
	typeDef->mHasOverrideMethods = nextTypeDef->mHasOverrideMethods;
	typeDef->mHasExtensionMethods = nextTypeDef->mHasExtensionMethods;
	typeDef->mHasUsingFields = nextTypeDef->mHasUsingFields;
	typeDef->mIsOpaque = nextTypeDef->mIsOpaque;

	typeDef->mDupDetectedRevision = nextTypeDef->mDupDetectedRevision;

	for (auto prevDirectNodes : typeDef->mDirectAllocNodes)
		delete prevDirectNodes;
	typeDef->mDirectAllocNodes = nextTypeDef->mDirectAllocNodes;
	nextTypeDef->mDirectAllocNodes.Clear();

	for (auto name : typeDef->mNamespaceSearch)
		ReleaseAtomComposite(name);
	typeDef->mNamespaceSearch = nextTypeDef->mNamespaceSearch;
	for (auto name : typeDef->mNamespaceSearch)
		RefAtomComposite(name);

	typeDef->mStaticSearch = nextTypeDef->mStaticSearch;
	typeDef->mInternalAccessSet = nextTypeDef->mInternalAccessSet;

	for (auto prevField : typeDef->mFields)
	{
		delete prevField;
	}
	typeDef->mFields = nextTypeDef->mFields;
	if (setDeclaringType)
		for (auto field : typeDef->mFields)
			field->mDeclaringType = typeDef;
	nextTypeDef->mFields.Clear();

	for (auto genericParam : typeDef->mGenericParamDefs)
		delete genericParam;
	typeDef->mGenericParamDefs.Clear();

	typeDef->mGenericParamDefs = nextTypeDef->mGenericParamDefs;
	typeDef->mExternalConstraints = nextTypeDef->mExternalConstraints;
	nextTypeDef->mGenericParamDefs.Clear();

	typeDef->mBaseTypes = nextTypeDef->mBaseTypes;
	typeDef->mNestedTypes = nextTypeDef->mNestedTypes;

	// If we are a partial then the mOuterType gets set to the combined partial so don't do that here
	if (!typeDef->mIsCombinedPartial)
	{
		for (auto nestedType : typeDef->mNestedTypes)
		{
			BF_ASSERT(nestedType->mNestDepth == typeDef->mNestDepth + 1);
			nestedType->mOuterType = typeDef;
		}
	}
	typeDef->mPartials = nextTypeDef->mPartials;
	typeDef->mMethodSet.Clear();
	typeDef->mFieldSet.Clear();
	typeDef->mPropertySet.Clear();

	delete nextTypeDef;
	typeDef->mNextRevision = NULL;

	typeDef->mDefState = BfTypeDef::DefState_Defined;
	typeDef->mForceUseNextRevision = false;

	VerifyTypeDef(typeDef);
}

void BfSystem::AddToCompositePartial(BfPassInstance* passInstance, BfTypeDef* compositeTypeDef, BfTypeDef* partialTypeDef)
{
	VerifyTypeDef(compositeTypeDef);
	VerifyTypeDef(partialTypeDef);

	bool isFirst = false;

	BF_ASSERT(compositeTypeDef->mFullNameEx == partialTypeDef->mFullNameEx);

	auto typeDef = compositeTypeDef->mNextRevision;
	if (typeDef == NULL)
	{
		typeDef = new BfTypeDef();
		compositeTypeDef->mNextRevision = typeDef;

		typeDef->mIsCombinedPartial = true;

		typeDef->mTypeDeclaration = partialTypeDef->mTypeDeclaration;
		typeDef->mSource = partialTypeDef->mSource;
		typeDef->mSource->mRefCount++;

		typeDef->mSystem = partialTypeDef->mSystem;
		typeDef->mTypeCode = partialTypeDef->mTypeCode;
		typeDef->mShow = partialTypeDef->mShow;
		typeDef->mIsFunction = partialTypeDef->mIsFunction;
		typeDef->mIsDelegate = partialTypeDef->mIsDelegate;
		typeDef->mNestDepth = partialTypeDef->mNestDepth;
		typeDef->mOuterType = partialTypeDef->mOuterType;
		typeDef->mNamespace = partialTypeDef->mNamespace;
		typeDef->mName = partialTypeDef->mName;
		typeDef->mName->Ref();
		TrackName(typeDef);
		typeDef->mNameEx = partialTypeDef->mNameEx;
		typeDef->mNameEx->Ref();
		typeDef->mFullName = partialTypeDef->mFullName;
		typeDef->mFullNameEx = partialTypeDef->mFullNameEx;
		typeDef->mProtection = partialTypeDef->mProtection;
		typeDef->mIsDelegate = partialTypeDef->mIsDelegate;
		typeDef->mIsAbstract = partialTypeDef->mIsAbstract;
		typeDef->mIsStatic = partialTypeDef->mIsStatic;
		typeDef->mHasAppendCtor = partialTypeDef->mHasAppendCtor;
		typeDef->mHasCtorNoBody = partialTypeDef->mHasCtorNoBody;
		typeDef->mHasExtensionMethods = partialTypeDef->mHasExtensionMethods;
		typeDef->mHasUsingFields = partialTypeDef->mHasUsingFields;
		typeDef->mHasOverrideMethods = partialTypeDef->mHasOverrideMethods;
		typeDef->mIsAlwaysInclude = partialTypeDef->mIsAlwaysInclude;
		typeDef->mHasCEOnCompile = partialTypeDef->mHasCEOnCompile;

		for (auto generic : partialTypeDef->mGenericParamDefs)
		{
			BfGenericParamDef* newGeneric = new BfGenericParamDef();
			*newGeneric = *generic;
			typeDef->mGenericParamDefs.push_back(newGeneric);
		}
		typeDef->mExternalConstraints = partialTypeDef->mExternalConstraints;

		typeDef->mBaseTypes = partialTypeDef->mBaseTypes;

		isFirst = true;

		VerifyTypeDef(typeDef);
	}
	else
	{
		VerifyTypeDef(typeDef);

		//TODO: Assert protection and junk all matches
		if (partialTypeDef->mTypeCode != BfTypeCode_Extension)
		{
			typeDef->mTypeCode = partialTypeDef->mTypeCode;
			typeDef->mTypeDeclaration = partialTypeDef->mTypeDeclaration;
		}

// 		for (auto& externConstraint : partialTypeDef->mExternalConstraints)
// 			typeDef->mExternalConstraints.Add(externConstraint);
	}

	// Merge attributes together
	typeDef->mIsAbstract |= partialTypeDef->mIsAbstract;
	typeDef->mIsStatic |= partialTypeDef->mIsStatic;
	typeDef->mHasAppendCtor |= partialTypeDef->mHasAppendCtor;
	typeDef->mHasCEOnCompile |= partialTypeDef->mHasCEOnCompile;
	typeDef->mHasExtensionMethods |= partialTypeDef->mHasExtensionMethods;
	typeDef->mHasUsingFields |= partialTypeDef->mHasUsingFields;
	typeDef->mHasOverrideMethods |= partialTypeDef->mHasOverrideMethods;

	for (auto innerType : partialTypeDef->mNestedTypes)
	{
		typeDef->mNestedTypes.push_back(innerType);
	}

	//TODO: We had the CLEAR here, but it caused an issue because when we have to rebuild the composite then
	//  we don't actually have the nested types from the original typeDef if they original typedef wasn't rebuilt
	//partialTypeDef->mNestedTypes.Clear(); // Only reference from main typedef

	for (auto field : partialTypeDef->mFields)
	{
		BfFieldDef* newField = new BfFieldDef();
		*newField = *field;
		newField->mIdx = (int)typeDef->mFields.size();
		typeDef->mFields.push_back(newField);
	}
	typeDef->mFieldSet.Clear();

	bool hadNoDeclMethod = false;
	int startMethodIdx = (int)typeDef->mMethods.size();
	for (auto method : partialTypeDef->mMethods)
	{
		bool ignoreNewMethod = false;

		if (typeDef->mTypeCode == BfTypeCode_Interface)
		{
			if (method->mMethodDeclaration == NULL)
				continue;

			if (auto methodDeclaration = method->GetMethodDeclaration())
			{
				if (methodDeclaration->mProtectionSpecifier == NULL)
					method->mProtection = BfProtection_Public;
			}
		}

		BfMethodDef* newMethod = NULL;
		if (method->mMethodType == BfMethodType_Operator)
		{
			BfOperatorDef* newOperator = new BfOperatorDef();
			*newOperator = *(BfOperatorDef*)method;
			newMethod = newOperator;
			typeDef->mOperators.push_back(newOperator);
		}
		else
		{
			newMethod = new BfMethodDef();
			*newMethod = *method;
		}
		method->mParamNameMap = NULL;
		newMethod->mIdx = (int)typeDef->mMethods.size();
		for (int paramIdx = 0; paramIdx < (int)newMethod->mParams.size(); paramIdx++)
		{
			BfParameterDef* param = newMethod->mParams[paramIdx];
			BfParameterDef* newParam = new BfParameterDef();
			*newParam = *param;
			newMethod->mParams[paramIdx] = newParam;
		}
		for (int genericIdx = 0; genericIdx < (int)newMethod->mGenericParams.size(); genericIdx++)
		{
			BfGenericParamDef* generic = newMethod->mGenericParams[genericIdx];
			BfGenericParamDef* newGeneric = new BfGenericParamDef();
			*newGeneric = *generic;
			newMethod->mGenericParams[genericIdx] = newGeneric;
		}
		if (ignoreNewMethod)
			newMethod->mMethodType = BfMethodType_Ignore;
		typeDef->mMethods.push_back(newMethod);
	}
	typeDef->mMethodSet.Clear();

	for (auto prop : partialTypeDef->mProperties)
	{
		BfPropertyDef* newProp = new BfPropertyDef();
		*newProp = *prop;
		BF_ASSERT(newProp->mDeclaringType != NULL);
		for (int methodIdx = 0; methodIdx < (int)newProp->mMethods.size(); methodIdx++)
			newProp->mMethods[methodIdx] = typeDef->mMethods[startMethodIdx + newProp->mMethods[methodIdx]->mIdx];
		typeDef->mProperties.push_back(newProp);
	}
	typeDef->mPropertySet.Clear();

	BF_ASSERT(partialTypeDef->mPartials.empty());
	partialTypeDef->mPartialIdx = (int)typeDef->mPartials.size();
	typeDef->mPartials.push_back(partialTypeDef);

	VerifyTypeDef(typeDef);

	typeDef->mHash = partialTypeDef->mHash;
	typeDef->mSignatureHash = Hash128(&partialTypeDef->mSignatureHash, sizeof(Val128), typeDef->mSignatureHash);
	typeDef->mFullHash = Hash128(&partialTypeDef->mFullHash, sizeof(Val128), typeDef->mFullHash);
	typeDef->mInlineHash = Hash128(&partialTypeDef->mInlineHash, sizeof(Val128), typeDef->mInlineHash);

	VerifyTypeDef(compositeTypeDef);
	VerifyTypeDef(typeDef);
}

void BfSystem::FinishCompositePartial(BfTypeDef* compositeTypeDef)
{
	VerifyTypeDef(compositeTypeDef);

	auto nextRevision = compositeTypeDef->mNextRevision;

	struct _HasMethods
	{
		int mCtor;
		int mCtorPublic;
		int mDtor;
		int mMark;
	};

	_HasMethods allHasMethods[2][2] = { 0 };
	auto primaryDef = nextRevision->mPartials[0];

	//Dictionary<BfProject*, int> projectCount;

	bool hasCtorNoBody = false;

	bool primaryHasFieldInitializers = false;
	bool anyHasInitializers = false;

	// For methods that require chaining, make sure the primary def has a definition
	for (auto partialTypeDef : nextRevision->mPartials)
	{
		bool isExtension = partialTypeDef->mTypeDeclaration != nextRevision->mTypeDeclaration;
		if (!isExtension)
			primaryDef = partialTypeDef;

		bool hasInitializers = false;
		for (auto methodDef : partialTypeDef->mMethods)
		{
			if ((methodDef->mMethodType == BfMethodType_Init) && (!methodDef->mIsStatic))
				hasInitializers = true;
			auto& hasMethods = allHasMethods[isExtension ? 1 : 0][methodDef->mIsStatic ? 1 : 0];
			if (methodDef->mMethodType == BfMethodType_Ctor)
			{
				hasMethods.mCtor++;
				if (methodDef->mProtection == BfProtection_Public)
					hasMethods.mCtorPublic++;

				if ((methodDef->mParams.size() == 0) && (!methodDef->mIsStatic) && (methodDef->mBody == NULL))
				{
					hasCtorNoBody = true;
				}
			}
			else if (methodDef->mMethodType == BfMethodType_Dtor)
				hasMethods.mDtor++;
			else if (methodDef->mMethodType == BfMethodType_Normal)
			{
				if ((methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))
					hasMethods.mMark++;
			}
		}

		for (auto fieldDef : partialTypeDef->mFields)
		{
			if (!fieldDef->mIsStatic)
			{
				if (auto fieldDeclaration = BfNodeDynCast<BfFieldDeclaration>(fieldDef->mFieldDeclaration))
					if (fieldDeclaration->mInitializer != NULL)
						hasInitializers = true;
				if (auto paramDeclaration = BfNodeDynCast<BfParameterDeclaration>(fieldDef->mFieldDeclaration))
					if (paramDeclaration->mInitializer != NULL)
						hasInitializers = true;
				if (fieldDef->mIsAppend)
					hasInitializers = true;
			}
		}

		if (hasInitializers)
		{
			anyHasInitializers = true;
			if (!isExtension)
				primaryHasFieldInitializers = true;
			nextRevision->mHasCtorNoBody = true;
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_CtorNoBody, BfProtection_Protected, false, "");
			methodDef->mDeclaringType = partialTypeDef;
			methodDef->mIsMutating = true;
		}
	}

	if ((anyHasInitializers) && (!primaryHasFieldInitializers))
	{
		nextRevision->mHasCtorNoBody = true;
		auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_CtorNoBody, BfProtection_Protected, false, "");
		methodDef->mDeclaringType = primaryDef;
		methodDef->mIsMutating = true;
	}

	if ((allHasMethods[0][0].mCtor == 0) && (allHasMethods[1][0].mCtor > 1))
	{
		auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Ctor, (allHasMethods[1][0].mCtorPublic > 0) ? BfProtection_Public : BfProtection_Protected, false, "");
		methodDef->mDeclaringType = primaryDef;
		methodDef->mIsMutating = true;
	}

// 	if (!hasCtorNoBody)
// 	{
// 		auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_CtorNoBody, BfProtection_Protected, false, "");
// 		methodDef->mDeclaringType = primaryDef;
// 		methodDef->mIsMutating = true;
// 	}

	if (!primaryDef->IsGlobalsContainer())
	{
		// Static ctor
		if ((allHasMethods[0][1].mCtor == 0) && (allHasMethods[1][1].mCtor > 1))
		{
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Ctor, BfProtection_Public, true, "");
			methodDef->mDeclaringType = primaryDef;
		}

		if ((allHasMethods[0][0].mDtor == 0) && (allHasMethods[1][0].mDtor > 1))
		{
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Dtor, BfProtection_Public, false, "");
			methodDef->mDeclaringType = primaryDef;
		}

		if ((allHasMethods[0][1].mDtor == 0) && (allHasMethods[1][1].mDtor > 1))
		{
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Dtor, BfProtection_Public, true, "");
			methodDef->mDeclaringType = primaryDef;
		}

		if ((allHasMethods[0][0].mMark == 0) && (allHasMethods[1][0].mMark > 1))
		{
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Normal, BfProtection_Public, false, BF_METHODNAME_MARKMEMBERS);
			methodDef->mDeclaringType = primaryDef;
			methodDef->mIsVirtual = true;
			methodDef->mIsOverride = true;
		}

		if ((allHasMethods[0][1].mMark == 0) && (allHasMethods[1][1].mMark > 1))
		{
			auto methodDef = BfDefBuilder::AddMethod(nextRevision, BfMethodType_Normal, BfProtection_Public, true, BF_METHODNAME_MARKMEMBERS_STATIC);
			methodDef->mDeclaringType = primaryDef;
		}
	}

	// If this fails, it's probably because there were no actual composite pieces to put into it
	BF_ASSERT(nextRevision != NULL);
	if ((nextRevision->mDefState == BfTypeDef::DefState_Signature_Changed) || (compositeTypeDef->mSignatureHash != nextRevision->mSignatureHash))
		compositeTypeDef->mDefState = BfTypeDef::DefState_Signature_Changed;
	else if ((nextRevision->mDefState == BfTypeDef::DefState_InlinedInternals_Changed) || (compositeTypeDef->mInlineHash != nextRevision->mInlineHash))
		compositeTypeDef->mDefState = BfTypeDef::DefState_InlinedInternals_Changed;
	else if ((nextRevision->mDefState == BfTypeDef::DefState_Internals_Changed) || (compositeTypeDef->mFullHash != nextRevision->mFullHash))
		compositeTypeDef->mDefState = BfTypeDef::DefState_Internals_Changed;
	else if (nextRevision->mDefState == BfTypeDef::DefState_Refresh)
		compositeTypeDef->mDefState = BfTypeDef::DefState_Refresh;
	//InjectNewRevision(compositeTypeDef);

	VerifyTypeDef(compositeTypeDef);
	VerifyTypeDef(nextRevision);
}

void BfSystem::CopyTypeDef(BfTypeDef* typeDef, BfTypeDef* fromTypeDef)
{
	BfLogSys(this, "CopyTypeDef %p from %p Hash: %d\n", typeDef, fromTypeDef, fromTypeDef->mHash);

	for (auto fromMethodDef : fromTypeDef->mMethods)
	{
		BfMethodDef* methodDef;
		if (fromMethodDef->mIsOperator)
		{
			auto fromOperatorDef = (BfOperatorDef*)fromMethodDef;
			auto operatorDef = new BfOperatorDef();
			methodDef = operatorDef;
			*operatorDef = *fromOperatorDef;
		}
		else
		{
			methodDef = new BfMethodDef();
			*methodDef = *fromMethodDef;
		}
		fromMethodDef->mParamNameMap = NULL;

		if (methodDef->mDeclaringType == fromTypeDef)
			methodDef->mDeclaringType = typeDef;

		for (int paramIdx = 0; paramIdx < fromMethodDef->mParams.mSize; paramIdx++)
		{
			auto fromParamDef = fromMethodDef->mParams[paramIdx];
			BfParameterDef* paramDef = new BfParameterDef();
			*paramDef = *fromParamDef;
			methodDef->mParams[paramIdx] = paramDef;
		}

		for (int genericParamIdx = 0; genericParamIdx < fromMethodDef->mGenericParams.mSize; genericParamIdx++)
		{
			auto fromGenericParam = fromMethodDef->mGenericParams[genericParamIdx];
			BfGenericParamDef* genericParam = new BfGenericParamDef();
			*genericParam = *fromGenericParam;
			methodDef->mGenericParams[genericParamIdx] = genericParam;
		}

		methodDef->mNextWithSameName = NULL;
		typeDef->mMethods.Add(methodDef);
	}

	for (auto operatorDef : fromTypeDef->mOperators)
	{
		auto methodDef = typeDef->mMethods[operatorDef->mIdx];
		BF_ASSERT(methodDef->mIsOperator);
		if (methodDef->mIsOperator)
			typeDef->mOperators.Add((BfOperatorDef*)methodDef);
	}

	for (auto fromPropDef : fromTypeDef->mProperties)
	{
		BfPropertyDef* propDef = new BfPropertyDef();
		*propDef = *fromPropDef;
		if (propDef->mDeclaringType == fromTypeDef)
			propDef->mDeclaringType = typeDef;
		for (auto& methodDef : propDef->mMethods)
			methodDef = typeDef->mMethods[methodDef->mIdx];
		propDef->mNextWithSameName = NULL;
		typeDef->mProperties.Add(propDef);
	}

	for (auto fromField : fromTypeDef->mFields)
	{
		BfFieldDef* fieldDef = new BfFieldDef();
		*fieldDef = *fromField;
		if (fieldDef->mDeclaringType == fromTypeDef)
			fieldDef->mDeclaringType = typeDef;
		fieldDef->mNextWithSameName = NULL;
		typeDef->mFields.Add(fieldDef);
	}

	typeDef->mSystem = fromTypeDef->mSystem;
	typeDef->mProject = fromTypeDef->mProject;
	typeDef->mPartialIdx = fromTypeDef->mPartialIdx;
	typeDef->mTypeDeclaration = fromTypeDef->mTypeDeclaration;
	typeDef->mHash = fromTypeDef->mHash;
	typeDef->mSignatureHash = fromTypeDef->mSignatureHash;
	typeDef->mFullHash = fromTypeDef->mFullHash;
	typeDef->mInlineHash = fromTypeDef->mInlineHash;
	typeDef->mNestDepth = fromTypeDef->mNestDepth;

	typeDef->mOuterType = fromTypeDef->mOuterType;
	//typeDef->mOuterType = fromTypeDef->mOuterType;
	typeDef->mNamespace = fromTypeDef->mNamespace;

	typeDef->mName = fromTypeDef->mName;
	if (typeDef->mName != mEmptyAtom)
		typeDef->mName->mRefCount++;

	//typeDef->mName = fromTypeDef->mName;
	typeDef->mNameEx = fromTypeDef->mNameEx;
	if (typeDef->mNameEx != NULL)
		typeDef->mNameEx->mRefCount++;
	//typeDef->mNameEx = fromTypeDef->mNameEx;
	typeDef->mFullName = fromTypeDef->mFullName;

	typeDef->mFullNameEx = fromTypeDef->mFullNameEx;
	//RefAtomComposite(typeDef->mFullNameEx);

	typeDef->mProtection = fromTypeDef->mProtection;

	typeDef->mTypeCode = fromTypeDef->mTypeCode;
	typeDef->mShow = fromTypeDef->mShow;
	typeDef->mIsAlwaysInclude = fromTypeDef->mIsAlwaysInclude;
	typeDef->mIsNoDiscard = fromTypeDef->mIsNoDiscard;
	typeDef->mIsPartial = fromTypeDef->mIsPartial;
	typeDef->mIsExplicitPartial = fromTypeDef->mIsExplicitPartial;
	//mPartialUsed
	typeDef->mIsCombinedPartial = fromTypeDef->mIsCombinedPartial;
	typeDef->mIsDelegate = fromTypeDef->mIsDelegate;
	typeDef->mIsFunction = fromTypeDef->mIsFunction;
	typeDef->mIsClosure = fromTypeDef->mIsClosure;
	typeDef->mIsAbstract = fromTypeDef->mIsAbstract;
	typeDef->mIsStatic = fromTypeDef->mIsStatic;
	typeDef->mHasAppendCtor = fromTypeDef->mHasAppendCtor;
	typeDef->mHasCEOnCompile = fromTypeDef->mHasCEOnCompile;
	typeDef->mHasCtorNoBody = fromTypeDef->mHasCtorNoBody;
	typeDef->mHasOverrideMethods = fromTypeDef->mHasOverrideMethods;
	typeDef->mHasExtensionMethods = fromTypeDef->mHasExtensionMethods;
	typeDef->mHasUsingFields = fromTypeDef->mHasUsingFields;
	typeDef->mIsOpaque = fromTypeDef->mIsOpaque;

	typeDef->mDupDetectedRevision = fromTypeDef->mDupDetectedRevision;

	typeDef->mDirectAllocNodes = fromTypeDef->mDirectAllocNodes;
	fromTypeDef->mDirectAllocNodes.Clear();

	typeDef->mNamespaceSearch = fromTypeDef->mNamespaceSearch;
	for (auto name : typeDef->mNamespaceSearch)
		RefAtomComposite(name);

	typeDef->mStaticSearch = fromTypeDef->mStaticSearch;
	typeDef->mInternalAccessSet = fromTypeDef->mInternalAccessSet;

	for (auto fromGenericParamDef : fromTypeDef->mGenericParamDefs)
	{
		BfGenericParamDef* genericParamDef = new BfGenericParamDef();
		*genericParamDef = *fromGenericParamDef;
		typeDef->mGenericParamDefs.Add(genericParamDef);
	}

	typeDef->mExternalConstraints = fromTypeDef->mExternalConstraints;

	typeDef->mBaseTypes = fromTypeDef->mBaseTypes;
	typeDef->mNestedTypes = fromTypeDef->mNestedTypes;

	typeDef->mPartials = fromTypeDef->mPartials;

	VerifyTypeDef(typeDef);
}

void BfSystem::UpdateEmittedTypeDef(BfTypeDef* typeDef)
{
	auto fromTypeDef = typeDef->mEmitParent;

	BF_ASSERT(fromTypeDef->mNextRevision == NULL);

	BfLogSys(this, "UpdateTypeDefCopy %p from %p (decl:%p)\n", typeDef, fromTypeDef, fromTypeDef->mTypeDeclaration);

	BF_ASSERT((typeDef->mDefState == BfTypeDef::DefState_Emitted) || (typeDef->mDefState == BfTypeDef::DefState_EmittedDirty));
	BF_ASSERT((fromTypeDef->mDefState != BfTypeDef::DefState_Emitted) && (fromTypeDef->mDefState != BfTypeDef::DefState_EmittedDirty));

	typeDef->mTypeDeclaration = fromTypeDef->mTypeDeclaration;
	typeDef->mOuterType = fromTypeDef->mOuterType;

	for (int methodIdx = 0; methodIdx < (int)typeDef->mMethods.size(); methodIdx++)
	{
		auto methodDef = typeDef->mMethods[methodIdx];
		if (methodIdx >= fromTypeDef->mMethods.mSize)
		{
			BF_ASSERT(methodDef->mDeclaringType == typeDef);
			continue;
		}

		BF_ASSERT(methodDef->mDeclaringType != typeDef);

		for (auto param : methodDef->mParams)
			delete param;
		for (auto genericParam : methodDef->mGenericParams)
			delete genericParam;

		auto fromMethodDef = fromTypeDef->mMethods[methodIdx];

		if ((fromMethodDef->mIsOperator) && (methodDef->mIsOperator))
		{
			auto fromOperatorDef = (BfOperatorDef*)fromMethodDef;
			auto operatorDef = (BfOperatorDef*)methodDef;
			*operatorDef = *fromOperatorDef;
		}
		else
		{
			*methodDef = *fromMethodDef;
		}

		for (int paramIdx = 0; paramIdx < fromMethodDef->mParams.mSize; paramIdx++)
		{
			auto fromParamDef = fromMethodDef->mParams[paramIdx];
			BfParameterDef* paramDef = new BfParameterDef();
			*paramDef = *fromParamDef;
			methodDef->mParams[paramIdx] = paramDef;
		}

		for (int genericParamIdx = 0; genericParamIdx < fromMethodDef->mGenericParams.mSize; genericParamIdx++)
		{
			auto fromGenericParam = fromMethodDef->mGenericParams[genericParamIdx];
			BfGenericParamDef* genericParam = new BfGenericParamDef();
			*genericParam = *fromGenericParam;
			methodDef->mGenericParams[genericParamIdx] = genericParam;
		}
	}

	typeDef->mOperators.Clear();
	for (auto operatorDef : fromTypeDef->mOperators)
	{
		auto methodDef = typeDef->mMethods[operatorDef->mIdx];
		BF_ASSERT(methodDef->mIsOperator);
		if (methodDef->mIsOperator)
			typeDef->mOperators.Add((BfOperatorDef*)methodDef);
	}

	for (int fieldIdx = 0; fieldIdx < typeDef->mFields.mSize; fieldIdx++)
	{
		auto fieldDef = typeDef->mMethods[fieldIdx];
		if (fieldIdx >= fromTypeDef->mFields.mSize)
		{
			BF_ASSERT(fieldDef->mDeclaringType == typeDef);
			continue;
		}

		BF_ASSERT(fieldDef->mDeclaringType != typeDef);
		auto fromFieldDef = fromTypeDef->mMethods[fieldIdx];
		fieldDef->mDeclaringType = fromFieldDef->mDeclaringType;
	}

	for (int propertyIdx = 0; propertyIdx < typeDef->mProperties.mSize; propertyIdx++)
	{
		auto propertyDef = typeDef->mProperties[propertyIdx];
		if (propertyIdx >= fromTypeDef->mProperties.mSize)
		{
			BF_ASSERT(propertyDef->mDeclaringType == typeDef);
			continue;
		}

		BF_ASSERT(propertyDef->mDeclaringType != typeDef);
		auto fromPropertyDef = fromTypeDef->mProperties[propertyIdx];
		propertyDef->mDeclaringType = fromPropertyDef->mDeclaringType;
	}

	typeDef->mGenericParamDefs.Clear();
	for (auto fromGenericParamDef : fromTypeDef->mGenericParamDefs)
	{
		BfGenericParamDef* genericParamDef = new BfGenericParamDef();
		*genericParamDef = *fromGenericParamDef;
		typeDef->mGenericParamDefs.Add(genericParamDef);
	}

	typeDef->mPartials = fromTypeDef->mPartials;

	if (typeDef->mDefState == BfTypeDef::DefState_EmittedDirty)
		typeDef->mDefState = BfTypeDef::DefState_Emitted;

	BF_ASSERT(typeDef->mDefState == BfTypeDef::DefState_Emitted);
}

BfTypeDef* BfSystem::GetCombinedPartial(BfTypeDef* typeDef)
{
	if ((!typeDef->mIsPartial) || (typeDef->mIsCombinedPartial))
		return typeDef;

	bool foundPartial = false;
	BfTypeDef* checkTypeDef = typeDef;
	auto itr = mTypeDefs.TryGet(checkTypeDef->mFullName);
	do
	{
		if (checkTypeDef == typeDef)
			foundPartial = true;
		checkTypeDef = *itr;
		itr.MoveToNextHashMatch();
	} while ((!checkTypeDef->mIsCombinedPartial) || (!foundPartial));
	return checkTypeDef;
}

BfTypeDef* BfSystem::GetOuterTypeNonPartial(BfTypeDef* typeDef)
{
	auto checkType = typeDef->mOuterType;
	if ((checkType == NULL) || (!checkType->mIsPartial))
		return checkType;

	return GetCombinedPartial(checkType);
}

int BfSystem::GetGenericParamIdx(const Array<BfGenericParamDef*>& genericParams, const StringImpl& name)
{
	for (int i = 0; i < (int)genericParams.size(); i++)
		if (genericParams[i]->mName == name)
			return i;
	return -1;
}

int BfSystem::GetGenericParamIdx(const Array<BfGenericParamDef*>& genericParams, BfTypeReference* typeRef)
{
	if ((typeRef == NULL) || (!typeRef->IsA<BfNamedTypeReference>()))
		return -1;
	return GetGenericParamIdx(genericParams, typeRef->ToString());
}

void BfSystem::StartYieldSection()
{
	mYieldTickCount = BFTickCount();
	mHighestYieldTime = 0;
}

void BfSystem::SummarizeYieldSection()
{
	OutputDebugStrF("Highest yield time: %d\n", mHighestYieldTime);
}

void BfSystem::CheckLockYield()
{
	if (mYieldDisallowCount != 0)
		return;

	//uint32 curTime = BFTickCount();
	//int yieldTime = (int)(curTime - mYieldTickCount);
	//mHighestYieldTime = BF_MAX(yieldTime, mHighestYieldTime);
	//mYieldTickCount = curTime;

	if ((mPendingSystemLockPri > mCurSystemLockPri) && (mCurSystemLockThreadId == BfpThread_GetCurrentId()))
	{
		int mySystemLockPri = mCurSystemLockPri;
		BF_ASSERT(mSystemLock.mLockCount == 1);
		mSystemLock.Unlock();
		// Wait for the other thread to actually acquire the lock.  This only spins between the time
		//  we get a NotifyWillRequestLock and when that thread actually does the Lock
		while (mPendingSystemLockPri != -1)
		{
            BfpThread_Yield();
		}
		Lock(mySystemLockPri);
        mCurSystemLockThreadId = BfpThread_GetCurrentId();
	}
}

void BfSystem::NotifyWillRequestLock(int priority)
{
	mPendingSystemLockPri = priority;
}

void BfSystem::Lock(int priority)
{
#ifdef  _DEBUG
	if (priority > 0)
	{
		if (!mSystemLock.TryLock(10))
			mSystemLock.Lock();
	}
	else
		mSystemLock.Lock();
#else
	mSystemLock.Lock();
#endif
	BF_ASSERT(mSystemLock.mLockCount == 1);
	if (mPendingSystemLockPri == priority)
		mPendingSystemLockPri = -1;
	mCurSystemLockPri = priority;
    mCurSystemLockThreadId = BfpThread_GetCurrentId();
}

void BfSystem::Unlock()
{
	BF_ASSERT(mYieldDisallowCount == 0);

	mCurSystemLockPri = -1;
	mSystemLock.Unlock();
	BF_ASSERT(mSystemLock.mLockCount >= 0);
}

void BfSystem::AssertWeHaveLock()
{
#ifdef BF_PLATFORM_WINDOWS
	BF_ASSERT_REL(((CRITICAL_SECTION*)mSystemLock.mCritSect)->OwningThread == (HANDLE)(uintptr)::GetCurrentThreadId());
#endif
}

void BfSystem::RemoveDeletedParsers()
{
	while (true)
	{
		BfParser* bfParser = NULL;
		{
			AutoCrit crit(mDataLock);
			if (mParserDeleteQueue.size() == 0)
				break;
			bfParser = mParserDeleteQueue.back();
			mParserDeleteQueue.pop_back();
			/*auto itr = std::find(mParsers.begin(), mParsers.end(), bfParser);
			BF_ASSERT(itr != mParsers.end());
			mParsers.erase(itr);*/
			bool wasRemoved = mParsers.Remove(bfParser);
			BF_ASSERT(wasRemoved);
		}

		BfLogSys(this, "Removing Queued Parser: %p\n", bfParser);
		delete bfParser;
		CheckLockYield();
	}
}

void BfSystem::RemoveOldParsers()
{
	mDataLock.Lock();

	// We can't be allowed to delete old parsers if the new typedefs haven't been
	//  injected yet by the compiler
	if (mNeedsTypesHandledByCompiler)
	{
		mDataLock.Unlock();
		return;
	}

	RemoveDeletedParsers();

	for (int i = 0; i < (int)mParsers.size(); i++)
	{
		auto bfParser = mParsers[i];

		bool wantsDelete = false;

		if (bfParser->mRefCount == 0)
		{
			if ((bfParser->mNextRevision != NULL) || (bfParser->mAwaitingDelete))
			{
				if (bfParser->mNextRevision != NULL)
					bfParser->mNextRevision->mPrevRevision = bfParser->mPrevRevision;
				if (bfParser->mPrevRevision != NULL)
					bfParser->mPrevRevision->mNextRevision = bfParser->mNextRevision;

				BfLogSys(this, "Deleting Old Parser: %p  New Parser: %p\n", bfParser, bfParser->mNextRevision);

				mParsers.RemoveAt(i);
				i--;

				mDataLock.Unlock();
				delete bfParser;
				mDataLock.Lock();
			}
		}
	}
	mDataLock.Unlock();
}

void BfSystem::RemoveOldData()
{
	{
		AutoCrit autoCrit(mDataLock);

		for (int i = 0; i < (int)mTypeDefDeleteQueue.size(); i++)
		{
			auto typeDef = mTypeDefDeleteQueue[i];
			mTypeDefDeleteQueue[i] = NULL;
			BfLogSys(this, "RemoveOldData deleting from mTypeDefDeleteQueue %p\n", typeDef);
			delete typeDef;
		}
		mTypeDefDeleteQueue.Clear();

		if (!mProjectDeleteQueue.IsEmpty())
		{
			for (auto project : mProjectDeleteQueue)
				project->mDeleteStage = BfProject::DeleteStage_AwaitingRefs;

			for (auto typeDef : mTypeDefs)
				if (typeDef->mProject->mDeleteStage == BfProject::DeleteStage_AwaitingRefs)
					typeDef->mProject->mDeleteStage = BfProject::DeleteStage_Queued;

			for (auto parser : mParsers)
				if ((parser->mProject != NULL) && (parser->mProject->mDeleteStage == BfProject::DeleteStage_AwaitingRefs))
					parser->mProject->mDeleteStage = BfProject::DeleteStage_Queued;

			for (int projectIdx = 0; projectIdx < (int)mProjectDeleteQueue.size(); projectIdx++)
			{
				auto project = mProjectDeleteQueue[projectIdx];
				if (project->mDeleteStage == BfProject::DeleteStage_AwaitingRefs)
				{
					mProjectDeleteQueue.RemoveAtFast(projectIdx);
					delete project;
					projectIdx--;
				}
			}
		}
	}

	RemoveOldParsers();
}

void BfSystem::VerifyTypeDef(BfTypeDef* typeDef)
{
#if defined _DEBUG && false
	auto _FindTypeDef = [&](BfTypeReference* typeRef)
	{
		if (auto directStrTypeRef = BfNodeDynCast<BfDirectStrTypeReference>(typeRef))
		{
			bool found = false;

			for (auto directRef : typeDef->mDirectAllocNodes)
				if (directRef == directStrTypeRef)
					found = true;

			for (auto partialTypeDef : typeDef->mPartials)
			{
				for (auto directRef : partialTypeDef->mDirectAllocNodes)
					if (directRef == directStrTypeRef)
						found = true;
			}

			for (auto directRef : mDirectTypeRefs)
				if (directRef == directStrTypeRef)
					found = true;

			BF_ASSERT(found);
		}
	};

	for (auto methodDef : typeDef->mMethods)
	{
		_FindTypeDef(methodDef->mReturnTypeRef);
		for (auto paramDef : methodDef->mParams)
		{
			_FindTypeDef(paramDef->mTypeRef);
		}
	}
#endif
}

BfTypeOptions* BfSystem::GetTypeOptions(int optionsIdx)
{
	BF_ASSERT(optionsIdx != -2);
	if (optionsIdx < 0)
		return NULL;
	if (optionsIdx < mTypeOptions.size())
		return &mTypeOptions[optionsIdx];
	return &mMergedTypeOptions[optionsIdx - mTypeOptions.size()];
}

bool BfSystem::HasTestProjects()
{
	for (auto project : mProjects)
		if (project->mTargetType == BfTargetType_BeefTest)
			return true;
	return false;
}

bool BfSystem::IsCompatibleCallingConvention(BfCallingConvention callConvA, BfCallingConvention callConvB)
{
	if (mPtrSize == 8)
		return true; // There's only one 64-bit calling convention
	if (callConvA == BfCallingConvention_Unspecified)
		callConvA = BfCallingConvention_Cdecl;
	if (callConvB == BfCallingConvention_Unspecified)
		callConvB = BfCallingConvention_Cdecl;
	return callConvA == callConvB;
}

//////////////////////////////////////////////////////////////////////////
BF_EXPORT BfSystem* BF_CALLTYPE BfSystem_Create()
{
	auto bfSystem = new BfSystem();
	return bfSystem;
}

void BfReportMemory();

BF_EXPORT void BF_CALLTYPE BfSystem_Delete(BfSystem* bfSystem)
{
	//OutputDebugStrF("Before Deleting BfSystem ");
	//BfReportMemory();
	delete bfSystem;
	//OutputDebugStrF("After Deleting BfSystem ");
	//BfReportMemory();
}

BF_EXPORT void BF_CALLTYPE BfSystem_CheckLock(BfSystem* bfSystem)
{
	BF_ASSERT(bfSystem->mSystemLock.mLockCount == 0);
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_Delete(BfResolvePassData* resolvePassData)
{
	delete resolvePassData->mAutoComplete;
	for (auto tempType : resolvePassData->mAutoCompleteTempTypes)
		delete tempType;
	delete resolvePassData;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetLocalId(BfResolvePassData* resolvePassData, int localId)
{
	resolvePassData->mSymbolReferenceLocalIdx = localId;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Local;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetTypeGenericParamIdx(BfResolvePassData* resolvePassData, int typeGenericParamIdx)
{
	resolvePassData->mSymbolTypeGenericParamIdx = typeGenericParamIdx;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_TypeGenericParam;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetMethodGenericParamIdx(BfResolvePassData* resolvePassData, int methodGenericParamIdx)
{
	resolvePassData->mSymbolMethodGenericParamIdx = methodGenericParamIdx;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_MethodGenericParam;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetSymbolReferenceTypeDef(BfResolvePassData* resolvePassData, const char* replaceTypeDef)
{
	resolvePassData->mQueuedReplaceTypeDef = replaceTypeDef;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Type;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetSymbolReferenceNamespace(BfResolvePassData* resolvePassData, const char* namespaceName)
{
	resolvePassData->mQueuedSymbolReferenceNamespace = namespaceName;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Namespace;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetSymbolReferenceFieldIdx(BfResolvePassData* resolvePassData, int fieldIdx)
{
	resolvePassData->mSymbolReferenceFieldIdx = fieldIdx;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Field;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetSymbolReferenceMethodIdx(BfResolvePassData* resolvePassData, int methodIdx)
{
	resolvePassData->mSymbolReferenceMethodIdx = methodIdx;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Method;
}

BF_EXPORT void BF_CALLTYPE BfResolvePassData_SetSymbolReferencePropertyIdx(BfResolvePassData* resolvePassData, int propertyIdx)
{
	resolvePassData->mSymbolReferencePropertyIdx = propertyIdx;
	resolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_Property;
}

BF_EXPORT void BfResolvePassData_SetDocumentationRequest(BfResolvePassData* resolvePassData, char* entryName)
{
	resolvePassData->mAutoComplete->mDocumentationEntryName = entryName;
}

BF_EXPORT void BfResolvePassData_AddEmitEmbed(BfResolvePassData* resolvePassData, char* typeName, int32 cursorIdx)
{
	BfEmitEmbedEntry emitEmbedEntry;
	emitEmbedEntry.mCursorIdx = cursorIdx;
	resolvePassData->mEmitEmbedEntries[typeName] = emitEmbedEntry;
}

BF_EXPORT void* BfResolvePassData_GetEmitEmbedData(BfResolvePassData* resolvePassData, char* typeName, int* charCount, int* revision)
{
	*charCount = -1;
	*revision = 0;

	BfEmitEmbedEntry* emitEmbedEntry = NULL;
	if (!resolvePassData->mEmitEmbedEntries.TryGetValue(typeName, &emitEmbedEntry))
		return NULL;
	if (emitEmbedEntry->mParser == NULL)
		return NULL;
	*revision = emitEmbedEntry->mRevision;
	*charCount = emitEmbedEntry->mParser->mSrcLength;

	auto emitParser = emitEmbedEntry->mParser;
	emitParser->mSourceClassifier->FlushDeferredNodes();

	return emitEmbedEntry->mParser->mSourceClassifier->mCharData;
}

BF_EXPORT bool BfResolvePassData_GetHadEmits(BfResolvePassData* resolvePassData)
{
	return resolvePassData->mHadEmits;
}

BF_EXPORT BfParser* BF_CALLTYPE BfSystem_CreateParser(BfSystem* bfSystem, BfProject* bfProject)
{
	return bfSystem->CreateParser(bfProject);
}

BF_EXPORT void BF_CALLTYPE BfSystem_DeleteParser(BfSystem* bfSystem, BfParser* bfParser)
{
	BfLogSys(bfSystem, "BfSystem_DeleteParser: %p\n", bfParser);
	AutoCrit crit(bfSystem->mDataLock);
	bfParser->mAwaitingDelete = true;

	if (bfParser->mNextRevision == NULL)
	{
		for (auto typeDef : bfParser->mTypeDefs)
		{
			BfLogSys(bfSystem, "BfSystem_DeleteParser %p deleting typeDef %p\n", bfParser, typeDef);
			typeDef->mDefState = BfTypeDef::DefState_Deleted;
		}
	}
	//bfSystem->mParserDeleteQueue.push_back(bfParser);
}

class BfLocationNameVisitor : public BfElementVisitor
{
public:
	int mIndex;
	String mName;

	bool TryNode(BfAstNode* node)
	{
		if ((mIndex >= node->mSrcStart) && (mIndex < node->mSrcEnd))
		{
			if (!mName.IsEmpty())
				mName += ".";
			return true;
		}
		return false;
	}

	virtual void Visit(BfTypeDeclaration* typeDeclaration)
	{
		if ((typeDeclaration->mNameNode != NULL) && (TryNode(typeDeclaration)))
		{
			typeDeclaration->mNameNode->ToString(mName);
		}

		BfElementVisitor::Visit(typeDeclaration);
	}

	virtual void Visit(BfMethodDeclaration* methodDeclaration)
	{
		if ((methodDeclaration->mNameNode != NULL) && (TryNode(methodDeclaration)))
		{
			if (methodDeclaration->mNameNode != NULL)
				methodDeclaration->mNameNode->ToString(mName);
		}

		BfElementVisitor::Visit(methodDeclaration);
	}

	virtual void Visit(BfPropertyDeclaration* propertyDeclaration)
	{
		if ((propertyDeclaration->mNameNode != NULL) && (TryNode(propertyDeclaration)))
		{
			if (propertyDeclaration->mNameNode != NULL)
				propertyDeclaration->mNameNode->ToString(mName);
		}

		BfElementVisitor::Visit(propertyDeclaration);
	}
};

BF_EXPORT const char* BfSystem_GetParserLocationName(BfParser* parser, int line, int column)
{
	int idx = parser->GetIndexAtLine(line);
	if (idx == -1)
		return NULL;
	idx += column;

	BfLocationNameVisitor visitor;
	visitor.mIndex = idx;
	visitor.VisitMembers(parser->mRootNode);

	String& outString = *gTLStrReturn.Get();
	outString = visitor.mName;
	return outString.c_str();
}

BF_EXPORT BfCompiler* BF_CALLTYPE BfSystem_CreateCompiler(BfSystem* bfSystem, bool isResolveOnly)
{
	return bfSystem->CreateCompiler(isResolveOnly);
}

BF_EXPORT const char* BF_CALLTYPE BfPassInstance_PopOutString(BfPassInstance* bfPassInstance)
{
	String& outString = *gTLStrReturn.Get();
	if (!bfPassInstance->PopOutString(&outString))
		return NULL;
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE BfPassInstance_SetClassifierPassId(BfPassInstance* bfPassInstance, uint8 classifierPassId)
{
	bfPassInstance->mClassifierPassId = classifierPassId;
}

BF_EXPORT int BF_CALLTYPE BfPassInstance_GetErrorCount(BfPassInstance* bfPassInstance)
{
	return (int)bfPassInstance->mErrors.size();
}

BF_EXPORT const char* BF_CALLTYPE BfPassInstance_GetErrorData(BfPassInstance* bfPassInstance, int errorIdx, int& outCode, bool& outIsWarning, bool& outIsAfter, bool& outIsDeferred, BfWhileSpecializingFlags& outIsWhileSpecializing, bool& outIsPersistent,
	char*& projectName, char*& fileName, int& outSrcStart, int& outSrcEnd, int* outLine, int* outColumn, int& outMoreInfoCount)
{
	BfError* bfError = bfPassInstance->mErrors[errorIdx];
	outIsWarning = bfError->mIsWarning;
	outIsAfter = bfError->mIsAfter;
	outIsDeferred = bfError->mIsDeferred;
	outIsWhileSpecializing = bfError->mIsWhileSpecializing;
	outIsPersistent = bfError->mIsPersistent;
	outCode = bfError->mWarningNumber;
	if (bfError->mProject != NULL)
		projectName = (char*)bfError->mProject->mName.c_str();
	if (bfError->mSource != NULL)
	{
		String* srcFileName;
		if (bfPassInstance->mSourceFileNameMap.TryGetValue(bfError->mSource, &srcFileName))
		{
			fileName = (char*)srcFileName->c_str();
		}

		if (outLine != NULL)
		{
			auto parserData = bfError->mSource->ToParserData();
			if (parserData != NULL)
			{
				parserData->GetLineCharAtIdx(bfError->mSrcStart, *outLine, *outColumn);
			}
		}
	}

	if (bfError->mLocation != NULL)
	{
		fileName = (char*)bfError->mLocation->mFile.c_str();
		*outLine = bfError->mLocation->mLine;
		*outColumn = bfError->mLocation->mColumn;
	}

	outSrcStart = bfError->mSrcStart;
	outSrcEnd = bfError->mSrcEnd;
	outMoreInfoCount = (int)bfError->mMoreInfo.size();
	return bfError->mError.c_str();
}

BF_EXPORT const char* BfPassInstance_Error_GetMoreInfoData(BfPassInstance* bfPassInstance, int errorIdx, int moreInfoIdx, char*& fileName, int& srcStart, int& srcEnd, int* outLine, int* outColumn)
{
	BfError* rootError = bfPassInstance->mErrors[errorIdx];
	BfMoreInfo* moreInfo = rootError->mMoreInfo[moreInfoIdx];
	if (moreInfo->mSource != NULL)
	{
		String* srcFileName;
		if (bfPassInstance->mSourceFileNameMap.TryGetValue(moreInfo->mSource, &srcFileName))
		{
			fileName = (char*)srcFileName->c_str();
		}
	}

	if (moreInfo->mLocation != NULL)
	{
		fileName = (char*)moreInfo->mLocation->mFile.c_str();
		if (outLine != NULL)
			*outLine = moreInfo->mLocation->mLine;
		if (outColumn != NULL)
			*outColumn = moreInfo->mLocation->mColumn;
	}

	srcStart = moreInfo->mSrcStart;
	srcEnd = moreInfo->mSrcEnd;
	return moreInfo->mInfo.c_str();
}

BF_EXPORT bool BF_CALLTYPE BfPassInstance_HadSignatureChanges(BfPassInstance* bfPassInstance)
{
	return bfPassInstance->mHadSignatureChanges;
}

BF_EXPORT void BF_CALLTYPE BfPassInstance_Delete(BfPassInstance* bfPassInstance)
{
	delete bfPassInstance;
}

BF_EXPORT BfPassInstance* BF_CALLTYPE BfSystem_CreatePassInstance(BfSystem* bfSystem)
{
	return new BfPassInstance(bfSystem);
}

BF_EXPORT void BF_CALLTYPE BfSystem_RemoveDeletedParsers(BfSystem* bfSystem)
{
	bfSystem->RemoveDeletedParsers();
}

BF_EXPORT void BF_CALLTYPE BfSystem_RemoveOldParsers(BfSystem* bfSystem)
{
	bfSystem->RemoveOldParsers();
}

BF_EXPORT void BF_CALLTYPE BfSystem_RemoveOldData(BfSystem* bfSystem)
{
	bfSystem->RemoveOldData();
}

BF_EXPORT void BF_CALLTYPE BfSystem_NotifyWillRequestLock(BfSystem* bfSystem, int priority)
{
	bfSystem->NotifyWillRequestLock(priority);
}

BF_EXPORT void BF_CALLTYPE BfSystem_Lock(BfSystem* bfSystem, int priority)
{
	bfSystem->Lock(priority);
}

BF_EXPORT void BF_CALLTYPE BfSystem_Unlock(BfSystem* bfSystem)
{
	bfSystem->Unlock();
}

BF_EXPORT void BF_CALLTYPE BfSystem_Update(BfSystem* bfSystem)
{
	bfSystem->mUpdateCnt++;
	if (bfSystem->mUpdateCnt % 60 == 0)
	{
		if (bfSystem->mParserDeleteQueue.size() != 0)
		{
#ifdef _DEBUG
			//OutputDebugStrF("mParserDeleteQueue = %d\n", (int)bfSystem->mParserDeleteQueue.size());
#endif
		}
	}
}

BF_EXPORT void BF_CALLTYPE BfSystem_ReportMemory(BfSystem* bfSystem)
{
	AutoCrit crit(bfSystem->mDataLock);

	MemReporter memReporter;

	for (auto compiler : bfSystem->mCompilers)
	{
		AutoMemReporter autoMemReporter(&memReporter, "Compiler");
		compiler->ReportMemory(&memReporter);
	}

	for (auto typeDef : bfSystem->mTypeDefs)
	{
		AutoMemReporter autoMemReporter(&memReporter, "TypeDef");
		typeDef->ReportMemory(&memReporter);
	}

	for (auto parser : bfSystem->mParsers)
	{
		AutoMemReporter autoMemReporter(&memReporter, "Parsers");
		parser->ReportMemory(&memReporter);
	}

	memReporter.Report();
}

BF_EXPORT void BF_CALLTYPE BfSystem_StartTiming()
{
	gPerfManager->StartRecording();
}

BF_EXPORT void BF_CALLTYPE BfSystem_PerfZoneStart(const char* name)
{
	gPerfManager->ZoneStart(name);
}

BF_EXPORT void BF_CALLTYPE BfSystem_PerfZoneEnd()
{
	gPerfManager->ZoneEnd();
}

BF_EXPORT void BF_CALLTYPE BfSystem_StopTiming()
{
	gPerfManager->StopRecording();
}

BF_EXPORT void BF_CALLTYPE BfSystem_DbgPrintTimings()
{
	gPerfManager->DbgPrint();
}

BF_EXPORT const char* BF_CALLTYPE BfSystem_GetNamespaceSearch(BfSystem* bfSystem, const char* typeName, BfProject* project)
{
	auto typeDef = bfSystem->FindTypeDef(typeName, project);
	if (typeDef == NULL)
		return NULL;

	String& outString = *gTLStrReturn.Get();
	outString.clear();
	for (auto namespaceEntry : typeDef->mNamespaceSearch)
	{
		if (!outString.empty())
			outString += "\n";
		outString += namespaceEntry.ToString();
	}

	return outString.c_str();
}

BF_EXPORT BfProject* BF_CALLTYPE BfSystem_CreateProject(BfSystem* bfSystem, const char* projectName, const char* projectDir)
{
	AutoCrit autoCrit(bfSystem->mDataLock);
	BfProject* bfProject = new BfProject();
	bfProject->mName = projectName;
	bfProject->mDirectory = projectDir;
	bfProject->mSystem = bfSystem;
	bfProject->mIdx = (int)bfSystem->mProjects.size();
	bfSystem->mProjects.push_back(bfProject);

	String safeProjectName = projectName;
	for (auto& c : safeProjectName)
	{
		if (((c >= 'A') && (c <= 'Z')) ||
			((c >= 'a') && (c <= 'z')) ||
			((c >= '0') && (c <= '9')))
		{
			// Leave
		}
		else
			c = '_';
	}

	String tryName = safeProjectName;
	for (int i = 2; true; i++)
	{
		if (bfSystem->mUsedSafeProjectNames.Add(ToUpper(tryName)))
			break;
		tryName = safeProjectName + StrFormat("_%d", i);
	}
	bfProject->mSafeName = tryName;

	BfLogSys(bfSystem, "Creating project %p\n", bfProject);
	return bfProject;
}

BF_EXPORT void BF_CALLTYPE BfSystem_ClearTypeOptions(BfSystem* bfSystem)
{
	AutoCrit autoCrit(bfSystem->mDataLock);
	bfSystem->mTypeOptions.Clear();
}

BF_EXPORT void BF_CALLTYPE BfSystem_AddTypeOptions(BfSystem* bfSystem, char* filter, int32 simdSetting, int32 optimizationLevel, int32 emitDebugInfo, int32 andFlags, int32 orFlags, int32 allocStackTraceDepth, char* reflectMethodFilter)
{
	AutoCrit autoCrit(bfSystem->mDataLock);
	BfTypeOptions typeOptions;

	auto _ParseFilters = [&](char* filter, Array<String>& filters, Array<String>& attributeFilters)
	{
		String filterStr = filter;
		int idx = 0;
		while (true)
		{
			int semiIdx = (int)filterStr.IndexOf(';', idx);
			String newFilter;
			if (semiIdx == -1)
				newFilter = filterStr.Substring(idx);
			else
				newFilter = filterStr.Substring(idx, semiIdx - idx);
			newFilter.Trim();
			if (!newFilter.IsEmpty())
			{
				if (newFilter.StartsWith('['))
				{
					newFilter.Remove(0);
					if (newFilter.EndsWith(']'))
						newFilter.Remove(newFilter.length() - 1);
					newFilter.Trim();
					attributeFilters.Add(newFilter);
				}
				else
					filters.Add(newFilter);
			}

			if (semiIdx == -1)
				break;
			idx = semiIdx + 1;
		}
	};

	_ParseFilters(filter, typeOptions.mTypeFilters, typeOptions.mAttributeFilters);

	if ((typeOptions.mTypeFilters.IsEmpty()) && (typeOptions.mAttributeFilters.IsEmpty()))
		return;
	typeOptions.mSIMDSetting = simdSetting;
	typeOptions.mOptimizationLevel = optimizationLevel;
	typeOptions.mEmitDebugInfo = emitDebugInfo;
	typeOptions.mAndFlags = (BfOptionFlags)andFlags;
	typeOptions.mOrFlags = (BfOptionFlags)orFlags;
	typeOptions.mAllocStackTraceDepth = allocStackTraceDepth;

	Array<String> methodReflectFilters;
	Array<String> methodReflectAttributeFilters;

	_ParseFilters(reflectMethodFilter, methodReflectFilters, methodReflectAttributeFilters);
	// When we have method filters we want to apply those to the methods for when we merge options
	if ((!methodReflectFilters.IsEmpty()) || (!methodReflectAttributeFilters.IsEmpty()))
	{
		for (auto& filter : methodReflectFilters)
			typeOptions.mReflectMethodFilters.Add({ filter, (BfOptionFlags)andFlags, (BfOptionFlags)orFlags });
		for (auto& filter : methodReflectAttributeFilters)
			typeOptions.mReflectMethodAttributeFilters.Add({ filter, (BfOptionFlags)andFlags, (BfOptionFlags)orFlags });

		typeOptions.mAndFlags = (BfOptionFlags)(typeOptions.mAndFlags | BfOptionFlags_Reflect_MethodMask);
		typeOptions.mOrFlags = (BfOptionFlags)(typeOptions.mOrFlags & ~BfOptionFlags_Reflect_MethodMask);
	}

	bfSystem->mTypeOptions.push_back(typeOptions);
}

BF_EXPORT void BF_CALLTYPE BfProject_Delete(BfProject* bfProject)
{
	auto bfSystem = bfProject->mSystem;
	AutoCrit autoCrit(bfSystem->mSystemLock);
	bfProject->mDeleteStage = BfProject::DeleteStage_Queued;

	bfSystem->mProjectDeleteQueue.push_back(bfProject);
	bfSystem->mUsedSafeProjectNames.Remove(bfProject->mSafeName);

	BF_ASSERT(bfSystem->mProjects[bfProject->mIdx] == bfProject);
	bool wasRemoved = bfSystem->mProjects.Remove(bfProject);
	BF_ASSERT(wasRemoved);

	for (int i = bfProject->mIdx; i < (int)bfSystem->mProjects.size(); i++)
		bfSystem->mProjects[i]->mIdx = i;

	bfProject->mIdx = -1;

/*#ifdef _DEBUG
	{
		AutoCrit autoCrit(bfSystem->mSystemLock);
		for (auto typeDefKV : bfSystem->mTypeDefs)
		{
			auto typeDef = typeDefKV.second;
			BF_ASSERT(typeDef->mProject != bfProject);
		}
	}
#endif

	delete bfProject;*/
}

BF_EXPORT void BF_CALLTYPE BfProject_ClearDependencies(BfProject* bfProject)
{
	bfProject->mDependencies.Clear();
}

BF_EXPORT void BF_CALLTYPE BfProject_AddDependency(BfProject* bfProject, BfProject* depProject)
{
	bfProject->mDependencies.push_back(depProject);
}

BF_EXPORT void BF_CALLTYPE BfProject_SetDisabled(BfProject* bfProject, bool disabled)
{
	bfProject->mDisabled = disabled;
}

BF_EXPORT void BF_CALLTYPE BfProject_SetOptions(BfProject* bfProject, int targetType, const char* startupObject, const char* preprocessorMacros,
	int optLevel, int ltoType, int relocType, int picLevel, BfProjectFlags flags)
{
	bfProject->mTargetType = (BfTargetType)targetType;
	bfProject->mStartupObject = startupObject;

	BfCodeGenOptions codeGenOptions;
	codeGenOptions.mOptLevel = (BfOptLevel)optLevel;
	codeGenOptions.mLTOType = (BfLTOType)ltoType;
	codeGenOptions.mRelocType = (BfRelocType)relocType;
	codeGenOptions.mPICLevel = (BfPICLevel)picLevel;
	codeGenOptions.mMergeFunctions = (flags & BfProjectFlags_MergeFunctions) != 0;
	codeGenOptions.mLoadCombine = (flags & BfProjectFlags_CombineLoads) != 0;
	codeGenOptions.mLoopVectorize = (flags & BfProjectFlags_VectorizeLoops) != 0;
	codeGenOptions.mSLPVectorize = (flags & BfProjectFlags_VectorizeSLP) != 0;
	if ((flags & BfProjectFlags_AsmOutput) != 0)
	{
		static bool setLLVMAsmKind = false;
		if ((flags & BfProjectFlags_AsmOutput_ATT) != 0)
			codeGenOptions.mAsmKind = BfAsmKind_ATT;
		else
			codeGenOptions.mAsmKind = BfAsmKind_Intel;

		if (!setLLVMAsmKind)
		{
			setLLVMAsmKind = true;
			BfIRCodeGen::SetAsmKind(codeGenOptions.mAsmKind);
		}
	}
	bfProject->mCodeGenOptions = codeGenOptions;
	bfProject->mSingleModule = (flags & BfProjectFlags_SingleModule) != 0;
	bfProject->mAlwaysIncludeAll = (flags & BfProjectFlags_AlwaysIncludeAll) != 0;

	bfProject->mPreprocessorMacros.Clear();

	int startIdx = 0;
	int idx = 0;
	while (true)
	{
		char c = preprocessorMacros[idx];
		if ((c == '\n') || (c == 0))
		{
			String macroStr = String(preprocessorMacros + startIdx, preprocessorMacros + idx);
			if (macroStr.length() > 0)
				bfProject->mPreprocessorMacros.Add(macroStr);
			startIdx = idx + 1;
		}

		if (c == 0)
			break;

		idx++;
	}
}

//////////////////////////////////////////////////////////////////////////

class FixTypesHelper : BfElementVisitor
{
public:
	BfSystem* mBfSystem;
	bool mInMethod;
	bool mInTypeRef;

	struct ReplaceRecord
	{
		BfAstNode* mNode;
		String mNewStr;
	};

	std::map<int, ReplaceRecord> mReplaceMap;

public:

	String GetTypeName(const StringImpl& typeName)
	{
		if (typeName == "long")
			return "int64";
		else if (typeName == "ulong")
			return "uint64";
		else if (typeName == "intptr")
			return "int";
		else if (typeName == "uintptr")
			return "uint";
		else if (typeName == "short")
			return "int16";
		else if (typeName == "ushort")
			return "uint16";
		else if (typeName == "byte")
			return "uint8";
		else if (typeName == "sbyte")
			return "int8";
		else if (typeName == "SByte")
			return "Int8";
		else if (typeName == "Byte")
			return "UInt8";
		else if (typeName == "Single")
			return "Float";
		else if (typeName == "IntPtr")
			return "Int";
		else if (typeName == "UIntPtr")
			return "UInt";
		//else if ((!mInMethod) || (mInTypeRef))
		{
			if (typeName == "int")
				return "int32";
			else if (typeName == "uint")
				return "uint32";
		}
		return typeName;
	}

	void Visit(BfTypeReference* typeRef)
	{
		String typeName = typeRef->ToString();
		String wantTypeName = GetTypeName(typeName);
		if (typeName != wantTypeName)
		{
			ReplaceRecord replaceRecord = { typeRef, wantTypeName };
			mReplaceMap[typeRef->GetSrcStart()] = replaceRecord;
		}

		SetAndRestoreValue<bool> prevInTypeRef(mInTypeRef, true);
		BfElementVisitor::Visit(typeRef);
	}

	void Visit(BfIdentifierNode* identifier)
	{
		String typeName = identifier->ToString();
		String wantTypeName = GetTypeName(typeName);
		if (typeName != wantTypeName)
		{
			ReplaceRecord replaceRecord = { identifier, wantTypeName };
			mReplaceMap[identifier->GetSrcStart()] = replaceRecord;
		}
		BfElementVisitor::Visit(identifier);
	}

	void Visit(BfTypeDeclaration* typeDecl)
	{
		BfElementVisitor::Visit(typeDecl);
	}

	void Visit(BfTupleTypeRef* typeRef)
	{
		SetAndRestoreValue<bool> prevInTypeRef(mInTypeRef, true);
		BfElementVisitor::Visit(typeRef);
	}

	void Visit(BfGenericInstanceTypeRef* typeRef)
	{
		SetAndRestoreValue<bool> prevInTypeRef(mInTypeRef, true);
		BfElementVisitor::Visit(typeRef);
	}

	void Visit(BfMethodDeclaration* methodDecl)
	{
		SetAndRestoreValue<bool> prevInMethod(mInMethod, true);
		BfElementVisitor::Visit(methodDecl);
	}

	void FixStr(String& source)
	{
		for (int i = 0; i < (int)source.length(); i++)
		{
			if (source[i] == '\r')
				source.Remove(i--, 1);
		}
	}

	void Fix()
	{
		mInMethod = false;
		mInTypeRef = false;

		for (auto typeDef : mBfSystem->mTypeDefs)
		{
			if (typeDef->mTypeDeclaration == NULL)
				continue;

			auto parser = typeDef->mTypeDeclaration->GetSourceData()->ToParserData();
			String fileName = parser->mFileName;

			String origFileName = parser->mFileName;
			origFileName.Insert(3, "Orig_");

			String source;
			source.Insert(0, parser->mSrc, parser->mSrcLength);

			String origSource = source;
			FixStr(origSource);
			RecursiveCreateDirectory(GetFileDir(origFileName));
			FILE* fp = fopen(origFileName.c_str(), "w");
			fwrite(origSource.c_str(), 1, (int)origSource.length(), fp);
			fclose(fp);

			VisitMembers(parser->mRootNode);

			int ofs = 0;
			for (auto& pair : mReplaceMap)
			{
				int origLen = pair.second.mNode->GetSrcLength();
				source.Remove(pair.first + ofs, origLen);
				source.Insert(pair.first + ofs, pair.second.mNewStr);
				ofs += (int)pair.second.mNewStr.length() - origLen;
			}

			mReplaceMap.clear();

			FixStr(source);
			fp = fopen(fileName.c_str(), "w");
			fwrite(source.c_str(), 1, (int)source.length(), fp);
			fclose(fp);
		}
	}
};

BF_EXPORT void BF_CALLTYPE BfSystem_FixTypes(BfSystem* bfSystem)
{
	FixTypesHelper fixTypesHelper;
	fixTypesHelper.mBfSystem = bfSystem;
	fixTypesHelper.Fix();
}

BF_EXPORT void BF_CALLTYPE BfSystem_Log(BfSystem* bfSystem, char* str)
{
	BfLogSys(bfSystem, str);
	BfLogSys(bfSystem, "\n");
}