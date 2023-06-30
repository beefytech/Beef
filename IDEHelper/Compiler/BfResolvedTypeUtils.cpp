#include "BeefySysLib/util/AllocDebug.h"

#include "BfCompiler.h"
#include "BfParser.h"
#include "BfDefBuilder.h"
#include "BfMangler.h"
#include "BfConstResolver.h"
#include "BfModule.h"
#include "CeMachine.h"
#include "BeefySysLib/util/BeefPerf.h"

#pragma warning(disable:4996)
#pragma warning(disable:4267)

USING_NS_BF;

//void Beefy::DbgCheckType(llvm::Type* checkType)
//{
//#ifdef _DEBUG
//	/*while (auto ptrType = llvm::dyn_cast<llvm::PointerType>(checkType))
//	{
//		checkType = ptrType->getElementType();
//	}
//
//	auto structType = llvm::dyn_cast<llvm::StructType>(checkType);
//	if (structType != NULL)
//	{
//		auto stringRef = structType->getName();
//		BF_ASSERT(strncmp(stringRef.data(), "DEAD", 4) != 0);
//	}*/
//#endif
//}

void BfTypedValue::DbgCheckType() const
{
	/*if (mType != NULL)
	{
		auto structType = llvm::dyn_cast<llvm::StructType>(mType->mIRType);
		if (structType != NULL)
		{
			auto stringRef = structType->getName();
			BF_ASSERT(strncmp(stringRef.data(), "DEAD", 4) != 0);
		}
	}*/

#ifdef _DEBUG
	/*if (mValue != NULL)
	{
		auto checkType = mValue->getType();
		Beefy::DbgCheckType(checkType);
	}*/
#endif
}

bool BfTypedValue::IsValuelessType() const
{
	return mType->IsValuelessType();
}

bool BfTypedValue::CanModify() const
{
	return (((IsAddr()) || (mType->IsValuelessType())) && (!IsReadOnly()));
}

//////////////////////////////////////////////////////////////////////////

BfProtection BfUsingFieldData::MemberRef::GetProtection() const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->mProtection;
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->mProtection;
	case Kind_Method:
		return mTypeInstance->mTypeDef->mMethods[mIdx]->mProtection;
	}
	return BfProtection_Public;
}

BfProtection BfUsingFieldData::MemberRef::GetUsingProtection() const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->mUsingProtection;
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->mUsingProtection;
	case Kind_Method:
		return mTypeInstance->mTypeDef->mMethods[mIdx]->mProtection;
	}
	return BfProtection_Public;
}

BfTypeDef* BfUsingFieldData::MemberRef::GetDeclaringType(BfModule* curModule) const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->mDeclaringType;
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->mDeclaringType;
	case Kind_Method:
		return mTypeInstance->mTypeDef->mMethods[mIdx]->mDeclaringType;
	case Kind_Local:
		return curModule->GetActiveTypeDef();
	}
	return NULL;
}

String BfUsingFieldData::MemberRef::GetFullName(BfModule* curModule) const
{
	if (mKind == Kind_Local)
		return curModule->mCurMethodState->mLocals[mIdx]->mName;

	String result = curModule->TypeToString(mTypeInstance);
	if (!result.IsEmpty())
		result += ".";

	switch (mKind)
	{
	case Kind_Field:
		result += mTypeInstance->mTypeDef->mFields[mIdx]->mName;
		break;
	case Kind_Property:
		result += mTypeInstance->mTypeDef->mProperties[mIdx]->mName;
		break;
	case Kind_Method:
		result += mTypeInstance->mTypeDef->mMethods[mIdx]->mName;
		break;
	}
	return result;
}

String BfUsingFieldData::MemberRef::GetName(BfModule* curModule) const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->mName;
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->mName;
	case Kind_Method:
		{
			auto methodInstance = curModule->GetRawMethodInstance(mTypeInstance, mTypeInstance->mTypeDef->mMethods[mIdx]);
			return curModule->MethodToString(methodInstance, BfMethodNameFlag_OmitTypeName);
		}
	case Kind_Local:
		return curModule->mCurMethodState->mLocals[mIdx]->mName;
	}
	return "";
}

BfAstNode* BfUsingFieldData::MemberRef::GetRefNode(BfModule* curModule) const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->GetRefNode();
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->GetRefNode();
	case Kind_Method:
		return mTypeInstance->mTypeDef->mMethods[mIdx]->GetRefNode();
	case Kind_Local:
		return curModule->mCurMethodState->mLocals[mIdx]->mNameNode;
	}
	return NULL;
}

bool BfUsingFieldData::MemberRef::IsStatic() const
{
	switch (mKind)
	{
	case Kind_Field:
		return mTypeInstance->mTypeDef->mFields[mIdx]->mIsStatic;
	case Kind_Property:
		return mTypeInstance->mTypeDef->mProperties[mIdx]->mIsStatic;
	case Kind_Method:
		return mTypeInstance->mTypeDef->mMethods[mIdx]->mIsStatic;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

bool BfGenericParamInstance::IsEnum()
{
	if ((mGenericParamFlags & BfGenericParamFlag_Enum) != 0)
		return true;
	if (mTypeConstraint != NULL)
	{
		auto module = mTypeConstraint->GetModule();
		if ((module != NULL) && (mTypeConstraint->IsInstanceOf(module->mCompiler->mEnumTypeDef)))
			return true;
	}
	return true;
	return false;
}

//////////////////////////////////////////////////////////////////////////

bool BfDependencyMap::AddUsedBy(BfType* dependentType, BfDependencyMap::DependencyFlags flags)
{
	BF_ASSERT(dependentType != NULL);
	BF_ASSERT(dependentType->mRevision != -1);

	//auto itr = mTypeSet.insert(BfDependencyMap::TypeMap::value_type(dependentType, DependencyEntry(dependentType->mRevision, flags)));
	//if (!itr.second)

	DependencyEntry* dependencyEntry = NULL;
	if (mTypeSet.TryAddRaw(dependentType, NULL, &dependencyEntry))
	{
		if ((flags & ~DependencyFlag_UnspecializedType) != 0)
		{
			if (auto dependentDepType = dependentType->ToDependedType())
			{
				int tryDepth = dependentDepType->mDependencyMap.mMinDependDepth + 1;
				if (tryDepth < mMinDependDepth)
					mMinDependDepth = tryDepth;
			}
		}

		dependencyEntry->mRevision = dependentType->mRevision;
		dependencyEntry->mFlags = flags;
		return true;
	}
	else
	{
		if (dependencyEntry->mRevision != dependentType->mRevision)
		{
			dependencyEntry->mRevision = dependentType->mRevision;
			dependencyEntry->mFlags = flags;
			return true;
		}
		else
		{
			if ((dependencyEntry->mFlags & flags) == flags)
				return false;
			dependencyEntry->mFlags = (DependencyFlags)(dependencyEntry->mFlags | flags);
			return true;
		}
	}
}

bool BfDependencyMap::IsEmpty()
{
	return mTypeSet.size() == 0;
}

BfDependencyMap::TypeMap::iterator BfDependencyMap::begin()
{
	return mTypeSet.begin();
}

BfDependencyMap::TypeMap::iterator BfDependencyMap::end()
{
	return mTypeSet.end();
}

BfDependencyMap::TypeMap::iterator BfDependencyMap::erase(BfDependencyMap::TypeMap::iterator& itr)
{
	return mTypeSet.Remove(itr);
}

//////////////////////////////////////////////////////////////////////////

BfFieldDef* BfFieldInstance::GetFieldDef()
{
	if (mFieldIdx == -1)
		return NULL;
	return mOwner->mTypeDef->mFields[mFieldIdx];
}

//////////////////////////////////////////////////////////////////////////

BfType::BfType()
{
	mTypeId = -1;
	mContext = NULL;
	mRevision = -1;

	//mLastUsedRevision = -1;

	mDefineState = BfTypeDefineState_Undefined;
	//mDICallbackVH = NULL;
	//mInnerDICallbackVH = NULL;
	mRebuildFlags = BfTypeRebuildFlag_None;
	mAlign = -1;
	mSize = -1;
	//mDICallbackVH = NULL;
	//mInnerDICallbackVH = NULL;
	mDirty = true;
}

BfModule* BfType::GetModule()
{
	if (mContext->mCompiler->mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
		return mContext->mScratchModule;
	else
		return mContext->mUnreifiedModule;
}

BfTypeInstance* BfType::FindUnderlyingTypeInstance()
{
	auto typeInstance = ToTypeInstance();
	if (typeInstance != NULL)
		return typeInstance;

	auto underlyingType = GetUnderlyingType();
	while (underlyingType != NULL)
	{
		auto underlyingTypeInst = underlyingType->ToTypeInstance();
		if (underlyingTypeInst != NULL)
			return underlyingTypeInst;
		underlyingType = underlyingType->GetUnderlyingType();
	}

	return NULL;
}

void BfType::ReportMemory(MemReporter* memReporter)
{
	memReporter->Add(sizeof(BfType));
}

//////////////////////////////////////////////////////////////////////////

BfNonGenericMethodRef::BfNonGenericMethodRef(BfMethodInstance* methodInstance)
{
	*this = methodInstance;
}

BfNonGenericMethodRef::operator BfMethodInstance* () const
{
	if (mTypeInstance == NULL)
		return NULL;
	if (mMethodNum < 0)
		return NULL;
	auto& methodSpecializationGroup = mTypeInstance->mMethodInstanceGroups[mMethodNum];
	BF_ASSERT((methodSpecializationGroup.mDefault != NULL) || (mTypeInstance->mTypeFailed));
	return methodSpecializationGroup.mDefault;
}

BfMethodInstance* BfNonGenericMethodRef::operator->() const
{
	return *this;
}

BfNonGenericMethodRef& BfNonGenericMethodRef::operator=(BfMethodInstance* methodInstance)
{
	if (methodInstance == NULL)
	{
		mTypeInstance = NULL;
		mMethodNum = 0;
	}
	else
	{
		mTypeInstance = methodInstance->mMethodInstanceGroup->mOwner;
		mMethodNum = methodInstance->mMethodInstanceGroup->mMethodIdx;
		BF_ASSERT((methodInstance->GetNumGenericArguments() == 0) ||
			((methodInstance->mIsUnspecialized) && (!methodInstance->mIsUnspecializedVariation)));
		mSignatureHash = (int)mTypeInstance->mTypeDef->mSignatureHash;
	}
	return *this;
}

bool BfNonGenericMethodRef::operator==(const BfNonGenericMethodRef& methodRef) const
{
	bool eq = ((methodRef.mKind == mKind) &&
		(methodRef.mTypeInstance == mTypeInstance) &&
		(methodRef.mMethodNum == mMethodNum));
	if (eq)
	{
		BF_ASSERT((methodRef.mSignatureHash == mSignatureHash) || (methodRef.mSignatureHash == 0) || (mSignatureHash == 0));
	}

	return eq;
}

bool BfNonGenericMethodRef::operator==(BfMethodInstance* methodInstance) const
{
	if (mTypeInstance != methodInstance->GetOwner())
		return false;
	return methodInstance == (BfMethodInstance*)*this;
}

size_t BfNonGenericMethodRef::Hash::operator()(const BfNonGenericMethodRef& val) const
{
	return (val.mTypeInstance->mTypeId << 10) ^ val.mMethodNum;
}

//////////////////////////////////////////////////////////////////////////

BfMethodRef::BfMethodRef(BfMethodInstance* methodInstance)
{
	*this = methodInstance;
}

BfMethodRef::operator BfMethodInstance* () const
{
	if (mTypeInstance == NULL)
		return NULL;
	if (mMethodNum < 0)
		return NULL;
	auto& methodSpecializationGroup = mTypeInstance->mMethodInstanceGroups[mMethodNum];
	if (mMethodGenericArguments.size() != 0)
	{
		bool isSpecialied = false;
		int paramIdx = 0;
		for (auto genericArg : mMethodGenericArguments)
		{
			if (!genericArg->IsGenericParam())
			{
				isSpecialied = true;
				break;
			}

			auto genericParam = (BfGenericParamType*)genericArg;
			if ((genericParam->mGenericParamKind != BfGenericParamKind_Method) || (genericParam->mGenericParamIdx != paramIdx))
			{
				isSpecialied = true;
				break;
			}

			paramIdx++;
		}

		if (isSpecialied)
		{
			BfMethodInstance** methodInstancePtr = NULL;
			if (methodSpecializationGroup.mMethodSpecializationMap->TryGetValue(mMethodGenericArguments, &methodInstancePtr))
				return *methodInstancePtr;
			return NULL;
		}
	}
	BF_ASSERT(methodSpecializationGroup.mDefault != NULL);
	return methodSpecializationGroup.mDefault;
}

BfMethodInstance* BfMethodRef::operator->() const
{
	return *this;
}

BfMethodRef& BfMethodRef::operator=(BfMethodInstance* methodInstance)
{
	if (methodInstance == NULL)
	{
		mTypeInstance = NULL;
		mMethodNum = 0;
		mMethodRefFlags = BfMethodRefFlag_None;
	}
	else
	{
		mTypeInstance = methodInstance->mMethodInstanceGroup->mOwner;
		mMethodNum = methodInstance->mMethodInstanceGroup->mMethodIdx;
		if (methodInstance->mMethodInfoEx != NULL)
		{
			mMethodGenericArguments.Clear();
			for (auto type : methodInstance->mMethodInfoEx->mMethodGenericArguments)
				mMethodGenericArguments.Add(type);
		}
		mSignatureHash = (int)mTypeInstance->mTypeDef->mSignatureHash;
		if (methodInstance->mAlwaysInline)
			mMethodRefFlags = BfMethodRefFlag_AlwaysInclude;
		else
			mMethodRefFlags = BfMethodRefFlag_None;
	}
	return *this;
}

bool BfMethodRef::operator==(const BfMethodRef& methodRef) const
{
	bool eq = ((methodRef.mKind == mKind) &&
		(methodRef.mTypeInstance == mTypeInstance) &&
		(methodRef.mMethodNum == mMethodNum) &&
		(methodRef.mMethodGenericArguments == mMethodGenericArguments) &&
		(methodRef.mMethodRefFlags == mMethodRefFlags));
	if (eq)
	{
		BF_ASSERT((methodRef.mSignatureHash == mSignatureHash) || (methodRef.mSignatureHash == 0) || (mSignatureHash == 0));
	}

	return eq;
}

bool BfMethodRef::operator==(BfMethodInstance* methodInstance) const
{
	if (mTypeInstance != methodInstance->GetOwner())
		return false;
	return methodInstance == (BfMethodInstance*)*this;
}

size_t BfMethodRef::Hash::operator()(const BfMethodRef& val) const
{
	return (val.mTypeInstance->mTypeId << 10) ^ (val.mMethodNum << 1) ^ (int)(val.mMethodRefFlags);
}

//////////////////////////////////////////////////////////////////////////

BfFieldRef::BfFieldRef(BfTypeInstance* typeInst, BfFieldDef* fieldDef)
{
	mTypeInstance = typeInst;
	mFieldIdx = fieldDef->mIdx;
}

BfFieldRef::BfFieldRef(BfFieldInstance* fieldInstance)
{
	mTypeInstance = fieldInstance->mOwner;
	mFieldIdx = fieldInstance->mFieldIdx;
}

BfFieldRef::operator BfFieldInstance*() const
{
	BF_ASSERT(!mTypeInstance->IsDataIncomplete());
	return &mTypeInstance->mFieldInstances[mFieldIdx];
}

BfFieldRef::operator BfFieldDef*() const
{
	return mTypeInstance->mTypeDef->mFields[mFieldIdx];
}

//////////////////////////////////////////////////////////////////////////

BfPropertyRef::BfPropertyRef(BfTypeInstance* typeInst, BfPropertyDef* propDef)
{
	mTypeInstance = typeInst;
	mPropIdx = propDef->mIdx;
}

BfPropertyRef::operator BfPropertyDef*() const
{
	return mTypeInstance->mTypeDef->mProperties[mPropIdx];
}

//////////////////////////////////////////////////////////////////////////

/*BfMethodInstance* BfTypeInstance::GetVTableMethodInstance(int vtableIdx)
{
	auto& methodSpecializationGroup = mVirtualMethodTable[vtableIdx].mTypeInstance->mMethodInstanceGroups[mVirtualMethodTable[vtableIdx].mMethodNum];
	return &methodSpecializationGroup.mMethodSpecializationMap.begin()->second;
}*/

static int gDelIdx = 0;

BfType::~BfType()
{
	if (mContext != NULL)
		BfLogSys(mContext->mSystem, "~BfType %p\n", this);

	/*gDelIdx++;
	auto typeInst = ToTypeInstance();
	OutputDebugStrF("%d Deleting %08X type %s\n", gDelIdx, this, (typeInst != NULL) ? typeInst->mTypeDef->mName.c_str() : "");*/

	//delete mDICallbackVH;
	//delete mInnerDICallbackVH;
}

BfFieldInstance::~BfFieldInstance()
{
	delete mCustomAttributes;
}

BfType* BfFieldInstance::GetResolvedType()
{
	return mResolvedType;
}

void BfFieldInstance::SetResolvedType(BfType* type)
{
	mResolvedType = type;
}

void BfFieldInstance::GetDataRange(int& dataIdx, int& dataCount)
{
	int minMergedDataIdx = mMergedDataIdx;
	int maxMergedDataIdx = minMergedDataIdx + 1;
	if (mResolvedType->IsStruct())
		maxMergedDataIdx = minMergedDataIdx + mResolvedType->ToTypeInstance()->mMergedFieldDataCount;

	if (mOwner->mIsUnion)
	{
		for (auto& checkFieldInstance : mOwner->mFieldInstances)
		{
			if (&checkFieldInstance == this)
				continue;

			if (checkFieldInstance.mDataIdx == mDataIdx)
			{
				int checkMinMergedDataIdx = checkFieldInstance.mMergedDataIdx;
				int checkMaxMergedDataIdx = checkMinMergedDataIdx + 1;
				if (checkFieldInstance.GetResolvedType()->IsStruct())
					checkMaxMergedDataIdx = checkMinMergedDataIdx + checkFieldInstance.mResolvedType->ToTypeInstance()->mMergedFieldDataCount;

				minMergedDataIdx = BF_MIN(minMergedDataIdx, checkMinMergedDataIdx);
				maxMergedDataIdx = BF_MAX(maxMergedDataIdx, checkMaxMergedDataIdx);
			}
		}
	}

	int fieldIdx = dataIdx - 1;
	if (fieldIdx == -1)
	{
		dataIdx = minMergedDataIdx + 1;
	}
	else
	{
		fieldIdx += minMergedDataIdx;
		dataIdx = fieldIdx + 1;
	}
	dataCount = maxMergedDataIdx - minMergedDataIdx;
}

int BfFieldInstance::GetAlign(int packing)
{
	int align = mResolvedType->mAlign;
	if (IsAppendedObject())
		align = mResolvedType->ToTypeInstance()->mInstAlign;
	if (packing > 0)
		align = BF_MIN(align, packing);
	if (mCustomAttributes != NULL)
	{
		auto module = mOwner->mModule;
		for (auto& attrib : mCustomAttributes->mAttributes)
		{
			if (attrib.mType->IsInstanceOf(module->mCompiler->mAlignAttributeTypeDef))
			{
				align = 16; // System conservative default

				if (!attrib.mCtorArgs.IsEmpty())
				{
					BfIRConstHolder* constHolder = module->mCurTypeInstance->mConstHolder;
					auto constant = constHolder->GetConstant(attrib.mCtorArgs[0]);
					if (constant != NULL)
					{
						int alignOverride = (int)BF_MAX(1, constant->mInt64);
						if ((alignOverride & (alignOverride - 1)) == 0)
							align = alignOverride;
						else
							module->Fail("Alignment must be a power of 2", attrib.GetRefNode());
					}
				}
			}
		}
	}
	return align;
}

bool BfFieldInstance::IsAppendedObject()
{
	auto fieldDef = GetFieldDef();
	return (fieldDef != NULL) && (fieldDef->mIsAppend) && (mResolvedType->IsObject()) && (mOwner->IsObject());
}

//////////////////////////////////////////////////////////////////////////

int64 BfDeferredMethodCallData::GenerateMethodId(BfModule* module, int64 methodId)
{
	// The mMethodId MUST be unique within a given deferred method processor. We are even more conservative, making it
	// unique per module
	if (module->mDeferredMethodIds.Add(methodId))
	{
		return methodId;
	}
	else
	{
		// Ideally the passed in methodId just works, otherwise --
		// We hope to create a hash that will hopefully be globally unique. If it isn't then it just means we will end up with two
		//  conflicting debug info definitions for the same name, which is not an error but may cause a debugger to show the
		//  wrong one to the user. Does not affect runtime correctness.
		int64 checkId = Hash64(module->mModuleName.c_str(), (int)module->mModuleName.length(), module->mDeferredMethodIds.size());
		while (true)
		{
			if (!module->mDeferredMethodIds.Contains(checkId))
				break;
			checkId += 0x100;
		}
		module->mDeferredMethodIds.Add(checkId);
		return checkId;
	}
}

//////////////////////////////////////////////////////////////////////////

BfMethodCustomAttributes::~BfMethodCustomAttributes()
{
	delete mCustomAttributes;
	delete mReturnCustomAttributes;
	for (auto paramCustomAttributes : mParamCustomAttributes)
		delete paramCustomAttributes;
}

BfMethodInstance* BfMethodParam::GetDelegateParamInvoke()
{
	if (mResolvedType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)mResolvedType;
		return methodRefType->mMethodRef;
	}

	BF_ASSERT(mResolvedType->IsDelegate() || mResolvedType->IsFunction());
	auto bfModule = BfModule::GetModuleFor(mResolvedType);
	BfMethodInstance* invokeMethodInstance = bfModule->GetRawMethodInstanceAtIdx(mResolvedType->ToTypeInstance(), 0, "Invoke");
	return invokeMethodInstance;
}

BfMethodInfoEx::~BfMethodInfoEx()
{
	for (auto genericParam : mGenericParams)
		genericParam->Release();
	delete mMethodCustomAttributes;
	delete mClosureInstanceInfo;
}

BfMethodInstance::~BfMethodInstance()
{
	Dispose(true);

	if (mHasMethodRefType)
	{
		auto module = GetOwner()->mModule;
		if (!module->mContext->mDeleting)
		{
			auto methodRefType = module->CreateMethodRefType(this);
			module->mContext->DeleteType(methodRefType);
		}
	}

	delete mMethodInfoEx;
}

void BfMethodInstance::Dispose(bool isDeleting)
{
	if (mIsDisposed)
		return;
	mIsDisposed = true;

	if (mMethodInstanceGroup != NULL)
	{
		BfLogSys(GetOwner()->mModule->mSystem, "BfMethodInstance::~BfMethodInstance %p Local:%d InCEMachine:%d Deleting:%d\n", this, mMethodDef->mIsLocalMethod, mInCEMachine, isDeleting);
	}
	else
	{
		BF_ASSERT(!mMethodDef->mIsLocalMethod);
	}

	if (mInCEMachine)
	{
		auto module = GetOwner()->mModule;
		if (module->mCompiler->mCeMachine != NULL)
			module->mCompiler->mCeMachine->RemoveMethod(this);
	}

	if (mMethodProcessRequest != NULL)
	{
		BF_ASSERT(mMethodProcessRequest->mMethodInstance == this);
		mMethodProcessRequest->mMethodInstance = NULL;
	}

	if (mHotMethod != NULL)
	{
		mHotMethod->mFlags = (BfHotDepDataFlags)(mHotMethod->mFlags & ~BfHotDepDataFlag_IsBound);
		mHotMethod->Deref();
	}
}

void BfMethodInstance::CopyFrom(BfMethodInstance* methodInstance)
{
	*this = *methodInstance;
	if (mMethodInfoEx != NULL)
	{
		mMethodInfoEx = new BfMethodInfoEx();
		*mMethodInfoEx = *(methodInstance->mMethodInfoEx);
		for (auto genericParam : mMethodInfoEx->mGenericParams)
			genericParam->AddRef();
		mMethodInfoEx->mMethodCustomAttributes = NULL;

		if (mMethodInfoEx->mClosureInstanceInfo != NULL)
		{
			mMethodInfoEx->mClosureInstanceInfo = new BfClosureInstanceInfo();
			*mMethodInfoEx->mClosureInstanceInfo = *methodInstance->mMethodInfoEx->mClosureInstanceInfo;
		}
	}
	mHasMethodRefType = false;
	mHasBeenProcessed = false;
	mIRFunction = BfIRValue();
	mMethodProcessRequest = NULL;
	mHotMethod = NULL;
}

BfImportKind BfMethodInstance::GetImportKind()
{
	if (mMethodDef->mImportKind != BfImportKind_Import_Unknown)
		return mMethodDef->mImportKind;

	auto module = GetOwner()->mModule;
	auto customAttributes = GetCustomAttributes();
	if (customAttributes == NULL)
		return BfImportKind_None;

	BfCustomAttribute* customAttribute = customAttributes->Get(module->mCompiler->mImportAttributeTypeDef);
	if (customAttribute == NULL)
		return BfImportKind_Import_Static;

	BfIRConstHolder* constHolder = GetOwner()->mConstHolder;
	String* filePath = module->GetStringPoolString(customAttribute->mCtorArgs[0], constHolder);
	if (filePath == NULL)
		return BfImportKind_Import_Static;
	return BfMethodDef::GetImportKindFromPath(*filePath);
}

BfMethodFlags BfMethodInstance::GetMethodFlags()
{
	BfMethodFlags methodFlags = (BfMethodFlags)0;

	if (mMethodDef->mProtection == BfProtection_Protected)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Protected);
	if (mMethodDef->mProtection == BfProtection_Public)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Public);
	if (mMethodDef->mIsStatic)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Static);
	if ((mMethodDef->mIsVirtual) || (mVirtualTableIdx != -1))
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Virtual);
	if (mMethodDef->mCallingConvention == BfCallingConvention_Fastcall)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_FastCall);
	if (mMethodDef->mIsMutating)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Mutating);
	if (mMethodDef->mMethodType == BfMethodType_Ctor)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_Constructor);
	if (mMethodDef->mIsReadOnly)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_ReadOnly);

	auto callingConvention = GetOwner()->mModule->GetIRCallingConvention(this);
	if (callingConvention == BfIRCallingConv_ThisCall)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_ThisCall);
	else if (callingConvention == BfIRCallingConv_StdCall)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_StdCall);
	else if (callingConvention == BfIRCallingConv_FastCall)
		methodFlags = (BfMethodFlags)(methodFlags | BfMethodFlags_FastCall);

	return methodFlags;
}

void BfMethodInstance::UndoDeclaration(bool keepIRFunction)
{
	if (mMethodInfoEx != NULL)
	{
		for (auto genericParam : mMethodInfoEx->mGenericParams)
		 	genericParam->Release();
		mMethodInfoEx->mGenericParams.Clear();
		delete mMethodInfoEx->mMethodCustomAttributes;
		mMethodInfoEx->mMethodCustomAttributes = NULL;
		mMethodInfoEx->mGenericTypeBindings.Clear();
	}

	mReturnType = NULL;
	if (!keepIRFunction)
		mIRFunction = BfIRValue();
	mParams.Clear();

	mDefaultValues.Clear();
	if (mMethodProcessRequest != NULL)
	{
		BF_ASSERT(mMethodProcessRequest->mMethodInstance == this);
		mMethodProcessRequest->mMethodInstance = NULL;
	}
	mHasStartedDeclaration = false;
	mHasBeenDeclared = false;
	mHasBeenProcessed = false;
	mIsUnspecialized = false;
	mIsUnspecializedVariation = false;
	mDisallowCalling = false;
	mIsIntrinsic = false;
	mHasFailed = false;
	mFailedConstraints = false;
}

BfTypeInstance* BfMethodInstance::GetOwner()
{
	return mMethodInstanceGroup->mOwner;
}

BfModule * BfMethodInstance::GetModule()
{
	return mMethodInstanceGroup->mOwner->mModule;
}

bool Beefy::BfMethodInstance::IsSpecializedGenericMethod()
{
	return (mMethodInfoEx != NULL) && (mMethodInfoEx->mGenericParams.size() != 0) && (!mIsUnspecialized);
}

bool Beefy::BfMethodInstance::IsSpecializedGenericMethodOrType()
{
	if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mGenericParams.size() != 0) && (!mIsUnspecialized))
		return true;
	auto owner = GetOwner();
	if (!owner->IsGenericTypeInstance())
		return false;
	BfTypeInstance* genericTypeInstance = (BfTypeInstance*)owner;
	return !genericTypeInstance->mGenericTypeInfo->mIsUnspecialized;
}

bool BfMethodInstance::IsSpecializedByAutoCompleteMethod()
{
	if (mMethodInstanceGroup->mOwner->IsSpecializedByAutoCompleteMethod())
		return true;
	if (mMethodInfoEx != NULL)
	{
		for (auto methodArg : mMethodInfoEx->mMethodGenericArguments)
		{
			// If we are specialized by an autocompleted method reference
			if (methodArg->IsMethodRef())
			{
				auto methodRefType = (BfMethodRefType*)methodArg;
				if (methodRefType->mIsAutoCompleteMethod)
					return true;
			}
		}
	}

	return false;
}

bool BfMethodInstance::IsOrInUnspecializedVariation()
{
	return (mIsUnspecializedVariation) || (GetOwner()->IsUnspecializedTypeVariation());
}

bool BfMethodInstance::HasExternConstraints()
{
	return (mMethodInfoEx != NULL) && (mMethodInfoEx->mGenericParams.size() > mMethodInfoEx->mMethodGenericArguments.size());
}

bool BfMethodInstance::HasParamsArray()
{
	if (mParams.size() == 0)
		return false;
	return GetParamKind((int)mParams.size() - 1) == BfParamKind_Params;
}

int BfMethodInstance::GetStructRetIdx(bool forceStatic)
{
	if ((mReturnType->IsComposite()) && (!mReturnType->IsValuelessType()) && (!GetLoweredReturnType(NULL, NULL, forceStatic)) && (!mIsIntrinsic))
	{
		auto returnTypeInst = mReturnType->ToTypeInstance();
		if ((returnTypeInst != NULL) && (returnTypeInst->mHasUnderlyingArray))
			return -1;

		auto thisType = mMethodInstanceGroup->mOwner;
		if (thisType->mModule->mCompiler->mOptions.mPlatformType != BfPlatformType_Windows)
			return 0;
		if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodInfoEx->mClosureInstanceInfo->mThisOverride != NULL))
			thisType = mMethodInfoEx->mClosureInstanceInfo->mThisOverride;

		if ((!HasThis()) || (forceStatic))
			return 0;
		if (!thisType->IsValueType())
			return 1;
		if ((mMethodDef->mIsMutating) || (!thisType->IsSplattable()) || ((!AllowsSplatting(-1)) && (!thisType->GetLoweredType(BfTypeUsage_Parameter))))
			return 1;
		return 0;
	}

	return -1;
}

bool BfMethodInstance::HasSelf()
{
	if (mReturnType->IsSelf())
		return true;
	for (int paramIdx = 0; paramIdx < GetParamCount(); paramIdx++)
		if (GetParamType(paramIdx)->IsSelf())
			return true;
	return false;
}

bool BfMethodInstance::GetLoweredReturnType(BfTypeCode* loweredTypeCode, BfTypeCode* loweredTypeCode2, bool forceStatic)
{
	// Win32 handler
	if (((mMethodDef->mIsStatic) || (forceStatic)) &&
		(mReturnType->IsComposite()) &&
		((mReturnType->mSize == 4) || (mReturnType->mSize == 8)))
	{
		auto returnTypeInst = mReturnType->ToTypeInstance();
		if ((returnTypeInst != NULL) && (returnTypeInst->mIsCRepr))
		{
			auto module = GetOwner()->mModule;
			auto compiler = module->mCompiler;
			if ((compiler->mOptions.mMachineType == BfMachineType_x86) && (compiler->mOptions.mPlatformType == BfPlatformType_Windows))
			{
				if (loweredTypeCode != NULL)
				{
					*loweredTypeCode = BfTypeCode_Int32;
					if (mReturnType->mSize == 8)
						*loweredTypeCode = BfTypeCode_Int64;
				}
				return true;
			}
		}
	}

	return mReturnType->GetLoweredType((mMethodDef->mIsStatic || forceStatic) ? BfTypeUsage_Return_Static : BfTypeUsage_Return_NonStatic, loweredTypeCode, loweredTypeCode2);
}

bool BfMethodInstance::WantsStructsAttribByVal(BfType* paramType)
{
	auto owner = GetOwner();
	if ((owner->mModule->mCompiler->mOptions.mPlatformType == BfPlatformType_Windows) &&
		(owner->mModule->mCompiler->mOptions.mMachineType == BfMachineType_x64))
		return false;
	if (owner->mModule->mCompiler->mOptions.mMachineType == BfMachineType_AArch64)
		return false;
	auto typeInst = paramType->ToTypeInstance();
	return (typeInst != NULL) && (typeInst->mIsCRepr);
}

bool BfMethodInstance::IsSkipCall(bool bypassVirtual)
{
	if ((mMethodDef->mIsSkipCall) &&
		((!mMethodDef->mIsVirtual) || (bypassVirtual)))
		return true;
	return false;
}

bool BfMethodInstance::IsVarArgs()
{
	if (mMethodDef->mParams.IsEmpty())
		return false;
	return mMethodDef->mParams.back()->mParamKind == BfParamKind_VarArgs;
}

bool BfMethodInstance::AlwaysInline()
{
	return mAlwaysInline;
}

BfImportCallKind BfMethodInstance::GetImportCallKind()
{
	if (GetImportKind() != BfImportKind_Import_Dynamic)
		return BfImportCallKind_None;
	if ((mHotMethod != NULL) && ((mHotMethod->mFlags & BfHotDepDataFlag_IsOriginalBuild) == 0))
		return BfImportCallKind_GlobalVar_Hot;
	return BfImportCallKind_GlobalVar;
}

bool BfMethodInstance::IsTestMethod()
{
	return (mMethodInfoEx != NULL) && (mMethodInfoEx->mMethodCustomAttributes != NULL) && (mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes != NULL) &&
		(mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes != NULL) && (mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes->Contains(GetOwner()->mModule->mCompiler->mTestAttributeTypeDef));
}

bool BfMethodInstance::AllowsSplatting(int paramIdx)
{
	if (paramIdx == -1)
	{
		if (mCallingConvention != BfCallingConvention_Unspecified)
			return false;
		if (mMethodDef->mIsNoSplat)
			return false;
		return !mMethodDef->HasNoThisSplat();
	}
	else
	{
		if (mCallingConvention != BfCallingConvention_Unspecified)
			return false;
		if ((mMethodDef->mIsNoSplat) || (mMethodDef->mMethodType == BfMethodType_Mixin))
		{
			if (IsImplicitCapture(paramIdx))
				return true;
			return false;
		}
		return true;
	}
}

bool BfMethodInstance::HasThis()
{
	if (mMethodDef->mIsStatic)
		return false;
	if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodInfoEx->mClosureInstanceInfo->mThisOverride != NULL))
		return !mMethodInfoEx->mClosureInstanceInfo->mThisOverride->IsValuelessType();
	return (!mMethodInstanceGroup->mOwner->IsValuelessType());
}

bool BfMethodInstance::IsVirtual()
{
	return mMethodDef->mIsVirtual && !mIsInnerOverride;
}

BfType* BfMethodInstance::GetThisType()
{
	BF_ASSERT(!mMethodDef->mIsStatic);
	if (mMethodDef->mHasExplicitThis)
	{
		auto thisType = mParams[0].mResolvedType;
		auto owner = GetOwner();
		if ((thisType->IsValueType()) && ((mMethodDef->mIsMutating) || (!AllowsSplatting(-1))) && (!thisType->GetLoweredType(BfTypeUsage_Parameter)))
			return owner->mModule->CreatePointerType(thisType);
		return thisType;
	}
	return GetParamType(-1);
}

int BfMethodInstance::GetThisIdx()
{
	if (mMethodDef->mIsStatic)
		return -2;
	if (mMethodDef->mHasExplicitThis)
		return 0;
	return -1;
}

bool BfMethodInstance::HasExplicitThis()
{
	if (mMethodDef->mIsStatic)
		return false;
	return mMethodInstanceGroup->mOwner->IsFunction();
}

int BfMethodInstance::GetParamCount()
{
	return (int)mParams.size();
}

int BfMethodInstance::GetImplicitParamCount()
{
	if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodDef->mIsLocalMethod))
		return (int)mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries.size();
	return 0;
}

void BfMethodInstance::GetParamName(int paramIdx, StringImpl& name, int& namePrefixCount)
{
	if (paramIdx == -1)
	{
		BF_ASSERT(!mMethodDef->mIsStatic);
		name = "this";
		return;
	}

	if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodDef->mIsLocalMethod))
	{
		if (paramIdx < (int)mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries.size())
		{
			name = mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries[paramIdx].mName;
			return;
		}
	}

	BfMethodParam* methodParam = &mParams[paramIdx];
	BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
	if (methodParam->mDelegateParamIdx != -1)
	{
		BfMethodInstance* invokeMethodInstance = methodParam->GetDelegateParamInvoke();
		if (methodParam->mDelegateParamNameCombine)
			name = paramDef->mName + "__" + invokeMethodInstance->GetParamName(methodParam->mDelegateParamIdx);
		else
			invokeMethodInstance->GetParamName(methodParam->mDelegateParamIdx, name, namePrefixCount);
		return;
	}
	name.Reference(paramDef->mName);
	namePrefixCount = paramDef->mNamePrefixCount;
}

String BfMethodInstance::GetParamName(int paramIdx)
{
	StringT<256> paramName;
	int namePrefixCount = 0;
	GetParamName(paramIdx, paramName, namePrefixCount);
	return paramName;
}

String BfMethodInstance::GetParamName(int paramIdx, int& namePrefixCount)
{
	StringT<256> paramName;
	GetParamName(paramIdx, paramName, namePrefixCount);
	return paramName;
}

BfType* BfMethodInstance::GetParamType(int paramIdx, bool returnUnderlyingParamsType)
{
	if (paramIdx == -1)
	{
		if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodInfoEx->mClosureInstanceInfo->mThisOverride != NULL))
			return mMethodInfoEx->mClosureInstanceInfo->mThisOverride;
		BF_ASSERT(!mMethodDef->mIsStatic);
		auto owner = mMethodInstanceGroup->mOwner;
		BfType* thisType = owner;
		if (owner->IsFunction())
		{
			BF_FATAL("Wrong 'this' index");
		}
		if ((thisType->IsValueType()) && ((mMethodDef->mIsMutating) || (!AllowsSplatting(paramIdx))) && (!thisType->GetLoweredType(BfTypeUsage_Parameter)))
			return owner->mModule->CreatePointerType(thisType);
		return thisType;
	}

	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mDelegateParamIdx != -1)
	{
		BfMethodInstance* invokeMethodInstance = methodParam->GetDelegateParamInvoke();
		return invokeMethodInstance->GetParamType(methodParam->mDelegateParamIdx, true);
	}

	if (returnUnderlyingParamsType)
	{
		BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
		if (paramDef->mParamKind == BfParamKind_Params)
		{
			auto underlyingType = methodParam->mResolvedType->GetUnderlyingType();
			if (underlyingType != NULL)
				return underlyingType;
			return methodParam->mResolvedType;
		}
	}

	return methodParam->mResolvedType;
}

bool BfMethodInstance::GetParamIsSplat(int paramIdx)
{
	if (paramIdx == -1)
	{
		BF_ASSERT(!mMethodDef->mIsStatic);
		auto owner = mMethodInstanceGroup->mOwner;
		if ((owner->IsValueType()) && (mMethodDef->mIsMutating || !AllowsSplatting(paramIdx)))
			return false;
		return owner->mIsSplattable;
	}

	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mDelegateParamIdx != -1)
	{
		BfMethodInstance* invokeMethodInstance = methodParam->GetDelegateParamInvoke();
		return invokeMethodInstance->GetParamIsSplat(methodParam->mDelegateParamIdx);
	}
	return methodParam->mIsSplat;
}

BfParamKind BfMethodInstance::GetParamKind(int paramIdx)
{
	if (paramIdx == -1)
		return BfParamKind_Normal;
	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mParamDefIdx == -1)
		return BfParamKind_ImplicitCapture;
	BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
	if (methodParam->mDelegateParamIdx != -1)
		return BfParamKind_DelegateParam;
	return paramDef->mParamKind;
}

bool BfMethodInstance::WasGenericParam(int paramIdx)
{
	if (paramIdx == -1)
		return false;
	BfMethodParam* methodParam = &mParams[paramIdx];
	return methodParam->mWasGenericParam;
}

bool BfMethodInstance::IsParamSkipped(int paramIdx)
{
	if (paramIdx == -1)
		return false;
	BfType* paramType = GetParamType(paramIdx);
	if ((paramType->CanBeValuelessType()) && (paramType->IsDataIncomplete()))
		GetModule()->PopulateType(paramType, BfPopulateType_Data);
	if ((paramType->IsValuelessType()) && (!paramType->IsMethodRef()))
		return true;
	return false;
}

bool BfMethodInstance::IsImplicitCapture(int paramIdx)
{
	if (paramIdx == -1)
		return false;
	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mParamDefIdx == -1)
		return true;
	return false;
}

BfExpression* BfMethodInstance::GetParamInitializer(int paramIdx)
{
	if (paramIdx == -1)
		return NULL;
	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mParamDefIdx == -1)
		return NULL;
	BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
	if (paramDef->mParamDeclaration != NULL)
		return paramDef->mParamDeclaration->mInitializer;
	return NULL;
}

BfTypeReference* BfMethodInstance::GetParamTypeRef(int paramIdx)
{
	if (paramIdx == -1)
		return NULL;
	BfMethodParam* methodParam = &mParams[paramIdx];
	if (methodParam->mParamDefIdx == -1)
		return NULL;
	BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
	if (paramDef->mParamDeclaration != NULL)
		return paramDef->mParamDeclaration->mTypeRef;
	return NULL;
}

BfIdentifierNode* BfMethodInstance::GetParamNameNode(int paramIdx)
{
	if (paramIdx == -1)
		return NULL;

	if ((mMethodInfoEx != NULL) && (mMethodInfoEx->mClosureInstanceInfo != NULL) && (mMethodDef->mIsLocalMethod))
	{
		if (paramIdx < (int)mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries.size())
			return mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries[paramIdx].mNameNode;
	}

	BfMethodParam* methodParam = &mParams[paramIdx];
	BfParameterDef* paramDef = mMethodDef->mParams[methodParam->mParamDefIdx];
	if (paramDef->mParamDeclaration != NULL)
		return BfNodeDynCast<BfIdentifierNode>(paramDef->mParamDeclaration->mNameNode);
	return NULL;
}

int BfMethodInstance::DbgGetVirtualMethodNum()
{
	auto module = GetOwner()->mModule;

	int vDataVal = -1;
	if (mVirtualTableIdx != -1)
	{
		module->HadSlotCountDependency();

		int vDataIdx = -1;
		vDataIdx = 1 + module->mCompiler->mMaxInterfaceSlots;
		vDataIdx += module->mCompiler->GetDynCastVDataCount();
		if ((module->mCompiler->mOptions.mHasVDataExtender) && (module->mCompiler->IsHotCompile()))
		{
			auto typeInst = mMethodInstanceGroup->mOwner;

			int extMethodIdx = (mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) - typeInst->GetOrigSelfVTableSize();
			if (extMethodIdx >= 0)
			{
				// Extension?
				int vExtOfs = typeInst->GetOrigImplBaseVTableSize();
				vDataVal = ((vDataIdx + vExtOfs + 1) << 20) | (extMethodIdx);
			}
			else
			{
				// Map this new virtual index back to the original index
				vDataIdx += (mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) + typeInst->GetOrigImplBaseVTableSize();
			}
		}
		else
		{
			vDataIdx += mVirtualTableIdx;
		}
		if (vDataVal == -1)
			vDataVal = vDataIdx;
	}
	return vDataVal;
}

void BfMethodInstance::GetIRFunctionInfo(BfModule* module, BfIRType& returnType, SizedArrayImpl<BfIRType>& paramTypes, bool forceStatic)
{
	module->PopulateType(mReturnType);

	BfTypeCode loweredReturnTypeCode = BfTypeCode_None;
	BfTypeCode loweredReturnTypeCode2 = BfTypeCode_None;
	if ((!module->mIsComptimeModule) && (GetLoweredReturnType(&loweredReturnTypeCode, &loweredReturnTypeCode2, forceStatic)) && (loweredReturnTypeCode != BfTypeCode_None))
	{
		auto irReturnType = module->GetIRLoweredType(loweredReturnTypeCode, loweredReturnTypeCode2);
		returnType = irReturnType;
	}
	else if ((mReturnType->IsValuelessType()) || (mReturnType->IsVar()))
	{
		auto voidType = module->GetPrimitiveType(BfTypeCode_None);
		returnType = module->mBfIRBuilder->MapType(voidType);
	}
	else if ((!module->mIsComptimeModule) && (GetStructRetIdx(forceStatic) != -1))
	{
		auto voidType = module->GetPrimitiveType(BfTypeCode_None);
		returnType = module->mBfIRBuilder->MapType(voidType);
		auto typeInst = mReturnType->ToTypeInstance();
		if (typeInst != NULL)
		{
			paramTypes.push_back(module->mBfIRBuilder->MapTypeInstPtr(typeInst));
		}
		else
		{
			auto ptrType = module->CreatePointerType(mReturnType);
			paramTypes.push_back(module->mBfIRBuilder->MapType(ptrType));
		}
	}
	else
	{
		returnType = module->mBfIRBuilder->MapType(mReturnType);
	}

	for (int paramIdx = -1; paramIdx < GetParamCount(); paramIdx++)
	{
		BfType* checkType = NULL;
		if (paramIdx == -1)
		{
			if ((mMethodDef->mIsStatic) || (forceStatic))
				continue;
			if (mIsClosure)
			{
				checkType = module->mCurMethodState->mClosureState->mClosureType;
			}
			else
			{
				if (HasExplicitThis())
					checkType = GetParamType(0);
				else
					checkType = GetOwner();
			}
		}
		else
		{
			if ((paramIdx == 0) && (mMethodDef->mHasExplicitThis))
				continue; // Skip over the explicit 'this'

			checkType = GetParamType(paramIdx);
		}

		/*if (GetParamName(paramIdx) == "this")
		{
			NOP;
		}*/

		bool checkLowered = false;
		bool doSplat = false;
		if (paramIdx == -1)
		{
			if ((!mMethodDef->mIsMutating) && (checkType->IsTypedPrimitive()))
			{
				checkType = checkType->GetUnderlyingType();
			}
			else if ((!module->mIsComptimeModule) && (checkType->IsSplattable()) && (AllowsSplatting(-1)))
			{
				doSplat = true;
			}
			else if ((!module->mIsComptimeModule) && (!mMethodDef->mIsMutating) && (mCallingConvention == BfCallingConvention_Unspecified))
				checkLowered = true;
		}
		else
		{
			if ((checkType->IsComposite()) && (checkType->IsIncomplete()))
				module->PopulateType(checkType, BfPopulateType_Data);

			if (checkType->IsMethodRef())
			{
				doSplat = true;
			}
			else if (checkType->IsTypedPrimitive())
			{
				checkType = checkType->GetUnderlyingType();
			}
			else if ((!module->mIsComptimeModule) && (checkType->IsSplattable()) && (AllowsSplatting(paramIdx)))
			{
				doSplat = true;
			}
			else if (!module->mIsComptimeModule)
				checkLowered = true;
		}

		BfType* checkType2 = NULL;
		if (checkLowered)
		{
			BfTypeCode loweredTypeCode = BfTypeCode_None;
			BfTypeCode loweredTypeCode2 = BfTypeCode_None;
			if (checkType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
			{
				paramTypes.push_back(module->mBfIRBuilder->GetPrimitiveType(loweredTypeCode));
				if (loweredTypeCode2 != BfTypeCode_None)
					paramTypes.push_back(module->mBfIRBuilder->GetPrimitiveType(loweredTypeCode2));
				continue;
			}
		}

		if (checkType->CanBeValuelessType())
			module->PopulateType(checkType, BfPopulateType_Data);
		if ((checkType->IsValuelessType()) && (!checkType->IsMethodRef()))
			continue;

		if ((doSplat) && (!checkType->IsMethodRef()))
		{
			int splatCount = checkType->GetSplatCount();
			if ((int)paramTypes.size() + splatCount > module->mCompiler->mOptions.mMaxSplatRegs)
			{
				auto checkTypeInst = checkType->ToTypeInstance();
				if ((checkTypeInst != NULL) && (checkTypeInst->mIsCRepr))
				{
					// CRepr splat means always splat
				}
				else
					doSplat = false;
			}
		}

		auto _AddType = [&](BfType* type)
		{
			if ((type->IsComposite()) || ((!doSplat) && (paramIdx == -1) && (type->IsTypedPrimitive())))
			{
				auto typeInst = type->ToTypeInstance();
				if (typeInst != NULL)
					paramTypes.push_back(module->mBfIRBuilder->MapTypeInstPtr(typeInst));
				else
					paramTypes.push_back(module->mBfIRBuilder->MapType(module->CreatePointerType(type)));
			}
			else
			{
				paramTypes.push_back(module->mBfIRBuilder->MapType(type));
			}
		};

		if (doSplat)
		{
			BfTypeUtils::SplatIterate([&](BfType* checkType)
			{
				_AddType(checkType);
			}, checkType);
		}
		else
			_AddType(checkType);

		if (checkType2 != NULL)
			_AddType(checkType2);
	}

	if ((!module->mIsComptimeModule) && (GetStructRetIdx(forceStatic) == 1))
	{
		BF_SWAP(paramTypes[0], paramTypes[1]);
	}
}

int BfMethodInstance::GetIRFunctionParamCount(BfModule* module)
{
	//TODO: This is dumb, do this better
	SizedArray<BfIRType, 8> params;
	BfIRType returnType;
	GetIRFunctionInfo(module, returnType, params);
	return (int)params.size();
}

bool BfMethodInstance::IsExactMatch(BfMethodInstance* other, bool ignoreImplicitParams, bool checkThis)
{
	if (mReturnType != other->mReturnType)
		return false;

	int implicitParamCountA = ignoreImplicitParams ? GetImplicitParamCount() : 0;
	int implicitParamCountB = ignoreImplicitParams ? other->GetImplicitParamCount() : 0;

	if (HasExplicitThis())
	{
	}

// 	if (other->HasExplicitThis())
// 	{
// 		if (!HasExplicitThis())
// 			return false;
// 		if (GetParamType(-1) != other->GetParamType(-1))
// 			return false;
// 	}

	if (checkThis)
	{
		if (other->mMethodDef->mIsStatic != mMethodDef->mIsStatic)
			return false;

// 		{
// 			// If we are static and we have to match a non-static method, allow us to do so if we have an explicitly defined 'this' param that matches
//
// 			if (other->mMethodDef->mIsStatic)
// 				return false;
//
// 			if ((GetParamCount() > 0) && (GetParamName(0) == "this"))
// 			{
// 				auto thisType = GetParamType(0);
// 				auto otherThisType = other->GetParamType(-1);
// 				if (thisType != otherThisType)
// 					return false;
//
// 				implicitParamCountA++;
// 			}
// 			else
// 			{
// 				// Valueless types don't actually pass a 'this' anyway
// 				if (!other->GetOwner()->IsValuelessType())
// 					return false;
// 			}
// 		}

		if (!mMethodDef->mIsStatic)
		{
			if (GetThisType() != other->GetThisType())
			{
				return false;
			}
		}
	}

	if (mMethodDef->mHasExplicitThis)
		implicitParamCountA++;
	if (other->mMethodDef->mHasExplicitThis)
		implicitParamCountB++;

	if (GetParamCount() - implicitParamCountA != other->GetParamCount() - implicitParamCountB)
		return false;
	for (int i = 0; i < (int)GetParamCount() - implicitParamCountA; i++)
	{
		auto paramA = GetParamType(i + implicitParamCountA);
		auto paramB = other->GetParamType(i + implicitParamCountB);
		if (paramA != paramB)
			return false;
	}
	return true;
}

bool BfMethodInstance::IsReifiedAndImplemented()
{
	return mIsReified && mMethodInstanceGroup->IsImplemented();
}

BfMethodInfoEx* BfMethodInstance::GetMethodInfoEx()
{
	if (mMethodInfoEx == NULL)
		mMethodInfoEx = new BfMethodInfoEx();
	return mMethodInfoEx;
}

void BfMethodInstance::ReportMemory(MemReporter* memReporter)
{
	memReporter->BeginSection("MethodInstance");
	memReporter->Add(sizeof(BfMethodInstance));

	if (mMethodInfoEx != NULL)
	{
		memReporter->BeginSection("MethodInfoEx");
		memReporter->Add(sizeof(BfMethodInfoEx));

		if (!mMethodInfoEx->mGenericParams.IsEmpty())
			memReporter->AddVecPtr("GenericParams", mMethodInfoEx->mGenericParams, false);
		if (!mMethodInfoEx->mGenericTypeBindings.IsEmpty())
			memReporter->AddMap("GenericTypeBindings", mMethodInfoEx->mGenericTypeBindings, false);
		if (mMethodInfoEx->mMethodCustomAttributes != NULL)
		{
			if (mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes != NULL)
				mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes->ReportMemory(memReporter);
		}
		if (!mMethodInfoEx->mMethodGenericArguments.IsEmpty())
			memReporter->AddVec("MethodGenericArguments", mMethodInfoEx->mMethodGenericArguments, false);
		if (!mMethodInfoEx->mMangledName.IsEmpty())
			memReporter->AddStr("MangledName", mMethodInfoEx->mMangledName);

		memReporter->EndSection();
	}

	memReporter->AddVec("Params", mParams, false);
	if (!mDefaultValues.IsEmpty())
		memReporter->AddVec("DefaultValues", mDefaultValues, false);

	memReporter->EndSection();
}

void BfCustomAttributes::ReportMemory(MemReporter* memReporter)
{
	memReporter->BeginSection("CustomAttributes");
	memReporter->Add(sizeof(BfCustomAttributes));
	memReporter->AddVec(mAttributes);
	memReporter->EndSection();
}

//////////////////////////////////////////////////////////////////////////

BfModuleMethodInstance::BfModuleMethodInstance(BfMethodInstance* methodInstance)
{
	mMethodInstance = methodInstance;
	if (methodInstance != NULL)
		mFunc = mMethodInstance->mIRFunction;
	else
		mFunc = BfIRValue();
// 	if (methodInstance->GetImportCallKind() == BfImportCallKind_Thunk)
// 	{
// 		auto declModule = methodInstance->mDeclModule;
// 		BfIRValue* irFuncPtr = NULL;
// 		if (declModule->mFuncReferences.TryGetValue(methodInstance, &irFuncPtr))
// 			mFunc = *irFuncPtr;
// 	}
}

//////////////////////////////////////////////////////////////////////////

BfMethodRefType::~BfMethodRefType()
{
	if (!mContext->mDeleting)
		BF_ASSERT(mMethodRef == NULL);
}

BfMethodInstanceGroup::BfMethodInstanceGroup(BfMethodInstanceGroup&& prev) noexcept
{
	mOwner = prev.mOwner;
	mDefault = prev.mDefault;
	mMethodSpecializationMap = prev.mMethodSpecializationMap;
	mDefaultCustomAttributes = prev.mDefaultCustomAttributes;
	mMethodIdx = prev.mMethodIdx;
	mRefCount = prev.mRefCount;
	mOnDemandKind = prev.mOnDemandKind;
	mExplicitlyReflected = prev.mExplicitlyReflected;
	mHasEmittedReference = prev.mHasEmittedReference;

	if (mDefault != NULL)
		mDefault->mMethodInstanceGroup = this;
	if (mMethodSpecializationMap != NULL)
	{
		for (auto& pair : *mMethodSpecializationMap)
			pair.mValue->mMethodInstanceGroup = this;
	}

	prev.mDefaultCustomAttributes = NULL;
	prev.mRefCount = 0;
	prev.mDefault = NULL;
	prev.mMethodSpecializationMap = NULL;
}

BfMethodInstanceGroup::~BfMethodInstanceGroup()
{
	if (mRefCount != 0)
	{
		BF_ASSERT(mOwner->mModule->mContext->mDeleting);
	}
	delete mDefault;
	if (mMethodSpecializationMap != NULL)
	{
		for (auto& kv : *mMethodSpecializationMap)
			delete kv.mValue;
		delete mMethodSpecializationMap;
	}
	delete mDefaultCustomAttributes;
}

//////////////////////////////////////////////////////////////////////////

BfTypeInstance::~BfTypeInstance()
{
	ReleaseData();
	delete mTypeInfoEx;
	delete mGenericTypeInfo;
	delete mCeTypeInfo;
	delete mCustomAttributes;
	delete mAttributeData;
	for (auto methodInst : mInternalMethods)
		delete methodInst;
	for (auto operatorInfo : mOperatorInfo)
		delete operatorInfo;
	for (auto localMethod : mOwnedLocalMethods)
		delete localMethod;
	delete mHotTypeData;
	delete mConstHolder;
	if ((mTypeDef != NULL) && (mTypeDef->mEmitParent != NULL))
	{
		mMethodInstanceGroups.Clear();
		BfLogSys(mModule->mSystem, "Type %p dtor deleting typeDef %p\n", this, mTypeDef);
		delete mTypeDef;
	}
}

void BfTypeInstance::ReleaseData()
{
	for (auto& kv : mInternalAccessMap)
	{
		auto& internalAcessSet = kv.mValue;
		for (auto& namespaceComposite : internalAcessSet.mNamespaces)
			mModule->mSystem->ReleaseAtomComposite(namespaceComposite);
	}
	mInternalAccessMap.Clear();
}

void BfTypeInstance::Dispose()
{
	delete mGenericTypeInfo;
	mGenericTypeInfo = NULL;
	mTypeDef = NULL;
}

int BfTypeInstance::GetSplatCount()
{
	if (IsValuelessType())
		return 0;
	if (!mIsSplattable)
		return 1;
	int splatCount = 0;
	BfTypeUtils::SplatIterate([&](BfType* checkType) { splatCount++; }, this);
	return splatCount;
}

bool BfTypeInstance::IsString()
{
	return IsInstanceOf(mContext->mCompiler->mStringTypeDef);
}

int BfTypeInstance::GetOrigVTableSize()
{
	if (!mModule->mCompiler->mOptions.mHasVDataExtender)
	{
		BF_ASSERT(mHotTypeData == NULL);
		return mVirtualMethodTableSize;
	}

	if (mHotTypeData != NULL)
	{
		// When we have a pending data change, treat it as a fresh vtable
		if ((!mHotTypeData->mPendingDataChange) && (mHotTypeData->mVTableOrigLength != -1))
			return mHotTypeData->mVTableOrigLength;
	}
	if (mBaseType != NULL)
		return mBaseType->GetOrigVTableSize() + (mVirtualMethodTableSize - mBaseType->mVirtualMethodTableSize);
    return mVirtualMethodTableSize;
}

int BfTypeInstance::GetSelfVTableSize()
{
	if (mBaseType != NULL)
	{
		BF_ASSERT(mBaseType->mVirtualMethodTableSize > 0);
		return mVirtualMethodTableSize - mBaseType->mVirtualMethodTableSize;
	}
	return mVirtualMethodTableSize;
}

int BfTypeInstance::GetOrigSelfVTableSize()
{
	if (mBaseType != NULL)
		return GetOrigVTableSize() - GetOrigImplBaseVTableSize();
	return GetOrigVTableSize();
}

int BfTypeInstance::GetImplBaseVTableSize()
{
	auto implBase = GetImplBaseType();
	if (implBase != NULL)
		return implBase->mVirtualMethodTableSize;
	return 0;
}

int BfTypeInstance::GetOrigImplBaseVTableSize()
{
	auto implBase = GetImplBaseType();
	if (implBase != NULL)
		return mBaseType->GetOrigVTableSize();
	return 0;
}

int BfTypeInstance::GetIFaceVMethodSize()
{
	int maxIFaceIdx = 0;
	auto checkTypeInstance = this;
	while (checkTypeInstance != NULL)
	{
		for (auto&& interfaceEntry : checkTypeInstance->mInterfaces)
		{
			maxIFaceIdx = BF_MAX(maxIFaceIdx, interfaceEntry.mStartVirtualIdx + interfaceEntry.mInterfaceType->mVirtualMethodTableSize);
		}
		checkTypeInstance = checkTypeInstance->mBaseType;
	}
	return maxIFaceIdx;
}

BfType* BfTypeInstance::GetUnionInnerType(bool* wantSplat)
{
	if (wantSplat != NULL)
		*wantSplat = false;

	if (!mIsUnion)
		return NULL;

	BfTypeState typeState(this, mContext->mCurTypeState);
	typeState.mPopulateType = BfPopulateType_Data;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	int unionSize = 0;
	BfType* unionInnerType = NULL;
	bool makeRaw = false;
	for (int fieldIdx = 0; fieldIdx < (int)mFieldInstances.size(); fieldIdx++)
	{
		auto fieldInstance = (BfFieldInstance*)&mFieldInstances[fieldIdx];
		auto fieldDef = fieldInstance->GetFieldDef();

		BfType* checkInnerType = NULL;

		if (fieldDef == NULL)
			continue;

		if ((fieldDef->mIsConst) && (fieldInstance->mIsEnumPayloadCase))
		{
			BF_ASSERT(mIsUnion);
			checkInnerType = fieldInstance->mResolvedType;
		}

		if (fieldInstance->mDataIdx >= 0)
		{
			checkInnerType = fieldInstance->mResolvedType;
		}

		if (checkInnerType != NULL)
		{
			SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);

			mModule->PopulateType(checkInnerType, checkInnerType->IsValueType() ? BfPopulateType_Data : BfPopulateType_Declaration);
			if (checkInnerType->mSize > unionSize)
				unionSize = checkInnerType->mSize;

			if ((!checkInnerType->IsValuelessType()) && (checkInnerType != unionInnerType))
			{
				if (unionInnerType == NULL)
				{
					unionInnerType = checkInnerType;
				}
				else
				{
					if (checkInnerType->mSize > unionInnerType->mSize)
					{
						bool needsMemberCasting = false;
						if (!mModule->AreSplatsCompatible(checkInnerType, unionInnerType, &needsMemberCasting))
						{
							unionInnerType = NULL;
							makeRaw = true;
						}
						else
						{
							unionInnerType = checkInnerType;
						}
					}
					else
					{
						bool needsMemberCasting = false;
						if (!mModule->AreSplatsCompatible(unionInnerType, checkInnerType, &needsMemberCasting))
						{
							unionInnerType = NULL;
							makeRaw = true;
						}
					}
				}
			}
		}
	}

	BF_ASSERT(unionInnerType != this);

	// Don't allow a float for the inner type -- to avoid invalid loading invalid FP bit patterns during copies
	if ((unionInnerType != NULL) && (!makeRaw))
	{
		if (wantSplat != NULL)
			*wantSplat = true;
	}
	else
	{
		switch (unionSize)
		{
		case 0: return mModule->CreateSizedArrayType(mModule->GetPrimitiveType(BfTypeCode_Int8), 0);
		case 1: return mModule->GetPrimitiveType(BfTypeCode_Int8);
		case 2: if (mInstAlign >= 2) return mModule->GetPrimitiveType(BfTypeCode_Int16);
		case 4: if (mInstAlign >= 4) return mModule->GetPrimitiveType(BfTypeCode_Int32);
		case 8: if (mInstAlign >= 8) return mModule->GetPrimitiveType(BfTypeCode_Int64);
		}

		if ((unionSize % 8 == 0) && (mInstAlign >= 8))
			return mModule->CreateSizedArrayType(mModule->GetPrimitiveType(BfTypeCode_Int64), unionSize / 8);
		if ((unionSize % 4 == 0) && (mInstAlign >= 4))
			return mModule->CreateSizedArrayType(mModule->GetPrimitiveType(BfTypeCode_Int32), unionSize / 4);
		if ((unionSize % 2 == 0) && (mInstAlign >= 2))
			return mModule->CreateSizedArrayType(mModule->GetPrimitiveType(BfTypeCode_Int16), unionSize / 2);
		return mModule->CreateSizedArrayType(mModule->GetPrimitiveType(BfTypeCode_Int8), unionSize);
	}

	return unionInnerType;
}

BfPrimitiveType* BfTypeInstance::GetDiscriminatorType(int* outDataIdx)
{
	BF_ASSERT(IsPayloadEnum());
	auto& fieldInstance = mFieldInstances.back();
	BF_ASSERT(fieldInstance.GetFieldDef() == NULL);

	if (fieldInstance.mResolvedType == NULL)
	{
		BF_ASSERT(IsIncomplete());
		// Use Int64 as a placeholder until we determine the correct type...
		return mModule->GetPrimitiveType(BfTypeCode_Int64);
	}

	BF_ASSERT(fieldInstance.mResolvedType != NULL);
	BF_ASSERT(fieldInstance.mResolvedType->IsPrimitiveType());
	if (outDataIdx != NULL)
		*outDataIdx = fieldInstance.mDataIdx;
	return (BfPrimitiveType*)fieldInstance.mResolvedType;
}

void BfTypeInstance::GetUnderlyingArray(BfType*& type, int& size, bool& isVector)
{
	if (mCustomAttributes == NULL)
		return;
	auto attributes = mCustomAttributes->Get(mModule->mCompiler->mUnderlyingArrayAttributeTypeDef);
	if (attributes == NULL)
		return;
	if (attributes->mCtorArgs.size() != 3)
		return;

	auto typeConstant = mConstHolder->GetConstant(attributes->mCtorArgs[0]);
	auto sizeConstant = mConstHolder->GetConstant(attributes->mCtorArgs[1]);
	auto isVectorConstant = mConstHolder->GetConstant(attributes->mCtorArgs[2]);
	if ((typeConstant == NULL) || (sizeConstant == NULL) || (isVectorConstant == NULL))
		return;
	if (typeConstant->mConstType != BfConstType_TypeOf)
		return;

	type = (BfType*)(intptr)typeConstant->mInt64;
	size = sizeConstant->mInt32;
	isVector = isVectorConstant->mBool;
}

bool BfTypeInstance::GetLoweredType(BfTypeUsage typeUsage, BfTypeCode* outTypeCode, BfTypeCode* outTypeCode2)
{
	if ((mTypeDef->mTypeCode != BfTypeCode_Struct) || (IsBoxed()) || (mIsSplattable))
		return false;
	if (mHasUnderlyingArray)
		return false;

	bool deepCheck = false;

	if (mModule->mCompiler->mOptions.mPlatformType == BfPlatformType_Windows)
	{
		// Odd Windows rule: composite returns for non-static methods are always sret
		if (typeUsage == BfTypeUsage_Return_NonStatic)
			return false;
	}
	else
	{
		// Non-Win64 systems allow lowered splitting of composites over multiple params
		if (mModule->mSystem->mPtrSize == 8)
			deepCheck = true;
		else
		{
			// We know this is correct for Linux x86 and Android armv7
			if (mModule->mCompiler->mOptions.mPlatformType == BfPlatformType_Linux)
			{
				if ((typeUsage == BfTypeUsage_Return_NonStatic) || (typeUsage == BfTypeUsage_Return_Static))
					return false;
			}
		}
	}

	int maxInstSize = 16;
	if (mModule->mCompiler->mOptions.mMachineType == BfMachineType_AArch64)
		maxInstSize = 32;

	if (deepCheck)
	{
		if ((mInstSize >= 4) && (mInstSize <= maxInstSize))
		{
			BfTypeCode types[8] = { BfTypeCode_None };

			std::function<void(BfType*, int)> _CheckType = [&](BfType* type, int offset)
			{
				if (auto typeInst = type->ToTypeInstance())
				{
					if (typeInst->IsValueType())
					{
						if (typeInst->mBaseType != NULL)
							_CheckType(typeInst->mBaseType, offset);

						for (auto& fieldInstance : typeInst->mFieldInstances)
						{
							if (fieldInstance.mDataOffset >= 0)
								_CheckType(fieldInstance.mResolvedType, offset + fieldInstance.mDataOffset);
						}
					}
					else
					{
						types[offset / 4] = BfTypeCode_Object;
					}
				}
				else if (type->IsPrimitiveType())
				{
					auto primType = (BfPrimitiveType*)type;
					types[offset / 4] = primType->mTypeDef->mTypeCode;
				}
				else if (type->IsSizedArray())
				{
					auto sizedArray = (BfSizedArrayType*)type;
					for (int i = 0; i < sizedArray->mElementCount; i++)
						_CheckType(sizedArray->mElementType, offset + i * sizedArray->mElementType->GetStride());
				}
			};

			_CheckType(this, 0);

			bool handled = false;

			if (mModule->mCompiler->mOptions.mMachineType == BfMachineType_AArch64)
			{
				// For returns, we want to avoid sret but not actually lower
				bool writeOutCode = (typeUsage != BfTypeUsage_Return_NonStatic) && (typeUsage != BfTypeUsage_Return_Static);

				if ((types[0] == BfTypeCode_Float) &&
					(types[1] == BfTypeCode_Float) &&
					(types[2] == BfTypeCode_None))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_FloatX2;
					return true;
				}

				if ((types[0] == BfTypeCode_Float) &&
					(types[1] == BfTypeCode_Float) &&
					(types[2] == BfTypeCode_Float) &&
					(types[3] == BfTypeCode_None))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_FloatX3;
					return true;
				}

				if ((types[0] == BfTypeCode_Float) &&
					(types[1] == BfTypeCode_Float) &&
					(types[2] == BfTypeCode_Float) &&
					(types[3] == BfTypeCode_Float) &&
					(types[4] == BfTypeCode_None))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_FloatX4;
					return true;
				}

				if ((types[0] == BfTypeCode_Double) &&
					(types[2] == BfTypeCode_Double) &&
					(types[4] == BfTypeCode_None))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_DoubleX2;
					return true;
				}

				if ((types[0] == BfTypeCode_Double) &&
					(types[2] == BfTypeCode_Double) &&
					(types[4] == BfTypeCode_Double) &&
					(types[6] == BfTypeCode_None))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_DoubleX3;
					return true;
				}

				if ((types[0] == BfTypeCode_Double) &&
					(types[2] == BfTypeCode_Double) &&
					(types[4] == BfTypeCode_Double) &&
					(types[6] == BfTypeCode_Double))
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_DoubleX4;
					return true;
				}

				if (mInstSize <= 8)
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_Int64;
					return true;
				}

				if (mInstSize <= 16)
				{
					if ((outTypeCode != NULL) && (writeOutCode))
						*outTypeCode = BfTypeCode_Int64X2;
					return true;
				}

				return false;
			}
			else if (mModule->mCompiler->mOptions.mPlatformType == BfPlatformType_Windows)
			{
				bool hasFloat = false;
				for (int type = 0; type < 4; type++)
				{
					if ((types[type] == BfTypeCode_Float) ||
						(types[type] == BfTypeCode_Double))
						hasFloat = false;
				}

				if (!hasFloat)
				{
					if (mInstSize == 4)
					{
						if (outTypeCode != NULL)
							*outTypeCode = BfTypeCode_Int32;
						return true;
					}

					if (mInstSize == 8)
					{
						if (outTypeCode != NULL)
							*outTypeCode = BfTypeCode_Int64;
						return true;
					}
				}
			}
			else
			{
				if (mInstSize >= 8)
				{
					if (outTypeCode != NULL)
						*outTypeCode = BfTypeCode_Int64;
				}

				if (mInstSize == 8)
				{
					handled = true;
				}

				if (mInstSize == 9)
				{
					handled = true;
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Int8;
				}
				if (mInstSize == 10)
				{
					handled = true;
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Int16;
				}
				if (mInstSize == 12)
				{
					handled = true;
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Int32;
				}
				if (mInstSize == 16)
				{
					handled = true;
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Int64;
				}

				if ((types[0] == BfTypeCode_Float) && (types[1] == BfTypeCode_None))
				{
					handled = true;
					if (outTypeCode != NULL)
						*outTypeCode = BfTypeCode_Float;
				}
				if ((types[0] == BfTypeCode_Float) && (types[1] == BfTypeCode_Float))
				{
					if (outTypeCode != NULL)
						*outTypeCode = BfTypeCode_Float2;
				}
				if (types[0] == BfTypeCode_Double)
				{
					if (outTypeCode != NULL)
						*outTypeCode = BfTypeCode_Double;
				}

				if ((types[2] == BfTypeCode_Float) && (mInstSize == 12))
				{
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Float;
				}
				if ((types[2] == BfTypeCode_Float) && (types[3] == BfTypeCode_Float))
				{
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Float2;
				}
				if (types[2] == BfTypeCode_Double)
				{
					if (outTypeCode2 != NULL)
						*outTypeCode2 = BfTypeCode_Double;
				}

				if (handled)
					return true;
			}
		}
	}

	BfTypeCode typeCode = BfTypeCode_None;
	BfTypeCode pow2TypeCode = BfTypeCode_None;

	switch (mInstSize)
	{
	case 1:
		pow2TypeCode = BfTypeCode_Int8;
		break;
	case 2:
		pow2TypeCode = BfTypeCode_Int16;
		break;
	case 3:
		typeCode = BfTypeCode_Int24;
		break;
	case 4:
		pow2TypeCode = BfTypeCode_Int32;
		break;
	case 5:
		typeCode = BfTypeCode_Int40;
		break;
	case 6:
		typeCode = BfTypeCode_Int48;
		break;
	case 7:
		typeCode = BfTypeCode_Int56;
		break;
	case 8:
		if (mModule->mSystem->mPtrSize == 8)
		{
			pow2TypeCode = BfTypeCode_Int64;
			break;
		}
		if ((typeUsage == BfTypeUsage_Return_Static) && (mModule->mCompiler->mOptions.mPlatformType == BfPlatformType_Windows))
		{
			pow2TypeCode = BfTypeCode_Int64;
			break;
		}
		break;
	}

	if (pow2TypeCode != BfTypeCode_None)
	{
		if (outTypeCode != NULL)
			*outTypeCode = pow2TypeCode;
		return true;
	}

	if ((mModule->mCompiler->mOptions.mPlatformType != BfPlatformType_Windows) && (mModule->mSystem->mPtrSize == 8))
	{
		if (typeCode != BfTypeCode_None)
		{
			if (outTypeCode != NULL)
				*outTypeCode = typeCode;
			return true;
		}
	}

	return false;
}

bool BfTypeInstance::HasEquivalentLayout(BfTypeInstance* compareTo)
{
	if (mFieldInstances.size() != compareTo->mFieldInstances.size())
		return false;

	for (int fieldIdx = 0; fieldIdx < (int)mFieldInstances.size(); fieldIdx++)
	{
		auto fieldInstance = &mFieldInstances[fieldIdx];
		auto otherFieldInstance = &compareTo->mFieldInstances[fieldIdx];
		if (fieldInstance->mResolvedType != otherFieldInstance->mResolvedType)
			return false;
	}
	return true;
}

BfIRConstHolder* BfTypeInstance::GetOrCreateConstHolder()
{
	if (mConstHolder == NULL)
		mConstHolder = new BfIRConstHolder(mModule);
	return mConstHolder;
}

BfIRValue BfTypeInstance::CreateConst(BfConstant* fromConst, BfIRConstHolder* fromHolder)
{
	if (mConstHolder == NULL)
		mConstHolder = new BfIRConstHolder(mModule);
	return mConstHolder->CreateConst(fromConst, fromHolder);
}

bool BfTypeInstance::HasOverrideMethods()
{
	if (mTypeDef->mHasOverrideMethods)
		return true;
	if (mBaseType != NULL)
		return mBaseType->HasOverrideMethods();
	return false;
}

bool BfTypeInstance::GetResultInfo(BfType*& valueType, int& okTagId)
{
	BF_ASSERT(!IsDataIncomplete());
	if (mFieldInstances.size() < 2)
		return false;

	for (auto& fieldInstance : mFieldInstances)
	{
		if (!fieldInstance.mIsEnumPayloadCase)
			continue;

		if ((fieldInstance.mIsEnumPayloadCase) && (fieldInstance.GetFieldDef()->mName == "Ok") && (fieldInstance.mResolvedType->IsTuple()))
		{
			auto tupleType = (BfTypeInstance*)fieldInstance.mResolvedType;
			if (tupleType->mFieldInstances.size() == 1)
			{
				valueType = tupleType->mFieldInstances[0].mResolvedType;
				okTagId = -fieldInstance.mDataIdx - 1;
				return true;
			}
		}
		break;
	}
	return false;
}

void BfTypeInstance::ReportMemory(MemReporter* memReporter)
{
	if (mGenericTypeInfo != NULL)
		mGenericTypeInfo->ReportMemory(memReporter);

	memReporter->Add(sizeof(BfTypeInstance));

	int depSize = 0;
	depSize += sizeof((int)mDependencyMap.mTypeSet.mAllocSize * sizeof(BfDependencyMap::TypeMap::EntryPair));
	memReporter->Add("DepMap", depSize);
	memReporter->AddVec(mInterfaces, false);
	memReporter->AddVec(mInterfaceMethodTable, false);

	if (mCustomAttributes != NULL)
		mCustomAttributes->ReportMemory(memReporter);

	int methodCount = 0;
	memReporter->BeginSection("MethodData");
	for (auto& methodInstGroup : mMethodInstanceGroups)
	{
		memReporter->Add(sizeof(BfMethodInstanceGroup));
		if (methodInstGroup.mDefault != NULL)
		{
			methodInstGroup.mDefault->ReportMemory(memReporter);
			methodCount++;
		}
		if (methodInstGroup.mMethodSpecializationMap != NULL)
		{
			memReporter->Add((int)methodInstGroup.mMethodSpecializationMap->mAllocSize * sizeof(Dictionary<BfTypeVector, BfMethodInstance*>::EntryPair));
			for (auto kv : *methodInstGroup.mMethodSpecializationMap)
			{
				methodCount++;
				kv.mValue->ReportMemory(memReporter);
			}
		}
	}
	memReporter->EndSection();

	memReporter->AddVec("VirtualMethodTable", mVirtualMethodTable, false);
	memReporter->AddVec(mFieldInstances, false);
	memReporter->AddVec(mInternalMethods, false);
	memReporter->AddMap("SpecializedMethodReferences", mSpecializedMethodReferences, false);
	memReporter->AddMap("LookupResults", mLookupResults, false);
	if (mConstHolder != NULL)
		memReporter->Add("ConstHolder", mConstHolder->mTempAlloc.GetTotalAllocSize());

	if (mHotTypeData != NULL)
	{
		AutoMemReporter autoMemReporter(memReporter, "HotTypeData");
		memReporter->Add(sizeof(BfHotTypeData));
		memReporter->AddVec(mHotTypeData->mTypeVersions, false);
		for (auto typeVersion : mHotTypeData->mTypeVersions)
		{
			memReporter->AddVec(typeVersion->mMembers, false);
			memReporter->AddVec(typeVersion->mInterfaceMapping, false);
		}
		memReporter->AddVec(mHotTypeData->mVTableEntries, false);
		for (auto& entry : mHotTypeData->mVTableEntries)
			memReporter->AddStr(entry.mFuncName, false);
	}

	BfLog("%s\t%d\t%d\n", mContext->mScratchModule->TypeToString(this, BfTypeNameFlags_None).c_str(), IsGenericTypeInstance(), methodCount);
}

bool BfTypeInstance::IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfTypeDef* activeTypeDef)
{
	if (activeTypeDef == NULL)
		return false;
	if (declaringTypeDef == activeTypeDef)
		return true;
	return activeTypeDef->mProject->ContainsReference(declaringTypeDef->mProject);
}

bool BfTypeInstance::IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProject* curProject)
{
	if (declaringTypeDef->mProject == curProject)
		return true;
	return curProject->ContainsReference(declaringTypeDef->mProject);
}

bool BfTypeInstance::IsTypeMemberAccessible(BfTypeDef* declaringTypeDef, BfProjectSet* visibleProjectSet)
{
	if (visibleProjectSet == NULL)
		return false;
	return visibleProjectSet->Contains(declaringTypeDef->mProject);
}

bool BfTypeInstance::WantsGCMarking()
{
	BF_ASSERT(mTypeDef->mTypeCode != BfTypeCode_Extension);
	if (IsObjectOrInterface())
		return true;
	if ((IsEnum()) && (!IsPayloadEnum()))
		return false;
	BF_ASSERT((mDefineState >= BfTypeDefineState_Defined) || (mTypeFailed));
	return mWantsGCMarking;
}

///

BfGenericExtensionEntry::~BfGenericExtensionEntry()
{
	for (auto genericParamInstance : mGenericParams)
		genericParamInstance->Release();
}

///

BfGenericTypeInfo::~BfGenericTypeInfo()
{
	for (auto genericParamInstance : mGenericParams)
		genericParamInstance->Release();
	delete mGenericExtensionInfo;
}

BfGenericTypeInfo::GenericParamsVector* BfTypeInstance::GetGenericParamsVector(BfTypeDef* declaringTypeDef)
{
	if (mGenericTypeInfo == NULL)
		return NULL;
	if ((declaringTypeDef == mTypeDef) ||
		(declaringTypeDef->mTypeDeclaration == mTypeDef->mTypeDeclaration))
		return &mGenericTypeInfo->mGenericParams;

	if (mGenericTypeInfo->mGenericExtensionInfo == NULL)
		return NULL;

	BfGenericExtensionEntry* genericExEntry = NULL;
	if (mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(declaringTypeDef, &genericExEntry))
		return &genericExEntry->mGenericParams;

	return &mGenericTypeInfo->mGenericParams;
}

void BfTypeInstance::GenerateProjectsReferenced()
{
	if (mGenericTypeInfo == NULL)
		return;
	BF_ASSERT(mGenericTypeInfo->mProjectsReferenced.empty());
	mGenericTypeInfo->mProjectsReferenced.push_back(mTypeDef->mProject);
	for (auto genericArgType : mGenericTypeInfo->mTypeGenericArguments)
		BfTypeUtils::GetProjectList(genericArgType, &mGenericTypeInfo->mProjectsReferenced, 0);
}

bool BfTypeInstance::IsAlwaysInclude()
{
	bool alwaysInclude = mTypeDef->mIsAlwaysInclude || mTypeDef->mProject->mAlwaysIncludeAll;
	if (mTypeOptionsIdx > 0)
	{
		auto typeOptions = mModule->mSystem->GetTypeOptions(mTypeOptionsIdx);
		typeOptions->Apply(alwaysInclude, BfOptionFlags_ReflectAlwaysIncludeType);
	}
	if ((mAlwaysIncludeFlags & BfAlwaysIncludeFlag_Type) != 0)
		alwaysInclude = true;
	return alwaysInclude;
}

bool BfTypeInstance::IsSpecializedByAutoCompleteMethod()
{
	if (mGenericTypeInfo == NULL)
		return false;
	for (auto methodArg : mGenericTypeInfo->mTypeGenericArguments)
	{
		// If we are specialized by an autocompleted method reference
		if (methodArg->IsMethodRef())
		{
			auto methodRefType = (BfMethodRefType*)methodArg;
			if (methodRefType->mIsAutoCompleteMethod)
				return true;
		}
	}

	return false;
}

bool BfTypeInstance::IsNullable()
{
	return IsInstanceOf(mContext->mCompiler->mNullableTypeDef);
}

bool BfTypeInstance::HasVarConstraints()
{
	if (mGenericTypeInfo == NULL)
		return false;
	for (auto genericParam : mGenericTypeInfo->mGenericParams)
	{
		if (genericParam->mGenericParamFlags & BfGenericParamFlag_Var)
			return true;
	}
	return false;
}

bool BfTypeInstance::IsTypeMemberIncluded(BfTypeDef* typeDef, BfTypeDef* activeTypeDef, BfModule* module)
{
	if (mGenericTypeInfo == NULL)
		return true;
	if (mGenericTypeInfo->mGenericExtensionInfo == NULL)
		return true;
	if ((typeDef == NULL) || (typeDef == activeTypeDef))
		return true;

	// The combined type declaration is the root type declaration, it's implicitly included
	if (typeDef->mTypeDeclaration == mTypeDef->mTypeDeclaration)
		return true;

	BfGenericExtensionEntry* genericExEntry = NULL;
	if (!mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(typeDef, &genericExEntry))
		return true;

	if (mGenericTypeInfo->mIsUnspecialized)
	{
		if (module == NULL)
			return true; // During population

		auto declConstraints = &genericExEntry->mGenericParams;

		for (int genericIdx = 0; genericIdx < (int)declConstraints->size(); genericIdx++)
		{
			auto declGenericParam = (*declConstraints)[genericIdx];

			BfType* genericArg;
			if (genericIdx < (int)mGenericTypeInfo->mTypeGenericArguments.size())
			{
				genericArg = mGenericTypeInfo->mTypeGenericArguments[genericIdx];
			}
			else
			{
				genericArg = declGenericParam->mExternType;
			}

			if ((genericArg == NULL) || (!module->CheckGenericConstraints(BfGenericParamSource(), genericArg, NULL, declGenericParam)))
				return false;
		}

		return true;
	}

	return genericExEntry->mConstraintsPassed;
}

void BfGenericTypeInfo::ReportMemory(MemReporter* memReporter)
{
	memReporter->Add(sizeof(BfGenericTypeInfo));
	memReporter->AddVec(mTypeGenericArguments, false);
	memReporter->AddVec(mGenericParams, false);
	memReporter->AddVec(mProjectsReferenced, false);
}

BfType* BfTypeInstance::GetUnderlyingType()
{
	if (!mIsTypedPrimitive)
	{
		if ((mGenericTypeInfo != NULL) && (!mGenericTypeInfo->mTypeGenericArguments.IsEmpty()))
			return mGenericTypeInfo->mTypeGenericArguments[0];
		return NULL;
	}

	if (mTypeInfoEx == NULL)
		mTypeInfoEx = new BfTypeInfoEx();
	if (mTypeInfoEx->mUnderlyingType != NULL)
		return mTypeInfoEx->mUnderlyingType;

	auto checkTypeInst = this;
	while (checkTypeInst != NULL)
	{
		if (!checkTypeInst->mFieldInstances.empty())
		{
			mTypeInfoEx->mUnderlyingType = checkTypeInst->mFieldInstances.back().mResolvedType;
			return mTypeInfoEx->mUnderlyingType;
		}
		checkTypeInst = checkTypeInst->mBaseType;
		if (checkTypeInst->IsIncomplete())
			mModule->PopulateType(checkTypeInst, BfPopulateType_Data);
	}
	BF_FATAL("Failed");
	return mTypeInfoEx->mUnderlyingType;
}

bool BfTypeInstance::IsValuelessType()
{
	BF_ASSERT(mTypeDef->mTypeCode != BfTypeCode_Extension);
	if ((mTypeDef->mTypeCode == BfTypeCode_Object) || (mTypeDef->mTypeCode == BfTypeCode_Interface))
	{
		return false;
	}
	if (mTypeDef->mIsOpaque)
		return false;

	BF_ASSERT((mDefineState >= BfTypeDefineState_Defined) || (mTypeFailed));
	BF_ASSERT(mInstSize >= 0);
	if (mInstSize == 0)
	{
		return true;
	}

	return false;
}

bool BfTypeInstance::IsIRFuncUsed(BfIRFunction func)
{
	for (auto& group : mMethodInstanceGroups)
	{
		if (group.mDefault != NULL)
			if (group.mDefault->mIRFunction == func)
				return true;
		if (group.mMethodSpecializationMap != NULL)
		{
			for (auto& methodInstPair : *group.mMethodSpecializationMap)
			{
				auto methodInstance = methodInstPair.mValue;
				if (methodInstance->mIRFunction == func)
					return true;
			}
		}
	}
	return false;
}

void BfTypeInstance::CalcHotVirtualData(Array<int>* ifaceMapping)
{
	if (IsIncomplete())
	{
		BF_ASSERT(mHotTypeData != NULL);
		return;
	}

	if (ifaceMapping != NULL)
	{
		for (auto iface : mInterfaces)
		{
			int slotNum = iface.mInterfaceType->mSlotNum;
			if (slotNum >= 0)
			{
				if (slotNum >= (int)ifaceMapping->size())
					ifaceMapping->Resize(slotNum + 1);
				(*ifaceMapping)[slotNum] = iface.mInterfaceType->mTypeId;
			}
		}
		if (mBaseType != NULL)
			mBaseType->CalcHotVirtualData(ifaceMapping);
	}
}

//////////////////////////////////////////////////////////////////////////

BfClosureType::BfClosureType(BfTypeInstance* srcDelegate, Val128 closureHash) :
	mSource(srcDelegate->mTypeDef->mSystem)
{
	BF_ASSERT(srcDelegate->IsDelegate());
	mSrcDelegate = srcDelegate;
	mTypeDef = mSrcDelegate->mTypeDef;
	mCreatedTypeDef = false;
	mClosureHash = closureHash;
	// Hash in 72 bits of closureHash (12 characters) - low does 60 bits, high does 12 bits
	mNameAdd = "_" + BfTypeUtils::HashEncode64(mClosureHash.mLow) + BfTypeUtils::HashEncode64(mClosureHash.mHigh >> 52);
	mIsUnique = false;
}

BfClosureType::~BfClosureType()
{
	mMethodInstanceGroups.Clear();
	if (mCreatedTypeDef)
	{
		delete mTypeDef;
		mTypeDef = NULL;
	}
	for (auto directAllocNode : mDirectAllocNodes)
		delete directAllocNode;
}

void BfClosureType::Init(BfProject* bfProject)
{
	auto srcTypeDef = mSrcDelegate->mTypeDef;
	auto system = mSrcDelegate->mModule->mSystem;

	mTypeDef = new BfTypeDef();
	mTypeDef->mSystem = system;
	mTypeDef->mSource = &mSource;
	mTypeDef->mSource->mRefCount++;
	mTypeDef->mProject = bfProject;
	mTypeDef->mTypeCode = srcTypeDef->mTypeCode;
	mTypeDef->mName = system->GetAtom(srcTypeDef->mName->mString + mNameAdd);
	// Purposely leave out 'mOuterType' - this fails if the outer type is generic
	//mTypeDef->mOuterType = srcTypeDef->mOuterType;
	mTypeDef->mNamespace = srcTypeDef->mNamespace;
	system->AddNamespaceUsage(mTypeDef->mNamespace, mTypeDef->mProject);
	mTypeDef->mHash = srcTypeDef->mHash;
	mTypeDef->mSignatureHash = srcTypeDef->mSignatureHash;
// 	mTypeDef->mFullName = srcTypeDef->mFullName;
// 	if (!mTypeDef->mFullName.mParts.IsEmpty())
// 		mTypeDef->mFullName.mParts.pop_back();
// 	mTypeDef->mFullName.mParts.push_back(mTypeDef->mName);
	if (srcTypeDef->mFullName.mSize > 0)
		mTypeDef->mFullName.Set(srcTypeDef->mFullName.mParts, srcTypeDef->mFullName.mSize - 1, &mTypeDef->mName, 1);
	else
		mTypeDef->mFullName.Set(&mTypeDef->mName, 1, NULL, 0);
	system->TrackName(mTypeDef);

	mTypeDef->mTypeCode = BfTypeCode_Object;
	mTypeDef->mIsDelegate = true;
	mTypeDef->mIsClosure = true;
	mTypeDef->mDefState = BfTypeDef::DefState_Defined;

	auto baseDirectTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
	baseDirectTypeRef->Init(mSrcDelegate);
	mDirectAllocNodes.push_back(baseDirectTypeRef);
	mTypeDef->mBaseTypes.push_back(baseDirectTypeRef);
	//mTypeDef->mBaseTypes.push_back(BfDefBuilder::AllocTypeReference(&mSource, mSrcDelegate));

	mCreatedTypeDef = true;
	//mTypeDef->mBaseTypes.push_back(srcTypeDef);
}

BfFieldDef* BfClosureType::AddField(BfType* type, const StringImpl& name)
{
	auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
	directTypeRef->Init(type);
	mDirectAllocNodes.push_back(directTypeRef);
	return BfDefBuilder::AddField(mTypeDef, directTypeRef, name);
}

BfMethodDef* BfClosureType::AddDtor()
{
	return BfDefBuilder::AddDtor(mTypeDef);
}

void BfClosureType::Finish()
{
	HASH128_MIXIN(mTypeDef->mSignatureHash, mClosureHash);

	auto bfSource = mTypeDef->mSource;
	auto bfSystem = bfSource->mSystem;

	BfDefBuilder bfDefBuilder(bfSystem);
	bfDefBuilder.mCurTypeDef = mTypeDef;
	bfDefBuilder.FinishTypeDef(false);
}

//////////////////////////////////////////////////////////////////////////

BfDelegateType::~BfDelegateType()
{
	mMethodInstanceGroups.Clear();
	delete mTypeDef;
	mTypeDef = NULL;
}

void BfDelegateType::Dispose()
{
	delete mTypeDef;
	mTypeDef = NULL;
	BfTypeInstance::Dispose();
}

//////////////////////////////////////////////////////////////////////////

BfTupleType::BfTupleType()
{
	mCreatedTypeDef = false;
	mSource = NULL;
	mTypeDef = NULL;
	mIsUnspecializedType = false;
	mIsUnspecializedTypeVariation = false;
	mGenericDepth = 0;
}

BfTupleType::~BfTupleType()
{
	mMethodInstanceGroups.Clear();
	if (mCreatedTypeDef)
	{
		delete mTypeDef;
		mTypeDef = NULL;
	}
	delete mSource;
}

void BfTupleType::Init(BfProject* bfProject, BfTypeInstance* valueTypeInstance)
{
	auto srcTypeDef = valueTypeInstance->mTypeDef;
	auto system = valueTypeInstance->mModule->mSystem;

	if (mTypeDef == NULL)
		mTypeDef = new BfTypeDef();
	for (auto field : mTypeDef->mFields)
		delete field;
	mTypeDef->mFields.Clear();
	mTypeDef->mSystem = system;
	mTypeDef->mProject = bfProject;
	mTypeDef->mTypeCode = srcTypeDef->mTypeCode;
	mTypeDef->mName = system->mEmptyAtom;
	mTypeDef->mSystem = system;

	mTypeDef->mHash = srcTypeDef->mHash;
	mTypeDef->mSignatureHash = srcTypeDef->mSignatureHash;
	mTypeDef->mTypeCode = BfTypeCode_Struct;

	mCreatedTypeDef = true;
}

void BfTupleType::Dispose()
{
	if (mCreatedTypeDef)
	{
		delete mTypeDef;
		mTypeDef = NULL;
		mCreatedTypeDef = false;
	}
	BfTypeInstance::Dispose();
}

BfFieldDef* BfTupleType::AddField(const StringImpl& name)
{
	return BfDefBuilder::AddField(mTypeDef, NULL, name);
}

void BfTupleType::Finish()
{
	BF_ASSERT(!mTypeFailed);

	auto bfSystem = mTypeDef->mSystem;
	mSource = new BfSource(bfSystem);
	mTypeDef->mSource = mSource;
	mTypeDef->mSource->mRefCount++;

	BfDefBuilder bfDefBuilder(bfSystem);
	bfDefBuilder.mCurTypeDef = mTypeDef;
	bfDefBuilder.mCurDeclaringTypeDef = mTypeDef;
	bfDefBuilder.FinishTypeDef(true);
}

//////////////////////////////////////////////////////////////////////////

BfBoxedType::~BfBoxedType()
{
	//if ((mTypeDef != NULL) && (mTypeDef->mEmitParent != NULL))
	mTypeDef = NULL;
}

BfType* BfBoxedType::GetModifiedElementType()
{
	if ((mBoxedFlags & BoxedFlags_StructPtr) != 0)
	{
		auto module = mModule;
		if (module == NULL)
			module = mContext->mUnreifiedModule;
		return module->CreatePointerType(mElementType);
	}
	return mElementType;
}

//////////////////////////////////////////////////////////////////////////

int BfArrayType::GetLengthBitCount()
{
	if (mBaseType == NULL)
		mModule->PopulateType(mBaseType, BfPopulateType_BaseType);
	mModule->PopulateType(mBaseType);
	if ((mBaseType->mFieldInstances.size() == 0) || (mBaseType->mFieldInstances[0].GetFieldDef()->mName != "mLength"))
	{
		return 0;
	}
	return mBaseType->mFieldInstances[0].mResolvedType->mSize * 8;
}

//////////////////////////////////////////////////////////////////////////

int BfMethodRefType::GetCaptureDataCount()
{
	if (mMethodRef == NULL)
		return 0;
	return (int)mDataToParamIdx.size();
}

BfType* BfMethodRefType::GetCaptureType(int captureDataIdx)
{
	return mMethodRef->GetParamType(mDataToParamIdx[captureDataIdx]);
}

int BfMethodRefType::GetDataIdxFromParamIdx(int paramIdx)
{
	if (paramIdx == -1)
	{
		if (mMethodRef->HasThis())
			return 0;
		return -1;
	}
	return mParamToDataIdx[paramIdx];
}

int BfMethodRefType::GetParamIdxFromDataIdx(int dataIdx)
{
	return mDataToParamIdx[dataIdx];
}

bool BfMethodRefType::WantsDataPassedAsSplat(int dataIdx)
{
	if (dataIdx != -1)
		return false;
	return mMethodRef->GetParamIsSplat(mDataToParamIdx[dataIdx]);
}

//////////////////////////////////////////////////////////////////////////

size_t BfTypeVectorHash::operator()(const BfTypeVector& typeVec) const
{
	size_t hash = typeVec.size();
	BfResolvedTypeSet::LookupContext ctx;
	for (auto type : typeVec)
		hash = ((hash ^ BfResolvedTypeSet::Hash(type, &ctx, Beefy::BfResolvedTypeSet::BfHashFlag_None, 0)) << 5) - hash;
	return hash;
}

bool BfTypeVectorEquals::operator()(const BfTypeVector& lhs, const BfTypeVector& rhs) const
{
	if (lhs.size() != rhs.size())
		return false;
	for (int i = 0; i < (int)lhs.size(); i++)
		if (lhs[i] != rhs[i])
			return false;
	return true;
}

//////////////////////////////////////////////////////////////////////////

bool BfCustomAttributes::Contains(BfTypeDef* typeDef)
{
	for (auto& customAttr : mAttributes)
		if (customAttr.mType->mTypeDef->GetDefinition() == typeDef)
			return true;
	return false;
}

BfCustomAttribute* BfCustomAttributes::Get(BfTypeDef * typeDef)
{
	for (auto& customAttr : mAttributes)
		if (customAttr.mType->mTypeDef->GetDefinition() == typeDef)
			return &customAttr;
	return NULL;
}

BfCustomAttribute* BfCustomAttributes::Get(BfType* type)
{
	for (auto& customAttr : mAttributes)
		if (customAttr.mType == type)
			return &customAttr;
	return NULL;
}

BfCustomAttribute* BfCustomAttributes::Get(int idx)
{
	if (idx >= mAttributes.size())
		return NULL;
	return &mAttributes[idx];
}

//////////////////////////////////////////////////////////////////////////

BfResolvedTypeSet::~BfResolvedTypeSet()
{
}

#define HASH_MIX(origHashVal, newHashVal) ((((origHashVal) << 5) - (origHashVal)) ^ (newHashVal))

#define HASH_VAL_PTR 1
#define HASH_VAL_BOXED 2
#define HASH_VAL_REF 3
#define HASH_VAL_OUT 4
#define HASH_VAL_MUT 5
#define HASH_MODTYPE 6
#define HASH_CONCRETE_INTERFACE 7
#define HASH_SIZED_ARRAY 8
#define HASH_CONSTTYPE 9
#define HASH_VAL_TUPLE 10
#define HASH_DELEGATE 11
#define HASH_CONSTEXPR 12
#define HASH_GLOBAL 13
#define HASH_DOTDOTDOT 14

BfVariant BfResolvedTypeSet::EvaluateToVariant(LookupContext* ctx, BfExpression* expr, BfType*& outType)
{
	outType = NULL;

	BfConstResolver constResolver(ctx->mModule);
	BfVariant variant;
	constResolver.mBfEvalExprFlags = BfEvalExprFlags_NoCast;
	constResolver.mBfEvalExprFlags = (BfEvalExprFlags)(constResolver.mBfEvalExprFlags | BfEvalExprFlags_AllowGenericConstValue);
	constResolver.mExpectingType = ctx->mModule->GetPrimitiveType(BfTypeCode_Int64);
	auto result = constResolver.Resolve(expr);
	if (result)
	{
		// Limit the types of constants to prevent duplicate values with different types - we don't want to hash a typeref with an int32
		//  when the constraint requirement is int64 (but we don't know that at hash time)
		if ((result.mType->IsInteger()) && ((result.mType->mSize < 8) || (result.mType->GetTypeCode() == BfTypeCode_IntPtr)))
			result = ctx->mModule->Cast(expr, result, ctx->mModule->GetPrimitiveType(BfTypeCode_Int64));
		else if ((result.mType->IsFloat()) && (result.mType->mSize < 8))
			result = ctx->mModule->Cast(expr, result, ctx->mModule->GetPrimitiveType(BfTypeCode_Double));
		else if ((result.mType->IsChar()) && (result.mType->mSize < 4))
			result = ctx->mModule->Cast(expr, result, ctx->mModule->GetPrimitiveType(BfTypeCode_Char32));
		outType = result.mType;

		if (result.mKind == BfTypedValueKind_GenericConstValue)
		{
			return variant;
		}
		else
		{
			variant = ctx->mModule->TypedValueToVariant(expr, result, true);
		}
	}
	return variant;
}

int BfResolvedTypeSet::DoHash(BfType* type, LookupContext* ctx, bool allowRef, int hashSeed)
{
	//BP_ZONE("BfResolvedTypeSet::Hash");

// 	if (type->IsTypeAlias())
// 	{
// 		auto underlyingType = type->GetUnderlyingType();
// 		BF_ASSERT(underlyingType != NULL);
// 		if (underlyingType == NULL)
// 		{
// 			ctx->mFailed = true;
// 			return 0;
// 		}
// 		return Hash(underlyingType, ctx, allowRef);
// 	}
// 	else

	if (type->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)type;
		int elemHash = Hash(boxedType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_VAL_BOXED;
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsArray())
	{
		BfArrayType* arrayType = (BfArrayType*)type;
		int elemHash = Hash(arrayType->mGenericTypeInfo->mTypeGenericArguments[0], ctx, BfHashFlag_None, hashSeed) ^ (arrayType->mDimensions << 8);
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsDelegateFromTypeRef() || type->IsFunctionFromTypeRef())
	{
		auto typeInst = (BfTypeInstance*)type;
		int hashVal = HASH_DELEGATE;

		auto delegateInfo = type->GetDelegateInfo();

		hashVal = HASH_MIX(hashVal, Hash(delegateInfo->mReturnType, ctx, BfHashFlag_None, hashSeed + 1));

		auto methodDef = typeInst->mTypeDef->mMethods[0];
		BF_ASSERT(methodDef->mName == "Invoke");

		int infoParamCount = (int)delegateInfo->mParams.size();
		if (delegateInfo->mHasVarArgs)
			infoParamCount++;

		BF_ASSERT(infoParamCount == methodDef->mParams.size());

		for (int paramIdx = 0; paramIdx < delegateInfo->mParams.size(); paramIdx++)
		{
			// Parse attributes?
			hashVal = HASH_MIX(hashVal, Hash(delegateInfo->mParams[paramIdx], ctx, BfHashFlag_None, hashSeed + 1));
			String paramName = methodDef->mParams[paramIdx]->mName;
			int nameHash = (int)Hash64(paramName.c_str(), (int)paramName.length());
			hashVal = HASH_MIX(hashVal, nameHash);
		}

		if (delegateInfo->mHasVarArgs)
			hashVal = HASH_MIX(hashVal, HASH_DOTDOTDOT);

		return hashVal;
	}
	else if (type->IsTypeInstance())
	{
		BfTypeInstance* typeInst = (BfTypeInstance*)type;
		int hashVal;
		if (typeInst->mTypeDef != NULL)
			hashVal = typeInst->mTypeDef->mHash;

		if (type->IsClosure())
		{
			auto closureType = (BfClosureType*)type;
			if (closureType->mIsUnique)
				return false;
			hashVal = ((hashVal ^ (int)closureType->mClosureHash.mLow) << 5) - hashVal;
		}
		else if (type->IsTuple())
		{
			hashVal = HASH_VAL_TUPLE;

			BfTypeInstance* tupleType = (BfTypeInstance*)type;
			for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
			{
				BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];

				auto fieldType = fieldInstance->mResolvedType;
				hashVal = HASH_MIX(hashVal, Hash(fieldType, ctx, BfHashFlag_None, hashSeed + 1));

				BfFieldDef* fieldDef = NULL;
                if (tupleType->mTypeDef != NULL)
                    fieldDef = fieldInstance->GetFieldDef();
				int nameHash = 0;
				if (fieldDef == NULL)
				{
					char nameStr[64];
					sprintf(nameStr, "%d", fieldIdx);
					nameHash = (int)Hash64(nameStr, strlen(nameStr));
				}
				else
				{
					nameHash = (int)Hash64(fieldDef->mName.c_str(), (int)fieldDef->mName.length());
				}
				hashVal = HASH_MIX(hashVal, nameHash);
			}
		}
		else if (type->IsGenericTypeInstance())
		{
			BfTypeInstance* genericType = (BfTypeInstance*)type;
			for (auto genericArg : genericType->mGenericTypeInfo->mTypeGenericArguments)
				hashVal = HASH_MIX(hashVal, Hash(genericArg, ctx, BfHashFlag_None, hashSeed + 1));
		}
		return hashVal;
	}
	else if (type->IsPrimitiveType())
	{
		BfPrimitiveType* primType = (BfPrimitiveType*)type;
		return primType->mTypeDef->mHash;
	}
	else if (type->IsPointer())
	{
		BfPointerType* pointerType = (BfPointerType*) type;
		int elemHash = Hash(pointerType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_VAL_PTR;
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsGenericParam())
	{
		auto genericParam = (BfGenericParamType*)type;
		return (((int)genericParam->mGenericParamKind + 0xB00) << 8) ^ (genericParam->mGenericParamIdx + 1);
	}
	else if (type->IsRef())
	{
		auto refType = (BfRefType*)type;
		int elemHash = Hash(refType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ (HASH_VAL_REF + (int)refType->mRefKind);
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsModifiedTypeType())
	{
		auto modifiedTypeType = (BfModifiedTypeType*)type;
		int elemHash = Hash(modifiedTypeType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_MODTYPE + (int)modifiedTypeType->mModifiedKind;
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsConcreteInterfaceType())
	{
		auto concreteInterfaceType = (BfConcreteInterfaceType*)type;
		int elemHash = Hash(concreteInterfaceType->mInterface, ctx, BfHashFlag_None, hashSeed) ^ HASH_CONCRETE_INTERFACE;
		return (elemHash << 5) - elemHash;
	}
	else if (type->IsSizedArray())
	{
		auto sizedArray = (BfSizedArrayType*)type;
		int elemHash = Hash(sizedArray->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_SIZED_ARRAY;
		int hashVal = (elemHash << 5) - elemHash;
		if (type->IsUnknownSizedArrayType())
		{
			auto unknownSizedArray = (BfUnknownSizedArrayType*)type;
			int elemHash = Hash(unknownSizedArray->mElementCountSource, ctx, BfHashFlag_None, hashSeed);
			hashVal = ((hashVal ^ elemHash) << 5) - hashVal;
		}
		else
			hashVal = ((hashVal ^ (int)sizedArray->mElementCount) << 5) - hashVal;

		return hashVal;
	}
	else if (type->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)type;
		if (methodRefType->IsNull())
			return 0;

		return (int)((int)(intptr)(methodRefType->mMethodRef) << 5) ^ (int)(intptr)(methodRefType->mOwner) ^ methodRefType->mOwnerRevision;
	}
	else if (type->IsConstExprValue())
	{
		BfConstExprValueType* constExprValueType = (BfConstExprValueType*)type;
		int hashVal = ((int)constExprValueType->mValue.mTypeCode << 17) ^ (constExprValueType->mValue.mInt32 << 3) ^ HASH_CONSTTYPE;
		hashVal = ((hashVal ^ (Hash(constExprValueType->mType, ctx, BfHashFlag_AllowRef, hashSeed))) << 5) - hashVal;
		return hashVal;
	}
	else
	{
		BF_FATAL("Not handled");
	}
	return 0;
}

int BfResolvedTypeSet::Hash(BfType* type, LookupContext* ctx, bool allowRef, int hashSeed)
{
	int hashVal = DoHash(type, ctx, allowRef, hashSeed);
	if (hashSeed == 0)
		return hashVal;
	return HASH_MIX(hashVal, hashSeed);
}

void BfResolvedTypeSet::HashGenericArguments(BfTypeReference* typeRef, LookupContext* ctx, int& hashVal, int hashSeed)
{
	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		for (int genericIdx = 0; genericIdx < BF_MAX(genericTypeRef->mGenericArguments.mSize, genericTypeRef->mCommas.mSize + 1); genericIdx++)
		{
			bool allowUnboundGeneric = ((ctx->mResolveFlags & BfResolveTypeRefFlag_AllowUnboundGeneric) != 0) && (hashSeed == 0);

			BfAstNode* genericArgTypeRef = NULL;
			if (genericIdx < genericTypeRef->mGenericArguments.mSize)
				genericArgTypeRef = genericTypeRef->mGenericArguments[genericIdx];

			if (allowUnboundGeneric)
			{
				if (BfNodeIsExact<BfWildcardTypeReference>(genericArgTypeRef))
					genericArgTypeRef = NULL;
			}

			int argHashVal = 0;
			if (genericArgTypeRef != NULL)
			{
				argHashVal = Hash(genericArgTypeRef, ctx, BfHashFlag_AllowGenericParamConstValue, hashSeed + 1);
				if ((allowUnboundGeneric) && ((ctx->mResolveFlags & BfResolveTypeRefFlag_ForceUnboundGeneric) != 0))
					genericArgTypeRef = NULL;
			}

			if (genericArgTypeRef == NULL)
			{
				if (allowUnboundGeneric)
				{
					ctx->mIsUnboundGeneric = true;
					argHashVal = (((int)BfGenericParamKind_Type + 0xB00) << 8) ^ (genericIdx + 1);
					argHashVal = HASH_MIX(argHashVal, hashSeed + 1);
				}
				else
				{
					ctx->mModule->Fail("Generic argument expected", genericTypeRef->mOpenChevron);
					ctx->mFailed = true;
					return;
				}
			}

			hashVal = HASH_MIX(hashVal, argHashVal);
		}
	}
}

static int HashNode(BfAstNode* node)
{
	if (node == NULL)
		return (int)Hash64(NULL, 0);
	const char* nameStr = node->GetSourceData()->mSrc + node->GetSrcStart();
	return (int)Hash64(nameStr, node->GetSrcLength());
}

BfResolveTypeRefFlags BfResolvedTypeSet::GetResolveFlags(BfAstNode* typeRef, LookupContext* ctx, BfHashFlags flags)
{
	bool isHeadType = typeRef == ctx->mRootTypeRef;
	BfResolveTypeRefFlags resolveFlags = ctx->mResolveFlags;
	if ((flags & BfHashFlag_AllowGenericParamConstValue) != 0)
		resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_AllowGenericParamConstValue);
	if (!isHeadType)
		resolveFlags = (BfResolveTypeRefFlags)(resolveFlags & ~BfResolveTypeRefFlag_Attribute);
	return resolveFlags;
}

int BfResolvedTypeSet::DirectHash(BfTypeReference* typeRef, LookupContext* ctx, BfHashFlags flags, int hashSeed)
{
	auto resolvedType = ctx->mModule->ResolveTypeRef(typeRef, BfPopulateType_Identity, GetResolveFlags(typeRef, ctx, flags));
	if (resolvedType == NULL)
	{
		ctx->mFailed = true;
		return 0;
	}
	return Hash(resolvedType, ctx, BfHashFlag_None, hashSeed);
}

BfTypeDef* BfResolvedTypeSet::FindRootCommonOuterType(BfTypeDef* outerType, LookupContext* ctx, BfTypeInstance*& outOuterTypeInstance)
{
	if (ctx->mModule->mCurTypeInstance == NULL)
		return NULL;
	BfTypeDef* commonOuterType = ctx->mModule->FindCommonOuterType(ctx->mModule->mCurTypeInstance->mTypeDef, outerType);
	if ((commonOuterType == NULL) && (outerType != NULL))
	{
		auto staticSearch = ctx->mModule->GetStaticSearch();
		if (staticSearch != NULL)
		{
			for (auto staticTypeInst : staticSearch->mStaticTypes)
			{
				auto foundOuterType = ctx->mModule->FindCommonOuterType(staticTypeInst->mTypeDef, outerType);
				if ((foundOuterType != NULL) &&
					((commonOuterType == NULL) || (foundOuterType->mNestDepth > commonOuterType->mNestDepth)))
				{
					commonOuterType = foundOuterType;
					outOuterTypeInstance = staticTypeInst;
				}
			}
		}
	}
	if (outOuterTypeInstance != NULL)
		ctx->mRootOuterTypeInstance = outOuterTypeInstance;
	return commonOuterType;
}

int BfResolvedTypeSet::DoHash(BfTypeReference* typeRef, LookupContext* ctx, BfHashFlags flags, int& hashSeed)
{
	if ((typeRef == ctx->mRootTypeRef) && (ctx->mRootTypeDef != NULL) &&
		((typeRef->IsNamedTypeReference()) || (BfNodeIsA<BfDirectTypeDefReference>(typeRef))))
	{
		BfTypeDef* typeDef = ctx->mRootTypeDef;

		int hashVal = typeDef->mHash;

		if (typeDef->mGenericParamDefs.size() != 0)
		{
			BfTypeDef* commonOuterType = NULL;

			auto checkTypeInstance = ctx->mModule->mCurTypeInstance;
			if (checkTypeInstance != NULL)
			{
				if (checkTypeInstance->IsBoxed())
					checkTypeInstance = checkTypeInstance->GetUnderlyingType()->ToTypeInstance();

				auto outerType = ctx->mModule->mSystem->GetOuterTypeNonPartial(typeDef);

				if (typeRef == ctx->mRootTypeRef)
					commonOuterType = FindRootCommonOuterType(outerType, ctx, checkTypeInstance);
				else
					commonOuterType = ctx->mModule->FindCommonOuterType(ctx->mModule->mCurTypeInstance->mTypeDef, outerType);

				if ((commonOuterType == NULL) && (outerType != NULL))
				{
					auto staticSearch = ctx->mModule->GetStaticSearch();
					if (staticSearch != NULL)
					{
						for (auto staticTypeInst : staticSearch->mStaticTypes)
						{
							auto foundOuterType = ctx->mModule->FindCommonOuterType(staticTypeInst->mTypeDef, outerType);
							if ((foundOuterType != NULL) &&
								((commonOuterType == NULL) || (foundOuterType->mNestDepth > commonOuterType->mNestDepth)))
							{
								commonOuterType = foundOuterType;
								checkTypeInstance = staticTypeInst;
							}
						}
					}
				}
			}

			if ((commonOuterType == NULL) || (commonOuterType->mGenericParamDefs.size() == 0))
			{
				ctx->mModule->Fail("Generic arguments expected", typeRef);
				ctx->mFailed = true;
				return 0;
			}

			BF_ASSERT(checkTypeInstance->IsGenericTypeInstance());
			auto curGenericTypeInst = (BfTypeInstance*)checkTypeInstance;
			int numParentGenericParams = (int)commonOuterType->mGenericParamDefs.size();
			for (int i = 0; i < numParentGenericParams; i++)
			{
				hashVal = HASH_MIX(hashVal, Hash(curGenericTypeInst->mGenericTypeInfo->mTypeGenericArguments[i], ctx, BfHashFlag_None, hashSeed + 1));
			}
		}

		return hashVal;
	}

	if (typeRef->IsNamedTypeReference())
	{
		int hashVal = DirectHash(typeRef, ctx, flags, hashSeed);
		hashSeed = 0;
		return hashVal;
	}
	if (auto genericInstTypeRef = BfNodeDynCastExact<BfGenericInstanceTypeRef>(typeRef))
	{
		//BfType* type = NULL;
		BfTypeDef* elementTypeDef = ctx->mModule->ResolveGenericInstanceDef(genericInstTypeRef, NULL, GetResolveFlags(genericInstTypeRef, ctx, flags));

		if (elementTypeDef == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}

		int typeAliasHash = 0;

		bool isInnerTypeAlias = false;

		// Don't translate aliases for the root type, just element types
		if (ctx->mRootTypeRef == typeRef)
		{
			BF_ASSERT((ctx->mRootTypeDef == NULL) || (ctx->mRootTypeDef == elementTypeDef));
			ctx->mRootTypeDef = elementTypeDef;
		}
		else if (elementTypeDef->mTypeCode == BfTypeCode_TypeAlias)
		{
			isInnerTypeAlias = true;
		}

		BfTypeVector typeAliasGenericArgs;

		bool fullyQualified = false;
		int hashVal = elementTypeDef->mHash;

		BfTypeInstance* outerType = NULL;

		int checkIdx = 0;
		auto checkTypeRef = genericInstTypeRef->mElementType;
		while (checkTypeRef != NULL)
		{
			checkIdx++;
			if (checkIdx >= 2)
			{
				fullyQualified = true;
				if ((elementTypeDef->mOuterType != NULL) && (!elementTypeDef->mOuterType->mGenericParamDefs.IsEmpty()))
				{
					auto resolvedType = ctx->mModule->ResolveTypeRef(checkTypeRef, BfPopulateType_Identity,
						(BfResolveTypeRefFlags)(GetResolveFlags(checkTypeRef, ctx, flags) | BfResolveTypeRefFlag_IgnoreLookupError));
					if (resolvedType == NULL)
					{
						ctx->mFailed = true;
						return hashVal;
					}
					ctx->SetCachedResolvedType(checkTypeRef, resolvedType);
					outerType = resolvedType->ToTypeInstance();
				}
				break;
			}

			if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(checkTypeRef))
			{
				checkTypeRef = elementedTypeRef->mElementType;
				continue;
			}

			if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkTypeRef))
			{
				checkTypeRef = qualifiedTypeRef->mLeft;
				continue;
			}
			break;
		}

		if (fullyQualified)
		{
			if (outerType != NULL)
			{
				for (auto genericArg : outerType->mGenericTypeInfo->mTypeGenericArguments)
				{
					if (isInnerTypeAlias)
						typeAliasGenericArgs.Add(genericArg);
					else
						hashVal = HASH_MIX(hashVal, Hash(genericArg, ctx, Beefy::BfResolvedTypeSet::BfHashFlag_None, hashSeed + 1));
				}
			}
		}
		else
		{
			// Do we need to add generic arguments from an in-context outer class?
			if ((elementTypeDef->mOuterType != NULL) && (ctx->mModule->mCurTypeInstance != NULL))
			{
				BfTypeInstance* checkTypeInstance = ctx->mModule->mCurTypeInstance;
				BfTypeDef* commonOuterType;
				if (typeRef == ctx->mRootTypeRef)
					commonOuterType = FindRootCommonOuterType(elementTypeDef->mOuterType, ctx, checkTypeInstance);
				else
					commonOuterType = ctx->mModule->FindCommonOuterType(ctx->mModule->mCurTypeInstance->mTypeDef, elementTypeDef->mOuterType);

				if ((commonOuterType != NULL) && (checkTypeInstance->IsGenericTypeInstance()))
				{
					auto parentTypeInstance = checkTypeInstance;
					int numParentGenericParams = (int)commonOuterType->mGenericParamDefs.size();
					for (int i = 0; i < numParentGenericParams; i++)
					{
						if (isInnerTypeAlias)
							typeAliasGenericArgs.Add(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
						else
							hashVal = HASH_MIX(hashVal, Hash(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i], ctx, Beefy::BfResolvedTypeSet::BfHashFlag_None, hashSeed + 1));
					}
				}
			}
		}

		if (isInnerTypeAlias)
		{
			for (auto genericArgTypeRef : genericInstTypeRef->mGenericArguments)
			{
				auto argType = ctx->mModule->ResolveTypeRef(genericArgTypeRef, NULL, BfPopulateType_Identity, GetResolveFlags(genericArgTypeRef, ctx, flags));
				if (argType != NULL)
					typeAliasGenericArgs.Add(argType);
				else
					ctx->mFailed = true;
			}

			if (!ctx->mFailed)
			{
				auto resolvedType = ctx->mModule->ResolveTypeDef(elementTypeDef, typeAliasGenericArgs);
				if ((resolvedType != NULL) && (resolvedType->IsTypeAlias()))
				{
					auto underlyingType = resolvedType->GetUnderlyingType();
					if (underlyingType == NULL)
					{
						ctx->mFailed = true;
						return 0;
					}
					int hashVal = Hash(underlyingType, ctx, flags, hashSeed);
					hashSeed = 0;
					return hashVal;
				}
			}
		}
		else
			HashGenericArguments(genericInstTypeRef, ctx, hashVal, hashSeed);

		return hashVal;
	}
	else if (auto tupleTypeRef = BfNodeDynCastExact<BfTupleTypeRef>(typeRef))
	{
		int hashVal = HASH_VAL_TUPLE;

		for (int fieldIdx = 0; fieldIdx < (int)tupleTypeRef->mFieldTypes.size(); fieldIdx++)
		{
			BfTypeReference* fieldType = tupleTypeRef->mFieldTypes[fieldIdx];
			hashVal = HASH_MIX(hashVal, Hash(fieldType, ctx, BfHashFlag_None, hashSeed + 1));

			int nameHash = 0;
			BfIdentifierNode* fieldName = NULL;
			if (fieldIdx < (int)tupleTypeRef->mFieldNames.size())
				fieldName = tupleTypeRef->mFieldNames[fieldIdx];
			if (fieldName != NULL)
			{
				const char* nameStr = fieldName->GetSourceData()->mSrc + fieldName->GetSrcStart();
				nameHash = (int)Hash64(nameStr, fieldName->GetSrcLength());
			}
			else
			{
				char nameStr[64];
				sprintf(nameStr, "%d", fieldIdx);
				nameHash = (int)Hash64(nameStr, strlen(nameStr));
			}
			hashVal = HASH_MIX(hashVal, nameHash);
		}

		return hashVal;
	}
	else if (auto arrayType = BfNodeDynCastExact<BfArrayTypeRef>(typeRef))
	{
		if ((arrayType->mDimensions == 1) && (arrayType->mParams.size() != 0))
		{
			int rawElemHash = Hash(arrayType->mElementType, ctx, BfHashFlag_None, hashSeed);
			int elemHash = rawElemHash ^ HASH_SIZED_ARRAY;
			int hashVal = (elemHash << 5) - elemHash;

			// Sized array
			if (arrayType->mParams.size() != 1)
			{
				ctx->mFailed = true;
				ctx->mModule->Fail("Only one size parameter expected", arrayType->mParams[1]);
				return 0;
			}

			intptr elementCount = -1;
			BfExpression* sizeExpr = BfNodeDynCast<BfExpression>(arrayType->mParams[0]);
			BF_ASSERT(sizeExpr != NULL);
			if (sizeExpr != NULL)
			{
				BfMethodState methodState;
				methodState.mTempKind = BfMethodState::TempKind_Static;
				SetAndRestoreValue<BfMethodState*> prevMethodState;
				if (ctx->mModule->mCurMethodState == NULL)
					prevMethodState.Init(ctx->mModule->mCurMethodState, &methodState);

				BfConstResolver constResolver(ctx->mModule);
				BfType* intType = ctx->mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				constResolver.mBfEvalExprFlags = (BfEvalExprFlags)(constResolver.mBfEvalExprFlags | BfEvalExprFlags_AllowGenericConstValue);
				constResolver.mExpectingType = intType;
				BfTypedValue typedVal = constResolver.Resolve(sizeExpr, NULL, BfConstResolveFlag_ArrayInitSize);

				if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
				{
					ctx->mResolvedValueMap[sizeExpr] = typedVal;
					int elemHash = Hash(typedVal.mType, ctx, BfHashFlag_None, hashSeed);
					hashVal = ((hashVal ^ elemHash) << 5) - hashVal;
					return hashVal;
				}
				if (!typedVal)
					ctx->mFailed = true;
				if (typedVal)
				{
					SetAndRestoreValue<bool> prevIgnoreWrites(ctx->mModule->mBfIRBuilder->mIgnoreWrites, true);
					typedVal = ctx->mModule->Cast(sizeExpr, typedVal, intType);
				}
				ctx->mResolvedValueMap[sizeExpr] = typedVal;

				if (typedVal)
				{
					auto constant = ctx->mModule->mBfIRBuilder->GetConstant(typedVal.mValue);
					if (constant == NULL)
					{
						ctx->mFailed = true;
						ctx->mModule->Fail("Array size not a constant value", arrayType->mParams[0]);
					}
					else if (constant->mConstType == BfConstType_Undef)
					{
						elementCount = -1; // Marker for undef
						if ((arrayType->IsInferredSize()) && ((ctx->mResolveFlags & BfResolveTypeRefFlag_AllowInferredSizedArray) == 0))
						{
							ctx->mModule->Fail("Invalid use of inferred-sized array", sizeExpr);
						}
					}
					else if (!BfIRBuilder::IsInt(constant->mTypeCode))
					{
						ctx->mFailed = true;
						ctx->mModule->Fail("Array size not a constant value", arrayType->mParams[0]);
					}
					else
					{
						elementCount = (intptr)constant->mInt64;
						if (elementCount < 0)
						{
							ctx->mFailed = true;
							ctx->mModule->Fail("Arrays cannot have negative sizes", arrayType->mParams[0]);
							return 0;
						}
					}
				}
			}

			hashVal = ((hashVal ^ (int)elementCount) << 5) - hashVal;
			return hashVal;
		}
		else
		{
			if (arrayType->mDimensions != (int)arrayType->mParams.size() + 1)
			{
				for (auto arg : arrayType->mParams)
				{
					if (auto tokenNode = BfNodeDynCastExact<BfTokenNode>(arg))
					{
						if (tokenNode->GetToken() == BfToken_Comma)
							continue;
					}

					ctx->mFailed = true;
					ctx->mModule->Fail("Multidimensional arrays cannot have explicit sizes. Consider using a strided array (ie: int[2][2]) instead.", arg);
					return 0;
				}
			}

			int elemHash = Hash(arrayType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ (arrayType->mDimensions << 8);
			return (elemHash << 5) - elemHash;
		}
	}
	else if (auto pointerType = BfNodeDynCastExact<BfPointerTypeRef>(typeRef))
	{
		int elemHash = Hash(pointerType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_VAL_PTR;
		return (elemHash << 5) - elemHash;
	}
	else if (auto nullableType = BfNodeDynCastExact<BfNullableTypeRef>(typeRef))
	{
		if (ctx->mRootTypeRef == typeRef)
			ctx->mRootTypeDef = ctx->mModule->mCompiler->mNullableTypeDef;

		int hashVal = ctx->mModule->mCompiler->mNullableTypeDef->mHash;
		hashVal = HASH_MIX(hashVal, Hash(nullableType->mElementType, ctx, BfHashFlag_None, hashSeed + 1));
		return hashVal;
	}
	else if (auto refType = BfNodeDynCastExact<BfRefTypeRef>(typeRef))
	{
		if ((flags & BfHashFlag_AllowRef) != 0)
		{
			auto refKind = BfRefType::RefKind_Ref;
			if (refType->mRefToken == NULL)
				refKind = BfRefType::RefKind_Ref;
			else if (refType->mRefToken->GetToken() == BfToken_In)
				refKind = BfRefType::RefKind_In;
			else if (refType->mRefToken->GetToken() == BfToken_Out)
				refKind = BfRefType::RefKind_Out;
			else if (refType->mRefToken->GetToken() == BfToken_Mut)
				refKind = BfRefType::RefKind_Mut;

			int elemHash = Hash(refType->mElementType, ctx, BfHashFlag_None, hashSeed) ^ (HASH_VAL_REF + (int)refKind);
			return (elemHash << 5) - elemHash;
		}
		else
		{
			ctx->mModule->ResolveTypeRef(typeRef, BfPopulateType_Identity, GetResolveFlags(typeRef, ctx, flags)); // To throw an error...
			ctx->mFailed = true;
			return 0;
			//return Hash(refType->mElementType, ctx);
		}
	}
	else if (auto genericParamTypeRef = BfNodeDynCastExact<BfGenericParamTypeRef>(typeRef))
	{
		return (((int)genericParamTypeRef->mGenericParamKind) << 8) ^ (genericParamTypeRef->mGenericParamIdx + 1);
	}
	else if (auto qualifiedTypeRef = BfNodeDynCastExact<BfQualifiedTypeReference>(typeRef))
	{
		/*auto leftType = ctx->mModule->ResolveTypeRef(qualifiedTypeRef->mLeft, BfPopulateType_Identity);
		if (leftType == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}

		if (qualifiedTypeRef->mRight == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}

		auto rightType = ctx->mModule->ResolveInnerType(leftType, qualifiedTypeRef->mRight);
		if (rightType == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}
		return Hash(rightType, ctx);*/

		auto resolvedType = ctx->mModule->ResolveTypeRef(typeRef, BfPopulateType_Identity, GetResolveFlags(typeRef, ctx, flags));
		if (resolvedType == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}
		int hashVal = Hash(resolvedType, ctx, BfHashFlag_None, hashSeed);
		hashSeed = 0;
		return hashVal;
	}
	else if (auto varType = BfNodeDynCastExact<BfVarTypeReference>(typeRef))
	{
		// Don't allow 'var'
		ctx->mModule->Fail("Invalid use of 'var'", typeRef);
		ctx->mFailed = true;
		return 0;
	}
	else if (auto letType = BfNodeDynCastExact<BfLetTypeReference>(typeRef))
	{
		// Don't allow 'let'
		ctx->mModule->Fail("Invalid use of 'let'", typeRef);
		ctx->mFailed = true;
		return 0;
	}
	else if (auto retTypeTypeRef = BfNodeDynCastExact<BfModifiedTypeRef>(typeRef))
	{
		// Don't cause infinite loop, but if we have an inner 'rettype' then try to directly resolve that --
		//  Only use the HAS_RETTYPE for root-level rettype insertions
		if (ctx->mRootTypeRef != retTypeTypeRef)
		{
			auto type = ctx->mModule->ResolveTypeRef(retTypeTypeRef, BfPopulateType_Identity, GetResolveFlags(retTypeTypeRef, ctx, flags));
			if (type == NULL)
			{
				ctx->mFailed = true;
				return 0;
			}
			if (type->IsRef())
				type = type->GetUnderlyingType();
			return Hash(type, ctx, flags, hashSeed);
		}

		int elemHash = Hash(retTypeTypeRef->mElementType, ctx, BfHashFlag_None, hashSeed) ^ HASH_MODTYPE + retTypeTypeRef->mRetTypeToken->mToken;
		return (elemHash << 5) - elemHash;
	}
	else if (auto resolvedTypeRef = BfNodeDynCastExact<BfResolvedTypeReference>(typeRef))
	{
		return Hash(resolvedTypeRef->mType, ctx, BfHashFlag_None, hashSeed);
	}
	else if (auto constTypeRef = BfNodeDynCastExact<BfConstTypeRef>(typeRef))
	{
		// We purposely don't mix in a HASH_CONSTTYPE because there's no such thing as a const type in Beef, so we just strip it
		return Hash(constTypeRef->mElementType, ctx, flags, hashSeed);
	}
	else if (auto delegateTypeRef = BfNodeDynCastExact<BfDelegateTypeRef>(typeRef))
	{
		int hashVal = HASH_DELEGATE;
		if (delegateTypeRef->mReturnType != NULL)
			hashVal = HASH_MIX(hashVal, Hash(delegateTypeRef->mReturnType, ctx, BfHashFlag_AllowRef, hashSeed + 1));
		else
			ctx->mFailed = true;

		bool isFirstParam = true;

		for (int paramIdx = 0; paramIdx < delegateTypeRef->mParams.size(); paramIdx++)
		{
			auto param = delegateTypeRef->mParams[paramIdx];
			// Parse attributes?
			BfTypeReference* fieldType = param->mTypeRef;

			if (isFirstParam)
			{
				if ((param->mNameNode != NULL) && (param->mNameNode->Equals("this")))
				{
					if (auto refNode = BfNodeDynCast<BfRefTypeRef>(fieldType))
						fieldType = refNode->mElementType;
				}
			}

			if (paramIdx == delegateTypeRef->mParams.size() - 1)
			{
				if (auto dotTypeRef = BfNodeDynCastExact<BfDotTypeReference>(fieldType))
				{
					if (dotTypeRef->mDotToken->mToken == BfToken_DotDotDot)
					{
						hashVal = HASH_MIX(hashVal, HASH_DOTDOTDOT);
						continue;
					}
				}
			}

			if (fieldType != NULL)
				hashVal = HASH_MIX(hashVal, Hash(fieldType, ctx, (BfHashFlags)(BfHashFlag_AllowRef), hashSeed + 1));
			hashVal = HASH_MIX(hashVal, HashNode(param->mNameNode));
			isFirstParam = true;
		}

		return hashVal;
	}
	else if (auto exprModTypeRef = BfNodeDynCastExact<BfExprModTypeRef>(typeRef))
	{
		auto cachedResolvedType = ctx->GetCachedResolvedType(typeRef);
		if (cachedResolvedType == NULL)
		{
			if (exprModTypeRef->mTarget != NULL)
			{
				BfTypedValue result;
				//
				{
					BfMethodState methodState;
					SetAndRestoreValue<BfMethodState*> prevMethodState(ctx->mModule->mCurMethodState, &methodState, false);
					if (ctx->mModule->mCurMethodState == NULL)
						prevMethodState.Set();
					methodState.mTempKind = BfMethodState::TempKind_NonStatic;

					SetAndRestoreValue<bool> ignoreWrites(ctx->mModule->mBfIRBuilder->mIgnoreWrites, true);
					SetAndRestoreValue<bool> allowUninitReads(ctx->mModule->mCurMethodState->mAllowUinitReads, true);

					BfEvalExprFlags exprFlags = BfEvalExprFlags_None;
					if ((ctx->mResolveFlags & BfResolveTypeRefFlag_DisallowComptime) != 0)
					{
						exprFlags = (BfEvalExprFlags)(exprFlags | BfEvalExprFlags_DisallowComptime);
					}

					if (exprModTypeRef->mToken->mToken == BfToken_Comptype)
					{
						auto typeType = ctx->mModule->ResolveTypeDef(ctx->mModule->mCompiler->mTypeTypeDef);
						exprFlags = (BfEvalExprFlags)(exprFlags | BfEvalExprFlags_Comptime | BfEvalExprFlags_NoCast);
						result = ctx->mModule->CreateValueFromExpression(exprModTypeRef->mTarget, typeType, exprFlags);
						if ((result.mType != NULL) && (!result.mType->IsInteger()) && (result.mType != typeType) &&
							(!result.mType->IsInstanceOf(ctx->mModule->mCompiler->mReflectTypeIdTypeDef)))
							result = ctx->mModule->Cast(exprModTypeRef->mTarget, result, typeType);
					}
					else
					{
						result = ctx->mModule->CreateValueFromExpression(exprModTypeRef->mTarget, NULL, BfEvalExprFlags_DeclType);
					}
				}

				if ((result) && (exprModTypeRef->mToken->mToken == BfToken_Comptype))
				{
					auto constant = ctx->mModule->mBfIRBuilder->GetConstant(result.mValue);
					if (constant != NULL)
					{
						if ((constant->mConstType == BfConstType_TypeOf) || (constant->mConstType == BfConstType_TypeOf_WithData))
						{
							auto typeOf = (BfTypeOf_Const*)constant;
							cachedResolvedType = typeOf->mType;
						}
						else if (constant->mConstType == BfConstType_Undef)
						{
							ctx->mHadVar = true;
							cachedResolvedType = ctx->mModule->GetPrimitiveType(BfTypeCode_Var);

							auto typeState = ctx->mModule->mContext->mCurTypeState;
							if ((typeState != NULL) && (typeState->mType != NULL) && (typeState->mType->IsTypeInstance()))
							{
								auto typeInst = typeState->mType->ToTypeInstance();
								if (typeInst->mDefineState == BfTypeDefineState_ResolvingBaseType)
								{
									// Make sure we regenerate this type
									ctx->mModule->mContext->mFailTypes.TryAdd(typeState->mType->ToTypeInstance(), BfFailKind_Deep);
								}
							}
						}
						else if (BfIRConstHolder::IsInt(constant->mTypeCode))
						{
							int typeId = constant->mInt32;
							BfType* type = NULL;
							if ((typeId >= 0) && (typeId < ctx->mModule->mContext->mTypes.mSize))
								type = ctx->mModule->mContext->mTypes[typeId];

							if (type != NULL)
							{
								cachedResolvedType = type;
							}
							else
							{
								ctx->mModule->Fail(StrFormat("Invalid type id '%d'", typeId), exprModTypeRef->mTarget);
								ctx->mHadVar = true;
								cachedResolvedType = ctx->mModule->GetPrimitiveType(BfTypeCode_Var);
							}
						}
					}

					if (cachedResolvedType == NULL)
						ctx->mModule->Fail("Constant System.Type value required", exprModTypeRef->mTarget);
				}
				else
					cachedResolvedType = result.mType;

				if ((cachedResolvedType != NULL) && (cachedResolvedType->IsRef()))
					cachedResolvedType = cachedResolvedType->GetUnderlyingType();

				if (cachedResolvedType != NULL)
					ctx->SetCachedResolvedType(typeRef, cachedResolvedType);
			}
		}

		if (cachedResolvedType == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}

		int hashVal = Hash(cachedResolvedType, ctx, flags, hashSeed);
		hashSeed = 0;
		return hashVal;
	}
	else if (auto constExprTypeRef = BfNodeDynCastExact<BfConstExprTypeRef>(typeRef))
	{
		if ((flags & BfHashFlag_AllowGenericParamConstValue) == 0)
		{
			ctx->mModule->Fail("Invalid use of const expression", constExprTypeRef->mConstToken);
			ctx->mFailed = true;
			return 0;
		}

		BfVariant result;
		BfType* resultType = NULL;
		if (constExprTypeRef->mConstExpr != NULL)
		{
			result = EvaluateToVariant(ctx, constExprTypeRef->mConstExpr, resultType);
			if ((resultType != NULL) && (resultType->IsGenericParam()))
			{
				int hashVal = Hash(resultType, ctx, BfHashFlag_None, hashSeed);
				hashSeed = 0;
				return hashVal;
			}
		}

		if (resultType == NULL)
		{
			ctx->mFailed = true;
			return 0;
		}

		auto hashVal = ((int)result.mTypeCode << 17) ^ (result.mInt32 << 3) ^ HASH_CONSTTYPE;
		hashVal = ((hashVal ^ (Hash(resultType, ctx, BfHashFlag_AllowRef, hashSeed))) << 5) - hashVal;
		return hashVal;
	}
	else if (auto dotTypeRef = BfNodeDynCastExact<BfDotTypeReference>(typeRef))
	{
		ctx->mModule->ResolveTypeRef(dotTypeRef, BfPopulateType_Identity, GetResolveFlags(dotTypeRef, ctx, flags));
		ctx->mFailed = true;
		return 0;
	}
	else if (auto wildcardTypeRef = BfNodeDynCastExact<BfWildcardTypeReference>(typeRef))
	{
		ctx->mFailed = true;
		return 0;
	}
	else
	{
		BF_FATAL("Not handled");
	}
	return 0;
}

int BfResolvedTypeSet::Hash(BfTypeReference* typeRef, LookupContext* ctx, BfHashFlags flags, int hashSeed)
{
	int hashVal = DoHash(typeRef, ctx, flags, hashSeed);
	if (hashSeed == 0)
		return hashVal;
	return HASH_MIX(hashVal, hashSeed);
}

int BfResolvedTypeSet::Hash(BfAstNode* typeRefNode, LookupContext* ctx, BfHashFlags flags, int hashSeed)
{
	if (auto typeRef = BfNodeDynCast<BfTypeReference>(typeRefNode))
		return Hash(typeRef, ctx, flags, hashSeed);

	auto result = ctx->mModule->ResolveTypeRef(typeRefNode, NULL, BfPopulateType_Identity, (BfResolveTypeRefFlags)(ctx->mResolveFlags | BfResolveTypeRefFlag_AllowImplicitConstExpr));
	if (result == NULL)
	{
		ctx->mFailed = true;
		return 0;
	}

	ctx->mResolvedTypeMap[typeRefNode] = result;
	return Hash(result, ctx, false, hashSeed);
}

bool BfResolvedTypeSet::Equals(BfType* lhs, BfType* rhs, LookupContext* ctx)
{
	if (lhs->IsBoxed())
	{
		if (!rhs->IsBoxed())
			return false;
		BfBoxedType* lhsBoxedType = (BfBoxedType*)lhs;
		BfBoxedType* rhsBoxedType = (BfBoxedType*)rhs;
		if (lhsBoxedType->mBoxedFlags != rhsBoxedType->mBoxedFlags)
			return false;
		return lhsBoxedType->mElementType == rhsBoxedType->mElementType;
	}
	else if (lhs->IsArray())
	{
		if (!rhs->IsArray())
			return false;
		BfArrayType* lhsArrayType = (BfArrayType*) lhs;
		BfArrayType* rhsArrayType = (BfArrayType*) rhs;
		if (lhsArrayType->mDimensions != rhsArrayType->mDimensions)
			return false;
		return lhsArrayType->mGenericTypeInfo->mTypeGenericArguments[0] == rhsArrayType->mGenericTypeInfo->mTypeGenericArguments[0];
	}
	else if (lhs->IsTypeInstance())
	{
		if ((!rhs->IsTypeInstance()) || (rhs->IsBoxed()))
			return false;
		BfTypeInstance* lhsInst = (BfTypeInstance*)lhs;
		BfTypeInstance* rhsInst = (BfTypeInstance*)rhs;

		if (lhs->IsClosure())
		{
			if (!rhs->IsClosure())
				return false;
			auto lhsClosure = (BfClosureType*)lhs;
			auto rhsClosure = (BfClosureType*)rhs;
			if ((lhsClosure->mIsUnique) || (rhsClosure->mIsUnique))
				return false;
			if (lhsClosure->mBaseType != rhsClosure->mBaseType)
				return false;
			return lhsClosure->mClosureHash == rhsClosure->mClosureHash;
		}

		if (lhs->IsDelegateFromTypeRef() || lhs->IsFunctionFromTypeRef())
		{
			if (!rhs->IsDelegateFromTypeRef() && !rhs->IsFunctionFromTypeRef())
				return false;
			if (lhs->IsDelegate() != rhs->IsDelegate())
				return false;
			BfDelegateInfo* lhsDelegateInfo = lhs->GetDelegateInfo();
			BfDelegateInfo* rhsDelegateInfo = rhs->GetDelegateInfo();
			if (lhsInst->mTypeDef->mIsDelegate != rhsInst->mTypeDef->mIsDelegate)
				return false;
			if (lhsDelegateInfo->mCallingConvention != rhsDelegateInfo->mCallingConvention)
				return false;

			auto lhsMethodDef = lhsInst->mTypeDef->mMethods[0];
			auto rhsMethodDef = rhsInst->mTypeDef->mMethods[0];

			if (lhsMethodDef->mCallingConvention != rhsMethodDef->mCallingConvention)
				return false;
			if (lhsMethodDef->mIsMutating != rhsMethodDef->mIsMutating)
				return false;
			if (lhsDelegateInfo->mReturnType != rhsDelegateInfo->mReturnType)
				return false;
			if (lhsDelegateInfo->mParams.size() != rhsDelegateInfo->mParams.size())
				return false;
			for (int paramIdx = 0; paramIdx < lhsDelegateInfo->mParams.size(); paramIdx++)
			{
				if (lhsDelegateInfo->mParams[paramIdx] != rhsDelegateInfo->mParams[paramIdx])
					return false;
				if (lhsMethodDef->mParams[paramIdx]->mName != rhsMethodDef->mParams[paramIdx]->mName)
					return false;
			}
			return true;
		}

		if (lhs->IsTuple())
		{
			if (!rhs->IsTuple())
				return false;

			BfTypeInstance* lhsTupleType = (BfTypeInstance*)lhs;
			BfTypeInstance* rhsTupleType = (BfTypeInstance*)rhs;
			if (lhsTupleType->mFieldInstances.size() != rhsTupleType->mFieldInstances.size())
				return false;
			for (int fieldIdx = 0; fieldIdx < (int)lhsTupleType->mFieldInstances.size(); fieldIdx++)
			{
				auto lhsFieldInstance = &lhsTupleType->mFieldInstances[fieldIdx];
				auto rhsFieldInstance = &rhsTupleType->mFieldInstances[fieldIdx];

				if (lhsFieldInstance->mResolvedType != rhsFieldInstance->mResolvedType)
					return false;

				auto lhsFieldDef = lhsFieldInstance->GetFieldDef();
				if (rhsTupleType->mTypeDef == NULL)
				{
					char c = lhsFieldDef->mName[0];
					if ((c < '0') || (c > '9'))
						return false;
				}
				else
				{
					auto rhsFieldDef = rhsFieldInstance->GetFieldDef();
					if (lhsFieldDef->mName != rhsFieldDef->mName)
						return false;
				}
			}
			return true;
		}

		if (lhs->IsGenericTypeInstance())
		{
			if (!rhs->IsGenericTypeInstance())
				return false;
			BfTypeInstance* lhsGenericType = (BfTypeInstance*)lhs;
			BfTypeInstance* rhsGenericType = (BfTypeInstance*)rhs;
			if (lhsGenericType->mGenericTypeInfo->mTypeGenericArguments.size() != rhsGenericType->mGenericTypeInfo->mTypeGenericArguments.size())
				return false;
			if (lhsGenericType->mTypeDef->GetDefinition() != rhsGenericType->mTypeDef->GetDefinition())
				return false;
			for (int i = 0; i < (int)lhsGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); i++)
			{
				if (lhsGenericType->mGenericTypeInfo->mTypeGenericArguments[i] != rhsGenericType->mGenericTypeInfo->mTypeGenericArguments[i])
					return false;
			}
		}

		return lhsInst->mTypeDef->GetDefinition() == rhsInst->mTypeDef->GetDefinition();
	}
	else if (lhs->IsPrimitiveType())
	{
		if (!rhs->IsPrimitiveType())
			return false;
		BfPrimitiveType* lhsPrimType = (BfPrimitiveType*)lhs;
		BfPrimitiveType* rhsPrimType = (BfPrimitiveType*)rhs;
		return lhsPrimType->mTypeDef == rhsPrimType->mTypeDef;
	}
	else if (lhs->IsPointer())
	{
		if (!rhs->IsPointer())
			return false;
		BfPointerType* lhsPtrType = (BfPointerType*)lhs;
		BfPointerType* rhsPtrType = (BfPointerType*)rhs;
		return lhsPtrType->mElementType == rhsPtrType->mElementType;
	}
	else if (lhs->IsGenericParam())
	{
		if (!rhs->IsGenericParam())
			return false;
		BfGenericParamType* lhsGenericParamType = (BfGenericParamType*)lhs;
		BfGenericParamType* rhsGenericParamType = (BfGenericParamType*)rhs;
		return (lhsGenericParamType->mGenericParamKind == rhsGenericParamType->mGenericParamKind) &&
			(lhsGenericParamType->mGenericParamIdx == rhsGenericParamType->mGenericParamIdx);
	}
	else if (lhs->IsRef())
	{
		if (!rhs->IsRef())
			return false;
		BfRefType* lhsRefType = (BfRefType*)lhs;
		BfRefType* rhsRefType = (BfRefType*)rhs;
		return (lhsRefType->mElementType == rhsRefType->mElementType) && (lhsRefType->mRefKind == rhsRefType->mRefKind);
	}
	else if (lhs->IsModifiedTypeType())
	{
		if (!rhs->IsModifiedTypeType())
			return false;
		BfModifiedTypeType* lhsRetTypeType = (BfModifiedTypeType*)lhs;
		BfModifiedTypeType* rhsRetTypeType = (BfModifiedTypeType*)rhs;
		return (lhsRetTypeType->mModifiedKind == rhsRetTypeType->mModifiedKind) &&
			(lhsRetTypeType->mElementType == rhsRetTypeType->mElementType);
	}
	else if (lhs->IsConcreteInterfaceType())
	{
		if (!rhs->IsConcreteInterfaceType())
			return false;
		BfConcreteInterfaceType* lhsConcreteInterfaceType = (BfConcreteInterfaceType*)lhs;
		BfConcreteInterfaceType* rhsConcreteInterfaceType = (BfConcreteInterfaceType*)rhs;
		return (lhsConcreteInterfaceType->mInterface == rhsConcreteInterfaceType->mInterface);
	}
	else if (lhs->IsSizedArray())
	{
		if (!rhs->IsSizedArray())
			return false;
		BfSizedArrayType* lhsSizedArrayType = (BfSizedArrayType*)lhs;
		BfSizedArrayType* rhsSizedArrayType = (BfSizedArrayType*)rhs;
		return (lhsSizedArrayType->mElementType == rhsSizedArrayType->mElementType) &&
			(lhsSizedArrayType->mElementCount == rhsSizedArrayType->mElementCount);
	}
	else if (lhs->IsMethodRef())
	{
		if (!rhs->IsMethodRef())
			return false;
		BfMethodRefType* lhsMethodRefType = (BfMethodRefType*)lhs;
		BfMethodRefType* rhsMethodRefType = (BfMethodRefType*)rhs;
		return (lhsMethodRefType->mMethodRef == rhsMethodRefType->mMethodRef) &&
			(lhsMethodRefType->mOwner == rhsMethodRefType->mOwner) &&
			(lhsMethodRefType->mOwnerRevision == rhsMethodRefType->mOwnerRevision);
	}
	else if ((lhs->IsConstExprValue()) || (rhs->IsConstExprValue()))
	{
		if (!lhs->IsConstExprValue() || !rhs->IsConstExprValue())
			return false;

		BfConstExprValueType* lhsConstExprValueType = (BfConstExprValueType*)lhs;
		BfConstExprValueType* rhsConstExprValueType = (BfConstExprValueType*)rhs;

		return (lhsConstExprValueType->mType == rhsConstExprValueType->mType) &&
			(lhsConstExprValueType->mValue.mInt64 == rhsConstExprValueType->mValue.mInt64);
	}
	else
	{
		BF_FATAL("Not handled");
	}
	return 0;
}

bool BfResolvedTypeSet::GenericTypeEquals(BfTypeInstance* lhsGenericType, BfTypeVector* lhsTypeGenericArguments, BfTypeReference* rhs, LookupContext* ctx, int& genericParamOffset, bool skipElement)
{
	//BP_ZONE("BfResolvedTypeSet::GenericTypeEquals");

	if (!skipElement)
	{
		if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(rhs))
		{
			if (!GenericTypeEquals(lhsGenericType, lhsTypeGenericArguments, elementedTypeRef->mElementType, ctx, genericParamOffset))
				return false;
			//_GetTypeRefs(elementedTypeRef->mElementType);
		}
		else if (auto qualifiedTypeRef = BfNodeDynCastExact<BfQualifiedTypeReference>(rhs))
		{
			//_GetTypeRefs(qualifiedTypeRef->mLeft);
			if (!GenericTypeEquals(lhsGenericType, lhsTypeGenericArguments, qualifiedTypeRef->mLeft, ctx, genericParamOffset))
				return false;
		}
	}

	if (auto genericTypeRef = BfNodeDynCastExact<BfGenericInstanceTypeRef>(rhs))
	{
		int rhsGenericArgCount = BF_MAX(genericTypeRef->mGenericArguments.mSize, genericTypeRef->mCommas.mSize + 1);

		if (genericTypeRef->mGenericArguments.size() > rhsGenericArgCount + genericParamOffset)
			return false;

		for (int genericIdx = 0; genericIdx < BF_MAX(genericTypeRef->mGenericArguments.mSize, genericTypeRef->mCommas.mSize + 1); genericIdx++)
		{
			BfType* lhsArgType = (*lhsTypeGenericArguments)[genericParamOffset++];

			BfAstNode* genericArgTypeRef = NULL;
			if (genericIdx < genericTypeRef->mGenericArguments.mSize)
				genericArgTypeRef = genericTypeRef->mGenericArguments[genericIdx];

			if ((ctx->mResolveFlags & BfResolveTypeRefFlag_ForceUnboundGeneric) != 0)
			{
				genericArgTypeRef = NULL;
			}
			else if ((ctx->mResolveFlags & BfResolveTypeRefFlag_AllowUnboundGeneric) != 0)
			{
				if (BfNodeIsExact<BfWildcardTypeReference>(genericArgTypeRef))
					genericArgTypeRef = NULL;
			}

			if (genericArgTypeRef == NULL)
			{
				if (lhsArgType->IsGenericParam())
				{
					auto lhsGenericArgType = (BfGenericParamType*)lhsArgType;
					if ((lhsGenericArgType->mGenericParamKind != BfGenericParamKind_Type) || (lhsGenericArgType->mGenericParamIdx != genericIdx))
						return false;
				}
				else
					return false;
			}
			else
			{
				if (!Equals(lhsArgType, genericArgTypeRef, ctx))
					return false;
			}
		}
	}

	return true;
}

bool BfResolvedTypeSet::GenericTypeEquals(BfTypeInstance* lhsGenericType, BfTypeVector* lhsTypeGenericArguments, BfTypeReference* rhs, BfTypeDef* rhsTypeDef, LookupContext* ctx)
{
	int genericParamOffset = 0;
	bool isFullyQualified = false;
	BfTypeInstance* outerType = NULL;

	if (auto genericInstTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(rhs))
	{
		int checkIdx = 0;
		auto checkTypeRef = genericInstTypeRef->mElementType;
		while (checkTypeRef != NULL)
		{
			checkIdx++;
			if (checkIdx >= 2)
			{
				isFullyQualified = true;

				BfType* checkType = ctx->GetCachedResolvedType(checkTypeRef);
				if (checkType != NULL)
					outerType = checkType->ToTypeInstance();

				if (outerType != NULL)
				{
					BfTypeInstance* lhsCheckType = lhsGenericType;
					while (lhsCheckType->mTypeDef->mNestDepth > outerType->mTypeDef->mNestDepth)
					{
						lhsCheckType = ctx->mModule->GetOuterType(lhsCheckType);
					}
					if (lhsCheckType != outerType)
						return false;

					if (outerType->mGenericTypeInfo != NULL)
						genericParamOffset = (int)outerType->mGenericTypeInfo->mTypeGenericArguments.mSize;
				}

				break;
			}

			if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(checkTypeRef))
			{
				checkTypeRef = elementedTypeRef->mElementType;
				continue;
			}

			if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkTypeRef))
			{
				checkTypeRef = qualifiedTypeRef->mLeft;
				continue;
			}
			break;
		}
	}

	BfTypeInstance* rootOuterTypeInstance = NULL;
	auto rhsGenericTypeInstRef = BfNodeDynCastExact<BfGenericInstanceTypeRef>(rhs);

	if (!isFullyQualified)
	{
		rootOuterTypeInstance = ctx->mModule->mCurTypeInstance;
		if ((rhsTypeDef == ctx->mRootTypeDef) && (ctx->mRootOuterTypeInstance != NULL))
			rootOuterTypeInstance = ctx->mRootOuterTypeInstance;

		if (rhsGenericTypeInstRef == NULL)
		{
			if (auto rhsNullableTypeRef = BfNodeDynCastExact<BfNullableTypeRef>(rhs))
			{
				if (rhsNullableTypeRef != NULL)
				{
					if (lhsGenericType->mTypeDef != ctx->mModule->mContext->mCompiler->mNullableTypeDef)
						return false;

					auto rhsElemType = ctx->mModule->ResolveTypeRef(rhsNullableTypeRef->mElementType, BfPopulateType_Identity, ctx->mResolveFlags);
					return lhsGenericType->mGenericTypeInfo->mTypeGenericArguments[0] == rhsElemType;
				}
			}

			if ((rhsTypeDef != NULL) && (rootOuterTypeInstance != NULL))
			{
				// See if we're referring to an non-generic inner type where the outer type is generic
				if (lhsGenericType->mTypeDef->GetDefinition() != rhsTypeDef->GetDefinition())
					return false;

				BfTypeDef* commonOuterType = ctx->mModule->FindCommonOuterType(rootOuterTypeInstance->mTypeDef, rhsTypeDef->mOuterType);
				if (commonOuterType != NULL)
				{
					BfTypeInstance* checkTypeInstance = rootOuterTypeInstance;
					if (checkTypeInstance->IsBoxed())
						checkTypeInstance = checkTypeInstance->GetUnderlyingType()->ToTypeInstance();
					BF_ASSERT(checkTypeInstance->IsGenericTypeInstance());
					int numParentGenericParams = (int)commonOuterType->mGenericParamDefs.size();
					auto curTypeInstance = (BfTypeInstance*)checkTypeInstance;
					if (lhsGenericType->mGenericTypeInfo->mTypeGenericArguments.size() != numParentGenericParams)
						return false;
					for (int i = 0; i < (int)numParentGenericParams; i++)
						if ((*lhsTypeGenericArguments)[i] != curTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i])
							return false;
					return true;
				}
			}

			if (auto rhsQualifiedTypeRef = BfNodeDynCastExact<BfQualifiedTypeReference>(rhs))
			{
				auto rhsRightType = ctx->mModule->ResolveTypeRef(rhs, BfPopulateType_Identity, ctx->mResolveFlags);
				return rhsRightType == lhsGenericType;
			}

			return false;
		}
	}

	if (rhsGenericTypeInstRef == NULL)
		return true;

	BfTypeDef* elementTypeDef = ctx->mModule->ResolveGenericInstanceDef(rhsGenericTypeInstRef, NULL, ctx->mResolveFlags);
	if (elementTypeDef == NULL)
		return false;
	if (elementTypeDef->GetDefinition() != lhsGenericType->mTypeDef->GetDefinition())
		return false;

	// Do we need to add generic arguments from an in-context outer class?
	if ((elementTypeDef->mOuterType != NULL) && (rootOuterTypeInstance != NULL) && (rootOuterTypeInstance->IsGenericTypeInstance()))
	{
		BfTypeDef* commonOuterType = ctx->mModule->FindCommonOuterType(rootOuterTypeInstance->mTypeDef, elementTypeDef->mOuterType);
		if (commonOuterType != NULL)
		{
			auto parentTypeInstance = rootOuterTypeInstance;
			genericParamOffset = (int) commonOuterType->mGenericParamDefs.size();
			for (int i = 0; i < genericParamOffset; i++)
				for (auto genericArg : parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
				{
				if (parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i] != (*lhsTypeGenericArguments)[i])
					return false;
				}
		}
	}

	if (!GenericTypeEquals(lhsGenericType, lhsTypeGenericArguments, rhs, ctx, genericParamOffset, isFullyQualified))
		return false;

	return genericParamOffset == (int)lhsTypeGenericArguments->size();
}

BfType* BfResolvedTypeSet::LookupContext::GetCachedResolvedType(BfTypeReference* typeReference)
{
	if (typeReference == mRootTypeRef)
		return mRootResolvedType;
	BfType** typePtr = NULL;
	if (mResolvedTypeMap.TryGetValue(typeReference, &typePtr))
		return *typePtr;
	return NULL;
}

void BfResolvedTypeSet::LookupContext::SetCachedResolvedType(BfTypeReference* typeReference, BfType* type)
{
	if (typeReference == mRootTypeRef)
		mRootResolvedType = type;
	else
		mResolvedTypeMap[typeReference] = type;
}

BfType* BfResolvedTypeSet::LookupContext::ResolveTypeRef(BfTypeReference* typeReference)
{
	return mModule->ResolveTypeRef(typeReference, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
}

BfTypeDef* BfResolvedTypeSet::LookupContext::ResolveToTypeDef(BfTypeReference* typeReference, BfType** outType)
{
	if (outType != NULL)
		*outType = NULL;

	if (typeReference == mRootTypeRef)
		return mRootTypeDef;

	if (auto typeDefTypeRef = BfNodeDynCast<BfDirectTypeDefReference>(typeReference))
	{
		return typeDefTypeRef->mTypeDef;
	}

	auto type = mModule->ResolveTypeRef(typeReference, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
	if (type == NULL)
		return NULL;
	if (outType != NULL)
		*outType = type;
	if (type->IsPrimitiveType())
		return ((BfPrimitiveType*)type)->mTypeDef;
	auto typeInst = type->ToTypeInstance();
	if (typeInst == NULL)
		return NULL;
	return typeInst->mTypeDef->GetDefinition();
}

bool BfResolvedTypeSet::Equals(BfType* lhs, BfTypeReference* rhs, BfTypeDef* rhsTypeDef, LookupContext* ctx)
{
	auto rhsType = ctx->mModule->ResolveTypeDef(rhsTypeDef, BfPopulateType_Identity);
	if (rhsType == NULL)
	{
		ctx->mFailed = true;
		return false;
	}

	return BfResolvedTypeSet::Equals(lhs, rhsType, ctx);
}

bool BfResolvedTypeSet::Equals(BfType* lhs, BfTypeReference* rhs, LookupContext* ctx)
{
	//BP_ZONE("BfResolvedTypeSet::Equals");

	if (ctx->mRootTypeRef != rhs)
	{
		if (auto retTypeRef = BfNodeDynCastExact<BfModifiedTypeRef>(rhs))
		{
			auto resolvedType = ctx->mModule->ResolveTypeRef(rhs);
			if ((resolvedType != NULL) && (resolvedType->IsRef()))
				resolvedType = resolvedType->GetUnderlyingType();
			return lhs == resolvedType;
		}
	}

 	if ((rhs->IsNamedTypeReference()) || (rhs->IsA<BfGenericInstanceTypeRef>()) || (rhs->IsA<BfQualifiedTypeReference>()))
	{
		if ((ctx->mRootTypeRef != rhs) || (ctx->mRootTypeDef == NULL))
		{
			auto rhsResolvedType = ctx->ResolveTypeRef(rhs);
			if (rhsResolvedType == NULL)
			{
				ctx->mFailed = true;
				return false;
			}
			return lhs == rhsResolvedType;
		}
	}

	if (auto declTypeRef = BfNodeDynCastExact<BfExprModTypeRef>(rhs))
	{
		auto cachedResolveType = ctx->GetCachedResolvedType(rhs);
		BF_ASSERT(cachedResolveType != NULL);
		return lhs == cachedResolveType;
	}

	// Strip off 'const' - it's just an error when applied to a typeRef in Beef
	auto constTypeRef = BfNodeDynCastExact<BfConstTypeRef>(rhs);
	if (constTypeRef != NULL)
		return Equals(lhs, constTypeRef->mElementType, ctx);

	if (lhs->IsBoxed())
	{
		return false;
	}
	else if (lhs->IsArray())
	{
		auto rhsArrayTypeRef = BfNodeDynCastExact<BfArrayTypeRef>(rhs);
		if (rhsArrayTypeRef == NULL)
			return false;
		// Any non-comma param means it's a sized array
		for (auto param : rhsArrayTypeRef->mParams)
		{
			bool isComma = false;
			if (auto tokenNode = BfNodeDynCast<BfTokenNode>(param))
				isComma = tokenNode->mToken == BfToken_Comma;
			if (!isComma)
				return false;
		}

		BfArrayType* lhsArrayType = (BfArrayType*) lhs;
		if (lhsArrayType->mDimensions != rhsArrayTypeRef->mDimensions)
			return false;
		return Equals(lhsArrayType->mGenericTypeInfo->mTypeGenericArguments[0], rhsArrayTypeRef->mElementType, ctx);
	}
	else if (lhs->IsDelegateFromTypeRef() || lhs->IsFunctionFromTypeRef())
	{
		auto rhsDelegateType = BfNodeDynCastExact<BfDelegateTypeRef>(rhs);
		if (rhsDelegateType == NULL)
			return false;

		bool wantGeneric = false;

		BfDelegateInfo* lhsDelegateInfo = lhs->GetDelegateInfo();

		auto lhsTypeInstance = lhs->ToTypeInstance();
		BfMethodDef* invokeMethodDef = lhsTypeInstance->mTypeDef->mMethods[0];
		BF_ASSERT(invokeMethodDef->mName == "Invoke");

		bool rhsIsDelegate = rhsDelegateType->mTypeToken->GetToken() == BfToken_Delegate;

		if ((lhs->IsDelegate()) != rhsIsDelegate)
			return false;

		auto _CheckType = [&](BfType* type)
		{
			if (type->IsTypeGenericParam())
				wantGeneric = true;
		};

		BfCallingConvention rhsCallingConvention = BfCallingConvention_Unspecified;
		if (ctx->mRootTypeRef == rhsDelegateType)
			rhsCallingConvention = ctx->mCallingConvention;
		else
			ctx->mModule->GetDelegateTypeRefAttributes(rhsDelegateType, rhsCallingConvention);
		if (lhsDelegateInfo->mCallingConvention != rhsCallingConvention)
			return false;
		if (!Equals(lhsDelegateInfo->mReturnType, rhsDelegateType->mReturnType, ctx))
			return false;
		_CheckType(lhsDelegateInfo->mReturnType);

		bool isMutating = true;

		int paramRefOfs = 0;
		if ((!rhsDelegateType->mParams.IsEmpty()) && (lhs->IsFunction()))
		{
			auto param0 = rhsDelegateType->mParams[0];
			if ((param0->mNameNode != NULL) && (param0->mNameNode->Equals("this")))
			{
				if (!lhsDelegateInfo->mHasExplicitThis)
					return false;

				bool handled = false;
				auto lhsThisType = lhsDelegateInfo->mParams[0];

				auto rhsThisType = ctx->mModule->ResolveTypeRef(param0->mTypeRef, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoWarnOnMut | BfResolveTypeRefFlag_AllowRef));
				bool wantsMutating = false;

				if (rhsThisType->IsRef())
				{
					if (lhsThisType != rhsThisType->GetUnderlyingType())
						return false;
					wantsMutating = (lhsThisType->IsValueType()) || (lhsThisType->IsGenericParam());
				}
				else
				{
					if (lhsThisType != rhsThisType)
						return false;
				}
				if (invokeMethodDef->mIsMutating != wantsMutating)
					return false;

				paramRefOfs = 1;
			}
		}

		int lhsParamsCount = (int)lhsDelegateInfo->mParams.size();
		if (lhsDelegateInfo->mHasVarArgs)
			lhsParamsCount++;

		if (lhsParamsCount != (int)rhsDelegateType->mParams.size())
			return false;
		for (int paramIdx = paramRefOfs; paramIdx < lhsDelegateInfo->mParams.size(); paramIdx++)
		{
			auto paramTypeRef = rhsDelegateType->mParams[paramIdx]->mTypeRef;
			if (!Equals(lhsDelegateInfo->mParams[paramIdx], paramTypeRef, ctx))
				return false;
			_CheckType(lhsDelegateInfo->mParams[paramIdx]);
			StringView rhsParamName;
			if (rhsDelegateType->mParams[paramIdx]->mNameNode != NULL)
				rhsParamName = rhsDelegateType->mParams[paramIdx]->mNameNode->ToStringView();
			if (invokeMethodDef->mParams[paramIdx]->mName != rhsParamName)
				return false;
		}

		if ((ctx->mModule->mCurTypeInstance == NULL) || (!ctx->mModule->mCurTypeInstance->IsGenericTypeInstance()))
			wantGeneric = false;

		//TODO:
		wantGeneric = false;

 		if (wantGeneric != lhsTypeInstance->IsGenericTypeInstance())
 			return false;

		return true;
	}
	else if (lhs->IsTypeInstance())
	{
		BfTypeInstance* lhsInst = (BfTypeInstance*) lhs;

		if (lhs->IsTuple())
		{
			if (!rhs->IsA<BfTupleTypeRef>())
				return false;

			BfTupleTypeRef* rhsTupleTypeRef = (BfTupleTypeRef*)rhs;
			BfTypeInstance* lhsTupleType = (BfTypeInstance*)lhs;

			if (lhsTupleType->mFieldInstances.size() != rhsTupleTypeRef->mFieldTypes.size())
				return false;

			for (int fieldIdx = 0; fieldIdx < (int)lhsTupleType->mFieldInstances.size(); fieldIdx++)
			{
				BfFieldInstance* fieldInstance = &lhsTupleType->mFieldInstances[fieldIdx];

				auto rhsResolvedType = ctx->mModule->ResolveTypeRef(rhsTupleTypeRef->mFieldTypes[fieldIdx], BfPopulateType_Identity);
				if (rhsResolvedType != fieldInstance->mResolvedType)
					return false;

				BfFieldDef* fieldTypeDef = fieldInstance->GetFieldDef();

				BfIdentifierNode* fieldName = NULL;
				if (fieldIdx < (int)rhsTupleTypeRef->mFieldNames.size())
					fieldName = rhsTupleTypeRef->mFieldNames[fieldIdx];
				if (fieldName != NULL)
				{
					if (fieldName->ToString() != fieldTypeDef->mName)
						return false;
				}
				else
				{
					char nameStr[64];
					sprintf(nameStr, "%d", fieldIdx);
					if (fieldTypeDef->mName != nameStr)
						return false;
				}
			}

			return true;
		}
		else if (lhs->IsGenericTypeInstance())
		{
			BfType* rhsType = NULL;
 			auto rhsTypeDef = ctx->ResolveToTypeDef(rhs, &rhsType);
 			if (rhsTypeDef == NULL)
 				return false;

			if (rhsType != NULL)
				return lhs == rhsType;

			BfTypeInstance* lhsGenericType = (BfTypeInstance*) lhs;
			return GenericTypeEquals(lhsGenericType, &lhsGenericType->mGenericTypeInfo->mTypeGenericArguments, rhs, rhsTypeDef, ctx);
		}
		else
		{
			if (rhs->IsA<BfElementedTypeRef>())
				return false;

			if (!rhs->IsTypeDefTypeReference())
			{
				if (rhs->IsA<BfDelegateTypeRef>())
					return false; // Would have caught it before

				if (rhs->IsA<BfQualifiedTypeReference>())
				{
					//TODO: Under what circumstances was this supposed to be used?  This caused an infinite loop comparing
					//  'var' against a delegate type instance
					auto resolvedType = ctx->mModule->ResolveTypeRef(rhs, BfPopulateType_Identity);
					if (resolvedType == lhsInst)
					{
						return true;
					}
				}

				return false;
			}
			auto rhsTypeDef = ctx->ResolveToTypeDef(rhs);
			if (rhsTypeDef == NULL)
				return false;

			return lhsInst->IsInstanceOf(rhsTypeDef);
		}
	}
	else if (lhs->IsPrimitiveType())
	{
		if (lhs->IsDot())
		{
			auto varTypeReference = BfNodeDynCastExact<BfDotTypeReference>(rhs);
			if (varTypeReference != NULL)
				return true;
		}

		if (lhs->IsVar())
		{
			auto varTypeReference = BfNodeDynCastExact<BfVarTypeReference>(rhs);
			if (varTypeReference != NULL)
				return true;
		}

		if (lhs->IsLet())
		{
			auto letTypeReference = BfNodeDynCastExact<BfLetTypeReference>(rhs);
			if (letTypeReference != NULL)
				return true;
		}

		BfPrimitiveType* lhsPrimType = (BfPrimitiveType*)lhs;
		auto rhsTypeDef = ctx->ResolveToTypeDef(rhs);
// 		if (rhsTypeDef->mTypeCode == BfTypeCode_TypeAlias)
// 			return Equals(lhs, rhs, rhsTypeDef, ctx);
		return lhsPrimType->mTypeDef == rhsTypeDef;
	}
	else if (lhs->IsPointer())
	{
		auto rhsPointerTypeRef = BfNodeDynCastExact<BfPointerTypeRef>(rhs);
		if (rhsPointerTypeRef == NULL)
			return false;
		BfPointerType* lhsPtrType = (BfPointerType*)lhs;
		return Equals(lhsPtrType->mElementType, rhsPointerTypeRef->mElementType, ctx);
	}
	else if (lhs->IsGenericParam())
	{
		auto lhsGenericParamType = (BfGenericParamType*)lhs;
		auto rhsGenericParamTypeRef = BfNodeDynCastExact<BfGenericParamTypeRef>(rhs);
		if (rhsGenericParamTypeRef == NULL)
		{
			if (auto constExprTypeRef = BfNodeDynCastExact<BfConstExprTypeRef>(rhs))
			{
				BfVariant result;
				if (constExprTypeRef->mConstExpr == NULL)
					return false;

				BfType* resultType = NULL;
				result = EvaluateToVariant(ctx, constExprTypeRef->mConstExpr, resultType);
				return resultType == lhs;
			}

			return false;
		}
		return (lhsGenericParamType->mGenericParamKind == rhsGenericParamTypeRef->mGenericParamKind) &&
			(lhsGenericParamType->mGenericParamIdx == rhsGenericParamTypeRef->mGenericParamIdx);
	}
	else if (lhs->IsRef())
	{
		auto lhsRefType = (BfRefType*)lhs;
		auto rhsRefTypeRef = BfNodeDynCastExact<BfRefTypeRef>(rhs);
		if (rhsRefTypeRef == NULL)
			return false;

		auto refKind = BfRefType::RefKind_Ref;
		if (rhsRefTypeRef->mRefToken == NULL)
			refKind = BfRefType::RefKind_Ref;
		else if (rhsRefTypeRef->mRefToken->GetToken() == BfToken_In)
			refKind = BfRefType::RefKind_In;
		else if (rhsRefTypeRef->mRefToken->GetToken() == BfToken_Out)
			refKind = BfRefType::RefKind_Out;
		else if (rhsRefTypeRef->mRefToken->GetToken() == BfToken_Mut)
			refKind = BfRefType::RefKind_Mut;

		return (lhsRefType->mRefKind == refKind) &&
			Equals(lhsRefType->mElementType, rhsRefTypeRef->mElementType, ctx);
	}
	else if (lhs->IsModifiedTypeType())
	{
		auto lhsRetTypeType = (BfModifiedTypeType*)lhs;
		auto rhsRetTypeTypeRef = BfNodeDynCastExact<BfModifiedTypeRef>(rhs);
		if (rhsRetTypeTypeRef == NULL)
			return false;
		if (lhsRetTypeType->mModifiedKind != rhsRetTypeTypeRef->mRetTypeToken->mToken)
			return false;
		return Equals(lhsRetTypeType->mElementType, rhsRetTypeTypeRef->mElementType, ctx);
	}
	else if (lhs->IsConcreteInterfaceType())
	{
		// No way to create a reference to one of these
		return false;
	}
	else if (lhs->IsSizedArray())
	{
		auto rhsArrayTypeRef = BfNodeDynCastExact<BfArrayTypeRef>(rhs);
		if (rhsArrayTypeRef == NULL)
			return false;
		if ((rhsArrayTypeRef->mDimensions != 1) && (rhsArrayTypeRef->mParams.size() != 1))
			return false;
		BfSizedArrayType* lhsArrayType = (BfSizedArrayType*)lhs;
		if (!Equals(lhsArrayType->mElementType, rhsArrayTypeRef->mElementType, ctx))
			return false;

		intptr elementCount = -1;
		BfExpression* sizeExpr = BfNodeDynCast<BfExpression>(rhsArrayTypeRef->mParams[0]);
		BF_ASSERT(sizeExpr != NULL);
		if (sizeExpr != NULL)
		{
			BfType* intType = ctx->mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			BfTypedValue typedVal;
			ctx->mResolvedValueMap.TryGetValue(sizeExpr, &typedVal);
			if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
			{
				if (!lhs->IsUnknownSizedArrayType())
					return false;

				auto lhsUnknownSizedArray = (BfUnknownSizedArrayType*)lhs;
				return lhsUnknownSizedArray->mElementCountSource = typedVal.mType;
			}
			if (typedVal)
				typedVal = ctx->mModule->Cast(sizeExpr, typedVal, intType);

			if (typedVal)
			{
				if (lhs->IsUnknownSizedArrayType())
					return false;

				auto constant = ctx->mModule->mBfIRBuilder->GetConstant(typedVal.mValue);
				if ((constant->mConstType == BfConstType_Undef) || (!BfIRBuilder::IsInt(constant->mTypeCode)))
				{
					elementCount = -1; // Marker for undef
				}
				else
				{
					elementCount = (intptr)constant->mInt64;
					BF_ASSERT(elementCount >= 0); // Should have been caught in hash
				}
			}
		}

		return lhsArrayType->mElementCount == elementCount;
	}
	else if (lhs->IsMethodRef())
	{
		// Always make these unique.  The MethodInstance value will change on rebuild anyway
		return false;
	}
	else if (lhs->IsConstExprValue())
	{
		auto constExprTypeRef = BfNodeDynCastExact<BfConstExprTypeRef>(rhs);
		if (constExprTypeRef == NULL)
			return false;

		if (!lhs->IsConstExprValue())
			return false;

		BfConstExprValueType* lhsConstExprType = (BfConstExprValueType*)lhs;

		BfVariant result;
		if (constExprTypeRef->mConstExpr != NULL)
		{
			BfType* resultType = NULL;
			result = EvaluateToVariant(ctx, constExprTypeRef->mConstExpr, resultType);
			if (resultType != lhsConstExprType->mType)
				return false;
		}

		return (result.mTypeCode == lhsConstExprType->mValue.mTypeCode) &&
			(result.mInt64 == lhsConstExprType->mValue.mInt64);
	}
	else
	{
		BF_FATAL("Not handled");
	}

	return false;
}

bool BfResolvedTypeSet::Equals(BfType* lhs, BfAstNode* rhs, LookupContext* ctx)
{
	if (auto rhsTypeRef = BfNodeDynCast<BfTypeReference>(rhs))
		return Equals(lhs, rhsTypeRef, ctx);

	BfType* rhsResultType;
	if (ctx->mResolvedTypeMap.TryGetValue(rhs, &rhsResultType))
		return lhs == rhsResultType;

	BF_FATAL("Invalid value in BfResolvedTypeSet::Equals");

	return false;
}

void BfResolvedTypeSet::RemoveEntry(BfResolvedTypeSet::EntryRef entry)
{
 	int hashIdx = (entry->mHashCode & 0x7FFFFFFF) % mHashSize;
// 	if (entry->mPrev == NULL)
// 	{
// 		if (entry->mNext != NULL)
// 			entry->mNext->mPrev = NULL;
// 		BF_ASSERT(mHashHeads[bucket] == entry);
// 		mHashHeads[bucket] = entry->mNext;
// 	}
// 	else
// 	{
// 		entry->mPrev->mNext = entry->mNext;
// 		if (entry->mNext != NULL)
// 			entry->mNext->mPrev = entry->mPrev;
// 	}
//
// 	mSize--;

	bool found = false;

	int* srcCheckEntryPtr = &this->mHashHeads[hashIdx];
	int checkEntryIdx = *srcCheckEntryPtr;
	while (checkEntryIdx != -1)
	{
		auto checkEntry = &mEntries[checkEntryIdx];
		if (checkEntryIdx == entry.mIndex)
		{
			*srcCheckEntryPtr = checkEntry->mNext;
			found = true;
		}
		srcCheckEntryPtr = &checkEntry->mNext;
		checkEntryIdx = checkEntry->mNext;
	}

	BF_ASSERT(found);
	BF_ASSERT(entry->mValue == NULL);
	FreeIdx(entry.mIndex);
}

// BfResolvedTypeSet::Iterator BfResolvedTypeSet::begin()
// {
// 	return ++Iterator(this);
// }
//
// BfResolvedTypeSet::Iterator BfResolvedTypeSet::end()
// {
// 	Iterator itr(this);
// 	itr.mCurBucket = HashSize;
// 	return itr;
// }
//
// BfResolvedTypeSet::Iterator BfResolvedTypeSet::erase(BfResolvedTypeSet::Iterator& itr)
// {
// 	auto next = itr;
// 	++next;
//
// 	auto cur = itr.mCurEntry;
//
// 	auto& hashHead = itr.mTypeSet->mHashHeads[itr.mCurBucket];
//
// 	if (hashHead == cur)
// 		hashHead = cur->mNext;
// 	if (cur->mPrev != NULL)
// 		cur->mPrev->mNext = cur->mNext;
// 	if (cur->mNext != NULL)
// 		cur->mNext->mPrev = cur->mPrev;
// 	delete cur;
//
// 	//BfLogSys("Deleting node %@ from bucket %d\n", cur, itr.mCurBucket);
//
// 	mSize--;
// 	return next;
// }

//////////////////////////////////////////////////////////////////////////

BfHotTypeVersion::~BfHotTypeVersion()
{
	for (auto member : mMembers)
		member->Deref();
}

BfHotTypeData::~BfHotTypeData()
{
	for (auto version : mTypeVersions)
	{
		version->Deref();
	}
}

BfHotTypeVersion* BfHotTypeData::GetTypeVersion(int hotCommitedIdx)
{
	for (int checkIdx = (int)mTypeVersions.size() - 1; checkIdx >= 0; checkIdx--)
	{
		BfHotTypeVersion* hotTypeVersion = mTypeVersions[checkIdx];
		if (hotTypeVersion->mDeclHotCompileIdx <= hotCommitedIdx)
			return hotTypeVersion;
	}
	return NULL;
}

BfHotTypeVersion* BfHotTypeData::GetLatestVersion()
{
	return mTypeVersions.back();
}

BfHotTypeVersion* BfHotTypeData::GetLatestVersionHead()
{
	auto lastestVersion = mTypeVersions.back();
	for (int versionIdx = (int)mTypeVersions.size() - 1; versionIdx >= 0; versionIdx--)
	{
		auto checkVersion = mTypeVersions[versionIdx];
		if (checkVersion->mDataHash != lastestVersion->mDataHash)
			break;
		lastestVersion = checkVersion;
	}

	return lastestVersion;
}

void BfHotTypeData::ClearVersionsAfter(int hotIdx)
{
	while (!mTypeVersions.IsEmpty())
	{
		auto hotTypeVersion = mTypeVersions.back();
		if (hotTypeVersion->mDeclHotCompileIdx > hotIdx)
		{
			hotTypeVersion->Deref();
			mTypeVersions.pop_back();
		}
		else
			break;
	}
}

void BfHotDepData::Deref()
{
	mRefCount--;
	BF_ASSERT(mRefCount >= 0);

	if (mRefCount == 0)
	{
		switch (mDataKind)
		{
		case BfHotDepDataKind_TypeVersion:
			delete (BfHotTypeVersion*)this;
			break;
		case BfHotDepDataKind_ThisType:
			delete (BfHotThisType*)this;
			break;
		case BfHotDepDataKind_Allocation:
			delete (BfHotAllocation*)this;
			break;
		case BfHotDepDataKind_Method:
			delete (BfHotMethod*)this;
			break;
		case BfHotDepDataKind_DupMethod:
			delete (BfHotDupMethod*)this;
			break;
		case BfHotDepDataKind_DevirtualizedMethod:
			delete (BfHotDevirtualizedMethod*)this;
			break;
		case BfHotDepDataKind_InnerMethod:
			delete (BfHotInnerMethod*)this;
			break;
		case BfHotDepDataKind_FunctionPtr:
			delete (BfHotFunctionReference*)this;
			break;
		case BfHotDepDataKind_VirtualDecl:
			delete (BfHotVirtualDeclaration*)this;
			break;
		default:
			BF_FATAL("Not handled");
		}
	}
}

void BfHotMethod::Clear(bool keepDupMethods)
{
	if (mPrevVersion != NULL)
	{
		mPrevVersion->Deref();
		mPrevVersion = NULL;
	}

	if (mSrcTypeVersion != NULL)
	{
		mSrcTypeVersion->Deref();
		mSrcTypeVersion = NULL;
	}

	if ((keepDupMethods) && ((mFlags & BfHotDepDataFlag_HasDup) != 0))
	{
		int writeIdx = 0;
		for (int i = 0; i < (int)mReferences.size(); i++)
		{
			auto depData = mReferences[i];
			if (depData->mDataKind == BfHotDepDataKind_DupMethod)
			{
				mReferences[writeIdx++] = depData;
			}
			else
			{
				depData->Deref();
			}
		}
		mReferences.mSize = writeIdx;
	}
	else
	{
		for (auto depData : mReferences)
		{
			depData->Deref();
		}
		mReferences.Clear();
	}
}

BfHotMethod::~BfHotMethod()
{
	Clear();
}

//////////////////////////////////////////////////////////////////////////

#pragma warning(disable:4146)

// Only 63 chars - skip zero
static const char cHash64bToChar[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
	'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
	'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
	'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_' };
String BfTypeUtils::HashEncode64(uint64 val)
{
	String outStr;
	if ((int64)val < 0)
	{
		uint64 flippedNum = (uint64)-(int64)val;
		// Only flip if the encoded result would actually be shorter
		if (flippedNum <= 0x00FFFFFFFFFFFFFFLL)
		{
			val = flippedNum;
			outStr.Append('_');
		}
	}

	while (val > 0)
	{
		int charIdx = val % 0x3F;
		val /= 0x3F;
		outStr.Append(cHash64bToChar[charIdx]);
	}

	return outStr;
}

BfPrimitiveType* BfTypeUtils::GetPrimitiveType(BfModule* module, BfTypeCode typeCode)
{
	return module->GetPrimitiveType(typeCode);
}

void BfTypeUtils::PopulateType(BfModule* module, BfType* type)
{
	module->PopulateType(type);
}

String BfTypeUtils::TypeToString(BfAstNode* typeRefNode)
{
	if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(typeRefNode))
		return identifierNode->ToString();

	auto typeRef = BfNodeDynCast<BfTypeReference>(typeRefNode);
	if (typeRef == NULL)
		return "";

	if (auto typeDefTypeRef = BfNodeDynCast<BfDirectTypeDefReference>(typeRef))
	{
		if (!typeDefTypeRef->mTypeDef->mNamespace.IsEmpty())
			return typeDefTypeRef->mTypeDef->mNamespace.ToString() + "." + typeDefTypeRef->mTypeDef->mName->mString;
		else
			return String(typeDefTypeRef->mTypeDef->mName->mString);
	}

	if (typeRef->IsNamedTypeReference())
	{
		return typeRef->ToString();
	}

	if (auto ptrType = BfNodeDynCast<BfPointerTypeRef>(typeRef))
		return TypeToString(ptrType->mElementType) + "*";
	if (auto ptrType = BfNodeDynCast<BfArrayTypeRef>(typeRef))
	{
		String name = TypeToString(ptrType->mElementType) + "[";
		for (int i = 1; i < ptrType->mDimensions; i++)
			name += ",";
		name += "]";
		return name;
	}
	if (auto genericInstanceType = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		String name = TypeToString(genericInstanceType->mElementType);
		name += "<";
		for (int i = 0; i < (int)genericInstanceType->mGenericArguments.size(); i++)
		{
			if (i > 0)
				name += ", ";
			name += TypeToString(genericInstanceType->mGenericArguments[i]);
		}
		name += ">";
		return name;
	}
	if (auto genericParamTypeRef = BfNodeDynCast<BfGenericParamTypeRef>(typeRef))
	{
		if (genericParamTypeRef->mGenericParamKind == BfGenericParamKind_Method)
			return StrFormat("@M%d", genericParamTypeRef->mGenericParamIdx);
		return StrFormat("@T%d", genericParamTypeRef->mGenericParamIdx);
	}
	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		return TypeToString(qualifiedTypeRef->mLeft) + "." + TypeToString(qualifiedTypeRef->mRight);
	}
	if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(typeRef))
	{
		String str = BfTokenToString(refTypeRef->mRefToken->GetToken());
		str += " ";
		str += TypeToString(refTypeRef->mElementType);
		return str;
	}
	if (auto directStrTypeName = BfNodeDynCast<BfDirectStrTypeReference>(typeRef))
		return directStrTypeName->mTypeName;

	if (auto tupleTypeRef = BfNodeDynCast<BfTupleTypeRef>(typeRef))
	{
		String name = "(";

		for (int i = 0; i < tupleTypeRef->mFieldTypes.size(); i++)
		{
			if (i > 0)
				name += ", ";
			name += TypeToString(tupleTypeRef->mFieldTypes[i]);
			if ((i < tupleTypeRef->mFieldNames.size()) && (tupleTypeRef->mFieldNames[i] != NULL))
			{
				name += " ";
				name += tupleTypeRef->mFieldNames[i]->ToString();
			}
		}

		name += ")";
		return name;
	}

	if (auto constTypeRef = BfNodeDynCast<BfConstExprTypeRef>(typeRef))
	{
		String name = "const ";
		name += constTypeRef->mConstExpr->ToString();
		return name;
	}

	//BF_DBG_FATAL("Not implemented");
	return typeRef->ToString();
}

bool BfTypeUtils::TypeEquals(BfType* typeA, BfType* typeB, BfTypeInstance* selfType)
{
	if (typeA->IsUnspecializedTypeVariation())
		typeA = selfType->mModule->ResolveSelfType(typeA, selfType);
	if (typeB->IsUnspecializedTypeVariation())
		typeB = selfType->mModule->ResolveSelfType(typeB, selfType);
	return typeA == typeB;
}

String BfTypeUtils::TypeToString(BfTypeDef* typeDef, BfTypeNameFlags typeNameFlags)
{
	String str;
	TypeToString(str, typeDef, typeNameFlags);
	return str;
}

bool BfTypeUtils::TypeToString(StringImpl& str, BfTypeDef* typeDef, BfTypeNameFlags typeNameFlags)
{
	auto checkTypeDef = typeDef;
	char needsSep = 0;

	if (checkTypeDef->mOuterType != NULL)
	{
		if (TypeToString(str, checkTypeDef->mOuterType, typeNameFlags))
		{
			if ((typeNameFlags & BfTypeNameFlag_InternalName) != 0)
				needsSep = '+';
			else
				needsSep = '.';
		}
	}
	else
	{
		if (((typeNameFlags & BfTypeNameFlag_OmitNamespace) == 0) && (!typeDef->mNamespace.IsEmpty()))
		{
			typeDef->mNamespace.ToString(str);
			needsSep = '.';
		}
	}

	if (needsSep != 0)
		str += needsSep;

	if (((typeNameFlags & BfTypeNameFlag_HideGlobalName) != 0) && (typeDef->IsGlobalsContainer()))
		return false;

	typeDef->mName->ToString(str);

	if (typeDef->mGenericParamDefs.size() != 0)
	{
		int prevGenericParamCount = 0;
		if (checkTypeDef->mOuterType != NULL)
			prevGenericParamCount = (int)typeDef->mOuterType->mGenericParamDefs.size();

		if (prevGenericParamCount != (int)checkTypeDef->mGenericParamDefs.size())
		{
			str += "<";
			for (int i = prevGenericParamCount; i < (int)checkTypeDef->mGenericParamDefs.size(); i++)
			{
				if ((typeNameFlags & BfTypeNameFlag_InternalName) != 0)
				{
					if (i > prevGenericParamCount)
						str += ",";
				}
				else
				{
					if (i > prevGenericParamCount)
						str += ", ";
					str += checkTypeDef->mGenericParamDefs[i]->mName;
				}
			}
			str += ">";
		}
	}

	return true;
}

int BfTypeUtils::GetSplatCount(BfType* type)
{
	int splatCount = 0;
	SplatIterate([&](BfType* checkType) { splatCount++; }, type);
	return splatCount;
}

BfConstExprValueType::~BfConstExprValueType()
{
// 	mContext->mTypeConstExprCount--;
// 	BF_ASSERT(mContext->mTypeConstExprCount == 0);
}