#include "BeefySysLib/util/AllocDebug.h"

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfCodeGen.h"
#include "BfExprEvaluator.h"
#include <fcntl.h>
#include "BfConstResolver.h"
#include "BfMangler.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BfSourceClassifier.h"
#include "BfAutoComplete.h"
#include "BfDemangler.h"
#include "BfResolvePass.h"
#include "BfFixits.h"
#include "BfIRCodeGen.h"
#include "BfDefBuilder.h"

//////////////////////////////////////////////////////////////////////////

int32 GetNumLowZeroBits(int32 n)
{
	if (n == 0)
		return 32;

	int i = 0;
	while ((n & 1) == 0)
	{
		n = (int32)((uint32)n >> 1);
		i++;
	}
	return i;
}

//////////////////////////////////////////////////////////////////////////

USING_NS_BF;

BfGenericExtensionEntry* BfModule::BuildGenericExtensionInfo(BfGenericTypeInstance* genericTypeInst, BfTypeDef* partialTypeDef)
{
	if (!partialTypeDef->IsExtension())
		return NULL;

	if (partialTypeDef->mGenericParamDefs.size() != genericTypeInst->mTypeGenericArguments.size())
	{
		AssertErrorState();
		return NULL;
	}

	BfGenericExtensionInfo* genericExtensionInfo = genericTypeInst->mGenericExtensionInfo;
	if (genericExtensionInfo == NULL)
	{
		genericExtensionInfo = new BfGenericExtensionInfo();
		genericTypeInst->mGenericExtensionInfo = genericExtensionInfo;
	}

	BfTypeState typeState;
	typeState.mPrevState = mContext->mCurTypeState;
	typeState.mTypeInstance = genericTypeInst;
	typeState.mCurTypeDef = partialTypeDef;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	//auto genericExEntry = new BfGenericExtensionEntry();
	//auto insertPair = genericExtensionInfo->mExtensionMap.insert(std::make_pair(partialTypeDef, BfGenericExtensionEntry()));
	//auto genericExEntry = &insertPair.first->second;

	BfGenericExtensionEntry* genericExEntry;
	genericExtensionInfo->mExtensionMap.TryAdd(partialTypeDef, NULL, &genericExEntry);

	int startDefGenericParamIdx = (int)genericExEntry->mGenericParams.size();
	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = new BfGenericTypeParamInstance(partialTypeDef, paramIdx);
		genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);
		genericExEntry->mGenericParams.push_back(genericParamInstance);
	}

	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = genericExEntry->mGenericParams[paramIdx];

		auto rootGenericParamInstance = genericTypeInst->mGenericParams[paramIdx];
		genericParamInstance->mTypeConstraint = rootGenericParamInstance->mTypeConstraint;
		genericParamInstance->mInterfaceConstraints = rootGenericParamInstance->mInterfaceConstraints;
		genericParamInstance->mGenericParamFlags |= rootGenericParamInstance->mGenericParamFlags;

		ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType());
	}

	for (auto genericParam : genericExEntry->mGenericParams)
	{
		for (auto constraintTypeInst : genericParam->mInterfaceConstraints)
			AddDependency(constraintTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
		if (genericParam->mTypeConstraint != NULL)
			AddDependency(genericParam->mTypeConstraint, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
	}
	return genericExEntry;
}

bool BfModule::BuildGenericParams(BfType* resolvedTypeRef)
{
	BfTypeState typeState;
	typeState.mPrevState = mContext->mCurTypeState;
	typeState.mBuildingGenericParams = true;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	BF_ASSERT(mCurMethodInstance == NULL);

	auto genericTypeInst = (BfGenericTypeInstance*)resolvedTypeRef;

	if (genericTypeInst->mTypeGenericArguments[0]->IsGenericParam())
	{
		BF_ASSERT(genericTypeInst->mIsUnspecialized);
	}

	auto typeDef = genericTypeInst->mTypeDef;
	int startDefGenericParamIdx = (int)genericTypeInst->mGenericParams.size();
	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = new BfGenericTypeParamInstance(typeDef, paramIdx);
		genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);
		genericTypeInst->mGenericParams.push_back(genericParamInstance);
	}	

	if (!typeDef->mPartials.empty())
	{
		for (auto partialTypeDef : typeDef->mPartials)
		{
			if (!partialTypeDef->IsExtension())
			{
				typeState.mCurTypeDef = partialTypeDef;
				for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mTypeGenericArguments.size(); paramIdx++)
				{
					auto genericParamDef = typeDef->mGenericParamDefs[paramIdx];
					auto genericParamInstance = genericTypeInst->mGenericParams[paramIdx];
					ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType());

					for (auto nameNode : genericParamDef->mNameNodes)
					{
						HandleTypeGenericParamRef(nameNode, typeDef, paramIdx);
					}
				}
			}
			else
			{
				auto genericExEntry = BuildGenericExtensionInfo(genericTypeInst, partialTypeDef);
				if (genericExEntry == NULL)
					continue;
				if (!genericTypeInst->IsUnspecializedType())
				{
					SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
					for (int paramIdx = 0; paramIdx < genericExEntry->mGenericParams.size(); paramIdx++)
					{
						auto genericParamInstance = genericExEntry->mGenericParams[paramIdx];
						BfGenericParamSource genericParamSource;
						genericParamSource.mCheckAccessibility = false;
						genericParamSource.mTypeInstance = genericTypeInst;
						BfError* error = NULL;
						if (!CheckGenericConstraints(genericParamSource, genericTypeInst->mTypeGenericArguments[paramIdx], NULL, genericParamInstance, NULL, &error))
						{
							genericExEntry->mConstraintsPassed = false;
						}
					}
				}
			}
		}
	}
	else
	{
		for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericParams.size(); paramIdx++)
		{			
			auto genericParamInstance = genericTypeInst->mGenericParams[paramIdx];
			ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType());
			auto genericParamDef = genericParamInstance->GetGenericParamDef();
			if (genericParamDef != NULL)
			{
				for (auto nameNode : genericParamDef->mNameNodes)
				{
					HandleTypeGenericParamRef(nameNode, typeDef, paramIdx);
				}
			}
		}
	}

	for (auto genericParam : genericTypeInst->mGenericParams)
	{
		for (auto constraintTypeInst : genericParam->mInterfaceConstraints)
			AddDependency(constraintTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
		if (genericParam->mTypeConstraint != NULL)
			AddDependency(genericParam->mTypeConstraint, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
	}

	return true;
}

bool BfModule::ValidateGenericConstraints(BfTypeReference* typeRef, BfGenericTypeInstance* genericTypeInst, bool ignoreErrors)
{
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsTypeAlias()))
	{
		// Don't validate constraints during the population of a concrete generic type alias instance, we want to
		//  throw those errors at the usage sites
		return true;
	}

	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, mIgnoreErrors || ignoreErrors);
	genericTypeInst->mValidatedGenericConstraints = true;

	if (genericTypeInst->IsTypeAlias())
	{
		auto underlyingType = genericTypeInst->GetUnderlyingType();
		if ((underlyingType != NULL) && (underlyingType->IsGenericTypeInstance()))
			return ValidateGenericConstraints(typeRef, (BfGenericTypeInstance*)underlyingType, ignoreErrors);
		return true;
	}

	auto typeDef = genericTypeInst->mTypeDef;
	for (int paramIdx = 0; paramIdx < (int)genericTypeInst->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = genericTypeInst->mGenericParams[paramIdx];
		// Why did we remove this line?  This breaks determining compatibility of one unspecialized type to another unspecialized type, called from ResolveTypeResult
		//if (!genericTypeInst->mIsUnspecialized)
		{
			BfError* error = NULL;
			if (!CheckGenericConstraints(BfGenericParamSource(genericTypeInst), genericTypeInst->mTypeGenericArguments[paramIdx], typeRef, genericParamInstance, NULL, &error))
			{
				genericTypeInst->mHadValidateErrors = true;
				return false;
			}
		}
	}
	return true;
}

bool BfModule::AreConstraintsSubset(BfGenericParamInstance* checkInner, BfGenericParamInstance* checkOuter)
{
	// Added new flags?
	if ((checkInner->mGenericParamFlags | checkOuter->mGenericParamFlags) != checkOuter->mGenericParamFlags)
	{
		// If the outer had a type flag and the inner has a specific type constraint, then see if those are compatible
		auto outerFlags = checkOuter->mGenericParamFlags;
		if (checkOuter->mTypeConstraint != NULL)
		{
			if (checkOuter->mTypeConstraint->IsStruct())
				outerFlags |= BfGenericParamFlag_Struct;
			else if (checkOuter->mTypeConstraint->IsStructOrStructPtr())
				outerFlags |= BfGenericParamFlag_StructPtr;
			else if (checkOuter->mTypeConstraint->IsObject())
				outerFlags |= BfGenericParamFlag_Class;
		}

		if ((checkInner->mGenericParamFlags | outerFlags) != outerFlags)
			return false;
	}

	if (checkInner->mTypeConstraint != NULL)
	{
		if (checkOuter->mTypeConstraint == NULL)
			return false;		
		if (!TypeIsSubTypeOf(checkInner->mTypeConstraint->ToTypeInstance(), checkOuter->mTypeConstraint->ToTypeInstance()))
			return false;		
	}
	
	for (auto& innerIFace : checkInner->mInterfaceConstraints)
	{		
		if (!checkOuter->mInterfaceConstraints.Contains(innerIFace))
			return false;
	}

	for (auto& innerOp : checkInner->mOperatorConstraints)
	{
		if (!checkOuter->mOperatorConstraints.Contains(innerOp))
			return false;
	}

	return true;
}

bool BfModule::CheckConstraintState(BfAstNode* refNode)
{
	if (mContext->mCurConstraintState == NULL)
		return true;

	auto checkState = mContext->mCurConstraintState->mPrevState;
	while (checkState != NULL)
	{
		if (*checkState == *mContext->mCurConstraintState)
		{
			if (refNode != NULL)
			{
				Fail("Constraints cause circular operator invocations", refNode);
			}

			return false;
		}

		checkState = checkState->mPrevState;
	}

	return true;
}

bool BfModule::ShouldAllowMultipleDefinitions(BfTypeInstance* typeInst, BfTypeDef* firstDeclaringTypeDef, BfTypeDef* secondDeclaringTypeDef)
{
	if (firstDeclaringTypeDef == secondDeclaringTypeDef)
		return false;

	// Since we will use shared debugging info, we won't be able to differentiate between these two fields.
	//  If we created per-target debug info then we could "fix" this.
	// Can these projects even see each other?
	if ((!firstDeclaringTypeDef->mProject->ContainsReference(secondDeclaringTypeDef->mProject)) &&
		(!secondDeclaringTypeDef->mProject->ContainsReference(firstDeclaringTypeDef->mProject)))
		return true;

	if (typeInst->IsUnspecializedType())
	{
		bool alwaysCoincide = true;

		auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
		if (genericTypeInst->mGenericExtensionInfo != NULL)
		{
			auto firstConstraints = genericTypeInst->GetGenericParamsVector(firstDeclaringTypeDef);
			auto secondConstraints = genericTypeInst->GetGenericParamsVector(secondDeclaringTypeDef);

			for (int genericIdx = 0; genericIdx < (int)firstConstraints->size(); genericIdx++)
			{
				auto firstConstraint = (*firstConstraints)[genericIdx];
				auto secondConstraint = (*secondConstraints)[genericIdx];

				if ((!AreConstraintsSubset(firstConstraint, secondConstraint)) &&
					(!AreConstraintsSubset(secondConstraint, firstConstraint)))
					alwaysCoincide = false;
			}
		}

		// Only show an error if we are certain both members will always appear at the same time
		if (!alwaysCoincide)
			return true;
	}

	return false;
}

void BfModule::CheckInjectNewRevision(BfTypeInstance* typeInstance)
{
	if ((typeInstance != NULL) && (typeInstance->mTypeDef != NULL))
	{
		if (typeInstance->mTypeDef->mNextRevision != NULL)
		{
			// It's possible that our main compiler thread is generating a new typedef while we're autocompleting. This handles that case...
			if (typeInstance->mDefineState == BfTypeDefineState_Undefined)
			{
				if (typeInstance->IsBoxed())
				{
					BfBoxedType* boxedType = (BfBoxedType*)typeInstance;
					BfTypeInstance* innerType = boxedType->mElementType->ToTypeInstance();
					PopulateType(innerType, BfPopulateType_Data);
				}
				else
				{
					mContext->HandleChangedTypeDef(typeInstance->mTypeDef);
					mSystem->InjectNewRevision(typeInstance->mTypeDef);
				}
			}
			else
			{
				BF_ASSERT(mCompiler->IsAutocomplete());
			}
		}
		if ((!typeInstance->IsDeleting()) && (!mCompiler->IsAutocomplete()))
			BF_ASSERT((typeInstance->mTypeDef->mDefState == BfTypeDef::DefState_Defined) || (typeInstance->mTypeDef->mDefState == BfTypeDef::DefState_New));
	}
}

bool BfModule::InitType(BfType* resolvedTypeRef, BfPopulateType populateType)
{
	BP_ZONE("BfModule::InitType");

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, resolvedTypeRef->ToTypeInstance());
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

	if (mCompiler->mHotState != NULL)
		mCompiler->mHotState->mHasNewTypes = true;

	auto typeInst = resolvedTypeRef->ToTypeInstance();
	if (typeInst != NULL)
	{
		CheckInjectNewRevision(typeInst);

		if (typeInst->mBaseType != NULL)
			BF_ASSERT((typeInst->mBaseType->mRebuildFlags & BfTypeRebuildFlag_Deleted) == 0);

		if ((typeInst->mTypeDef != NULL) && (typeInst->mTypeDef->mDefState == BfTypeDef::DefState_New) &&
			(typeInst->mTypeDef->mNextRevision == NULL))
		{
			mContext->HandleChangedTypeDef(typeInst->mTypeDef);
			typeInst->mTypeDef->mDefState = BfTypeDef::DefState_Defined;
		}

		typeInst->mIsReified = mIsReified;

		//BF_ASSERT(typeInst->mTypeDef->mTypeCode != BfTypeCode_Extension);

		if (resolvedTypeRef->IsTuple())
		{
			auto tupleType = (BfTupleType*)resolvedTypeRef;
			for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
			{
				auto fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
				if (fieldInstance->GetResolvedType()->IsUnspecializedType())
					tupleType->mHasUnspecializedMembers = true;
			}
		}
		
		typeInst->mRevision = mCompiler->mRevision;
		if (typeInst->mTypeDef != NULL)
			BF_ASSERT(typeInst->mTypeDef->mDefState != BfTypeDef::DefState_Deleted);
	}

	if (resolvedTypeRef->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfGenericTypeInstance*)resolvedTypeRef;
		for (auto typeGenericArg : genericTypeInst->mTypeGenericArguments)
			BF_ASSERT((typeGenericArg->mRebuildFlags & BfTypeRebuildFlag_Deleted) == 0);
	}

	if (!mContext->mSavedTypeDataMap.IsEmpty())
	{
		String typeName = BfSafeMangler::Mangle(resolvedTypeRef, this);
		BfSavedTypeData* savedTypeData;
		if (mContext->mSavedTypeDataMap.Remove(typeName, &savedTypeData))
		{
			// 			if (resolvedTypeRef->mTypeId != -1)
			// 			{
			// 				// If we have an ID and it as the last one assigned the roll back the ID counter
			// 				if (resolvedTypeRef->mTypeId == mCompiler->mCurTypeId - 1)
			// 					mCompiler->mCurTypeId--;
			// 			}

			mContext->mSavedTypeData[savedTypeData->mTypeId] = NULL;

			resolvedTypeRef->mTypeId = savedTypeData->mTypeId;
			BfLogSysM("Using mSavedTypeData for %p %s\n", resolvedTypeRef, typeName.c_str());
			if (typeInst != NULL)
			{
				if (mCompiler->IsHotCompile())
				{
					BfLogSysM("Using mSavedTypeData HotTypeData %p for %p\n", savedTypeData->mHotTypeData, resolvedTypeRef);
					typeInst->mHotTypeData = savedTypeData->mHotTypeData;
					savedTypeData->mHotTypeData = NULL;
				}				
			}			
			delete savedTypeData;
			mContext->mTypes[resolvedTypeRef->mTypeId] = resolvedTypeRef;
		}
		else
		{
			BfLogSysM("No mSavedTypeData entry for %p %s\n", resolvedTypeRef, typeName.c_str());
		}
	}

	resolvedTypeRef->mContext = mContext;
	if (resolvedTypeRef->IsGenericTypeInstance())
	{
		auto genericTypeInstance = (BfGenericTypeInstance*)resolvedTypeRef;				

		// Do it here so the location we attempted to specialize this type will throw the failure if there is one
		if (!BuildGenericParams(resolvedTypeRef))
			return false;
	}

	BfLogSysM("%p InitType: %s Type: %p TypeDef: %p Revision:%d\n", mContext, TypeToString(resolvedTypeRef).c_str(), resolvedTypeRef, (typeInst != NULL) ? typeInst->mTypeDef : NULL, mCompiler->mRevision);

	// When we're autocomplete, we can't do the method processing so we have to add this type to the type work list
	if (((populateType < BfPopulateType_Full) || (mCompiler->IsAutocomplete())) /*&& (!resolvedTypeRef->IsUnspecializedTypeVariation())*/ && (resolvedTypeRef->IsTypeInstance()) &&
		(!resolvedTypeRef->IsTypeAlias()))
	{
		BfTypeProcessRequest* typeProcessRequest = mContext->mPopulateTypeWorkList.Alloc();
		typeProcessRequest->mType = resolvedTypeRef;
		BF_ASSERT(resolvedTypeRef->mContext == mContext);		
		mCompiler->mStats.mTypesQueued++;
		mCompiler->UpdateCompletion();
	}
	return PopulateType(resolvedTypeRef, populateType);
}

void BfModule::AddFieldDependency(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfType* fieldType)
{
	auto fieldTypeInstance = fieldType->ToTypeInstance();

	if (fieldTypeInstance == NULL)
	{
		auto underlyingType = fieldType->GetUnderlyingType();
		if (underlyingType != NULL)
			AddFieldDependency(typeInstance, fieldInstance, underlyingType);
		return;
	}

	auto depFlag = fieldTypeInstance->IsValueType() ? BfDependencyMap::DependencyFlag_ValueTypeMemberData : BfDependencyMap::DependencyFlag_PtrMemberData;
	AddDependency(fieldTypeInstance, typeInstance, depFlag);

	if ((fieldTypeInstance->IsStruct()) && (fieldTypeInstance->IsGenericTypeInstance()))
	{
		// When we're a generic struct, our data layout can depend on our generic parameters as well
		auto genericTypeInstance = (BfGenericTypeInstance*)fieldTypeInstance;
		for (auto typeGenericArg : genericTypeInstance->mTypeGenericArguments)
			AddFieldDependency(typeInstance, fieldInstance, typeGenericArg);
	}
}

void BfModule::CheckMemberNames(BfTypeInstance* typeInst)
{
	struct MemberRef
	{
		BfMemberDef* mMemberDef;
		String mName;
		String mKindName;
		BfTypeInstance* mTypeInst;
		BfAstNode* mNameNode;
		BfProtection mProtection;
		BfTypeDef* mDeclaringType;		
		bool mIsOverride;
	};

	SizedArray<MemberRef, 64> memberList;
	
	// Check base types first and then current type
	auto checkType = typeInst;
	while (checkType != NULL)
	{
		for (auto prop : checkType->mTypeDef->mProperties)
		{
			BfPropertyDeclaration* propDecl = (BfPropertyDeclaration*)prop->mFieldDeclaration;
			if ((propDecl != NULL) && (propDecl->mExplicitInterface != NULL))
				continue;

			if (!typeInst->IsTypeMemberIncluded(prop->mDeclaringType))
				continue;

			MemberRef memberRef;
			memberRef.mMemberDef = prop;
			memberRef.mTypeInst = checkType;
			memberRef.mProtection = prop->mProtection;
			memberRef.mName = prop->mName;
			memberRef.mKindName = "property";
			if (prop->mFieldDeclaration != NULL)
				memberRef.mNameNode = prop->mFieldDeclaration->mNameNode;
			memberRef.mDeclaringType = prop->mDeclaringType;
			auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(prop->mFieldDeclaration);
			if (propertyDeclaration != NULL)
				memberRef.mIsOverride = (propertyDeclaration->mNewSpecifier != NULL) ||
					((propertyDeclaration->mVirtualSpecifier != NULL) && (propertyDeclaration->mVirtualSpecifier->GetToken() == BfToken_Override));
			memberList.push_back(memberRef);
		}

		for (auto field : checkType->mTypeDef->mFields)
		{
			if (!typeInst->IsTypeMemberIncluded(field->mDeclaringType))
				continue;

			MemberRef memberRef;
			memberRef.mMemberDef = field;
			memberRef.mTypeInst = checkType;
			memberRef.mProtection = field->mProtection;
			memberRef.mName = field->mName;
			memberRef.mKindName = "field";
			memberRef.mDeclaringType = field->mDeclaringType;
			if (field->mFieldDeclaration != NULL)
			{
				memberRef.mNameNode = field->mFieldDeclaration->mNameNode;
				memberRef.mIsOverride = field->mFieldDeclaration->mNewSpecifier != NULL;
			}
			memberList.push_back(memberRef);
		}

		checkType = checkType->mBaseType;
	}

	Dictionary<String, MemberRef> memberMap;
	memberMap.Reserve(memberList.size());

	for (int i = (int)memberList.size() - 1; i >= 0; i--)
	{
		MemberRef& memberRef = memberList[i];
		if (memberRef.mName.empty())
			continue;
		if ((memberRef.mTypeInst == typeInst) && (!memberRef.mIsOverride))
		{
			MemberRef* prevMemberRef = NULL;
			if (memberMap.TryGetValue(memberRef.mName, &prevMemberRef))
			{
				//auto& prevMemberRef = itr->second;

				MemberRef* firstMemberRef = &memberRef;
				MemberRef* secondMemberRef = prevMemberRef;
				bool showPrevious = false;

				BfError* error = NULL;
				if (prevMemberRef->mTypeInst != typeInst)
				{
					if ((prevMemberRef->mProtection != BfProtection_Private) && (memberRef.mNameNode != NULL))
					{
						error = Warn(BfWarning_CS0108_MemberHidesInherited, StrFormat("%s hides inherited member '%s'. Use the 'new' keyword if hiding was intentional.", prevMemberRef->mKindName.c_str(), memberRef.mName.c_str()), memberRef.mNameNode, true);
						showPrevious = true;
					}
				}
				else
				{
					if (ShouldAllowMultipleDefinitions(typeInst, firstMemberRef->mDeclaringType, secondMemberRef->mDeclaringType))
					{
						if (firstMemberRef->mMemberDef != NULL)
						{
							firstMemberRef->mMemberDef->mHasMultiDefs = true;
							secondMemberRef->mMemberDef->mHasMultiDefs = true;
						}

						continue;
					}

					bool wantsSwap = false;

					if ((secondMemberRef->mNameNode != NULL) && (firstMemberRef->mNameNode != NULL) &&
						(secondMemberRef->mNameNode->GetSourceData() == firstMemberRef->mNameNode->GetSourceData()) &&
						(secondMemberRef->mNameNode->GetSrcStart() < firstMemberRef->mNameNode->GetSrcStart()))
					{
						wantsSwap = true;
					}

					if (secondMemberRef->mDeclaringType->IsExtension() != firstMemberRef->mDeclaringType->IsExtension())
					{
						wantsSwap = firstMemberRef->mDeclaringType->IsExtension();
					}

					if (wantsSwap)
					{
						std::swap(firstMemberRef, secondMemberRef);
					}

					if (secondMemberRef->mNameNode != NULL)
						error = Fail(StrFormat("A %s named '%s' has already been declared.", secondMemberRef->mKindName.c_str(), memberRef.mName.c_str()), secondMemberRef->mNameNode, true);
					showPrevious = true;
				}
				if ((secondMemberRef->mNameNode != NULL) && (error != NULL))
					mCompiler->mPassInstance->MoreInfo("Previous declaration", firstMemberRef->mNameNode);
			}
		}
		memberMap.TryAdd(memberRef.mName, memberRef);
	}
}

void BfModule::TypeFailed(BfTypeInstance* typeInstance)
{
	BfLogSysM("TypeFailed: %p\n", typeInstance);
	typeInstance->mTypeFailed = true;
	// Punt on field types - just substitute 'var' where we have NULLs
	for (auto& fieldInstance : typeInstance->mFieldInstances)
	{		
		if ((fieldInstance.mResolvedType == NULL) || (fieldInstance.mResolvedType->IsNull()))
		{
			if (fieldInstance.mDataIdx >= 0)
				fieldInstance.mResolvedType = GetPrimitiveType(BfTypeCode_Var);
		}
		if (fieldInstance.mOwner == NULL)
			fieldInstance.mOwner = typeInstance;
	}
	if (typeInstance->mAlign == -1)
		typeInstance->mAlign = 1;
	if (typeInstance->mSize == -1)
		typeInstance->mSize = 1;
	mContext->mFailTypes.Add(typeInstance);
	mHadBuildError = true;
}

bool BfModule::CheckCircularDataError()
{
	bool hadError = false;

	int checkIdx = 0;
	auto checkTypeState = mContext->mCurTypeState;
	bool isPreBaseCheck = checkTypeState->mPopulateType == BfPopulateType_Declaration;
	while (true)
	{
		if (checkTypeState == NULL)
			return hadError;

		if (isPreBaseCheck)
		{
			if (checkTypeState->mPopulateType != BfPopulateType_Declaration)
				return hadError;
		}
		else
		{
			if (checkTypeState->mPopulateType == BfPopulateType_Declaration)
				return hadError;
			if ((checkIdx > 0) && (checkTypeState->mCurBaseTypeRef == NULL) && (checkTypeState->mCurAttributeTypeRef == NULL) && (checkTypeState->mCurFieldDef == NULL))
				return hadError;
		}

		if ((checkTypeState->mTypeInstance == mCurTypeInstance) && (checkIdx > 0))
			break;
		checkTypeState = checkTypeState->mPrevState;
		checkIdx++;
	}


	checkTypeState = mContext->mCurTypeState->mPrevState;
	while (true)
	{
		if (checkTypeState == NULL)
			return hadError;
		if ((checkTypeState->mCurAttributeTypeRef == NULL) && (checkTypeState->mCurBaseTypeRef == NULL) && (checkTypeState->mCurFieldDef == NULL))
			return hadError;

		// We only get one chance to fire off these errors, they can't be ignored.
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, false);

		hadError = true;
		if (checkTypeState->mCurAttributeTypeRef != NULL)
		{
			Fail(StrFormat("Attribute type '%s' causes a data cycle", BfTypeUtils::TypeToString(checkTypeState->mCurAttributeTypeRef).c_str()), checkTypeState->mCurAttributeTypeRef, true);
		}
		else if (checkTypeState->mCurBaseTypeRef != NULL)
		{
			Fail(StrFormat("Base type '%s' causes a data cycle", BfTypeUtils::TypeToString(checkTypeState->mCurBaseTypeRef).c_str()), checkTypeState->mCurBaseTypeRef, true);
		}
		else if (checkTypeState->mCurFieldDef->mFieldDeclaration != NULL)
		{
			Fail(StrFormat("Field '%s.%s' causes a data cycle", TypeToString(checkTypeState->mTypeInstance).c_str(), checkTypeState->mCurFieldDef->mName.c_str()),
				checkTypeState->mCurFieldDef->mFieldDeclaration->mTypeRef, true);
		}
		else
		{
			Fail(StrFormat("Field '%s.%s' causes a data cycle", TypeToString(checkTypeState->mTypeInstance).c_str(), checkTypeState->mCurFieldDef->mName.c_str()));
		}

		auto module = GetModuleFor(checkTypeState->mTypeInstance);
		if (module != NULL)
			module->TypeFailed(checkTypeState->mTypeInstance);
		else
			checkTypeState->mTypeInstance->mTypeFailed = true;

		checkTypeState = checkTypeState->mPrevState;
	}
}

bool BfModule::PopulateType(BfType* resolvedTypeRef, BfPopulateType populateType)
{		
	if ((populateType == BfPopulateType_Declaration) && (resolvedTypeRef->mDefineState >= BfTypeDefineState_Declared))
		return true;
	
	// Are we "demanding" to reify a type that is currently resolve-only?
	if (mIsReified)
	{
		if (resolvedTypeRef->IsTypeInstance())
		{
			auto typeModule = resolvedTypeRef->GetModule();
			if ((typeModule != NULL) && (typeModule->mIsSpecialModule))
			{
				auto typeInst = resolvedTypeRef->ToTypeInstance();
				if (!typeInst->mIsReified)
				{
					BfLogSysM("Reifying type %p in scratch module in PopulateType\n", resolvedTypeRef);

					// It's important for unspecialized types to be in the correct module --
					//  when we process their methods, new types will be determined as 
					//  resolve-only or reified based on the module the unresolved type is in
					BF_ASSERT(typeInst->mModule == mContext->mUnreifiedModule);
					typeInst->mIsReified = true;
					typeInst->mModule = mContext->mScratchModule;

					// Why did we need to do this at all? Why is just marking the type as reified not enough?					
					//  This causes issues where we may delete a method instance that is currently being used as the generic bindings for 
					//  a method of a specialized generic type
// 					if (typeInst->IsOnDemand())
// 					{
// 						RebuildMethods(typeInst);
// 					}
// 					else
// 						mContext->RebuildType(typeInst, false, false);					
				}
			}
			else
			{				
				if ((typeModule != NULL) && (!typeModule->mIsReified) && (!typeModule->mReifyQueued))
				{
					BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));

					BfLogSysM("Queued reification of type %p in module %p in PopulateType\n", resolvedTypeRef, typeModule);

					BF_ASSERT(!typeModule->mIsSpecialModule);
					// This caused issues - we may need to reify a type and then request a method
					typeModule->mReifyQueued = true;
					mContext->mReifyModuleWorkList.Add(typeModule);
	                //typeModule->ReifyModule();
				}
			}
		}
	}

	if (!resolvedTypeRef->IsIncomplete())
		return true;

	auto typeInstance = resolvedTypeRef->ToTypeInstance();
	CheckInjectNewRevision(typeInstance);	

	BF_ASSERT((resolvedTypeRef->mRebuildFlags & (BfTypeRebuildFlag_Deleted | BfTypeRebuildFlag_DeleteQueued)) == 0);

	/*BfTypeRebuildFlags allowedFlags = (BfTypeRebuildFlags)(BfTypeRebuildFlag_AddedToWorkList | BfTypeRebuildFlag_AwaitingReference | BfTypeRebuildFlag_UnderlyingTypeDeferred);
	if ((resolvedTypeRef->mRebuildFlags & ~allowedFlags) != 0)
	{
		// BfContext::UpdateAfterDeletingTypes should clear out all flags except for the Deleted flag
		// If this type was deleted then we should never be able to reach PopulateType here.
		//  This may happen if dependent types were not properly rebuilt when a used type
		//  was deleted.
		auto hadFlags = resolvedTypeRef->mRebuildFlags;
		BF_ASSERT((resolvedTypeRef->mRebuildFlags & ~allowedFlags) == 0);
		resolvedTypeRef->mRebuildFlags = (BfTypeRebuildFlags)(resolvedTypeRef->mRebuildFlags & ~allowedFlags);
	}*/

	bool isNew = resolvedTypeRef->mDefineState == BfTypeDefineState_Undefined;
	if (isNew)
	{
		BP_ZONE("BfModule::PopulateType");
		if (resolvedTypeRef->mTypeId == -1)
		{
			mCompiler->mTypeInitCount++;

			auto typeInstance = resolvedTypeRef->ToTypeInstance();

			if (!mCompiler->mTypeIdFreeList.IsEmpty())
			{
				resolvedTypeRef->mTypeId = mCompiler->mTypeIdFreeList.back();
				mCompiler->mTypeIdFreeList.pop_back();
			}
			else
				resolvedTypeRef->mTypeId = mCompiler->mCurTypeId++;

			while (resolvedTypeRef->mTypeId >= (int)mContext->mTypes.size())
				mContext->mTypes.Add(NULL);
			mContext->mTypes[resolvedTypeRef->mTypeId] = resolvedTypeRef;

			if (typeInstance != NULL)
			{
				typeInstance->mSignatureRevision = mCompiler->mRevision;
				typeInstance->mLastNonGenericUsedRevision = mCompiler->mRevision;
			}
		}

		BfLogSysM("PopulateType: %p %s populateType:%d ResolveOnly:%d Reified:%d AutoComplete:%d Ctx:%p Mod:%p TypeId:%d\n", resolvedTypeRef, TypeToString(resolvedTypeRef, BfTypeNameFlags_None).c_str(), populateType, mCompiler->mIsResolveOnly, mIsReified, mCompiler->IsAutocomplete(), mContext, this, resolvedTypeRef->mTypeId);

		BF_ASSERT(!resolvedTypeRef->IsDeleting());
	}

	if (resolvedTypeRef->IsRef())
	{
		BfRefType* refType = (BfRefType*)resolvedTypeRef;
		if (refType->mElementType->IsValueType())
		{
			PopulateType(refType->mElementType, populateType);
			resolvedTypeRef->mDefineState = refType->mElementType->mDefineState;
		}
		else
		{
			PopulateType(refType->mElementType, BfPopulateType_Identity);
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
		}
		refType->mSize = refType->mAlign = mSystem->mPtrSize;
		return true;
	}

	if (resolvedTypeRef->IsTypeAlias())
	{
		auto typeAlias = (BfTypeInstance*)resolvedTypeRef;
		SetAndRestoreValue<BfTypeInstance*> prevCurType(mCurTypeInstance, typeAlias);
		auto typeDef = typeAlias->mTypeDef;
		auto typeAliasDecl = (BfTypeAliasDeclaration*)typeDef->mTypeDeclaration;
		BfType* aliasToType = NULL;

		BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
		typeState.mPopulateType = populateType;
		typeState.mCurBaseTypeRef = typeAliasDecl->mAliasToType;
		SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
		if (!CheckCircularDataError())
		{
			if (typeAliasDecl->mAliasToType != NULL)
				aliasToType = ResolveTypeRef(typeAliasDecl->mAliasToType, BfPopulateType_IdentityNoRemapAlias);
		}
		//typeAlias->mModule = mContext->mScratchModule;
		typeAlias->mTypeIncomplete = false;
		typeAlias->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
		if (aliasToType != NULL)
		{
			AddDependency(aliasToType, typeAlias, BfDependencyMap::DependencyFlag_DerivedFrom);
		}
		else
			mContext->mFailTypes.Add(typeAlias);

		if (typeAlias->mTypeFailed)
			aliasToType = NULL;

		if (resolvedTypeRef->IsGenericTypeInstance())
			((BfGenericTypeAliasType*)resolvedTypeRef)->mAliasToType = aliasToType;
		else
			((BfTypeAliasType*)resolvedTypeRef)->mAliasToType = aliasToType;

		if (aliasToType != NULL)
		{
			resolvedTypeRef->mSize = aliasToType->mSize;
			resolvedTypeRef->mAlign = aliasToType->mAlign;

			if (auto aliasToTypeInst = aliasToType->ToTypeInstance())
			{
				typeAlias->mInstSize = aliasToTypeInst->mInstSize;
				typeAlias->mInstAlign = aliasToTypeInst->mInstAlign;
			}
			else
			{
				typeAlias->mInstSize = aliasToType->mSize;
				typeAlias->mInstAlign = aliasToType->mAlign;
			}
		}
		else
		{
			resolvedTypeRef->mSize = 0;
			resolvedTypeRef->mAlign = 1;
			typeAlias->mInstSize = 0;
			typeAlias->mInstAlign = 1;
		}
		resolvedTypeRef->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
		resolvedTypeRef->mRebuildFlags = BfTypeRebuildFlag_None;

		return true;
	}

	if (resolvedTypeRef->IsSizedArray())
	{		
		resolvedTypeRef->mRevision = mRevision;

		BfSizedArrayType* arrayType = (BfSizedArrayType*)resolvedTypeRef;
		auto elementType = arrayType->mElementType;
		if (elementType->IsValueType())
		{			
			PopulateType(arrayType->mElementType, BfPopulateType_Data);
			resolvedTypeRef->mDefineState = arrayType->mElementType->mDefineState;
			AddDependency(elementType, resolvedTypeRef, BfDependencyMap::DependencyFlag_ValueTypeMemberData);							
		}
		else
		{
			PopulateType(arrayType->mElementType, BfPopulateType_Identity);
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			AddDependency(elementType, resolvedTypeRef, BfDependencyMap::DependencyFlag_PtrMemberData);
		}
		if (arrayType->mElementCount > 0)
		{
			arrayType->mSize = (arrayType->mElementType->GetStride() * ((int)arrayType->mElementCount - 1)) + arrayType->mElementType->mSize;
			arrayType->mAlign = std::max((int32)arrayType->mElementType->mAlign, 1);
		}
		else
		{
			arrayType->mSize = 0;
			arrayType->mAlign = 1;
		}

		arrayType->mWantsGCMarking = elementType->WantsGCMarking();
		resolvedTypeRef->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
		resolvedTypeRef->mRebuildFlags = BfTypeRebuildFlag_None;

		bool isValueless = arrayType->IsValuelessType();
		return true;
	}	
	
	if (isNew)
	{
		BfTypeDef* typeDef = NULL;
		if (typeInstance != NULL)
		{
			if ((populateType == BfPopulateType_Data) && (typeInstance->mNeedsMethodProcessing))
				return true;
			typeDef = typeInstance->mTypeDef;
		}

		if (resolvedTypeRef->IsMethodRef())
			return true;

		if (resolvedTypeRef->IsPointer())
		{
			BfPointerType* pointerType = (BfPointerType*)resolvedTypeRef;
			if (pointerType->mElementType->IsIncomplete())
				PopulateType(pointerType->mElementType, BfPopulateType_Declaration);
			pointerType->mSize = pointerType->mAlign = mSystem->mPtrSize;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		}

		if (resolvedTypeRef->IsGenericParam())
		{
			BfGenericParamType* genericParamType = (BfGenericParamType*)resolvedTypeRef;
			PopulateType(mContext->mBfObjectType);
			genericParamType->mSize = mContext->mBfObjectType->mSize;
			genericParamType->mAlign = mContext->mBfObjectType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		}

		if (resolvedTypeRef->IsRetTypeType())
		{
			BfRetTypeType* retTypeType = (BfRetTypeType*)resolvedTypeRef;
			BF_ASSERT(retTypeType->mElementType->IsGenericParam());
			resolvedTypeRef->mSize = mContext->mBfObjectType->mSize;
			resolvedTypeRef->mAlign = mContext->mBfObjectType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		}

		if (resolvedTypeRef->IsConcreteInterfaceType())
		{
			BfConcreteInterfaceType* concreteInterfaceType = (BfConcreteInterfaceType*)resolvedTypeRef;
			BF_ASSERT(concreteInterfaceType->mInterface->IsInterface());
			resolvedTypeRef->mSize = concreteInterfaceType->mInterface->mSize;
			resolvedTypeRef->mAlign = concreteInterfaceType->mInterface->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		}

		if (resolvedTypeRef->IsConstExprValue())
		{
			resolvedTypeRef->mSize = 0;
			resolvedTypeRef->mAlign = 0;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		}
		
		// The autocomplete pass doesn't need to do the method processing, allow type to be (partially) incomplete
		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) &&
			(typeInstance != NULL) && (typeInstance->mNeedsMethodProcessing) && (!typeInstance->IsDelegate()))
			return true;

		BfPrimitiveType* primitiveType = NULL;
		if (typeInstance == NULL)
		{
			BF_ASSERT(resolvedTypeRef->IsPrimitiveType());
			primitiveType = (BfPrimitiveType*)resolvedTypeRef;
			typeDef = primitiveType->mTypeDef;
		}

#define PRIMITIVE_TYPE(name, llvmType, size, dType) \
			primitiveType->mSize = primitiveType->mAlign = size; \
			primitiveType->mDefineState = BfTypeDefineState_Defined;

		switch (typeDef->mTypeCode)
		{
		case BfTypeCode_None:
			primitiveType->mSize = primitiveType->mAlign = 0;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return true;
		case BfTypeCode_Self:
		case BfTypeCode_Dot:
		case BfTypeCode_Var:
		case BfTypeCode_Let:
		{
			auto objType = mContext->mBfObjectType;
			primitiveType->mSize = objType->mSize;
			primitiveType->mAlign = objType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
		}
		return true;
		case BfTypeCode_NullPtr:
			primitiveType->mSize = primitiveType->mAlign = mSystem->mPtrSize;
			primitiveType->mDefineState = BfTypeDefineState_Defined;
			return true;
		case BfTypeCode_Boolean:
			PRIMITIVE_TYPE("bool", Int1, 1, DW_ATE_boolean);
			return true;
		case BfTypeCode_Int8:
			PRIMITIVE_TYPE("sbyte", Int8, 1, DW_ATE_signed);
			return true;
		case BfTypeCode_UInt8:
			PRIMITIVE_TYPE("byte", Int8, 1, DW_ATE_unsigned);
			return true;
		case BfTypeCode_Int16:
			PRIMITIVE_TYPE("short", Int16, 2, DW_ATE_signed);
			return true;
		case BfTypeCode_UInt16:
			PRIMITIVE_TYPE("ushort", Int16, 2, DW_ATE_unsigned);
			return true;
		case BfTypeCode_Int32:
			PRIMITIVE_TYPE("int", Int32, 4, DW_ATE_signed);
			return true;
		case BfTypeCode_UInt32:
			PRIMITIVE_TYPE("uint", Int32, 4, DW_ATE_unsigned);
			return true;
		case BfTypeCode_Int64:
			PRIMITIVE_TYPE("long", Int64, 8, DW_ATE_signed);
			return true;
		case BfTypeCode_UInt64:
			PRIMITIVE_TYPE("ulong", Int64, 8, DW_ATE_unsigned);
			return true;
		case BfTypeCode_IntPtr:
			if (mSystem->mPtrSize == 4)
			{
				PRIMITIVE_TYPE("intptr", Int32, 4, DW_ATE_signed);
			}
			else
			{
				PRIMITIVE_TYPE("intptr", Int64, 8, DW_ATE_signed);
			}
			return true;
		case BfTypeCode_UIntPtr:
			if (mSystem->mPtrSize == 4)
			{
				PRIMITIVE_TYPE("uintptr", Int32, 4, DW_ATE_unsigned);
			}
			else
			{
				PRIMITIVE_TYPE("uintptr", Int64, 8, DW_ATE_unsigned);
			}
			return true;
		case BfTypeCode_IntUnknown:			
		case BfTypeCode_UIntUnknown:
			return true;
		case BfTypeCode_Char8:
			PRIMITIVE_TYPE("char8", Int8, 1, DW_ATE_unsigned_char);
			return true;
		case BfTypeCode_Char16:
			PRIMITIVE_TYPE("char16", Int16, 2, DW_ATE_unsigned_char);
			return true;
		case BfTypeCode_Char32:
			PRIMITIVE_TYPE("char32", Int32, 4, DW_ATE_unsigned_char);
			return true;
		case BfTypeCode_Single:
			PRIMITIVE_TYPE("float", Float, 4, DW_ATE_float);
			return true;
		case BfTypeCode_Double:
			PRIMITIVE_TYPE("double", Double, 8, DW_ATE_float);
			return true;
		case BfTypeCode_Object:
		case BfTypeCode_Struct:
		case BfTypeCode_Interface:
		case BfTypeCode_Enum:		
			// Implemented below
			break;
		case BfTypeCode_Extension:
			// This can only happen if we didn't actually find the type the extension referred to			
			break;
		default:
			//NotImpl(resolvedTypeRef->mTypeRef);
			BF_FATAL("Invalid type");
			return false;
		}
		//////////////////////////////////////////////////////////////////////////				

		BF_ASSERT(typeInstance != NULL);

		if (!typeInstance->IsArray())
		{			
			BF_ASSERT(typeInstance->mTypeDef != mContext->mCompiler->mArray1TypeDef);
		}

		if (mContext->mBfObjectType == NULL)
		{
			if (typeInstance->mTypeDef == mCompiler->mBfObjectTypeDef)
				mContext->mBfObjectType = typeInstance;
			else
				ResolveTypeDef(mCompiler->mBfObjectTypeDef);
		}

		if (typeInstance->mModule == NULL)
		{
			// Create a module for this type
			mContext->HandleTypeWorkItem(resolvedTypeRef);
		}
	}

	if (typeInstance == NULL)
		return true;

	auto result = typeInstance->mModule->DoPopulateType(typeInstance, populateType);	
	return result;
}

int BfModule::GenerateTypeOptions(BfCustomAttributes* customAttributes, BfTypeInstance* typeInstance, bool checkTypeName)
{
	if (mContext->mSystem->mTypeOptions.size() == 0)
	{
		return -1;
	}

	Array<int> matchedIndices;

	if ((!checkTypeName) && (typeInstance->mTypeOptionsIdx != -1))
	{
		// Methods should 'inherit' the owner's type options before applying type options from custom attributes
		auto typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);
		if (typeOptions->mMatchedIndices.size() == 0)
			matchedIndices.push_back(typeInstance->mTypeOptionsIdx);
		else
			matchedIndices = typeOptions->mMatchedIndices;
	}

	if (customAttributes != NULL)
	{
		if (!mCompiler->mAttributeTypeOptionMap.IsEmpty())
		{
			StringT<128> attrName;
			for (auto& customAttrs : customAttributes->mAttributes)
			{
				attrName.Clear();
				customAttrs.mType->mTypeDef->mFullName.ToString(attrName);
				Array<int>* arrPtr;
				if (mCompiler->mAttributeTypeOptionMap.TryGetValue(attrName, &arrPtr))
				{
					for (auto optionsIdx : *arrPtr)
					{
						matchedIndices.Add(optionsIdx);
					}
				}
			}
		}
	}

	int typeOptionsCount = (int)mContext->mSystem->mTypeOptions.size();
	if (checkTypeName)
	{
		auto _CheckTypeName = [&](const StringImpl& typeName)
		{
			for (int optionIdx = 0; optionIdx < (int)mContext->mSystem->mTypeOptions.size(); optionIdx++)
			{
				auto& typeOptions = mContext->mSystem->mTypeOptions[optionIdx];

				bool matched = false;
				for (auto& filter : typeOptions.mTypeFilters)
				{
					int filterIdx = 0;
					int typeNameIdx = 0;

					const char* filterPtr = filter.c_str();
					const char* namePtr = typeName.c_str();

					char prevFilterC = 0;
					while (true)
					{
						char filterC;
						while (true)
						{
							filterC = *(filterPtr++);
							if (filterC != ' ')
								break;
						}

						char nameC;
						while (true)
						{
							nameC = *(namePtr++);
							if (nameC != ' ')
								break;
						}

						if ((filterC == 0) || (nameC == 0))
						{
							matched = (filterC == 0) && (nameC == 0);
							break;
						}

						bool doWildcard = false;

						if (nameC != filterC)
						{
							if (filterC == '*')
								doWildcard = true;
							else if (((filterC == ',') || (filterC == '>')) &&
								((prevFilterC == '<') || (prevFilterC == ',')))
							{
								doWildcard = true;
								filterPtr--;
							}

							if (!doWildcard)
							{
								matched = false;
								break;
							}
						}

						if (doWildcard)
						{
							int openDepth = 0;

							const char* startNamePtr = namePtr;

							while (true)
							{
								nameC = *(namePtr++);
								if (nameC == 0)
								{
									namePtr--;
									if (openDepth != 0)
										matched = false;
									break;
								}
								if ((nameC == '>') && (openDepth == 0))
								{
									namePtr--;
									break;
								}

								if (nameC == '<')
									openDepth++;
								else if (nameC == '>')
									openDepth--;
								else if ((nameC == ',') && (openDepth == 0))
								{
									namePtr--;
									break;
								}
							}

							if (!matched)
								break;
						}

						prevFilterC = filterC;
					}
				}

				if (matched)
				 	matchedIndices.push_back(optionIdx);
			}
		};

		if (typeInstance->IsTypedPrimitive())
		{
			auto underlyingType = typeInstance->GetUnderlyingType();
			String typeName = TypeToString(underlyingType);
			_CheckTypeName(typeName);
		}

		if ((!typeInstance->IsBoxed()) && (typeInstance->mTypeDef == mCompiler->mPointerTTypeDef))
		{
			BF_ASSERT(typeInstance->IsGenericTypeInstance());
			auto innerType = ((BfGenericTypeInstance*)typeInstance)->mTypeGenericArguments[0];
			auto ptrType = CreatePointerType(innerType);
			String typeName = TypeToString(ptrType);
			_CheckTypeName(typeName);
		}

		String typeName = TypeToString(typeInstance);
		_CheckTypeName(typeName);		
	}

	int matchedIdx = -1;
	if (matchedIndices.size() == 1)
	{
		matchedIdx = matchedIndices[0];
	}
	else if (matchedIndices.size() > 1)
	{
		// Try to find a merged typeoptions with these indices
		for (int mergedIdx = 0; mergedIdx < (int)mContext->mSystem->mMergedTypeOptions.size(); mergedIdx++)
		{
			auto& typeOptions = mContext->mSystem->mMergedTypeOptions[mergedIdx];
			if (typeOptions.mMatchedIndices == matchedIndices)
			{
				matchedIdx = typeOptionsCount + mergedIdx;
				break;
			}
		}

		// Otherwise make one...
		if (matchedIdx == -1)
		{
			auto& first = mContext->mSystem->mTypeOptions[matchedIndices[0]];
			BfTypeOptions mergedTypeOptions;
			mergedTypeOptions.mSIMDSetting = first.mSIMDSetting;
			mergedTypeOptions.mOptimizationLevel = first.mOptimizationLevel;
			mergedTypeOptions.mEmitDebugInfo = first.mEmitDebugInfo;
			mergedTypeOptions.mRuntimeChecks = first.mRuntimeChecks;
			mergedTypeOptions.mInitLocalVariables = first.mInitLocalVariables;
			mergedTypeOptions.mEmitDynamicCastCheck = first.mEmitDynamicCastCheck;
			mergedTypeOptions.mEmitObjectAccessCheck = first.mEmitObjectAccessCheck;
			mergedTypeOptions.mAllocStackTraceDepth = first.mAllocStackTraceDepth;			

			mergedTypeOptions.mMatchedIndices = matchedIndices;
			for (int idx = 1; idx < (int)matchedIndices.size(); idx++)
			{
				auto& typeOptions = mContext->mSystem->mTypeOptions[matchedIndices[idx]];
				if (typeOptions.mSIMDSetting != -1)
					mergedTypeOptions.mSIMDSetting = typeOptions.mSIMDSetting;
				if (typeOptions.mOptimizationLevel != -1)
					mergedTypeOptions.mOptimizationLevel = typeOptions.mOptimizationLevel;
				if (typeOptions.mEmitDebugInfo != -1)
					mergedTypeOptions.mEmitDebugInfo = typeOptions.mEmitDebugInfo;
				if (typeOptions.mRuntimeChecks != BfOptionalBool_NotSet)
					mergedTypeOptions.mRuntimeChecks = typeOptions.mRuntimeChecks;
				if (typeOptions.mInitLocalVariables != BfOptionalBool_NotSet)
					mergedTypeOptions.mInitLocalVariables = typeOptions.mInitLocalVariables;
				if (typeOptions.mEmitDynamicCastCheck != BfOptionalBool_NotSet)
					mergedTypeOptions.mEmitDynamicCastCheck = typeOptions.mEmitDynamicCastCheck;
				if (typeOptions.mEmitObjectAccessCheck != BfOptionalBool_NotSet)
					mergedTypeOptions.mEmitObjectAccessCheck = typeOptions.mEmitObjectAccessCheck;
				if (typeOptions.mAllocStackTraceDepth != -1)
					mergedTypeOptions.mAllocStackTraceDepth = typeOptions.mAllocStackTraceDepth;
			}
			matchedIdx = typeOptionsCount + (int)mContext->mSystem->mMergedTypeOptions.size();
			mContext->mSystem->mMergedTypeOptions.push_back(mergedTypeOptions);
		}
	}

	return matchedIdx;
}

void BfModule::SetTypeOptions(BfTypeInstance* typeInstance)
{
	typeInstance->mTypeOptionsIdx = GenerateTypeOptions(typeInstance->mCustomAttributes, typeInstance, true);
}

bool BfModule::DoPopulateType(BfType* resolvedTypeRef, BfPopulateType populateType)
{
	auto typeInstance = resolvedTypeRef->ToTypeInstance();
	auto typeDef = typeInstance->mTypeDef;	

	BF_ASSERT((typeInstance->mTypeDef->mNextRevision == NULL) || (mCompiler->IsAutocomplete()));

	// This is a special case where our base type has been rebuilt but we haven't
	if ((typeInstance->mBaseTypeMayBeIncomplete) && (!typeInstance->mTypeIncomplete))
	{
		BfLogSysM("BaseTypeMayBeIncomplete processing. Type:%p -> Base:%p\n", typeInstance, typeInstance->mBaseType);
		PopulateType(typeInstance->mBaseType, populateType);
		if (!typeInstance->mBaseType->IsIncomplete())
			typeInstance->mBaseTypeMayBeIncomplete = false;
		if (!typeInstance->mTypeIncomplete)
			return true;
	}
	typeInstance->mBaseTypeMayBeIncomplete = false;	

	BF_ASSERT(mIsModuleMutable);

	// Don't do type instance method processing for an autocomplete pass - this will get handled later on during
	//  the PopulateType worklist pass in the full resolver.  We do need to handle the methods for delegates, though,
	//  since those can affect method declarations of other methods
	// TODO: Investigate this "Delegate" claim
	bool canDoMethodProcessing = ((mCompiler->mResolvePassData == NULL) || (mCompiler->mResolvePassData->mAutoComplete == NULL) /*|| (typeInstance->IsDelegate())*/);

	if (populateType == BfPopulateType_Full_Force)
		canDoMethodProcessing = true;

	if (typeInstance->mResolvingConstField)
		return !typeInstance->mTypeFailed;

	if (typeInstance->mNeedsMethodProcessing)
	{
		if ((canDoMethodProcessing) && (populateType >= BfPopulateType_DataAndMethods))
			DoTypeInstanceMethodProcessing(typeInstance);
		return true;
	}

	// Partial population break out point
	if ((populateType >= BfPopulateType_Identity) && (populateType <= BfPopulateType_IdentityNoRemapAlias))
		return true;

	if (!resolvedTypeRef->IsValueType())
	{
		resolvedTypeRef->mSize = typeInstance->mAlign = mSystem->mPtrSize;
	}
	
	BF_ASSERT((typeInstance->mMethodInstanceGroups.size() == 0) || (typeInstance->mMethodInstanceGroups.size() == typeDef->mMethods.size()));
	typeInstance->mMethodInstanceGroups.Resize(typeDef->mMethods.size());
	for (int i = 0; i < (int)typeInstance->mMethodInstanceGroups.size(); i++)
	{
		typeInstance->mMethodInstanceGroups[i].mOwner = typeInstance;
		typeInstance->mMethodInstanceGroups[i].mMethodIdx = i;
	}

	AutoDisallowYield disableYield(mSystem);	
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);
	SetAndRestoreValue<bool> prevHadError(mHadBuildError, false);
	SetAndRestoreValue<bool> prevHadWarning(mHadBuildWarning, false);

	BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
	typeState.mPopulateType = populateType;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfGenericTypeInstance*)typeInstance;
		if (genericTypeInst->mGenericParams.size() == 0)
			BuildGenericParams(resolvedTypeRef);
	}

	// Don't do TypeToString until down here.	Otherwise we can infinitely loop on BuildGenericParams

	bool isStruct = resolvedTypeRef->IsStruct();

	bool reportErrors = true;
	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		reportErrors = true;

	// If we're not the defining context then we don't report errors for this type, but errors will still put the system
	// into an errored state
	SetAndRestoreValue<bool> prevReportErrors(mReportErrors, reportErrors);
	
	if (typeInstance->mIsFinishingType)
	{
		// This type already failed
		return true;
	}

	CheckCircularDataError();

	bool underlyingTypeDeferred = false;
	BfType* underlyingType = NULL;
	if (typeInstance->mBaseType != NULL)
	{		
		if (typeInstance->IsTypedPrimitive())
			underlyingType = typeInstance->GetUnderlyingType();
		if ((typeInstance->mRebuildFlags & BfTypeRebuildFlag_UnderlyingTypeDeferred) != 0)
			underlyingTypeDeferred = true;
	}
	else if (typeInstance->IsEnum())
	{
		bool hasPayloads = false;
		for (auto fieldDef : typeDef->mFields)
		{
			if ((fieldDef->IsEnumCaseEntry()) && (fieldDef->mTypeRef != NULL))
			{
				hasPayloads = true;
				break;
			}
		}

		if (!hasPayloads)
		{			
			bool hadType = false;

			for (auto baseTypeRef : typeDef->mBaseTypes)
			{
				SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, baseTypeRef);				
				SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);				
				SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);

				auto baseType = ResolveTypeRef(baseTypeRef, BfPopulateType_Declaration);
				if (baseType != NULL)
				{
					if (baseType->IsIntegral())
					{
						if (!hadType)
						{
							hadType = true;
							underlyingType = baseType;
						}
						else
						{
							Fail("Underlying enum type already specified", baseTypeRef);
						}
					}
					else
					{
						Fail("Invalid underlying enum type", baseTypeRef);
					}
				}
				else
				{
					AssertErrorState();
					typeInstance->mTypeFailed = true;
				}
			}

			if (underlyingType == NULL)
			{
				underlyingType = GetPrimitiveType(BfTypeCode_Int64);
				underlyingTypeDeferred = true;
			}
		}
	}
	else if (((typeInstance->IsStruct()) || (typeInstance->IsTypedPrimitive())) &&
		(!typeInstance->mTypeFailed))
	{
		for (auto baseTypeRef : typeDef->mBaseTypes)
		{			
			auto declTypeDef = typeDef;
			if (typeDef->mIsCombinedPartial)
				declTypeDef = typeDef->mPartials.front();
			SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, declTypeDef);

			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, baseTypeRef);
			// We ignore errors here to avoid double-errors for type lookups, but this is where data cycles are detected
			//  but that type of error supersedes the mIgnoreErrors setting
			SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
			// Temporarily allow us to derive from private classes, to avoid infinite loop from TypeIsSubTypeOf
			SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);
			auto baseType = ResolveTypeRef(baseTypeRef, BfPopulateType_Declaration);
			if (baseType != NULL)
			{
				if (baseType->IsPrimitiveType())
				{
					underlyingType = baseType;
				}
				else if (baseType->IsTypedPrimitive())
				{
					//PopulateType(baseType, true);
					underlyingType = baseType->GetUnderlyingType();
					BF_ASSERT(underlyingType != NULL);
				}
			}
			else
			{
				AssertErrorState();
				typeInstance->mTypeFailed = true;
			}
		}

		// Incase we had re-entry, work this through ourselves again here
		typeInstance->mIsTypedPrimitive = false;
	}

	if (underlyingTypeDeferred)
		typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags | BfTypeRebuildFlag_UnderlyingTypeDeferred);

	typeInstance->mIsTypedPrimitive = underlyingType != NULL;
	int wantFieldCount = (int)typeDef->mFields.size() + (((underlyingType != NULL) || (typeInstance->IsPayloadEnum())) ? 1 : 0);
	if ((int)typeInstance->mFieldInstances.size() < wantFieldCount)
	{
		// Closures don't include the enclosed fields on their first pass through PopulateType, and they have no typeDef of their own
		//  so we need to take care not to truncate their fieldInstance vector here (thus the 'wantFieldCount' check above)
		typeInstance->mFieldInstances.Resize(wantFieldCount);
	}
	
	if (underlyingType != NULL)
	{
		auto fieldInstance = &typeInstance->mFieldInstances.back();
		fieldInstance->mDataOffset = 0;
		fieldInstance->mDataSize = underlyingType->mSize;
		fieldInstance->mOwner = typeInstance;
		fieldInstance->mResolvedType = underlyingType;

		typeInstance->mSize = underlyingType->mSize;
		typeInstance->mAlign = underlyingType->mAlign;
		typeInstance->mInstSize = underlyingType->mSize;
		typeInstance->mInstAlign = underlyingType->mAlign;
		typeInstance->mHasPackingHoles = underlyingType->HasPackingHoles();		
	}

	// Partial population break out point
	if (typeInstance->mDefineState < BfTypeDefineState_Declared)
	{
		typeInstance->mDefineState = BfTypeDefineState_Declared;

		if (typeInstance->IsGenericTypeInstance())
		{
			auto genericTypeInstance = (BfGenericTypeInstance*)typeInstance;

			// Add generic dependencies if needed
			for (auto genericType : genericTypeInstance->mTypeGenericArguments)
			{
				if (genericType->IsPrimitiveType())
					genericType = GetWrappedStructType(genericType);
				if (genericType != NULL)
				{
					AddDependency(genericType, genericTypeInstance, BfDependencyMap::DependencyFlag_TypeGenericArg);
					BfLogSysM("Adding generic dependency of %p for type %p\n", genericType, genericTypeInstance);
				}
			}
			if (genericTypeInstance->IsSpecializedType())
			{
				// This ensures we rebuild the unspecialized type whenever the specialized type rebuilds. This is important
				// for generic type binding
				auto unspecializedTypeInstance = GetUnspecializedTypeInstance(genericTypeInstance);
				BF_ASSERT(!unspecializedTypeInstance->IsUnspecializedTypeVariation());
				mContext->mScratchModule->AddDependency(genericTypeInstance, unspecializedTypeInstance, BfDependencyMap::DependencyFlag_UnspecializedType);
			}
		}

		auto _AddStaticSearch = [&](BfTypeDef* typeDef)
		{
			if (typeDef->mStaticSearch.IsEmpty())
				return;			
			BfStaticSearch* staticSearch;
			if (typeInstance->mStaticSearchMap.TryAdd(typeDef, NULL, &staticSearch))
			{
				for (auto typeRef : typeDef->mStaticSearch)
				{
					auto staticType = ResolveTypeRef(typeRef, NULL, BfPopulateType_Declaration);
					if (staticType != NULL)
					{
						auto staticTypeInst = staticType->ToTypeInstance();
						if (staticTypeInst == NULL)
						{
							Fail(StrFormat("Type '%s' cannot be used in a 'using static' declaration", TypeToString(staticType).c_str()), typeRef);
						}
						else
						{
							AddDependency(staticTypeInst, typeInstance, BfDependencyMap::DependencyFlag_StaticValue);
						}
					}
				}
			}			
		};

		if (typeDef->mIsCombinedPartial)
		{
			for (auto partialTypeDef : typeDef->mPartials)
				_AddStaticSearch(partialTypeDef);
		}
		else
			_AddStaticSearch(typeDef);
	}
	if (populateType == BfPopulateType_Declaration)
	{
		return true;
	}

	if ((!mCompiler->mIsResolveOnly) && (!typeInstance->mHasBeenInstantiated))
	{
		for (auto& dep : typeInstance->mDependencyMap)
		{
			auto& depEntry = dep.mValue;
			if ((depEntry.mFlags & BfDependencyMap::DependencyFlag_Allocates) != 0)
			{
				auto depType = dep.mKey;
				if (depType->mRevision == depEntry.mRevision)
				{
					BfLogSysM("Setting mHasBeenInstantiated for %p instantiated from %p\n", typeInstance, depType);
					typeInstance->mHasBeenInstantiated = true;
				}
			}
		}
	}

	//BfLogSysM("Setting revision.  Type: %p  Revision: %d\n", typeInstance, mRevision);
	//typeInstance->mRevision = mRevision;

	// Temporarily allow us to derive from private classes, to avoid infinite loop from TypeIsSubTypeOf
	SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);

	if ((typeDef->mOuterType != NULL) && (typeDef->mOuterType->IsGlobalsContainer()))
	{
		if ((typeDef->mTypeDeclaration != NULL) && (typeDef->mTypeDeclaration->mTypeNode != NULL))
			Fail("Global blocks cannot contain type declarations", typeDef->mTypeDeclaration->mTypeNode);
	}

	/// Create DI data	
	SizedArray<BfIRType, 8> llvmFieldTypes;

	int curFieldDataIdx = 0;
	typeInstance->mBaseType = NULL;
	BfTypeInstance* defaultBaseTypeInst = NULL;

	// Find base type
	BfType* baseType = NULL;

	struct BfInterfaceDecl
	{
		BfTypeInstance* mIFaceTypeInst;
		BfTypeReference* mTypeRef;
		BfTypeDef* mDeclaringType;
	};

	SizedArray<BfInterfaceDecl, 8> interfaces;
	HashSet<BfTypeInstance*> ifaceSet;

	if (resolvedTypeRef == mContext->mBfObjectType)
	{
		baseType = NULL;
	}
	else if (typeInstance->IsEnum())
	{
		if (mCompiler->mEnumTypeDef == NULL)
		{
			Fail("Enum type required");
			TypeFailed(typeInstance);
		}
		else
			baseType = ResolveTypeDef(mCompiler->mEnumTypeDef)->ToTypeInstance();
	}
	else if (resolvedTypeRef->IsObject())
		baseType = mContext->mBfObjectType;
	else if (resolvedTypeRef->IsPointer())
	{
		baseType = ResolveTypeDef(mCompiler->mPointerTTypeDef, BfPopulateType_Data);
	}
	else if ((resolvedTypeRef->IsValueType()) && (typeDef != mCompiler->mValueTypeTypeDef))
	{
		baseType = ResolveTypeDef(mCompiler->mValueTypeTypeDef, BfPopulateType_Data)->ToTypeInstance();
	}

	if (baseType != NULL)
		defaultBaseTypeInst = baseType->ToTypeInstance();

	BfTypeReference* baseTypeRef = NULL;
	if ((typeDef->mIsDelegate) && (!typeInstance->IsClosure()))
	{
		if (mCompiler->mDelegateTypeDef == NULL)
		{
			Fail("Delegate type required");
			TypeFailed(typeInstance);
		}
		else
			baseType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();
	}
	else if (typeDef->mIsFunction)
	{
		if (mCompiler->mFunctionTypeDef == NULL)
		{
			Fail("Function type required");
			TypeFailed(typeInstance);
		}
		else
			baseType = ResolveTypeDef(mCompiler->mFunctionTypeDef)->ToTypeInstance();
	}
	else
	{
		for (auto checkTypeRef : typeDef->mBaseTypes)
		{
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, checkTypeRef);

			auto declTypeDef = typeDef;
			if (typeDef->mIsCombinedPartial)
				declTypeDef = typeDef->mPartials.front();
			SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, declTypeDef);

			bool populateBase = !typeInstance->mTypeFailed;
			auto checkType = ResolveTypeRef(checkTypeRef, populateBase ? BfPopulateType_Data : BfPopulateType_Declaration);
			if (checkType != NULL)
			{
				auto checkTypeInst = checkType->ToTypeInstance();
				bool canDeriveFrom = checkTypeInst != NULL;
				if ((typeInstance->IsStruct()) || (typeInstance->IsTypedPrimitive()) || (typeInstance->IsBoxed()))
					canDeriveFrom |= checkType->IsPrimitiveType();
				if ((typeInstance->IsEnum()) && (!checkType->IsInterface()))
				{
					if (typeInstance->IsTypedPrimitive())
						continue;
					if (checkType->IsPrimitiveType())
						Fail(StrFormat("Enum '%s' cannot be specified as '%s' because it has a payload",
							TypeToString(typeInstance).c_str(), TypeToString(checkType).c_str()),
							checkTypeRef);
					else
						Fail("Enums cannot derive from other types", checkTypeRef);
					continue;
				}

				if ((checkTypeInst != NULL) && (checkTypeInst->mTypeFailed))
				{
					// To keep circular references from breaking type invariants (ie: base type loops)
					continue;
				}

				if (!canDeriveFrom)
				{
					Fail("Cannot derive from this type", checkTypeRef);
					continue;
				}

				if (checkType->IsInterface())
				{
					auto ifaceInst = checkType->ToTypeInstance();
					if (ifaceSet.Add(ifaceInst))
					{
						// Not base type
						BfInterfaceDecl ifaceDecl;
						ifaceDecl.mIFaceTypeInst = ifaceInst;
						ifaceDecl.mTypeRef = checkTypeRef;
						ifaceDecl.mDeclaringType = typeDef;
						interfaces.push_back(ifaceDecl);
					}
					else
					{
						Fail(StrFormat("Interface '%s' is already specified", TypeToString(checkType).c_str()), checkTypeRef);
					}
				}
				else if (resolvedTypeRef == mContext->mBfObjectType)
				{
					Fail(StrFormat("Type '%s' cannot define a base type", TypeToString(baseType).c_str()), checkTypeRef);
				}
				else
				{
					if (baseTypeRef != NULL)
					{
						Fail(StrFormat("Base type '%s' already declared", TypeToString(baseType).c_str()), checkTypeRef);
					}
					else
					{
						baseTypeRef = checkTypeRef;
						if (checkTypeInst != NULL)
						{
							baseType = checkTypeInst;
							/*if ((resolvedTypeRef->IsBoxed()) && (baseType->IsValueType()))
							{
								baseType = CreateBoxedType(baseType);
							}*/
						}
					}
				}
			}
			else
			{
				AssertErrorState();
				// Why did we go around setting mTypeFailed on all these things?
				//typeInstance->mTypeFailed = true;
			}
		}

		for (auto partialTypeDef : typeDef->mPartials)
		{
			if (!typeInstance->IsTypeMemberIncluded(partialTypeDef))
				continue;
			if (partialTypeDef->mTypeDeclaration == typeInstance->mTypeDef->mTypeDeclaration)
				continue;

			for (auto checkTypeRef : partialTypeDef->mBaseTypes)
			{
				SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, checkTypeRef);
				SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, partialTypeDef);
				bool populateBase = !typeInstance->mTypeFailed;
				auto checkType = ResolveTypeRef(checkTypeRef, BfPopulateType_Declaration);
				if (checkType != NULL)
				{
					if (checkType->IsInterface())
					{
						BfInterfaceDecl ifaceDecl;
						ifaceDecl.mIFaceTypeInst = checkType->ToTypeInstance();
						ifaceDecl.mTypeRef = checkTypeRef;
						ifaceDecl.mDeclaringType = partialTypeDef;
						interfaces.push_back(ifaceDecl);
					}
					else
					{
						Fail(StrFormat("Extensions can only specify new interfaces, type '%s' is not a valid ", TypeToString(checkType).c_str()), checkTypeRef);
					}
				}
			}
		}
	}

	if (resolvedTypeRef->IsBoxed())
	{
		if ((baseType != NULL) && (baseType->IsStruct()))
		{
			BfBoxedType* boxedType = (BfBoxedType*)resolvedTypeRef;
			BfType* modifiedBaseType = baseType;
			if (boxedType->IsBoxedStructPtr())
				modifiedBaseType = CreatePointerType(modifiedBaseType);
			boxedType->mBoxedBaseType = CreateBoxedType(modifiedBaseType);

			PopulateType(boxedType->mBoxedBaseType);
			AddDependency(boxedType->mBoxedBaseType, typeInstance, BfDependencyMap::DependencyFlag_DerivedFrom);
		}
		
		baseType = mContext->mBfObjectType;
	}	

	BfTypeInstance* baseTypeInst = NULL;

	if (baseType != NULL)
	{
		baseTypeInst = baseType->ToTypeInstance();
	}

	if (typeInstance->mBaseType != NULL)
	{
		BF_ASSERT(typeInstance->mBaseType == baseTypeInst);
	}

	BfType* outerType = GetOuterType(typeInstance);
	if (outerType != NULL)
		AddDependency(outerType, typeInstance, BfDependencyMap::DependencyFlag_OuterType);

	if ((baseTypeInst != NULL) && (typeInstance->mBaseType == NULL))
	{
		//curFieldDataIdx = 1;
// 		if (!typeInstance->mTypeFailed)
// 			PopulateType(baseTypeInst, BfPopulateType_Data);

		if (typeInstance->mTypeFailed)
		{
			if (baseTypeInst->IsDataIncomplete())
			{
				if (baseTypeInst->IsStruct())
					baseTypeInst = ResolveTypeDef(mCompiler->mValueTypeTypeDef)->ToTypeInstance();
				else if (baseTypeInst->IsObject())
					baseTypeInst = ResolveTypeDef(mCompiler->mBfObjectTypeDef)->ToTypeInstance();
			}
		}
		PopulateType(baseTypeInst, BfPopulateType_Data);

		typeInstance->mBaseTypeMayBeIncomplete = false;

		typeInstance->mMergedFieldDataCount = baseTypeInst->mMergedFieldDataCount;

		if ((resolvedTypeRef->IsObject()) && (!baseTypeInst->IsObject()))
		{
			Fail("Class can only derive from another class", baseTypeRef, true);
			//typeInstance->mTypeFailed = true;
			baseTypeInst = defaultBaseTypeInst;
			typeInstance->mBaseType = baseTypeInst;
		}
		else if ((resolvedTypeRef->IsStruct()) && (!baseTypeInst->IsValueType()))
		{
			Fail("Struct can only derive from another struct", baseTypeRef, true);
			//typeInstance->mTypeFailed = true;
			baseTypeInst = defaultBaseTypeInst;
			typeInstance->mBaseType = baseTypeInst;
		}

		if (!typeInstance->IsIncomplete())
		{
			// Re-entry may cause this type to be completed already
			return true;
		}

		//BfLogSysM("Adding DerivedFrom dependency. Used:%p Using:%p\n", baseType, typeInstance);
		auto checkBaseType = baseTypeInst;
		while (checkBaseType != NULL)
		{
			// Add 'DerivedFrom' dependency all the way up the inheritance chain
			AddDependency(checkBaseType, typeInstance, BfDependencyMap::DependencyFlag_DerivedFrom);
			checkBaseType = checkBaseType->mBaseType;
		}

		typeInstance->mBaseType = baseTypeInst;
		typeInstance->mInheritDepth = baseTypeInst->mInheritDepth + 1;
		typeInstance->mHasParameterizedBase = baseTypeInst->mHasParameterizedBase;
		if ((baseTypeInst->IsArray()) || (baseTypeInst->IsSizedArray()) || (baseTypeInst->IsGenericTypeInstance()))
			typeInstance->mHasParameterizedBase = true;

		if (underlyingType == NULL)
		{
			typeInstance->mInstSize = baseTypeInst->mInstSize;
			typeInstance->mInstAlign = baseTypeInst->mInstAlign;
			typeInstance->mAlign = baseTypeInst->mAlign;
			typeInstance->mSize = baseTypeInst->mSize;
			typeInstance->mHasPackingHoles = baseTypeInst->mHasPackingHoles;
			if (baseTypeInst->mIsTypedPrimitive)
				typeInstance->mIsTypedPrimitive = true;
		}
	}

	if (populateType <= BfPopulateType_BaseType)
		return true;

	if ((typeInstance->mBaseType != NULL) && (!typeInstance->IsTypedPrimitive()))
	{
		curFieldDataIdx++;
	}

	if (!interfaces.empty())
	{
		for (int iFaceIdx = 0; iFaceIdx < (int)interfaces.size(); iFaceIdx++)
		{
			auto checkInterface = interfaces[iFaceIdx].mIFaceTypeInst;
			PopulateType(checkInterface, BfPopulateType_Data);

			BfTypeInterfaceEntry* found = NULL;
			bool foundExact = false;
			for (auto& typeInterfaceInst : typeInstance->mInterfaces)
			{
				if (typeInterfaceInst.mInterfaceType == checkInterface)
				{
					if (typeInterfaceInst.mDeclaringType == interfaces[iFaceIdx].mDeclaringType)
					{
						foundExact = true;
						break;
					}

					found = &typeInterfaceInst;
				}
			}
			if (foundExact)
				continue;

			BfTypeInterfaceEntry typeInterfaceInst;
			typeInterfaceInst.mDeclaringType = interfaces[iFaceIdx].mDeclaringType;
			typeInterfaceInst.mInterfaceType = checkInterface;
			typeInterfaceInst.mStartInterfaceTableIdx = -1;
			typeInterfaceInst.mStartVirtualIdx = -1;
			typeInterfaceInst.mIsRedeclared = false;
			typeInstance->mInterfaces.push_back(typeInterfaceInst);

			// Interfaces can list other interfaces in their declaration, so pull those in too
			for (auto depIFace : checkInterface->mInterfaces)
			{
				auto depIFaceEntry = interfaces[iFaceIdx];
				depIFaceEntry.mIFaceTypeInst = depIFace.mInterfaceType;
				interfaces.push_back(depIFaceEntry);
			}
		}
	}

	typeInstance->mDefineState = BfTypeDefineState_HasInterfaces;
	if (populateType <= BfPopulateType_Interfaces)
		return true;

	prevSkipTypeProtectionChecks.Restore();
	typeInstance->mInstSize = std::max(0, typeInstance->mInstSize);
	typeInstance->mInstAlign = std::max(0, typeInstance->mInstAlign);

	if ((typeInstance->mCustomAttributes == NULL) && (typeDef->mTypeDeclaration != NULL) && (typeDef->mTypeDeclaration->mAttributes != NULL))
	{
		BfAttributeTargets attrTarget;
		if ((typeDef->mIsDelegate) || (typeDef->mIsFunction))
			attrTarget = BfAttributeTargets_Delegate;
		else if (typeInstance->IsEnum())
			attrTarget = BfAttributeTargets_Enum;
		else if (typeInstance->IsInterface())
			attrTarget = BfAttributeTargets_Interface;
		else if (typeInstance->IsStruct())
			attrTarget = BfAttributeTargets_Struct;
		else
			attrTarget = BfAttributeTargets_Class;
		if (!typeInstance->mTypeFailed)
		{
			// This allows us to avoid reentrancy when checking for inner types
			SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);
			if (typeDef->mIsCombinedPartial)
			{
				for (auto partialTypeDef : typeDef->mPartials)
				{
					if (partialTypeDef->mTypeDeclaration->mAttributes == NULL)
						continue;
					BfTypeState typeState;
					typeState.mPrevState = mContext->mCurTypeState;
					typeState.mCurTypeDef = partialTypeDef;
					typeState.mTypeInstance = typeInstance;
					SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

					if (typeInstance->mCustomAttributes == NULL)
						typeInstance->mCustomAttributes = new BfCustomAttributes();
					GetCustomAttributes(typeInstance->mCustomAttributes, partialTypeDef->mTypeDeclaration->mAttributes, attrTarget);
				}
			}
			else
				typeInstance->mCustomAttributes = GetCustomAttributes(typeDef->mTypeDeclaration->mAttributes, attrTarget);
		}
	}

	if (typeInstance->mTypeOptionsIdx == -2)
	{
		SetTypeOptions(typeInstance);
	}

	ProcessCustomAttributeData();
	bool isPacked = false;
	bool isUnion = false;
	bool isCRepr = false;
	bool isOrdered = false;
	ProcessTypeInstCustomAttributes(isPacked, isUnion, isCRepr, isOrdered);
	if (isPacked) // Packed infers ordered
		isOrdered = true;
	typeInstance->mIsUnion = isUnion;
	if ((typeInstance->IsEnum()) && (typeInstance->IsStruct()))
		typeInstance->mIsUnion = true;
	typeInstance->mIsPacked = isPacked;
	typeInstance->mIsCRepr = isCRepr;

	BfType* unionInnerType = NULL;
	bool hadDeferredVars = false;
	int dataPos;
	if (resolvedTypeRef->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)resolvedTypeRef;
		BfType* innerType = boxedType->mElementType;
		if (boxedType->IsBoxedStructPtr())
			innerType = CreatePointerType(innerType);
		if (innerType->IsIncomplete())
			PopulateType(innerType, BfPopulateType_Data);		

		auto baseType = typeInstance->mBaseType;
		dataPos = baseType->mInstSize;
		int alignSize = BF_MAX(innerType->mAlign, baseType->mInstAlign);
		if (alignSize > 1)
			dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
		int dataSize = innerType->mSize;

		typeInstance->mFieldInstances.push_back(BfFieldInstance());
		BfFieldInstance* fieldInstance = &typeInstance->mFieldInstances.back();
		fieldInstance->mDataOffset = dataPos;
		fieldInstance->mDataSize = innerType->mSize;
		fieldInstance->mOwner = typeInstance;
		fieldInstance->mResolvedType = innerType;

		if (!innerType->IsValuelessType())
		{
			curFieldDataIdx++;
		}
		dataPos += dataSize;
		typeInstance->mInstAlign = std::max(baseType->mInstAlign, alignSize);
		int instAlign = typeInstance->mInstAlign;
		if (instAlign != 0)
		{
			int instSize = (dataPos + (instAlign - 1)) & ~(instAlign - 1);
			if (instSize != typeInstance->mInstSize)
			{
				typeInstance->mInstSize = instSize;
				typeInstance->mHasPackingHoles = true;
			}
		}
		typeInstance->mInstSize = std::max(1, typeInstance->mInstSize);
	}
	else
	{
		dataPos = typeInstance->mInstSize;

		if (underlyingType != NULL)
		{
			if (!underlyingType->IsValuelessType())
			{
				curFieldDataIdx++;
			}
		}
		
		struct DeferredResolveEntry
		{
			BfFieldDef* mFieldDef;
			int mTypeArrayIdx;
		};
		
		BfSizedVector<DeferredResolveEntry, 8> deferredVarResolves;

		for (auto field : typeDef->mFields)
		{
			auto fieldInstance = &typeInstance->mFieldInstances[field->mIdx];
			if (fieldInstance->mResolvedType != NULL)
				continue;

			if (!typeInstance->IsTypeMemberIncluded(field->mDeclaringType))
			{
				fieldInstance->mFieldIncluded = false;
				continue;
			}

			fieldInstance->mOwner = typeInstance;
			fieldInstance->mFieldIdx = field->mIdx;

			if (typeInstance->IsInterface())
				Fail("Interfaces cannot include fields. Consider making this a property", field->GetRefNode());
		}

		int enumCaseEntryIdx = 0;
		for (int pass = 0; pass < 2; pass++)
		{
			for (auto field : typeDef->mFields)
			{
				// Do consts then non-consts. Somewhat of a hack for using consts as sized array size
				if (field->mIsConst != (pass == 0))
					continue;

				auto fieldInstance = &typeInstance->mFieldInstances[field->mIdx];
				if ((fieldInstance->mResolvedType != NULL) || (!fieldInstance->mFieldIncluded))
					continue;

				SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, field);

				BfType* resolvedFieldType = NULL;

				if (field->IsEnumCaseEntry())
				{
					fieldInstance->mDataIdx = -(enumCaseEntryIdx++) - 1;
					resolvedFieldType = typeInstance;

					BfType* payloadType = NULL;
					if (field->mTypeRef != NULL)
						payloadType = ResolveTypeRef(field->mTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_NoResolveGenericParam);
					if (payloadType == NULL)
					{
						if (!typeInstance->IsTypedPrimitive())
							payloadType = CreateTupleType(BfTypeVector(), Array<String>());
					}
					if (payloadType != NULL)
					{
						AddDependency(payloadType, typeInstance, BfDependencyMap::DependencyFlag_ValueTypeMemberData);
						BF_ASSERT(payloadType->IsTuple());
						resolvedFieldType = payloadType;
						fieldInstance->mIsEnumPayloadCase = true;
					}
				}
				else if ((field->mTypeRef != NULL) && ((field->mTypeRef->IsExact<BfVarTypeReference>()) || (field->mTypeRef->IsExact<BfLetTypeReference>()) || (field->mTypeRef->IsExact<BfDeclTypeRef>())))
				{
					resolvedFieldType = GetPrimitiveType(BfTypeCode_Var);

					DeferredResolveEntry resolveEntry;
					resolveEntry.mFieldDef = field;
					resolveEntry.mTypeArrayIdx = (int)llvmFieldTypes.size();
					deferredVarResolves.push_back(resolveEntry);

					fieldInstance->mIsInferredType = true;
					// For 'let', make read-only
				}
				else
				{
					resolvedFieldType = ResolveTypeRef(field->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);
					if (resolvedFieldType == NULL)
					{
						// Failed, just put in placeholder 'var'
						AssertErrorState();
						resolvedFieldType = GetPrimitiveType(BfTypeCode_Var);
					}
				}

				if (resolvedFieldType->IsMethodRef())
				{
					auto methodRefType = (BfMethodRefType*)resolvedFieldType;
				}

				if (fieldInstance->mResolvedType == NULL)
					fieldInstance->mResolvedType = resolvedFieldType;

				if (field->mIsConst)
				{
					// Resolve in ResolveConstField after we finish populating entire FieldInstance list							
				}
				else if (field->mIsStatic)
				{
					// Don't allocate this until after we're finished populating entire FieldInstance list,
					//  because we may have re-entry and create multiple instances of this static field				
				}
			}
		}

		if (!resolvedTypeRef->IsIncomplete())
		{
			// We finished resolving ourselves through a re-entry, so we're actually done here
			return true;
		}

		for (auto& resolveEntry : deferredVarResolves)
		{
			hadDeferredVars = true;
			auto fieldType = ResolveVarFieldType(typeInstance, &typeInstance->mFieldInstances[resolveEntry.mFieldDef->mIdx], resolveEntry.mFieldDef);
			if (fieldType == NULL)
			{
				fieldType = mContext->mBfObjectType;
				// We used to set mTypeFailed, but mHasBuildError is enough to cause a type rebuild properly
				mHadBuildError = true;
				//typeInstance->mTypeFailed = true;
			}
			auto fieldInstance = &typeInstance->mFieldInstances[resolveEntry.mFieldDef->mIdx];
			fieldInstance->SetResolvedType(fieldType);
		}

		if (typeInstance->mResolvingConstField)
			return !typeInstance->mTypeFailed;

		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			auto resolvedFieldType = fieldInstance->GetResolvedType();

			if (!fieldInstance->mFieldIncluded)
				continue;

			if (resolvedFieldType == NULL)
			{
				if ((underlyingType != NULL) || (typeInstance->IsPayloadEnum()))
					continue;
			}

			if (!fieldInstance->mFieldIncluded)
				continue;

			if (fieldDef == NULL)
				continue;

			if ((!fieldDef->mIsStatic) && (resolvedFieldType->IsValueType()))
			{
				// We need that type finished up for alignment and data size
				//  But if the type has failed then we need to avoid stack overflow so we don't finish it
				SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);
				bool populateChildType = !typeInstance->mTypeFailed;
				//bool populateChildType = true;
				PopulateType(resolvedFieldType, populateChildType ? BfPopulateType_Data : BfPopulateType_Declaration);
				
				if (populateChildType)
				{
					BF_ASSERT(!resolvedFieldType->IsDataIncomplete());
				}
				else
				{
					if (resolvedFieldType->IsDataIncomplete())
					{
						AssertErrorState();
						resolvedFieldType = mContext->mBfObjectType;
						fieldInstance->SetResolvedType(resolvedFieldType);

						// We used to set mTypeFailed, but mHasBuildError is enough to cause a type rebuild properly
						mHadBuildError = true;
					}
				}
			}
		}
	}

	if ((!typeInstance->IsIncomplete()) || (typeInstance->mNeedsMethodProcessing))
	{
		return !typeInstance->mTypeFailed;
	}

	BF_ASSERT(mContext->mCurTypeState == &typeState);

	//BF_ASSERT(!typeInstance->mIsFinishingType);
	typeInstance->mIsFinishingType = true;

	// No re-entry is allowed below here -- we will run all the way to the end at this point
	BfSizedVector<BfIRMDNode, 8> diFieldTypes;

	HashContext dataMemberHashCtx;
	
	if (!resolvedTypeRef->IsBoxed())
	{
		for (auto propDef : typeDef->mProperties)
		{
			if (!typeInstance->IsTypeMemberIncluded(propDef->mDeclaringType))
				continue;

			if (propDef->mFieldDeclaration != NULL)
			{
				BfTypeState typeState;
				typeState.mPrevState = mContext->mCurTypeState;
				typeState.mCurTypeDef = propDef->mDeclaringType;
				typeState.mCurFieldDef = propDef;
				typeState.mTypeInstance = typeInstance;
				SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

				if (propDef->mFieldDeclaration->mAttributes != NULL)
				{
					auto customAttrs = GetCustomAttributes(propDef->mFieldDeclaration->mAttributes, BfAttributeTargets_Property);
					delete customAttrs;
				}

				if (propDef->mFieldDeclaration->mAttributes != NULL)
				{
					auto customAttrs = GetCustomAttributes(propDef->mFieldDeclaration->mAttributes, BfAttributeTargets_Property);
					delete customAttrs;
				}

				auto propDecl = (BfPropertyDeclaration*)propDef->mFieldDeclaration;
				if (propDecl->mExplicitInterface != NULL)
				{
					if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
						mCompiler->mResolvePassData->mAutoComplete->CheckTypeRef(propDecl->mExplicitInterface, false);
					auto explicitInterface = ResolveTypeRef(propDecl->mExplicitInterface, BfPopulateType_Declaration);
					if (explicitInterface != NULL)
					{
						bool interfaceFound = false;
						for (auto ifaceInst : typeInstance->mInterfaces)
							interfaceFound |= ifaceInst.mInterfaceType == explicitInterface;
						if (!interfaceFound)
						{
							Fail("Containing class has not declared to implement this interface", propDecl->mExplicitInterface, true);
						}
					}
				}
			}

			if (propDef->mMethods.IsEmpty())
			{
				auto nameNode = ((BfPropertyDeclaration*)propDef->mFieldDeclaration)->mNameNode;

				if (nameNode != NULL)
				{
					Fail(StrFormat("Property or indexer '%s.%s' must have at least one accessor", TypeToString(typeInstance).c_str(), propDef->mName.c_str()),
						nameNode, true); // CS0548
				}
			}
		}

		bool isGlobalContainer = typeDef->IsGlobalsContainer();		

		if (typeInstance->mBaseType != NULL)
		{
			dataMemberHashCtx.Mixin(typeInstance->mBaseType->mTypeId);
			if (typeInstance->mBaseType->mHotTypeData != NULL)
			{
				BfHotTypeVersion* ver = typeInstance->mBaseType->mHotTypeData->GetLatestVersion();
				dataMemberHashCtx.Mixin(ver->mDataHash);
			}
		}
		dataMemberHashCtx.Mixin(typeInstance->mIsPacked);
		dataMemberHashCtx.Mixin(typeInstance->mIsCRepr);
		dataMemberHashCtx.Mixin(typeInstance->mIsUnion);

		int startDataPos = dataPos;
		int maxDataPos = dataPos;

		BfSizedVector<BfFieldInstance*, 16> dataFieldVec;
		
		// We've resolved all the 'var' entries, so now build the actual composite type
		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			if (!fieldInstance->mFieldIncluded)
				continue;
			auto resolvedFieldType = fieldInstance->GetResolvedType();
			if (fieldInstance->mResolvedType == NULL)
			{
				if ((underlyingType == NULL) && (!typeInstance->IsPayloadEnum()))
					BF_ASSERT(typeInstance->mTypeFailed);
				continue;
			}
			if ((fieldInstance->GetFieldDef() != NULL) && (fieldInstance->GetFieldDef()->mIsConst))
			{
				// Resolve later
			}
			else if (fieldInstance->GetFieldDef() != NULL)
			{
				if (!fieldInstance->GetFieldDef()->mIsStatic)
					AddFieldDependency(typeInstance, fieldInstance, resolvedFieldType);
				else
					AddDependency(resolvedFieldType, typeInstance, BfDependencyMap::DependencyFlag_StaticValue);
			}

			auto fieldDef = fieldInstance->GetFieldDef();

			BF_ASSERT(fieldInstance->mCustomAttributes == NULL);
			if ((fieldDef != NULL) && (fieldDef->mFieldDeclaration != NULL) && (fieldDef->mFieldDeclaration->mAttributes != NULL))
			{
				BfTypeState typeState;
				typeState.mPrevState = mContext->mCurTypeState;
				typeState.mCurFieldDef = fieldDef;
				typeState.mCurTypeDef = fieldDef->mDeclaringType;
				typeState.mTypeInstance = typeInstance;
				SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

				fieldInstance->mCustomAttributes = GetCustomAttributes(fieldDef->mFieldDeclaration->mAttributes, fieldDef->mIsStatic ? BfAttributeTargets_StaticField : BfAttributeTargets_Field);

				for (auto customAttr : fieldInstance->mCustomAttributes->mAttributes)
				{
					if (TypeToString(customAttr.mType) == "System.ThreadStaticAttribute")
					{
						if ((!fieldDef->mIsStatic) || (fieldDef->mIsConst))
						{
							Fail("ThreadStatic attribute can only be used on static fields", fieldDef->mFieldDeclaration->mAttributes);
						}
					}
				}
			}

			if (fieldInstance->mResolvedType != NULL)
			{
				auto resolvedFieldType = fieldInstance->GetResolvedType();				
				if ((!typeInstance->IsBoxed()) && (fieldDef != NULL))
				{					
					if (fieldInstance->mIsEnumPayloadCase)
					{						
 						PopulateType(resolvedFieldType, BfPopulateType_Data);
 						if (resolvedFieldType->WantsGCMarking())
 							typeInstance->mWantsGCMarking = true;
					}

					if ((!fieldDef->mIsConst) && (!fieldDef->mIsStatic))
					{
						PopulateType(resolvedFieldType, resolvedFieldType->IsValueType() ? BfPopulateType_Data : BfPopulateType_Declaration);
						if (resolvedFieldType->WantsGCMarking())
							typeInstance->mWantsGCMarking = true;

						fieldInstance->mMergedDataIdx = typeInstance->mMergedFieldDataCount;
						if (resolvedFieldType->IsStruct())
						{
							auto resolvedFieldTypeInstance = resolvedFieldType->ToTypeInstance();
							typeInstance->mMergedFieldDataCount += resolvedFieldTypeInstance->mMergedFieldDataCount;
						}
						else if (!resolvedFieldType->IsValuelessType())
							typeInstance->mMergedFieldDataCount++;

						if (fieldDef->mIsExtern)
						{
							Fail("Cannot declare instance member as 'extern'", fieldDef->mFieldDeclaration->mExternSpecifier, true);
						}

						BfAstNode* nameRefNode = NULL;
						if (fieldDef->mFieldDeclaration != NULL)
							nameRefNode = fieldDef->mFieldDeclaration->mNameNode;
						if (nameRefNode == NULL)
							nameRefNode = fieldDef->mTypeRef;

						if (underlyingType != NULL)
						{
							if (typeInstance->IsEnum())
								Fail("Cannot declare instance members in an enum", nameRefNode, true);
							else
								Fail("Cannot declare instance members in a typed primitive struct", nameRefNode, true);
							TypeFailed(typeInstance);
							fieldInstance->mDataIdx = -1;
							continue;
						}

						if (typeDef->mIsStatic)
						{
							//CS0708
							Fail("Cannot declare instance members in a static class", nameRefNode, true);
						}

						if (resolvedFieldType->IsValueType())
						{
							BF_ASSERT(!resolvedFieldType->IsDataIncomplete());
						}

						if (!mCompiler->mIsResolveOnly)
						{
							dataMemberHashCtx.MixinStr(fieldDef->mName);							
							dataMemberHashCtx.Mixin(resolvedFieldType->mTypeId);
						}

						int dataSize = resolvedFieldType->mSize;
						int alignSize = resolvedFieldType->mAlign;
						fieldInstance->mDataSize = dataSize;
						if (!isUnion)
						{
							if (!resolvedFieldType->IsValuelessType())
							{
								if (isCRepr)
								{
									dataFieldVec.push_back(fieldInstance);
								}
								else
								{
									dataFieldVec.push_back(fieldInstance);
								}
							}
						}
						else
						{
							BF_ASSERT(resolvedFieldType->mSize >= 0);														

							if ((alignSize > 1) && (!isPacked))
								dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
							fieldInstance->mDataOffset = dataPos;

							if (!isPacked)
								typeInstance->mInstAlign = std::max(typeInstance->mInstAlign, alignSize);
							dataPos += dataSize;

							if (dataPos > maxDataPos)
							{
								maxDataPos = dataPos;
							}
							dataPos = startDataPos;
						}						

						auto fieldTypeInst = resolvedFieldType->ToTypeInstance();
						if (fieldTypeInst != NULL)
						{
							if ((fieldTypeInst->mRebuildFlags & BfTypeRebuildFlag_UnderlyingTypeDeferred) != 0)
							{
								BfAstNode* refNode = fieldDef->mFieldDeclaration;
								String failStr;
								failStr = StrFormat("Circular data reference detected between '%s' and '%s'", TypeToString(mCurTypeInstance).c_str(), TypeToString(fieldTypeInst).c_str());
								if (!mContext->mFieldResolveReentrys.IsEmpty())
								{
									failStr += StrFormat(" with the following fields:", TypeToString(mCurTypeInstance).c_str());
									for (int i = 0; i < (int)mContext->mFieldResolveReentrys.size(); i++)
									{
										auto checkField = mContext->mFieldResolveReentrys[i];

										if (i > 0)
											failStr += ",";
										failStr += "\n  '" + TypeToString(typeInstance) + "." + checkField->GetFieldDef()->mName + "'";

										if (checkField->mOwner == fieldTypeInst)
											refNode = checkField->GetFieldDef()->mFieldDeclaration;
									}
								}
								BfError* err = Fail(failStr, refNode);
								if (err)
									err->mIsPersistent = true;
							}
						}
					}

					bool useForUnion = false;

					if (fieldInstance->mIsEnumPayloadCase)
					{
						if (!typeInstance->IsEnum())
						{
							Fail("Cases can only be used in enum types", fieldDef->mFieldDeclaration);
						}
						else
						{
							BF_ASSERT(typeInstance->mIsUnion);
						}						
					}
					
					if ((!fieldDef->mIsStatic) && (!resolvedFieldType->IsValuelessType()))
					{
						if (isUnion)
						{
							fieldInstance->mDataIdx = curFieldDataIdx;							
						}						
					}
				}

				if ((!typeInstance->IsSpecializedType()) && (!typeInstance->IsOnDemand()) && (fieldDef != NULL) && (!CheckDefineMemberProtection(fieldDef->mProtection, resolvedFieldType)))
				{
					//CS0052
					Fail(StrFormat("Inconsistent accessibility: field type '%s' is less accessible than field '%s.%s'",
						TypeToString(resolvedFieldType).c_str(), TypeToString(mCurTypeInstance).c_str(), fieldDef->mName.c_str()),
						fieldDef->mTypeRef, true);
				}
			}
		}
		if (typeInstance->mIsUnion)
			unionInnerType = typeInstance->GetUnionInnerType();		

		if (!isOrdered)
		{
			int dataFieldCount = (int)dataFieldVec.size();

			Array<Deque<BfFieldInstance*>> alignBuckets;
			for (auto fieldInst : dataFieldVec)
			{
				int alignBits = GetHighestBitSet(fieldInst->mResolvedType->mAlign);
				while (alignBits >= alignBuckets.size())
					alignBuckets.Add({});
				alignBuckets[alignBits].Add(fieldInst);
			}
			dataFieldVec.clear();

			int curSize = typeInstance->mInstSize;
			while (dataFieldVec.size() != dataFieldCount)
			{
				// Clear out completed buckets
				while (alignBuckets[alignBuckets.size() - 1].IsEmpty())
				{
					alignBuckets.pop_back();
				}

				int alignBits = GetNumLowZeroBits(curSize) + 1;							
				alignBits = BF_MIN(alignBits, (int)alignBuckets.size() - 1);

				bool foundEntry = false;
				while (alignBits >= 0)
				{
					if (alignBuckets[alignBits].IsEmpty())
					{
						alignBits--;
						continue;
					}

					bool isHighestBucket = alignBits == alignBuckets.size() - 1;
					
					auto fieldInst = alignBuckets[alignBits][0];
					alignBuckets[alignBits].RemoveAt(0);
					dataFieldVec.push_back(fieldInst);
					curSize = BF_ALIGN(curSize, fieldInst->mResolvedType->mAlign);
					curSize += fieldInst->mResolvedType->mSize;
					foundEntry = true;

					if (!isHighestBucket)
					{
						// We may have a larger type that can fit now...
						break;
					}
				}
				
				if (!foundEntry)
				{
					// If no entries will fit, then force an entry of the smallest alignment
					for (int alignBits = 0; alignBits < alignBuckets.size(); alignBits++)
					{
						if (!alignBuckets[alignBits].IsEmpty())
						{												 	
						 	auto fieldInst = alignBuckets[alignBits][0];
						 	alignBuckets[alignBits].RemoveAt(0);
						 	dataFieldVec.push_back(fieldInst);
						 	curSize = BF_ALIGN(curSize, fieldInst->mResolvedType->mAlign);
						 	curSize += fieldInst->mResolvedType->mSize;
							break;
						}
					}
				}
			}
		}

		for (auto fieldInstance : dataFieldVec)
		{
			auto resolvedFieldType = fieldInstance->GetResolvedType();

			BF_ASSERT(resolvedFieldType->mSize >= 0);
			int dataSize = resolvedFieldType->mSize;
			int alignSize = resolvedFieldType->mAlign;
			fieldInstance->mDataSize = dataSize;

			bool needsExplicitAlignment = !isCRepr || resolvedFieldType->NeedsExplicitAlignment();

			int nextDataPos = dataPos;
			if (!isPacked)
				nextDataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
			int padding = nextDataPos - dataPos;
			if ((alignSize > 1) && (needsExplicitAlignment) && (padding > 0))
			{				
				curFieldDataIdx++;							
			}
			dataPos = nextDataPos;
			fieldInstance->mDataOffset = dataPos;
			fieldInstance->mDataIdx = curFieldDataIdx++;

			if (!isPacked)
				typeInstance->mInstAlign = std::max(typeInstance->mInstAlign, alignSize);
			dataPos += dataSize;			
		}

		if (unionInnerType != NULL)
		{
			dataPos = unionInnerType->mSize;
			typeInstance->mInstAlign = BF_MAX(unionInnerType->mAlign, typeInstance->mInstAlign);
		}

		// Old dataMemberHash location		

		CheckMemberNames(typeInstance);

		if (isPacked)
			typeInstance->mInstAlign = 1;
		else
			typeInstance->mInstAlign = std::max(1, typeInstance->mInstAlign);
		int alignSize = typeInstance->mInstAlign;

		if (isCRepr)
		{
			// Align size to alignment
			if (alignSize >= 1)
				typeInstance->mInstSize = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
			typeInstance->mIsCRepr = true;
		}
		else
		{
			typeInstance->mInstSize = dataPos;
			typeInstance->mIsCRepr = false;
		}		

		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		{
			for (auto propDef : typeInstance->mTypeDef->mProperties)
				if (propDef->mFieldDeclaration != NULL)
					mCompiler->mResolvePassData->mAutoComplete->CheckProperty(BfNodeDynCast<BfPropertyDeclaration>(propDef->mFieldDeclaration));
		}
	}
	
	if (typeInstance->IsObjectOrInterface())
		typeInstance->mWantsGCMarking = true;
	if ((mCompiler->mOptions.mEnableRealtimeLeakCheck) && (!typeInstance->mWantsGCMarking))
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(String(BF_METHODNAME_MARKMEMBERS), &entry))
		{
			methodDef = (BfMethodDef*)entry->mMemberDef;
			if (methodDef->HasBody())
				typeInstance->mWantsGCMarking = true;
		}
	}
	
	if (typeInstance->IsValueType())
	{
		typeInstance->mSize = typeInstance->mInstSize;
		typeInstance->mAlign = typeInstance->mInstAlign;
	}	
	
	if ((mCompiler->mOptions.mAllowHotSwapping) && (typeInstance->mDefineState < BfTypeDefineState_Defined))
	{
		if (typeInstance->mHotTypeData == NULL)
			typeInstance->mHotTypeData = new BfHotTypeData();

		// Clear any unused versions (if we have errors, etc)
		if (mCompiler->mHotState != NULL)
			typeInstance->mHotTypeData->ClearVersionsAfter(mCompiler->mHotState->mCommittedHotCompileIdx);
		else
			BF_ASSERT(typeInstance->mHotTypeData->mTypeVersions.IsEmpty()); // We should have created a new HotTypeData when rebuilding the type

		BfHotTypeVersion* hotTypeVersion = new BfHotTypeVersion();
		hotTypeVersion->mTypeId = typeInstance->mTypeId;
		if (typeInstance->mBaseType != NULL)
		{			
			if (typeInstance->mBaseType->mHotTypeData != NULL)
				hotTypeVersion->mBaseType = typeInstance->mBaseType->mHotTypeData->GetLatestVersion();
			else
			{
				AssertErrorState();
			}
		}
		hotTypeVersion->mDeclHotCompileIdx = mCompiler->mOptions.mHotCompileIdx;
		if (mCompiler->IsHotCompile())
			hotTypeVersion->mCommittedHotCompileIdx = -1;
		else
			hotTypeVersion->mCommittedHotCompileIdx = 0;
		hotTypeVersion->mRefCount++;
		typeInstance->mHotTypeData->mTypeVersions.Add(hotTypeVersion);

		if ((typeInstance->mBaseType != NULL) && (typeInstance->mBaseType->mHotTypeData != NULL))
		{
			hotTypeVersion->mMembers.Add(typeInstance->mBaseType->mHotTypeData->GetLatestVersion());
		}

		for (auto& fieldInst : typeInstance->mFieldInstances)
		{
			auto fieldDef = fieldInst.GetFieldDef();
			if ((fieldDef == NULL) || (fieldDef->mIsStatic))
				continue;
			auto depType = fieldInst.mResolvedType;

			while (depType->IsSizedArray())
				depType = ((BfSizedArrayType*)depType)->mElementType;
			if (depType->IsStruct())
			{
				PopulateType(depType);
				auto depTypeInst = depType->ToTypeInstance();
				BF_ASSERT(depTypeInst->mHotTypeData != NULL);
				if (depTypeInst->mHotTypeData != NULL)
					hotTypeVersion->mMembers.Add(depTypeInst->mHotTypeData->GetLatestVersion());
			}
		}

		for (auto member : hotTypeVersion->mMembers)
			member->mRefCount++;

		BfLogSysM("BfHotTypeVersion %p created for type %p\n", hotTypeVersion, typeInstance);
	}

	typeInstance->mDefineState = BfTypeDefineState_Defined;

	if (typeInstance->mTypeFailed)
		mHadBuildError = true;

	CheckAddFailType();
	
	typeInstance->mNeedsMethodProcessing = true;
	typeInstance->mIsFinishingType = false;

	///

	// 'Splattable' means that we can be passed via 3 or fewer primitive/pointer values
	if (typeInstance->IsStruct())
	{
		bool hadNonSplattable = false;

		if (typeInstance->mBaseType != NULL)
			PopulateType(typeInstance->mBaseType, BfPopulateType_Data);
		if ((typeInstance->mBaseType == NULL) || (typeInstance->mBaseType->IsSplattable()))
		{
			int dataCount = 0;
			
			std::function<void(BfType*)> splatIterate;
			splatIterate = [&](BfType* checkType)
			{
				if (checkType->IsMethodRef())
				{
					// For simplicitly, any methodRef inside a struct makes the struct non-splattable.  This reduces cases of needing to 
					//  handle embedded methodRefs
					hadNonSplattable = true;
				}
				else if (checkType->IsStruct())
				{
					PopulateType(checkType, BfPopulateType_Data);
					auto checkTypeInstance = checkType->ToTypeInstance();
					if (checkTypeInstance->mBaseType != NULL)
						splatIterate(checkTypeInstance->mBaseType);

					if (checkTypeInstance->mIsUnion)
					{
						bool wantSplat = false;
						auto unionInnerType = checkTypeInstance->GetUnionInnerType(&wantSplat);
						if (!wantSplat)
							hadNonSplattable = true;

						splatIterate(unionInnerType);

						if (checkTypeInstance->IsEnum())
							dataCount++; // Discriminator
					}
					else
					{
						for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
							if (fieldInstance->mDataIdx >= 0)
								splatIterate(fieldInstance->GetResolvedType());
						}
					}
				}
				else if (!checkType->IsValuelessType())
				{
					if (checkType->IsSizedArray())
						hadNonSplattable = true;
					dataCount += checkType->GetSplatCount();
				}
			};
			splatIterate(typeInstance);

			if (isCRepr)
			{				
				typeInstance->mIsSplattable = false;

				if ((mCompiler->mOptions.mPlatformType == BfPlatformType_Windows) && (mCompiler->mSystem->mPtrSize == 4))
				{					
					// Win32 splat rules
					if ((typeInstance->mBaseType->mInstSize == 0) && (typeInstance->mInstSize <= 16))
					{
						typeInstance->mIsSplattable = true;

						for (int fieldIdx = 0; fieldIdx < (int)typeInstance->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = (BfFieldInstance*)&typeInstance->mFieldInstances[fieldIdx];
							if (fieldInstance->mDataIdx >= 0)
							{
								if (!fieldInstance->mResolvedType->IsPrimitiveType())
									typeInstance->mIsSplattable = false;
								else
								{
									auto primType = (BfPrimitiveType*)fieldInstance->mResolvedType;
									if ((primType->mTypeDef->mTypeCode != BfTypeCode_Int32) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_UInt32) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_IntPtr) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_UIntPtr) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_Pointer) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_Single) &&
										(primType->mTypeDef->mTypeCode != BfTypeCode_Double))
										typeInstance->mIsSplattable = false;
								}
							}
						}
					}
				}
			}
			else
				typeInstance->mIsSplattable = (dataCount <= 3) && (!hadNonSplattable);
		}
	}
	if (typeInstance->IsTypedPrimitive())
		typeInstance->mIsSplattable = true;
	
	BF_ASSERT(mContext->mCurTypeState == &typeState);

	// This is only required for autocomplete and finding type references
	if (!typeInstance->IsSpecializedType())
	{
		for (auto propDef : typeDef->mProperties)
		{
			if (propDef->mTypeRef == NULL)
				continue;

			BfTypeState typeState;
			typeState.mPrevState = mContext->mCurTypeState;
			typeState.mCurTypeDef = propDef->mDeclaringType;
			typeState.mTypeInstance = typeInstance;
			SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
			ResolveTypeRef(propDef->mTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowRef);
		}
	}

	///
	{
		Dictionary<int64, BfFieldDef*> valueMap;

		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			if (!fieldInstance->mFieldIncluded)
				continue;
			auto fieldDef = fieldInstance->GetFieldDef();
			if (fieldDef == NULL)
				continue;
			if ((fieldInstance->mConstIdx == -1) && (fieldDef->mIsConst))
			{
				SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);
				typeInstance->mModule->ResolveConstField(typeInstance, fieldInstance, fieldDef);

				auto underlyingType = fieldInstance->mResolvedType->GetUnderlyingType();
				if ((fieldDef->IsEnumCaseEntry()) && (fieldInstance->mConstIdx != -1) && (underlyingType->IsIntegral()))
				{
					auto foreignConst = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
					BfFieldDef** fieldDefPtr;
					if (valueMap.TryAdd(foreignConst->mInt64, NULL, &fieldDefPtr))
					{
						*fieldDefPtr = fieldDef;
					}
					else if ((typeInstance->mCustomAttributes == NULL) || (typeInstance->mCustomAttributes->Get(mCompiler->mAllowDuplicatesAttributeTypeDef) == NULL))
					{
						auto error = Warn(0, StrFormat("Enum value '%lld' for field '%s' is not unique. Considering adding [AllowDuplicates] to the type declaration.", foreignConst->mInt64, fieldDef->mName.c_str()), fieldDef->GetRefNode(), true);
						if (error != NULL)
							mCompiler->mPassInstance->MoreInfo(StrFormat("This value was previously used for field '%s'", (*fieldDefPtr)->mName.c_str()), (*fieldDefPtr)->GetRefNode());
					}
				}				
			}
		}
	}
	
	if ((typeInstance->IsEnum()) && (!typeInstance->IsPayloadEnum()))
	{
		BfLogSysM("Setting underlying type %p %d\n", typeInstance, underlyingTypeDeferred);
	}

	if (typeInstance->IsEnum())
	{
		int64 min = 0;
		int64 max = 0;

		bool isFirst = false;

		if (typeInstance->mTypeInfoEx == NULL)
			typeInstance->mTypeInfoEx = new BfTypeInfoEx();

		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			if ((fieldDef != NULL) && (fieldDef->IsEnumCaseEntry()))
			{
				if (fieldInstance->mConstIdx == -1)
					continue;

				auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
				BF_ASSERT((constant->mTypeCode == BfTypeCode_Int64) || (!underlyingTypeDeferred));

				if (isFirst)
				{
					min = constant->mInt64;
					max = constant->mInt64;
					isFirst = false;
				}
				else
				{
					min = BF_MIN(constant->mInt64, min);
					max = BF_MAX(constant->mInt64, max);
				}
			}
		}		
		
		typeInstance->mTypeInfoEx->mMinValue = min;
		typeInstance->mTypeInfoEx->mMaxValue = max;

		if (underlyingTypeDeferred)
		{			
			BfTypeCode typeCode;

			if ((min >= -0x80) && (max <= 0x7F))
				typeCode = BfTypeCode_Int8;
			else if ((min >= 0) && (max <= 0xFF))
				typeCode = BfTypeCode_UInt8;
			else if ((min >= -0x8000) && (max <= 0x7FFF))
				typeCode = BfTypeCode_Int16;
			else if ((min >= 0) && (max <= 0xFFFF))
				typeCode = BfTypeCode_UInt16;
			else if ((min >= -0x80000000LL) && (max <= 0x7FFFFFFF))
				typeCode = BfTypeCode_Int32;
			else if ((min >= 0) && (max <= 0xFFFFFFFFLL))
				typeCode = BfTypeCode_UInt32;
			else
				typeCode = BfTypeCode_Int64;

			if (typeCode != BfTypeCode_Int64)
			{
				for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
				{
					auto fieldInstance = &fieldInstanceRef;
					if (fieldInstance->mConstIdx != -1)
					{
						auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
						BfIRValue newConstant = typeInstance->mConstHolder->CreateConst(typeCode, constant->mUInt64);
						fieldInstance->mConstIdx = newConstant.mId;
					}
				}
			}

			underlyingType = GetPrimitiveType(typeCode);
			auto fieldInstance = &typeInstance->mFieldInstances.back();
			fieldInstance->mResolvedType = underlyingType;
			fieldInstance->mDataSize = underlyingType->mSize;

			typeInstance->mTypeInfoEx->mUnderlyingType = underlyingType;

			typeInstance->mSize = underlyingType->mSize;
			typeInstance->mAlign = underlyingType->mAlign;
			typeInstance->mInstSize = underlyingType->mSize;
			typeInstance->mInstAlign = underlyingType->mAlign;

			typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags & ~BfTypeRebuildFlag_UnderlyingTypeDeferred);
		}
	}
	else
	{
		BF_ASSERT(!underlyingTypeDeferred);
	}

	if ((typeInstance->IsPayloadEnum()) && (!typeInstance->IsBoxed()))
	{
		int lastTagId = -1;
		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			if ((fieldDef != NULL) && (fieldInstance->mDataIdx < 0))
				lastTagId = -fieldInstance->mDataIdx - 1;
		}

		auto fieldInstance = &typeInstance->mFieldInstances.back();
		BF_ASSERT(fieldInstance->mResolvedType == NULL);
		BfPrimitiveType* discriminatorType;
		if (lastTagId > 0x7FFFFFFF) // HOW?
			discriminatorType = GetPrimitiveType(BfTypeCode_Int64);
		else if (lastTagId > 0x7FFF)
			discriminatorType = GetPrimitiveType(BfTypeCode_Int32);
		else if (lastTagId > 0x7F)
			discriminatorType = GetPrimitiveType(BfTypeCode_Int16);
		else
			discriminatorType = GetPrimitiveType(BfTypeCode_Int8);
		fieldInstance->mResolvedType = discriminatorType;

		fieldInstance->mDataOffset = unionInnerType->mSize;
		fieldInstance->mDataIdx = 2; // 0 = base, 1 = payload, 2 = discriminator
		if (!isPacked)
		{
			if ((fieldInstance->mDataOffset % discriminatorType->mAlign) != 0)
			{
				fieldInstance->mDataOffset = BF_ALIGN(fieldInstance->mDataOffset, discriminatorType->mAlign);				
				fieldInstance->mDataIdx++; // Add room for explicit padding
			}
		}
		
		typeInstance->mAlign = BF_MAX(unionInnerType->mAlign, discriminatorType->mAlign);
		typeInstance->mSize = fieldInstance->mDataOffset + discriminatorType->mSize;

		typeInstance->mInstSize = typeInstance->mSize;
		typeInstance->mInstAlign = typeInstance->mAlign;

		dataMemberHashCtx.Mixin(unionInnerType->mTypeId);
		dataMemberHashCtx.Mixin(discriminatorType->mTypeId);

		typeInstance->mMergedFieldDataCount = 1; // Track it as a single entry
	}

	if (!typeInstance->IsBoxed())
	{
		if (typeInstance->IsTypedPrimitive())
		{
			auto underlyingType = typeInstance->GetUnderlyingType();
			dataMemberHashCtx.Mixin(underlyingType->mTypeId);
		}

		Val128 dataMemberHash = dataMemberHashCtx.Finish128();

		if (typeInstance->mHotTypeData != NULL)
		{
			auto newHotTypeVersion = typeInstance->mHotTypeData->GetLatestVersion();			
			newHotTypeVersion->mDataHash = dataMemberHash;

			if (mCompiler->mHotState != NULL)
			{
				auto committedHotTypeVersion = typeInstance->mHotTypeData->GetTypeVersion(mCompiler->mHotState->mCommittedHotCompileIdx);
				if (committedHotTypeVersion != NULL)
				{
					if ((newHotTypeVersion->mDataHash != committedHotTypeVersion->mDataHash) && (typeInstance->mIsReified))
					{
						BfLogSysM("Hot compile detected data changes in %p '%s'\n", resolvedTypeRef, TypeToString(typeInstance).c_str());

						if (!typeInstance->mHotTypeData->mPendingDataChange)
						{
							mCompiler->mHotState->mPendingDataChanges.Add(typeInstance->mTypeId);
							typeInstance->mHotTypeData->mPendingDataChange = true;							
						}
						else
						{
							BF_ASSERT(mCompiler->mHotState->mPendingDataChanges.Contains(typeInstance->mTypeId));
						}

						bool baseHadChanges = (typeInstance->mBaseType != NULL) && (typeInstance->mBaseType->mHotTypeData != NULL) && (typeInstance->mBaseType->mHotTypeData->mPendingDataChange);
						if (!baseHadChanges)
							Warn(0, StrFormat("Hot compile detected data changes in '%s'", TypeToString(typeInstance).c_str()), typeDef->GetRefNode());
					}
					else if (typeInstance->mHotTypeData->mPendingDataChange)
					{
						BfLogSysM("Hot compile removed pending data change for %p '%s'\n", resolvedTypeRef, TypeToString(typeInstance).c_str());
						mCompiler->mHotState->RemovePendingChanges(typeInstance);
					}
				}
			}
		}
	}

	if (typeInstance == mContext->mBfObjectType)
		typeInstance->mHasBeenInstantiated = true;

	if (populateType == BfPopulateType_Data)
		return true;

	disableYield.Release();

	if (canDoMethodProcessing)
	{
		if (typeInstance->mNeedsMethodProcessing) // May have been handled by GetRawMethodInstanceAtIdx above
			DoTypeInstanceMethodProcessing(typeInstance);
	}

	return true;
}

void BfModule::DoTypeInstanceMethodProcessing(BfTypeInstance* typeInstance)
{	
	if (typeInstance->IsSpecializedByAutoCompleteMethod())
		return;
	
	BF_ASSERT(typeInstance->mModule == this);

	//TODO: This is new, make sure this is in the right place
	/*if (mAwaitingInitFinish)
		FinishInit();*/

	AutoDisallowYield disableYield(mSystem);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

	BfLogSysM("DoTypeInstanceMethodProcessing: %p %s Revision:%d\n", typeInstance, TypeToString(typeInstance).c_str(), typeInstance->mRevision);

	auto typeDef = typeInstance->mTypeDef;

	// Generate all methods. Pass 0
	for (auto methodDef : typeDef->mMethods)
	{
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

		// This should still be set to the default value
		BF_ASSERT((methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet) || (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude));
	}


	if (typeInstance == mContext->mBfObjectType)
	{
		BF_ASSERT(typeInstance->mInterfaceMethodTable.size() == 0);
	}

	int newIntefaceStartIdx = 0;
	auto implBaseType = typeInstance->GetImplBaseType();	
	if (implBaseType != NULL)
	{
		auto baseTypeInst = implBaseType->ToTypeInstance();
		if (implBaseType->IsIncomplete())
			PopulateType(implBaseType, BfPopulateType_Full_Force);

		typeInstance->mInterfaceMethodTable = baseTypeInst->mInterfaceMethodTable;				
		typeInstance->mVirtualMethodTable = implBaseType->mVirtualMethodTable;
		typeInstance->mVirtualMethodTableSize = implBaseType->mVirtualMethodTableSize;
		if ((!mCompiler->IsHotCompile()) && (!mCompiler->mPassInstance->HasFailed()) && ((mCompiler->mResolvePassData == NULL) || (mCompiler->mResolvePassData->mAutoComplete == NULL)))
		{
			BF_ASSERT(typeInstance->mVirtualMethodTable.size() == typeInstance->mVirtualMethodTableSize);
		}
		else
		{
			BF_ASSERT(typeInstance->mVirtualMethodTableSize >= (int)typeInstance->mVirtualMethodTable.size());
		}
	}	

	// Add new interfaces		
	for (int iFaceIdx = 0; iFaceIdx < (int)typeInstance->mInterfaces.size(); iFaceIdx++)
	{
		BfTypeInterfaceEntry& typeInterfaceInst = typeInstance->mInterfaces[iFaceIdx];		

		auto checkInterface = typeInterfaceInst.mInterfaceType;
		if (checkInterface->IsIncomplete())			
			PopulateType(checkInterface, BfPopulateType_Full_Force);
		
		typeInterfaceInst.mStartInterfaceTableIdx = (int)typeInstance->mInterfaceMethodTable.size();					

		// We don't add to the vtable for interface declarations, we just reference the listed interfaces
		if (!typeInstance->IsInterface())
		{
			auto interfaceTypeDef = checkInterface->mTypeDef;
			BF_ASSERT(interfaceTypeDef->mMethods.size() == checkInterface->mMethodInstanceGroups.size());

			// Reserve empty entries
			for (int methodIdx = 0; methodIdx < (int)interfaceTypeDef->mMethods.size(); methodIdx++)			
				typeInstance->mInterfaceMethodTable.push_back(BfTypeInterfaceMethodEntry());			
		}		
	}

	auto checkTypeInstance = typeInstance;
	while (checkTypeInstance != NULL)
	{
		for (auto&& interfaceEntry : checkTypeInstance->mInterfaces)
		{
			AddDependency(interfaceEntry.mInterfaceType, typeInstance, BfDependencyMap::DependencyFlag_ImplementsInterface);
		}
		checkTypeInstance = checkTypeInstance->GetImplBaseType();
	}

	//for (auto& intefaceInst : typeInstance->mInterfaces)

	if (typeInstance == mContext->mBfObjectType)
	{
		BF_ASSERT(typeInstance->mInterfaceMethodTable.size() == 1);
	}
	
	// Slot interfaces method blocks in vtable
	{
		int ifaceVirtIdx = 0;
		std::unordered_map<BfTypeInstance*, BfTypeInterfaceEntry*> interfaceMap;
		BfTypeInstance* checkType = typeInstance->GetImplBaseType();
		while (checkType != NULL)
		{
			for (auto&& ifaceEntry : checkType->mInterfaces)
			{
				interfaceMap[ifaceEntry.mInterfaceType] = &ifaceEntry;
				ifaceVirtIdx = std::max(ifaceVirtIdx, ifaceEntry.mStartVirtualIdx + ifaceEntry.mInterfaceType->mVirtualMethodTableSize);
			}
			checkType = checkType->GetImplBaseType();
		}

		for (int iFaceIdx = 0; iFaceIdx < (int)typeInstance->mInterfaces.size(); iFaceIdx++)
		{
			BfTypeInterfaceEntry& typeInterfaceInst = typeInstance->mInterfaces[iFaceIdx];
			auto itr = interfaceMap.find(typeInterfaceInst.mInterfaceType);
			if (itr != interfaceMap.end())
			{
				auto prevEntry = itr->second;
				typeInterfaceInst.mStartVirtualIdx = prevEntry->mStartVirtualIdx;
			}
			else
			{
				typeInterfaceInst.mStartVirtualIdx = ifaceVirtIdx;
				ifaceVirtIdx += typeInterfaceInst.mInterfaceType->mVirtualMethodTableSize;
				interfaceMap[typeInterfaceInst.mInterfaceType] = &typeInterfaceInst;
			}
		}
	}

	auto isBoxed = typeInstance->IsBoxed();

	typeInstance->mNeedsMethodProcessing = false;
	typeInstance->mTypeIncomplete = false;

	auto checkBaseType = typeInstance->GetImplBaseType();
	while (checkBaseType != NULL)
	{
		PopulateType(checkBaseType, BfPopulateType_Full_Force);
		BF_ASSERT((!checkBaseType->IsIncomplete()) || (checkBaseType->mTypeFailed));
		checkBaseType = checkBaseType->GetImplBaseType();
	}

	if ((mCompiler->mOptions.mHasVDataExtender) && (!typeInstance->IsInterface()))
	{
		// This is the vExt entry for this type instance
		BfVirtualMethodEntry entry;		
		entry.mDeclaringMethod.mMethodNum = -1;
		entry.mDeclaringMethod.mTypeInstance = typeInstance;
		typeInstance->mVirtualMethodTable.push_back(entry);
		typeInstance->mVirtualMethodTableSize++;		
	}

	// Fill out to correct size
	if (typeInstance->mHotTypeData != NULL)
	{		
		//auto hotLatestVersionHead = typeInstance->mHotTypeData->GetLatestVersionHead();
		int wantVTableSize = typeInstance->GetImplBaseVTableSize() + (int)typeInstance->mHotTypeData->mVTableEntries.size();
		while ((int)typeInstance->mVirtualMethodTable.size() < wantVTableSize)
		{
			typeInstance->mVirtualMethodTable.push_back(BfVirtualMethodEntry());
			typeInstance->mVirtualMethodTableSize++;
		}
	}
	
	BfAmbiguityContext ambiguityContext;
	ambiguityContext.mTypeInstance = typeInstance;
	ambiguityContext.mModule = this;
	ambiguityContext.mIsProjectSpecific = false;

	bool wantsOnDemandMethods = false;
	//TODO: Testing having interface methods be "on demand"...
	//if (!typeInstance->IsInterface())

	//
	{
		if (typeInstance->IsSpecializedType())
			wantsOnDemandMethods = true;
		else if ((mCompiler->mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude) &&
			(!typeInstance->IsUnspecializedTypeVariation()))
		{
			//if (typeDef->mName->ToString() != "AttributeUsageAttribute")

			auto attributeDef = mCompiler->mAttributeTypeDef;
			auto attributeType = mContext->mUnreifiedModule->ResolveTypeDef(attributeDef, BfPopulateType_Identity)->ToTypeInstance();			
			if (!TypeIsSubTypeOf(mCurTypeInstance, attributeType, false))
			{
				wantsOnDemandMethods = true;
			}
		}
	}

	//bool allDeclsRequired = (mIsReified) && (mCompiler->mOptions.mEmitDebugInfo) && ();
	bool allDeclsRequired = false;
	//if ((mIsReified) && (mCompiler->mOptions.mEmitDebugInfo) && (!mCompiler->mWantsDeferMethodDecls))
// 	if ((mIsReified) && (mCompiler->mOptions.mEmitDebugInfo))
// 	{
// 		allDeclsRequired = true;
// 	}

	HashSet<String> ifaceMethodNameSet;
	if (wantsOnDemandMethods)
	{
		for (int iFaceIdx = newIntefaceStartIdx; iFaceIdx < (int)typeInstance->mInterfaces.size(); iFaceIdx++)
		{
			BfTypeInterfaceEntry& typeInterfaceInst = typeInstance->mInterfaces[iFaceIdx];
			for (auto checkMethodDef : typeInterfaceInst.mInterfaceType->mTypeDef->mMethods)
			{
				ifaceMethodNameSet.Add(checkMethodDef->mName);				
			}
		}
	}

	// Generate all methods. Pass 1
	for (auto methodDef : typeDef->mMethods)
	{			
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

		if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude)
			continue;

		// This should still be set to the default value
		BF_ASSERT(methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet);

		if ((isBoxed) && (!methodDef->mIsVirtual))
		{
			if (methodDef->mIsStatic)
				continue;
			bool boxedRequired = false;
			if (((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mParams.size() == 0)) ||			
				(methodDef->mMethodType == BfMethodType_Dtor) ||
				((methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC) || (methodDef->mName == BF_METHODNAME_INVOKE) || (methodDef->mName == BF_METHODNAME_DYNAMICCAST)) ||
				(methodDef->mGenericParams.size() != 0))
				boxedRequired = true;
			if (!boxedRequired)
			{
				if (wantsOnDemandMethods)
					methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
				continue;
			}
		}

		if (methodDef->mMethodType == BfMethodType_Ignore)
			continue;		

		if ((methodDef->mName == BF_METHODNAME_DYNAMICCAST) && (typeInstance->IsValueType()))
			continue; // This is just a placeholder for boxed types

		methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
		
		if (wantsOnDemandMethods)
		{	
			bool implRequired = false;
			bool declRequired = false;

			if ((!typeInstance->IsGenericTypeInstance()) && (methodDef->mGenericParams.IsEmpty()))
			{
				// For non-generic methods, declare all methods. This is useful for debug info.
				declRequired = true;
			}

			if (methodDef->mMethodType == BfMethodType_CtorNoBody)
				declRequired = true;

			if ((methodDef->mIsStatic) &&
				((methodDef->mMethodType == BfMethodType_Dtor) || (methodDef->mMethodType == BfMethodType_Ctor)))
			{
				implRequired = true;
			}

			if (mCompiler->mOptions.mEnableRealtimeLeakCheck)
			{
				if ((methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC) ||
					(methodDef->mName == BF_METHODNAME_FIND_TLS_MEMBERS) ||
					((methodDef->mName == BF_METHODNAME_MARKMEMBERS) && (typeInstance->IsObject())))
					implRequired = true;
			}

			BfAttributeDirective* attributes = NULL;
			if (auto methodDeclaration = methodDef->GetMethodDeclaration())
				attributes = methodDeclaration->mAttributes;
			if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
				attributes = propertyDeclaration->mAttributes;
			while (attributes != NULL)
			{
				if (attributes->mAttributeTypeRef != NULL)
				{
					auto typeRefName = attributes->mAttributeTypeRef->ToString();

					if (typeRefName == "AlwaysInclude")
						implRequired = true;
					else if (typeRefName == "Export")
						implRequired = true;
					else if (typeRefName == "Test")
						implRequired = true;
					else
						declRequired = true; // We need to create so we can check for AlwaysInclude in included attributes
				}

				attributes = attributes->mNextAttribute;
			}
			if ((mProject != NULL) && (mProject->mAlwaysIncludeAll) && (methodDef->mBody != NULL))
			{				
				implRequired = true;
				declRequired = true;
			}

			if (typeInstance->IsInterface())
				declRequired = true;

			if (methodDef->mIsVirtual)
				declRequired = true;

			if (!implRequired)
			{
				// Any interface with the same name causes us to not be on-demand
				if (ifaceMethodNameSet.Contains(methodDef->mName))
					declRequired = true;
			}			

			// Is this strictly necessary? It will reduce our compilation speed in order to ensure methods are available for debug info
			if (allDeclsRequired)
				declRequired = true;

			if (methodDef->mMethodDeclaration == NULL)
			{
				// Internal methods don't need decls
				if (methodDef->mName == BF_METHODNAME_DEFAULT_EQUALS)
					declRequired = false;
			}

			if (!implRequired)
			{
				if (!mIsScratchModule)
					mOnDemandMethodCount++;

				if (!declRequired)
				{
					methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
					continue;
				}
				else
				{
					methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
				}
			}
		}		
	}

	BfLogSysM("Starting DoTypeInstanceMethodProcessing %p GetMethodInstance pass.  OnDemandMethods: %d\n", typeInstance, mOnDemandMethodCount);

	// Pass 2
	for (auto methodDef : typeDef->mMethods)
	{
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

		if ((methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude) &&
			(methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Decl_AwaitingDecl))
		{
			BfLogSysM("Skipping GetMethodInstance on MethodDef: %p OnDemandKind: %d\n", methodDef, methodInstanceGroup->mOnDemandKind);
			continue;
		}

		int prevWorklistSize = (int)mContext->mMethodWorkList.size();
		auto moduleMethodInstance = GetMethodInstance(typeInstance, methodDef, BfTypeVector(), ((methodDef->mGenericParams.size() != 0) || (typeInstance->IsUnspecializedType())) ? BfGetMethodInstanceFlag_UnspecializedPass : BfGetMethodInstanceFlag_None);
				
		auto methodInstance = moduleMethodInstance.mMethodInstance;
		if (methodInstance == NULL)
		{
			BF_ASSERT(typeInstance->IsGenericTypeInstance() && (typeInstance->mTypeDef->mIsCombinedPartial));
			continue;
		}

		if ((!mCompiler->mIsResolveOnly) &&
			((methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference) || (!typeInstance->IsReified())))
		{
			bool forceMethodImpl = false;

			BfCustomAttributes* customAttributes = methodInstance->GetCustomAttributes();
			if ((customAttributes != NULL) && (typeInstance->IsReified()))
			{
				for (auto& attr : customAttributes->mAttributes)
				{
					auto attrTypeInst = attr.mType->ToTypeInstance();
					auto attrCustomAttributes = attrTypeInst->mCustomAttributes;
					if (attrCustomAttributes == NULL)
						continue;

					for (auto& attrAttr : attrCustomAttributes->mAttributes)
					{
						if (attrAttr.mType->ToTypeInstance()->mTypeDef == mCompiler->mAttributeUsageAttributeTypeDef)
						{
							// Check for Flags arg
							if (attrAttr.mCtorArgs.size() < 2)
								continue;														
							auto constant = attrTypeInst->mConstHolder->GetConstant(attrAttr.mCtorArgs[1]);								
							if (constant == NULL)
								continue;
							if (constant->mTypeCode == BfTypeCode_Boolean)
								continue;
							if ((constant->mInt8 & BfCustomAttributeFlags_AlwaysIncludeTarget) != 0)
								forceMethodImpl = true;
						}
					}
				}
			}

			if (typeInstance->mTypeDef->mProject->mTargetType == BfTargetType_BeefTest)
			{
				if ((customAttributes != NULL) && (customAttributes->Contains(mCompiler->mTestAttributeTypeDef)))
				{
					forceMethodImpl = true;					
				}
			}

			if (forceMethodImpl)
			{
				if (!typeInstance->IsReified())
					mContext->mScratchModule->PopulateType(typeInstance, BfPopulateType_Data);
				// Reify method
				mContext->mScratchModule->GetMethodInstance(typeInstance, methodDef, BfTypeVector());
				BF_ASSERT(methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Decl_AwaitingReference);
			}
		}


		bool methodUsedVirtually = false;
		if (typeInstance->IsInterface())
		{
			if ((!methodDef->mIsConcrete) && (!methodDef->mIsStatic) && (!methodInstance->HasSelf()))
				SlotInterfaceMethod(methodInstance);
		}
		else if (!methodDef->IsEmptyPartial())
		{
			methodUsedVirtually = SlotVirtualMethod(methodInstance, &ambiguityContext);
		}

		// This is important for reducing latency of autocomplete popup, but it's important we don't allow the autocomplete
		//  thread to cause any reentry issues by re-populating a type at an "inopportune time".  We do allow certain 
		//  reentries in PopulateType, but not when we're resolving fields (for example)
		if ((mContext->mFieldResolveReentrys.size() == 0) && (!mContext->mResolvingVarField))
		{
			disableYield.Release();
			mSystem->CheckLockYield();
			disableYield.Acquire();
		}
	}


	BF_ASSERT(typeInstance->mVirtualMethodTable.size() == typeInstance->mVirtualMethodTableSize);

	if ((isBoxed) && (!typeInstance->IsUnspecializedTypeVariation()))
	{
		// Any interface method that can be called virtually via an interface pointer needs to go into the boxed type
		auto underlyingType = typeInstance->GetUnderlyingType();
		BfTypeInstance* underlyingTypeInstance;
		if (underlyingType->IsPrimitiveType())
			underlyingTypeInstance = GetPrimitiveStructType(((BfPrimitiveType*)underlyingType)->mTypeDef->mTypeCode);
		else
			underlyingTypeInstance = underlyingType->ToTypeInstance();
		if (underlyingTypeInstance != NULL)
		{
			PopulateType(underlyingTypeInstance, BfPopulateType_Full_Force);
			for (int ifaceIdx = 0; ifaceIdx < (int)underlyingTypeInstance->mInterfaces.size(); ifaceIdx++)
			{
				auto& underlyingIFaceTypeInst = underlyingTypeInstance->mInterfaces[ifaceIdx];
				auto& boxedIFaceTypeInst = typeInstance->mInterfaces[ifaceIdx];
				BF_ASSERT(underlyingIFaceTypeInst.mInterfaceType == boxedIFaceTypeInst.mInterfaceType);

				auto ifaceInst = underlyingIFaceTypeInst.mInterfaceType;
				int startIdx = underlyingIFaceTypeInst.mStartInterfaceTableIdx;
				int boxedStartIdx = boxedIFaceTypeInst.mStartInterfaceTableIdx;
				int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();

				for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)
				{
					auto matchedMethodRef = &underlyingTypeInstance->mInterfaceMethodTable[iMethodIdx + startIdx].mMethodRef;
					auto boxedMatchedMethodRef = &typeInstance->mInterfaceMethodTable[iMethodIdx + boxedStartIdx].mMethodRef;
					BfMethodInstance* matchedMethod = *matchedMethodRef;
					auto ifaceMethodInst = ifaceInst->mMethodInstanceGroups[iMethodIdx].mDefault;
					if (ifaceMethodInst->mVirtualTableIdx != -1)
					{
						if (matchedMethod == NULL)
						{
							AssertErrorState();
						}
						else
						{
							if (!matchedMethod->mIsForeignMethodDef)
							{
								BfMethodInstanceGroup* boxedMethodInstanceGroup = &typeInstance->mMethodInstanceGroups[matchedMethod->mMethodDef->mIdx];
								if (boxedMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference)
								{
									boxedMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
									if (!mIsScratchModule)
										mOnDemandMethodCount++;									
								}
							}							

							auto moduleMethodInstance = GetMethodInstance(typeInstance, matchedMethod->mMethodDef, BfTypeVector(),
								matchedMethod->mIsForeignMethodDef ? BfGetMethodInstanceFlag_ForeignMethodDef : BfGetMethodInstanceFlag_None,
								matchedMethod->GetForeignType());
							auto methodInstance = moduleMethodInstance.mMethodInstance;
							UniqueSlotVirtualMethod(methodInstance);
							*boxedMatchedMethodRef = methodInstance;
						}
					}
				}
			}
		}
	}

	if (typeInstance->mHotTypeData != NULL)
	{
		auto latestVersion = typeInstance->mHotTypeData->GetLatestVersion();
		auto latestVersionHead = typeInstance->mHotTypeData->GetLatestVersionHead();		
		if (typeInstance->mHotTypeData->mVTableOrigLength != -1)
		{
			bool hasSlotError = false;

			BF_ASSERT(mCompiler->IsHotCompile());
			//typeInstance->mHotTypeData->mDirty = true;
			//Val128 vtHash;
			Array<int> ifaceMapping;
			ifaceMapping.Resize(latestVersionHead->mInterfaceMapping.size());
			typeInstance->CalcHotVirtualData(&ifaceMapping);

			// Hot swapping allows for interfaces to be added to types or removed from types, but it doesn't allow
			//  interfaces to be added when the slot number has already been used -- even if the interface using
			//  that slot has been removed.
			for (int slotIdx = 0; slotIdx < (int)ifaceMapping.size(); slotIdx++)
			{
				int newId = ifaceMapping[slotIdx];
				int oldId = 0;
				if (slotIdx < (int)latestVersionHead->mInterfaceMapping.size())
					oldId = latestVersionHead->mInterfaceMapping[slotIdx];

				if ((newId != oldId) && (newId != 0) && (oldId != 0))
				{
					String interfaceName;
					for (auto iface : typeInstance->mInterfaces)
					{
						if (iface.mInterfaceType->mTypeId == newId)
							interfaceName = TypeToString(iface.mInterfaceType);
					}

					Warn(0, StrFormat("Hot swap detected resolvable interface slot collision with '%s'.", interfaceName.c_str()), typeDef->mTypeDeclaration);
					BF_ASSERT(latestVersion != latestVersionHead);
					if (!hasSlotError)
					{
						latestVersion->mInterfaceMapping = ifaceMapping;
					}
					hasSlotError = true;					
				}
				else if (hasSlotError)
				{
					if (oldId != 0)
						latestVersion->mInterfaceMapping[slotIdx] = oldId;
				}

				if (oldId != 0)
					ifaceMapping[slotIdx] = oldId;
			}
			latestVersionHead->mInterfaceMapping = ifaceMapping;

			if (hasSlotError)
				mCompiler->mHotState->mPendingFailedSlottings.Add(typeInstance->mTypeId);
			else
				mCompiler->mHotState->mPendingFailedSlottings.Remove(typeInstance->mTypeId);
		}
	}
	
	if ((typeInstance->IsInterface()) && (!typeInstance->IsUnspecializedType()) && (typeInstance->mIsReified) && (typeInstance->mSlotNum == -1) && (mCompiler->IsHotCompile()))
	{
		mCompiler->mHotState->mHasNewInterfaceTypes = true;		
	}

	if ((!typeInstance->IsInterface()) && (!typeInstance->IsUnspecializedTypeVariation()) && (!isBoxed))
	{
		if (!typeInstance->mTypeDef->mIsAbstract)
		{
			for (int methodIdx = 0; methodIdx < (int) typeInstance->mVirtualMethodTable.size(); methodIdx++)
			{
				auto& methodRef = typeInstance->mVirtualMethodTable[methodIdx].mImplementingMethod;
				if (methodRef.mMethodNum == -1)
				{
					BF_ASSERT(mCompiler->mOptions.mHasVDataExtender);
					if (methodRef.mTypeInstance == typeInstance)
					{
						if (typeInstance->GetImplBaseType() != NULL)
							BF_ASSERT(methodIdx == (int)typeInstance->GetImplBaseType()->mVirtualMethodTableSize);
					}
					continue;
				}
				auto methodInstance = (BfMethodInstance*)methodRef;
				if ((methodInstance != NULL) && (methodInstance->mMethodDef->mIsAbstract))
				{
					if (methodInstance->mMethodDef->mIsAbstract)
					{
						if (!typeInstance->IsUnspecializedTypeVariation())
						{
							if (Fail(StrFormat("'%s' does not implement inherited abstract method '%s'", TypeToString(typeInstance).c_str(), MethodToString(methodInstance).c_str()), typeDef->mTypeDeclaration->mNameNode, true) != NULL)
								mCompiler->mPassInstance->MoreInfo("Abstract method declared", methodInstance->mMethodDef->GetRefNode());
						}
					}
					else
					{
						if (!typeInstance->IsUnspecializedType())
							AssertErrorState();
					}
				}
			}
		}

		std::unordered_set<String> missingIFaceMethodNames;
		for (auto& ifaceTypeInst : typeInstance->mInterfaces)
		{
			auto ifaceInst = ifaceTypeInst.mInterfaceType;
			int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
			int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();
			auto declTypeDef = ifaceTypeInst.mDeclaringType;

			for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)
			{
				auto matchedMethodRef = &typeInstance->mInterfaceMethodTable[iMethodIdx + startIdx].mMethodRef;
				BfMethodInstance* matchedMethod = *matchedMethodRef;
				auto ifaceMethodInst = ifaceInst->mMethodInstanceGroups[iMethodIdx].mDefault;
				if ((matchedMethod == NULL) && (ifaceMethodInst != NULL))
				{
					missingIFaceMethodNames.insert(ifaceMethodInst->mMethodDef->mName);
				}
			}
		}

		if (!missingIFaceMethodNames.empty())
		{
			// Attempt to find matching entries in base types
			ambiguityContext.mIsReslotting = true;
			auto checkType = typeInstance->GetImplBaseType();
			while (checkType != NULL)
			{
				for (auto& methodGroup : checkType->mMethodInstanceGroups)
				{
					auto methodInstance = methodGroup.mDefault;
					if (methodInstance != NULL)
					{
						if ((methodInstance->mMethodDef->mProtection != BfProtection_Private) &&
							(!methodInstance->mMethodDef->mIsOverride) &&
							(missingIFaceMethodNames.find(methodInstance->mMethodDef->mName) != missingIFaceMethodNames.end()))
						{							
							SlotVirtualMethod(methodInstance, &ambiguityContext);
						}
					}
				}
				checkType = checkType->GetImplBaseType();
			}
		}

		for (auto& ifaceTypeInst : typeInstance->mInterfaces)
		{			
			auto ifaceInst = ifaceTypeInst.mInterfaceType;
			int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
			int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();
			auto declTypeDef = ifaceTypeInst.mDeclaringType;

			for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)
			{
				auto matchedMethodRef = &typeInstance->mInterfaceMethodTable[iMethodIdx + startIdx].mMethodRef;
				BfMethodInstance* matchedMethod = *matchedMethodRef;
				auto ifaceMethodInst = ifaceInst->mMethodInstanceGroups[iMethodIdx].mDefault;
				if (ifaceMethodInst == NULL)
					continue;

				// Don't even try to match generics
				if (!ifaceMethodInst->mMethodDef->mGenericParams.IsEmpty())
					continue;

				auto iReturnType = ifaceMethodInst->mReturnType;
				if (iReturnType->IsSelf())
					iReturnType = typeInstance;

				if (ifaceMethodInst->mMethodDef->mIsOverride)
					continue; // Don't consider overrides here

				// If we have "ProjA depends on LibBase", "ProjB depends on LibBase", then a type ClassC in LibBase implementing IFaceD,
				//  where IFaceD gets extended with MethodE in ProjA, an implementing MethodE is still required to exist on ClassC -- 
				//  the visibility is bidirectional.  A type ClassF implementing IFaceD inside ProjB will not be required to implement
				//  MethodE, however
				if ((!ifaceInst->IsTypeMemberAccessible(ifaceMethodInst->mMethodDef->mDeclaringType, ifaceTypeInst.mDeclaringType)) &&
					(!ifaceInst->IsTypeMemberAccessible(ifaceTypeInst.mDeclaringType, ifaceMethodInst->mMethodDef->mDeclaringType)))
					continue;

				if (!ifaceInst->IsTypeMemberIncluded(ifaceMethodInst->mMethodDef->mDeclaringType, ifaceTypeInst.mDeclaringType))
					continue;

				bool hadMatch = matchedMethod != NULL;
				bool hadPubFailure = false;
				bool hadMutFailure = false;

				if (hadMatch)
				{
					if ((matchedMethod->GetExplicitInterface() == NULL) && (matchedMethod->mMethodDef->mProtection != BfProtection_Public))
					{
						hadMatch = false;
						hadPubFailure = true;						
					}
					
					if (ifaceMethodInst->mVirtualTableIdx != -1)
					{
						if (matchedMethod->mReturnType != iReturnType)
							hadMatch = false;
					}
					else 
					{
						// Concrete
						if (matchedMethod->mReturnType->IsInterface())
							hadMatch = false;
						else if (!CanCast(GetFakeTypedValue(matchedMethod->mReturnType), iReturnType))
							hadMatch = false;
					}
					
					// If we have mExplicitInterface set then we already gave a mut error (if needed)					
					if ((typeInstance->IsValueType()) && (matchedMethod->GetExplicitInterface() == NULL) &&
						(matchedMethod->mMethodDef->mIsMutating) && (!ifaceMethodInst->mMethodDef->mIsMutating))
					{
						hadMutFailure = true;
						hadMatch = false;
					}										
				}

				if (!hadMatch)
				{		
					if (!typeInstance->IsUnspecializedTypeVariation())
					{
						auto bestMethodInst = ifaceMethodInst;
						auto bestInterface = ifaceInst;

						if (matchedMethod == NULL)
						{
							bool searchFailed = false;
							for (auto& checkIFaceTypeInst : typeInstance->mInterfaces)
							{								
								auto checkIFaceInst = checkIFaceTypeInst.mInterfaceType;
								int checkStartIdx = checkIFaceTypeInst.mStartInterfaceTableIdx;
								int checkIMethodCount = (int)checkIFaceInst->mMethodInstanceGroups.size();
								for (int checkIMethodIdx = 0; checkIMethodIdx < checkIMethodCount; checkIMethodIdx++)
								{
									auto checkIFaceMethodInst = checkIFaceInst->mMethodInstanceGroups[checkIMethodIdx].mDefault;
									if ((checkIFaceMethodInst != NULL) && (checkIFaceMethodInst->mMethodDef->mIsOverride))
									{
										if (CompareMethodSignatures(checkIFaceMethodInst, ifaceMethodInst))
										{
											bool isBetter = TypeIsSubTypeOf(checkIFaceInst, bestInterface);
											bool isWorse = TypeIsSubTypeOf(bestInterface, checkIFaceInst);
											if (isBetter == isWorse)
											{
												CompareDeclTypes(checkIFaceMethodInst->mMethodDef->mDeclaringType, bestMethodInst->mMethodDef->mDeclaringType, isBetter, isWorse);
											}
											if ((isBetter) && (!isWorse))
											{
												bestInterface = checkIFaceInst;
												bestMethodInst = checkIFaceMethodInst;
											}
											else if (isBetter == isWorse)
											{
												if (!searchFailed)
												{
													searchFailed = true;
													auto error = Fail(StrFormat("There is no most-specific default implementation of '%s'", MethodToString(ifaceMethodInst).c_str()), declTypeDef->mTypeDeclaration->mNameNode);
													if (error != NULL)
													{
														mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate",
															MethodToString(bestMethodInst).c_str()), bestMethodInst->mMethodDef->GetRefNode());
														mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate",
															MethodToString(checkIFaceMethodInst).c_str()), checkIFaceMethodInst->mMethodDef->GetRefNode());
													}
													//candidate implementations include '%s' and '%s'", 
													//TypeToString(checkIFaceInst).c_str(), TypeToString(bestInterface).c_str()), );
												}
											}
										}
									}
								}
							}

							if (bestMethodInst->mReturnType != ifaceMethodInst->mReturnType)
							{
								auto error = Fail(StrFormat("Default interface method '%s' cannot be used does not have the return type '%s'",
									MethodToString(bestMethodInst).c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), declTypeDef->mTypeDeclaration->mNameNode);
								if (error != NULL)
								{
									mCompiler->mPassInstance->MoreInfo("See original method declaration", ifaceMethodInst->mMethodDef->GetRefNode());
									mCompiler->mPassInstance->MoreInfo("See override method declaration", bestMethodInst->mMethodDef->GetRefNode());
								}
							}
						}

						if ((bestMethodInst->mMethodDef->HasBody()) && (bestMethodInst->mMethodDef->mGenericParams.size() == 0) && (matchedMethod == NULL))
						{
							auto methodDef = bestMethodInst->mMethodDef;
							BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_ForeignMethodDef;
							if ((methodDef->mGenericParams.size() != 0) || (typeInstance->IsUnspecializedType()))
								flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_UnspecializedPass);
							auto methodInst = GetMethodInstance(typeInstance, methodDef, BfTypeVector(), flags, ifaceInst);
							if (methodInst)
							{
								*matchedMethodRef = methodInst.mMethodInstance;

								BfMethodInstance* newMethodInstance = methodInst.mMethodInstance;
								BF_ASSERT(newMethodInstance->mIsForeignMethodDef);					
								if (newMethodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference)
									mOnDemandMethodCount++;
								continue;
							}
						}

						if (typeInstance->IsBoxed())
						{
							if (ifaceMethodInst->mMethodDef->mIsStatic)
							{
								// Skip the statics, those can't be invoked
							}
							else
							{
								// The unboxed version should have had the same error
								if (!typeInstance->GetUnderlyingType()->IsIncomplete())
									AssertErrorState();
							}
						}
						else
						{							
							BfTypeDeclaration* typeDecl = declTypeDef->mTypeDeclaration;
							BfError* error = Fail(StrFormat("'%s' does not implement interface member '%s'", TypeToString(typeInstance).c_str(), MethodToString(ifaceMethodInst).c_str()), typeDecl->mNameNode, true);
							if ((matchedMethod != NULL) && (error != NULL))
							{
								if (hadPubFailure)
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because because it is not public",
										MethodToString(matchedMethod).c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
								}
								else if (ifaceMethodInst->mReturnType->IsConcreteInterfaceType())
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because because it does not have a concrete return type that implements '%s'",
										MethodToString(matchedMethod).c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
								}					
								else if (hadMutFailure)
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because because it is market as 'mut' but interface method does not allow it",
										MethodToString(matchedMethod).c_str()), matchedMethod->mMethodDef->GetMutNode());
									mCompiler->mPassInstance->MoreInfo(StrFormat("Declare the interface method as 'mut' to allow matching 'mut' implementations"), ifaceMethodInst->mMethodDef->mMethodDeclaration);
								}
								else
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because because it does not have the return type '%s'",
										MethodToString(matchedMethod).c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
									if ((ifaceMethodInst->mVirtualTableIdx != -1) && (ifaceMethodInst->mReturnType->IsInterface()))
										mCompiler->mPassInstance->MoreInfo("Declare the interface method as 'concrete' to allow matching concrete return values", ifaceMethodInst->mMethodDef->GetMethodDeclaration()->mVirtualSpecifier);
								}
							}							
						}
					}

					// Clear out the entry
					*matchedMethodRef = BfMethodRef();
				}
			}			
		}
	}

	ambiguityContext.Finish();
	CheckAddFailType();

	typeInstance->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
	mCompiler->mStats.mTypesPopulated++;		
	mCompiler->UpdateCompletion();

	BfLogSysM("Finished DoTypeInstanceMethodProcessing %p.  OnDemandMethods: %d  Virtual Size: %d\n", typeInstance, mOnDemandMethodCount, typeInstance->mVirtualMethodTable.size());
}

void BfModule::RebuildMethods(BfTypeInstance* typeInstance)
{	
	if (typeInstance->IsIncomplete())
		return;
	
	typeInstance->mNeedsMethodProcessing = true;
	typeInstance->mDefineState = BfTypeDefineState_Defined;
	typeInstance->mTypeIncomplete = true;

	for (auto& methodInstanceGroup : typeInstance->mMethodInstanceGroups)
	{
		delete methodInstanceGroup.mDefault;
		methodInstanceGroup.mDefault = NULL;
		delete methodInstanceGroup.mMethodSpecializationMap;
		methodInstanceGroup.mMethodSpecializationMap = NULL;		
		methodInstanceGroup.mOnDemandKind = BfMethodOnDemandKind_NotSet;
	}

	BfTypeProcessRequest* typeProcessRequest = mContext->mPopulateTypeWorkList.Alloc();
	typeProcessRequest->mType = typeInstance;
	BF_ASSERT(typeInstance->mContext == mContext);	
	mCompiler->mStats.mTypesQueued++;
	mCompiler->UpdateCompletion();
}

BfModule* BfModule::GetModuleFor(BfType* type)
{
	auto typeInst = type->ToTypeInstance();
	if (typeInst == NULL)
		return NULL;
	return typeInst->mModule;
}

void BfModule::AddMethodToWorkList(BfMethodInstance* methodInstance)
{
	BF_ASSERT(!methodInstance->mMethodDef->mIsAbstract);

	if (methodInstance->IsSpecializedByAutoCompleteMethod())
		return;

	BF_ASSERT(mCompiler->mCompileState != BfCompiler::CompileState_VData);
	if ((methodInstance->mIsReified) && (!methodInstance->mIsUnspecialized))
	{
		BF_ASSERT(mCompiler->mCompileState != BfCompiler::CompileState_Unreified);
	}

	if (methodInstance->mIsUnspecializedVariation)
	{
		return;
	}

	BF_ASSERT(methodInstance->mMethodProcessRequest == NULL);

	auto defaultMethod = methodInstance->mMethodInstanceGroup->mDefault;
	if (defaultMethod != methodInstance)
	{
		BF_ASSERT(defaultMethod != NULL);
		if (methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference)
		{
			AddMethodToWorkList(defaultMethod);
		}
	}

	if (methodInstance->mDeclModule != NULL)
	{
		if (methodInstance->mDeclModule != this)
		{
			methodInstance->mDeclModule->AddMethodToWorkList(methodInstance);
			return;
		}
	}
	else
	{
		auto module = GetOrCreateMethodModule(methodInstance);
		methodInstance->mDeclModule = module;
		
		BfIRValue func = CreateFunctionFrom(methodInstance, false, methodInstance->mAlwaysInline);
		methodInstance->mIRFunction = func;

		module->mFuncReferences[methodInstance] = func;

		module->AddMethodToWorkList(methodInstance);
		return;
	}

	if ((!methodInstance->mIRFunction) && (methodInstance->mIsReified) && (!methodInstance->mIsUnspecialized) &&
		(methodInstance->GetImportCallKind() == BfImportCallKind_None))
	{
		if (!mIsModuleMutable)
			PrepareForIRWriting(methodInstance->GetOwner());
		
		BfIRValue func = CreateFunctionFrom(methodInstance, false, methodInstance->mAlwaysInline);		
		methodInstance->mIRFunction = func;
		mFuncReferences[methodInstance] = func;		
	}
	
	BF_ASSERT(methodInstance->mDeclModule == this);

	if (defaultMethod == methodInstance)
	{
		if (methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude)
		{
			auto owningModule = methodInstance->GetOwner()->GetModule();

			BF_ASSERT(methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Referenced);
			if (!mIsScratchModule)
			{
				if (owningModule->mParentModule != NULL)
					BF_ASSERT(owningModule->mParentModule->mOnDemandMethodCount > 0);
				else
					BF_ASSERT(owningModule->mOnDemandMethodCount > 0);
			}
			methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_InWorkList;
		}
	}
	else
	{
		BF_ASSERT(defaultMethod->mMethodInstanceGroup->IsImplemented());
	}

	BF_ASSERT(methodInstance->mDeclModule != NULL);

	auto typeInstance = methodInstance->GetOwner();
		
	BfMethodProcessRequest* methodProcessRequest = mContext->mMethodWorkList.Alloc();
	methodProcessRequest->mType = typeInstance;
	methodProcessRequest->mMethodInstance = methodInstance;	
	methodProcessRequest->mRevision = typeInstance->mRevision;
	methodProcessRequest->mFromModuleRebuildIdx = mRebuildIdx;
	methodProcessRequest->mFromModule = this;

	if ((!mCompiler->mIsResolveOnly) && (methodInstance->mIsReified))
		BF_ASSERT(mIsModuleMutable || mReifyQueued);

	BF_ASSERT(mBfIRBuilder != NULL);

	BfLogSysM("Adding to mMethodWorkList Module: %p IncompleteMethodCount: %d Type %p MethodInstance: %p Name:%s TypeRevision: %d ModuleRevision: %d ReqId:%d\n", this, mIncompleteMethodCount, typeInstance, methodInstance, methodInstance->mMethodDef->mName.c_str(), methodProcessRequest->mRevision, methodProcessRequest->mFromModuleRevision, methodProcessRequest->mReqId);

	if (mAwaitingFinish)
	{
		BfLogSysM("Module: %p No longer awaiting finish\n", this);
		mAwaitingFinish = false;
	}

	mCompiler->mStats.mMethodsQueued++;
	mCompiler->UpdateCompletion();
	mIncompleteMethodCount++;
	if (methodInstance->GetNumGenericArguments() != 0)
		mHasGenericMethods = true;
	
	methodInstance->mMethodProcessRequest = methodProcessRequest;
}

BfArrayType* BfModule::CreateArrayType(BfType* resolvedType, int dimensions)
{
	BF_ASSERT(!resolvedType->IsVar());

	auto arrayType = mContext->mArrayTypePool.Get();
	arrayType->mContext = mContext;
	arrayType->mTypeDef = mCompiler->GetArrayTypeDef(dimensions);
	arrayType->mDimensions = dimensions;
	arrayType->mTypeGenericArguments.clear();
	arrayType->mTypeGenericArguments.push_back(resolvedType);
	auto resolvedArrayType = ResolveType(arrayType);
	if (resolvedArrayType != arrayType)
		mContext->mArrayTypePool.GiveBack(arrayType);
	return (BfArrayType*)resolvedArrayType;
}

BfSizedArrayType* BfModule::CreateSizedArrayType(BfType * resolvedType, int size)
{
	BF_ASSERT(!resolvedType->IsVar());

	auto arrayType = mContext->mSizedArrayTypePool.Get();
	arrayType->mContext = mContext;
	arrayType->mElementType = resolvedType;
	arrayType->mElementCount = size;
	auto resolvedArrayType = ResolveType(arrayType);
	if (resolvedArrayType != arrayType)
		mContext->mSizedArrayTypePool.GiveBack(arrayType);
	return (BfSizedArrayType*)resolvedArrayType;
}

BfUnknownSizedArrayType* BfModule::CreateUnknownSizedArrayType(BfType* resolvedType, BfType* sizeParam)
{
	BF_ASSERT(!resolvedType->IsVar());
	BF_ASSERT(sizeParam->IsGenericParam());

	auto arrayType = mContext->mUnknownSizedArrayTypePool.Get();
	arrayType->mContext = mContext;
	arrayType->mElementType = resolvedType;
	arrayType->mElementCount = -1;
	arrayType->mElementCountSource = sizeParam;
	auto resolvedArrayType = ResolveType(arrayType);
	if (resolvedArrayType != arrayType)
		mContext->mUnknownSizedArrayTypePool.GiveBack(arrayType);
	return (BfUnknownSizedArrayType*)resolvedArrayType;
}

BfPointerType* BfModule::CreatePointerType(BfType* resolvedType)
{
	auto pointerType = mContext->mPointerTypePool.Get();
	pointerType->mContext = mContext;
	pointerType->mElementType = resolvedType;
	auto resolvedPointerType = (BfPointerType*)ResolveType(pointerType);
	if (resolvedPointerType != pointerType)
		mContext->mPointerTypePool.GiveBack(pointerType);

	BF_ASSERT(resolvedPointerType->mElementType == resolvedType);

	return resolvedPointerType;
}

BfConstExprValueType* BfModule::CreateConstExprValueType(const BfTypedValue& typedValue)
{
	auto variant = TypedValueToVariant(NULL, typedValue);
	if (variant.mTypeCode == BfTypeCode_None)
		return NULL;

	auto constExprValueType = mContext->mConstExprValueTypePool.Get();
	constExprValueType->mContext = mContext;
	constExprValueType->mType = typedValue.mType;
	constExprValueType->mValue = variant;	

	auto resolvedConstExprValueType = (BfConstExprValueType*)ResolveType(constExprValueType);
	if (resolvedConstExprValueType != constExprValueType)
		mContext->mConstExprValueTypePool.GiveBack(constExprValueType);

	BF_ASSERT(resolvedConstExprValueType->mValue.mInt64 == constExprValueType->mValue.mInt64);

	return resolvedConstExprValueType;
}

BfTypeInstance* BfModule::GetWrappedStructType(BfType* type, bool allowSpecialized)
{
	if (type->IsPointer())
	{
		if (allowSpecialized)
		{
			BfPointerType* pointerType = (BfPointerType*)type;
			BfTypeVector typeVector;
			typeVector.Add(pointerType->mElementType);
			return ResolveTypeDef(mCompiler->mPointerTTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
		}
		else
			return ResolveTypeDef(mCompiler->mPointerTTypeDef, BfPopulateType_Data)->ToTypeInstance();
	}
	else if (type->IsMethodRef())
	{
		if (allowSpecialized)
		{
			BfMethodRefType* methodRefType = (BfMethodRefType*)type;
			BfTypeVector typeVector;
			typeVector.Add(methodRefType);
			return ResolveTypeDef(mCompiler->mMethodRefTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
		}
		else
			return ResolveTypeDef(mCompiler->mMethodRefTypeDef, BfPopulateType_Data)->ToTypeInstance();
	}
	else if (type->IsSizedArray())
	{
		if (allowSpecialized)
		{
			if (type->IsUnknownSizedArray())
			{
				BfUnknownSizedArrayType* sizedArrayType = (BfUnknownSizedArrayType*)type;
				BfTypeVector typeVector;
				typeVector.Add(sizedArrayType->mElementType);				
				typeVector.Add(sizedArrayType->mElementCountSource);
				return ResolveTypeDef(mCompiler->mSizedArrayTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
			}

			BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)type;
			BfTypeVector typeVector;
			typeVector.Add(sizedArrayType->mElementType);
			auto sizeValue = BfTypedValue(GetConstValue(BF_MAX(sizedArrayType->mElementCount, 0)), GetPrimitiveType(BfTypeCode_IntPtr));
			typeVector.Add(CreateConstExprValueType(sizeValue));
			return ResolveTypeDef(mCompiler->mSizedArrayTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
		}
		else
			return ResolveTypeDef(mCompiler->mSizedArrayTypeDef, BfPopulateType_Data)->ToTypeInstance();
	}

	BF_ASSERT(type->IsPrimitiveType());
	return GetPrimitiveStructType(((BfPrimitiveType*)type)->mTypeDef->mTypeCode);
}

BfPrimitiveType* BfModule::GetPrimitiveType(BfTypeCode typeCode)
{	
	BfPrimitiveType* primType = mContext->mPrimitiveTypes[typeCode];
	if (primType == NULL)
	{
		switch (typeCode)
		{
		case BfTypeCode_NullPtr:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeNullPtr);
			break;
		case BfTypeCode_Self:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeSelf);
			break;
		case BfTypeCode_Dot:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeDot);
			break;
		case BfTypeCode_Var:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeVar);
			break;
		case BfTypeCode_Let:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeLet);
			break;
		case BfTypeCode_None:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeVoid);
			break;
		case BfTypeCode_Boolean:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeBool);
			break;
		case BfTypeCode_Int8:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeInt8);
			break;
		case BfTypeCode_UInt8:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUInt8);
			break;
		case BfTypeCode_Int16:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeInt16);
			break;
		case BfTypeCode_UInt16:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUInt16);
			break;
		case BfTypeCode_Int32:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeInt32);
			break;
		case BfTypeCode_UInt32:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUInt32);
			break;
		case BfTypeCode_Int64:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeInt64);
			break;
		case BfTypeCode_UInt64:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUInt64);
			break;
		case BfTypeCode_Char8:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeChar8);
			break;
		case BfTypeCode_Char16:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeChar16);
			break;
		case BfTypeCode_Char32:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeChar32);
			break;
		case BfTypeCode_Single:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeSingle);
			break;
		case BfTypeCode_Double:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeDouble);
			break;
		case BfTypeCode_IntPtr:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeIntPtr);
			break;
		case BfTypeCode_UIntPtr:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUIntPtr);
			break;
		case BfTypeCode_IntUnknown:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeIntUnknown);
			break;
		case BfTypeCode_UIntUnknown:
			primType = (BfPrimitiveType*)ResolveTypeDef(mSystem->mTypeUIntUnknown);
			break;
		case BfTypeCode_StringId:
			BF_FATAL("Invalid use of StringId");
			break;
		default: break;
		}
		mContext->mPrimitiveTypes[typeCode] = primType;
	}	
	return primType;
}

BfMethodRefType* BfModule::CreateMethodRefType(BfMethodInstance* methodInstance, bool mustAlreadyExist)
{
	auto methodRefType = new BfMethodRefType();
	methodRefType->mContext = mContext;
	//methodRefType->mCaptureType = NULL;
	methodRefType->mMethodRef = methodInstance;	
	methodRefType->mOwner = methodInstance->GetOwner();
	methodRefType->mOwnerRevision = methodRefType->mOwner->mRevision;	
	//methodRefType->mMangledName = BfMangler::Mangle(mCompiler->GetMangleKind(), methodInstance);
	methodRefType->mIsAutoCompleteMethod = methodInstance->mIsAutocompleteMethod;
	methodRefType->mIsUnspecialized = methodInstance->mIsUnspecialized;
	methodRefType->mIsUnspecializedVariation = methodInstance->mIsUnspecializedVariation;
	methodRefType->mSize = 0;

	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mModule = this;
	BfResolvedTypeSet::Entry* typeEntry = NULL;
	auto inserted = mContext->mResolvedTypes.Insert(methodRefType, &lookupCtx, &typeEntry);
	if (typeEntry->mValue == NULL)
	{	
		BF_ASSERT(!mustAlreadyExist);
		BF_ASSERT(!methodInstance->mHasMethodRefType);

		InitType(methodRefType, BfPopulateType_Identity);
		methodRefType->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;

		methodInstance->mHasMethodRefType = true;
		methodInstance->mMethodInstanceGroup->mRefCount++;
		typeEntry->mValue = methodRefType;
		BfLogSysM("Create MethodRefType %p MethodInstance: %p\n", methodRefType, methodInstance);
		methodRefType->mRevision = 0;
		AddDependency(methodInstance->GetOwner(), methodRefType, BfDependencyMap::DependencyFlag_Calls);
				
		BfTypeVector tupleTypes;
		Array<String> tupleNames;

		int offset = 0;

		methodRefType->mAlign = 1;

		int dataIdx = 0;
		
		// CRepr, just because we're lazy (for now)		
		int implicitParamCount = methodInstance->GetImplicitParamCount();
		for (int implicitParamIdx = methodInstance->HasThis() ? -1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
		{			
			auto paramType = methodInstance->GetParamType(implicitParamIdx);
			if (!paramType->IsValuelessType())
			{
				methodRefType->mDataToParamIdx.Add(implicitParamIdx);
				if (implicitParamIdx >= 0)
					methodRefType->mParamToDataIdx.Add(dataIdx);

				offset = BF_ALIGN(offset, paramType->mAlign);
				offset += paramType->mSize;
				methodRefType->mAlign = std::max(methodRefType->mAlign, paramType->mAlign);

				dataIdx++;
			}	
			else
			{
				methodRefType->mParamToDataIdx.Add(-1);
			}
		}
		offset = BF_ALIGN(offset, methodRefType->mAlign);
		methodRefType->mSize = offset;

//		if (!tupleTypes.empty())
// 		{
// 			methodRefType->mCaptureType = CreateTupleType(tupleTypes, tupleNames);
// 			AddDependency(methodRefType->mCaptureType, methodRefType, BfDependencyMap::DependencyFlag_ReadFields);
// 
// 			methodRefType->mSize = methodRefType->mCaptureType->mSize;
// 			methodRefType->mAlign = methodRefType->mCaptureType->mAlign;
// 		}		
// 		else
// 		{
// 			methodRefType->mSize = 0;
// 			methodRefType->mAlign = 0;
// 		}
	}
	else
	{
		methodRefType->mMethodRef = NULL;
		delete methodRefType;
		methodRefType = (BfMethodRefType*)typeEntry->mValue;
	}
	return methodRefType;
}

BfType* BfModule::FixIntUnknown(BfType* type)
{
	if ((type != NULL) && (type->IsPrimitiveType()))
	{
		auto primType = (BfPrimitiveType*)type;
		if (primType->mTypeDef->mTypeCode == BfTypeCode_IntUnknown)
			return GetPrimitiveType(BfTypeCode_IntPtr);
		if (primType->mTypeDef->mTypeCode == BfTypeCode_UIntUnknown)
			return GetPrimitiveType(BfTypeCode_UIntPtr);
	}
	return type;
}

void BfModule::FixIntUnknown(BfTypedValue& typedVal)
{
	if (!typedVal.mValue.IsConst())
	{
		if ((typedVal.mType != NULL) && (typedVal.mType->IsPrimitiveType()))
		{
			auto primType = (BfPrimitiveType*)typedVal.mType;
			BF_ASSERT((primType->mTypeDef->mTypeCode != BfTypeCode_IntUnknown) && (primType->mTypeDef->mTypeCode != BfTypeCode_UIntUnknown));
		}

		return;
	}

	if (!typedVal.mType->IsPrimitiveType())
		return;

	BfTypeCode wantTypeCode;
	auto primType = (BfPrimitiveType*)typedVal.mType;
	if (primType->mTypeDef->mTypeCode == BfTypeCode_IntUnknown)
		wantTypeCode = BfTypeCode_IntPtr;
	else if (primType->mTypeDef->mTypeCode == BfTypeCode_UIntUnknown)
		wantTypeCode = BfTypeCode_UIntPtr;
	else
		return;

	auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
	if (mSystem->mPtrSize == 4)
	{
		if (primType->mTypeDef->mTypeCode == BfTypeCode_IntUnknown)
		{
			if ((constant->mInt64 >= -0x80000000LL) && (constant->mInt64 <= 0x7FFFFFFFLL))
			{
				typedVal.mValue = mBfIRBuilder->CreateNumericCast(typedVal.mValue, true, BfTypeCode_IntPtr);
				typedVal.mType = GetPrimitiveType(BfTypeCode_IntPtr);
			}
			else
				typedVal.mType = GetPrimitiveType(BfTypeCode_Int64);
			return;
		}
		else
		{
			if ((constant->mInt64 >= 0) && (constant->mInt64 <= 0xFFFFFFFF))
			{
				typedVal.mValue = mBfIRBuilder->CreateNumericCast(typedVal.mValue, false, BfTypeCode_IntPtr);
				typedVal.mType = GetPrimitiveType(BfTypeCode_UIntPtr);
			}
			else
				typedVal.mType = GetPrimitiveType(BfTypeCode_UInt64);
			return;
		}
	}

	typedVal.mType = GetPrimitiveType(wantTypeCode);
}

void BfModule::FixIntUnknown(BfTypedValue& lhs, BfTypedValue& rhs)
{	
	if ((lhs.mType != NULL) && (lhs.mType->IsIntUnknown()) && (rhs.mType != NULL) && (rhs.mType->IsInteger()))
	{
		if (CanCast(lhs, rhs.mType))
		{
			lhs = Cast(NULL, lhs, rhs.mType, BfCastFlags_SilentFail);
			if (!lhs)
				lhs = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
			return;
		}
	}

	if ((rhs.mType != NULL) && (rhs.mType->IsIntUnknown()) && (lhs.mType != NULL) && (lhs.mType->IsInteger()))
	{
		if (CanCast(rhs, lhs.mType))
		{
			rhs = Cast(NULL, rhs, lhs.mType, BfCastFlags_SilentFail);
			if (!rhs)
				rhs = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
			return;
		}
	}

	FixIntUnknown(lhs);
	FixIntUnknown(rhs);
}

BfTypeInstance* BfModule::GetPrimitiveStructType(BfTypeCode typeCode)
{
	BfTypeInstance* typeInst = NULL;
	switch (typeCode)
	{
	case BfTypeCode_None:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Void"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Boolean:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Boolean"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Int8:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Int8"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_UInt8:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.UInt8"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Int16:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Int16"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_UInt16:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.UInt16"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Int32:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Int32"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_UInt32:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.UInt32"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Int64:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Int64"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_UInt64:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.UInt64"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_IntPtr:
	case BfTypeCode_IntUnknown:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Int"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_UIntPtr:
	case BfTypeCode_UIntUnknown:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.UInt"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Char8:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Char8"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Char16:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Char16"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Char32:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Char32"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Single:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Float"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Double:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Double"), BfPopulateType_Identity)->ToTypeInstance(); break;
	default:
		//BF_FATAL("not implemented");
		break;
	}
	return typeInst;
}

BfBoxedType* BfModule::CreateBoxedType(BfType* resolvedTypeRef)
{	
	bool isStructPtr = false;

	if (resolvedTypeRef->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)resolvedTypeRef;
		resolvedTypeRef = GetPrimitiveStructType(primType->mTypeDef->mTypeCode);
		if (resolvedTypeRef == NULL)
		{
			BF_FATAL("Unable to find primitive type");
			return NULL;
		}
	}
	else if (resolvedTypeRef->IsPointer())
	{
	 	BfPointerType* pointerType = (BfPointerType*)resolvedTypeRef;
		if (pointerType->mElementType->IsStruct())
		{
			resolvedTypeRef = pointerType->mElementType;
			isStructPtr = true;
		}
		else
		{
			BfTypeVector typeVector;
			typeVector.Add(pointerType->mElementType);
			resolvedTypeRef = ResolveTypeDef(mCompiler->mPointerTTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
		}
	}
	else if (resolvedTypeRef->IsMethodRef())
	{
		BfMethodRefType* methodRefType = (BfMethodRefType*)resolvedTypeRef;
		BfTypeVector typeVector;
		typeVector.Add(methodRefType);
		resolvedTypeRef = ResolveTypeDef(mCompiler->mMethodRefTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
	}
	else if (resolvedTypeRef->IsSizedArray())
	{
	 	BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)resolvedTypeRef;
	 	BfTypeVector typeVector;
	 	typeVector.Add(sizedArrayType->mElementType);
	 	auto sizeValue = BfTypedValue(GetConstValue(sizedArrayType->mElementCount), GetPrimitiveType(BfTypeCode_IntPtr));
	 	typeVector.Add(CreateConstExprValueType(sizeValue));
		resolvedTypeRef = ResolveTypeDef(mCompiler->mSizedArrayTypeDef, typeVector, BfPopulateType_Data)->ToTypeInstance();
	}	

	BfTypeInstance* typeInst = resolvedTypeRef->ToTypeInstance();
	if ((typeInst == NULL) && (!resolvedTypeRef->IsGenericParam()))
		return NULL;

	auto boxedType = mContext->mBoxedTypePool.Get();
	boxedType->mContext = mContext;
	boxedType->mElementType = resolvedTypeRef;
	if (typeInst != NULL)
		boxedType->mTypeDef = typeInst->mTypeDef;
	else
		boxedType->mTypeDef = mCompiler->mValueTypeTypeDef;
	boxedType->mBoxedFlags = isStructPtr ? BfBoxedType::BoxedFlags_StructPtr : BfBoxedType::BoxedFlags_None;
	auto resolvedBoxedType = ResolveType(boxedType);
	if (resolvedBoxedType != boxedType)
		mContext->mBoxedTypePool.GiveBack(boxedType);
	return (BfBoxedType*)resolvedBoxedType;
}

BfTupleType* BfModule::CreateTupleType(const BfTypeVector& fieldTypes, const Array<String>& fieldNames)
{
	auto tupleType = mContext->mTupleTypePool.Get();
	tupleType->mContext = mContext;
	tupleType->mFieldInstances.Resize(fieldTypes.size());

	auto baseType = (BfTypeInstance*)ResolveTypeDef(mContext->mCompiler->mValueTypeTypeDef);
	tupleType->Init(baseType->mTypeDef->mProject, baseType);

	for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
	{
		BfFieldInstance* fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
		fieldInstance->mFieldIdx = fieldIdx;
		fieldInstance->SetResolvedType(fieldTypes[fieldIdx]);
		fieldInstance->mOwner = tupleType;

		String fieldName;
		if (fieldIdx < (int)fieldNames.size())
			fieldName = fieldNames[fieldIdx];
		if (fieldName.empty())
			fieldName = StrFormat("%d", fieldIdx);
		BfFieldDef* fieldDef = tupleType->AddField(fieldName);
	}

	auto resolvedTupleType = ResolveType(tupleType);
	if (resolvedTupleType != tupleType)
		mContext->mTupleTypePool.GiveBack(tupleType);
	return (BfTupleType*)resolvedTupleType;
}

BfTupleType * BfModule::SantizeTupleType(BfTupleType* tupleType)
{
	bool needsSanitize = false;
	for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
	{
		BfFieldInstance* fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
		if ((fieldInstance->mResolvedType->IsVar()) || (fieldInstance->mResolvedType->IsLet()))
		{
			needsSanitize = true;			
			break;
		}
	}

	if (!needsSanitize)
		return tupleType;

	BfTypeVector fieldTypes;
	Array<String> fieldNames;

	for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
	{
		BfFieldInstance* fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
		auto fieldDef = fieldInstance->GetFieldDef();
		if ((fieldInstance->mResolvedType->IsVar()) || (fieldInstance->mResolvedType->IsLet()))
			fieldTypes.Add(mContext->mBfObjectType);
		else
			fieldTypes.Add(fieldInstance->mResolvedType);		
		if (!fieldDef->IsUnnamedTupleField())
		{
			for (int i = 0; i < fieldIdx; i++)
				fieldNames.Add(String());
			fieldNames.Add(fieldDef->mName);
		}
	}
	return CreateTupleType(fieldTypes, fieldNames);
}

BfRefType* BfModule::CreateRefType(BfType* resolvedTypeRef, BfRefType::RefKind refKind)
{
	auto refType = mContext->mRefTypePool.Get();
	refType->mContext = mContext;
	refType->mElementType = resolvedTypeRef;
	refType->mRefKind = refKind;
	auto resolvedRefType = ResolveType(refType);
	if (resolvedRefType != refType)
		mContext->mRefTypePool.GiveBack(refType);
	return (BfRefType*)resolvedRefType;
}

BfRetTypeType* BfModule::CreateRetTypeType(BfType* resolvedTypeRef)
{
	auto retTypeType = mContext->mRetTypeTypePool.Get();
	retTypeType->mContext = mContext;
	retTypeType->mElementType = resolvedTypeRef;
	auto resolvedRetTypeType = ResolveType(retTypeType);
	if (resolvedRetTypeType != retTypeType)
		mContext->mRetTypeTypePool.GiveBack(retTypeType);
	return (BfRetTypeType*)resolvedRetTypeType;
}

BfConcreteInterfaceType* BfModule::CreateConcreteInterfaceType(BfTypeInstance* interfaceType)
{
	auto concreteInterfaceType = mContext->mConcreteInterfaceTypePool.Get();
	concreteInterfaceType->mContext = mContext;
	concreteInterfaceType->mInterface = interfaceType;
	auto resolvedConcreteInterfaceType = ResolveType(concreteInterfaceType);
	if (resolvedConcreteInterfaceType != concreteInterfaceType)
		mContext->mConcreteInterfaceTypePool.GiveBack(concreteInterfaceType);
	return (BfConcreteInterfaceType*)resolvedConcreteInterfaceType;
}

BfPointerType* BfModule::CreatePointerType(BfTypeReference* typeRef)
{
	auto resolvedTypeRef = ResolveTypeRef(typeRef);
	if (resolvedTypeRef == NULL)
		return NULL;
	return CreatePointerType(resolvedTypeRef);
}

BfType* BfModule::ResolveTypeDef(BfTypeDef* typeDef, BfPopulateType populateType)
{
	//BF_ASSERT(typeDef->mTypeCode != BfTypeCode_Extension);
	BF_ASSERT(!typeDef->mIsPartial || typeDef->mIsCombinedPartial);

	BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Deleted);
	BF_ASSERT((typeDef->mOuterType == NULL) || (typeDef->mOuterType->mDefState != BfTypeDef::DefState_Deleted));

	if (typeDef->mGenericParamDefs.size() != 0)
		return ResolveTypeDef(typeDef, BfTypeVector(), populateType);

	auto typeDefTypeRef = mContext->mTypeDefTypeRefPool.Get();	
	typeDefTypeRef->mTypeDef = typeDef;
	auto resolvedtypeDefType = ResolveTypeRef(typeDefTypeRef, populateType);
	if (resolvedtypeDefType == NULL)
	{
		mContext->mTypeDefTypeRefPool.GiveBack(typeDefTypeRef);
		return NULL;
	}

	mContext->mTypeDefTypeRefPool.GiveBack(typeDefTypeRef);
	//BF_ASSERT(resolvedtypeDefType->IsTypeInstance() || resolvedtypeDefType->IsPrimitiveType());
	return resolvedtypeDefType;
}

// Get BaseClass even when we haven't populated the type yet2
BfTypeInstance* BfModule::GetBaseType(BfTypeInstance* typeInst)
{
	if ((mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mTypeInstance == typeInst))
	{
		if (typeInst->mBaseType == NULL)
			return NULL;
	}

	if ((typeInst->mBaseType == NULL) && (typeInst != mContext->mBfObjectType))	
		PopulateType(typeInst, BfPopulateType_BaseType);
	return typeInst->mBaseType;
}

void BfModule::HandleTypeGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, int typeGenericParamIdx)
{
	if (mCompiler->IsAutocomplete())
	{
		BfAutoComplete* autoComplete = mCompiler->mResolvePassData->mAutoComplete;
		if ((autoComplete != NULL) && (autoComplete->mIsGetDefinition) && (autoComplete->IsAutocompleteNode(refNode)))
		{
			if ((autoComplete->mDefMethod == NULL) && (autoComplete->mDefField == NULL) &&
				(autoComplete->mDefProp == NULL))
			{
				autoComplete->mDefType = typeDef;				
				autoComplete->mDefTypeGenericParamIdx = typeGenericParamIdx;
				autoComplete->SetDefinitionLocation(refNode);
			}
		}
	}
	if (mCompiler->mResolvePassData != NULL)
		mCompiler->mResolvePassData->HandleTypeGenericParam(refNode, typeDef, typeGenericParamIdx);
}

void BfModule::HandleMethodGenericParamRef(BfAstNode* refNode, BfTypeDef* typeDef, BfMethodDef* methodDef, int methodGenericParamIdx)
{
	if (mCompiler->IsAutocomplete())
	{
		BfAutoComplete* autoComplete = mCompiler->mResolvePassData->mAutoComplete;
		if ((autoComplete != NULL) && (autoComplete->mIsGetDefinition) && (autoComplete->IsAutocompleteNode(refNode)))
		{
			if ((autoComplete->mDefMethod == NULL) && (autoComplete->mDefField == NULL) &&
				(autoComplete->mDefProp == NULL))
			{
				autoComplete->mDefType = typeDef;
				autoComplete->mDefMethod = methodDef;
				autoComplete->mDefMethodGenericParamIdx = methodGenericParamIdx;
				autoComplete->SetDefinitionLocation(refNode);
			}
		}
	}
	if (mCompiler->mResolvePassData != NULL)
		mCompiler->mResolvePassData->HandleMethodGenericParam(refNode, typeDef, methodDef, methodGenericParamIdx);
}

BfType* BfModule::ResolveInnerType(BfType* outerType, BfTypeReference* typeRef, BfPopulateType populateType, bool ignoreErrors)
{
	BfTypeDef* nestedTypeDef = NULL;

	if (outerType->IsBoxed())
		outerType = outerType->GetUnderlyingType();

	BfNamedTypeReference* namedTypeRef = NULL;
	BfGenericInstanceTypeRef* genericTypeRef = NULL;
	BfDirectStrTypeReference* directStrTypeRef = NULL;
	if ((namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef)))
	{
		//TYPEDEF nestedTypeDef = namedTypeRef->mTypeDef;
	}
	else if ((genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef)))
	{
		namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(genericTypeRef->mElementType);
		//TYPEDEF nestedTypeDef = namedTypeRef->mTypeDef;
	}
	else if ((directStrTypeRef = BfNodeDynCast<BfDirectStrTypeReference>(typeRef)))
	{
		//
	}

	BF_ASSERT((namedTypeRef != NULL) || (directStrTypeRef != NULL));

	if (nestedTypeDef == NULL)
	{
		StringView findName;
		if (namedTypeRef != NULL)
			findName = namedTypeRef->mNameNode->ToStringView();
		else
			findName = directStrTypeRef->mTypeName;

		if (!findName.Contains('.'))
		{			
			if (outerType->IsTypeInstance())
			{
				auto outerTypeInstance = outerType->ToTypeInstance();

				for (int pass = 0; pass < 2; pass++)
				{
					bool isFailurePass = pass == 1;
					bool allowPrivate = (mCurTypeInstance != NULL) &&
						((mCurTypeInstance == outerTypeInstance) || TypeHasParent(mCurTypeInstance->mTypeDef, outerTypeInstance->mTypeDef));
					bool allowProtected = allowPrivate;/*(mCurTypeInstance != NULL) &&
													   (allowPrivate || (mCurTypeInstance->mSkipTypeProtectionChecks) || TypeIsSubTypeOf(mCurTypeInstance, outerTypeInstance));*/

					auto checkOuterType = outerTypeInstance;
					while (checkOuterType != NULL)
					{
						for (auto checkType : checkOuterType->mTypeDef->mNestedTypes)
						{							
							auto latestCheckType = checkType->GetLatest();

							if ((!isFailurePass) && (!CheckProtection(latestCheckType->mProtection, allowProtected, allowPrivate)))
								continue;

							if (checkType->mName->mString == findName)
							{
								if (isFailurePass)
								{
									// This is the one error we don't ignore when ignoreErrors is set
									Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(checkOuterType).c_str(), BfTypeUtils::TypeToString(typeRef).c_str()), typeRef); // CS0122
								}

								nestedTypeDef = checkType;
								break;
							}
						}
						if (nestedTypeDef != NULL)
							break;
						allowPrivate = false;
						checkOuterType = GetBaseType(checkOuterType);
					}
					if (nestedTypeDef != NULL)
						break;
				}
			}
		}

		if (nestedTypeDef == NULL)
		{
			if (!mIgnoreErrors && !ignoreErrors)
			{
				StringT<64> name;
				name.Append(findName);
				Fail(StrFormat("'%s' does not contain a definition for '%s'", TypeToString(outerType).c_str(), name.c_str()), typeRef);
			}
			return NULL;
		}
	}

	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, ignoreErrors || mIgnoreErrors);
	if ((genericTypeRef != NULL) || (outerType->IsGenericTypeInstance()))
	{
		BfTypeVector genericArgs;
		if (outerType->IsGenericTypeInstance())
		{
			auto genericTypeInst = (BfGenericTypeInstance*)outerType;
			genericArgs = genericTypeInst->mTypeGenericArguments;
		}

		if (genericTypeRef != NULL)
		{
			for (auto genericArgTypeRef : genericTypeRef->mGenericArguments)
			{
				auto genericArgType = ResolveTypeRef(genericArgTypeRef, BfPopulateType_IdentityNoRemapAlias);
				if (genericArgType == NULL)
					return NULL;
				genericArgs.push_back(genericArgType);
			}
		}

		if (genericArgs.size() != nestedTypeDef->mGenericParamDefs.size())
		{			
			if (populateType == BfPopulateType_TypeDef)
			{
				// Probably from inside ResolveGenericInstanceDef, just return unresolved typedef
				genericArgs.clear();
			}
			else
			{
				ShowGenericArgCountError(typeRef, (int)nestedTypeDef->mGenericParamDefs.size() - (int)nestedTypeDef->mOuterType->mGenericParamDefs.size());
				return NULL;
			}
		}

		if (nestedTypeDef->mIsPartial)
		{
			nestedTypeDef = GetCombinedPartialTypeDef(nestedTypeDef);
			if (nestedTypeDef == NULL)
				return NULL;
		}
		return ResolveTypeDef(nestedTypeDef, genericArgs, BfPopulateType_IdentityNoRemapAlias);
	}
	else
	{
		if (nestedTypeDef->mIsPartial)
		{
			nestedTypeDef = GetCombinedPartialTypeDef(nestedTypeDef);
			if (nestedTypeDef == NULL)
				return NULL;
		}
		return ResolveTypeDef(nestedTypeDef, BfPopulateType_IdentityNoRemapAlias);
	}

	return NULL;
}

BfTypeDef* BfModule::GetCombinedPartialTypeDef(BfTypeDef* typeDef)
{
	BF_ASSERT(!typeDef->mIsExplicitPartial);
	if (!typeDef->mIsPartial)
		return typeDef;
	auto result = mSystem->FindTypeDef(typeDef->mFullName.ToString(), (int)typeDef->mGenericParamDefs.size());
	return result;
}

BfTypeInstance* BfModule::GetOuterType(BfType* type)
{
	if (type == NULL)
		return NULL;
	if (type->IsBoxed())
		return GetOuterType(((BfBoxedType*)type)->mElementType);
	auto typeInst = type->ToTypeInstance();
	if ((typeInst == NULL) || (typeInst->mTypeDef->mOuterType == NULL))
		return NULL;
	auto outerTypeDef = typeInst->mTypeDef->mOuterType;
	if (outerTypeDef->mIsPartial)
	{
		outerTypeDef = GetCombinedPartialTypeDef(outerTypeDef);
		if (outerTypeDef == NULL)
			return NULL;
	}

	BfTypeVector typeGenericArguments;
	if (type->IsGenericTypeInstance())
	{
		auto genericType = (BfGenericTypeInstance*)type;
		typeGenericArguments = genericType->mTypeGenericArguments;
	}
	BF_ASSERT((intptr)typeGenericArguments.size() >= (intptr)outerTypeDef->mGenericParamDefs.size());
	typeGenericArguments.resize(outerTypeDef->mGenericParamDefs.size());

	//auto outerType = ResolveTypeDef(outerTypeDef, typeGenericArguments, BfPopulateType_Declaration);
	auto outerType = ResolveTypeDef(outerTypeDef, typeGenericArguments, BfPopulateType_Identity);
	if (outerType == NULL)
		return NULL;
	return outerType->ToTypeInstance();
}

bool BfModule::IsInnerType(BfType* checkInnerType, BfType* checkOuterType)
{
	BfType* outerType = GetOuterType(checkInnerType);
	if (outerType == NULL)
		return false;
	if (outerType == checkOuterType)
		return true;
	return IsInnerType(outerType, checkOuterType);
}

bool BfModule::IsInnerType(BfTypeDef* checkInnerType, BfTypeDef* checkOuterType)
{
	BF_ASSERT(!checkOuterType->mIsPartial);
	if (checkInnerType->mNestDepth <= checkOuterType->mNestDepth)
		return false;
	while (true)
	{		
		BfTypeDef* outerType = checkInnerType->mOuterType;
		if (outerType == NULL)
			return false;
		if (outerType->mIsPartial)
			outerType = mSystem->GetCombinedPartial(outerType);
		if (outerType == checkOuterType)
			return true;
		checkInnerType = checkInnerType->mOuterType;
	}	
}

BfType* BfModule::ResolveTypeDef(BfTypeDef* typeDef, const BfTypeVector& genericArgs, BfPopulateType populateType)
{
	if (typeDef->mGenericParamDefs.size() == 0)
		return ResolveTypeDef(typeDef, populateType);

	if ((typeDef == mCompiler->mArray1TypeDef) || (typeDef == mCompiler->mArray2TypeDef))
	{
		auto arrayInstType = mContext->mArrayTypeInstancePool.Get();
		arrayInstType->mContext = mContext;
		if (typeDef == mCompiler->mArray1TypeDef)
			arrayInstType->mDimensions = 1;
		else
			arrayInstType->mDimensions = 2;
		auto typeRef = mContext->mTypeDefTypeRefPool.Get();
		typeRef->mTypeDef = typeDef;
		arrayInstType->mTypeDef = typeDef;
		arrayInstType->mIsUnspecialized = false;
		arrayInstType->mTypeGenericArguments.clear();
		for (auto genericArg : genericArgs)
		{
			arrayInstType->mIsUnspecialized |= genericArg->IsGenericParam();
			arrayInstType->mTypeGenericArguments.push_back(genericArg);
		}

		if (genericArgs.size() == 0)
		{
			for (int i = 0; i < (int)typeDef->mGenericParamDefs.size(); i++)
			{
				auto genericParamTypeRef = GetGenericParamType(BfGenericParamKind_Type, i);
				arrayInstType->mTypeGenericArguments.push_back(genericParamTypeRef);
				arrayInstType->mIsUnspecialized = true;
			}
		}

		auto resolvedType = ResolveType(arrayInstType, populateType);
		if (resolvedType != arrayInstType)
		{
			mContext->mArrayTypeInstancePool.GiveBack(arrayInstType);
			mContext->mTypeDefTypeRefPool.GiveBack(typeRef);
		}
		BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
		return resolvedType;
	}	

	BfGenericTypeInstance* genericInstType;
	if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
		genericInstType = mContext->mGenericTypeAliasPool.Get();
	else
		genericInstType = mContext->mGenericTypeInstancePool.Get();
	genericInstType->mContext = mContext;
	auto typeRef = mContext->mTypeDefTypeRefPool.Get();
	typeRef->mTypeDef = typeDef;
	genericInstType->mTypeDef = typeDef;
	genericInstType->mIsUnspecialized = false;
	genericInstType->mTypeGenericArguments.clear();
	for (auto genericArg : genericArgs)
	{
		genericInstType->mIsUnspecialized |= genericArg->IsGenericParam();
		genericInstType->mTypeGenericArguments.push_back(genericArg);
	}

	if (genericArgs.size() == 0)
	{
		for (int i = 0; i < (int)typeDef->mGenericParamDefs.size(); i++)
		{
			auto genericParamTypeRef = GetGenericParamType(BfGenericParamKind_Type, i);
			genericInstType->mTypeGenericArguments.push_back(genericParamTypeRef);
			genericInstType->mIsUnspecialized = true;
		}
	}

	auto resolvedType = ResolveType(genericInstType, populateType);
	if (resolvedType != genericInstType)
	{
		if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
			mContext->mGenericTypeAliasPool.GiveBack((BfGenericTypeAliasType*)genericInstType);
		else
			mContext->mGenericTypeInstancePool.GiveBack(genericInstType);
		mContext->mTypeDefTypeRefPool.GiveBack(typeRef);
	}
	BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
	return resolvedType;
}

int checkIdx = 0;

BfTypeDef* BfModule::ResolveGenericInstanceDef(BfGenericInstanceTypeRef* genericTypeRef)
{
	BfTypeReference* typeRef = genericTypeRef->mElementType;
	int numGenericParams = genericTypeRef->GetGenericArgCount();

	BfTypeDef* curTypeDef = NULL;
	if (mCurTypeInstance != NULL)
		curTypeDef = mCurTypeInstance->mTypeDef;

	if (auto directTypeDef = BfNodeDynCast<BfDirectTypeReference>(typeRef))
	{
		auto typeInst = directTypeDef->mType->ToTypeInstance();
		return typeInst->mTypeDef;
	}

	auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef);
	auto directStrTypeDef = BfNodeDynCastExact<BfDirectStrTypeReference>(typeRef);
	if ((namedTypeRef != NULL) || (directStrTypeDef != NULL))
	{
		
		BfTypeLookupError error;
		error.mRefNode = typeRef;
		BfTypeDef* typeDef = FindTypeDef(typeRef, NULL, &error, numGenericParams);		
		if (typeDef != NULL)
		{
			BfAutoComplete* autoComplete = NULL;
			if (mCompiler->IsAutocomplete())
				autoComplete = mCompiler->mResolvePassData->mAutoComplete;
			
			if ((autoComplete != NULL) && (autoComplete->mIsGetDefinition) && (autoComplete->IsAutocompleteNode(typeRef)))
			{
				if ((autoComplete->mDefMethod == NULL) && (autoComplete->mDefField == NULL) &&
					(autoComplete->mDefProp == NULL) && (typeDef->mTypeDeclaration != NULL))
				{
					autoComplete->mDefType = typeDef;
					autoComplete->SetDefinitionLocation(typeDef->mTypeDeclaration->mNameNode);
				}
			}

			if (mCompiler->mResolvePassData != NULL)
				mCompiler->mResolvePassData->HandleTypeReference(typeRef, typeDef);

            return typeDef;
		}

		if (mCurTypeInstance != NULL)
		{
			bool wasGenericParam = false;

			// Check generics first
			if (typeRef->IsA<BfNamedTypeReference>())
			{
				String findName = typeRef->ToString();
				if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsGenericTypeInstance()))
				{
					auto genericTypeInst = (BfGenericTypeInstance*)mCurTypeInstance;
					for (int genericParamIdx = 0; genericParamIdx < (int)curTypeDef->mGenericParamDefs.size(); genericParamIdx++)
					{
						String genericName = curTypeDef->mGenericParamDefs[genericParamIdx]->mName;
						if (genericName == findName)
							wasGenericParam = true;
					}
				}

				if (mCurMethodInstance != NULL)
				{
					for (int genericParamIdx = 0; genericParamIdx < (int)mCurMethodInstance->mMethodDef->mGenericParams.size(); genericParamIdx++)
					{
						String genericName = mCurMethodInstance->mMethodDef->mGenericParams[genericParamIdx]->mName;
						if (genericName == findName)
							wasGenericParam = true;
					}
				}
			}

			if (wasGenericParam)
				Fail("Cannot use generic param as generic instance type", typeRef);
		}

		//if (mCurTypeInstance != NULL)
		//{
		//	String findName;
		//	if (directStrTypeDef != NULL)
		//		findName = directStrTypeDef->mTypeName;
		//	else
		//		findName = namedTypeRef->mNameNode->ToString();
		//	auto outerTypeInstance = mCurTypeInstance;

		//	for (int pass = 0; pass < 2; pass++)
		//	{
		//		bool isFailurePass = pass == 1;
		//		bool allowPrivate = true;
		//		bool allowProtected = true;

		//		auto checkOuterType = outerTypeInstance;
		//		while (checkOuterType != NULL)
		//		{
		//			for (auto checkType : checkOuterType->mTypeDef->mNestedTypes)
		//			{
		//				if ((!isFailurePass) && (!CheckProtection(checkType->mProtection, allowProtected, allowPrivate)))
		//					continue;

		//				if (checkType->mName->mString == findName)
		//				{
		//					if (isFailurePass)
		//					{
		//						// This is the one error we don't ignore when ignoreErrors is set
		//						Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(checkOuterType).c_str(), BfTypeUtils::TypeToString(namedTypeRef).c_str()), namedTypeRef); // CS0122
		//					}

		//					return checkType;
		//				}
		//			}
		//			allowPrivate = false;
		//			if (checkOuterType == mContext->mBfObjectType)
		//				break;
		//			checkOuterType = GetBaseType(checkOuterType);
		//		}
		//	}
		//}

		if (typeDef == NULL)
		{
			TypeRefNotFound(typeRef);
			return NULL;
		}
	}

	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{		
		BfAutoParentNodeEntry autoParentNodeEntry(this, genericTypeRef);
		auto type = ResolveTypeRef(qualifiedTypeRef, BfPopulateType_TypeDef);
		if (type == NULL)
			return NULL;
		auto typeInst = type->ToTypeInstance();
		if (typeInst != NULL)
			return typeInst->mTypeDef;
	}

	Fail("Invalid generic type", typeRef);
	return NULL;
}

BfType* BfModule::ResolveGenericType(BfType* unspecializedType, const BfTypeVector& methodGenericArguments, bool allowFail)
{	
	if (unspecializedType->IsGenericParam())
	{
		auto genericParam = (BfGenericParamType*)unspecializedType;
		if (genericParam->mGenericParamKind == BfGenericParamKind_Method)
		{
			if (genericParam->mGenericParamIdx < (int)methodGenericArguments.size())
			{
				return methodGenericArguments[genericParam->mGenericParamIdx];				
			}
			BF_ASSERT(allowFail);
		}
		return unspecializedType;
	}

	if (!unspecializedType->IsUnspecializedType())
		return unspecializedType;

	if (unspecializedType->IsUnknownSizedArray())
	{
		auto* arrayType = (BfUnknownSizedArrayType*)unspecializedType;
		auto elementType = ResolveGenericType(arrayType->mElementType, methodGenericArguments, allowFail);
		if (elementType == NULL)
			return NULL;		
		auto sizeType = ResolveGenericType(arrayType->mElementCountSource, methodGenericArguments, allowFail);
		if (sizeType == NULL)
			return NULL;
		if (sizeType->IsConstExprValue())
		{
			return CreateSizedArrayType(elementType, ((BfConstExprValueType*)sizeType)->mValue.mInt32);
		}
		return CreateUnknownSizedArrayType(elementType, sizeType);
	}

	if (unspecializedType->IsSizedArray())
	{
		auto* arrayType = (BfSizedArrayType*)unspecializedType;
		auto elementType = ResolveGenericType(arrayType->mElementType, methodGenericArguments, allowFail);
		if (elementType == NULL)
			return NULL;
		return CreateSizedArrayType(elementType, (int)arrayType->mElementCount);
	}

	if (unspecializedType->IsRef())
	{
		auto refType = (BfRefType*)unspecializedType;
		auto elementType = ResolveGenericType(refType->GetUnderlyingType(), methodGenericArguments, allowFail);
		if (elementType == NULL)
			return NULL;
		return CreateRefType(elementType, refType->mRefKind);
	}

	if (unspecializedType->IsPointer())
	{
		auto ptrType = (BfPointerType*)unspecializedType;
		auto elementType = ResolveGenericType(ptrType->GetUnderlyingType(), methodGenericArguments, allowFail);
		if (elementType == NULL)
			return NULL;
		return CreatePointerType(elementType);
	}

	if (unspecializedType->IsArray())
	{
		auto arrayType = (BfArrayType*)unspecializedType;
		auto elementType = ResolveGenericType(arrayType->GetUnderlyingType(), methodGenericArguments, allowFail);
		if (elementType == NULL)
			return NULL;
		return CreateArrayType(elementType, arrayType->mDimensions);
	}

	if (unspecializedType->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfGenericTypeInstance*)unspecializedType;

		BfTypeVector genericArgs;
		for (auto genericArg : genericTypeInst->mTypeGenericArguments)
		{
			if (genericArg->IsUnspecializedType())
			{
				auto resolvedArg = ResolveGenericType(genericArg, methodGenericArguments, allowFail);
				if (resolvedArg == NULL)
					return NULL;
				genericArgs.push_back(resolvedArg);
			}
			else
				genericArgs.push_back(genericArg);
		}

		return ResolveTypeDef(genericTypeInst->mTypeDef, genericArgs);
	}

	if (unspecializedType->IsTuple())
	{
		auto tupleType = (BfTupleType*)unspecializedType;
		Array<String> names;

		BfTypeVector genericArgs;
		bool hadChange = false;

		for (auto& fieldInstance : tupleType->mFieldInstances)
		{
			names.push_back(fieldInstance.GetFieldDef()->mName);
			auto origGenericArg = fieldInstance.mResolvedType;
			auto newGenericArg = ResolveGenericType(origGenericArg, methodGenericArguments, allowFail);
			if (newGenericArg == NULL)
				return NULL;
			if (newGenericArg != origGenericArg)
				hadChange = true;
			genericArgs.push_back(newGenericArg);
		}
		if (!hadChange)
			return unspecializedType;

		return CreateTupleType(genericArgs, names);
	}

	if (unspecializedType->IsDelegate())
	{
		BfDelegateType* unspecializedDelegateType = (BfDelegateType*)unspecializedType;
		BfDelegateType* delegateType = mContext->mDelegateTypePool.Get();
	
		bool failed = false;

		auto returnType = ResolveGenericType(unspecializedDelegateType->mReturnType, methodGenericArguments, allowFail);
		if (returnType == NULL)
			return NULL;

		auto baseDelegateType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();
		
		delegateType->mIsUnspecializedType = false;
		delegateType->mIsUnspecializedTypeVariation = false;
		if (delegateType->mTypeDef != NULL)
			delete delegateType->mTypeDef;
		delegateType->mParams.Clear();

		BfTypeDef* typeDef = new BfTypeDef();

		typeDef->mProject = baseDelegateType->mTypeDef->mProject;
		typeDef->mSystem = mCompiler->mSystem;
		typeDef->mName = mSystem->mEmptyAtom;
		typeDef->mTypeCode = unspecializedDelegateType->mTypeDef->mTypeCode;
		typeDef->mIsDelegate = unspecializedDelegateType->mTypeDef->mIsDelegate;
		typeDef->mIsFunction = unspecializedDelegateType->mTypeDef->mIsFunction;
		
		BfMethodDef* methodDef = new BfMethodDef();
		methodDef->mDeclaringType = typeDef;
		methodDef->mName = "Invoke";
		methodDef->mProtection = BfProtection_Public;
		methodDef->mIdx = 0;
		methodDef->mIsStatic = !typeDef->mIsDelegate;

		auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateType->mDirectAllocNodes.push_back(directTypeRef);
		if (typeDef->mIsDelegate)
			directTypeRef->Init(delegateType);
		else
			directTypeRef->Init(ResolveTypeDef(mCompiler->mFunctionTypeDef));
		typeDef->mBaseTypes.push_back(directTypeRef);

		directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateType->mDirectAllocNodes.push_back(directTypeRef);
		directTypeRef->Init(returnType);
		methodDef->mReturnTypeRef = directTypeRef;
		delegateType->mReturnType = returnType;

		AddDependency(directTypeRef->mType, baseDelegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		
		BfMethodDef* unspecializedInvokeMethodDef = unspecializedDelegateType->mTypeDef->GetMethodByName("Invoke");

		int paramIdx = 0;
		for (auto param : unspecializedDelegateType->mParams)
		{			
			auto paramType = ResolveGenericType(param, methodGenericArguments, allowFail);
			if (paramType == NULL)
			{
				failed = true;
				break;
			}
					
			BfParameterDef* unspecializedParamDef = unspecializedInvokeMethodDef->mParams[paramIdx];
			
			if (paramType->IsUnspecializedType())
				delegateType->mIsUnspecializedType = true;
			if (paramType->IsUnspecializedTypeVariation())
				delegateType->mIsUnspecializedTypeVariation = true;
			if (!paramType->IsReified())
				delegateType->mIsReified = false;
			if (paramType->IsGenericParam())
			{
				delegateType->mIsUnspecializedTypeVariation = true;
			}

			auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
			delegateType->mDirectAllocNodes.push_back(directTypeRef);
			directTypeRef->Init(paramType);

			BfParameterDef* paramDef = new BfParameterDef();

			paramDef->mTypeRef = directTypeRef;
			paramDef->mName = unspecializedParamDef->mName;
			methodDef->mParams.push_back(paramDef);
			paramIdx++;

			delegateType->mParams.Add(paramType);
			AddDependency(paramType, baseDelegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		}

		typeDef->mMethods.push_back(methodDef);

		//	

		BfDefBuilder::AddMethod(typeDef, BfMethodType_Ctor, BfProtection_Public, false, "");
		if (typeDef->mIsDelegate)
			BfDefBuilder::AddDynamicCastMethods(typeDef);

		delegateType->mContext = mContext;
		delegateType->mTypeDef = typeDef;
		
		BfType* resolvedType = NULL;
		if (!failed)
			resolvedType = ResolveType(delegateType, BfPopulateType_Identity);
		if (resolvedType != delegateType)
		{
			mContext->mDelegateTypePool.GiveBack(delegateType);
			//mContext->mTypeDefTypeRefPool.GiveBack(typeRef);
		}
		BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
		return resolvedType;

	}

	return unspecializedType;
}

BfType* BfModule::ResolveType(BfType* lookupType, BfPopulateType populateType)
{
	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mModule = this;
	BfResolvedTypeSet::Entry* resolvedEntry = NULL;
	bool inserted = mContext->mResolvedTypes.Insert(lookupType, &lookupCtx, &resolvedEntry);	
	if (!inserted)
	{
		auto resolvedTypeRef = resolvedEntry->mValue;
		PopulateType(resolvedTypeRef, populateType);
		return resolvedTypeRef;
	}

	if (lookupType->IsGenericTypeInstance())
		CheckUnspecializedGenericType((BfGenericTypeInstance*)lookupType, populateType);

	if (lookupType->IsTuple())
	{
		auto tupleType = (BfTupleType*)lookupType;
		tupleType->Finish();
	}

	resolvedEntry->mValue = lookupType;
	if (!InitType(lookupType, populateType))
		return NULL;
	return lookupType;
}


bool BfModule::IsUnboundGeneric(BfType* type)
{
	if (type->IsVar())
		return true;
	if (!type->IsGenericParam())
		return false;
	auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)type);
	return (genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0;
}

BfGenericParamInstance* BfModule::GetGenericTypeParamInstance(int genericParamIdx)
{
	// When we're evaluating a method, make sure the params refer back to that method context
	auto curTypeInstance = mCurTypeInstance;
	if (mCurMethodInstance != NULL)
		curTypeInstance = mCurMethodInstance->mMethodInstanceGroup->mOwner;

	BfGenericTypeInstance* genericTypeInst = curTypeInstance->ToGenericTypeInstance();
	if ((genericTypeInst->IsIncomplete()) && (genericTypeInst->mGenericParams.size() == 0))
	{
		// Set this to NULL so we don't recurse infinitely
		SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mCurTypeInstance, NULL);
		PopulateType(genericTypeInst, BfPopulateType_Declaration);
	}

	if (genericTypeInst->mGenericExtensionInfo != NULL)
	{
		auto activeTypeDef = GetActiveTypeDef(NULL, true);
		if ((activeTypeDef->mTypeDeclaration != genericTypeInst->mTypeDef->mTypeDeclaration) && (activeTypeDef->IsExtension()))
		{
			BfTypeDef* lookupTypeDef = activeTypeDef;
			while (lookupTypeDef->mNestDepth > genericTypeInst->mTypeDef->mNestDepth)
				lookupTypeDef = lookupTypeDef->mOuterType;

			BfGenericExtensionEntry* genericExEntry;
			if (genericTypeInst->mGenericExtensionInfo->mExtensionMap.TryGetValue(lookupTypeDef, &genericExEntry))
			{
				return genericExEntry->mGenericParams[genericParamIdx];
			}
			else
			{
				if ((mCompiler->mResolvePassData == NULL) || (mCompiler->mResolvePassData->mAutoComplete == NULL))
				{
					BF_FATAL("Invalid GetGenericParamInstance with extension");
				}
			}
		}
	}

	BF_ASSERT(genericTypeInst != NULL);
	return genericTypeInst->mGenericParams[genericParamIdx];
}

BfGenericParamInstance* BfModule::GetGenericParamInstance(BfGenericParamType* type)
{
	if (type->mGenericParamKind == BfGenericParamKind_Method)
	{		
		return mCurMethodInstance->mMethodInfoEx->mGenericParams[type->mGenericParamIdx];
	}

	return GetGenericTypeParamInstance(type->mGenericParamIdx);
}

BfType* BfModule::ResolveTypeResult(BfTypeReference* typeRef, BfType* resolvedTypeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	if (mCompiler->mIsResolveOnly)
	{
		bool isGetDefinition = false;
		BfAutoComplete* autoComplete = NULL;
		if (mCompiler->IsAutocomplete())
			autoComplete = mCompiler->mResolvePassData->mAutoComplete;

		BfSourceData* typeRefSource = NULL;
		if (typeRef->IsTemporary())
		{
			BfTypeReference* checkTypeRef = typeRef;
			if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(checkTypeRef))
				checkTypeRef = genericTypeRef->mElementType;
			if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(checkTypeRef))
				typeRefSource = namedTypeRef->mNameNode->GetSourceData();
		}
		else
			typeRefSource = typeRef->GetSourceData();
		if ((mCompiler->mResolvePassData->mSourceClassifier != NULL) && (typeRefSource != NULL) && (mCompiler->mResolvePassData->mParser != NULL) && 
			(typeRefSource == mCompiler->mResolvePassData->mParser->mSourceData))
		{
			//TODO: By only breaking out for "mIgnoreErrors", we classified elements (below) even when a resolvedTypeRef was not found!
			//Why did we have this mIgnoreErrors check in there?
// 			if ((resolvedTypeRef == NULL) && (mIgnoreErrors))
			if (resolvedTypeRef == NULL)
		 	{
		 		return NULL;
		 	}

			BfTypeInstance* resolvedTypeInstance = NULL;
			if (resolvedTypeRef != NULL)
				resolvedTypeInstance = resolvedTypeRef->ToTypeInstance();

			bool isNamespace = false;
			auto checkTypeRef = typeRef;
			if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(checkTypeRef))
				checkTypeRef = elementedTypeRef->mElementType;

			if (!mIsInsideAutoComplete)
			{
				if ((resolvedTypeInstance != NULL) && (resolvedTypeInstance->mTypeDef->IsGlobalsContainer()))
				{
					isNamespace = true;
				}
				else
				{
					//TODO: This broke colorizing of inner expressions for things like "T2[T3]"
					//mCompiler->mResolvePassData->mSourceClassifier->VisitChildNoRef(typeRef);
				}
			}
			
			while (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkTypeRef))
			{
				StringView leftString = qualifiedTypeRef->mLeft->ToStringView();
				BfSizedAtomComposite leftComposite;
				bool isValid = mSystem->ParseAtomComposite(leftString, leftComposite);
				mCompiler->mResolvePassData->mSourceClassifier->SetElementType(qualifiedTypeRef->mRight, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_TypeRef);
				if (resolvedTypeInstance == NULL)
				{
					if ((isValid) && (mCompiler->mSystem->ContainsNamespace(leftComposite, mCurTypeInstance->mTypeDef->mProject)))
						isNamespace = true;
				}
				else if ((isValid) && (resolvedTypeInstance->mTypeDef->mNamespace.EndsWith(leftComposite)))
				{
					if ((autoComplete != NULL) && (autoComplete->CheckFixit(typeRef)))
						autoComplete->FixitCheckNamespace(GetActiveTypeDef(), qualifiedTypeRef->mLeft, qualifiedTypeRef->mDot);
					isNamespace = true;
				}
				checkTypeRef = qualifiedTypeRef->mLeft;
			}

			if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(checkTypeRef))
			{
				auto checkNameNode = namedTypeRef->mNameNode;

				while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNameNode))
				{
					StringView leftString =  qualifiedNameNode->mLeft->ToStringView();
					BfSizedAtomComposite leftComposite;
					bool isValid = mSystem->ParseAtomComposite(leftString, leftComposite);
					mCompiler->mResolvePassData->mSourceClassifier->SetElementType(qualifiedNameNode->mRight, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_TypeRef);
					if (resolvedTypeInstance == NULL)
					{
						if ((isValid) && (mCompiler->mSystem->ContainsNamespace(leftComposite, mCurTypeInstance->mTypeDef->mProject)))
							isNamespace = true;
					}
					else if ((isValid) && (resolvedTypeInstance->mTypeDef->mNamespace.EndsWith(leftComposite)))
						isNamespace = true;
					checkNameNode = qualifiedNameNode->mLeft;
				}				
				mCompiler->mResolvePassData->mSourceClassifier->SetElementType(checkNameNode, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_TypeRef);
			}
		}
		
		if (autoComplete != NULL)
		{
			isGetDefinition = autoComplete->mIsGetDefinition;
		}

		if (((mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Type) || (isGetDefinition)) &&
			((resolveFlags & BfResolveTypeRefFlag_FromIndirectSource) == 0) && (resolvedTypeRef != NULL) && (typeRefSource != NULL))
		{
			BfAstNode* elementTypeRef = typeRef;
			if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(elementTypeRef))
				elementTypeRef = namedTypeRef->mNameNode;

			if (elementTypeRef != NULL)
			{				
				BfType* elementType = resolvedTypeRef;								
				if (BfTypeInstance* elementTypeInst = elementType->ToTypeInstance())
				{
					mCompiler->mResolvePassData->HandleTypeReference(elementTypeRef, elementTypeInst->mTypeDef);
					if (mCompiler->IsAutocomplete())
					{
						BfAutoComplete* autoComplete = mCompiler->mResolvePassData->mAutoComplete;
						if ((autoComplete->mIsGetDefinition) && (autoComplete->IsAutocompleteNode(elementTypeRef)))
						{
							BfAstNode* baseNode = elementTypeRef;
							while (true)
							{
								if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(baseNode))
								{
									baseNode = qualifiedTypeRef->mRight;
								}
								else if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(baseNode))
								{
									baseNode = elementedTypeRef->mElementType;
								}
								else if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(baseNode))
								{
									baseNode = namedTypeRef->mNameNode;
								}
								else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(baseNode))
								{
									baseNode = qualifiedNameNode->mRight;
								}
								else if (auto declTypeRef = BfNodeDynCast<BfDeclTypeRef>(baseNode))
								{
									baseNode = NULL;
									break;
								}
								else
									break;
							}
							if ((baseNode != NULL) && (autoComplete->IsAutocompleteNode(baseNode)))
							{
								// We didn't have this mDefType check before - why? We always want to catch the FIRST definition, 
								//  so 'Type?' will catch on 'Type' and not 'Type?'
								if ((autoComplete->mDefType == NULL) &&
									(autoComplete->mDefMethod == NULL) && (autoComplete->mDefField == NULL) &&
									(autoComplete->mDefProp == NULL) && (elementTypeInst->mTypeDef->mTypeDeclaration != NULL))
								{
									autoComplete->mDefType = elementTypeInst->mTypeDef;
									autoComplete->SetDefinitionLocation(elementTypeInst->mTypeDef->mTypeDeclaration->mNameNode);
								}
							}
						}
					}
				}				
			}
		}
	}

	if (resolvedTypeRef == NULL)
		return NULL;	

	if (resolvedTypeRef->IsTuple())
	{
		// Add the fields from the tuple as references since those inner fields types would have been explicitly stated, so we need
		//  to make sure to record the current type instance as a referring type.  This mostly matters for symbol renaming.
		BfTupleType* payloadTupleType = (BfTupleType*)resolvedTypeRef;
		for (auto& payloadFieldInst : payloadTupleType->mFieldInstances)
		{
			auto payloadFieldType = payloadFieldInst.mResolvedType;
			AddDependency(payloadFieldType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		}
	}
	else if (resolvedTypeRef->IsDelegateFromTypeRef() || resolvedTypeRef->IsFunctionFromTypeRef())
	{
		auto delegateType = (BfDelegateType*)resolvedTypeRef;
		auto invokeMethod = GetDelegateInvokeMethod(delegateType);
		
		AddDependency(invokeMethod->mReturnType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		for (auto& param : invokeMethod->mParams)
		{
			AddDependency(param.mResolvedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		}
	}

	BfGenericTypeInstance* genericTypeInstance = NULL;
	if (resolvedTypeRef != NULL)
		genericTypeInstance = resolvedTypeRef->ToGenericTypeInstance();
	
	bool hadError = false;
	hadError = !PopulateType(resolvedTypeRef, populateType);
	
	if ((genericTypeInstance != NULL) && (genericTypeInstance != mCurTypeInstance) && (populateType > BfPopulateType_Identity))
	{
		if (((genericTypeInstance->mHadValidateErrors) || (!genericTypeInstance->mValidatedGenericConstraints) || (genericTypeInstance->mIsUnspecializedVariation)) &&
			((mCurMethodInstance == NULL) || (!mCurMethodInstance->mIsUnspecializedVariation)) &&
			((mCurTypeInstance == NULL) || (!mCurTypeInstance->IsUnspecializedTypeVariation())))
			ValidateGenericConstraints(typeRef, genericTypeInstance, false);
	}

	if (populateType != BfPopulateType_IdentityNoRemapAlias)
	{
		while ((resolvedTypeRef != NULL) && (resolvedTypeRef->IsTypeAlias()))
		{
			if (mCurTypeInstance != NULL)
				AddDependency(resolvedTypeRef, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
			resolvedTypeRef = resolvedTypeRef->GetUnderlyingType();
		}
	}

	return resolvedTypeRef;
}

void BfModule::ShowAmbiguousTypeError(BfAstNode* refNode, BfTypeDef* typeDef, BfTypeDef* otherTypeDef)
{
	BfType* type = ResolveTypeDef(typeDef, BfPopulateType_Identity);
	if (type == NULL)
		return;
	BfType* otherType = ResolveTypeDef(otherTypeDef, BfPopulateType_Identity);
	if (otherType == NULL)
		return;

	auto error = Fail(StrFormat("'%s' is an ambiguous reference between '%s' and '%s'",
		refNode->ToString().c_str(), TypeToString(type, BfTypeNameFlags_None).c_str(), TypeToString(otherType, BfTypeNameFlags_None).c_str()), refNode); // CS0104
	if (error != NULL)
	{
		mCompiler->mPassInstance->MoreInfo("See first definition", typeDef->mTypeDeclaration->mNameNode);
		mCompiler->mPassInstance->MoreInfo("See second definition", otherTypeDef->mTypeDeclaration->mNameNode);
	}
}

void BfModule::ShowGenericArgCountError(BfTypeReference* typeRef, int wantedGenericParams)
{
	BfGenericInstanceTypeRef* genericTypeInstRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef);

	BfAstNode* lastNode = typeRef;
	int genericArgDiffCount;
	if (genericTypeInstRef != NULL)
	{
		genericArgDiffCount = (int)genericTypeInstRef->mGenericArguments.size() - wantedGenericParams;
		lastNode = genericTypeInstRef->mOpenChevron;
		if (genericTypeInstRef->mCloseChevron != NULL)
			lastNode = genericTypeInstRef->mCloseChevron;
		if (genericTypeInstRef->mGenericArguments.size() > wantedGenericParams)
		{
			lastNode = genericTypeInstRef->mGenericArguments[wantedGenericParams];
			if (genericArgDiffCount == 1)
				Fail("Too many generic parameters, expected one fewer", lastNode);
			else
				Fail(StrFormat("Too many generic parameters, expected %d fewer", genericArgDiffCount), lastNode);
			return;
		}
	}
	else
		genericArgDiffCount = -wantedGenericParams;	
	
	if (wantedGenericParams == 1)
		Fail("Too few generic parameters, expected one more", lastNode);
	else
		Fail(StrFormat("Too few generic parameters, expected %d more", -genericArgDiffCount), lastNode);	
}

BfTypeDef* BfModule::GetActiveTypeDef(BfTypeInstance* typeInstanceOverride, bool useMixinDecl)
{
	BfTypeDef* useTypeDef = NULL;
	BfTypeInstance* typeInstance = (typeInstanceOverride != NULL) ? typeInstanceOverride : mCurTypeInstance;
	if (typeInstance != NULL)
		useTypeDef = typeInstance->mTypeDef;
	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL) && (useMixinDecl))
		useTypeDef = mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType;
	else if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mDeclaringType != NULL))
		useTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;
	else if (mContext->mCurTypeState != NULL)
	{
		if ((mContext->mCurTypeState->mCurFieldDef != NULL) && (mContext->mCurTypeState->mCurFieldDef->mDeclaringType != NULL))
			useTypeDef = mContext->mCurTypeState->mCurFieldDef->mDeclaringType;
		else if (mContext->mCurTypeState->mCurTypeDef != NULL)
			useTypeDef = mContext->mCurTypeState->mCurTypeDef;
	}
	return useTypeDef;
}

BfTypeDef* BfModule::FindTypeDefRaw(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstance, BfTypeDef* useTypeDef, BfTypeLookupError* error)
{
	if ((findName.mSize == 1) && (findName.mParts[0]->mIsSystemType))
	{
		//BP_ZONE("BfModule::FindTypeDefRaw_1");
		return mSystem->FindTypeDef(findName, 0, useTypeDef->mProject);
	}

	BfTypeInstance* skipCheckBaseType = NULL;	
	if ((mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mCurBaseTypeRef != NULL))
		skipCheckBaseType = mContext->mCurTypeState->mTypeInstance;

	BfTypeDefLookupContext lookupCtx;
	bool allowPrivate = true;

	int curPri = 1000;
	auto checkTypeInst = typeInstance;

	BfTypeDef* protErrorTypeDef = NULL;
	BfTypeInstance* protErrorOuterType = NULL;
	
	if (!lookupCtx.HasValidMatch())
	{
		std::function<bool(BfTypeInstance*)> _CheckType = [&](BfTypeInstance* typeInstance)
		{
			auto checkTypeInst = typeInstance;
			allowPrivate = true;
			while (checkTypeInst != NULL)
			{
				if (!checkTypeInst->mTypeDef->mNestedTypes.IsEmpty())
				{
					if (mSystem->FindTypeDef(findName, numGenericArgs, useTypeDef->mProject, checkTypeInst->mTypeDef->mFullNameEx, allowPrivate, &lookupCtx))
					{
						if (lookupCtx.HasValidMatch())
							return true;

						if ((lookupCtx.mBestTypeDef->mProtection == BfProtection_Private) && (!allowPrivate))
						{
							protErrorTypeDef = lookupCtx.mBestTypeDef;
							protErrorOuterType = checkTypeInst;
						}
					}
				}
				if (checkTypeInst == skipCheckBaseType)
					break;

				checkTypeInst = GetBaseType(checkTypeInst);
				allowPrivate = false;
			}			

			checkTypeInst = typeInstance;
			allowPrivate = true;
			while (checkTypeInst != NULL)
			{
				auto outerTypeInst = GetOuterType(checkTypeInst);
				if (outerTypeInst != NULL)
				{
					if (_CheckType(outerTypeInst))
						return true;
				}
				if (checkTypeInst == skipCheckBaseType)
					break;
				
				checkTypeInst = GetBaseType(checkTypeInst);
				allowPrivate = false;
			}

			return false;
		};

		_CheckType(typeInstance);
	}	
	
	if (!lookupCtx.HasValidMatch())
	{
		if (mSystem->mTypeDefs.TryGet(findName, NULL))
			mSystem->FindTypeDef(findName, numGenericArgs, useTypeDef->mProject, BfAtomComposite(), allowPrivate, &lookupCtx);
			
		for (auto& checkNamespace : useTypeDef->mNamespaceSearch)
		{			
			BfAtom* atom = findName.mParts[0];			
			BfAtom* prevAtom = checkNamespace.mParts[checkNamespace.mSize - 1];
			if (atom->mPrevNamesMap.ContainsKey(prevAtom))			
				mSystem->FindTypeDef(findName, numGenericArgs, useTypeDef->mProject, checkNamespace, allowPrivate, &lookupCtx);
		}
	}
	
	if ((error != NULL) && (lookupCtx.mAmbiguousTypeDef != NULL))
	{
		if (error->mErrorKind == BfTypeLookupError::BfErrorKind_None)		
			error->mErrorKind = BfTypeLookupError::BfErrorKind_Ambiguous;						
		error->mAmbiguousTypeDef = lookupCtx.mAmbiguousTypeDef;

		if (error->mRefNode != NULL)
			ShowAmbiguousTypeError(error->mRefNode, lookupCtx.mBestTypeDef, lookupCtx.mAmbiguousTypeDef);
	}

	if ((protErrorTypeDef != NULL) && (lookupCtx.mBestTypeDef == protErrorTypeDef) && (error != NULL) && (error->mRefNode != NULL))
		Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(protErrorOuterType).c_str(), findName.ToString().c_str()), error->mRefNode); // CS0122

	return lookupCtx.mBestTypeDef;
}

BfTypeDef* BfModule::FindTypeDef(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error)
{
	BP_ZONE("BfModule::FindTypeDef_1");

	BfTypeInstance* typeInstance = (typeInstanceOverride != NULL) ? typeInstanceOverride : mCurTypeInstance;
	if (typeInstance == NULL)
	{
		BfProject* project = NULL;
		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mParser != NULL))
			project = mCompiler->mResolvePassData->mParser->mProject;

		BP_ZONE("System.FindTypeDef_2");		
		BfTypeDef* ambiguousTypeDef = NULL;
		BfTypeDef *result = mSystem->FindTypeDef(findName, numGenericArgs, project, Array<BfAtomComposite>(), &ambiguousTypeDef);
		if ((ambiguousTypeDef != NULL) && (error != NULL))
		{
			error->mErrorKind = BfTypeLookupError::BfErrorKind_Ambiguous;
			error->mAmbiguousTypeDef = ambiguousTypeDef;
			if (error->mRefNode != NULL)
				ShowAmbiguousTypeError(error->mRefNode, result, ambiguousTypeDef);
		}

		return result;
	}

	auto useTypeDef = GetActiveTypeDef(typeInstanceOverride, true);

	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		if (mCompiler->mResolvePassData->mAutoCompleteTempTypes.Contains(useTypeDef))
			return FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, error);
	}

	BfTypeLookupEntry typeLookupEntry;
	typeLookupEntry.mName = findName;
	typeLookupEntry.mNumGenericParams = numGenericArgs;
	typeLookupEntry.mUseTypeDef = useTypeDef;

	BfTypeLookupEntry* typeLookupEntryPtr = NULL;
	BfTypeLookupResult* resultPtr = NULL;
	if (typeInstance->mLookupResults.TryAdd(typeLookupEntry, &typeLookupEntryPtr, &resultPtr))
	{
		typeLookupEntryPtr->mAtomUpdateIdx = typeLookupEntry.mName.GetAtomUpdateIdx();

		// FindTypeDefRaw may re-enter when finding base types, so we need to expect that resultPtr can change
		resultPtr->mForceLookup = true;
		resultPtr->mTypeDef = NULL;
		int prevAllocSize = (int)typeInstance->mLookupResults.size();

		BfTypeLookupError localError;
		BfTypeLookupError* errorPtr = (error != NULL) ? error : &localError;
		auto typeDef = FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, errorPtr);

		if (prevAllocSize != typeInstance->mLookupResults.size())
		{
			bool found = typeInstance->mLookupResults.TryGetValue(typeLookupEntry, &resultPtr);
			BF_ASSERT(found);
		}

		resultPtr->mTypeDef = typeDef;
		resultPtr->mForceLookup = errorPtr->mErrorKind != BfTypeLookupError::BfErrorKind_None;		

		return typeDef;
	}
	else
	{
		if (resultPtr->mForceLookup)
			return FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, error);
		else
			return resultPtr->mTypeDef;
	}
	
}

BfTypeDef* BfModule::FindTypeDef(const StringImpl& typeName, int numGenericArgs, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error)
{
	BP_ZONE("BfModule::FindTypeDef_4");

	BfSizedAtomComposite findName;
	if (!mSystem->ParseAtomComposite(typeName, findName))
		return NULL;
	auto result = FindTypeDef(findName, numGenericArgs, typeInstanceOverride, error);
	BF_ASSERT((result == NULL) || (result->mTypeCode != BfTypeCode_Extension));
	return result;
}

BfTypeDef* BfModule::FindTypeDef(BfTypeReference* typeRef, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error, int numGenericParams)
{
	BP_ZONE("BfModule::FindTypeDef_5");

	if (auto typeDefTypeRef = BfNodeDynCast<BfDirectTypeDefReference>(typeRef))
	{
		if (typeDefTypeRef->mTypeDef != NULL)
			return mSystem->FilterDeletedTypeDef(typeDefTypeRef->mTypeDef);
	}

	//TODO: When does this get called?
	if (auto elementedType = BfNodeDynCast<BfElementedTypeRef>(typeRef))
		return FindTypeDef(elementedType->mElementType, typeInstanceOverride, error);

	BF_ASSERT(typeRef->IsA<BfNamedTypeReference>() || typeRef->IsA<BfQualifiedTypeReference>() || typeRef->IsA<BfDirectStrTypeReference>());
	auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef);

	StringView findNameStr;	
	if (namedTypeRef != NULL)
		findNameStr = namedTypeRef->mNameNode->ToStringView();
	else
	{
		auto directStrTypeDef = BfNodeDynCastExact<BfDirectStrTypeReference>(typeRef);
		if (directStrTypeDef != NULL)
			findNameStr = directStrTypeDef->mTypeName;
		else
			BF_FATAL("Error?");
	}

	if (findNameStr.mLength == 6)
	{
		if (findNameStr == "object")
		{
			findNameStr = "System.Object";
			Fail("'object' alias not supported, use 'Object'", typeRef);
		}
		else if (findNameStr == "string")
		{
			findNameStr = "System.String";
			Fail("'string' alias not supported, use 'String'", typeRef);
		}
	}	

	BfSizedAtomComposite findName;
	if (!mSystem->ParseAtomComposite(findNameStr, findName))
	{
		return NULL;
	}
	
#ifdef BF_AST_HAS_PARENT_MEMBER
	if (auto parentGenericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef->mParent))
	{
		if (parentGenericTypeRef->mElementType == typeRef)
			BF_ASSERT(numGenericParams == parentGenericTypeRef->GetGenericArgCount());			
	}
#endif

	auto typeDef = FindTypeDef(findName, numGenericParams, typeInstanceOverride, error);
//TYPEDEF 	if (namedTypeRef != NULL)
// 		namedTypeRef->mTypeDef = typeDef;

	return typeDef;
}

void BfModule::CheckTypeRefFixit(BfAstNode* typeRef, const char* appendName)
{
	if ((mCompiler->IsAutocomplete()) && (mCompiler->mResolvePassData->mAutoComplete->CheckFixit((typeRef))))
	{
		String typeName = typeRef->ToString();
		if (appendName != NULL)
			typeName += appendName;
		
		std::set<String> fixitNamespaces;

		//TODO: Do proper value for numGenericArgs		
		mSystem->FindFixitNamespaces(typeName, -1, mCompiler->mResolvePassData->mParser->mProject, fixitNamespaces);

		int insertLoc = 0;

		BfUsingFinder usingFinder;
		usingFinder.VisitMembers(typeRef->GetSourceData()->mRootNode);

		for (auto& namespaceStr : fixitNamespaces)
		{
			BfParserData* parser = typeRef->GetSourceData()->ToParserData();
			if (parser != NULL)
				mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("using %s;\tusing|%s|%d||using %s;", namespaceStr.c_str(), parser->mFileName.c_str(), usingFinder.mLastIdx, namespaceStr.c_str()).c_str()));
		}
	}
}

void BfModule::CheckIdentifierFixit(BfAstNode* node)
{
	//TODO: Check globals, possibly spelling mistakes?
}

void BfModule::TypeRefNotFound(BfTypeReference* typeRef, const char* appendName)
{
	if (typeRef->IsTemporary())
		return;

	Fail("Type could not be found (are you missing a using directive or library reference?)", typeRef);

	if (!mIgnoreErrors)
	{
		while (auto elementedType = BfNodeDynCast<BfElementedTypeRef>(typeRef))
			typeRef = elementedType->mElementType;

		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
		{
			String findNameStr = namedTypeRef->mNameNode->ToString();
			if (appendName != NULL)
				findNameStr += appendName;
			BfSizedAtomComposite findName;
			if ((!mSystem->ParseAtomComposite(findNameStr, findName)) && (mCurTypeInstance != NULL))
			{
				//BfTypeInstance* typeInstance = (typeInstanceOverride != NULL) ? typeInstanceOverride : mCurTypeInstance;
				// We don't need a typeInstanceOverride because that is used to lookup references
				//  from mixins, but it's the type using the mixin (mCurTypeInstance) that needs 
				// rebuilding if the lookup fails
				BfTypeInstance* typeInstance = mCurTypeInstance;
				BfTypeLookupEntry typeLookupEntry;
				typeLookupEntry.mNumGenericParams = 0;
				typeLookupEntry.mAtomUpdateIdx = mSystem->mAtomUpdateIdx;
				typeInstance->mLookupResults.TryAdd(typeLookupEntry, BfTypeLookupResult());
			}
		}
	}

	CheckTypeRefFixit(typeRef, appendName);
}

bool BfModule::ValidateTypeWildcard(BfTypeReference* typeRef, bool isAttributeRef)
{
	if (typeRef == NULL)
		return false;

	if (auto wildcardTypeRef = BfNodeDynCast<BfWildcardTypeReference>(typeRef))
		return true;
	
	StringT<128> nameStr;
	typeRef->ToString(nameStr);
	if (isAttributeRef)
		nameStr.Append("Attribute");
	auto typeDef = mSystem->FindTypeDef(nameStr, (BfProject*)NULL);
	if ((typeDef != NULL) && (typeDef->mGenericParamDefs.IsEmpty()))
		return true;	

	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		if (qualifiedTypeRef->mLeft == NULL)
			return false;

		StringT<128> leftNameStr;

		BfType* leftType = NULL;
		BfAtomComposite leftComposite;

		qualifiedTypeRef->mLeft->ToString(leftNameStr);
		if (!mSystem->ParseAtomComposite(leftNameStr, leftComposite))
			return false;

		if (auto wildcardTypeRef = BfNodeDynCast<BfWildcardTypeReference>(qualifiedTypeRef->mRight))
		{
			if (mSystem->ContainsNamespace(leftComposite, NULL))
				return true;

			return ValidateTypeWildcard(qualifiedTypeRef->mLeft, false);
		}		
	}

	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		StringT<128> nameStr;
		genericTypeRef->mElementType->ToString(nameStr);

		auto typeDef = mSystem->FindTypeDef(nameStr, (int)genericTypeRef->mGenericArguments.size(), NULL);
		if (typeDef == NULL)		
			return false;		

		if (typeDef->mGenericParamDefs.size() != genericTypeRef->GetGenericArgCount())
			return false;

		for (auto genericArgTypeRef : genericTypeRef->mGenericArguments)
		{
			if ((genericTypeRef != NULL) && (!ValidateTypeWildcard(genericArgTypeRef, false)))
				return false;
		}

		return true;
	}

	if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeRef))
	{
		return ValidateTypeWildcard(elementedTypeRef->mElementType, false);
	}

	return false;
}

//int sResolveTypeRefIdx = 0;

BfTypedValue BfModule::TryLookupGenericConstVaue(BfIdentifierNode* identifierNode, BfType* expectingType)
{
	BfTypeInstance* contextTypeInstance = mCurTypeInstance;
	BfMethodInstance* contextMethodInstance = mCurMethodInstance;
	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		contextTypeInstance = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
		contextMethodInstance = mCurMethodState->mMixinState->mMixinMethodInstance;
	}
	BfTypeDef* curTypeDef = NULL;
	if (contextTypeInstance != NULL)
	{
		curTypeDef = contextTypeInstance->mTypeDef;
		
		StringT<128> findName;
		identifierNode->ToString(findName);

		auto genericCheckTypeInstance = contextTypeInstance;
		if (contextTypeInstance->IsBoxed())
			genericCheckTypeInstance = contextTypeInstance->GetUnderlyingType()->ToTypeInstance();

		bool doFakeVal = false;
		if (genericCheckTypeInstance->IsUnspecializedTypeVariation())
		{
			genericCheckTypeInstance = GetUnspecializedTypeInstance(genericCheckTypeInstance);
			doFakeVal = true;
		}

		BfGenericParamDef* genericParamDef = NULL;
		BfType* genericParamResult = NULL;
		BfType* genericTypeConstraint = NULL;
		bool disallowConstExprValue = false;
		if ((genericCheckTypeInstance != NULL) && (genericCheckTypeInstance->IsGenericTypeInstance()))
		{
			auto genericTypeInst = (BfGenericTypeInstance*)genericCheckTypeInstance;
			auto* genericParams = &curTypeDef->mGenericParamDefs;
			
			if (genericTypeInst->mGenericExtensionInfo != NULL)
			{
				auto activeTypeDef = GetActiveTypeDef(NULL, true);
				genericParams = &activeTypeDef->mGenericParamDefs;
			}			
			
			for (int genericParamIdx = (int)genericParams->size() - 1; genericParamIdx >= 0; genericParamIdx--)
			{
				auto checkGenericParamDef = (*genericParams)[genericParamIdx];
				String genericName = checkGenericParamDef->mName;
							
				if (genericName == findName)
				{
					genericParamDef = checkGenericParamDef;
					genericParamResult = genericTypeInst->mTypeGenericArguments[genericParamIdx];
					genericTypeConstraint = genericTypeInst->mGenericParams[genericParamIdx]->mTypeConstraint;

					HandleTypeGenericParamRef(identifierNode, genericTypeInst->mTypeDef, genericParamIdx);
				}
			}
		}

		if ((contextMethodInstance != NULL) && (genericParamResult == NULL))
		{
			for (int genericParamIdx = (int)contextMethodInstance->mMethodDef->mGenericParams.size() - 1; genericParamIdx >= 0; genericParamIdx--)
			{
				auto checkGenericParamDef = contextMethodInstance->mMethodDef->mGenericParams[genericParamIdx];
				String genericName = checkGenericParamDef->mName;
				if (genericName == findName)
				{
					genericParamDef = checkGenericParamDef;
					genericParamResult = contextMethodInstance->mMethodInfoEx->mMethodGenericArguments[genericParamIdx];
					genericTypeConstraint = contextMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx]->mTypeConstraint;

					HandleMethodGenericParamRef(identifierNode, contextMethodInstance->GetOwner()->mTypeDef, contextMethodInstance->mMethodDef, genericParamIdx);
				}
			}
		}

		if (genericParamResult != NULL)
		{			
			auto typeRefSource = identifierNode->GetSourceData();
			if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mSourceClassifier != NULL) && (typeRefSource != NULL) && (typeRefSource == mCompiler->mResolvePassData->mParser->mSourceData))
				mCompiler->mResolvePassData->mSourceClassifier->SetElementType(identifierNode, BfSourceElementType_TypeRef);

			if (genericParamResult->IsConstExprValue())
			{
				BfConstExprValueType* constExprValueType = (BfConstExprValueType*)genericParamResult;

				BfExprEvaluator exprEvaluator(this);
				exprEvaluator.mExpectingType = genericTypeConstraint;
				exprEvaluator.GetLiteral(identifierNode, constExprValueType->mValue);
				
				// We don't want to validate type here				
				return exprEvaluator.mResult;
			}
			else if (genericParamResult->IsGenericParam())
			{
				if ((doFakeVal) && (genericTypeConstraint != NULL))
				{
					return BfTypedValue(mBfIRBuilder->GetFakeVal(), genericTypeConstraint);
				}

				if ((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0)									
				{
					if ((genericTypeConstraint != NULL) && (expectingType != NULL))
					{
						if (!CanCast(BfTypedValue(mBfIRBuilder->GetFakeVal(), genericTypeConstraint), expectingType))
						{
							Fail(StrFormat("Generic constraint '%s' is not convertible to 'int'", TypeToString(genericTypeConstraint).c_str()), identifierNode);
						}
					}

					BfTypedValue result;
					result.mType = genericParamResult;
					result.mKind = BfTypedValueKind_GenericConstValue;

					return result;
				}
			}
		}
	}

	return BfTypedValue();
}

BfType* BfModule::ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	BP_ZONE("BfModule::ResolveTypeRef");

	if (typeRef == NULL)
	{
		AssertErrorState();
		return NULL;
	}

	if (resolveFlags & BfResolveTypeRefFlag_AutoComplete)
	{
		resolveFlags = (BfResolveTypeRefFlags)(resolveFlags & ~BfResolveTypeRefFlag_AutoComplete);
		auto autoComplete = mCompiler->GetAutoComplete();
		if (autoComplete != NULL)
			autoComplete->CheckTypeRef(typeRef, false);
	}

	if ((resolveFlags & BfResolveTypeRefFlag_AllowRef) == 0)
	{
		if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(typeRef))
		{
			const char* refTypeStr = BfTokenToString(refTypeRef->mRefToken->mToken);
			Fail(StrFormat("Invalid use of '%s'. Only method parameters, return types, and local variables can be declared as %s types", refTypeStr, refTypeStr), refTypeRef->mRefToken);
			return ResolveTypeRef(refTypeRef->mElementType);
		}
	}

	if (auto directTypeRef = BfNodeDynCastExact<BfDirectTypeReference>(typeRef))
	{
		return directTypeRef->mType;
	}

	if (auto dotType = BfNodeDynCastExact<BfDotTypeReference>(typeRef))
	{		
		Fail(StrFormat("Invalid use of '%s'", BfTokenToString(dotType->mDotToken->mToken)), typeRef);
		return NULL;
	}

	if (auto varRefType = BfNodeDynCastExact<BfVarRefTypeReference>(typeRef))
	{
		Fail("Invalid use of 'var ref'. Generally references are generated with a 'var' declaration with 'ref' applied to the initializer", typeRef);
		return NULL;
	}

	if (mNoResolveGenericParams)
		resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_NoResolveGenericParam);
	SetAndRestoreValue<bool> prevNoResolveGenericParams(mNoResolveGenericParams, (resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0);

	//
	resolveFlags = (BfResolveTypeRefFlags)(resolveFlags & ~BfResolveTypeRefFlag_NoResolveGenericParam);

	BfTypeInstance* contextTypeInstance = mCurTypeInstance;
	BfMethodInstance* contextMethodInstance = mCurMethodInstance;
	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		contextTypeInstance = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
		contextMethodInstance = mCurMethodState->mMixinState->mMixinMethodInstance;
	}

	BfTypeDef* curTypeDef = NULL;
	if (contextTypeInstance != NULL)
	{
		curTypeDef = contextTypeInstance->mTypeDef;

		// Check generics first
		auto namedTypeRef = BfNodeDynCastExact<BfNamedTypeReference>(typeRef);
		auto directStrTypeRef = BfNodeDynCastExact<BfDirectStrTypeReference>(typeRef);
		if (((namedTypeRef != NULL) && (namedTypeRef->mNameNode != NULL)) || (directStrTypeRef != NULL))
		{			
			StringT<128> findName;
			if (namedTypeRef != NULL)
				namedTypeRef->mNameNode->ToString(findName);
			else
				findName = directStrTypeRef->mTypeName;
			if (findName == "Self")
			{
				BfType* selfType = mCurTypeInstance;
				if (selfType->IsInterface()) // For interfaces, 'Self' refers to the identity of the implementing type, so we use a placeholder
					return GetPrimitiveType(BfTypeCode_Self);
				else				
					resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_FromIndirectSource);				

				if (selfType->IsBoxed())
					selfType = selfType->GetUnderlyingType();
				if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
				{
					if ((selfType->IsSpecializedType()) || (selfType->IsUnspecializedTypeVariation()))
						selfType = ResolveTypeDef(selfType->ToTypeInstance()->mTypeDef, populateType);
				}
				if (selfType == NULL)
				{
					Fail("'Self' type is not usable here", typeRef);
				}
				return ResolveTypeResult(typeRef, selfType, populateType, resolveFlags);								
			}
			else if (findName == "SelfBase")
			{
				BfType* selfType = mCurTypeInstance;
				if (selfType->IsInterface())
					resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_FromIndirectSource);

				if (selfType->IsBoxed())
					selfType = selfType->GetUnderlyingType();
				if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
				{
					if ((selfType->IsSpecializedType()) || (selfType->IsUnspecializedTypeVariation()))
						selfType = ResolveTypeDef(selfType->ToTypeInstance()->mTypeDef, populateType);
				}
				BfType* baseType = NULL;
				if (selfType != NULL)
				{
					if (selfType->IsTypedPrimitive())
						baseType = selfType->GetUnderlyingType();
					else
					{
						auto selfTypeInst = selfType->ToTypeInstance();
						if (selfTypeInst != NULL)
						{
							baseType = selfTypeInst->mBaseType;
						}
					}
				}
				if (baseType == NULL)
				{
					Fail("'SelfBase' type is not usable here", typeRef);
				}				
				return ResolveTypeResult(typeRef, baseType, populateType, resolveFlags);
			}
			else if (findName == "ExpectedType")
			{
				Fail("'ExpectedType' is not usable here", typeRef);
				return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
			}

			auto genericCheckTypeInstance = contextTypeInstance;
			if (contextTypeInstance->IsBoxed())
				genericCheckTypeInstance = contextTypeInstance->GetUnderlyingType()->ToTypeInstance();

			BfGenericParamDef* genericParamDef = NULL;
			BfType* genericParamResult = NULL;
			bool disallowConstExprValue = false;
			if ((genericCheckTypeInstance != NULL) && (genericCheckTypeInstance->IsGenericTypeInstance()))
			{
				auto genericTypeInst = (BfGenericTypeInstance*)genericCheckTypeInstance;
				auto* genericParams = &curTypeDef->mGenericParamDefs;

				if (genericTypeInst->mGenericExtensionInfo != NULL)
				{
					auto activeTypeDef = GetActiveTypeDef(NULL, true);
					genericParams = &activeTypeDef->mGenericParamDefs;
				}

				for (int genericParamIdx = (int)genericParams->size() - 1; genericParamIdx >= 0; genericParamIdx--)
				{
					auto checkGenericParamDef = (*genericParams)[genericParamIdx];
					String genericName = checkGenericParamDef->mName;
					if (genericName == findName)
					{
						genericParamDef = checkGenericParamDef;
						if (((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0) &&
							((resolveFlags & BfResolveTypeRefFlag_AllowGenericTypeParamConstValue) == 0))
							disallowConstExprValue = true;

						HandleTypeGenericParamRef(typeRef, curTypeDef, genericParamIdx);

						if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
							return GetGenericParamType(BfGenericParamKind_Type, genericParamIdx);
						else
						{
							SetAndRestoreValue<BfGetSymbolReferenceKind> prevSymbolRefKind;
							if (mCompiler->mResolvePassData != NULL) // Don't add these typeRefs, they are indirect
								prevSymbolRefKind.Init(mCompiler->mResolvePassData->mGetSymbolReferenceKind, BfGetSymbolReferenceKind_None);							
							genericParamResult = genericTypeInst->mTypeGenericArguments[genericParamIdx];
							if ((genericParamResult != NULL) &&
								(genericParamResult->IsConstExprValue()) &&
								((resolveFlags & BfResolveTypeRefFlag_AllowGenericTypeParamConstValue) == 0))
								disallowConstExprValue = true;
						}
					}
				}
			}

			if ((contextMethodInstance != NULL) && (genericParamResult == NULL))
			{
				BfMethodInstance* prevMethodInstance = NULL;

				// If we're in a closure then use the outside method generic arguments
				auto checkMethodInstance = contextMethodInstance;
				if ((mCurMethodState != NULL) && (checkMethodInstance->mIsClosure))
				{
					auto checkMethodState = mCurMethodState;
					while (checkMethodState != NULL)
					{
						if ((checkMethodState->mMethodInstance != NULL) && (checkMethodState->mMethodInstance->mIsClosure))
						{
							checkMethodInstance = checkMethodState->mPrevMethodState->mMethodInstance;
						}
						checkMethodState = checkMethodState->mPrevMethodState;
					}
				}

				for (int genericParamIdx = (int)checkMethodInstance->mMethodDef->mGenericParams.size() - 1; genericParamIdx >= 0; genericParamIdx--)
				{
					auto checkGenericParamDef = checkMethodInstance->mMethodDef->mGenericParams[genericParamIdx];
					String genericName = checkGenericParamDef->mName;
					if (genericName == findName)
					{						
						genericParamDef = checkGenericParamDef;
						if (((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0) &&
							((resolveFlags & BfResolveTypeRefFlag_AllowGenericMethodParamConstValue) == 0))
							disallowConstExprValue = true;

						HandleMethodGenericParamRef(typeRef, checkMethodInstance->GetOwner()->mTypeDef, checkMethodInstance->mMethodDef, genericParamIdx);

						if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
							return GetGenericParamType(BfGenericParamKind_Method, genericParamIdx);
						else
						{
							SetAndRestoreValue<BfGetSymbolReferenceKind> prevSymbolRefKind;
							if (mCompiler->mResolvePassData != NULL) // Don't add these typeRefs, they are indirect
								prevSymbolRefKind.Init(mCompiler->mResolvePassData->mGetSymbolReferenceKind, BfGetSymbolReferenceKind_None);
							genericParamResult = checkMethodInstance->mMethodInfoEx->mMethodGenericArguments[genericParamIdx];
							if ((genericParamResult != NULL) &&
								(genericParamResult->IsConstExprValue()) &&
								((resolveFlags & BfResolveTypeRefFlag_AllowGenericMethodParamConstValue) == 0))
								disallowConstExprValue = true;
						}
					}
				}
			}

			if (genericParamResult != NULL)
			{
				if (disallowConstExprValue)
				{
					Fail("Invalid use of constant generic value", typeRef);
					return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
				}

				if (genericParamResult->IsRef())
				{
					if ((resolveFlags & BfResolveTypeRefFlag_AllowRefGeneric) == 0)
						genericParamResult = genericParamResult->GetUnderlyingType();
				}

				return ResolveTypeResult(typeRef, genericParamResult, populateType, (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_FromIndirectSource));
			}
		}
	}

	BfTypeDef* typeDef = NULL;
	
	if (typeRef->IsNamedTypeReference())	
	{
		BfTypeLookupError error;
		error.mRefNode = typeRef;
		typeDef = FindTypeDef(typeRef, contextTypeInstance, &error);

		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
		{
			if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(namedTypeRef->mNameNode))
			{
				// This handles the case where we have an "BaseClass.InnerClass", but the name is qualified as "DerivedClass.InnerClass"
				auto leftType = ResolveTypeRef(qualifiedNameNode->mLeft, NULL, BfPopulateType_Identity, (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef));
				if ((leftType != NULL) && (qualifiedNameNode->mRight != NULL))
				{
					// Try searching within inner type						
					auto resolvedType = ResolveInnerType(leftType, qualifiedNameNode->mRight, populateType, true);
					if (resolvedType != NULL)
					{
						if (mCurTypeInstance != NULL)
							AddDependency(leftType, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
						return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
					}
				}
			}
		}
			
		if ((typeDef == NULL) && (mCurTypeInstance != NULL))
		{
			// Try searching within inner type				
			auto checkOuterType = mCurTypeInstance;
			while (checkOuterType != NULL)
			{
				// We check for mBaseType to not be NULL because we can't inherit from an inner type, so don't even search there
				//  Causes reference cycles (bad).
				if ((checkOuterType != mCurTypeInstance) || (checkOuterType->mBaseType != NULL))
				{
					auto resolvedType = ResolveInnerType(checkOuterType, typeRef, populateType, true);
					if (resolvedType != NULL)
					{
						if (mCurTypeInstance != NULL)
							AddDependency(checkOuterType, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
						return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
					}
				}
				checkOuterType = GetOuterType(checkOuterType);
			}
		}
			
		if (typeDef == NULL)
		{
#ifdef BF_AST_HAS_PARENT_MEMBER
			if (auto parentQualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef->mParent))
			{
				BF_ASSERT(typeRef->mParent == mParentNodeEntry->mNode);
			}
#endif
			if (mParentNodeEntry != NULL)
			{
				if (auto parentQualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(mParentNodeEntry->mNode))
				{
					if (typeRef == parentQualifiedTypeRef->mLeft)
					{
						if ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0)
							TypeRefNotFound(typeRef);
						return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
					}
				}
			}

			if ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0)
			{
				TypeRefNotFound(typeRef);
				return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
			}

			return NULL;
		}	
	}
	else if (auto typeDefTypeRef = BfNodeDynCastExact<BfDirectTypeDefReference>(typeRef))
	{
		typeDef = typeDefTypeRef->mTypeDef;
	}

	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		//TODO: Determine why we had this prevIgnoreErrors set here.  It causes things like IEnumerator<Hey.Test<INVALIDNAME>> not fail
		//  properly on INVALIDNAME
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, /*true*/mIgnoreErrors);

		StringView leftNameStr;

		BfType* leftType = NULL;
		BfSizedAtomComposite leftComposite;

		bool leftIsValid = false;
		//bool leftIsValid = (qualifiedTypeRef->mLeft != NULL) && mSystem->ParseAtomComposite(qualifiedTypeRef->mLeft->ToString(), leftComposite);

		if (qualifiedTypeRef->mLeft != NULL)
		{
			leftNameStr = qualifiedTypeRef->mLeft->ToStringView();
			if (mSystem->ParseAtomComposite(leftNameStr, leftComposite))
				leftIsValid = true;
		}

		if ((leftIsValid) && (qualifiedTypeRef->mRight != NULL))
		{
			StringT<128> findName;

			auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(qualifiedTypeRef->mRight);
			auto activeTypeDef = GetActiveTypeDef();			
			BfProject* bfProject = NULL;
			if (activeTypeDef != NULL)
				bfProject = activeTypeDef->mProject;

			if (mSystem->ContainsNamespace(leftComposite, bfProject))
			{								
				qualifiedTypeRef->mLeft->ToString(findName);
				findName.Append('.');
				if (genericTypeRef != NULL)
					genericTypeRef->mElementType->ToString(findName);
				else
					qualifiedTypeRef->mRight->ToString(findName);
			}
			else if ((activeTypeDef != NULL) && (activeTypeDef->mNamespace.EndsWith(leftComposite)))
			{
				// Partial namespace reference, extend to a full reference

				findName += activeTypeDef->mNamespace.ToString();
				findName.Append('.');
				qualifiedTypeRef->mRight->ToString(findName);
			}

			if (!findName.IsEmpty())
			{
				int wantNumGenericArgs = 0;
#ifdef BF_AST_HAS_PARENT_MEMBER
				if (auto genericTypeParent = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef->mParent))
				{
					BF_ASSERT(mParentNodeEntry->mNode == genericTypeParent);
					//wantNumGenericArgs = (int)genericTypeParent->mGenericArguments.size();
					//genericTypeRef = genericTypeParent;
				}
#endif

				if (mParentNodeEntry != NULL)
				{
					if (auto genericTypeParent = BfNodeDynCast<BfGenericInstanceTypeRef>(mParentNodeEntry->mNode))
					{
						wantNumGenericArgs = (int)genericTypeParent->mGenericArguments.size();
						genericTypeRef = genericTypeParent;
					}
				}

				BfTypeDef* ambiguousTypeDef = NULL;

				auto typeDef = mSystem->FindTypeDef(findName, wantNumGenericArgs, bfProject, {}, &ambiguousTypeDef);
				if (typeDef != NULL)
				{
					if (ambiguousTypeDef != NULL)
						ShowAmbiguousTypeError(typeRef, typeDef, ambiguousTypeDef);

					BfTypeVector genericArgs;
					if (populateType != BfPopulateType_TypeDef)
					{
						if (genericTypeRef != NULL)
						{
							for (auto genericParamTypeRef : genericTypeRef->mGenericArguments)
							{
								auto genericParam = ResolveTypeRef(genericParamTypeRef, BfPopulateType_Declaration);
								if (genericParam == NULL)
									return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
								genericArgs.push_back(genericParam);
							}
						}

						if (typeDef->mGenericParamDefs.size() != genericArgs.size())
						{
							prevIgnoreErrors.Restore();

							BfAstNode* refNode = typeRef;
							if (genericTypeRef != NULL)
								refNode = genericTypeRef->mOpenChevron;

							int wantedGenericParams = (int)typeDef->mGenericParamDefs.size();
							if (wantedGenericParams == 1)
								Fail("Expected one generic argument", refNode);
							else
								Fail(StrFormat("Expected %d generic arguments", wantedGenericParams), refNode);
							return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
						}
					}

					return ResolveTypeResult(typeRef, ResolveTypeDef(typeDef, genericArgs, populateType), populateType, resolveFlags);
				}
			}
		}

		if (leftType == NULL)
		{
			BfAutoParentNodeEntry autoParentNodeEntry(this, qualifiedTypeRef);
			leftType = ResolveTypeRef(qualifiedTypeRef->mLeft, BfPopulateType_Declaration, BfResolveTypeRefFlag_IgnoreLookupError); // We throw an error below if we can't find the type
		}
				
		if (leftType == NULL)
		{
			mIgnoreErrors = prevIgnoreErrors.mPrevVal;
			BfTypeReference* errorRefNode = qualifiedTypeRef->mLeft;
			if ((leftIsValid) && (mCurTypeInstance != NULL) && (mSystem->ContainsNamespace(leftComposite, mCurTypeInstance->mTypeDef->mProject)))
			{
				// The left was a namespace name, so throw an error on the whole string
				errorRefNode = qualifiedTypeRef;
			}
			TypeRefNotFound(errorRefNode);

			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}
		prevIgnoreErrors.Restore();

		if (qualifiedTypeRef->mRight == NULL)
		{
			FailAfter("Expected identifier", qualifiedTypeRef->mDot);
			//AssertErrorState();
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		auto resolvedType = ResolveInnerType(leftType, qualifiedTypeRef->mRight, populateType);
		if ((resolvedType != NULL) && (mCurTypeInstance != NULL))
			AddDependency(leftType, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
		return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);

		// If we did a ResolveTypeResult, then that may process an alias as the alias-to type instead of the actual alias
		//return ResolveInnerType(leftType, qualifiedTypeRef->mRight, populateType);
	}

	if (auto resolvedTypeRef = BfNodeDynCast<BfResolvedTypeReference>(typeRef))
	{
		return ResolveTypeResult(typeRef, resolvedTypeRef->mType, populateType, resolveFlags);
	}

	if (auto retTypeTypeRef = BfNodeDynCastExact<BfRetTypeTypeRef>(typeRef))
	{
		bool allowThrough = false;
		BfType* resolvedType = NULL;
		if (retTypeTypeRef->mElementType != NULL)
		{
			auto innerType = ResolveTypeRef(retTypeTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
			if (innerType != NULL)
			{
				if ((innerType->IsDelegate()) || (innerType->IsFunction()))
				{
					PopulateType(innerType, BfPopulateType_DataAndMethods);
					BfMethodInstance* invokeMethodInstance = GetRawMethodInstanceAtIdx(innerType->ToTypeInstance(), 0, "Invoke");
					if (invokeMethodInstance != NULL)
					{
						resolvedType = invokeMethodInstance->mReturnType;
						return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
					}
				}				
				else if (innerType->IsGenericParam())
				{
					if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsUnspecializedTypeVariation()))
					{
						// We could have  case where we have "rettype(@T0)" and @T0 gets a type variation of @M0, but we can't do a 
						//  GetGenericParamInstance on that
						allowThrough = true;
					}
					else
					{
						auto genericParamInstance = GetGenericParamInstance((BfGenericParamType*)innerType);
						if (genericParamInstance->mTypeConstraint != NULL)
						{
							if ((genericParamInstance->mTypeConstraint->IsDelegate()) || (genericParamInstance->mTypeConstraint->IsFunction()))
							{
								resolvedType = GetDelegateReturnType(genericParamInstance->mTypeConstraint);
								return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
							}
							else if ((genericParamInstance->mTypeConstraint->IsTypeInstance()) && 
								((genericParamInstance->mTypeConstraint->ToTypeInstance()->mTypeDef == mCompiler->mDelegateTypeDef) ||
								 (genericParamInstance->mTypeConstraint->ToTypeInstance()->mTypeDef == mCompiler->mFunctionTypeDef)))
							{
								allowThrough = true;
							}
						}
					}
				}
				else if (innerType->IsMethodRef())
				{
					auto methodRefType = (BfMethodRefType*)innerType;					
					resolvedType = methodRefType->mMethodRef->mReturnType;
					return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
				}
			}
		}

		if (!allowThrough)
		{
			Fail("'rettype' can only be used on delegate or function types", retTypeTypeRef->mRetTypeToken);
			return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
		}
	}

	if (auto refTypeRef = BfNodeDynCastExact<BfRefTypeRef>(typeRef))
	{
		if ((refTypeRef->mRefToken != NULL) && (refTypeRef->mRefToken->GetToken() == BfToken_Mut) && (refTypeRef->mElementType != NULL))
		{
			bool needsRefWrap = false;

 			auto resolvedType = ResolveTypeRef(refTypeRef->mElementType, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
 			if (resolvedType != NULL)
 			{
				if ((resolvedType->IsComposite()) || (resolvedType->IsGenericParam()))
					needsRefWrap = true;								
 			}			

			if (!needsRefWrap)
			{
				// Non-composites (including pointers) don't actually need ref-wrapping for 'mut'
				return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
			}
		}
	}

	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mRootTypeRef = typeRef;
	lookupCtx.mRootTypeDef = typeDef;
	lookupCtx.mModule = this;
	BfResolvedTypeSet::Entry* resolvedEntry = NULL;
	auto inserted = mContext->mResolvedTypes.Insert(typeRef, &lookupCtx, &resolvedEntry);
	
	if (resolvedEntry == NULL)
	{		
		return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
	}

	if (!inserted)
	{
		BF_ASSERT(resolvedEntry->mValue != NULL);
		return ResolveTypeResult(typeRef, resolvedEntry->mValue, populateType, resolveFlags);
	}

	if (typeRef->IsTypeDefTypeReference())
	{
		//BF_ASSERT(typeDefTypeRef->mTypeDef != NULL); // Resolved higher up
		//auto typeDef = typeDefTypeRef->mTypeDef;
		if ((typeDef->mTypeCode >= BfTypeCode_None) && (typeDef->mTypeCode <= BfTypeCode_Double))
		{
			BfPrimitiveType* primType = new BfPrimitiveType();
			primType->mTypeDef = typeDef;
			resolvedEntry->mValue = primType;
			BF_ASSERT(BfResolvedTypeSet::Hash(primType, &lookupCtx, false) == resolvedEntry->mHash);
			InitType(primType, populateType);
			return ResolveTypeResult(typeRef, primType, populateType, resolveFlags);
		}

		if ((mCurTypeInstance != NULL) && (typeDef->mGenericParamDefs.size() != 0))
		{
			// Try to inherit generic params from current parent
						
			BfTypeDef* outerType = mSystem->GetCombinedPartial(typeDef->mOuterType);
			BF_ASSERT(!outerType->mIsPartial);
			if (TypeHasParent(mCurTypeInstance->mTypeDef, outerType))			
			{
				BfType* checkCurType = mCurTypeInstance;
				if (checkCurType->IsBoxed())
					checkCurType = checkCurType->GetUnderlyingType();
				if (checkCurType->IsTypeAlias())
					checkCurType = GetOuterType(checkCurType);

				BF_ASSERT(checkCurType->IsGenericTypeInstance());
				int numParentGenericParams = (int)outerType->mGenericParamDefs.size();
				int wantedGenericParams = (int)typeDef->mGenericParamDefs.size() - numParentGenericParams;
				if (wantedGenericParams != 0)
				{
					if (wantedGenericParams == 1)
						Fail("Expected generic argument", typeRef);
					else
						Fail(StrFormat("Expected %d generic arguments", wantedGenericParams), typeRef);
					mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
					return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
				}

				auto parentGenericTypeInstance = (BfGenericTypeInstance*)checkCurType;

				BfGenericTypeInstance* genericTypeInst;
				if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
				{
					auto typeAliasType = new BfGenericTypeAliasType();
					genericTypeInst = typeAliasType;
				}
				else
					genericTypeInst = new BfGenericTypeInstance();
				genericTypeInst->mTypeDef = typeDef;
				for (int i = 0; i < numParentGenericParams; i++)
				{
					genericTypeInst->mGenericParams.push_back(parentGenericTypeInstance->mGenericParams[i]->AddRef());
					genericTypeInst->mTypeGenericArguments.push_back(parentGenericTypeInstance->mTypeGenericArguments[i]);
				}

				CheckUnspecializedGenericType(genericTypeInst, populateType);

				resolvedEntry->mValue = genericTypeInst;
				BF_ASSERT(BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) == resolvedEntry->mHash);
				InitType(genericTypeInst, populateType);
				return ResolveTypeResult(typeRef, genericTypeInst, populateType, resolveFlags);
			}
		}

		BfTypeInstance* typeInst;
		if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
		{
			auto typeAliasType = new BfTypeAliasType();
			typeInst = typeAliasType;
		}
		else
		{
			typeInst = new BfTypeInstance();
		}
		typeInst->mTypeDef = typeDef;

		if (typeInst->mTypeDef->mGenericParamDefs.size() != 0)
		{
			Fail("Generic type arguments expected", typeRef);
			delete typeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}
		resolvedEntry->mValue = typeInst;
		BF_ASSERT(BfResolvedTypeSet::Hash(typeInst, &lookupCtx) == resolvedEntry->mHash);
		InitType(typeInst, populateType);
		return ResolveTypeResult(typeRef, typeInst, populateType, resolveFlags);
	}	
	else if (auto arrayTypeRef = BfNodeDynCast<BfArrayTypeRef>(typeRef))
	{
		if (arrayTypeRef->mDimensions > 4)
		{
			Fail("Too many array dimensions, consider using a jagged array.", arrayTypeRef);
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		auto elementType = ResolveTypeRef(arrayTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if (elementType == NULL)
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		if ((arrayTypeRef->mDimensions == 1) && (arrayTypeRef->mParams.size() == 1))
		{
			intptr elementCount = -1;
			BfExpression* sizeExpr = BfNodeDynCast<BfExpression>(arrayTypeRef->mParams[0]);
			BF_ASSERT(sizeExpr != NULL);
			if (sizeExpr != NULL)
			{				
				BfConstResolver constResolver(this);
				BfType* intType = GetPrimitiveType(BfTypeCode_IntPtr);
				constResolver.mExpectingType = intType;
				constResolver.mAllowGenericConstValue = true;
				BfTypedValue typedVal;
				{
					SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
					typedVal = constResolver.Resolve(sizeExpr);
				}
				if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
				{
					BfUnknownSizedArrayType* arrayType = new BfUnknownSizedArrayType();
					arrayType->mContext = mContext;
					arrayType->mElementType = elementType;
					arrayType->mElementCount = -1;
					arrayType->mElementCountSource = typedVal.mType;
					resolvedEntry->mValue = arrayType;

					BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHash);
					InitType(arrayType, populateType);
					return ResolveTypeResult(typeRef, arrayType, populateType, resolveFlags);
				}

				if (typedVal)
					typedVal = Cast(sizeExpr, typedVal, intType);
				if (typedVal)
				{
					auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
					if (constant != NULL)
					{
						if (constant->mConstType == BfConstType_Undef)
							elementCount = -1; // Undef marker
						else if (BfIRBuilder::IsInt(constant->mTypeCode))
							elementCount = constant->mInt32;
					}
				}
			}
			/*if (elementCount < 0)
			{
				Fail(StrFormat("Array length '%d' is illegal", elementCount), arrayTypeRef->mParams[0]);
				mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
				return CreateSizedArrayType(elementType, 0);
			}*/

			BfSizedArrayType* arrayType = new BfSizedArrayType();
			arrayType->mContext = mContext;
			arrayType->mElementType = elementType;
			arrayType->mElementCount = elementCount;
			arrayType->mWantsGCMarking = false; // Fill in in InitType
			resolvedEntry->mValue = arrayType;

			BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHash);
			InitType(arrayType, populateType);
			return ResolveTypeResult(typeRef, arrayType, populateType, resolveFlags);
		}

		BfArrayType* arrayType = new BfArrayType();
		arrayType->mContext = mContext;
		arrayType->mDimensions = arrayTypeRef->mDimensions;
		arrayType->mTypeDef = mCompiler->GetArrayTypeDef(arrayType->mDimensions);
		arrayType->mTypeGenericArguments.push_back(elementType);
		resolvedEntry->mValue = arrayType;

		CheckUnspecializedGenericType(arrayType, populateType);

		BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHash);
		InitType(arrayType, populateType);
		return ResolveTypeResult(typeRef, arrayType, populateType, resolveFlags);
	}
	else if (auto genericTypeInstRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		int wantNumGenericParams = genericTypeInstRef->GetGenericArgCount();
		BfTypeDef* ambiguousTypeDef = NULL;

		BfTypeDef* typeDef = ResolveGenericInstanceDef(genericTypeInstRef);

		BfGenericTypeInstance* genericTypeInst;
		if ((typeDef != NULL) && (typeDef->mTypeCode == BfTypeCode_TypeAlias))
		{
			auto typeAliasType = new BfGenericTypeAliasType();
			genericTypeInst = typeAliasType;
		}
		else
			genericTypeInst = new BfGenericTypeInstance();
		genericTypeInst->mContext = mContext;

		if (ambiguousTypeDef != NULL)
			ShowAmbiguousTypeError(typeRef, typeDef, ambiguousTypeDef);
		if (typeDef == NULL)
		{
			Fail("Unable to resolve type", typeRef);
			delete genericTypeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Deleted);

		if (typeDef->mGenericParamDefs.size() == 0)
		{
			Fail("Not a generic type", typeRef);
			delete genericTypeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		int startDefGenericParamIdx = 0;
		genericTypeInst->mTypeDef = typeDef;

		if (mCurTypeInstance != NULL)
		{
			// Copy generic params for our parent type if the current type instance shares that parent type			
			//auto outerType = mSystem->GetOuterTypeNonPartial(typeDef);
			auto outerType = typeDef->mOuterType;
			BfTypeDef* commonOuterType = FindCommonOuterType(mCurTypeInstance->mTypeDef, outerType);
			if ((commonOuterType) && (mCurTypeInstance->IsGenericTypeInstance()))
			{
				startDefGenericParamIdx = (int)commonOuterType->mGenericParamDefs.size();
				auto parentTypeInstance = (BfGenericTypeInstance*)mCurTypeInstance;
				if (parentTypeInstance->IsTypeAlias())
					parentTypeInstance = (BfGenericTypeInstance*)GetOuterType(parentTypeInstance)->ToTypeInstance();
				for (int i = 0; i < startDefGenericParamIdx; i++)
				{
					genericTypeInst->mGenericParams.push_back(parentTypeInstance->mGenericParams[i]->AddRef());
					genericTypeInst->mTypeGenericArguments.push_back(parentTypeInstance->mTypeGenericArguments[i]);
					auto typeGenericArg = genericTypeInst->mTypeGenericArguments[i];
					genericTypeInst->mIsUnspecialized |= typeGenericArg->IsGenericParam() || typeGenericArg->IsUnspecializedType();
				}
			}
		}

		Array<BfTypeReference*> genericArguments;
		std::function<void(BfTypeReference*)> _GetTypeRefs = [&](BfTypeReference* typeRef)
		{
			if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeRef))
			{
				_GetTypeRefs(elementedTypeRef->mElementType);
			}
			else if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
			{
				_GetTypeRefs(qualifiedTypeRef->mLeft);
			}

			if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
			{
				for (auto genericArg : genericTypeRef->mGenericArguments)
					genericArguments.push_back(genericArg);
			}						   
		};
		_GetTypeRefs(genericTypeInstRef);

		int wantedGenericParams = (int)typeDef->mGenericParamDefs.size() - startDefGenericParamIdx;
		int genericArgDiffCount = (int)genericArguments.size() - wantedGenericParams;
		if (genericArgDiffCount != 0)
		{
			int innerWantedGenericParams = (int)typeDef->mGenericParamDefs.size();
			if (typeDef->mOuterType != NULL)
				innerWantedGenericParams -= (int)typeDef->mOuterType->mGenericParamDefs.size();
			ShowGenericArgCountError(genericTypeInstRef, innerWantedGenericParams);			
			delete genericTypeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		int genericParamIdx = 0;
		for (auto genericArgRef : genericArguments)
		{
			auto genericArg = ResolveTypeRef(genericArgRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericMethodParamConstValue);
			if (genericArg == NULL)
			{
				delete genericTypeInst;
				mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
				return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
			}

			genericTypeInst->mTypeGenericArguments.push_back(genericArg);
			genericTypeInst->mTypeGenericArgumentRefs.push_back(genericArgRef);
			genericParamIdx++;
		}

		resolvedEntry->mValue = genericTypeInst;

		CheckUnspecializedGenericType(genericTypeInst, populateType);

		BF_ASSERT(BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) == resolvedEntry->mHash);
		InitType(genericTypeInst, populateType);
		return ResolveTypeResult(typeRef, genericTypeInst, populateType, resolveFlags);
	}
	else if (auto tupleTypeRef = BfNodeDynCast<BfTupleTypeRef>(typeRef))
	{
		Array<BfType*> types;
		Array<String> names;

		for (int fieldIdx = 0; fieldIdx < (int)tupleTypeRef->mFieldTypes.size(); fieldIdx++)
		{
			BfTypeReference* typeRef = tupleTypeRef->mFieldTypes[fieldIdx];
			auto type = ResolveTypeRef(typeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
			if (type == NULL)
			{
				mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
				return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
			}

			String fieldName;
			BfIdentifierNode* identifierNode = NULL;
			if (fieldIdx < (int)tupleTypeRef->mFieldNames.size())
				identifierNode = tupleTypeRef->mFieldNames[fieldIdx];

			if (identifierNode != NULL)
				fieldName = identifierNode->ToString();
			else
				fieldName = StrFormat("%d", fieldIdx);

			String typeName = TypeToString(type);
			types.push_back(type);
			names.push_back(fieldName);
		}

		auto baseType = (BfTypeInstance*)ResolveTypeDef(mContext->mCompiler->mValueTypeTypeDef, BfPopulateType_Identity);
		BfTupleType* tupleType = new BfTupleType();
		//TODO: Add to correct project
		tupleType->Init(baseType->mTypeDef->mProject, baseType);

		tupleType->mFieldInstances.Resize(types.size());

		for (int fieldIdx = 0; fieldIdx < (int)types.size(); fieldIdx++)
		{
			BfFieldDef* fieldDef = tupleType->AddField(names[fieldIdx]);
			fieldDef->mProtection = (names[fieldIdx][0] == '_') ? BfProtection_Private : BfProtection_Public;

			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			fieldInstance->mFieldIdx = fieldIdx;
			fieldInstance->SetResolvedType(types[fieldIdx]);
			fieldInstance->mOwner = tupleType;
		}

		tupleType->Finish();

		resolvedEntry->mValue = tupleType;
		BF_ASSERT(BfResolvedTypeSet::Hash(tupleType, &lookupCtx) == resolvedEntry->mHash);
		InitType(tupleType, populateType);
		return ResolveTypeResult(typeRef, tupleType, populateType, resolveFlags);
	}
	else if (auto nullableTypeRef = BfNodeDynCast<BfNullableTypeRef>(typeRef))
	{
		BfTypeReference* elementTypeRef = nullableTypeRef->mElementType;
		auto typeDef = mCompiler->mNullableTypeDef;

		auto elementType = ResolveTypeRef(elementTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if (elementType == NULL)
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		BfGenericTypeInstance* genericTypeInst = new BfGenericTypeInstance();
		genericTypeInst->mContext = mContext;
		genericTypeInst->mTypeDef = typeDef;

		auto genericParamInstance = new BfGenericTypeParamInstance(typeDef, 0);
		genericTypeInst->mGenericParams.push_back(genericParamInstance);
		genericTypeInst->mTypeGenericArguments.push_back(elementType);
		//genericTypeInst->mIsUnspecialized = elementType->IsGenericParam() || elementType->IsUnspecializedType();

		CheckUnspecializedGenericType(genericTypeInst, populateType);

		resolvedEntry->mValue = genericTypeInst;
		BF_ASSERT(BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) == resolvedEntry->mHash);
		InitType(genericTypeInst, populateType);
		return ResolveTypeResult(typeRef, genericTypeInst, populateType, resolveFlags);
	}
	else if (auto pointerTypeRef = BfNodeDynCast<BfPointerTypeRef>(typeRef))
	{
		BfPointerType* pointerType = new BfPointerType();
		pointerType->mElementType = ResolveTypeRef(pointerTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		pointerType->mContext = mContext;
		if (pointerType->mElementType == NULL)
		{
			delete pointerType;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		resolvedEntry->mValue = pointerType;

		//int hashVal = mContext->mResolvedTypes.Hash(typeRef, &lookupCtx);
		BF_ASSERT(BfResolvedTypeSet::Hash(pointerType, &lookupCtx) == resolvedEntry->mHash);

		InitType(pointerType, populateType);
		return ResolveTypeResult(typeRef, pointerType, populateType, resolveFlags);
	}
	else if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(typeRef))
	{
		BfRefType* refType = new BfRefType();
		refType->mRefKind = BfRefType::RefKind_Ref;
		if (refTypeRef->mRefToken == NULL)
			refType->mRefKind = BfRefType::RefKind_Ref;
		else if (refTypeRef->mRefToken->GetToken() == BfToken_Out)
			refType->mRefKind = BfRefType::RefKind_Out;
		else if (refTypeRef->mRefToken->GetToken() == BfToken_Mut)
			refType->mRefKind = BfRefType::RefKind_Mut;
		refType->mElementType = ResolveTypeRef(refTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if (refType->mElementType == NULL)
		{
			delete refType;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		resolvedEntry->mValue = refType;
		BF_ASSERT(BfResolvedTypeSet::Hash(refType, &lookupCtx) == resolvedEntry->mHash);
		InitType(refType, populateType);
		return ResolveTypeResult(typeRef, refType, populateType, resolveFlags);
	}
	else if (auto delegateTypeRef = BfNodeDynCast<BfDelegateTypeRef>(typeRef))
	{		
		auto returnType = ResolveTypeRef(delegateTypeRef->mReturnType);
		if (returnType == NULL)
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		auto baseDelegateType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();

		BfDelegateType* delegateType = new BfDelegateType();		
		
		Val128 hashContext;

		BfTypeDef* typeDef = new BfTypeDef();
		typeDef->mProject = baseDelegateType->mTypeDef->mProject;
		typeDef->mSystem = mCompiler->mSystem;
		typeDef->mName = mSystem->mEmptyAtom;
		if (delegateTypeRef->mTypeToken->GetToken() == BfToken_Delegate)
		{
			typeDef->mIsDelegate = true;
			typeDef->mTypeCode = BfTypeCode_Object;
		}
		else
		{
			typeDef->mIsFunction = true;
			typeDef->mTypeCode = BfTypeCode_Struct;
		}

		BfMethodDef* methodDef = new BfMethodDef();
		methodDef->mDeclaringType = typeDef;
		methodDef->mName = "Invoke";
		methodDef->mProtection = BfProtection_Public;
		methodDef->mIdx = 0;
		methodDef->mIsStatic = !typeDef->mIsDelegate;

		auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateType->mDirectAllocNodes.push_back(directTypeRef);
		if (typeDef->mIsDelegate)
			directTypeRef->Init(delegateType);
		else
			directTypeRef->Init(ResolveTypeDef(mCompiler->mFunctionTypeDef));
		typeDef->mBaseTypes.push_back(directTypeRef);		

		directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateType->mDirectAllocNodes.push_back(directTypeRef);		
		directTypeRef->Init(returnType);
		methodDef->mReturnTypeRef = directTypeRef;
		delegateType->mReturnType = returnType;

		AddDependency(directTypeRef->mType, baseDelegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);

		auto hashVal = mContext->mResolvedTypes.Hash(typeRef, &lookupCtx);

		int paramIdx = 0;
		for (auto param : delegateTypeRef->mParams)
		{
			auto paramType = ResolveTypeRef(param->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowRef);
			if (paramType == NULL)
				paramType = GetPrimitiveType(BfTypeCode_Var);
			String paramName;
			if (param->mNameNode != NULL)
				paramName = param->mNameNode->ToString();

			if (paramType->IsUnspecializedType())
				delegateType->mIsUnspecializedType = true;
			if (paramType->IsUnspecializedTypeVariation())
				delegateType->mIsUnspecializedTypeVariation = true;
			if (!paramType->IsReified())
				delegateType->mIsReified = false;
			if (paramType->IsGenericParam())
			{
				delegateType->mIsUnspecializedTypeVariation = true;
			}

			auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
			delegateType->mDirectAllocNodes.push_back(directTypeRef);
			directTypeRef->Init(paramType);

			BfParameterDef* paramDef = new BfParameterDef();

			paramDef->mTypeRef = directTypeRef;
			paramDef->mName = paramName;
			methodDef->mParams.push_back(paramDef);
			paramIdx++;

			delegateType->mParams.Add(paramType);
			AddDependency(paramType, baseDelegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		}

		typeDef->mMethods.push_back(methodDef);

		//	
		
		BfDefBuilder::AddMethod(typeDef, BfMethodType_Ctor, BfProtection_Public, false, "");
		if (typeDef->mIsDelegate)		
			BfDefBuilder::AddDynamicCastMethods(typeDef);

		delegateType->mContext = mContext;
		delegateType->mTypeDef = typeDef;
		InitType(delegateType, populateType);
		resolvedEntry->mValue = delegateType;

// #ifdef _DEBUG
// 		if (BfResolvedTypeSet::Hash(delegateType, &lookupCtx) != resolvedEntry->mHash)
// 		{
// 			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
// 			int typeHash = BfResolvedTypeSet::Hash(delegateType, &lookupCtx);
// 			BF_ASSERT(refHash == typeHash);
// 		}
// #endif

		BF_ASSERT(BfResolvedTypeSet::Hash(delegateType, &lookupCtx) == resolvedEntry->mHash);
		return ResolveTypeResult(typeRef, delegateType, populateType, resolveFlags);
	}
	else if (auto genericParamTypeRef = BfNodeDynCast<BfGenericParamTypeRef>(typeRef))
	{
		auto genericParamType = GetGenericParamType(genericParamTypeRef->mGenericParamKind, genericParamTypeRef->mGenericParamIdx);
		resolvedEntry->mValue = genericParamType;
		BF_ASSERT(BfResolvedTypeSet::Hash(genericParamType, &lookupCtx) == resolvedEntry->mHash);
		return ResolveTypeResult(typeRef, genericParamType, populateType, resolveFlags);
	}
	else if (auto retTypeTypeRef = BfNodeDynCast<BfRetTypeTypeRef>(typeRef))
	{
		auto retTypeType = new BfRetTypeType();
		retTypeType->mElementType = ResolveTypeRef(retTypeTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		// We know this is a generic param type, it can't fail to resolve
		BF_ASSERT(retTypeType->mElementType);

		resolvedEntry->mValue = retTypeType;
		BF_ASSERT(BfResolvedTypeSet::Hash(retTypeType, &lookupCtx) == resolvedEntry->mHash);

		InitType(retTypeType, populateType);
		return ResolveTypeResult(typeRef, retTypeType, populateType, resolveFlags);
	}
	else if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		auto leftType = ResolveTypeRef(qualifiedTypeRef->mLeft, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if (leftType == NULL)
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		return ResolveTypeResult(typeRef, ResolveInnerType(leftType, qualifiedTypeRef->mRight), populateType, resolveFlags);
	}
	else if (auto constTypeRef = BfNodeDynCastExact<BfConstTypeRef>(typeRef))
	{
		return ResolveTypeRef(constTypeRef->mElementType, populateType, (BfResolveTypeRefFlags)(resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam));
	}
	else if (auto constExprTypeRef = BfNodeDynCastExact<BfConstExprTypeRef>(typeRef))
	{
		auto constExprType = new BfConstExprValueType();
		constExprType->mContext = mContext;

		BfVariant result;
		if (constExprTypeRef->mConstExpr != NULL)
		{
			BfType* constGenericParam = NULL;
			result = mContext->mResolvedTypes.EvaluateToVariant(&lookupCtx, constExprTypeRef->mConstExpr, constGenericParam);
			BF_ASSERT(constGenericParam == NULL);			
		}

		constExprType->mType = GetPrimitiveType(result.mTypeCode);
		constExprType->mValue = result;

		resolvedEntry->mValue = constExprType;
		BF_ASSERT(BfResolvedTypeSet::Hash(constExprType, &lookupCtx) == resolvedEntry->mHash);

		InitType(constExprType, populateType);
		return constExprType;
	}
	else
	{
		BF_FATAL("Not implemented!");
		NotImpl(typeRef);
		return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
	}

	return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
}

BfType* BfModule::ResolveTypeRefAllowUnboundGenerics(BfTypeReference* typeRef, BfPopulateType populateType, bool resolveGenericParam)
{
	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		if (genericTypeRef->mGenericArguments.size() == 0)
		{
			auto genericTypeDef = ResolveGenericInstanceDef(genericTypeRef);
			if (genericTypeDef == NULL)
				return NULL;

			BfTypeVector typeVector;
			for (int i = 0; i < (int)genericTypeDef->mGenericParamDefs.size(); i++)
				typeVector.push_back(GetGenericParamType(BfGenericParamKind_Type, i));
			return ResolveTypeDef(genericTypeDef, typeVector);
		}
	}

	return ResolveTypeRef(typeRef, populateType, resolveGenericParam ? (BfResolveTypeRefFlags)0 : BfResolveTypeRefFlag_NoResolveGenericParam);
}

// This finds non-default unspecialized generic type instances and converts them into a BfUnspecializedGenericTypeVariation
BfType* BfModule::CheckUnspecializedGenericType(BfGenericTypeInstance* genericTypeInst, BfPopulateType populateType)
{
	int argCount = (int)genericTypeInst->mTypeGenericArguments.size();

	bool isDefaultUnspecialized = true;
	for (int argIdx = 0; argIdx < argCount; argIdx++)
	{
		auto argType = genericTypeInst->mTypeGenericArguments[argIdx];
		if (argType->IsGenericParam())
		{
			auto genericParamType = (BfGenericParamType*)argType;
			if ((genericParamType->mGenericParamKind != BfGenericParamKind_Type) || (genericParamType->mGenericParamIdx != argIdx))
				isDefaultUnspecialized = false;
			genericTypeInst->mIsUnspecialized = true;
		}
		else if (argType->IsUnspecializedType())
		{
			isDefaultUnspecialized = false;
			genericTypeInst->mIsUnspecialized = true;
		}
		else
			isDefaultUnspecialized = false;
	}

	if (genericTypeInst->mIsUnspecialized)
		genericTypeInst->mIsUnspecializedVariation = !isDefaultUnspecialized;
	return genericTypeInst;
}

BfTypeInstance* BfModule::GetUnspecializedTypeInstance(BfTypeInstance* typeInst)
{
	if (!typeInst->IsGenericTypeInstance())
		return typeInst;

	auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
	auto result = ResolveTypeDef(genericTypeInst->mTypeDef, BfPopulateType_Declaration);
	BF_ASSERT((result != NULL) && (result->IsUnspecializedType()));
	if (result == NULL)
		return NULL;
	return result->ToTypeInstance();
}

BfType* BfModule::ResolveInnerType(BfType* outerType, BfIdentifierNode* identifier, BfPopulateType populateType, bool ignoreErrors)
{
	BfDirectStrTypeReference typeRef;
	typeRef.Init(identifier->ToString());	

	auto type = ResolveInnerType(outerType, &typeRef, populateType, ignoreErrors);
	return type;
}

BfType* BfModule::ResolveTypeRef(BfAstNode* astNode, const BfSizedArray<BfTypeReference*>* genericArgs, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	if ((genericArgs == NULL) || (genericArgs->size() == 0))
	{
		if (auto identifier = BfNodeDynCast<BfIdentifierNode>(astNode))
		{			
			BfNamedTypeReference typeRef;
			typeRef.mNameNode = identifier;
			typeRef.mSrcEnd = 0;
			typeRef.mToken = BfToken_None;
			auto type = ResolveTypeRef(&typeRef, populateType, resolveFlags);
			return type;
		}
	}
	
	BfAstAllocator alloc;
	alloc.mSourceData = astNode->GetSourceData();

	std::function<BfTypeReference*(BfAstNode*)> _ConvType = [&] (BfAstNode* astNode) -> BfTypeReference*
	{	
		if (auto typeRef = BfNodeDynCast<BfTypeReference>(astNode))
			return typeRef;

		BfTypeReference* result = NULL;
		if (auto identifier = BfNodeDynCast<BfIdentifierNode>(astNode))
		{
			auto* typeRef = alloc.Alloc<BfNamedTypeReference>();
			typeRef->mNameNode = identifier;			
			result = typeRef;
		}		
		else if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(astNode))
		{
			auto qualifiedTypeRef = alloc.Alloc<BfQualifiedTypeReference>();
			qualifiedTypeRef->mLeft = _ConvType(memberRefExpr->mTarget);
			qualifiedTypeRef->mDot = memberRefExpr->mDotToken;
			qualifiedTypeRef->mRight = _ConvType(memberRefExpr->mMemberName);

			if ((qualifiedTypeRef->mLeft == NULL) || (qualifiedTypeRef->mRight == NULL))
				return NULL;
			result = qualifiedTypeRef;
		}

		if (result == NULL)
			return NULL;
		result->SetSrcStart(astNode->GetSrcStart());
		result->SetSrcEnd(astNode->GetSrcEnd());

		return result;
	};

	auto typeRef = _ConvType(astNode);
	if (typeRef == NULL)
		return NULL;

	if ((genericArgs != NULL) && (genericArgs->size() != 0))
	{
		auto genericInstanceTypeRef = alloc.Alloc<BfGenericInstanceTypeRef>();
		genericInstanceTypeRef->SetSrcStart(typeRef->GetSrcStart());
		genericInstanceTypeRef->mElementType = typeRef;
#ifdef BF_AST_HAS_PARENT_MEMBER
		typeRef->mParent = genericInstanceTypeRef;
#endif

		BfDeferredAstSizedArray<BfTypeReference*> arguments(genericInstanceTypeRef->mGenericArguments, &alloc);

		for (auto genericArg : *genericArgs)
		{	
			if (genericArg != NULL)
			{
				arguments.push_back(genericArg);
				genericInstanceTypeRef->SetSrcEnd(genericArg->GetSrcEnd());
			}
		}

		typeRef = genericInstanceTypeRef;
	}

	return ResolveTypeRef(typeRef, populateType, resolveFlags);
}

// This flow should mirror CastToValue
bool BfModule::CanCast(BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags)
{
	BP_ZONE("BfModule::CanCast");

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
	return CastToValue(NULL, typedVal, toType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));			
}

bool BfModule::AreSplatsCompatible(BfType* fromType, BfType* toType, bool* outNeedsMemberCasting)
{
	if ((fromType->IsTypeInstance()) && (!fromType->IsSplattable()))
		return false;
	if ((toType->IsTypeInstance()) && (!toType->IsSplattable()))
		return false;

	auto _GetTypes = [&](BfType* type, Array<BfType*>& types)
	{		
		BfTypeUtils::SplatIterate([&](BfType* memberType) { types.Add(memberType);  }, type);
	};

	Array<BfType*> fromTypes;
	_GetTypes(fromType, fromTypes);
	Array<BfType*> toTypes;
	_GetTypes(toType, toTypes);

	if (toTypes.size() > fromTypes.size())
		return false;

	for (int i = 0; i < toTypes.size(); i++)
	{
		BfType* fromMemberType = fromTypes[i];
		BfType* toMemberType = toTypes[i];

		if (fromMemberType != toMemberType)
		{
			if ((outNeedsMemberCasting != NULL) &&				
				(fromMemberType->IsIntPtrable()) && (toMemberType->IsIntPtrable()))
				*outNeedsMemberCasting = true;
			else
				return false;
		}
	}
		
	return true;
}

BfIRValue BfModule::CastToFunction(BfAstNode* srcNode, BfMethodInstance* methodInstance, BfType* toType, BfCastFlags castFlags)
{	
	auto invokeMethodInstance = GetDelegateInvokeMethod(toType->ToTypeInstance());

	if (invokeMethodInstance->IsExactMatch(methodInstance, false, true))
	{
		BfModuleMethodInstance methodRefMethod;
		if (methodInstance->mDeclModule == this)
			methodRefMethod = methodInstance;
		else
			methodRefMethod = ReferenceExternalMethodInstance(methodInstance);
		auto dataType = GetPrimitiveType(BfTypeCode_IntPtr);
		if (!methodRefMethod.mFunc)
		{
			if (HasCompiledOutput())
				AssertErrorState();
			return GetDefaultValue(dataType);
		}
		auto bindFuncVal = methodRefMethod.mFunc;
		if (mCompiler->mOptions.mAllowHotSwapping)
			bindFuncVal = mBfIRBuilder->RemapBindFunction(bindFuncVal);
		return mBfIRBuilder->CreatePtrToInt(bindFuncVal, BfTypeCode_IntPtr);
	}

	if ((castFlags & BfCastFlags_SilentFail) == 0)
	{
		if (invokeMethodInstance->IsExactMatch(methodInstance, true, true))
		{
			Fail(StrFormat("Non-static method '%s' cannot match '%s' because it contains captured variables, consider using a delegate or removing captures", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str()), srcNode);
		}
		else if (invokeMethodInstance->IsExactMatch(methodInstance, false, false))
		{
			Fail(StrFormat("Non-static method '%s' cannot match '%s', consider adding '%s this' to the function parameters", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str(), TypeToString(methodInstance->GetParamType(-1)).c_str()), srcNode);
		}
	}
	return BfIRValue();
}

BfIRValue BfModule::CastToValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags, BfCastResultFlags* resultFlags)
{
	bool explicitCast = (castFlags & BfCastFlags_Explicit) != 0;
	bool ignoreErrors = mIgnoreErrors || ((castFlags & BfCastFlags_SilentFail) != 0);	
	bool ignoreWrites = mBfIRBuilder->mIgnoreWrites;

	if (typedVal.mType == toType)
	{
		if (resultFlags != NULL)
		{
			if (typedVal.IsAddr())
				*resultFlags = (BfCastResultFlags)(*resultFlags | BfCastResultFlags_IsAddr);
			if (typedVal.mKind == BfTypedValueKind_TempAddr)
				*resultFlags = (BfCastResultFlags)(*resultFlags | BfCastResultFlags_IsTemp);
		}
		else if (typedVal.IsAddr())
			typedVal = LoadValue(typedVal);

		return typedVal.mValue;
	}

	BF_ASSERT(typedVal.mType->mContext == mContext);
	BF_ASSERT(toType->mContext == mContext);

	if ((typedVal.IsAddr()) && (!typedVal.mType->IsValueType()))
		typedVal = LoadValue(typedVal);

	//BF_ASSERT(!typedVal.IsAddr() || typedVal.mType->IsGenericParam() || typedVal.mType->IsValueType());

	// Ref X to Ref Y, X* to Y*
	{
		bool checkUnderlying = false;
		
		if (((typedVal.mType->IsRef()) && (toType->IsRef())))
		{
			auto fromRefType = (BfRefType*)typedVal.mType;
			auto toRefType = (BfRefType*)toType;
			if (fromRefType->mRefKind == toRefType->mRefKind)
				checkUnderlying = true;
			else if ((fromRefType->mRefKind == BfRefType::RefKind_Ref) && (toRefType->mRefKind == BfRefType::RefKind_Mut))
				checkUnderlying = true; // Allow a ref-to-mut implicit conversion
		}

		if ((typedVal.mType->IsPointer()) && (toType->IsPointer()))
			checkUnderlying = true;

		if (checkUnderlying)
		{
			auto fromInner = typedVal.mType->GetUnderlyingType();
			auto toInner = toType->GetUnderlyingType();
			
			if (fromInner == toInner)
			{				
				return typedVal.mValue;
			}

			// ref int <-> ref int64/int32 (of same size)
			if (((fromInner->IsInteger()) && (toInner->IsInteger())) &&
				(fromInner->mSize == toInner->mSize) &&
				(fromInner->IsSigned() == toInner->IsSigned()))
				return typedVal.mValue;
		}
	}

	// Null -> ObjectInst|IFace|ptr
	if ((typedVal.mType->IsNull()) &&
		((toType->IsObjectOrInterface()) || (toType->IsPointer() || (toType->IsFunction()))))
	{
		return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
	}

	if (explicitCast)
	{
		// Object -> void*
		if ((typedVal.mType->IsObject()) && (toType->IsVoidPtr()))
		{
			return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}

		// Func -> void*
		if ((typedVal.mType->IsFunction()) && (toType->IsVoidPtr()))
		{
			return mBfIRBuilder->CreateIntToPtr(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}

		// void* -> Func
		if ((typedVal.mType->IsVoidPtr()) && (toType->IsFunction()))
		{
			return mBfIRBuilder->CreatePtrToInt(typedVal.mValue, BfTypeCode_IntPtr);
		}

		// * -> Valueless
		if (toType->IsVoid())
			return mBfIRBuilder->GetFakeVal();

		// void* -> intptr
		if ((typedVal.mType->IsPointer()) && (toType->IsIntPtr()))
		{				
			if ((!ignoreErrors) && (!typedVal.mType->GetUnderlyingType()->IsVoid()) && ((castFlags & BfCastFlags_FromCompiler) == 0))
			{
				Fail(StrFormat("Unable to cast directly from '%s' to '%s', consider casting to void* first", TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
			}
						
			auto toPrimitive = (BfPrimitiveType*)toType;
			return mBfIRBuilder->CreatePtrToInt(typedVal.mValue, toPrimitive->mTypeDef->mTypeCode);
		}

		// intptr -> void*
		if ((typedVal.mType->IsIntPtr()) && (toType->IsPointer()))
		{			
			if ((!ignoreErrors) && (!toType->GetUnderlyingType()->IsVoid()) && ((castFlags & BfCastFlags_FromCompiler) == 0))
			{
				Fail(StrFormat("Unable to cast directly from '%s' to '%s', consider casting to void* first", TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
			}

			return mBfIRBuilder->CreateIntToPtr(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}
	}

	// * <-> Var 
	if ((typedVal.mType->IsVar()) || (toType->IsVar()))
	{
		return GetDefaultValue(toType);
	}

	// Generic param -> *
	if ((typedVal.mType->IsGenericParam()) && (!toType->IsGenericParam()))
	{
		if (toType == mContext->mBfObjectType)
		{			
			// Always allow casting from generic to object
			return typedVal.mValue;
		}

		auto _CheckGenericParamInstance = [&](BfGenericParamInstance* genericParamInst)
		{
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				return typedVal.mValue;
			}
			if (toType->IsInterface())
			{
				for (auto iface : genericParamInst->mInterfaceConstraints)
					if (TypeIsSubTypeOf(iface, toType->ToTypeInstance()))
						return GetDefaultValue(toType);
			}

			if (genericParamInst->mTypeConstraint != NULL)
			{
				auto constraintTypeInst = genericParamInst->mTypeConstraint->ToTypeInstance();
				if ((constraintTypeInst != NULL) && (constraintTypeInst->mTypeDef == mCompiler->mEnumTypeDef))
				{
					// Enum->int
					if (toType->IsInteger())
						return GetDefaultValue(toType);
				}

				auto defaultFromValue = GetDefaultTypedValue(genericParamInst->mTypeConstraint);
				auto result = CastToValue(srcNode, defaultFromValue, toType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));

				if (result)
				{
					if ((genericParamInst->mTypeConstraint->IsDelegate()) && (toType->IsDelegate()))
					{
						// Don't allow cast when we are constrained by a delegate type, because BfMethodRefs can match and we require an actual alloc
						Fail(StrFormat("Unable to cast '%s' to '%s' because delegate constraints allow valueless direct method references", TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
						return BfIRValue();
					}
					return result;
				}
			}

			// Generic constrained with class or pointer type -> void*
			if (toType->IsVoidPtr())
			{
				if ((genericParamInst->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_StructPtr)) ||
					((genericParamInst->mTypeConstraint != NULL) &&
					((genericParamInst->mTypeConstraint->IsPointer()) || (genericParamInst->mTypeConstraint->IsObjectOrInterface()))))
				{
					return GetDefaultValue(toType);
				}
			}

			return BfIRValue();
		};

		BfIRValue retVal;

		// For these casts, it's just important we get *A* value to work with here, 
		//  as this is just use for unspecialized parsing.  We don't use the generated code
		{
			auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)typedVal.mType);
			retVal = _CheckGenericParamInstance(genericParamInst);
			if (retVal)
				return retVal;
		}

		// Check method generic constraints
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecialized) && (mCurMethodInstance->mMethodInfoEx != NULL))
		{
			for (int genericParamIdx = (int)mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
				genericParamIdx < mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParamInst = mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
				if (genericParamInst->mExternType == typedVal.mType)
				{
					retVal = _CheckGenericParamInstance(genericParamInst);
					if (retVal)
						return retVal;
				}
			}
		}
	}

	// * -> Generic param 
	if (toType->IsGenericParam())
	{
		if (explicitCast)
		{
			// Either an upcast or an unbox
			if ((typedVal.mType == mContext->mBfObjectType) || (typedVal.mType->IsInterface()))
			{
				return GetDefaultValue(toType);
			}
		}

		auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)toType);
		if (genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var)
			return GetDefaultValue(toType);
		if (typedVal.mType->IsNull())
		{
			bool allowCast = (genericParamInst->mGenericParamFlags & BfGenericParamFlag_Class) || (genericParamInst->mGenericParamFlags & BfGenericParamFlag_StructPtr);
			if ((!allowCast) && (genericParamInst->mTypeConstraint != NULL))
				allowCast = genericParamInst->mTypeConstraint->IsObject() || genericParamInst->mTypeConstraint->IsPointer();
			if (allowCast)
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}

		if (genericParamInst->mTypeConstraint != NULL)
		{			
			auto castedVal = CastToValue(srcNode, typedVal, genericParamInst->mTypeConstraint, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));
			if (castedVal)
				return castedVal;

			//TODO: WHy did we do 'GetDefaultValue'? This messes up setting up method param defaults, which is important for inferring const generic params
				//return GetDefaultValue(toType);
		}
	}

	if ((typedVal.mType->IsTypeInstance()) && (toType->IsTypeInstance()))
	{
		auto fromTypeInstance = typedVal.mType->ToTypeInstance();
		auto toTypeInstance = toType->ToTypeInstance();

		if ((typedVal.mType->IsValueType()) && (toType->IsValueType()))
		{
			bool allowCast = false;
			if (TypeIsSubTypeOf(fromTypeInstance, toTypeInstance))
				allowCast = true;
			if (allowCast)
			{
				if (toType->IsValuelessType())
					return BfIRValue::sValueless;
				if (ignoreWrites)
					return mBfIRBuilder->GetFakeVal();
								
				if (resultFlags != NULL)
					*resultFlags = (BfCastResultFlags)(BfCastResultFlags_IsAddr);
				typedVal = MakeAddressable(typedVal);
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapTypeInstPtr(toTypeInstance));				
			}
		}

		// ObjectInst|IFace -> object|IFace
		if ((typedVal.mType->IsObject() || (typedVal.mType->IsInterface())) && ((toType->IsObject() || (toType->IsInterface()))))
		{
			bool allowCast = false;			

			if (TypeIsSubTypeOf(fromTypeInstance, toTypeInstance))
				allowCast = true;
			else if ((explicitCast) &&
				((toType->IsInterface()) || (TypeIsSubTypeOf(toTypeInstance, fromTypeInstance))))
			{
				if (toType->IsObjectOrInterface())
				{
					if ((castFlags & BfCastFlags_Unchecked) == 0)
						EmitDynamicCastCheck(typedVal, toType, true);
				}
				allowCast = true;
			}

			if (allowCast)
			{
				if (ignoreWrites)
					return mBfIRBuilder->GetFakeVal();
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
			}
		}
	}	

	// MethodRef -> Function
	if ((typedVal.mType->IsMethodRef()) && (toType->IsFunction()))
	{
		BfMethodInstance* methodInstance = ((BfMethodRefType*)typedVal.mType)->mMethodRef;
		auto result = CastToFunction(srcNode, methodInstance, toType, castFlags);
		if (result)
			return result;
	}

	// concrete IFace -> object|IFace
	if ((typedVal.mType->IsConcreteInterfaceType()) && ((toType->IsObject() || (toType->IsInterface()))))
	{
		auto concreteInterfaceType = (BfConcreteInterfaceType*)typedVal.mType;
		if ((toType->IsObject()) || (concreteInterfaceType->mInterface == toType))
			return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
	}

	// IFace -> object
	if ((typedVal.mType->IsInterface()) && (toType == mContext->mBfObjectType))
		return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));

	// * -> Pointer
	if (toType->IsPointer())
	{
		// Ptr -> Ptr
		if (typedVal.mType->IsPointer())
		{
			bool allowCast = explicitCast;
			auto fromPointerType = (BfPointerType*)typedVal.mType;
			auto toPointerType = (BfPointerType*)toType;

			auto fromUnderlying = fromPointerType->mElementType;
			auto toUnderlying = toPointerType->mElementType;

			// Allow cast from T[size]* to T* implicitly
			//  And from T* to T[size]* explicitly
			while (fromUnderlying->IsSizedArray())
				fromUnderlying = fromUnderlying->GetUnderlyingType();
			while ((toUnderlying->IsSizedArray()) && (explicitCast))
				toUnderlying = toUnderlying->GetUnderlyingType();

			if ((fromUnderlying == toUnderlying) ||
				(TypeIsSubTypeOf(fromUnderlying->ToTypeInstance(), toUnderlying->ToTypeInstance())) ||
				(toUnderlying->IsVoid()))
				allowCast = true;

			if (allowCast)
			{
				if (ignoreWrites)
					return mBfIRBuilder->GetFakeVal();
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
			}
		}
		else if (typedVal.mType->IsObject())
		{
			// ???
		}
		/*else if (typedVal.mType->IsSizedArray())
		{
			if (typedVal.IsAddr())
			{
				BfSizedArrayType* arrayType = (BfSizedArrayType*)typedVal.mType;
				auto ptrType = CreatePointerType(arrayType->mElementType);
				BfTypedValue returnPointer(mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(ptrType)), ptrType);
				return CastToValue(srcNode, returnPointer, toType, castFlags, silentFail);
			}
		}*/
	}

	// Boxing?
	bool mayBeBox = false;
	if (((typedVal.mType->IsValueType()) || (typedVal.mType->IsPointer()) || (typedVal.mType->IsValuelessType())) && 
		((toType->IsInterface()) || (toType == mContext->mBfObjectType)))
	{
		// Make sure there's no conversion operator before we box
		if ((!typedVal.mType->IsRef()) && (!typedVal.mType->IsRetTypeType()))
			mayBeBox = true;
	}

	//TODO: the IsGenericParam is not valid - why did we have that?  The generic param could be a struct for example...
	if ((explicitCast) && ((typedVal.mType->IsInterface()) || (typedVal.mType == mContext->mBfObjectType) /*|| (typedVal.mType->IsGenericParam())*/) && 
		((toType->IsValueType()) || (toType->IsPointer())))
	{
		if (toType->IsValuelessType())
			return BfIRValue::sValueless;		

		if (ignoreWrites)		
			return mBfIRBuilder->GetFakeVal();		

		// Unbox!
		if ((castFlags & BfCastFlags_Unchecked) == 0)
		{
			EmitDynamicCastCheck(typedVal, toType, false);
			EmitObjectAccessCheck(typedVal);
		}

		if (toType->IsNullable())
		{
			auto toTypeInst = toType->ToTypeInstance();
			int valueIdx = toTypeInst->mFieldInstances[0].mDataIdx;
			int hasValueIdx = toTypeInst->mFieldInstances[1].mDataIdx;

			typedVal = MakeAddressable(typedVal);

			auto elementType = toType->GetUnderlyingType();
			auto ptrElementType = CreatePointerType(elementType);
			auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

			auto allocaInst = CreateAlloca(toType, true, "unboxN");

			auto prevBB = mBfIRBuilder->GetInsertBlock();
			auto nullBB = mBfIRBuilder->CreateBlock("unboxN.null");
			auto notNullBB = mBfIRBuilder->CreateBlock("unboxN.notNull");
			auto endBB = mBfIRBuilder->CreateBlock("unboxN.end");

			auto isNull = mBfIRBuilder->CreateIsNull(typedVal.mValue);

			mBfIRBuilder->CreateCondBr(isNull, nullBB, notNullBB);

			int dataIdx = toTypeInst->mFieldInstances[1].mDataIdx;

			mBfIRBuilder->AddBlock(nullBB);
			mBfIRBuilder->SetInsertPoint(nullBB);
			auto hasValueAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, hasValueIdx); // has_value
			mBfIRBuilder->CreateStore(GetConstValue(0, boolType), hasValueAddr);
			auto nullableValueAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, valueIdx); // value
			auto nullableValueBits = mBfIRBuilder->CreateBitCast(nullableValueAddr, mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
			mBfIRBuilder->CreateMemSet(nullableValueBits, GetConstValue(0, GetPrimitiveType(BfTypeCode_Int8)), GetConstValue(elementType->mSize), elementType->mAlign);
			mBfIRBuilder->CreateBr(endBB);

			mBfIRBuilder->AddBlock(notNullBB);
			mBfIRBuilder->SetInsertPoint(notNullBB);
			hasValueAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, hasValueIdx); // has_value
			mBfIRBuilder->CreateStore(GetConstValue(1, boolType), hasValueAddr);
			nullableValueAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, valueIdx); // value
			auto srcObjBits = mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(ptrElementType));
			auto boxedValueAddr = mBfIRBuilder->CreateInBoundsGEP(srcObjBits, 1); // Skip over vdata
			auto boxedValue = mBfIRBuilder->CreateLoad(boxedValueAddr);
			mBfIRBuilder->CreateStore(boxedValue, nullableValueAddr);
			mBfIRBuilder->CreateBr(endBB);

			mBfIRBuilder->AddBlock(endBB);
			mBfIRBuilder->SetInsertPoint(endBB);

			if (resultFlags != NULL)
				*resultFlags = (BfCastResultFlags)(BfCastResultFlags_IsAddr | BfCastResultFlags_IsTemp);
			return allocaInst;
		}
		
		auto boxedType = CreateBoxedType(toType);
		mBfIRBuilder->PopulateType(boxedType);
		AddDependency(boxedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
		auto boxedObj = mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(boxedType));
		auto valPtr = mBfIRBuilder->CreateInBoundsGEP(boxedObj, 0, 1);
		if ((toType->IsPrimitiveType()) || (toType->IsTypedPrimitive()) || (toType->IsPointer()) || (toType->IsSizedArray()) || (toType->IsMethodRef()))
		{
			valPtr = mBfIRBuilder->CreateBitCast(valPtr, mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(toType)));
		}
		if ((toType->IsComposite()) && (resultFlags != NULL))
		{			
			*resultFlags = BfCastResultFlags_IsAddr;
			return valPtr;
		}
		else
			return mBfIRBuilder->CreateLoad(valPtr, false);
	}
		
	// Null -> Nullable<T>
	if ((typedVal.mType->IsNull()) && (toType->IsNullable()))
	{
		if (ignoreWrites)
			return mBfIRBuilder->GetFakeVal();

		if ((castFlags & BfCastFlags_PreferAddr) != 0)
		{
			auto boolType = GetPrimitiveType(BfTypeCode_Boolean);		
 			auto toTypeInst = toType->ToTypeInstance();		
 			int hasValueIdx = toTypeInst->mFieldInstances[1].mDataIdx;
 		
 			auto allocaInst = CreateAlloca(toType);
 			auto hasValueAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, hasValueIdx); // has_value
 			mBfIRBuilder->CreateStore(GetConstValue(0, boolType), hasValueAddr);		
 
 			auto typedValue = BfTypedValue(allocaInst, toType, true);		
 			if (resultFlags != NULL)
 				*resultFlags = (BfCastResultFlags)(BfCastResultFlags_IsAddr | BfCastResultFlags_IsTemp);
 			return allocaInst;
		}

		auto zeroNullable = mBfIRBuilder->CreateConstStructZero(mBfIRBuilder->MapType(toType));
		return zeroNullable;
	}

	// Nullable<A> -> Nullable<B>
	if ((typedVal.mType->IsNullable()) && (toType->IsNullable()))
	{
		if (ignoreWrites)		
			return mBfIRBuilder->GetFakeVal();

		auto fromNullableType = (BfGenericTypeInstance*)typedVal.mType;
		auto toNullableType = (BfGenericTypeInstance*)toType;

		BfIRValue srcPtr = typedVal.mValue;
		if (!typedVal.IsAddr())
		{
			auto srcAlloca = CreateAllocaInst(fromNullableType);
			mBfIRBuilder->CreateStore(typedVal.mValue, srcAlloca);
			srcPtr = srcAlloca;
		}

		auto srcAddr = mBfIRBuilder->CreateInBoundsGEP(srcPtr, 0, 1); // mValue
		auto srcVal = mBfIRBuilder->CreateLoad(srcAddr);

		auto toVal = CastToValue(srcNode, BfTypedValue(srcVal, fromNullableType->mTypeGenericArguments[0]), toNullableType->mTypeGenericArguments[0], ignoreErrors ? BfCastFlags_SilentFail : BfCastFlags_None);
		if (!toVal)
			return BfIRValue();

		auto allocaInst = CreateAllocaInst(toNullableType);

		auto destAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1); // mValue
		mBfIRBuilder->CreateStore(toVal, destAddr);

		srcAddr = mBfIRBuilder->CreateInBoundsGEP(srcPtr, 0, 2); // mHasValue
		srcVal = mBfIRBuilder->CreateLoad(srcAddr);
		destAddr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 2); // mHasValue
		mBfIRBuilder->CreateStore(srcVal, destAddr);

		if (resultFlags != NULL)
			*resultFlags = (BfCastResultFlags)(BfCastResultFlags_IsAddr | BfCastResultFlags_IsTemp);
		return allocaInst;
	}

	// Tuple -> Tuple
	if ((typedVal.mType->IsTuple()) && (toType->IsTuple()))
	{
		auto fromTupleType = (BfTupleType*)typedVal.mType;
		auto toTupleType = (BfTupleType*)toType;
		if (fromTupleType->mFieldInstances.size() == toTupleType->mFieldInstances.size())
		{
			typedVal = LoadValue(typedVal);

			BfIRValue curTupleValue = mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(toTupleType));
			for (int valueIdx = 0; valueIdx < (int)fromTupleType->mFieldInstances.size(); valueIdx++)
			{
				BfFieldInstance* fromFieldInstance = &fromTupleType->mFieldInstances[valueIdx];
				BfFieldInstance* toFieldInstance = &toTupleType->mFieldInstances[valueIdx];

				if (!explicitCast)
				{
					BfFieldDef* fromFieldDef = fromFieldInstance->GetFieldDef();
					BfFieldDef* toFieldDef = toFieldInstance->GetFieldDef();

					// Either the names have to match or one has to be unnamed
					if ((!fromFieldDef->IsUnnamedTupleField()) && (!toFieldDef->IsUnnamedTupleField()) &&
						(fromFieldDef->mName != toFieldDef->mName))
					{
						curTupleValue = BfIRValue();
						break;
					}
				}

				auto fromFieldType = fromFieldInstance->GetResolvedType();
				auto toFieldType = toFieldInstance->GetResolvedType();

				if (toFieldType->IsVoid())
					continue; // Allow sinking to void

				BfIRValue fromFieldValue;
				if (fromFieldInstance->mDataIdx >= 0)
					fromFieldValue = mBfIRBuilder->CreateExtractValue(typedVal.mValue, fromFieldInstance->mDataIdx);
				BfIRValue toFieldValue = CastToValue(srcNode, BfTypedValue(fromFieldValue, fromFieldType), toFieldType, (BfCastFlags)(castFlags | BfCastFlags_Explicit));
				if (!toFieldValue)
				{
					curTupleValue = BfIRValue();
					break;
				}

				if (toFieldInstance->mDataIdx >= 0)
					curTupleValue = mBfIRBuilder->CreateInsertValue(curTupleValue, toFieldValue, toFieldInstance->mDataIdx);
			}

			if (curTupleValue)
				return curTupleValue;
		}
	}

	// -> const <value>
	if (toType->IsConstExprValue())
	{
		auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
		if (constant != NULL)
		{
			BfConstExprValueType* toConstExprValueType = (BfConstExprValueType*)toType;
			
			auto variantVal = TypedValueToVariant(srcNode, typedVal);
			if ((mBfIRBuilder->IsInt(variantVal.mTypeCode)) && (mBfIRBuilder->IsInt(toConstExprValueType->mValue.mTypeCode)))
			{
				if (variantVal.mInt64 == toConstExprValueType->mValue.mInt64)
					return typedVal.mValue;
			}
			else if ((mBfIRBuilder->IsFloat(variantVal.mTypeCode)) && (mBfIRBuilder->IsFloat(toConstExprValueType->mValue.mTypeCode)))
			{
				if (variantVal.ToDouble() == toConstExprValueType->mValue.ToDouble())
					return typedVal.mValue;
			}

			if (!ignoreErrors)
			{	
				String valStr;
				VariantToString(valStr, variantVal);
				Fail(StrFormat("Unable to cast '%s %s' to '%s'", TypeToString(typedVal.mType).c_str(), valStr.c_str(), TypeToString(toType).c_str()), srcNode);
			}
		}
	}

	if ((typedVal.mType->IsPrimitiveType()) && (toType->IsPrimitiveType()))
	{
		auto fromPrimType = (BfPrimitiveType*)typedVal.mType;
		auto toPrimType = (BfPrimitiveType*)toType;

		BfTypeCode fromTypeCode = fromPrimType->mTypeDef->mTypeCode;
		BfTypeCode toTypeCode = toPrimType->mTypeDef->mTypeCode;

		if (toType->IsIntegral())
		{
			// Allow constant ints to be implicitly casted to a smaller type if they fit
			auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
			if (constant != NULL)
			{
				if (mBfIRBuilder->IsInt(constant->mTypeCode))
				{
					int64 srcVal = constant->mInt64;

					if (toPrimType->IsChar())
					{
						if (srcVal == 0)
							explicitCast = true;
					}
					else if ((fromPrimType->IsChar()) && (!toPrimType->IsChar()))
					{
						// Never allow this
					}
					else if ((constant->mTypeCode == BfTypeCode_UInt64) && (srcVal < 0))
					{
						// There's nothing that this could fit into
					}
					else if (toType->IsSigned())
					{
                        if (toType->mSize == 8) // int64
                            explicitCast = true;
                        else
                        {
                            int64 minVal = -(1LL << (8 * toType->mSize - 1));
                            int64 maxVal = (1LL << (8 * toType->mSize - 1)) - 1;
                            if ((srcVal >= minVal) && (srcVal <= maxVal))
                                explicitCast = true;
                        }
					}
					else if (toType->mSize == 8) // uint64
					{
						if (srcVal >= 0)
							explicitCast = true;
					}
					else
					{
						int64 minVal = 0;
						int64 maxVal = (1LL << (8 * toType->mSize)) - 1;
						if ((srcVal >= minVal) && (srcVal <= maxVal))
							explicitCast = true;
					}
				}
				else if (constant->mConstType == BfConstType_Undef)
				{
					BF_ASSERT(mBfIRBuilder->mIgnoreWrites);

					auto undefConst = (BfConstantUndef*)constant;
					
					auto fakeVal = GetFakeTypedValue(GetPrimitiveType(undefConst->mTypeCode));
					auto val = CastToValue(srcNode, fakeVal, toType, (BfCastFlags)(castFlags | BfCastFlags_Explicit));
					if (val)
						return val;
				}
			}
		}

		bool allowCast = false;
		switch (toTypeCode)
		{
		case BfTypeCode_Char16:
			switch (fromTypeCode)
			{
			case BfTypeCode_Char8:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_Int16:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
				allowCast = true; break;
			case BfTypeCode_UInt8:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_UInt16:
			switch (fromTypeCode)
			{
			case BfTypeCode_UInt8:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_Int32:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
			case BfTypeCode_Int16:
				allowCast = true; break;
			case BfTypeCode_IntPtr:
				if (mCompiler->mSystem->mPtrSize == 4)
					allowCast = true;
				break;
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_Char32:
			switch (fromTypeCode)
			{
			case BfTypeCode_Char8:
			case BfTypeCode_Char16:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_UInt32:
			switch (fromTypeCode)
			{
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
				allowCast = true; break;
			case BfTypeCode_UIntPtr:
				if (mCompiler->mSystem->mPtrSize == 4)
					allowCast = true;
				break;
			default: break;
			}
			break;
		case BfTypeCode_Int64:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
			case BfTypeCode_Int16:
			case BfTypeCode_Int32:
			case BfTypeCode_IntPtr:
				allowCast = true; break;
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_UInt64:
			switch (fromTypeCode)
			{
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
			case BfTypeCode_UIntPtr:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_IntPtr:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
			case BfTypeCode_Int16:
			case BfTypeCode_Int32:
				allowCast = true; break;
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
				allowCast = true; break;
			case BfTypeCode_UInt32:
			case BfTypeCode_Int64:
				// It may seem that we want this to require an explicit cast,
				// but consider the case of 
				//   int val = Math.Max(intA, intB)
				// Math.Max has an int32 and int64 override, so we want the correct one to be chosen and
				//  to be able to have the int64 return value implicitly used in a 64-bit build
				if (mCompiler->mSystem->mPtrSize == 8)
					allowCast = true;
				break;
			default: break;
			}
			break;
		case BfTypeCode_UIntPtr:
			switch (fromTypeCode)
			{
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
				allowCast = true; break;
			case BfTypeCode_UInt64:
				if (mCompiler->mSystem->mPtrSize == 8)
					allowCast = true;
				break;
			default: break;
			}
			break;
		case BfTypeCode_Single:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
			case BfTypeCode_Int16:
			case BfTypeCode_Int32:
			case BfTypeCode_Int64:
			case BfTypeCode_IntPtr:
			case BfTypeCode_IntUnknown:
				allowCast = true; break;
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
			case BfTypeCode_UInt64:
			case BfTypeCode_UIntPtr:
			case BfTypeCode_UIntUnknown:
				allowCast = true; break;
			default: break;
			}
			break;
		case BfTypeCode_Double:
			switch (fromTypeCode)
			{
			case BfTypeCode_Int8:
			case BfTypeCode_Int16:
			case BfTypeCode_Int32:
			case BfTypeCode_Int64:
			case BfTypeCode_IntPtr:
			case BfTypeCode_IntUnknown:
				allowCast = true; break;
			case BfTypeCode_UInt8:
			case BfTypeCode_UInt16:
			case BfTypeCode_UInt32:
			case BfTypeCode_UInt64:
			case BfTypeCode_UIntPtr:
			case BfTypeCode_UIntUnknown:
				allowCast = true; break;
			case BfTypeCode_Single:
				allowCast = true; break;
			default: break;
			}
			break;
		default: break;
		}

		if (explicitCast)
		{
			if (((fromPrimType->IsIntegral()) || (fromPrimType->IsFloat())) &&
				((toType->IsIntegral()) || (toType->IsFloat())))
				allowCast = true;
		}

		if (allowCast)
		{
			return mBfIRBuilder->CreateNumericCast(typedVal.mValue, typedVal.mType->IsSigned(), toTypeCode);
		}
	}

	// Check user-defined operators
	if ((castFlags & BfCastFlags_NoConversionOperator) == 0)
	{
		auto fromType = typedVal.mType;
		auto fromTypeInstance = typedVal.mType->ToTypeInstance();
		auto toTypeInstance = toType->ToTypeInstance();

		auto liftedFromType = ((fromTypeInstance != NULL) && fromTypeInstance->IsNullable()) ? fromTypeInstance->GetUnderlyingType() : NULL;
		auto liftedToType = ((toTypeInstance != NULL) && toTypeInstance->IsNullable()) ? toTypeInstance->GetUnderlyingType() : NULL;

		int bestFromDist = INT_MAX;
		BfType* bestFromType = NULL;
		int bestNegFromDist = INT_MAX;
		BfType* bestNegFromType = NULL;

		int bestToDist = INT_MAX;
		BfType* bestToType = NULL;
		int bestNegToDist = INT_MAX;
		BfType* bestNegToType = NULL;
		bool isAmbiguousCast = false;

		BfIRValue conversionResult;
		BfMethodInstance* opMethodInstance = NULL;
		BfType* opMethodSrcType = NULL;

		// Normal, lifted, execute
		for (int pass = 0; pass < 3; pass++)
		{
			auto checkToType = toType;
			auto checkFromType = fromType;

			if (pass == 1)
			{
				if ((bestFromType != NULL) && (bestToType != NULL))
					continue;

				if (liftedFromType != NULL)
					checkFromType = liftedFromType;
				if (liftedToType != NULL)
					checkToType = liftedToType;
			}
			else if (pass == 2)
			{
				if ((bestFromType == NULL) || (bestToType == NULL))
					break;
			}

			BfBaseClassWalker baseClassWalker(fromType, toType, this);
			
			while (true)
			{
				auto entry = baseClassWalker.Next();
				auto checkInstance = entry.mTypeInstance;
				if (checkInstance == NULL)
					break;

				for (auto operatorDef : checkInstance->mTypeDef->mOperators)
				{
					if (operatorDef->mOperatorDeclaration->mIsConvOperator)
					{
						if ((!explicitCast) && (operatorDef->mOperatorDeclaration->mExplicitToken != NULL) &&
							(operatorDef->mOperatorDeclaration->mExplicitToken->GetToken() == BfToken_Explicit))
							continue;

						auto methodInst = GetRawMethodInstanceAtIdx(checkInstance, operatorDef->mIdx);

						if (methodInst->GetParamCount() != 1)
						{
							BF_ASSERT(mCompiler->mPassInstance->HasFailed());
							continue;
						}

						auto methodFromType = methodInst->GetParamType(0);
						auto methodToType = methodInst->mReturnType;

						if (methodFromType->IsSelf())
							methodFromType = entry.mSrcType;
						if (methodToType->IsSelf())
							methodToType = entry.mSrcType;

						// Selection pass
						if (pass < 2)
						{
							auto methodCheckFromType = methodFromType;							
							auto methodCheckToType = methodToType;
							if (pass == 1)
							{
								// Only check inner type on lifted types when we aren't checking conversions within lifted class
								//  This avoid some infinite conversions
								if ((methodCheckFromType->IsNullable()) && (!checkInstance->IsNullable()))
									methodCheckFromType = methodCheckFromType->GetUnderlyingType();
								if ((methodCheckToType->IsNullable()) && (!checkInstance->IsNullable()))
									methodCheckToType = methodCheckToType->GetUnderlyingType();
							}

							int fromDist = GetTypeDistance(methodCheckFromType, checkFromType);
							if (fromDist < 0)
							{
								// Allow us to cast a constant int to a smaller type if it satisfies the cast operator
								if ((typedVal.mValue.IsConst()) && (CanCast(typedVal, methodCheckFromType, BfCastFlags_NoConversionOperator)))
								{
									fromDist = 0;
								}
							}

							int toDist = GetTypeDistance(methodCheckToType, checkToType);

							if ((fromDist == INT_MAX) || (toDist == INT_MAX))
								continue;

							if (((fromDist >= 0) && (toDist >= 0)) || (explicitCast))
							{
								if ((fromDist >= 0) && (fromDist < bestFromDist))
								{
									bestFromDist = fromDist;
									bestFromType = methodFromType;
								}

								if ((toDist >= 0) && (toDist < bestToDist))
								{
									bestToDist = toDist;
									bestToType = methodToType;
								}
							}

							if (explicitCast)
							{
								fromDist = abs(fromDist);
								toDist = abs(toDist);

								if ((fromDist >= 0) && (fromDist < bestNegFromDist))
								{
									bestNegFromDist = fromDist;
									bestNegFromType = methodFromType;
								}

								if ((toDist >= 0) && (toDist < bestNegToDist))
								{
									bestNegToDist = toDist;
									bestNegToType = methodInst->mReturnType;
								}
							}
						}
						else if (pass == 2) // Execution Pass
						{
							if ((methodFromType == bestFromType) && (methodToType == bestToType))
							{
								// Get in native module so our module doesn't get a reference to it - we may not end up calling it at all!
								//BfModuleMethodInstance methodInstance = checkInstance->mModule->GetMethodInstanceAtIdx(checkInstance, operatorDef->mIdx);
								BfMethodInstance* methodInstance = GetRawMethodInstanceAtIdx(checkInstance, operatorDef->mIdx);

								if (opMethodInstance != NULL)
								{
									int prevGenericCount = GetGenericParamAndReturnCount(opMethodInstance);
									int newGenericCount = GetGenericParamAndReturnCount(methodInstance);
									if (newGenericCount > prevGenericCount)
									{
										// Prefer generic match
										opMethodInstance = methodInstance;
										opMethodSrcType = entry.mSrcType;
									}
									else if (newGenericCount < prevGenericCount)
									{
										// Previous was a generic match
										continue;
									}
									else
									{
										isAmbiguousCast = true;
										break;
									}
								}
								else
								{								
									opMethodInstance = methodInstance;
									opMethodSrcType = entry.mSrcType;
								}
							}
						}
					}
				}

				if (isAmbiguousCast)
					break;

				if (opMethodInstance != NULL)
				{
					if (mayBeBox)
					{
						if ((!ignoreErrors) && (Fail("Ambiguous cast, may be conversion operator or may be boxing request", srcNode) != NULL))
							mCompiler->mPassInstance->MoreInfo("See conversion operator", opMethodInstance->mMethodDef->GetRefNode());
					}

					BfMethodInstance* methodInstance = GetRawMethodInstance(opMethodInstance->GetOwner(), opMethodInstance->mMethodDef);
					
					auto methodDeclaration = methodInstance->mMethodDef->GetMethodDeclaration();
					if (methodDeclaration->mBody == NULL)
					{						
						// Handle the typedPrim<->underlying part implicitly
						if (fromType->IsTypedPrimitive())
						{
							auto convTypedValue = BfTypedValue(typedVal.mValue, fromType->GetUnderlyingType());
							return CastToValue(srcNode, convTypedValue, toType, (BfCastFlags)(castFlags & ~BfCastFlags_Explicit), NULL);
						}
						else if (toType->IsTypedPrimitive())
						{							
							auto castedVal = CastToValue(srcNode, typedVal, toType->GetUnderlyingType(), (BfCastFlags)(castFlags & ~BfCastFlags_Explicit), NULL);
							return castedVal;
						}

						// Cannot cast (was error)
						return BfIRValue();
					}

					// Actually perform conversion
					BfExprEvaluator exprEvaluator(this);
					auto castedFromValue = Cast(srcNode, typedVal, bestFromType, castFlags);
					if (!castedFromValue)
						return BfIRValue();
					
					BfTypedValue operatorOut;
					if (ignoreWrites)
					{
						if (opMethodInstance->mReturnType == toType)
							return mBfIRBuilder->GetFakeVal();
						operatorOut = GetDefaultTypedValue(opMethodInstance->mReturnType);
					}
					else
					{
						BfModuleMethodInstance moduleMethodInstance = GetMethodInstance(opMethodInstance->GetOwner(), opMethodInstance->mMethodDef, BfTypeVector());

						SizedArray<BfIRValue, 1> args;
						exprEvaluator.PushArg(castedFromValue, args);
						operatorOut = exprEvaluator.CreateCall(moduleMethodInstance.mMethodInstance, mCompiler->IsSkippingExtraResolveChecks() ? BfIRValue() : moduleMethodInstance.mFunc, false, args);
						if ((operatorOut.mType != NULL) && (operatorOut.mType->IsSelf()))
						{
							BF_ASSERT(IsInGeneric());
							operatorOut = GetDefaultTypedValue(opMethodSrcType);
						}
					}

					return CastToValue(srcNode, operatorOut, toType, castFlags, resultFlags);
				}				
			}

			if (bestFromType == NULL)
				bestFromType = bestNegFromType;
			if (bestToType == NULL)
				bestToType = bestNegToType;
		}

		isAmbiguousCast |= ((bestFromType != NULL) && (bestToType != NULL));
		if (isAmbiguousCast)
		{
			if (!ignoreErrors)
			{
				const char* errStr = "Ambiguous conversion operators for casting from '%s' to '%s'";
				Fail(StrFormat(errStr, TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
			}
			return BfIRValue();
		}

		// Check method generic constraints
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecialized) && (mCurMethodInstance->mMethodInfoEx != NULL))
		{
			for (int genericParamIdx = 0; genericParamIdx < mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParam = mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
				for (auto& opConstraint : genericParam->mOperatorConstraints)
				{
					if ((opConstraint.mCastToken == BfToken_Implicit) ||
						((explicitCast) && (opConstraint.mCastToken == BfToken_Explicit)))
					{
						// If we can convert OUR fromVal to the constraint's fromVal then we may match
						if (CanCast(typedVal, opConstraint.mRightType))
						{
							// .. and we can convert the constraint's toType to OUR toType then we're good
							auto opToVal = genericParam->mExternType;
							if (CanCast(BfTypedValue(BfIRValue::sValueless, opToVal), toType))
								return mBfIRBuilder->GetFakeVal();
						}
					}
				}
			}
		}

		// Check type generic constraints
		if ((mCurTypeInstance->IsGenericTypeInstance()) && (mCurTypeInstance->IsUnspecializedType()))
		{
			auto genericTypeInst = (BfGenericTypeInstance*)mCurTypeInstance;
			for (int genericParamIdx = 0; genericParamIdx < genericTypeInst->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParam = GetGenericTypeParamInstance(genericParamIdx);
				for (auto& opConstraint : genericParam->mOperatorConstraints)
				{
					if ((opConstraint.mCastToken == BfToken_Implicit) ||
						((explicitCast) && (opConstraint.mCastToken == BfToken_Explicit)))
					{
						// If we can convert OUR fromVal to the constraint's fromVal then we may match
						if (CanCast(typedVal, opConstraint.mRightType))
						{
							// .. and we can convert the constraint's toType to OUR toType then we're good
							auto opToVal = genericParam->mExternType;
							if (CanCast(BfTypedValue(BfIRValue::sValueless, opToVal), toType))
								return mBfIRBuilder->GetFakeVal();
						}
					}
				}
			}
		}
	}

	// Default typed primitive 'underlying casts' happen after checking cast operators
	if (explicitCast)
	{
		// TypedPrimitive -> Primitive
		if ((typedVal.mType->IsTypedPrimitive()) && (!typedVal.mType->IsFunction()) && (toType->IsPrimitiveType()))
		{
			auto fromTypedPrimitiveType = typedVal.mType->ToTypeInstance();
			auto primTypedVal = BfTypedValue(typedVal.mValue, fromTypedPrimitiveType->mFieldInstances.back().mResolvedType, typedVal.IsAddr());
			primTypedVal = LoadValue(primTypedVal);
			return CastToValue(srcNode, primTypedVal, toType, castFlags);
		}

		// TypedPrimitive -> TypedPrimitive
		if ((typedVal.mType->IsTypedPrimitive()) && (!typedVal.mType->IsFunction()) && (toType->IsTypedPrimitive()))
		{
			auto fromTypedPrimitiveType = typedVal.mType->ToTypeInstance();
			auto toTypedPrimitiveType = toType->ToTypeInstance();

			auto fromUnderlyingType = fromTypedPrimitiveType->GetUnderlyingType();
			auto toUnderlyingType = toTypedPrimitiveType->GetUnderlyingType();

			BfTypedValue underlyingTypedValue(typedVal.mValue, fromUnderlyingType, typedVal.IsAddr());
			underlyingTypedValue = LoadValue(underlyingTypedValue);
			BfIRValue castedToValue = CastToValue(srcNode, underlyingTypedValue, toUnderlyingType, (BfCastFlags)(castFlags | BfCastFlags_Explicit));
			if (castedToValue)
				return castedToValue;
		}
	}
	else if ((typedVal.mType->IsTypedPrimitive()) && (toType->IsTypedPrimitive()))
	{
		if (TypeIsSubTypeOf(typedVal.mType->ToTypeInstance(), toType->ToTypeInstance()))
		{
			// These have the same underlying primitive type, just keep it all the same

			if ((resultFlags != NULL) && (typedVal.IsAddr()))
				*resultFlags = BfCastResultFlags_IsAddr;
			return typedVal.mValue;
		}
	}

	// Prim -> TypedPrimitive
	if ((typedVal.mType->IsPrimitiveType()) && (toType->IsTypedPrimitive()))
	{
		bool allowCast = explicitCast;
		if (toType == mCurTypeInstance)
			allowCast = true;
 		if ((!allowCast) && (typedVal.mType->IsIntegral()) /*&& (!toType->IsEnum())*/)
 		{
 			// Allow implicit cast of zero
 			auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
 			if ((constant != NULL) && (mBfIRBuilder->IsInt(constant->mTypeCode)))
 			{
 				allowCast = constant->mInt64 == 0;
 			}
 		}
		if (allowCast)
		{			
			return CastToValue(srcNode, typedVal, toType->GetUnderlyingType(), castFlags);
		}
	}

	if (typedVal.mType->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)typedVal.mType;
		if (boxedType->mElementType->IsGenericParam())
		{
			// If we have a boxed generic param, the actual available interfaces constraints won't be 
			// handled, so we need to pass through again as the root generic param
			BfTypedValue unboxedValue = typedVal;
			unboxedValue.mType = boxedType->mElementType;
			auto result = CastToValue(srcNode, unboxedValue, toType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail), resultFlags);
			if (result)
				return result;
		}
	}

	if (mayBeBox)
	{
		BfScopeData* scopeData = NULL;
		if (mCurMethodState != NULL)
		{
			if (mCurMethodState->mOverrideScope)
				scopeData = mCurMethodState->mOverrideScope;
			else
				scopeData = mCurMethodState->mCurScope;
		}

		if ((castFlags & BfCastFlags_WarnOnBox) != 0)
		{
			Warn(0, "This implicit boxing will only be in scope during the constructor. Consider using a longer-term allocation such as 'box new'", srcNode);
		}

		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, ignoreWrites);
		auto value = BoxValue(srcNode, typedVal, toType, scopeData, (castFlags & BfCastFlags_NoBoxDtor) == 0);
		if (value)
			return value.mValue;
	}

	if (!ignoreErrors)
	{
		const char* errStrF = explicitCast ?
			"Unable to cast '%s' to '%s'" :
			"Unable to implicitly cast '%s' to '%s'";

		String errStr = StrFormat(errStrF, TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str());		
		auto error = Fail(errStr, srcNode);
		if ((error != NULL) && (srcNode != NULL))
		{
			if ((mCompiler->IsAutocomplete()) && (mCompiler->mResolvePassData->mAutoComplete->CheckFixit((srcNode))))
			{
				SetAndRestoreValue<bool> ignoreWrites(mBfIRBuilder->mIgnoreWrites);
				SetAndRestoreValue<bool> ignoreErrors(mIgnoreErrors, true);

				if (CastToValue(srcNode, typedVal, toType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail)))
				{	
					bool doWrap = false;
					if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(srcNode))
					{
						if ((unaryOpExpr->mOp != BfUnaryOp_AddressOf) && (unaryOpExpr->mOp != BfUnaryOp_Dereference))
							doWrap = true;
					}
					if ((srcNode->IsA<BfCastExpression>()) ||
						(srcNode->IsA<BfBinaryOperatorExpression>()) ||
						(srcNode->IsA<BfConditionalExpression>()))
						doWrap = true;

					BfParserData* parser = srcNode->GetSourceData()->ToParserData();
					String typeName = TypeToString(toType);

					if (doWrap)
					{
						mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit",
							StrFormat("(%s)\tcast|%s|%d|(%s)(|`%d|)", typeName.c_str(), parser->mFileName.c_str(), srcNode->GetSrcStart(), typeName.c_str(), srcNode->GetSrcLength()).c_str()));						
					}
					else
					{
						mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit",
							StrFormat("(%s)\tcast|%s|%d|(%s)", typeName.c_str(), parser->mFileName.c_str(), srcNode->GetSrcStart(), typeName.c_str()).c_str()));
					}
				}
			}
		}
	}
	return BfIRValue();
}

BfTypedValue BfModule::Cast(BfAstNode* srcNode, const BfTypedValue& typedVal, BfType* toType, BfCastFlags castFlags)
{
	bool explicitCast = (castFlags & BfCastFlags_Explicit) != 0;	

	if (typedVal.mType == toType)
		return typedVal;

	PopulateType(toType, ((castFlags & BfCastFlags_NoConversionOperator) != 0) ? BfPopulateType_Data : BfPopulateType_DataAndMethods);

	if ((castFlags & BfCastFlags_Force) != 0)
	{
		if (toType->IsValuelessType())
			return BfTypedValue(mBfIRBuilder->GetFakeVal(), toType);

		if ((typedVal.mType->IsValueType()) && (!typedVal.IsAddr()) && (typedVal.IsSplat()) && (toType->IsValueType()))
		{
			bool needsMemberCasting = false;
			if (AreSplatsCompatible(typedVal.mType, toType, &needsMemberCasting))
			{
				return BfTypedValue(typedVal.mValue, toType, needsMemberCasting ? BfTypedValueKind_SplatHead_NeedsCasting : BfTypedValueKind_SplatHead);
			}
		}	
				
		if (typedVal.mType->IsValueType())
		{
			auto addrTypedValue = MakeAddressable(typedVal);
			auto toPtrType = CreatePointerType(toType);
			return BfTypedValue(mBfIRBuilder->CreateBitCast(addrTypedValue.mValue, mBfIRBuilder->MapType(toPtrType)), toType, BfTypedValueKind_Addr);
		}
		return BfTypedValue(mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType)), toType);
	}

	// This tuple cast may create a new type if the toType contains 'var' entries
	if ((typedVal.mType->IsTuple()) && (toType->IsTuple()))
	{
		//auto loadedVal = LoadValue(typedVal);
		PopulateType(toType);

		auto fromTupleType = (BfTupleType*)typedVal.mType;
		auto toTupleType = (BfTupleType*)toType;
		if (fromTupleType == toTupleType)
			return typedVal;

		if (fromTupleType->mFieldInstances.size() == toTupleType->mFieldInstances.size())
		{
			BfTypeVector fieldTypes;
			Array<String> fieldNames;
			bool isCompatible = true;
			bool isExactTypeMatch = true;

			for (int fieldIdx = 0; fieldIdx < (int)fromTupleType->mFieldInstances.size(); fieldIdx++)
			{
				auto fromFieldInst = &fromTupleType->mFieldInstances[fieldIdx];
				auto toFieldInst = &toTupleType->mFieldInstances[fieldIdx];

				auto fromFieldDef = fromFieldInst->GetFieldDef();
				auto toFieldDef = toFieldInst->GetFieldDef();

				if (!toFieldDef->IsUnnamedTupleField())
				{
					if ((!explicitCast) &&
						(!fromFieldDef->IsUnnamedTupleField()) &&
						(fromFieldDef->mName != toFieldDef->mName))
						isCompatible = false;
					fieldNames.push_back(toFieldDef->mName);
				}
				else
					fieldNames.push_back("");

				if (toFieldInst->mResolvedType->IsVar())
					fieldTypes.push_back(fromFieldInst->mResolvedType);
				else
				{
					if (fromFieldInst->mResolvedType != toFieldInst->mResolvedType)
						isExactTypeMatch = false;

					BfCastFlags tryCastFlags = BfCastFlags_SilentFail;
					if (explicitCast)
						tryCastFlags = (BfCastFlags)(tryCastFlags | BfCastFlags_Explicit);

					// The unused-token '?' comes out as 'void', so we allow that to match here.  We may want to wrap that with a different fake type
					//  so we can give normal implicit-cast-to-void errors
					if ((fromFieldInst->mResolvedType != toFieldInst->mResolvedType) && (!toFieldInst->mResolvedType->IsVoid()) &&
						(!CanCast(GetFakeTypedValue(fromFieldInst->mResolvedType), toFieldInst->mResolvedType, tryCastFlags)))
						isCompatible = false;
					fieldTypes.push_back(toFieldInst->mResolvedType);
				}
			}

			auto tupleType = CreateTupleType(fieldTypes, fieldNames);
			AddDependency(tupleType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
			mBfIRBuilder->PopulateType(tupleType);

			if (isCompatible)
			{
				if (isExactTypeMatch)
				{
  					if (typedVal.mKind == BfTypedValueKind_TempAddr)
 					{
 						return BfTypedValue(mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapTypeInstPtr(tupleType)), tupleType, BfTypedValueKind_TempAddr);
 					}
					else if (typedVal.IsAddr())
					{
						return BfTypedValue(mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapTypeInstPtr(tupleType)), tupleType, BfTypedValueKind_ReadOnlyAddr);
					}
								
					BfIRValue curTupleValue = CreateAlloca(tupleType);
					auto loadedVal = LoadValue(typedVal);
					mBfIRBuilder->CreateStore(loadedVal.mValue, mBfIRBuilder->CreateBitCast(curTupleValue, mBfIRBuilder->MapTypeInstPtr(fromTupleType)));
					return BfTypedValue(curTupleValue, tupleType, BfTypedValueKind_TempAddr);
				}			
									
				BfIRValue curTupleValue = CreateAlloca(tupleType);
				for (int fieldIdx = 0; fieldIdx < (int)fromTupleType->mFieldInstances.size(); fieldIdx++)
				{
					BfFieldInstance* fromFieldInstance = &fromTupleType->mFieldInstances[fieldIdx];
					BfFieldInstance* toFieldInstance = &tupleType->mFieldInstances[fieldIdx];
					if (toFieldInstance->mDataIdx >= 0)
					{
						if (fromFieldInstance->mDataIdx >= 0)
						{
							auto elementVal = ExtractValue(typedVal, fromFieldInstance, fromFieldInstance->mDataIdx);
							elementVal = LoadValue(elementVal);

							auto castedElementVal = Cast(srcNode, elementVal, toFieldInstance->GetResolvedType(), castFlags);
							if (!castedElementVal)
								return BfTypedValue();
							auto fieldRef = mBfIRBuilder->CreateInBoundsGEP(curTupleValue, 0, toFieldInstance->mDataIdx);
							mBfIRBuilder->CreateStore(castedElementVal.mValue, fieldRef);
						}
						else
							isCompatible = false;
					}
				}
				
				return BfTypedValue(curTupleValue, tupleType, BfTypedValueKind_TempAddr);
			}
		}

		const char* errStr = explicitCast ?
			"Unable to cast '%s' to '%s'" :
			"Unable to implicitly cast '%s' to '%s'";
		Fail(StrFormat(errStr, TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
		return BfTypedValue();
	}

	// Struct truncate
	if ((typedVal.mType->IsStruct()) && (toType->IsStruct()))
	{
		auto fromStructTypeInstance = typedVal.mType->ToTypeInstance();
		auto toStructTypeInstance = toType->ToTypeInstance();
		if (TypeIsSubTypeOf(fromStructTypeInstance, toStructTypeInstance))
		{
			if (typedVal.IsSplat())
			{
				BF_ASSERT(toStructTypeInstance->IsSplattable() || (toStructTypeInstance->mInstSize == 0));
				return BfTypedValue(typedVal.mValue, toStructTypeInstance, typedVal.IsThis() ? BfTypedValueKind_ThisSplatHead : BfTypedValueKind_SplatHead);
			}

			if (typedVal.IsAddr())
			{
				BfIRValue castedIRValue;
				if (typedVal.mValue.IsFake())
					castedIRValue = typedVal.mValue;
				else
					castedIRValue = mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapTypeInstPtr(toStructTypeInstance));

				return BfTypedValue(castedIRValue, toType, typedVal.IsThis() ?
					(typedVal.IsReadOnly() ? BfTypedValueKind_ReadOnlyThisAddr : BfTypedValueKind_ThisAddr) :
					(typedVal.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr));
			}

			BfTypedValue curTypedVal = typedVal;
			while (curTypedVal.mType != toStructTypeInstance)
			{
				mBfIRBuilder->PopulateType(curTypedVal.mType);
				auto curTypeInstance = curTypedVal.mType->ToTypeInstance();
				BfIRValue extractedValue;
				if (toStructTypeInstance->IsValuelessType())
					extractedValue = mBfIRBuilder->GetFakeVal();
				else
					extractedValue = mBfIRBuilder->CreateExtractValue(curTypedVal.mValue, 0);
				curTypedVal = BfTypedValue(extractedValue, curTypeInstance->mBaseType, typedVal.IsThis() ?
					(typedVal.IsReadOnly() ? BfTypedValueKind_ReadOnlyThisValue : BfTypedValueKind_ThisValue) :
					BfTypedValueKind_Value);
			}
			return curTypedVal;
		}
	}

	/*if ((explicitCast) && (toType->IsValuelessType()))
	{
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), toType);
	}*/

	BfCastResultFlags castResultFlags = BfCastResultFlags_None;
	auto castedValue = CastToValue(srcNode, typedVal, toType, castFlags, &castResultFlags);
	if (!castedValue)
		return BfTypedValue();

	if ((castResultFlags & BfCastResultFlags_IsAddr) != 0)
	{
		if ((castResultFlags & BfCastResultFlags_IsTemp) != 0)
			return BfTypedValue(castedValue, toType, BfTypedValueKind_TempAddr);
		return BfTypedValue(castedValue, toType, BfTypedValueKind_Addr);
	}
	return BfTypedValue(castedValue, toType, BfTypedValueKind_Value);
}

BfPrimitiveType* BfModule::GetIntCoercibleType(BfType* type)
{
	if (type->IsSizedArray())
	{
		auto sizedArray = (BfSizedArrayType*)type;
		if ((sizedArray->mElementType->IsChar()) && (sizedArray->mElementType->mSize == 1))
		{
			auto primType = (BfPrimitiveType*)sizedArray->mElementType;
			if (sizedArray->mElementCount == 1)
				return GetPrimitiveType(BfTypeCode_UInt8);
			if (sizedArray->mElementCount == 2)
				return GetPrimitiveType(BfTypeCode_UInt16);
			if (sizedArray->mElementCount == 4)
				return GetPrimitiveType(BfTypeCode_UInt32);
			if (sizedArray->mElementCount == 8)
				return GetPrimitiveType(BfTypeCode_UInt64);
		}
	}
	return NULL;
}

BfTypedValue BfModule::GetIntCoercible(const BfTypedValue& typedValue)
{
	auto intType = GetIntCoercibleType(typedValue.mType);
	if (intType == NULL)
		return BfTypedValue();

	if (typedValue.mValue.IsConst())
	{		
		auto constant = mBfIRBuilder->GetConstant(typedValue.mValue);
		if (constant->mConstType == BfConstType_Array)
		{
			uint64 intVal = 0;

			auto constantArray = (BfConstantArray*)constant;
			int memberIdx = 0;			
			for (int memberIdx = 0; memberIdx < (int)constantArray->mValues.size(); memberIdx++)
			{
				auto memberConstant = mBfIRBuilder->GetConstant(constantArray->mValues[memberIdx]);
				if (memberConstant->mTypeCode == BfTypeCode_Char8)
				{
					intVal |= (uint64)(memberConstant->mUInt8) << (8 * memberIdx);

					//intVal = (intVal << 8) | memberConstant->mUInt8;
				}
			}

			return BfTypedValue(mBfIRBuilder->CreateConst(intType->mTypeDef->mTypeCode, intVal), intType);
		}
	}

	auto convTypedValue = typedValue;
	convTypedValue = MakeAddressable(convTypedValue);
	auto intPtrType = CreatePointerType(intType);

	auto addrVal = mBfIRBuilder->CreateBitCast(convTypedValue.mValue, mBfIRBuilder->MapType(intPtrType));
	auto val = mBfIRBuilder->CreateLoad(addrVal);

	return BfTypedValue(val, intType);
}

bool BfModule::TypeHasParent(BfTypeDef* checkChildTypeDef, BfTypeDef* checkParentTypeDef)
{
	BfTypeDef* checkType = checkChildTypeDef;
	while (checkType != NULL)
	{
		if (checkType == checkParentTypeDef)
			return true;
		checkType = checkType->mOuterType;
	}
	return false;
}

BfTypeDef* BfModule::FindCommonOuterType(BfTypeDef* type, BfTypeDef* type2)
{
	if ((type == NULL) || (type2 == NULL))
		return NULL;
	int curNestDepth = std::min(type->mNestDepth, type2->mNestDepth);
	while (type->mNestDepth > curNestDepth)
		type = type->mOuterType;
	while (type2->mNestDepth > curNestDepth)
		type2 = type2->mOuterType;

	while (curNestDepth >= 0)
	{
		if ((!type->mIsPartial) && (!type2->mIsPartial))
		{
			if (type == type2)
				return type;
		}
		else
		{
			if (type->mFullNameEx == type2->mFullNameEx)
				return type;
		}
		type = type->mOuterType;
		type2 = type2->mOuterType;
		curNestDepth--;
	}

	return NULL;
}

bool BfModule::TypeIsSubTypeOf(BfTypeInstance* srcType, BfTypeInstance* wantType, bool checkAccessibility)
{
	if ((srcType == NULL) || (wantType == NULL))
		return false;
	if (srcType == wantType)
		return true;

	if (srcType->mDefineState < BfTypeDefineState_HasInterfaces)
	{
		// Type is incomplete.  We don't do the IsIncomplete check here because of re-entry
		//  While handling 'var' resolution, we don't want to force a PopulateType reentry 
		//  but we do have enough information for TypeIsSubTypeOf
		PopulateType(srcType, BfPopulateType_Interfaces);
	}

	if (wantType->IsInterface())
	{
		BfTypeDef* checkActiveTypeDef = NULL;
		bool checkAccessibility = true;		
		if (IsInSpecializedSection())
		{
			// When we have a specialized section, the generic params may not be considered "included" 
			//  in the module that contains the generic type definition.  We rely on any casting errors
			//  to be thrown on the unspecialized type pass. We have a similar issue with injecting mixins.
			checkAccessibility = false;			
		}				
		
		auto checkType = srcType;
		while (checkType != NULL)
		{
			for (auto ifaceInst : checkType->mInterfaces)
			{
				if (ifaceInst.mInterfaceType == wantType)
				{
					if (checkAccessibility)
					{
						if (checkActiveTypeDef == NULL)
							checkActiveTypeDef = GetActiveTypeDef(NULL, false);

						// We need to be lenient when validating generic constraints
						//  Otherwise "T<A> where T : IB" declared in a lib won't be able to match a type B in a using project 'C',
						//  because this check will see the lib using 'C', which it won't consider visible							
						if ((checkActiveTypeDef != NULL) &&
							((mCurMethodInstance != NULL) && (mContext->mCurTypeState != NULL) && (!mContext->mCurTypeState->mBuildingGenericParams)))
						{
							if ((!srcType->IsTypeMemberAccessible(ifaceInst.mDeclaringType, checkActiveTypeDef)) ||
								(!srcType->IsTypeMemberIncluded(ifaceInst.mDeclaringType, checkActiveTypeDef, this)))
							{
								continue;
							}
						}
					}

					return true;
				}
			}
			checkType = checkType->GetImplBaseType();
			if ((checkType != NULL) && (checkType->mDefineState < BfTypeDefineState_HasInterfaces))
			{				
				PopulateType(checkType, BfPopulateType_Interfaces);
			}
		}

		if (srcType->IsTypedPrimitive())
		{
			BfType* underlyingType = srcType->GetUnderlyingType();
			if (underlyingType->IsWrappableType())
			{
				BfTypeInstance* wrappedType = GetWrappedStructType(underlyingType);
				if ((wrappedType != NULL) && (wrappedType != srcType))
					return TypeIsSubTypeOf(wrappedType, wantType, checkAccessibility);
			}
		}

		return false;
	}

	auto srcBaseType = srcType->mBaseType;
	return TypeIsSubTypeOf(srcBaseType, wantType);
}

// Positive value means that toType encompasses fromType, negative value means toType is encompassed by formType
//	INT_MAX means the types are not related
int BfModule::GetTypeDistance(BfType* fromType, BfType* toType)
{
	if (fromType == toType)
		return 0;

	if (fromType->IsPrimitiveType())
	{
		if (!toType->IsPrimitiveType())
			return INT_MAX;
		auto fromPrimType = (BfPrimitiveType*)fromType;
		auto toPrimType = (BfPrimitiveType*)toType;

		if ((fromPrimType->IsIntegral()) && (toPrimType->IsIntegral()))
		{			
			int fromBitSize = fromPrimType->mSize * 8;
			if (fromPrimType->IsSigned())
				fromBitSize--;
			int toBitSize = toPrimType->mSize * 8;
			if (toPrimType->IsSigned())
				toBitSize--;
			return fromBitSize - toBitSize;
		}

		if ((fromPrimType->IsFloat()) && (toPrimType->IsFloat()))
		{
			return (fromPrimType->mSize * 8) - (toPrimType->mSize * 8);
		}

		if (((fromPrimType->IsIntegral()) || (fromPrimType->IsFloat())) &&
			((toPrimType->IsIntegral()) || (toPrimType->IsFloat())))
		{
			int sizeDiff = (fromPrimType->mSize * 8) - (toPrimType->mSize * 8);
			if (sizeDiff < 0)
				sizeDiff--;
			else
				sizeDiff++;
			return sizeDiff;
		}
		return INT_MAX;
	}

	auto fromTypeInstance = fromType->ToTypeInstance();
	auto toTypeInstance = toType->ToTypeInstance();

	if ((fromTypeInstance != NULL) != (toTypeInstance != NULL))
		return INT_MAX; // Ever valid?

	if ((fromTypeInstance != NULL) && (toTypeInstance != NULL))
	{
		if ((fromTypeInstance->IsNullable()) && (toTypeInstance->IsNullable()))
			return GetTypeDistance(fromTypeInstance->GetUnderlyingType(), toTypeInstance->GetUnderlyingType());

		int inheritDistance = toTypeInstance->mInheritDepth - fromTypeInstance->mInheritDepth;
		auto mostSpecificInstance = (inheritDistance < 0) ? fromTypeInstance : toTypeInstance;
		auto leastSpecificInstance = (inheritDistance < 0) ? toTypeInstance : fromTypeInstance;

		while (mostSpecificInstance != NULL)
		{
			if (mostSpecificInstance == leastSpecificInstance)
				return inheritDistance;
			mostSpecificInstance = mostSpecificInstance->mBaseType;
		}
	}

	return INT_MAX;
}

bool BfModule::IsTypeMoreSpecific(BfType* leftType, BfType* rightType)
{
	if (leftType->IsGenericTypeInstance())
	{
		if (!rightType->IsGenericTypeInstance())
			return true;

		auto leftGenericType = (BfGenericTypeInstance*)leftType;
		auto rightGenericType = (BfGenericTypeInstance*)rightType;

		if (leftGenericType->mTypeDef != rightGenericType->mTypeDef)
			return false;

		bool isBetter = false;
		bool isWorse = false;
		for (int argIdx = 0; argIdx < (int)leftGenericType->mTypeGenericArguments.size(); argIdx++)
		{
			if (IsTypeMoreSpecific(leftGenericType->mTypeGenericArguments[argIdx], rightGenericType->mTypeGenericArguments[argIdx]))
				isBetter = true;
			if (IsTypeMoreSpecific(rightGenericType->mTypeGenericArguments[argIdx], leftGenericType->mTypeGenericArguments[argIdx]))
				isWorse = true;
		}

		return (isBetter) && (!isWorse);
	}

	return false;
}

StringT<128> BfModule::TypeToString(BfType* resolvedType, Array<String>* genericMethodParamNameOverrides)
{
	BfTypeNameFlags flags = BfTypeNameFlags_None;
	if ((mCurTypeInstance == NULL) || (!mCurTypeInstance->IsUnspecializedTypeVariation()))
		flags = BfTypeNameFlag_ResolveGenericParamNames;
	
	StringT<128> str;
	DoTypeToString(str, resolvedType, flags, genericMethodParamNameOverrides);
	return str;
}

StringT<128> BfModule::TypeToString(BfType* resolvedType, BfTypeNameFlags typeNameFlags, Array<String>* genericMethodParamNameOverrides)
{	
	StringT<128> str;
	DoTypeToString(str, resolvedType, typeNameFlags, genericMethodParamNameOverrides);	
	return str;
}

void BfModule::VariantToString(StringImpl& str, const BfVariant& variant)
{
	switch (variant.mTypeCode)
	{
	case BfTypeCode_Char8:
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Int32:
		str += StrFormat("%d", variant.mInt32);
		break;
	case BfTypeCode_UInt32:
		str += StrFormat("%lu", variant.mUInt32);
		break;
	case BfTypeCode_Int64:
		str += StrFormat("%lld", variant.mInt64);
		break;
	case BfTypeCode_UInt64:
		str += StrFormat("%llu", variant.mInt64);
		break;
	case BfTypeCode_Single:
		{
			char cstr[64];
			ExactMinimalFloatToStr(variant.mSingle, cstr);
			str += cstr;
			if (strchr(cstr, '.') == NULL)
				str += ".0f";
			else
				str += "f";
		}
		break;
	case BfTypeCode_Double:
		{
			char cstr[64];
			ExactMinimalDoubleToStr(variant.mDouble, cstr);
			str += cstr;
			if (strchr(cstr, '.') == NULL)
				str += ".0";
		}
		break;
	default: break;
	}
}

void BfModule::DoTypeToString(StringImpl& str, BfType* resolvedType, BfTypeNameFlags typeNameFlags, Array<String>* genericMethodNameOverrides)
{
	BP_ZONE("BfModule::DoTypeToString");

	// This is clearly wrong.  If we pass in @T0 from a generic type, this would immediately disable the ability to get its name
	/*if (resolvedType->IsUnspecializedType())
	typeNameFlags = (BfTypeNameFlags)(typeNameFlags & ~BfTypeNameFlag_ResolveGenericParamNames);*/

	if (resolvedType->IsBoxed())
	{
		auto boxedType = (BfBoxedType*)resolvedType;
		str += "boxed ";
		DoTypeToString(str, boxedType->mElementType, typeNameFlags, genericMethodNameOverrides);
		if (boxedType->mBoxedFlags == BfBoxedType::BoxedFlags_StructPtr)
			str += "*";
		return;
	}
	else if ((resolvedType->IsArray()) && ((typeNameFlags & BfTypeNameFlag_UseArrayImplType) == 0))
	{
		auto arrayType = (BfArrayType*)resolvedType;
		DoTypeToString(str, arrayType->mTypeGenericArguments[0], typeNameFlags, genericMethodNameOverrides);
		str += "[";
		for (int i = 1; i < arrayType->mDimensions; i++)
			str += ",";		
		str += "]";
		return;
	}
	else if ((resolvedType->IsNullable()) && (!resolvedType->IsUnspecializedType()))
	{
		auto genericType = (BfGenericTypeInstance*)resolvedType;
		auto elementType = genericType->mTypeGenericArguments[0];
		DoTypeToString(str, elementType, typeNameFlags, genericMethodNameOverrides);
		str += "?";
		return;
	}
	else if (resolvedType->IsTuple())
	{
		BfTupleType* tupleType = (BfTupleType*)resolvedType;

		str += "(";
		for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
		{
			if (fieldIdx > 0)
				str += ", ";

			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			DoTypeToString(str, fieldInstance->GetResolvedType(), (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType)), genericMethodNameOverrides);

			char c = fieldDef->mName[0];
			if ((c < '0') || (c > '9'))
			{
				str += " ";
				str += fieldDef->mName;
			}
		}
		str += ")";
		return;
	}	
	else if (resolvedType->IsDelegateFromTypeRef() || resolvedType->IsFunctionFromTypeRef())
	{
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance);

		auto delegateType = (BfDelegateType*)resolvedType;

		if (mCurTypeInstance == delegateType)
		{
			// Don't try to use ourselves for generic param resolution.  This should only happen for debug printings from
			//  within InitType and such, not actual user-facing display
			mCurTypeInstance = NULL;
		}

		auto methodDef = delegateType->mTypeDef->mMethods[0];
		
		if (resolvedType->IsDelegateFromTypeRef())
			str += "delegate ";
		else
			str += "function ";
		DoTypeToString(str, delegateType->mReturnType);
		str += "(";
		for (int paramIdx = 0; paramIdx < methodDef->mParams.size(); paramIdx++)
		{
			if (paramIdx > 0)
				str += ", ";
			auto paramDef = methodDef->mParams[paramIdx];
			BfTypeNameFlags innerFlags = (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType));

			//TODO: Why was this necessary? It made some errors show incorrectly
// 			if (delegateType->mIsUnspecializedTypeVariation)
// 				innerFlags = (BfTypeNameFlags)(innerFlags & ~BfTypeNameFlag_ResolveGenericParamNames);
			DoTypeToString(str, delegateType->mParams[paramIdx], innerFlags, genericMethodNameOverrides);
			str += " ";
			str += paramDef->mName;
		}

		str += ")";
		return;
	}
	else if (resolvedType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)resolvedType;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;
		if (methodRefType->IsDeleting())
		{
			str += "DELETED METHODREF";
			return;
		}
		if (methodInstance == NULL)
		{
			str += "method NULL";
			return;
		}
		str += "method ";
		str += MethodToString(methodInstance);
		return;
	}
	else if (resolvedType->IsTypeInstance())
	{
		BfTypeInstance* typeInstance = (BfTypeInstance*)resolvedType;
		
		//auto checkTypeInst = typeInstance;
		//auto checkTypeDef = typeInstance->mTypeDef;
							
		bool omitNamespace = (typeNameFlags & BfTypeNameFlag_OmitNamespace) != 0;
		if ((typeNameFlags & BfTypeNameFlag_ReduceName) != 0)
		{
			for (auto& checkNamespace : mCurTypeInstance->mTypeDef->mNamespaceSearch)
			{
				if (checkNamespace == typeInstance->mTypeDef->mNamespace)
					omitNamespace = true;
			}
		}

		if ((!typeInstance->mTypeDef->mNamespace.IsEmpty()) && (!omitNamespace))
		{			
			if (!typeInstance->mTypeDef->mNamespace.IsEmpty())
			{
				typeInstance->mTypeDef->mNamespace.ToString(str);
				if (!typeInstance->mTypeDef->IsGlobalsContainer())
					str += '.';
			}
		}
		
		//_AddTypeName(typeInstance->mTypeDef, 0);

		//std::function<void(String& str, BfTypeInstance*, BfTypeDef*, int, BfTypeNameFlags)> _AddTypeName = [&](StringImpl& str, BfTypeInstance*& checkTypeInst, BfTypeDef* checkTypeDef, int depth, BfTypeNameFlags typeNameFlags)
		
// 		BfTypeDef* endTypeDef = NULL;
// 		if ((typeNameFlags & BfTypeNameFlag_ReduceName) != 0)
// 		{
// 			auto checkTypeInst = typeInstance;
// 			auto checkTypeDef = typeInstance->mTypeDef;
// 
// 			auto outerTypeInst = GetOuterType(checkTypeInst);
// 			if (outerTypeInst == NULL)
// 				return;
// 			checkTypeInst = outerTypeInst;
// 
// 			auto checkCurTypeInst = mCurTypeInstance; // Only used for ReduceName
// 			BfTypeDef* checkCurTypeDef = NULL;
// 			if (checkCurTypeInst != NULL)
// 				checkCurTypeDef = checkCurTypeInst->mTypeDef;
// 
// 			while (checkCurTypeDef->mNestDepth > checkTypeDef->mNestDepth)
// 			{
// 				checkCurTypeInst = GetOuterType(checkCurTypeInst);
// 				checkCurTypeDef = checkCurTypeInst->mTypeDef;
// 			}
// 
// 			if (TypeIsSubTypeOf(checkCurTypeInst, checkTypeInst))
// 				endTypeDef = checkCurTypeDef;
// 		}

		

		SizedArray<BfTypeDef*, 8> typeDefStack;		
		
		BfTypeDef* endTypeDef = NULL;
		if (((typeNameFlags & BfTypeNameFlag_ReduceName) != 0) && (mCurTypeInstance != NULL))
		{
			auto checkTypeInst = typeInstance; 			
 
 			auto outerTypeInst = GetOuterType(checkTypeInst);
 			if (outerTypeInst != NULL) 				
			{
				checkTypeInst = outerTypeInst;
				auto checkTypeDef = checkTypeInst->mTypeDef;

				auto checkCurTypeInst = mCurTypeInstance; // Only used for ReduceName
				BfTypeDef* checkCurTypeDef = NULL;
				if (checkCurTypeInst != NULL)
					checkCurTypeDef = checkCurTypeInst->mTypeDef;

				while (checkCurTypeDef->mNestDepth > checkTypeDef->mNestDepth)
				{
					checkCurTypeInst = GetOuterType(checkCurTypeInst);
					checkCurTypeDef = checkCurTypeInst->mTypeDef;
				}

				while (checkTypeDef != NULL)
				{
					if (TypeIsSubTypeOf(checkCurTypeInst, checkTypeInst))
					{
						endTypeDef = checkTypeDef;
						break;
					}

					checkCurTypeInst = GetOuterType(checkCurTypeInst);
					if (checkCurTypeInst == NULL)
						break;
					checkCurTypeDef = checkCurTypeInst->mTypeDef;

					checkTypeInst = GetOuterType(checkTypeInst);
					if (checkTypeInst == NULL)
						break;
					checkTypeDef = checkTypeInst->mTypeDef;
				}
			}
		}

		BfTypeDef* checkTypeDef = typeInstance->mTypeDef;
		while (checkTypeDef != NULL)
		{
			typeDefStack.Add(checkTypeDef);

			checkTypeDef = checkTypeDef->mOuterType;

			if ((typeNameFlags & BfTypeNameFlag_OmitOuterType) != 0)
				break;

			if (checkTypeDef == endTypeDef)
				break;
		}

		while (!typeDefStack.IsEmpty())
		{
			BfTypeDef* checkTypeDef = typeDefStack.back();
			int depth = (int)typeDefStack.size() - 1;
			typeDefStack.pop_back();

// 			if (depth > 0)
// 			{
// 				if ((typeNameFlags & BfTypeNameFlag_OmitOuterType) != 0)
// 					return;
// 
// 				if ((typeNameFlags & BfTypeNameFlag_ReduceName) != 0)
// 				{
// 					auto outerTypeInst = GetOuterType(checkTypeInst);
// 					if (outerTypeInst == NULL)
// 						return;
// 					checkTypeInst = outerTypeInst;
// 
// 					while (checkCurTypeDef->mNestDepth > checkTypeDef->mNestDepth)
// 					{
// 						checkCurTypeInst = GetOuterType(checkCurTypeInst);
// 						checkCurTypeDef = checkCurTypeInst->mTypeDef;
// 					}
// 
// 					if (TypeIsSubTypeOf(checkCurTypeInst, checkTypeInst))
// 						return; // Found outer type				
// 				}
// 			}

// 			auto parentTypeDef = checkTypeDef->mOuterType;
// 			if (parentTypeDef != NULL)
// 			{
// 				//_AddTypeName(parentTypeDef, depth + 1);
// 				typeDefStack.Add(parentTypeDef);
// 				continue;
// 			}

			if (checkTypeDef->IsGlobalsContainer())
			{
				if ((typeNameFlags & BfTypeNameFlag_AddGlobalContainerName) != 0)
				{
					str += "G$";
					str += checkTypeDef->mProject->mName;
				}
			}
			else
			{
				checkTypeDef->mName->ToString(str);
				if (!checkTypeDef->mGenericParamDefs.IsEmpty())
				{
					for (int ofs = 0; ofs < 3; ofs++)
					{
						int checkIdx = (int)str.length() - 1 - ofs;
						if (checkIdx < 0)
							break;
						if (str[checkIdx] == '`')
						{
							str.RemoveToEnd(checkIdx);
							break;
						}
					}
				}

				if (((typeNameFlags & BfTypeNameFlag_DisambiguateDups) != 0) && (checkTypeDef->mDupDetectedRevision != -1))
				{
					str += StrFormat("_%p", checkTypeDef);
				}
			}

			int prevGenericParamCount = 0;

			if (checkTypeDef->mOuterType != NULL)
			{
				prevGenericParamCount = (int)checkTypeDef->mOuterType->mGenericParamDefs.size();
			}

			if (resolvedType->IsGenericTypeInstance())
			{
				auto genericTypeInst = (BfGenericTypeInstance*)resolvedType;
				if (prevGenericParamCount != (int)checkTypeDef->mGenericParamDefs.size())
				{
					str += '<';
					for (int i = prevGenericParamCount; i < (int)checkTypeDef->mGenericParamDefs.size(); i++)
					{
						BfType* typeGenericArg = genericTypeInst->mTypeGenericArguments[i];
						if (typeGenericArg->IsGenericParam())
						{
							if ((typeNameFlags & BfTypeNameFlag_ResolveGenericParamNames) == 0)
							{
								// We don't want the param names, just the commas (this is an unspecialized type reference)
								if (i > prevGenericParamCount)
									str += ',';
								continue;
							}
						}

						if (i > prevGenericParamCount)
							str += ", ";
						DoTypeToString(str, typeGenericArg, (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType)), genericMethodNameOverrides);
					}
					str += '>';
				}
			}

			if (depth > 0)
				str += '.';
		};

		return;
	}
	else if (resolvedType->IsPrimitiveType())
	{
		auto primitiveType = (BfPrimitiveType*)resolvedType;
		if (!primitiveType->mTypeDef->mNamespace.IsEmpty())
		{
			primitiveType->mTypeDef->mNamespace.ToString(str);
			str += '.';
			primitiveType->mTypeDef->mName->ToString(str);
			return;
		}
		else
		{
			primitiveType->mTypeDef->mName->ToString(str);
			return;
		}
	}
	else if (resolvedType->IsPointer())
	{
		auto pointerType = (BfPointerType*)resolvedType;
		DoTypeToString(str, pointerType->mElementType, typeNameFlags, genericMethodNameOverrides);
		str += '*';
		return;
	}
	else if (resolvedType->IsGenericParam())
	{
		bool doResolveGenericParams = (typeNameFlags & BfTypeNameFlag_ResolveGenericParamNames) != 0;
		if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsUnspecializedTypeVariation()))
			doResolveGenericParams = false;

		auto genericParam = (BfGenericParamType*)resolvedType;
		if (genericParam->mGenericParamKind == BfGenericParamKind_Method)
		{
			if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecializedVariation))
				doResolveGenericParams = false;
		}
		
		if (!doResolveGenericParams)
		{
			if (genericParam->mGenericParamKind == BfGenericParamKind_Method)
			{
				if (genericMethodNameOverrides != NULL)
				{
					BF_ASSERT(genericParam->mGenericParamIdx < genericMethodNameOverrides->mSize);
					if (genericParam->mGenericParamIdx < genericMethodNameOverrides->mSize)
					{
						str += (*genericMethodNameOverrides)[genericParam->mGenericParamIdx];
						return;
					}
				}				
				str += StrFormat("@M%d", genericParam->mGenericParamIdx);
				return;
			}
			str += StrFormat("@T%d", genericParam->mGenericParamIdx);
			return;
		}

		if ((genericParam->mGenericParamKind == BfGenericParamKind_Type) && (mCurTypeInstance == NULL))
		{
			str += StrFormat("@T%d", genericParam->mGenericParamIdx);
			return;
		}

		if (genericParam->mGenericParamKind == BfGenericParamKind_Method)
		{
			if (genericMethodNameOverrides != NULL)
			{
				str += (*genericMethodNameOverrides)[genericParam->mGenericParamIdx];
				return;
			}

			if (mCurMethodInstance == NULL)
			{
				str += StrFormat("@M%d", genericParam->mGenericParamIdx);
				return;
			}
		}

		//TEMPORARY
		if (genericParam->mGenericParamKind == BfGenericParamKind_Type)
		{
			auto curTypeInstance = mCurTypeInstance;
			if (mCurMethodInstance != NULL)
				curTypeInstance = mCurMethodInstance->mMethodInstanceGroup->mOwner;
			if ((curTypeInstance == NULL) || (!curTypeInstance->IsGenericTypeInstance()))
			{
				str += StrFormat("@T%d", genericParam->mGenericParamIdx);
				return;
			}
		}

		auto genericParamInstance = GetGenericParamInstance(genericParam);
		auto genericParamDef = genericParamInstance->GetGenericParamDef();
		if (genericParamDef != NULL)
			str += genericParamInstance->GetGenericParamDef()->mName;
		else
			str += "external generic " + TypeToString(genericParamInstance->mExternType, typeNameFlags, genericMethodNameOverrides);
		return;
	}
	else if (resolvedType->IsRef())
	{
		auto refType = (BfRefType*)resolvedType;
		if (refType->mRefKind == BfRefType::RefKind_Ref)
		{
			str += "ref ";
			DoTypeToString(str, refType->mElementType, typeNameFlags, genericMethodNameOverrides);
			return;
		}
		else if (refType->mRefKind == BfRefType::RefKind_Out)
		{
			str += "out ";
			DoTypeToString(str, refType->mElementType, typeNameFlags, genericMethodNameOverrides);
			return;
		}
		else
		{
			str += "mut ";
			DoTypeToString(str, refType->mElementType, typeNameFlags, genericMethodNameOverrides);
			return;
		}
	}
	else if (resolvedType->IsRetTypeType())
	{
		auto retTypeType = (BfRetTypeType*)resolvedType;
		str += "rettype(";
		DoTypeToString(str, retTypeType->mElementType, typeNameFlags, genericMethodNameOverrides);
		str += ")";
		return;
	}
	else if (resolvedType->IsConcreteInterfaceType())
	{
		auto concreteTypeType = (BfConcreteInterfaceType*)resolvedType;
		str += "concrete ";
		DoTypeToString(str, concreteTypeType->mInterface, typeNameFlags, genericMethodNameOverrides);
		return;
	}
	else if (resolvedType->IsUnknownSizedArray())
	{
		auto arrayType = (BfUnknownSizedArrayType*)resolvedType;
		DoTypeToString(str, arrayType->mElementType, typeNameFlags, genericMethodNameOverrides);
		str += "[";
		DoTypeToString(str, arrayType->mElementCountSource, typeNameFlags, genericMethodNameOverrides);
		str += "]";
		return;
	}
	else if (resolvedType->IsSizedArray())
	{
		auto arrayType = (BfSizedArrayType*)resolvedType;
		if (arrayType->mElementCount == -1)
		{
			DoTypeToString(str, arrayType->mElementType, typeNameFlags, genericMethodNameOverrides);
			str += "[?]";
			return;
		}
		DoTypeToString(str, arrayType->mElementType, typeNameFlags, genericMethodNameOverrides);
		str += StrFormat("[%d]", arrayType->mElementCount);
		return;
	}
	else if (resolvedType->IsConstExprValue())
	{
		auto constExprValueType = (BfConstExprValueType*)resolvedType;
		str += "const ";

		DoTypeToString(str, constExprValueType->mType, typeNameFlags, genericMethodNameOverrides);
		str += " ";

		VariantToString(str, constExprValueType->mValue);
		
		return;
	}
	BF_FATAL("Not implemented");
	str += "???";
	return;
}

