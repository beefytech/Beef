#include "BeefySysLib/util/AllocDebug.h"

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfReducer.h"
#include "BfCodeGen.h"
#include "BfExprEvaluator.h"
#include <fcntl.h>
#include "BfConstResolver.h"
#include "BfMangler.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/StackHelper.h"
#include "BfSourceClassifier.h"
#include "BfAutoComplete.h"
#include "BfDemangler.h"
#include "BfResolvePass.h"
#include "BfFixits.h"
#include "BfIRCodeGen.h"
#include "BfDefBuilder.h"
#include "CeMachine.h"

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

BfGenericExtensionEntry* BfModule::BuildGenericExtensionInfo(BfTypeInstance* genericTypeInst, BfTypeDef* partialTypeDef)
{
	if (!partialTypeDef->IsExtension())
		return NULL;

	if (partialTypeDef->mGenericParamDefs.size() != genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size())
	{
		AssertErrorState();
		return NULL;
	}

	BfGenericExtensionInfo* genericExtensionInfo = genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo;
	if (genericExtensionInfo == NULL)
	{
		genericExtensionInfo = new BfGenericExtensionInfo();
		genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo = genericExtensionInfo;
	}

	BfTypeState typeState;
	typeState.mPrevState = mContext->mCurTypeState;
	typeState.mType = genericTypeInst;
	typeState.mCurTypeDef = partialTypeDef;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	BfGenericExtensionEntry* genericExEntry;
	genericExtensionInfo->mExtensionMap.TryAdd(partialTypeDef, NULL, &genericExEntry);

	int startDefGenericParamIdx = (int)genericExEntry->mGenericParams.size();
	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = new BfGenericTypeParamInstance(partialTypeDef, paramIdx);
		genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);

		auto prevPtr = genericExEntry->mGenericParams.mVals;
		genericExEntry->mGenericParams.push_back(genericParamInstance);
	}

	for (int externConstraintIdx = 0; externConstraintIdx < (int)partialTypeDef->mExternalConstraints.size(); externConstraintIdx++)
	{
		auto& genericConstraint = partialTypeDef->mExternalConstraints[externConstraintIdx];

		auto genericParamInstance = new BfGenericTypeParamInstance(partialTypeDef, externConstraintIdx + (int)partialTypeDef->mGenericParamDefs.size());
		genericParamInstance->mExternType = ResolveTypeRef(genericConstraint.mTypeRef, BfPopulateType_Identity);

		auto autoComplete = mCompiler->GetAutoComplete();
		if (autoComplete != NULL)
			autoComplete->CheckTypeRef(genericConstraint.mTypeRef, false);

		if (genericParamInstance->mExternType == NULL)
			genericParamInstance->mExternType = GetPrimitiveType(BfTypeCode_Var);

		ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType());
		genericExEntry->mGenericParams.push_back(genericParamInstance);
	}

	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = genericExEntry->mGenericParams[paramIdx];

		auto rootGenericParamInstance = genericTypeInst->mGenericTypeInfo->mGenericParams[paramIdx];
		genericParamInstance->mTypeConstraint = rootGenericParamInstance->mTypeConstraint;
		genericParamInstance->mInterfaceConstraints = rootGenericParamInstance->mInterfaceConstraints;
		genericParamInstance->mGenericParamFlags = (BfGenericParamFlags)(genericParamInstance->mGenericParamFlags | rootGenericParamInstance->mGenericParamFlags);

		ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType());
	}

	for (auto genericParam : genericExEntry->mGenericParams)
		AddDependency(genericParam, mCurTypeInstance);

	ValidateGenericParams(BfGenericParamKind_Type,
		Span<BfGenericParamInstance*>((BfGenericParamInstance**)genericExEntry->mGenericParams.mVals,
			genericExEntry->mGenericParams.mSize));

	return genericExEntry;
}

bool BfModule::InitGenericParams(BfType* resolvedTypeRef)
{
	BfTypeState typeState;
	typeState.mPrevState = mContext->mCurTypeState;
	typeState.mResolveKind = BfTypeState::ResolveKind_BuildingGenericParams;
	typeState.mType = resolvedTypeRef;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	BF_ASSERT(mCurMethodInstance == NULL);

	auto genericTypeInst = resolvedTypeRef->ToGenericTypeInstance();
	genericTypeInst->mGenericTypeInfo->mInitializedGenericParams = true;

	if (genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.IsEmpty())
		return true;

	if (genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[0]->IsGenericParam())
	{
		BF_ASSERT(genericTypeInst->mGenericTypeInfo->mIsUnspecialized);
	}

	auto typeDef = genericTypeInst->mTypeDef;
	int startDefGenericParamIdx = (int)genericTypeInst->mGenericTypeInfo->mGenericParams.size();
	for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size(); paramIdx++)
	{
		auto genericParamInstance = new BfGenericTypeParamInstance(typeDef, paramIdx);
		genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);
		genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(genericParamInstance);
	}

	for (int externConstraintIdx = 0; externConstraintIdx < (int)typeDef->mExternalConstraints.size(); externConstraintIdx++)
	{
		auto genericParamInstance = new BfGenericTypeParamInstance(typeDef, externConstraintIdx + (int)typeDef->mGenericParamDefs.size());
		genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(genericParamInstance);
	}

	return true;
}

bool BfModule::FinishGenericParams(BfType* resolvedTypeRef)
{
	BfTypeState typeState;
	typeState.mPrevState = mContext->mCurTypeState;
	typeState.mResolveKind = BfTypeState::ResolveKind_BuildingGenericParams;
	typeState.mType = resolvedTypeRef;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
	Array<BfTypeReference*> deferredResolveTypes;

	BF_ASSERT(mCurMethodInstance == NULL);

	auto genericTypeInst = resolvedTypeRef->ToGenericTypeInstance();
	genericTypeInst->mGenericTypeInfo->mFinishedGenericParams = true;

	if (genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[0]->IsGenericParam())
	{
		BF_ASSERT(genericTypeInst->mGenericTypeInfo->mIsUnspecialized);
	}

	auto typeDef = genericTypeInst->mTypeDef;
	int startDefGenericParamIdx = (int)genericTypeInst->mGenericTypeInfo->mGenericParams.size();

	if ((!resolvedTypeRef->IsTuple()) && (!resolvedTypeRef->IsDelegateFromTypeRef()) && (!resolvedTypeRef->IsFunctionFromTypeRef()))
	{
		startDefGenericParamIdx = startDefGenericParamIdx -
			(int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size() -
			(int)typeDef->mExternalConstraints.size();
	}
	BF_ASSERT(startDefGenericParamIdx >= 0);

	if (!typeDef->mPartials.empty())
	{
		BitSet prevConstraintsPassedSet;

		if (!genericTypeInst->IsUnspecializedType())
		{
			if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
			{
				auto genericExtensionInfo = genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo;
				prevConstraintsPassedSet = genericExtensionInfo->mConstraintsPassedSet;
				genericExtensionInfo->mConstraintsPassedSet.Clear();
			}
		}

		int extensionCount = 0;

		BfLogSysM("BfModule::FinishGenericParams %p\n", resolvedTypeRef);
		for (auto partialTypeDef : typeDef->mPartials)
		{
			if (!partialTypeDef->IsExtension())
			{
				typeState.mCurTypeDef = partialTypeDef;
				for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mGenericParams.size(); paramIdx++)
				{
					auto genericParamInstance = genericTypeInst->mGenericTypeInfo->mGenericParams[paramIdx];
					auto genericParamDef = genericParamInstance->GetGenericParamDef();

					if (paramIdx < (int)typeDef->mGenericParamDefs.size())
					{
						genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);
					}
					else
					{
						auto externConstraintDef = genericParamInstance->GetExternConstraintDef();
						genericParamInstance->mExternType = ResolveTypeRef(externConstraintDef->mTypeRef);
						if (genericParamInstance->mExternType == NULL)
							genericParamInstance->mExternType = GetPrimitiveType(BfTypeCode_Var);
					}

					ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType(), &deferredResolveTypes);

					if (genericParamDef != NULL)
					{
						for (auto nameNode : genericParamDef->mNameNodes)
						{
							HandleTypeGenericParamRef(nameNode, typeDef, paramIdx);
						}
					}
				}
			}
			else
			{
				auto genericExEntry = BuildGenericExtensionInfo(genericTypeInst, partialTypeDef);
				if (genericExEntry == NULL)
					continue;

				auto genericExtensionInfo = genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo;
				if (extensionCount == 0)
					genericExtensionInfo->mConstraintsPassedSet.Resize(typeDef->mPartials.mSize);

				extensionCount++;

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

						BfType* genericArg;
						if (paramIdx < (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size())
						{
							genericArg = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[paramIdx];
						}
						else
						{
							genericArg = genericParamInstance->mExternType;
						}

						if ((genericArg == NULL) || (!CheckGenericConstraints(genericParamSource, genericArg, NULL, genericParamInstance, NULL, &error)))
						{
							genericExEntry->mConstraintsPassed = false;
						}
					}
				}

				if (genericExEntry->mConstraintsPassed)
					genericExtensionInfo->mConstraintsPassedSet.Set(partialTypeDef->mPartialIdx);

				BfLogSysM("BfModule::FinishGenericParams %p partialTypeDef:%p passed:%d\n", resolvedTypeRef, partialTypeDef, genericExEntry->mConstraintsPassed);
			}
		}

		auto genericExtensionInfo = genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo;
		if ((extensionCount > 0) && (!prevConstraintsPassedSet.IsEmpty()) && (genericExtensionInfo->mConstraintsPassedSet != prevConstraintsPassedSet))
		{
			mContext->QueueMidCompileRebuildDependentTypes(genericTypeInst, "mConstraintsPassedSet changed");
		}
	}
	else
	{
		for (int paramIdx = startDefGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mGenericParams.size(); paramIdx++)
		{
			auto genericParamInstance = genericTypeInst->mGenericTypeInfo->mGenericParams[paramIdx];

			if (paramIdx < (int)typeDef->mGenericParamDefs.size())
			{
				genericParamInstance->mExternType = GetGenericParamType(BfGenericParamKind_Type, paramIdx);
			}
			else
			{
				auto externConstraintDef = genericParamInstance->GetExternConstraintDef();
				genericParamInstance->mExternType = ResolveTypeRef(externConstraintDef->mTypeRef);

				auto autoComplete = mCompiler->GetAutoComplete();
				if (autoComplete != NULL)
					autoComplete->CheckTypeRef(externConstraintDef->mTypeRef, false);

				if (genericParamInstance->mExternType != NULL)
				{
					//
				}
				else
					genericParamInstance->mExternType = GetPrimitiveType(BfTypeCode_Var);
			}

			ResolveGenericParamConstraints(genericParamInstance, genericTypeInst->IsUnspecializedType(), &deferredResolveTypes);
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

	for (auto typeRef : deferredResolveTypes)
		auto constraintType = ResolveTypeRef(typeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_None);

	ValidateGenericParams(BfGenericParamKind_Type,
		Span<BfGenericParamInstance*>((BfGenericParamInstance**)genericTypeInst->mGenericTypeInfo->mGenericParams.mVals,
			genericTypeInst->mGenericTypeInfo->mGenericParams.mSize));

	for (auto genericParam : genericTypeInst->mGenericTypeInfo->mGenericParams)
		AddDependency(genericParam, mCurTypeInstance);

	return true;
}

void BfModule::ValidateGenericParams(BfGenericParamKind genericParamKind, Span<BfGenericParamInstance*> genericParams)
{
	std::function<void(BfType*, Array<BfGenericParamType*>&)> _CheckType = [&](BfType* type, Array<BfGenericParamType*>& foundParams)
	{
		if (type == NULL)
			return;
		if (!type->IsGenericParam())
			return;
		auto genericParamType = (BfGenericParamType*)type;
		if (genericParamType->mGenericParamKind != genericParamKind)
			return;

		auto genericParam = genericParams[genericParamType->mGenericParamIdx];
		if (genericParam->mTypeConstraint == NULL)
			return;

		if (foundParams.Contains(genericParamType))
		{
			String error = "Circular constraint dependency between ";
			for (int i = 0; i < foundParams.mSize; i++)
			{
				auto foundParam = foundParams[i];
				if (i > 0)
					error += " and ";
				error += TypeToString(foundParam, BfTypeNameFlag_ResolveGenericParamNames);

				// Remove errored type constraint
				genericParams[foundParam->mGenericParamIdx]->mTypeConstraint = NULL;
			}

			if (foundParams.mSize == 1)
				error += " and itself";

			Fail(error, genericParams[genericParamType->mGenericParamIdx]->GetRefNode());
			return;
		}

		foundParams.Add(genericParamType);
		_CheckType(genericParam->mTypeConstraint, foundParams);
		foundParams.pop_back();
	};

	for (auto genericParam : genericParams)
	{
		if (genericParam->mTypeConstraint != NULL)
		{
			Array<BfGenericParamType*> foundParams;
			_CheckType(genericParam->mTypeConstraint, foundParams);
		}
	}
}

bool BfModule::ValidateGenericConstraints(BfAstNode* typeRef, BfTypeInstance* genericTypeInst, bool ignoreErrors)
{
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsTypeAlias()) && (mCurTypeInstance->IsGenericTypeInstance()))
	{
		// Don't validate constraints during the population of a concrete generic type alias instance, we want to
		//  throw those errors at the usage sites
		return true;
	}

	// We don't validate constraints for things like Tuples/Delegates
	if (genericTypeInst->IsOnDemand())
		return true;

	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, mIgnoreErrors || ignoreErrors);
	genericTypeInst->mGenericTypeInfo->mValidatedGenericConstraints = true;
	if (!genericTypeInst->mGenericTypeInfo->mFinishedGenericParams)
		PopulateType(genericTypeInst, BfPopulateType_Interfaces_All);

	if (genericTypeInst->IsTypeAlias())
	{
		auto underlyingType = genericTypeInst->GetUnderlyingType();
		if ((underlyingType != NULL) && (underlyingType->IsGenericTypeInstance()))
		{
			auto underlyingGenericType = underlyingType->ToGenericTypeInstance();
			PopulateType(underlyingType, BfPopulateType_Declaration);
			bool result = ValidateGenericConstraints(typeRef, underlyingGenericType, ignoreErrors);
			if (underlyingGenericType->mGenericTypeInfo->mHadValidateErrors)
				genericTypeInst->mGenericTypeInfo->mHadValidateErrors = true;
			return result;
		}
		return true;
	}

	for (auto typeArg : genericTypeInst->mGenericTypeInfo->mTypeGenericArguments)
	{
		auto genericArg = typeArg->ToGenericTypeInstance();
		if (genericArg != NULL)
			genericTypeInst->mGenericTypeInfo->mMaxGenericDepth = BF_MAX(genericTypeInst->mGenericTypeInfo->mMaxGenericDepth, genericArg->mGenericTypeInfo->mMaxGenericDepth + 1);
	}

	auto typeDef = genericTypeInst->mTypeDef;

	int startGenericParamIdx = 0;
	if (typeDef->mOuterType != NULL)
	{
		startGenericParamIdx = typeDef->mOuterType->mGenericParamDefs.mSize + typeDef->mOuterType->mExternalConstraints.mSize;
		auto outerType = GetOuterType(genericTypeInst);
		PopulateType(outerType, BfPopulateType_Declaration);
		if ((outerType->mGenericTypeInfo != NULL) && (outerType->mGenericTypeInfo->mHadValidateErrors))
			genericTypeInst->mGenericTypeInfo->mHadValidateErrors = true;
	}

	for (int paramIdx = startGenericParamIdx; paramIdx < (int)genericTypeInst->mGenericTypeInfo->mGenericParams.size(); paramIdx++)
	{
		auto genericParamInstance = genericTypeInst->mGenericTypeInfo->mGenericParams[paramIdx];

		BfType* genericArg;
		if (paramIdx < (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size())
		{
			genericArg = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[paramIdx];
		}
		else
		{
			genericArg = genericParamInstance->mExternType;
		}

		BfError* error = NULL;
		if ((genericArg == NULL) || (!CheckGenericConstraints(BfGenericParamSource(genericTypeInst), genericArg, typeRef, genericParamInstance, NULL, &error)))
		{
			if (!genericTypeInst->IsUnspecializedTypeVariation())
				genericTypeInst->mGenericTypeInfo->mHadValidateErrors = true;
			return false;
		}
	}

	return true;
}

BfType* BfModule::ResolveGenericMethodTypeRef(BfTypeReference* typeRef, BfMethodInstance* methodInstance, BfGenericParamInstance* genericParamInstance, BfTypeVector* methodGenericArgsOverride)
{
	BfConstraintState constraintSet;
	constraintSet.mPrevState = mContext->mCurConstraintState;
	constraintSet.mGenericParamInstance = genericParamInstance;
	constraintSet.mMethodInstance = methodInstance;
	constraintSet.mMethodGenericArgsOverride = methodGenericArgsOverride;

	SetAndRestoreValue<BfConstraintState*> prevConstraintSet(mContext->mCurConstraintState, &constraintSet);
	if (!CheckConstraintState(NULL))
		return NULL;

	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, methodInstance->GetOwner());
	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);

	BfType* type = ResolveTypeRef(typeRef);
	if (type == NULL)
		type = GetPrimitiveType(BfTypeCode_Var);
	return type;
}

bool BfModule::AreConstraintsSubset(BfGenericParamInstance* checkInner, BfGenericParamInstance* checkOuter)
{
	if (checkOuter == NULL)
		return false;
	if (checkInner == NULL)
		return true;

	// Added new flags?
	if ((checkInner->mGenericParamFlags | checkOuter->mGenericParamFlags) != checkOuter->mGenericParamFlags)
	{
		// If the outer had a type flag and the inner has a specific type constraint, then see if those are compatible
		auto outerFlags = checkOuter->mGenericParamFlags;
		if ((outerFlags & BfGenericParamFlag_Enum) != 0)
			outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_Struct);

		if (checkOuter->mTypeConstraint != NULL)
		{
			if (checkOuter->mTypeConstraint->IsStruct())
				outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_Struct);
			else if (checkOuter->mTypeConstraint->IsStructOrStructPtr())
				outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_StructPtr);
			else if ((checkOuter->mTypeConstraint->IsObject()) && (!checkOuter->mTypeConstraint->IsDelegate()))
				outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_Class);
			else if (checkOuter->mTypeConstraint->IsEnum())
				outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_Enum | BfGenericParamFlag_Struct);
			else if (checkOuter->mTypeConstraint->IsInterface())
				outerFlags = (BfGenericParamFlags)(outerFlags | BfGenericParamFlag_Interface);
		}

		auto innerFlags = checkInner->mGenericParamFlags;
		if ((innerFlags & BfGenericParamFlag_Enum) != 0)
			innerFlags = (BfGenericParamFlags)(innerFlags | BfGenericParamFlag_Struct);

		if (((innerFlags | outerFlags) & ~BfGenericParamFlag_Var) != (outerFlags & ~BfGenericParamFlag_Var))
			return false;
	}

	if (checkInner->mTypeConstraint != NULL)
	{
		if (checkOuter->mTypeConstraint == NULL)
			return false;
		if (!TypeIsSubTypeOf(checkOuter->mTypeConstraint->ToTypeInstance(), checkInner->mTypeConstraint->ToTypeInstance()))
			return false;
	}

	for (auto innerIFace : checkInner->mInterfaceConstraints)
	{
		if (checkOuter->mInterfaceConstraints.IsEmpty())
			return false;

		if (checkOuter->mInterfaceConstraintSet == NULL)
		{
			std::function<void(BfTypeInstance*)> _AddInterface = [&](BfTypeInstance* ifaceType)
			{
				if (!checkOuter->mInterfaceConstraintSet->Add(ifaceType))
					return;
				if (ifaceType->mDefineState < BfTypeDefineState_HasInterfaces_Direct)
					PopulateType(ifaceType, Beefy::BfPopulateType_Interfaces_Direct);
				for (auto& ifaceEntry : ifaceType->mInterfaces)
					_AddInterface(ifaceEntry.mInterfaceType);
			};

			checkOuter->mInterfaceConstraintSet = new HashSet<BfTypeInstance*>();
			for (auto outerIFace : checkOuter->mInterfaceConstraints)
				_AddInterface(outerIFace);
		}

		if (!checkOuter->mInterfaceConstraintSet->Contains(innerIFace))
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

		auto genericTypeInst = (BfTypeInstance*)typeInst;
		if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
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
		auto typeDef = typeInstance->mTypeDef;
		if (typeDef->mEmitParent != NULL)
			typeDef = typeDef->mEmitParent;
		if (typeDef->mNextRevision != NULL)
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
					mContext->HandleChangedTypeDef(typeDef);
					mSystem->InjectNewRevision(typeDef);
				}
			}
			else
			{
				BF_ASSERT(mCompiler->IsAutocomplete());
			}
		}
		if ((!typeInstance->IsDeleting()) && (!mCompiler->IsAutocomplete()))
			BF_ASSERT((typeDef->mDefState == BfTypeDef::DefState_Defined) || (typeDef->mDefState == BfTypeDef::DefState_New));

 		if ((typeInstance->mTypeDef->mDefState == BfTypeDef::DefState_EmittedDirty) && (typeInstance->mTypeDef->mEmitParent->mNextRevision == NULL))
 			mSystem->UpdateEmittedTypeDef(typeInstance->mTypeDef);
	}
}

void BfModule::InitType(BfType* resolvedTypeRef, BfPopulateType populateType)
{
	BP_ZONE("BfModule::InitType");

	if (auto depType = resolvedTypeRef->ToDependedType())
	{
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL))
		{
			depType->mDependencyMap.mMinDependDepth = mCurMethodInstance->mMethodInfoEx->mMinDependDepth + 1;
		}
		else if (mCurTypeInstance != NULL)
		{
			depType->mDependencyMap.mMinDependDepth = mCurTypeInstance->mDependencyMap.mMinDependDepth + 1;
		}
	}

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, resolvedTypeRef->ToTypeInstance());
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

	if (mCompiler->mHotState != NULL)
		mCompiler->mHotState->mHasNewTypes = true;

	auto typeInst = resolvedTypeRef->ToTypeInstance();
	if (typeInst != NULL)
	{
		CheckInjectNewRevision(typeInst);

		BF_ASSERT(!typeInst->mTypeDef->IsEmitted());

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

		typeInst->mRevision = mCompiler->mRevision;
		if (typeInst->mTypeDef != NULL)
			BF_ASSERT(typeInst->mTypeDef->mDefState != BfTypeDef::DefState_Deleted);

		if (resolvedTypeRef->IsTuple())
		{
			auto tupleType = (BfTypeInstance*)resolvedTypeRef;
			for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
			{
				auto fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
				// We need to make sure dependencies get set immediately since we already resolved the types
				AddFieldDependency(typeInst, fieldInstance, fieldInstance->mResolvedType);
			}
		}
	}

	if (resolvedTypeRef->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfTypeInstance*)resolvedTypeRef;
		for (auto typeGenericArg : genericTypeInst->mGenericTypeInfo->mTypeGenericArguments)
		{
			BF_ASSERT((typeGenericArg->mRebuildFlags & BfTypeRebuildFlag_Deleted) == 0);
			if (mIsReified)
			{
				// Try to reify any generic args
				for (auto genericArg : typeInst->mGenericTypeInfo->mTypeGenericArguments)
				{
					if (!genericArg->IsReified())
						PopulateType(genericArg, BfPopulateType_Declaration);
				}
			}
			BF_ASSERT(!typeGenericArg->IsIntUnknown());
		}
	}

	if (!mContext->mSavedTypeDataMap.IsEmpty())
	{
		String typeName = BfSafeMangler::Mangle(resolvedTypeRef, this);
		BfSavedTypeData* savedTypeData;
		if (mContext->mSavedTypeDataMap.Remove(typeName, &savedTypeData))
		{
			mContext->mSavedTypeData[savedTypeData->mTypeId] = NULL;

			resolvedTypeRef->mTypeId = savedTypeData->mTypeId;

			BfLogSysM("Using mSavedTypeData for %p %s\n", resolvedTypeRef, typeName.c_str());
			if (typeInst != NULL)
			{
				if (IsHotCompile())
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
		auto genericTypeInstance = (BfTypeInstance*)resolvedTypeRef;

#ifdef _DEBUG
		for (auto genericArg : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
			BF_ASSERT(!genericArg->IsVar());
#endif

		// We need to add generic dependencies here because when we are just doing an Identity population there may be
		//  on-demand types that could get deleted before initializing the type
		DoPopulateType_SetGenericDependencies(genericTypeInstance);

		// Do it here so the location we attempted to specialize this type will throw the failure if there is one
 		if (!InitGenericParams(resolvedTypeRef))
  			return;
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
	PopulateType(resolvedTypeRef, populateType);
}

void BfModule::AddFieldDependency(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfType* fieldType)
{
	auto depFlag = fieldType->IsValueType() ? BfDependencyMap::DependencyFlag_ValueTypeMemberData : BfDependencyMap::DependencyFlag_PtrMemberData;
	if (fieldInstance->IsAppendedObject())
		depFlag = BfDependencyMap::DependencyFlag_ValueTypeMemberData;

	AddDependency(fieldType, typeInstance, depFlag);

	if ((fieldType->IsStruct()) && (fieldType->IsGenericTypeInstance()))
	{
		// When we're a generic struct, our data layout can depend on our generic parameters as well
		auto genericTypeInstance = (BfTypeInstance*)fieldType;
		for (auto typeGenericArg : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
			AddFieldDependency(typeInstance, fieldInstance, typeGenericArg);
	}
}

BfFieldInstance* BfModule::GetFieldByName(BfTypeInstance* typeInstance, const StringImpl& fieldName, bool isRequired, BfAstNode* refNode)
{
	PopulateType(typeInstance);
	typeInstance->mTypeDef->PopulateMemberSets();
	BfMemberSetEntry* entry = NULL;
	BfFieldDef* fieldDef = NULL;
	if (typeInstance->mTypeDef->mFieldSet.TryGetWith(fieldName, &entry))
	{
		fieldDef = (BfFieldDef*)entry->mMemberDef;
		return &typeInstance->mFieldInstances[fieldDef->mIdx];
	}

	if (isRequired)
	{
		FailInternal(StrFormat("Field '%s' not found in '%s'", fieldName.c_str(), TypeToString(typeInstance).c_str()), refNode);
	}

	return NULL;
}

void BfModule::CheckMemberNames(BfTypeInstance* typeInst)
{
	struct MemberRef
	{
		BfMemberDef* mMemberDef;
		StringView mName;
		StringView mKindName;
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

			MemberRef memberRef = { 0 };
			memberRef.mMemberDef = prop;
			memberRef.mTypeInst = checkType;
			memberRef.mProtection = prop->mProtection;
			memberRef.mName = prop->mName;
			memberRef.mKindName = "property";
			auto fieldDecl = prop->GetFieldDeclaration();
			if (fieldDecl != NULL)
				memberRef.mNameNode = fieldDecl->mNameNode;
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

			MemberRef memberRef = { 0 };
			memberRef.mMemberDef = field;
			memberRef.mTypeInst = checkType;
			memberRef.mProtection = field->mProtection;
			memberRef.mName = field->mName;
			memberRef.mKindName = "field";
			memberRef.mDeclaringType = field->mDeclaringType;
			if (auto fieldDecl = field->GetFieldDeclaration())
			{
				memberRef.mNameNode = fieldDecl->mNameNode;
				memberRef.mIsOverride = fieldDecl->mNewSpecifier != NULL;
			}
			else if (auto paramDecl = field->GetParamDeclaration())
			{
				memberRef.mNameNode = paramDecl->mNameNode;
			}

			memberList.push_back(memberRef);
		}

		checkType = checkType->mBaseType;
	}

	Dictionary<StringView, MemberRef> memberMap;
	memberMap.Reserve(memberList.size());

	for (int i = (int)memberList.size() - 1; i >= 0; i--)
	{
		MemberRef& memberRef = memberList[i];
		if (memberRef.mName.IsEmpty())
			continue;
		if ((memberRef.mTypeInst == typeInst) && (!memberRef.mIsOverride))
		{
			MemberRef* prevMemberRef = NULL;
			if (memberMap.TryGetValue(memberRef.mName, &prevMemberRef))
			{
				if ((prevMemberRef->mDeclaringType->IsExtension()) && (!memberRef.mDeclaringType->IsExtension()))
					continue;

				MemberRef* firstMemberRef = &memberRef;
				MemberRef* secondMemberRef = prevMemberRef;
				bool showPrevious = false;

				BfError* error = NULL;
				if (prevMemberRef->mTypeInst != typeInst)
				{
					if ((prevMemberRef->mProtection != BfProtection_Private) && (memberRef.mNameNode != NULL))
					{
						error = Warn(BfWarning_CS0108_MemberHidesInherited, StrFormat("%s hides inherited member '%s'. Use the 'new' keyword if hiding was intentional.", String(prevMemberRef->mKindName).c_str(), String(memberRef.mName).c_str()), memberRef.mNameNode, true);
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

					if (typeInst->mTypeDef->mIsCombinedPartial)
					{
						if ((firstMemberRef->mKindName == "property") && (secondMemberRef->mKindName == "property"))
						{
							auto firstPropertyDef = (BfPropertyDef*)firstMemberRef->mMemberDef;
							auto secondPropertyDef = (BfPropertyDef*)secondMemberRef->mMemberDef;
							if (auto secondPropertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(secondPropertyDef->mFieldDeclaration))
							{
								if ((secondPropertyDeclaration->mVirtualSpecifier != NULL) && (secondPropertyDeclaration->mVirtualSpecifier->mToken == BfToken_Override))
									continue;
							}
						}
					}

					if (secondMemberRef->mNameNode != NULL)
						error = Fail(StrFormat("A %s named '%s' has already been declared.", String(secondMemberRef->mKindName).c_str(), String(memberRef.mName).c_str()), secondMemberRef->mNameNode, true);
					showPrevious = true;
					typeInst->mHasDeclError = true;
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
	mContext->mFailTypes.TryAdd(typeInstance, BfFailKind_Normal);
	mHadBuildError = true;
}

bool BfModule::CheckCircularDataError(bool failTypes)
{
	// Find two loops of mCurTypeInstance. Just finding one loop can give some false errors.

	BfTypeState* circularTypeStateEnd = NULL;

	int checkIdx = 0;
	auto checkTypeState = mContext->mCurTypeState;
	bool isPreBaseCheck = checkTypeState->mPopulateType == BfPopulateType_Declaration;
	while (true)
	{
		if (checkTypeState == NULL)
			return false;

		if (checkTypeState->mResolveKind == BfTypeState::ResolveKind_UnionInnerType)
		{
			checkTypeState = checkTypeState->mPrevState;
			continue;
		}

		if (isPreBaseCheck)
		{
			if (checkTypeState->mPopulateType != BfPopulateType_Declaration)
				return false;
		}
		else
		{
			if (checkTypeState->mPopulateType == BfPopulateType_Declaration)
				return false;
			if ((checkIdx > 0) && (checkTypeState->mCurBaseTypeRef == NULL) && (checkTypeState->mCurAttributeTypeRef == NULL) && (checkTypeState->mCurFieldDef == NULL) &&
				((checkTypeState->mType == NULL) || (checkTypeState->mType->IsTypeInstance())))
				return false;
		}

		if ((checkTypeState->mType == mCurTypeInstance) && (checkIdx > 1))
		{
			if (circularTypeStateEnd == NULL)
				circularTypeStateEnd = checkTypeState;
			else
				break;
		}
		checkTypeState = checkTypeState->mPrevState;
		checkIdx++;
	}

	bool hadError = false;
	checkTypeState = mContext->mCurTypeState->mPrevState;
	while (true)
	{
		if (checkTypeState == NULL)
			return hadError;

		if (checkTypeState == circularTypeStateEnd)
			return hadError;

		if (checkTypeState->mResolveKind == BfTypeState::ResolveKind_UnionInnerType)
		{
			// Skip over this to actual data references
			checkTypeState = checkTypeState->mPrevState;
			continue;
		}

		if ((checkTypeState->mCurAttributeTypeRef == NULL) && (checkTypeState->mCurBaseTypeRef == NULL) && (checkTypeState->mCurFieldDef == NULL) &&
			((checkTypeState->mType == NULL) || (checkTypeState->mType->IsTypeInstance())))
			return hadError;

		hadError = true;
		if (!failTypes)
			return hadError;

		// We only get one chance to fire off these errors, they can't be ignored.
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, false);

		if (checkTypeState->mCurAttributeTypeRef != NULL)
		{
			Fail(StrFormat("Attribute type '%s' causes a data cycle", BfTypeUtils::TypeToString(checkTypeState->mCurAttributeTypeRef).c_str()), checkTypeState->mCurAttributeTypeRef, true);
		}
		else if (checkTypeState->mCurBaseTypeRef != NULL)
		{
			Fail(StrFormat("Base type '%s' causes a data cycle", BfTypeUtils::TypeToString(checkTypeState->mCurBaseTypeRef).c_str()), checkTypeState->mCurBaseTypeRef, true);
		}
		else if ((checkTypeState->mCurFieldDef != NULL) && (checkTypeState->mCurFieldDef->mFieldDeclaration != NULL))
		{
			Fail(StrFormat("Field '%s.%s' causes a data cycle", TypeToString(checkTypeState->mType).c_str(), checkTypeState->mCurFieldDef->mName.c_str()),
				checkTypeState->mCurFieldDef->mTypeRef, true);
		}
		else if (checkTypeState->mCurFieldDef != NULL)
		{
			Fail(StrFormat("Field '%s.%s' causes a data cycle", TypeToString(checkTypeState->mType).c_str(), checkTypeState->mCurFieldDef->mName.c_str()));
		}
		else
		{
			Fail(StrFormat("Type '%s' causes a data cycle", TypeToString(checkTypeState->mType).c_str()));
		}

		auto typeInstance = checkTypeState->mType->ToTypeInstance();

		auto module = GetModuleFor(checkTypeState->mType);
		if (module != NULL)
			module->TypeFailed(typeInstance);
		else if (typeInstance != NULL)
			typeInstance->mTypeFailed = true;

		checkTypeState = checkTypeState->mPrevState;
	}
}

void BfModule::PopulateType(BfType* resolvedTypeRef, BfPopulateType populateType)
{
	if ((populateType == BfPopulateType_Declaration) && (resolvedTypeRef->mDefineState >= BfTypeDefineState_Declared))
		return;

	if ((resolvedTypeRef->mRebuildFlags & BfTypeRebuildFlag_PendingGenericArgDep) != 0)
	{
		BfLogSysM("PopulateType handling BfTypeRebuildFlag_PendingGenericArgDep for type %p\n", resolvedTypeRef);
		// Reinit dependencies
		resolvedTypeRef->mRebuildFlags = (BfTypeRebuildFlags)(resolvedTypeRef->mRebuildFlags & ~BfTypeRebuildFlag_PendingGenericArgDep);
		DoPopulateType_SetGenericDependencies(resolvedTypeRef->ToTypeInstance());
	}

	// Are we "demanding" to reify a type that is currently resolve-only?
	if ((mIsReified) && (populateType >= BfPopulateType_Declaration))
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

					if (typeInst->mGenericTypeInfo != NULL)
					{
						for (auto genericArg : typeInst->mGenericTypeInfo->mTypeGenericArguments)
						{
							if (!genericArg->IsReified())
								PopulateType(genericArg, BfPopulateType_Declaration);
						}
					}
				}
			}
			else
			{
				if ((typeModule != NULL) && (!typeModule->mIsReified) && (!typeModule->mReifyQueued))
				{
					bool canFastReify = false;
					if (typeModule->mAwaitingInitFinish)
					{
						canFastReify = true;
						for (auto ownedTypes : typeModule->mOwnedTypeInstances)
							if (ownedTypes->mDefineState > BfTypeDefineState_HasInterfaces_Direct)
								canFastReify = false;
					}

					if (canFastReify)
					{
						BfLogSysM("Setting reified type %p in module %p in PopulateType on module awaiting finish\n", resolvedTypeRef, typeModule);
						typeModule->mIsReified = true;
						typeModule->CalcGeneratesCode();
						typeModule->mWantsIRIgnoreWrites = false;
						for (auto ownedTypes : typeModule->mOwnedTypeInstances)
						{
							ownedTypes->mIsReified = true;

							if (ownedTypes->mCustomAttributes != NULL)
							{
								for (auto& attr : ownedTypes->mCustomAttributes->mAttributes)
								{
									if ((attr.mType->mAttributeData != NULL) && ((attr.mType->mAttributeData->mFlags & BfCustomAttributeFlags_ReflectAttribute) != 0))
									{
										// Reify this attribute
										typeModule->PopulateType(attr.mType);
									}
								}
							}
						}
						mCompiler->mStats.mReifiedModuleCount++;
						if (typeModule->mBfIRBuilder != NULL)
						{
							typeModule->mBfIRBuilder->ClearNonConstData();
							typeModule->mBfIRBuilder->mIgnoreWrites = false;
							typeModule->SetupIRBuilder(false);
						}
						else
							typeModule->PrepareForIRWriting(resolvedTypeRef->ToTypeInstance());
					}
					else
					{
						BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));

						BfLogSysM("Queued reification of type %p in module %p in PopulateType\n", resolvedTypeRef, typeModule);

						BF_ASSERT((typeModule != mContext->mUnreifiedModule) && (typeModule != mContext->mScratchModule));

						BF_ASSERT(!typeModule->mIsSpecialModule);
						// This caused issues - we may need to reify a type and then request a method
						typeModule->mReifyQueued = true;
						mContext->mReifyModuleWorkList.Add(typeModule);
						//typeModule->ReifyModule();
					}
				}
			}
		}
		else
		{
			// If we're a type like "A*", make sure we reify "A" if necessary
			auto checkUnderlying = resolvedTypeRef->GetUnderlyingType();
			while (checkUnderlying != NULL)
			{
				auto checkTypeInst = checkUnderlying->ToTypeInstance();
				if (checkTypeInst != NULL)
				{
					if (!checkTypeInst->mIsReified)
						PopulateType(checkTypeInst, BfPopulateType_BaseType);
					break;
				}
				checkUnderlying = checkUnderlying->GetUnderlyingType();
			}
		}
	}

	if (!resolvedTypeRef->IsIncomplete())
		return;

 	if (populateType <= BfPopulateType_TypeDef)
 		return;

	auto typeInstance = resolvedTypeRef->ToTypeInstance();
	CheckInjectNewRevision(typeInstance);

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);

	BF_ASSERT((resolvedTypeRef->mRebuildFlags & (BfTypeRebuildFlag_Deleted | BfTypeRebuildFlag_DeleteQueued)) == 0);

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

		BfTypeDef* typeDef = NULL;
		if (typeInstance != NULL)
			typeDef = typeInstance->mTypeDef;

		auto typeModule = resolvedTypeRef->GetModule();
		if (typeModule != NULL)
			BF_ASSERT(!typeModule->mAwaitingFinish);

		BfLogSysM("PopulateType: %p %s populateType:%d ResolveOnly:%d Reified:%d AutoComplete:%d Ctx:%p Mod:%p TypeId:%d TypeDef:%p\n", resolvedTypeRef, TypeToString(resolvedTypeRef, BfTypeNameFlags_None).c_str(), populateType, mCompiler->mIsResolveOnly, mIsReified, mCompiler->IsAutocomplete(), mContext, resolvedTypeRef->GetModule(), resolvedTypeRef->mTypeId, typeDef);

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
		return;
	}

	if (resolvedTypeRef->IsTypeAlias())
	{
		// Always populate these all the way
		if (populateType != BfPopulateType_IdentityNoRemapAlias)
			populateType = BfPopulateType_Data;
 	}

	if (resolvedTypeRef->IsSizedArray())
	{
		resolvedTypeRef->mRevision = mRevision;

		bool typeFailed = false;

		BfSizedArrayType* arrayType = (BfSizedArrayType*)resolvedTypeRef;
		auto elementType = arrayType->mElementType;
		if (elementType->IsValueType())
		{
			resolvedTypeRef->mDefineState = BfTypeDefineState_ResolvingBaseType;
			BfTypeState typeState(arrayType, mContext->mCurTypeState);
			typeState.mPopulateType = BfPopulateType_Data;
			SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, NULL);

			if (!CheckCircularDataError())
			{
				PopulateType(arrayType->mElementType, BfPopulateType_Data);
			}
			else
			{
				typeFailed = true;
				PopulateType(arrayType->mElementType, BfPopulateType_Identity);
			}
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
			arrayType->mSize = (int)(arrayType->mElementType->GetStride() * arrayType->mElementCount);
			if (arrayType->mElementType->mSize > 0)
			{
				int64 maxElements = 0x7FFFFFFF / arrayType->mElementType->GetStride();
				if (arrayType->mElementCount > maxElements)
				{
					Fail(StrFormat("Array size overflow: %s", TypeToString(arrayType).c_str()));
					arrayType->mSize = 0x7FFFFFFF;
				}
			}
			arrayType->mAlign = std::max((int32)arrayType->mElementType->mAlign, 1);
		}
		else if (arrayType->mElementCount < 0)
		{
			// Unknown size, don't assume it's valueless
			arrayType->mSize = 1;
			arrayType->mAlign = 1;
		}
		else
		{
			arrayType->mSize = 0;
			arrayType->mAlign = 1;
		}

		BF_ASSERT(arrayType->mSize >= 0);

		if (!typeFailed)
			arrayType->mWantsGCMarking = elementType->WantsGCMarking();
		resolvedTypeRef->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
		resolvedTypeRef->mRebuildFlags = BfTypeRebuildFlag_None;

		bool isValueless = arrayType->IsValuelessType();
		return;
	}

	if (isNew)
	{
		BfTypeDef* typeDef = NULL;
		if (typeInstance != NULL)
		{
			if ((populateType == BfPopulateType_Data) && (typeInstance->mNeedsMethodProcessing))
				return;
			typeDef = typeInstance->mTypeDef;
		}

		if (resolvedTypeRef->IsMethodRef())
			return;

		if (resolvedTypeRef->IsPointer())
		{
			BfPointerType* pointerType = (BfPointerType*)resolvedTypeRef;
			if (pointerType->mElementType->IsIncomplete())
				PopulateType(pointerType->mElementType, BfPopulateType_Declaration);
			pointerType->mSize = pointerType->mAlign = mSystem->mPtrSize;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return;
		}

		if (resolvedTypeRef->IsGenericParam())
		{
			BfGenericParamType* genericParamType = (BfGenericParamType*)resolvedTypeRef;
			PopulateType(mContext->mBfObjectType);
			genericParamType->mSize = mContext->mBfObjectType->mSize;
			genericParamType->mAlign = mContext->mBfObjectType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return;
		}

		if (resolvedTypeRef->IsModifiedTypeType())
		{
			BfModifiedTypeType* retTypeType = (BfModifiedTypeType*)resolvedTypeRef;
			BF_ASSERT(retTypeType->mElementType->IsGenericParam());
			resolvedTypeRef->mSize = mContext->mBfObjectType->mSize;
			resolvedTypeRef->mAlign = mContext->mBfObjectType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return;
		}

		if (resolvedTypeRef->IsConcreteInterfaceType())
		{
			BfConcreteInterfaceType* concreteInterfaceType = (BfConcreteInterfaceType*)resolvedTypeRef;
			BF_ASSERT(concreteInterfaceType->mInterface->IsInterface());
			resolvedTypeRef->mSize = mContext->mBfObjectType->mSize;
			resolvedTypeRef->mAlign = mContext->mBfObjectType->mAlign;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return;
		}

		if (resolvedTypeRef->IsConstExprValue())
		{
			resolvedTypeRef->mSize = 0;
			resolvedTypeRef->mAlign = 0;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
			return;
		}

		// The autocomplete pass doesn't need to do the method processing, allow type to be (partially) incomplete
		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) &&
			(typeInstance != NULL) && (typeInstance->mNeedsMethodProcessing) && (!typeInstance->IsDelegate()))
			return;

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
			return;
		case BfTypeCode_Self:
		case BfTypeCode_Dot:
		case BfTypeCode_Var:
		case BfTypeCode_Let:
		{
			primitiveType->mSize = mSystem->mPtrSize;
			primitiveType->mAlign = mSystem->mPtrSize;
			resolvedTypeRef->mDefineState = BfTypeDefineState_Defined;
		}
		return;
		case BfTypeCode_NullPtr:
			primitiveType->mSize = primitiveType->mAlign = mSystem->mPtrSize;
			primitiveType->mDefineState = BfTypeDefineState_Defined;
			return;
		case BfTypeCode_Boolean:
			PRIMITIVE_TYPE("bool", Int1, 1, DW_ATE_boolean);
			return;
		case BfTypeCode_Int8:
			PRIMITIVE_TYPE("sbyte", Int8, 1, DW_ATE_signed);
			return;
		case BfTypeCode_UInt8:
			PRIMITIVE_TYPE("byte", Int8, 1, DW_ATE_unsigned);
			return;
		case BfTypeCode_Int16:
			PRIMITIVE_TYPE("short", Int16, 2, DW_ATE_signed);
			return;
		case BfTypeCode_UInt16:
			PRIMITIVE_TYPE("ushort", Int16, 2, DW_ATE_unsigned);
			return;
		case BfTypeCode_Int32:
			PRIMITIVE_TYPE("int", Int32, 4, DW_ATE_signed);
			return;
		case BfTypeCode_UInt32:
			PRIMITIVE_TYPE("uint", Int32, 4, DW_ATE_unsigned);
			return;
		case BfTypeCode_Int64:
			PRIMITIVE_TYPE("long", Int64, 8, DW_ATE_signed);
			return;
		case BfTypeCode_UInt64:
			PRIMITIVE_TYPE("ulong", Int64, 8, DW_ATE_unsigned);
			return;
		case BfTypeCode_IntPtr:
			if (mSystem->mPtrSize == 4)
			{
				PRIMITIVE_TYPE("intptr", Int32, 4, DW_ATE_signed);
			}
			else
			{
				PRIMITIVE_TYPE("intptr", Int64, 8, DW_ATE_signed);
			}
			return;
		case BfTypeCode_UIntPtr:
			if (mSystem->mPtrSize == 4)
			{
				PRIMITIVE_TYPE("uintptr", Int32, 4, DW_ATE_unsigned);
			}
			else
			{
				PRIMITIVE_TYPE("uintptr", Int64, 8, DW_ATE_unsigned);
			}
			return;
		case BfTypeCode_IntUnknown:
		case BfTypeCode_UIntUnknown:
			return;
		case BfTypeCode_Char8:
			PRIMITIVE_TYPE("char8", Int8, 1, DW_ATE_unsigned_char);
			return;
		case BfTypeCode_Char16:
			PRIMITIVE_TYPE("char16", Int16, 2, DW_ATE_unsigned_char);
			return;
		case BfTypeCode_Char32:
			PRIMITIVE_TYPE("char32", Int32, 4, DW_ATE_unsigned_char);
			return;
		case BfTypeCode_Float:
			PRIMITIVE_TYPE("float", Float, 4, DW_ATE_float);
			return;
		case BfTypeCode_Double:
			PRIMITIVE_TYPE("double", Double, 8, DW_ATE_float);
			return;
		case BfTypeCode_Object:
		case BfTypeCode_Struct:
		case BfTypeCode_Interface:
		case BfTypeCode_Enum:
		case BfTypeCode_TypeAlias:
			// Implemented below
			break;
		case BfTypeCode_Extension:
			// This can only happen if we didn't actually find the type the extension referred to
			break;
		default:
			//NotImpl(resolvedTypeRef->mTypeRef);
			BFMODULE_FATAL(this, "Invalid type");
			return;
		}
		//////////////////////////////////////////////////////////////////////////

		BF_ASSERT(typeInstance != NULL);

		if (!typeInstance->IsArray())
		{
			BF_ASSERT(typeInstance->mTypeDef != mContext->mCompiler->mArray1TypeDef);
		}

		if (mContext->mBfObjectType == NULL)
		{
			if (typeInstance->IsInstanceOf(mCompiler->mBfObjectTypeDef))
				mContext->mBfObjectType = typeInstance;
			else if (mCompiler->mBfObjectTypeDef != NULL)
				ResolveTypeDef(mCompiler->mBfObjectTypeDef);
		}

		if (typeInstance->mModule == NULL)
		{
			// Create a module for this type
			mContext->HandleTypeWorkItem(resolvedTypeRef);
		}
	}

	if (typeInstance == NULL)
		return;

	if (typeInstance->mModule == NULL)
	{
		BF_ASSERT(typeInstance->mTypeFailed);
		return;
	}

	typeInstance->mModule->DoPopulateType(typeInstance, populateType);
}

BfTypeOptions* BfModule::GetTypeOptions(BfTypeDef* typeDef)
{
	if (mContext->mSystem->mTypeOptions.size() == 0)
	{
		return NULL;
	}

	Array<int> matchedIndices;

	if (!mCompiler->mAttributeTypeOptionMap.IsEmpty())
	{
		auto customAttributes = typeDef->mTypeDeclaration->mAttributes;

		while (customAttributes != NULL)
		{
			if (!mCompiler->mAttributeTypeOptionMap.IsEmpty())
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);

				auto typeRef = customAttributes->mAttributeTypeRef;

				// 			StringT<128> attrName;
				// 			for (auto& customAttrs : customAttributes->mAttributeTypeRef)
				// 			{
				// 				attrName.Clear();
				// 				customAttrs.mType->mTypeDef->mFullName.ToString(attrName);
				// 				Array<int>* arrPtr;
				// 				if (mCompiler->mAttributeTypeOptionMap.TryGetValue(attrName, &arrPtr))
				// 				{
				// 					for (auto optionsIdx : *arrPtr)
				// 					{
				// 						matchedIndices.Add(optionsIdx);
				// 					}
				// 				}
				// 			}
			}

			customAttributes = customAttributes->mNextAttribute;
		}
	}

	int typeOptionsCount = (int)mContext->mSystem->mTypeOptions.size();

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

// 	if (typeInstance->IsTypedPrimitive())
// 	{
// 		auto underlyingType = typeInstance->GetUnderlyingType();
// 		if (underlyingType != NULL)
// 		{
// 			String typeName = TypeToString(underlyingType);
// 			_CheckTypeName(typeName);
// 		}
// 		else
// 		{
// 			// Can this only happen for functions that are being extended?
// 		}
// 	}
//
// 	if ((!typeInstance->IsBoxed()) && (typeInstance->mTypeDef == mCompiler->mPointerTTypeDef))
// 	{
// 		BF_ASSERT(typeInstance->IsGenericTypeInstance());
// 		auto innerType = typeInstance->mGenericTypeInfo->mTypeGenericArguments[0];
// 		auto ptrType = CreatePointerType(innerType);
// 		String typeName = TypeToString(ptrType);
// 		_CheckTypeName(typeName);
// 	}

	String typeName = BfTypeUtils::TypeToString(typeDef);
	_CheckTypeName(typeName);

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
			mergedTypeOptions.mAndFlags = first.mAndFlags;
			mergedTypeOptions.mOrFlags = first.mOrFlags;
			mergedTypeOptions.mAllocStackTraceDepth = first.mAllocStackTraceDepth;
			mergedTypeOptions.mReflectMethodFilters = first.mReflectMethodFilters;
			mergedTypeOptions.mReflectMethodAttributeFilters = first.mReflectMethodAttributeFilters;

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

				mergedTypeOptions.mOrFlags = (BfOptionFlags)(mergedTypeOptions.mOrFlags | typeOptions.mOrFlags);
				mergedTypeOptions.mAndFlags = (BfOptionFlags)(mergedTypeOptions.mAndFlags | typeOptions.mOrFlags);

				mergedTypeOptions.mAndFlags = (BfOptionFlags)(mergedTypeOptions.mAndFlags & typeOptions.mAndFlags);
				mergedTypeOptions.mOrFlags = (BfOptionFlags)(mergedTypeOptions.mOrFlags & typeOptions.mAndFlags);

				if (mergedTypeOptions.HasReflectMethodFilters())
				{
					// If merging filter has non-default method flags but no filter then we need to append it as a filtered modification
					if ((!typeOptions.HasReflectMethodFilters()) &&
						(((typeOptions.mAndFlags & BfOptionFlags_Reflect_MethodMask) != BfOptionFlags_Reflect_MethodMask) ||
						 ((typeOptions.mOrFlags & BfOptionFlags_Reflect_MethodMask) != 0)))
					{
						mergedTypeOptions.mReflectMethodFilters.Add({"*", typeOptions.mAndFlags, typeOptions.mOrFlags});
					}

					mergedTypeOptions.mAndFlags = (BfOptionFlags)(mergedTypeOptions.mAndFlags | BfOptionFlags_Reflect_MethodMask);
					mergedTypeOptions.mOrFlags = (BfOptionFlags)(mergedTypeOptions.mOrFlags & ~BfOptionFlags_Reflect_MethodMask);
				}

				if (typeOptions.mAllocStackTraceDepth != -1)
					mergedTypeOptions.mAllocStackTraceDepth = typeOptions.mAllocStackTraceDepth;
				for (auto filter : typeOptions.mReflectMethodFilters)
					mergedTypeOptions.mReflectMethodFilters.Add(filter);
				for (auto filter : typeOptions.mReflectMethodAttributeFilters)
					mergedTypeOptions.mReflectMethodAttributeFilters.Add(filter);
			}
			matchedIdx = typeOptionsCount + (int)mContext->mSystem->mMergedTypeOptions.size();
			mContext->mSystem->mMergedTypeOptions.push_back(mergedTypeOptions);
		}
	}

	return mSystem->GetTypeOptions( matchedIdx);
}

bool BfModule::ApplyTypeOptionMethodFilters(bool includeMethod, BfMethodDef* methodDef, BfTypeOptions* typeOptions)
{
	BfOptionFlags findFlag = BfOptionFlags_None;
	if (methodDef->mMethodType == BfMethodType_Ctor)
		findFlag = BfOptionFlags_ReflectConstructors;
	else if (methodDef->mIsStatic)
		findFlag = BfOptionFlags_ReflectStaticMethods;
	else
		findFlag = BfOptionFlags_ReflectNonStaticMethods;

	if ((typeOptions->mAndFlags & findFlag) == 0)
		includeMethod = false;
	if ((typeOptions->mOrFlags & findFlag) != 0)
		includeMethod = true;

	if (!typeOptions->mReflectMethodFilters.IsEmpty())
	{
		for (auto& filter : typeOptions->mReflectMethodFilters)
		{
			if (BfCheckWildcard(filter.mFilter, methodDef->mName))
			{
				if ((filter.mAndFlags & findFlag) == 0)
					includeMethod = false;
				if ((filter.mAndFlags | findFlag) != 0)
					includeMethod = true;
			}
		}
	}

	return includeMethod;
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
		auto _CheckType = [&](BfType* type)
		{
			StringImpl typeName = TypeToString(type);

			for (int optionIdx = 0; optionIdx < (int)mContext->mSystem->mTypeOptions.size(); optionIdx++)
			{
				auto& typeOptions = mContext->mSystem->mTypeOptions[optionIdx];

				bool matched = false;
				for (auto& filter : typeOptions.mTypeFilters)
				{
					int filterIdx = 0;
					int typeNameIdx = 0;

					if (filter.StartsWith(':'))
					{
						BfTypeInstance* typeInst = type->ToTypeInstance();
						if (typeInst != NULL)
						{
							int startPos = 1;
							for (; startPos < (int)filter.length(); startPos++)
								if (filter[startPos] != ' ')
									break;
							String checkFilter;
							checkFilter.Reference(filter.c_str() + startPos, filter.mLength - startPos);

							BfTypeInstance* checkTypeInst = typeInst;
							while (checkTypeInst != NULL)
							{
								for (auto& iface : checkTypeInst->mInterfaces)
								{
									StringT<128> ifaceName = TypeToString(iface.mInterfaceType);
									if (BfCheckWildcard(checkFilter, ifaceName))
									{
										matched = true;
										break;
									}
								}
								checkTypeInst = checkTypeInst->mBaseType;
							}
							if (matched)
								break;
						}
					}
					else if (BfCheckWildcard(filter, typeName))
					{
						matched = true;
						break;
					}
				}

				if (matched)
					matchedIndices.push_back(optionIdx);
			}
		};

		if (typeInstance->IsTypedPrimitive())
		{
			auto underlyingType = typeInstance->GetUnderlyingType();
			if (underlyingType != NULL)
			{
				_CheckType(underlyingType);
			}
			else
			{
				// Can this only happen for functions that are being extended?
			}
		}

		if ((!typeInstance->IsBoxed()) && (typeInstance->IsInstanceOf(mCompiler->mPointerTTypeDef)))
		{
			BF_ASSERT(typeInstance->IsGenericTypeInstance());
			auto innerType = typeInstance->mGenericTypeInfo->mTypeGenericArguments[0];
			auto ptrType = CreatePointerType(innerType);
			_CheckType(ptrType);
		}

		_CheckType(typeInstance);
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
			mergedTypeOptions.mAndFlags = first.mAndFlags;
			mergedTypeOptions.mOrFlags = first.mOrFlags;
			mergedTypeOptions.mAllocStackTraceDepth = first.mAllocStackTraceDepth;
			mergedTypeOptions.mReflectMethodFilters = first.mReflectMethodFilters;
			mergedTypeOptions.mReflectMethodAttributeFilters = first.mReflectMethodAttributeFilters;

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

				mergedTypeOptions.mOrFlags = (BfOptionFlags)(mergedTypeOptions.mOrFlags | typeOptions.mOrFlags);
				mergedTypeOptions.mAndFlags = (BfOptionFlags)(mergedTypeOptions.mAndFlags | typeOptions.mOrFlags);

				mergedTypeOptions.mAndFlags = (BfOptionFlags)(mergedTypeOptions.mAndFlags & typeOptions.mAndFlags);
				mergedTypeOptions.mOrFlags = (BfOptionFlags)(mergedTypeOptions.mOrFlags & typeOptions.mAndFlags);

				if (typeOptions.mAllocStackTraceDepth != -1)
					mergedTypeOptions.mAllocStackTraceDepth = typeOptions.mAllocStackTraceDepth;
				for (auto& filter : typeOptions.mReflectMethodFilters)
					mergedTypeOptions.mReflectMethodFilters.Add(filter);
				for (auto& filter : typeOptions.mReflectMethodAttributeFilters)
					mergedTypeOptions.mReflectMethodAttributeFilters.Add(filter);
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

BfCEParseContext BfModule::CEEmitParse(BfTypeInstance* typeInstance, BfTypeDef* declaringType, const StringImpl& src, BfAstNode* refNode, BfCeTypeEmitSourceKind emitSourceKind)
{
	if (mCompiler->mResolvePassData != NULL)
		mCompiler->mResolvePassData->mHadEmits = true;

	BfCEParseContext ceParseContext;
	ceParseContext.mFailIdx = mCompiler->mPassInstance->mFailedIdx;
	ceParseContext.mWarnIdx = mCompiler->mPassInstance->mWarnIdx;

	bool createdParser = false;
	int startSrcIdx = 0;

	BfParser* emitParser = NULL;

	int64 emitSourceMapKey = ((int64)declaringType->mPartialIdx << 32) | refNode->mSrcStart;
	if (typeInstance->mCeTypeInfo == NULL)
		typeInstance->mCeTypeInfo = new BfCeTypeInfo();
	auto ceTypeInfo = typeInstance->mCeTypeInfo;
	if (ceTypeInfo->mNext != NULL)
		ceTypeInfo = ceTypeInfo->mNext;
	BfCeTypeEmitSource* ceEmitSource = NULL;
	if ((mCurMethodState != NULL) && (mCurMethodState->mClosureState != NULL) && (mCurMethodState->mClosureState->mCapturing))
	{
		// Don't create emit sources when we're in a capture phase
	}
	else
	{
		auto refParser = refNode->GetParser();
		if ((refParser != NULL) && (refParser->mIsEmitted))
		{
			// Default to type declaration
			emitSourceMapKey = mCurTypeInstance->mTypeDef->GetRefNode()->mSrcStart;
			for (auto& kv : ceTypeInfo->mEmitSourceMap)
			{
				if ((refNode->mSrcStart >= kv.mValue.mSrcStart) && (refNode->mSrcStart < kv.mValue.mSrcEnd))
				{
					// We found the initial emit source
					emitSourceMapKey = kv.mKey;
					break;
				}
			}
		}

		if (ceTypeInfo->mEmitSourceMap.TryAdd(emitSourceMapKey, NULL, &ceEmitSource))
		{
			if (typeInstance->IsSpecializedType())
			{
				auto unspecializedType = GetUnspecializedTypeInstance(typeInstance);
				if ((unspecializedType->mCeTypeInfo == NULL) || (!unspecializedType->mCeTypeInfo->mEmitSourceMap.ContainsKey(emitSourceMapKey)))
					ceTypeInfo->mMayHaveUniqueEmitLocations = true;
			}
		}
		ceEmitSource->mKind = emitSourceKind;
	}

	BfLogSysM("CEEmitParse type %p ceTypeInfo %p\n", typeInstance, ceTypeInfo);

	int emitSrcStart = 0;

	BfEmitEmbedEntry* emitEmbedEntry = NULL;

	if (typeInstance->mTypeDef->mEmitParent == NULL)
	{
		BF_ASSERT(typeInstance->mTypeDef->mNextRevision == NULL);

		BfTypeDef* emitTypeDef = new BfTypeDef();
		emitTypeDef->mEmitParent = typeInstance->mTypeDef;
		mSystem->CopyTypeDef(emitTypeDef, typeInstance->mTypeDef);
		emitTypeDef->mDefState = BfTypeDef::DefState_Emitted;

		typeInstance->mTypeDef = emitTypeDef;

		createdParser = true;
		emitParser = new BfParser(mSystem, typeInstance->mTypeDef->mProject);
		emitParser->mIsEmitted = true;

		BfLogSys(mSystem, "Emit typeDef for type %p created %p parser %p typeDecl %p\n", typeInstance, emitTypeDef, emitParser, emitTypeDef->mTypeDeclaration);

		String typeName = TypeToString(typeInstance, BfTypeNameFlag_AddProjectName);

		if ((mCompiler->mResolvePassData != NULL) && (!mCompiler->mResolvePassData->mEmitEmbedEntries.IsEmpty()))
			mCompiler->mResolvePassData->mEmitEmbedEntries.TryGetValue(typeName, &emitEmbedEntry);

		emitParser->mFileName = "$Emit$";
		emitParser->mFileName += typeName;

		emitTypeDef->mSource = emitParser;
		emitParser->mRefCount++;
		emitParser->SetSource(src.c_str(), src.mLength);

		if (emitEmbedEntry != NULL)
		{
			emitEmbedEntry->mRevision = typeInstance->mRevision;
			emitEmbedEntry->mParser = emitParser;
			emitEmbedEntry->mParser->mSourceClassifier = new BfSourceClassifier(emitEmbedEntry->mParser, NULL);
			mCompiler->mPassInstance->mFilterErrorsTo.Add(emitEmbedEntry->mParser->mParserData);

			if (emitEmbedEntry->mCursorIdx != -1)
			{
				emitParser->SetCursorIdx(emitEmbedEntry->mCursorIdx);
				emitParser->mParserFlags = (BfParserFlag)(emitParser->mParserFlags | ParserFlag_Autocomplete | ParserFlag_Classifying);
			}
		}

		// If we emit only from method attributes then we will already have method instances created
		auto _FixMethod = [&](BfMethodInstance* methodInstance)
		{
			if (methodInstance == NULL)
				return;
			methodInstance->mMethodDef = emitTypeDef->mMethods[methodInstance->mMethodDef->mIdx];
		};

		for (auto& methodInstanceGroup : typeInstance->mMethodInstanceGroups)
		{
			_FixMethod(methodInstanceGroup.mDefault);
			if (methodInstanceGroup.mMethodSpecializationMap != NULL)
			{
				for (auto& kv : *methodInstanceGroup.mMethodSpecializationMap)
					_FixMethod(kv.mValue);
			}
		};
	}
	else
	{
		emitParser = typeInstance->mTypeDef->mSource->ToParser();

		if ((mCompiler->mResolvePassData != NULL) && (!mCompiler->mResolvePassData->mEmitEmbedEntries.IsEmpty()))
		{
			int dollarPos = (int)emitParser->mFileName.LastIndexOf('$');
			if (dollarPos != -1)
				mCompiler->mResolvePassData->mEmitEmbedEntries.TryGetValue(emitParser->mFileName.Substring(dollarPos + 1), &emitEmbedEntry);
		}

		int idx = emitParser->AllocChars(2 + src.mLength + 1);
		emitSrcStart = idx + 2;

		memcpy((uint8*)emitParser->mSrc + idx, "\n\n", 2);
		memcpy((uint8*)emitParser->mSrc + idx + 2, src.c_str(), src.mLength + 1);
		emitParser->mSrcIdx = idx;
		emitParser->mSrcLength = idx + src.mLength + 2;
		emitParser->mParserData->mSrcLength = emitParser->mSrcLength;
		emitParser->mOrigSrcLength = emitParser->mSrcLength;
	}

	if (ceEmitSource == NULL)
	{
		// Ignored
	}
	else if (ceEmitSource->mSrcStart == -1)
	{
		auto parserData = refNode->GetParserData();
		if (parserData != NULL)
		{
			// Add the warning changes occur before the start of the buffer.
			//  We use this conservatively now - any temporary disabling will permanently disable
			for (auto& warning : parserData->mWarningEnabledChanges)
			{
				if (!warning.mValue.mEnable)
					emitParser->mParserData->mWarningEnabledChanges[-warning.mValue.mWarningNumber] = warning.mValue;
			}
		}

		ceEmitSource->mSrcStart = emitSrcStart;
		ceEmitSource->mSrcEnd = emitParser->mSrcLength;
	}
	else
	{
		ceEmitSource->mSrcStart = BF_MIN(ceEmitSource->mSrcStart, emitSrcStart);
		ceEmitSource->mSrcEnd = BF_MAX(ceEmitSource->mSrcEnd, emitParser->mSrcLength);
	}

	emitParser->Parse(mCompiler->mPassInstance);
	emitParser->FinishSideNodes();

	if (emitEmbedEntry != NULL)
	{
		int prevStart = emitEmbedEntry->mCharData.mSize;
		emitEmbedEntry->mCharData.GrowUninitialized(emitParser->mSrcLength - emitEmbedEntry->mCharData.mSize);
		auto charDataPtr = emitEmbedEntry->mCharData.mVals;
		for (int i = prevStart; i < emitParser->mSrcLength; i++)
		{
			charDataPtr[i].mChar = emitParser->mSrc[i];
			charDataPtr[i].mDisplayPassId = 0;
			charDataPtr[i].mDisplayTypeId = 0;
			charDataPtr[i].mDisplayFlags = 0;
		}

		emitEmbedEntry->mParser->mSourceClassifier->mCharData = emitEmbedEntry->mCharData.mVals;
	}

	if (createdParser)
	{
		AutoCrit crit(mSystem->mDataLock);
		mSystem->mParsers.Add(emitParser);
	}

	return ceParseContext;
}

void BfModule::FinishCEParseContext(BfAstNode* refNode, BfTypeInstance* typeInstance, BfCEParseContext* ceParseContext)
{
	if ((ceParseContext->mFailIdx != mCompiler->mPassInstance->mFailedIdx) && (refNode != NULL))
		Fail("Emitted code had errors", refNode);
	else if ((ceParseContext->mWarnIdx != mCompiler->mPassInstance->mWarnIdx) && (refNode != NULL))
		Warn(0, "Emitted code had warnings", refNode);
	else if ((ceParseContext->mFailIdx != mCompiler->mPassInstance->mFailedIdx) ||
		(ceParseContext->mWarnIdx != mCompiler->mPassInstance->mWarnIdx))
	{
		AddFailType(typeInstance);
	}
}

void BfModule::UpdateCEEmit(CeEmitContext* ceEmitContext, BfTypeInstance* typeInstance, BfTypeDef* declaringType, const StringImpl& ctxString, BfAstNode* refNode, BfCeTypeEmitSourceKind emitSourceKind)
{
	for (int ifaceTypeId : ceEmitContext->mInterfaces)
		typeInstance->mCeTypeInfo->mPendingInterfaces.Add(ifaceTypeId);

	if (ceEmitContext->mEmitData.IsEmpty())
		return;

	String src;

// 	if (typeInstance->mTypeDef->mEmitParent != NULL)
// 		src += "\n\n";

// 	src += "// Code emission in ";
// 	src += ctxString;
// 	src += "\n\n";
	src += ceEmitContext->mEmitData;
	ceEmitContext->mEmitData.Clear();

	BfCEParseContext ceParseContext = CEEmitParse(typeInstance, declaringType, src, refNode, emitSourceKind);
	auto emitParser = typeInstance->mTypeDef->mSource->ToParser();

	auto typeDeclaration = emitParser->mAlloc->Alloc<BfTypeDeclaration>();

	BfReducer bfReducer;
	bfReducer.mSource = emitParser;
	bfReducer.mPassInstance = mCompiler->mPassInstance;
	bfReducer.mAlloc = emitParser->mAlloc;
	bfReducer.mSystem = mSystem;
	bfReducer.mCurTypeDecl = typeDeclaration;
	typeDeclaration->mDefineNode = emitParser->mRootNode;
	bfReducer.HandleTypeDeclaration(typeDeclaration, NULL);

	BfDefBuilder defBuilder(mSystem);
	defBuilder.mCurSource = emitParser;
	defBuilder.mCurTypeDef = typeInstance->mTypeDef;
	defBuilder.mCurDeclaringTypeDef = typeInstance->mTypeDef;

	defBuilder.mPassInstance = mCompiler->mPassInstance;
	defBuilder.mIsComptime = true;
	defBuilder.DoVisitChild(typeDeclaration->mDefineNode);
	defBuilder.FinishTypeDef(typeInstance->mTypeDef->mTypeCode == BfTypeCode_Enum);

	FinishCEParseContext(refNode, typeInstance, &ceParseContext);

	if (emitParser->mSourceClassifier != NULL)
	{
		emitParser->mSourceClassifier->VisitChild(emitParser->mRootNode);
		emitParser->mSourceClassifier->DeferNodes(emitParser->mSidechannelRootNode);
		emitParser->mSourceClassifier->DeferNodes(emitParser->mErrorRootNode);
	}

	if (typeInstance->mTypeDef->mEmitParent != NULL)
	{
		// Remove generated fields like the 'underlying type' enum field
		typeInstance->mFieldInstances.Resize(typeInstance->mTypeDef->mEmitParent->mFields.mSize);
	}
}

void BfModule::HandleCEAttributes(CeEmitContext* ceEmitContext, BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfCustomAttributes* customAttributes, Dictionary<BfTypeInstance*, BfIRValue>& prevAttrInstances, bool underlyingTypeDeferred)
{
	for (auto& customAttribute : customAttributes->mAttributes)
	{
		auto attrType = customAttribute.mType;

		BfMethodInstance* methodInstance = NULL;
		bool isFieldApply = false;
		BfIRValue irValue;
		int checkDepth = 0;
		auto checkAttrType = attrType;
		while (checkAttrType != NULL)
		{
			mContext->mUnreifiedModule->PopulateType(checkAttrType, BfPopulateType_DataAndMethods);
			if (checkAttrType->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted)
				break;

			for (auto& ifaceEntry : checkAttrType->mInterfaces)
			{
				isFieldApply = false;
				isFieldApply = (ceEmitContext != NULL) && (fieldInstance != NULL) && (ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIOnFieldInitTypeDef));

				if ((isFieldApply) ||
					((ceEmitContext != NULL) && (ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIComptimeTypeApply))) ||
					((ceEmitContext != NULL) && (ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIOnTypeInitTypeDef))) ||
					((ceEmitContext == NULL) && (ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIOnTypeDoneTypeDef))))
				{
					 // Passes
				}
				else
					continue;

				prevAttrInstances.TryGetValue(checkAttrType, &irValue);
				methodInstance = checkAttrType->mInterfaceMethodTable[ifaceEntry.mStartInterfaceTableIdx].mMethodRef;
				break;
			}
			if (methodInstance != NULL)
				break;

			checkAttrType = checkAttrType->mBaseType;
			checkDepth++;
		}

		if (methodInstance == NULL)
			continue;

		SetAndRestoreValue<CeEmitContext*> prevEmitContext(mCompiler->mCeMachine->mCurEmitContext, ceEmitContext);
		auto ceContext = mCompiler->mCeMachine->AllocContext();

		BfIRValue attrVal =ceContext->CreateAttribute(customAttribute.mRef, this, typeInstance->mConstHolder, &customAttribute);
		for (int baseIdx = 0; baseIdx < checkDepth; baseIdx++)
			attrVal = mBfIRBuilder->CreateExtractValue(attrVal, 0);

		SizedArray<BfIRValue, 1> args;
		if (!attrType->IsValuelessType())
			args.Add(attrVal);
		if (isFieldApply)
		{
			auto fieldInfoType = ResolveTypeDef(mCompiler->mReflectFieldInfoTypeDef);
			if (fieldInfoType != NULL)
			{
				SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
				SizedArray<BfIRValue, 9> fieldData =
				{
					mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(fieldInfoType->ToTypeInstance()->mBaseType, BfIRPopulateType_Identity)),
					mBfIRBuilder->CreateTypeOf(mCurTypeInstance), // mTypeInstance
					CreateFieldData(fieldInstance, -1)
				};
				FixConstValueParams(fieldInfoType->ToTypeInstance(), fieldData);
				auto fieldDataAgg = mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapType(fieldInfoType, BfIRPopulateType_Identity), fieldData);
				args.Add(fieldDataAgg);
			}
		}
		else
			args.Add(mBfIRBuilder->CreateTypeOf(typeInstance));

		if (methodInstance->GetParamCount() > 1)
		{
			if (irValue)
				args.Add(irValue);
			else
				args.Add(mBfIRBuilder->CreateConstNull());
		}
		else
		{
			// Only allow a single instance
			if (irValue)
				continue;
		}

		DoPopulateType_CeCheckEnum(typeInstance, underlyingTypeDeferred);
		if (fieldInstance != NULL)
			mCompiler->mCeMachine->mFieldInstanceSet.Add(fieldInstance);
		BfTypedValue result;
		///
		{
			SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);

			CeCallSource callSource;
			callSource.mRefNode = customAttribute.mRef;
			if (isFieldApply)
			{
				callSource.mKind = CeCallSource::Kind_FieldInit;
				callSource.mFieldInstance = fieldInstance;
			}
			else if (ceEmitContext != NULL)
			{
				callSource.mKind = CeCallSource::Kind_TypeInit;
			}
			else
			{
				callSource.mKind = CeCallSource::Kind_TypeDone;
			}

			result = ceContext->Call(callSource, this, methodInstance, args, CeEvalFlags_ForceReturnThis, NULL);
		}
		if (fieldInstance != NULL)
			mCompiler->mCeMachine->mFieldInstanceSet.Remove(fieldInstance);
		if (result.mType == methodInstance->GetOwner())
			prevAttrInstances[methodInstance->GetOwner()] = result.mValue;

		if (ceEmitContext == NULL)
			continue;

		if (typeInstance->mDefineState == BfTypeDefineState_DefinedAndMethodsSlotted)
			return;

		if (typeInstance->mDefineState != BfTypeDefineState_CETypeInit)
		{
			// We populated before we could finish
			AssertErrorState();
		}
		else
		{
			auto owner = methodInstance->GetOwner();
			int typeId = owner->mTypeId;
			if ((!result) && (mCompiler->mFastFinish))
			{
				if ((typeInstance->mCeTypeInfo != NULL) && (typeInstance->mCeTypeInfo->mNext == NULL))
					typeInstance->mCeTypeInfo->mNext = new BfCeTypeInfo();
				if ((typeInstance->mCeTypeInfo != NULL) && (typeInstance->mCeTypeInfo->mNext != NULL))
					typeInstance->mCeTypeInfo->mNext->mFastFinished = true;
				if (typeInstance->mCeTypeInfo != NULL)
				{
					BfCeTypeEmitEntry* entry = NULL;
					if (typeInstance->mCeTypeInfo->mTypeIFaceMap.TryGetValue(typeId, &entry))
					{
						ceEmitContext->mEmitData = entry->mEmitData;
					}
				}
			}
			else if (ceEmitContext->HasEmissions())
			{
				if (typeInstance->mCeTypeInfo == NULL)
					typeInstance->mCeTypeInfo = new BfCeTypeInfo();
				if (typeInstance->mCeTypeInfo->mNext == NULL)
					typeInstance->mCeTypeInfo->mNext = new BfCeTypeInfo();

				BfCeTypeEmitEntry entry;
				entry.mEmitData = ceEmitContext->mEmitData;
				typeInstance->mCeTypeInfo->mNext->mTypeIFaceMap[typeId] = entry;
			}
			else if ((ceEmitContext->mFailed) && (typeInstance->mCeTypeInfo != NULL))
				typeInstance->mCeTypeInfo->mFailed = true;

			if ((ceEmitContext->HasEmissions()) && (!mCompiler->mFastFinish))
			{
				String ctxStr = "comptime ";
				ctxStr += methodInstance->mMethodDef->mName;
				ctxStr += " of ";
				ctxStr += TypeToString(attrType);
				ctxStr += " to ";
				ctxStr += TypeToString(typeInstance);
				ctxStr += " ";
				ctxStr += customAttribute.mRef->LocationToString();

				UpdateCEEmit(ceEmitContext, typeInstance, customAttribute.mDeclaringType, ctxStr, customAttribute.mRef, BfCeTypeEmitSourceKind_Type);
			}
		}

		mCompiler->mCeMachine->ReleaseContext(ceContext);
	}
}

void BfModule::CEMixin(BfAstNode* refNode, const StringImpl& code)
{
	if (code.IsEmpty())
		return;

	if (mCurMethodInstance == NULL)
	{
		Fail("Invalid code mixin", refNode);
		return;
	}

	auto activeTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;
	//auto emitParser = activeTypeDef->mEmitParser;

	String src;
// 	if (mCurTypeInstance->mTypeDef->mEmitParent != NULL)
// 		src += "\n\n";
// 	src += "// Code emission in ";
// 	src += MethodToString(mCurMethodInstance);
// 	src += "\n";
	src += code;

	BfReducer bfReducer;
	bfReducer.mPassInstance = mCompiler->mPassInstance;
	bfReducer.mSystem = mSystem;
	bfReducer.mCurTypeDecl = activeTypeDef->mTypeDeclaration;
	bfReducer.mCurMethodDecl = BfNodeDynCast<BfMethodDeclaration>(mCurMethodInstance->mMethodDef->mMethodDeclaration);

	SetAndRestoreValue<BfAstNode*> prevCustomAttribute(mCurMethodState->mEmitRefNode, refNode);

	EmitEnsureInstructionAt();

	BfCEParseContext ceParseContext = CEEmitParse(mCurTypeInstance, activeTypeDef, src, refNode, BfCeTypeEmitSourceKind_Method);
	auto emitParser = mCurTypeInstance->mTypeDef->mSource->ToParser();
	bfReducer.mSource = emitParser;
	bfReducer.mAlloc = emitParser->mAlloc;
	bfReducer.HandleBlock(emitParser->mRootNode, false);

	if (emitParser->mSourceClassifier != NULL)
	{
		emitParser->mSourceClassifier->VisitChild(emitParser->mRootNode);
		emitParser->mSourceClassifier->VisitChild(emitParser->mSidechannelRootNode);
		emitParser->mSourceClassifier->VisitChild(emitParser->mErrorRootNode);
	}

	Visit(emitParser->mRootNode);

	prevCustomAttribute.Restore();

	FinishCEParseContext(refNode, mCurTypeInstance, &ceParseContext);
}

void BfModule::ExecuteCEOnCompile(CeEmitContext* ceEmitContext, BfTypeInstance* typeInstance, BfCEOnCompileKind onCompileKind, bool underlyingTypeDeferred)
{
	Dictionary<BfTypeInstance*, BfIRValue> prevAttrInstances;

	if (typeInstance->mCustomAttributes != NULL)
		HandleCEAttributes(ceEmitContext, typeInstance, NULL, typeInstance->mCustomAttributes, prevAttrInstances, underlyingTypeDeferred);

	if (ceEmitContext != NULL)
	{
		for (auto& fieldInstance : typeInstance->mFieldInstances)
		{
			if (fieldInstance.mCustomAttributes != NULL)
				HandleCEAttributes(ceEmitContext, typeInstance, &fieldInstance, fieldInstance.mCustomAttributes, prevAttrInstances, underlyingTypeDeferred);
		}

		for (auto methodDef : typeInstance->mTypeDef->mMethods)
		{
			auto methodDeclaration = methodDef->GetMethodDeclaration();
			auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration();
			BfAttributeTargets attrTarget = ((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend)) ? BfAttributeTargets_Constructor : BfAttributeTargets_Method;
			BfAttributeDirective* attributeDirective = NULL;
			if (methodDeclaration != NULL)
				attributeDirective = methodDeclaration->mAttributes;
			else if (propertyMethodDeclaration != NULL)
			{
				attributeDirective = propertyMethodDeclaration->mAttributes;
				if (auto exprBody = BfNodeDynCast<BfPropertyBodyExpression>(propertyMethodDeclaration->mPropertyDeclaration->mDefinitionBlock))
				{
					attributeDirective = propertyMethodDeclaration->mPropertyDeclaration->mAttributes;
					attrTarget = (BfAttributeTargets)(BfAttributeTargets_Property | BfAttributeTargets_Method);
				}
			}
			if (attributeDirective == NULL)
				continue;

			// Corlib will never need to process
			if (methodDef->mDeclaringType->mProject == mContext->mBfObjectType->mTypeDef->mProject)
				continue;

			if (methodDef->mDeclaringType != mCurTypeInstance->mTypeDef)
			{
				if (typeInstance->IsUnspecializedTypeVariation())
					continue;
				if (!typeInstance->IsTypeMemberIncluded(methodDef->mDeclaringType, mCurTypeInstance->mTypeDef, this))
					continue;
			}

			if (methodDef->mIdx >= typeInstance->mMethodInstanceGroups.mSize)
				continue;

			auto& methodInstanceGroup = typeInstance->mMethodInstanceGroups[methodDef->mIdx];
			if (methodInstanceGroup.mDefaultCustomAttributes == NULL)
			{
				BfTypeState typeState;
				typeState.mPrevState = mContext->mCurTypeState;
				typeState.mForceActiveTypeDef = methodDef->mDeclaringType;
				SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
				methodInstanceGroup.mDefaultCustomAttributes = GetCustomAttributes(attributeDirective, attrTarget);
			}
			HandleCEAttributes(ceEmitContext, typeInstance, NULL, methodInstanceGroup.mDefaultCustomAttributes, prevAttrInstances, underlyingTypeDeferred);
		}
	}

	int methodCount = (int)typeInstance->mTypeDef->mMethods.size();
	for (int methodIdx = 0; methodIdx < methodCount; methodIdx++)
	{
		auto methodDef = typeInstance->mTypeDef->mMethods[methodIdx];
		auto methodDeclaration = BfNodeDynCast<BfMethodDeclaration>(methodDef->mMethodDeclaration);
		if (methodDeclaration == NULL)
			continue;
		if (methodDeclaration->mAttributes == NULL)
			continue;

		BfTypeState typeState;
		typeState.mPrevState = mContext->mCurTypeState;
		typeState.mForceActiveTypeDef = methodDef->mDeclaringType;
		SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

		bool wantsAttributes = false;
		BfAttributeDirective* checkAttributes = methodDeclaration->mAttributes;
		while (checkAttributes != NULL)
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
			BfType* attrType = ResolveTypeRef(checkAttributes->mAttributeTypeRef, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_Attribute | BfResolveTypeRefFlag_NoReify));
			if (attrType != NULL)
			{
				if (attrType->IsInstanceOf(mCompiler->mOnCompileAttributeTypeDef))
					wantsAttributes = true;
				auto attrTypeInstance = attrType->ToTypeInstance();
				if ((attrTypeInstance != NULL) && (!attrTypeInstance->mInterfaces.IsEmpty()))
					wantsAttributes = true;
			}

			checkAttributes = checkAttributes->mNextAttribute;
		}

		if (!wantsAttributes)
			continue;

		auto customAttributes = GetCustomAttributes(methodDeclaration->mAttributes, BfAttributeTargets_Method);
		defer({ delete customAttributes; });

		auto onCompileAttribute = customAttributes->Get(mCompiler->mOnCompileAttributeTypeDef);
		if (onCompileAttribute == NULL)
			continue;

		HandleCEAttributes(ceEmitContext, typeInstance, NULL, customAttributes, prevAttrInstances, underlyingTypeDeferred);

		if (onCompileAttribute->mCtorArgs.size() < 1)
			continue;
		auto constant = typeInstance->mConstHolder->GetConstant(onCompileAttribute->mCtorArgs[0]);
		if (constant == NULL)
			continue;
		if (onCompileKind != (BfCEOnCompileKind)constant->mInt32)
			continue;

		if (!methodDef->mIsStatic)
		{
			Fail("OnCompile methods must be static", methodDeclaration);
			continue;
		}

		if (!methodDef->mParams.IsEmpty())
		{
			Fail("OnCompile methods cannot declare parameters", methodDeclaration);
			continue;
		}

		SetAndRestoreValue<CeEmitContext*> prevEmitContext(mCompiler->mCeMachine->mCurEmitContext);
		if (onCompileKind == BfCEOnCompileKind_TypeInit)
		{
			mCompiler->mCeMachine->mCurEmitContext = ceEmitContext;
		}

		DoPopulateType_CeCheckEnum(typeInstance, underlyingTypeDeferred);
		auto methodInstance = GetRawMethodInstanceAtIdx(typeInstance, methodDef->mIdx);
		auto result = mCompiler->mCeMachine->Call(methodDef->GetRefNode(), this, methodInstance, {}, (CeEvalFlags)(CeEvalFlags_PersistantError | CeEvalFlags_DeferIfNotOnlyError), NULL);

		if ((onCompileKind == BfCEOnCompileKind_TypeDone) && (typeInstance->mDefineState > BfTypeDefineState_CETypeInit))
		{
			// Type done, okay
		}
		else if (typeInstance->mDefineState != BfTypeDefineState_CETypeInit)
		{
			// We populated before we could finish
			AssertErrorState();
		}
		else
		{
			if ((!result) && (mCompiler->mFastFinish))
			{
				if ((typeInstance->mCeTypeInfo != NULL) && (typeInstance->mCeTypeInfo->mNext == NULL))
					typeInstance->mCeTypeInfo->mNext = new BfCeTypeInfo();
				if ((typeInstance->mCeTypeInfo != NULL) && (typeInstance->mCeTypeInfo->mNext != NULL))
					typeInstance->mCeTypeInfo->mNext->mFastFinished = true;
				if (typeInstance->mCeTypeInfo != NULL)
				{
					BfCeTypeEmitEntry* entry = NULL;
					if (typeInstance->mCeTypeInfo->mOnCompileMap.TryGetValue(methodDef->mIdx, &entry))
					{
						ceEmitContext->mEmitData = entry->mEmitData;
					}
				}
			}
			else if (!ceEmitContext->mEmitData.IsEmpty())
			{
				if (typeInstance->mCeTypeInfo == NULL)
					typeInstance->mCeTypeInfo = new BfCeTypeInfo();
				if (typeInstance->mCeTypeInfo->mNext == NULL)
					typeInstance->mCeTypeInfo->mNext = new BfCeTypeInfo();

				BfCeTypeEmitEntry entry;
				entry.mEmitData = ceEmitContext->mEmitData;
				typeInstance->mCeTypeInfo->mNext->mOnCompileMap[methodDef->mIdx] = entry;
			}
			else if ((ceEmitContext->mFailed) && (typeInstance->mCeTypeInfo != NULL))
				typeInstance->mCeTypeInfo->mFailed = true;

			if (!ceEmitContext->mEmitData.IsEmpty())
			{
				String ctxStr = "OnCompile execution of ";
				ctxStr += MethodToString(methodInstance);
				ctxStr += " ";
				ctxStr += methodInstance->mMethodDef->GetRefNode()->LocationToString();
				UpdateCEEmit(ceEmitContext, typeInstance, methodDef->mDeclaringType, ctxStr, methodInstance->mMethodDef->GetRefNode(), BfCeTypeEmitSourceKind_Type);
			}
		}

		if (mCompiler->mCanceling)
		{
			DeferRebuildType(typeInstance);
		}
	}

// 	if ((!typeInstance->IsInstanceOf(mCompiler->mValueTypeTypeDef)) &&
// 		(!typeInstance->IsInstanceOf(mCompiler->mBfObjectTypeDef)) &&
// 		(!typeInstance->IsBoxed()) &&
// 		(!typeInstance->IsDelegate()) &&
// 		(!typeInstance->IsTuple()))
// 	{
// 		//zTODO: TESTING, remove!
// 		CEEmitParse(typeInstance, "// Testing");
// 	}
}

void BfModule::DoCEEmit(BfTypeInstance* typeInstance, bool& hadNewMembers, bool underlyingTypeDeferred)
{
	BfLogSysM("BfModule::DoCEEmit %p\n", typeInstance);

	if (((typeInstance->IsInstanceOf(mCompiler->mValueTypeTypeDef))) ||
		((typeInstance->IsInstanceOf(mCompiler->mEnumTypeDef))) ||
		((typeInstance->IsInstanceOf(mCompiler->mAttributeTypeDef))))
	{
		// These are not allowed to emit
		return;
	}

	CeEmitContext ceEmitContext;
	ceEmitContext.mType = typeInstance;
	ExecuteCEOnCompile(&ceEmitContext, typeInstance, BfCEOnCompileKind_TypeInit, underlyingTypeDeferred);
	hadNewMembers = (typeInstance->mTypeDef->mEmitParent != NULL);

	if (ceEmitContext.mFailed)
		TypeFailed(typeInstance);
}

void BfModule::DoCEEmit(BfMethodInstance* methodInstance)
{
	auto customAttributes = methodInstance->GetCustomAttributes();
	if (customAttributes == NULL)
		return;

	auto typeInstance = methodInstance->GetOwner();

	CeEmitContext ceEmitContext;
	ceEmitContext.mMethodInstance = methodInstance;

	Dictionary<BfTypeInstance*, BfIRValue> prevAttrInstances;

	for (auto& customAttribute : customAttributes->mAttributes)
	{
		auto attrType = customAttribute.mType;

		BfMethodInstance* applyMethodInstance = NULL;
		BfIRValue irValue;
		int checkDepth = 0;
		auto checkAttrType = attrType;

		while (checkAttrType != NULL)
		{
			mContext->mUnreifiedModule->PopulateType(checkAttrType, BfPopulateType_DataAndMethods);
			if (checkAttrType->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted)
				break;

			for (auto& ifaceEntry : checkAttrType->mInterfaces)
			{
				if ((!ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIComptimeMethodApply)) &&
					(!ifaceEntry.mInterfaceType->IsInstanceOf(mCompiler->mIOnMethodInitTypeDef)))
					continue;

				prevAttrInstances.TryGetValue(checkAttrType, &irValue);
				applyMethodInstance = checkAttrType->mInterfaceMethodTable[ifaceEntry.mStartInterfaceTableIdx].mMethodRef;
				break;
			}

			if (applyMethodInstance != NULL)
				break;

			checkAttrType = checkAttrType->mBaseType;
			checkDepth++;
		}

		if (applyMethodInstance == NULL)
			continue;

		SetAndRestoreValue<CeEmitContext*> prevEmitContext(mCompiler->mCeMachine->mCurEmitContext, &ceEmitContext);
		auto ceContext = mCompiler->mCeMachine->AllocContext();

		BfIRValue attrVal = ceContext->CreateAttribute(customAttribute.mRef, this, typeInstance->mConstHolder, &customAttribute);
		for (int baseIdx = 0; baseIdx < checkDepth; baseIdx++)
			attrVal = mBfIRBuilder->CreateExtractValue(attrVal, 0);

		SizedArray<BfIRValue, 1> args;
		if (!attrType->IsValuelessType())
			args.Add(attrVal);

		auto methodInfoType = ResolveTypeDef(mCompiler->mReflectMethodInfoTypeDef);
		SizedArray<BfIRValue, 9> methodData =
		{
			mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(methodInfoType->ToTypeInstance()->mBaseType, BfIRPopulateType_Identity)),
			mBfIRBuilder->CreateTypeOf(mCurTypeInstance), // mTypeInstance
			GetConstValue((int64)methodInstance, GetPrimitiveType(BfTypeCode_Int64)), // mNativeMethodInstance
		};
		FixConstValueParams(methodInfoType->ToTypeInstance(), methodData, true);
		auto fieldDataAgg = mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapType(methodInfoType, BfIRPopulateType_Identity), methodData);
		args.Add(fieldDataAgg);

		if (applyMethodInstance->GetParamCount() > 1)
		{
			if (irValue)
				args.Add(irValue);
			else
				args.Add(mBfIRBuilder->CreateConstNull());
		}
		else
		{
			// Only allow a single instance
			if (irValue)
				continue;
		}

		mCompiler->mCeMachine->mMethodInstanceSet.Add(methodInstance);
		auto activeTypeDef = typeInstance->mTypeDef;

		BfTypedValue result;
		///
		{
			SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);

			CeCallSource callSource;
			callSource.mRefNode = customAttribute.mRef;
			callSource.mKind = CeCallSource::Kind_MethodInit;
			result = ceContext->Call(callSource, this, applyMethodInstance, args, CeEvalFlags_ForceReturnThis, NULL);
		}
		if (result.mType == methodInstance->GetOwner())
			prevAttrInstances[methodInstance->GetOwner()] = result.mValue;

		if ((!result) && (mCompiler->mFastFinish))
		{
			methodInstance->mCeCancelled = true;
		}

		if ((!ceEmitContext.mEmitData.IsEmpty()) || (!ceEmitContext.mExitEmitData.IsEmpty()))
		{
			String src;
// 			src += "// Code emission in comptime ApplyToMethod of ";
// 			src += TypeToString(attrType);
// 			src += " to ";
// 			src += MethodToString(methodInstance);
// 			src += " ";
// 			src += customAttribute.mRef->LocationToString();
// 			src += "\n";

			//auto emitTypeDef = typeInstance->mCeTypeInfo->mNext->mTypeDef;
			//auto emitParser = emitTypeDef->mSource->ToParser();

			//auto emitParser = activeTypeDef->mEmitParser;

			BfReducer bfReducer;
			//bfReducer.mSource = emitParser;
			bfReducer.mPassInstance = mCompiler->mPassInstance;
			bfReducer.mSystem = mSystem;
			bfReducer.mCurTypeDecl = activeTypeDef->mTypeDeclaration;
			bfReducer.mCurMethodDecl = BfNodeDynCast<BfMethodDeclaration>(methodInstance->mMethodDef->mMethodDeclaration);

			BfAstNode* bodyNode = NULL;
			if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(methodInstance->mMethodDef->mMethodDeclaration))
				bodyNode = methodDecl->mBody;

			auto _Classify = [&](BfParser* emitParser)
			{
				if (emitParser->mSourceClassifier == NULL)
					return;
				emitParser->mSourceClassifier->VisitChild(emitParser->mRootNode);
				emitParser->mSourceClassifier->VisitChild(emitParser->mSidechannelRootNode);
				emitParser->mSourceClassifier->VisitChild(emitParser->mErrorRootNode);
			};

			if (!ceEmitContext.mEmitData.IsEmpty())
			{
				SetAndRestoreValue<BfAstNode*> prevCustomAttribute(mCurMethodState->mEmitRefNode, customAttribute.mRef);
				String entrySrc = src;
// 				if (mCurTypeInstance->mTypeDef->mEmitParent != NULL)
// 					entrySrc += "\n\n";
				entrySrc += src;
				entrySrc += ceEmitContext.mEmitData;

				BfAstNode* refNode = customAttribute.mRef;
				if (bodyNode != NULL)
				{
					refNode = bodyNode;
					if (auto blockNode = BfNodeDynCast<BfBlock>(bodyNode))
						if (blockNode->mOpenBrace != NULL)
							refNode = blockNode->mOpenBrace;
				}

				BfCEParseContext ceParseContext = CEEmitParse(typeInstance, methodInstance->mMethodDef->mDeclaringType, entrySrc, refNode, BfCeTypeEmitSourceKind_Type);
				auto emitParser = mCurTypeInstance->mTypeDef->mSource->ToParser();
				bfReducer.mSource = emitParser;
				bfReducer.mAlloc = emitParser->mAlloc;
				bfReducer.HandleBlock(emitParser->mRootNode, false);
				_Classify(emitParser);
				Visit(emitParser->mRootNode);
				FinishCEParseContext(customAttribute.mRef, typeInstance, &ceParseContext);
			}

			if (!ceEmitContext.mExitEmitData.IsEmpty())
			{
				String exitSrc;
				if (mCurTypeInstance->mTypeDef->mEmitParent != NULL)
					exitSrc += "\n\n";
				exitSrc += src;
				exitSrc += ceEmitContext.mExitEmitData;

				BfAstNode* refNode = customAttribute.mRef;
				if (bodyNode != NULL)
				{
					refNode = bodyNode;
					if (auto blockNode = BfNodeDynCast<BfBlock>(bodyNode))
						if (blockNode->mCloseBrace != NULL)
							refNode = blockNode->mCloseBrace;
				}

				BfCEParseContext ceParseContext = CEEmitParse(typeInstance, methodInstance->mMethodDef->mDeclaringType, exitSrc, refNode, BfCeTypeEmitSourceKind_Type);

				auto emitParser = mCurTypeInstance->mTypeDef->mSource->ToParser();
				bfReducer.mSource = emitParser;
				bfReducer.mAlloc = emitParser->mAlloc;
				bfReducer.HandleBlock(emitParser->mRootNode, false);
				_Classify(emitParser);
				auto deferredBlock = AddDeferredBlock(emitParser->mRootNode, &mCurMethodState->mHeadScope);
				deferredBlock->mEmitRefNode = customAttribute.mRef;
				FinishCEParseContext(customAttribute.mRef, typeInstance, &ceParseContext);
			}
		}

		mCompiler->mCeMachine->ReleaseContext(ceContext);
	}
}

void BfModule::PopulateUsingFieldData(BfTypeInstance* typeInstance)
{
	if (typeInstance->mTypeInfoEx == NULL)
		typeInstance->mTypeInfoEx = new BfTypeInfoEx();

	BfUsingFieldData* usingFieldData;
	if (typeInstance->mTypeInfoEx->mUsingFieldData != NULL)
	{
		usingFieldData = typeInstance->mTypeInfoEx->mUsingFieldData;
		Array<BfTypeInstance*> populatedTypes;

		for (auto checkType : usingFieldData->mAwaitingPopulateSet)
		{
			if (checkType->mDefineState >= BfTypeDefineState_Defined)
				populatedTypes.Add(checkType);
		}

		if (populatedTypes.IsEmpty())
			return;

		for (auto type : populatedTypes)
			usingFieldData->mAwaitingPopulateSet.Remove(type);
		usingFieldData->mEntries.Clear();
		usingFieldData->mMethods.Clear();
	}
	else
	{
		usingFieldData = new BfUsingFieldData();
		typeInstance->mTypeInfoEx->mUsingFieldData = usingFieldData;
	}

	HashSet<BfTypeInstance*> checkedTypeSet;
	Array<BfUsingFieldData::MemberRef> memberRefs;
	std::function<void(BfTypeInstance*, bool)> _CheckType = [&](BfTypeInstance* usingType, bool staticOnly)
	{
		if (!checkedTypeSet.Add(usingType))
			return;
		defer(
			{
				checkedTypeSet.Remove(usingType);
			});

		for (auto fieldDef : usingType->mTypeDef->mFields)
		{
			if ((staticOnly) && (!fieldDef->mIsStatic))
				continue;

			memberRefs.Add(BfUsingFieldData::MemberRef(usingType, fieldDef));
			defer(
				{
					memberRefs.pop_back();
				});

			if (memberRefs.Count() > 1)
			{
				BfUsingFieldData::Entry* entry = NULL;
				usingFieldData->mEntries.TryAdd(fieldDef->mName, NULL, &entry);
				SizedArray<BfUsingFieldData::MemberRef, 1> lookup;
				for (auto entry : memberRefs)
					lookup.Add(entry);
				entry->mLookups.Add(lookup);
			}

			if (fieldDef->mUsingProtection == BfProtection_Hidden)
				continue;

			if (usingType->mDefineState < BfTypeDefineState_Defined)
			{
				bool isPopulatingType = false;

				auto checkTypeState = mContext->mCurTypeState;
				while (checkTypeState != NULL)
				{
					if ((checkTypeState->mType == usingType) && (checkTypeState->mPopulateType >= BfPopulateType_Data))
					{
						isPopulatingType = true;
						break;
					}

					checkTypeState = checkTypeState->mPrevState;
				}

				if (!isPopulatingType)
				{
					// We need to populate this type now
					PopulateType(usingType, BfPopulateType_Data_Soft);
				}

				if (usingType->mDefineState < BfTypeDefineState_Defined)
					typeInstance->mTypeInfoEx->mUsingFieldData->mAwaitingPopulateSet.Add(usingType);
			}

			auto fieldInstance = &usingType->mFieldInstances[fieldDef->mIdx];
			auto fieldTypeInst = fieldInstance->mResolvedType->ToTypeInstance();
			if (fieldTypeInst != NULL)
				_CheckType(fieldTypeInst, fieldDef->mIsStatic);
		}

		for (auto propDef : usingType->mTypeDef->mProperties)
		{
			if ((staticOnly) && (!propDef->mIsStatic))
				continue;

			memberRefs.Add(BfUsingFieldData::MemberRef(usingType, propDef));
			defer(
				{
					memberRefs.pop_back();
				});

			if (memberRefs.Count() > 1)
			{
				BfUsingFieldData::Entry* entry = NULL;
				usingFieldData->mEntries.TryAdd(propDef->mName, NULL, &entry);
				SizedArray<BfUsingFieldData::MemberRef, 1> lookup;
				for (auto entry : memberRefs)
					lookup.Add(entry);
				entry->mLookups.Add(lookup);
			}

			if (propDef->mUsingProtection == BfProtection_Hidden)
				continue;

			if (usingType->mDefineState < BfTypeDefineState_Defined)
			{
				// We need to populate this type now
				PopulateType(usingType);
			}

			BfType* propType = NULL;
			for (auto methodDef : propDef->mMethods)
			{
				auto methodInstance = GetRawMethodInstance(usingType, methodDef);
				if (methodInstance == NULL)
					continue;
				if (methodDef->mMethodType == BfMethodType_PropertyGetter)
				{
					propType = methodInstance->mReturnType;
					break;
				}
				if (methodDef->mMethodType == BfMethodType_PropertySetter)
				{
					if (methodInstance->GetParamCount() > 0)
					{
						propType = methodInstance->GetParamType(0);
						break;
					}
				}
			}
			if ((propType != NULL) && (propType->IsTypeInstance()))
				_CheckType(propType->ToTypeInstance(), propDef->mIsStatic);
		}

		for (auto methodDef : usingType->mTypeDef->mMethods)
		{
			if ((staticOnly) && (!methodDef->mIsStatic))
				continue;

			//TODO: Support mixins as well
			if (methodDef->mMethodType != BfMethodType_Normal)
				continue;

			// No auto methods
			if (methodDef->mMethodDeclaration == NULL)
				continue;

			memberRefs.Add(BfUsingFieldData::MemberRef(usingType, methodDef));
			defer(
				{
					memberRefs.pop_back();
				});

			if (memberRefs.Count() > 1)
			{
				BfUsingFieldData::Entry* entry = NULL;
				usingFieldData->mMethods.TryAdd(methodDef->mName, NULL, &entry);
				SizedArray<BfUsingFieldData::MemberRef, 1> lookup;
				for (auto entry : memberRefs)
					lookup.Add(entry);
				entry->mLookups.Add(lookup);
			}
		}
	};

	_CheckType(typeInstance, false);
}

void BfModule::DoPopulateType_SetGenericDependencies(BfTypeInstance* genericTypeInstance)
{
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, genericTypeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);

	// Add generic dependencies if needed
	for (auto genericArgType : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
	{
		if (genericArgType->IsPrimitiveType())
			genericArgType = GetWrappedStructType(genericArgType);
		if (genericArgType != NULL)
		{
			AddDependency(genericArgType, genericTypeInstance, BfDependencyMap::DependencyFlag_TypeGenericArg);
			BfLogSysM("Adding generic dependency of %p for type %p revision %d\n", genericArgType, genericTypeInstance, genericTypeInstance->mRevision);

#ifdef _DEBUG
// 			auto argDepType = genericArgType->ToDependedType();
// 			if (argDepType != NULL)
// 			{
// 				BfDependencyMap::DependencyEntry* depEntry = NULL;
// 				argDepType->mDependencyMap.mTypeSet.TryGetValue(genericTypeInstance, &depEntry);
// 				BF_ASSERT(depEntry != NULL);
// 				BF_ASSERT(depEntry->mRevision == genericTypeInstance->mRevision);
// 				BF_ASSERT((depEntry->mFlags & BfDependencyMap::DependencyFlag_TypeGenericArg) != 0);
// 			}
#endif
		}
	}
	if ((genericTypeInstance->IsSpecializedType()) &&
		(!genericTypeInstance->IsDelegateFromTypeRef()) &&
		(!genericTypeInstance->IsFunctionFromTypeRef()))
	{
		// This ensures we rebuild the unspecialized type whenever the specialized type rebuilds. This is important
		// for generic type binding
		auto unspecializedTypeInstance = GetUnspecializedTypeInstance(genericTypeInstance);
		BF_ASSERT(!unspecializedTypeInstance->IsUnspecializedTypeVariation());
		mContext->mScratchModule->AddDependency(genericTypeInstance, unspecializedTypeInstance, BfDependencyMap::DependencyFlag_UnspecializedType);
	}
}

void BfModule::DoPopulateType_TypeAlias(BfTypeAliasType* typeAlias)
{
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeAlias);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);

	BF_ASSERT(mCurMethodInstance == NULL);
	auto typeDef = typeAlias->mTypeDef;
	auto typeAliasDecl = (BfTypeAliasDeclaration*)typeDef->mTypeDeclaration;
	BfType* aliasToType = NULL;

	if (typeAlias->mBaseType == NULL)
		typeAlias->mBaseType = ResolveTypeDef(mCompiler->mValueTypeTypeDef)->ToTypeInstance();

	if ((typeAlias->mGenericTypeInfo != NULL) && (!typeAlias->mGenericTypeInfo->mFinishedGenericParams))
		FinishGenericParams(typeAlias);

	BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
	typeState.mPopulateType = BfPopulateType_Data;
	typeState.mCurBaseTypeRef = typeAliasDecl->mAliasToType;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	if (typeAlias->mDefineState < BfTypeDefineState_Declaring)
	{
		typeAlias->mDefineState = BfTypeDefineState_Declaring;
		DoPopulateType_InitSearches(typeAlias);
	}

	typeAlias->mDefineState = BfTypeDefineState_ResolvingBaseType;

	if (!CheckCircularDataError())
	{
		if (typeAliasDecl->mAliasToType != NULL)
			aliasToType = ResolveTypeRef(typeAliasDecl->mAliasToType, BfPopulateType_IdentityNoRemapAlias,
				(BfResolveTypeRefFlags)(BfResolveTypeRefFlag_AllowGenericParamConstValue | BfResolveTypeRefFlag_AllowImplicitConstExpr));
	}

	BfLogSysM("DoPopulateType_TypeAlias %p %s = %p %s\n", typeAlias, TypeToString(typeAlias).c_str(), aliasToType, (aliasToType != NULL) ? TypeToString(aliasToType).c_str() : NULL);

	if (aliasToType != NULL)
	{
		if (aliasToType->IsConstExprValue())
		{
			Fail(StrFormat("Illegal alias to type '%s'", TypeToString(aliasToType).c_str()), typeAlias->mTypeDef->GetRefNode());
			aliasToType = NULL;
		}
	}

	if (aliasToType != NULL)
	{
		AddDependency(aliasToType, typeAlias, BfDependencyMap::DependencyFlag_DerivedFrom);
	}
	else
		mContext->mFailTypes.TryAdd(typeAlias, BfFailKind_Normal);

	if (typeAlias->mTypeFailed)
		aliasToType = NULL;

 	if ((typeAlias->mAliasToType != NULL) && (typeAlias->mAliasToType != aliasToType) && (!typeAlias->mDependencyMap.IsEmpty()))
 		mContext->QueueMidCompileRebuildDependentTypes(typeAlias, "type alias remapped");

	typeAlias->mAliasToType = aliasToType;

	if (aliasToType != NULL)
	{
		typeAlias->mSize = 0;
		typeAlias->mAlign = 1;
		typeAlias->mInstSize = 0;
		typeAlias->mInstAlign = 1;
	}
	typeAlias->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
	typeAlias->mRebuildFlags = BfTypeRebuildFlag_None;
	if ((typeAlias->mCustomAttributes == NULL) && (typeDef->mTypeDeclaration != NULL) && (typeDef->mTypeDeclaration->mAttributes != NULL))
		typeAlias->mCustomAttributes = GetCustomAttributes(typeDef->mTypeDeclaration->mAttributes, BfAttributeTargets_Alias);

	if (typeAlias->mGenericTypeInfo != NULL)
	{
		DoPopulateType_SetGenericDependencies(typeAlias);
	}
}

void BfModule::DoPopulateType_InitSearches(BfTypeInstance* typeInstance)
{
	auto typeDef = typeInstance->mTypeDef;

	auto _AddStaticSearch = [&](BfTypeDef* typeDef)
	{
		if (!typeDef->mStaticSearch.IsEmpty())
		{
			BfStaticSearch* staticSearch;
			if (typeInstance->mStaticSearchMap.TryAdd(typeDef, NULL, &staticSearch))
			{
				SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, typeDef);
				for (auto typeRef : typeDef->mStaticSearch)
				{
					auto staticType = ResolveTypeRef(typeRef, NULL, BfPopulateType_Identity);
					if (staticType != NULL)
					{
						auto staticTypeInst = staticType->ToTypeInstance();
						if (staticTypeInst == NULL)
						{
							Fail(StrFormat("Type '%s' cannot be used in a 'using static' declaration", TypeToString(staticType).c_str()), typeRef);
						}
						else
						{
							staticSearch->mStaticTypes.Add(staticTypeInst);
							AddDependency(staticTypeInst, typeInstance, BfDependencyMap::DependencyFlag_StaticValue);
						}
					}
				}
			}
		}
		if (!typeDef->mInternalAccessSet.IsEmpty())
		{
			BfInternalAccessSet* internalAccessSet;
			BF_ASSERT(!typeDef->IsEmitted());
			if (typeInstance->mInternalAccessMap.TryAdd(typeDef, NULL, &internalAccessSet))
			{
				for (auto typeRef : typeDef->mInternalAccessSet)
				{
					if ((typeRef->IsA<BfNamedTypeReference>()) ||
						(typeRef->IsA<BfQualifiedTypeReference>()))
					{
						String checkNamespaceStr;
						typeRef->ToString(checkNamespaceStr);
						BfAtomCompositeT<16> checkNamespace;
						if (mSystem->ParseAtomComposite(checkNamespaceStr, checkNamespace))
						{
							if (mSystem->ContainsNamespace(checkNamespace, typeDef->mProject))
							{
								mSystem->RefAtomComposite(checkNamespace);
								internalAccessSet->mNamespaces.Add(checkNamespace);
								continue;
							}
						}
					}

					BfType* internalType = NULL;
					if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
						internalType = mContext->mScratchModule->ResolveTypeRefAllowUnboundGenerics(typeRef, BfPopulateType_Identity);
					else
						internalType = ResolveTypeRef(typeRef, NULL, BfPopulateType_Identity);
					if (internalType != NULL)
					{
						auto internalTypeInst = internalType->ToTypeInstance();
						if (internalTypeInst == NULL)
						{
							Fail(StrFormat("Type '%s' cannot be used in a 'using internal' declaration", TypeToString(internalType).c_str()), typeRef);
						}
						else
						{
							internalAccessSet->mTypes.Add(internalTypeInst);
							AddDependency(internalTypeInst, typeInstance, BfDependencyMap::DependencyFlag_StaticValue);
						}
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

void BfModule::DoPopulateType_FinishEnum(BfTypeInstance* typeInstance, bool underlyingTypeDeferred, HashContext* dataMemberHashCtx, BfType* unionInnerType)
{
	if (typeInstance->IsEnum())
	{
		int64 min = 0;
		int64 max = 0;

		bool isFirst = true;

		if (typeInstance->mTypeInfoEx == NULL)
			typeInstance->mTypeInfoEx = new BfTypeInfoEx();

		bool isAllInt64 = true;

		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			if (fieldDef != NULL)
			{
				if ((fieldInstance->mConstIdx == -1) || (fieldInstance->mResolvedType != typeInstance))
					continue;

				auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
				if (constant->mTypeCode != BfTypeCode_Int64)
					isAllInt64 = false;

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

			if (typeInstance->mIsCRepr)
				typeCode = BfTypeCode_Int32;

			if ((typeCode != BfTypeCode_Int64) || (!isAllInt64))
			{
				for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
				{
					auto fieldInstance = &fieldInstanceRef;
					if ((fieldInstance->mConstIdx == -1) || (fieldInstance->mResolvedType != typeInstance))
						continue;
					auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
					if (constant->mTypeCode == typeCode)
						continue;
					BfIRValue newConstant = typeInstance->mConstHolder->CreateConst(typeCode, constant->mUInt64);
					fieldInstance->mConstIdx = newConstant.mId;
				}
			}

			BfType* underlyingType = GetPrimitiveType(typeCode);
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
		typeInstance->mAlign = unionInnerType->mAlign;

		int lastTagId = -1;
		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			if ((fieldDef != NULL) && (fieldInstance->mDataIdx < 0))
			{
				BF_ASSERT(fieldInstance->mResolvedType->mAlign >= 1);
				typeInstance->mAlign = BF_MAX(typeInstance->mAlign, fieldInstance->mResolvedType->mAlign);
				lastTagId = -fieldInstance->mDataIdx - 1;
			}
		}

		auto fieldInstance = &typeInstance->mFieldInstances.back();
		//BF_ASSERT(fieldInstance->mResolvedType == NULL);
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
		if (typeInstance->mPacking == 0)
		{
			if ((fieldInstance->mDataOffset % discriminatorType->mAlign) != 0)
			{
				fieldInstance->mDataOffset = BF_ALIGN(fieldInstance->mDataOffset, discriminatorType->mAlign);
				fieldInstance->mDataIdx++; // Add room for explicit padding
			}
		}

		typeInstance->mAlign = BF_MAX(typeInstance->mAlign, discriminatorType->mAlign);
		typeInstance->mSize = fieldInstance->mDataOffset + discriminatorType->mSize;

		typeInstance->mInstSize = typeInstance->mSize;
		typeInstance->mInstAlign = typeInstance->mAlign;

		if (dataMemberHashCtx != NULL)
		{
			dataMemberHashCtx->Mixin(unionInnerType->mTypeId);
			dataMemberHashCtx->Mixin(discriminatorType->mTypeId);
		}

		typeInstance->mMergedFieldDataCount = 1; // Track it as a single entry
	}
}

void BfModule::DoPopulateType_CeCheckEnum(BfTypeInstance* typeInstance, bool underlyingTypeDeferred)
{
	if (!typeInstance->IsEnum())
		return;
	if ((!underlyingTypeDeferred) && (!typeInstance->IsPayloadEnum()))
		return;
	if ((typeInstance->mCeTypeInfo != NULL) && (typeInstance->mCeTypeInfo->mNext != NULL))
		return;

	BfType* unionInnerType = NULL;
	if (typeInstance->mIsUnion)
	{
		SetAndRestoreValue<BfTypeState::ResolveKind> prevResolveKind(mContext->mCurTypeState->mResolveKind, BfTypeState::ResolveKind_UnionInnerType);
		unionInnerType = typeInstance->GetUnionInnerType();
	}
	DoPopulateType_FinishEnum(typeInstance, underlyingTypeDeferred, NULL, unionInnerType);
}

void BfModule::DoPopulateType(BfType* resolvedTypeRef, BfPopulateType populateType)
{
	if (populateType == BfPopulateType_Identity)
		return;

	if ((populateType <= BfPopulateType_Data) && (resolvedTypeRef->mDefineState >= BfTypeDefineState_Defined))
		return;

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
			return;
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
		return;

	// Partial population break out point
	if ((populateType >= BfPopulateType_Identity) && (populateType <= BfPopulateType_IdentityNoRemapAlias))
		return;

	if ((populateType <= BfPopulateType_AllowStaticMethods) && (typeInstance->mDefineState >= BfTypeDefineState_HasInterfaces_Direct))
		return;

	// During CE init we need to avoid interface checking loops, so we only allow show direct interface declarations
	if ((populateType == BfPopulateType_Interfaces_All) && (typeInstance->mDefineState >= Beefy::BfTypeDefineState_CETypeInit))
	{
		if ((typeInstance->mDefineState == Beefy::BfTypeDefineState_CEPostTypeInit) && (typeInstance->mCeTypeInfo != NULL) &&
			(!typeInstance->mCeTypeInfo->mPendingInterfaces.IsEmpty()))
		{
			// We have finished CETypeInit and we have pending interfaces we need to apply
		}
		else
			return;
	}

	if (!mCompiler->EnsureCeUnpaused(resolvedTypeRef))
	{
		// We need to avoid comptime reentry when the ceDebugger is paused
		BfLogSysM("DoPopulateType %p bailing due to IsCePaused\n", resolvedTypeRef);
		return;
	}

	auto _CheckTypeDone = [&]()
	{
		if (typeInstance->mNeedsMethodProcessing)
		{
			BF_ASSERT(typeInstance->mDefineState >= BfTypeDefineState_Defined);
			if ((canDoMethodProcessing) && (populateType >= BfPopulateType_DataAndMethods))
				DoTypeInstanceMethodProcessing(typeInstance);
			return true;
		}
		if (typeInstance->mDefineState == BfTypeDefineState_DefinedAndMethodsSlotted)
			return true;
		return false;
	};

	if (_CheckTypeDone())
		return;

	if (!resolvedTypeRef->IsValueType())
	{
		resolvedTypeRef->mSize = typeInstance->mAlign = mSystem->mPtrSize;
	}

	BF_ASSERT((typeInstance->mMethodInstanceGroups.size() == 0) || (typeInstance->mMethodInstanceGroups.size() == typeDef->mMethods.size()) || (typeInstance->mCeTypeInfo != NULL) || (typeInstance->IsBoxed()));
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

	// WHY were we clearing these values?
	//SetAndRestoreValue<bool> prevHadError(mHadBuildError, false);
	//SetAndRestoreValue<bool> prevHadWarning(mHadBuildWarning, false);

	BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
	typeState.mPopulateType = populateType;
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfTypeInstance*)typeInstance;
		if (!genericTypeInst->mGenericTypeInfo->mInitializedGenericParams)
			InitGenericParams(resolvedTypeRef);
	}

	if (resolvedTypeRef->IsTypeAlias())
	{
		prevTypeState.Restore();
		DoPopulateType_TypeAlias((BfTypeAliasType*)typeInstance);

		typeInstance->mTypeIncomplete = false;
		resolvedTypeRef->mRebuildFlags = BfTypeRebuildFlag_None;
		resolvedTypeRef->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
		return;
	}

	if (_CheckTypeDone())
		return;

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
		if (typeInstance->mTypeFailed)
			return;
	}

	if (!typeInstance->mTypeFailed)
	{
		if (populateType == BfPopulateType_Data_Soft)
		{
			if (CheckCircularDataError(false))
				return;
		}

		CheckCircularDataError();
	}

	if (typeInstance->mDefineState < BfTypeDefineState_Declaring)
	{
		typeInstance->mDefineState = BfTypeDefineState_Declaring;
		DoPopulateType_InitSearches(typeInstance);
	}

	//
	{
		BP_ZONE("DoPopulateType:CheckStack");
		StackHelper stackHelper;
		if (!stackHelper.CanStackExpand(128 * 1024))
		{
			if (!stackHelper.Execute([&]()
				{
					DoPopulateType(resolvedTypeRef, populateType);
				}))
			{
				Fail("Stack exhausted in DoPopulateType", typeDef->GetRefNode());
			}
			return;
		}
	}

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

			BfAstNode* deferredErrorNode = NULL;
			const char* deferredError = NULL;

			for (auto baseTypeRef : typeDef->mBaseTypes)
			{
				SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, baseTypeRef);
				SetAndRestoreValue<BfTypeDefineState> prevDefineState(typeInstance->mDefineState, BfTypeDefineState_ResolvingBaseType);
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
							deferredError = "Underlying enum type already specified";
							deferredErrorNode = baseTypeRef;
						}
					}
					else
					{
						deferredError = "Invalid underlying enum type";
						deferredErrorNode = baseTypeRef;
					}
				}
				else
				{
					AssertErrorState();
					TypeFailed(typeInstance);
				}
			}

			if (deferredError != NULL)
				Fail(deferredError, deferredErrorNode, true);

			if (underlyingType == NULL)
			{
				underlyingType = GetPrimitiveType(BfTypeCode_Int64);
				underlyingTypeDeferred = true;
			}
		}
	}
// 	else if (typeInstance->IsFunction())
// 	{
// 		underlyingType = GetPrimitiveType(BfTypeCode_NullPtr);
// 	}
	else if (((typeInstance->IsStruct()) || (typeInstance->IsTypedPrimitive())) &&
		(!typeInstance->mTypeFailed))
	{
		for (auto baseTypeRef : typeDef->mBaseTypes)
		{
			auto declTypeDef = typeDef;
			if (typeDef->mIsCombinedPartial)
				declTypeDef = typeDef->mPartials.front();
			SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, declTypeDef);
			SetAndRestoreValue<BfTypeDefineState> prevDefineState(typeInstance->mDefineState, BfTypeDefineState_ResolvingBaseType);
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, baseTypeRef);
			// We ignore errors here to avoid double-errors for type lookups, but this is where data cycles are detected
			//  but that type of error supersedes the mIgnoreErrors setting
			SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
			// Temporarily allow us to derive from private classes, to avoid infinite loop from TypeIsSubTypeOf
			SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);
			auto baseType = ResolveTypeRef(baseTypeRef, BfPopulateType_Declaration);
			if (baseType != NULL)
			{
				if (baseType->IsVar())
				{
					// Ignore
				}
				else if (baseType->IsPrimitiveType())
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
				TypeFailed(typeInstance);
			}

			if (_CheckTypeDone())
			{
				prevDefineState.CancelRestore();
				return;
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
		typeInstance->mDefineState = BfTypeDefineState_Declared;

	if (populateType == BfPopulateType_Declaration)
	{
		return;
	}

	if ((!mCompiler->mIsResolveOnly) && (!typeInstance->HasBeenInstantiated()))
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

	typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags | BfTypeRebuildFlag_ResolvingBase);

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

	struct _DeferredValidate
	{
		BfTypeReference* mTypeRef;
		BfTypeInstance* mGenericType;
		bool mIgnoreErrors;
	};
	Array<_DeferredValidate> deferredTypeValidateList;

	bool wantPopulateInterfaces = false;

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
			if ((typeInstance->mDefineState == BfTypeDefineState_ResolvingBaseType) && (typeInstance->mTypeFailed))
				break;

			auto declTypeDef = typeDef;
			if (typeDef->mIsCombinedPartial)
				declTypeDef = typeDef->mPartials.front();
			SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, declTypeDef);
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, checkTypeRef);
			SetAndRestoreValue<BfTypeDefineState> prevDefineState(typeInstance->mDefineState, BfTypeDefineState_ResolvingBaseType);

			bool populateBase = !typeInstance->mTypeFailed;
			BfType* checkType = checkType = ResolveTypeRef(checkTypeRef, BfPopulateType_Declaration);

			if ((checkType != NULL) && (!checkType->IsInterface()) && (populateBase))
			{
				SetAndRestoreValue<BfTypeInstance*> prevBaseType(mContext->mCurTypeState->mCurBaseType, checkType->ToTypeInstance());
				PopulateType(checkType, (populateType <= BfPopulateType_BaseType) ? BfPopulateType_BaseType : BfPopulateType_Data);
			}

			if (typeInstance->mDefineState >= BfTypeDefineState_Defined)
			{
				prevDefineState.CancelRestore();
				return;
			}

			if (checkType != NULL)
			{
				if (auto genericTypeInst = checkType->ToGenericTypeInstance())
				{
					// Specialized type variations don't need to validate their constraints
					if (!typeInstance->IsUnspecializedTypeVariation())
						deferredTypeValidateList.Add({ checkTypeRef, genericTypeInst, false });
				}

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
							checkTypeRef, true);
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

				if (checkType->IsVar())
				{
					// This can't explicitly be specified, but can occur from comptime
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
						ifaceDecl.mDeclaringType = typeDef->GetDefinition();
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
							auto checkOuter = checkTypeInst;
							while (checkOuter != NULL)
							{
								if (checkOuter == typeInstance)
								{
									Fail(StrFormat("Type '%s' cannot be declare inner type '%s' as a base type",
										TypeToString(typeInstance).c_str(),
										TypeToString(checkTypeInst).c_str()), checkTypeRef, true);
									checkTypeInst = NULL;
									break;
								}
								checkOuter = GetOuterType(checkOuter);
							}
						}

						if (checkTypeInst != NULL)
						{
							baseType = checkTypeInst;
						}
					}
				}

				if (_CheckTypeDone())
				{
					prevDefineState.CancelRestore();
					return;
				}
			}
			else
			{
				AssertErrorState();
				// Why did we go around setting mTypeFailed on all these things?
				//typeInstance->mTypeFailed = true;
			}
		}

		wantPopulateInterfaces = true;
	}

	if ((typeInstance->mCeTypeInfo != NULL) && (!typeInstance->mCeTypeInfo->mPendingInterfaces.IsEmpty()))
	{
		for (auto ifaceTypeId : typeInstance->mCeTypeInfo->mPendingInterfaces)
		{
			auto ifaceType = mContext->mTypes[ifaceTypeId];
			if ((ifaceType == NULL) || (!ifaceType->IsInterface()))
				continue;
			auto ifaceInst = ifaceType->ToTypeInstance();

			if (ifaceSet.Add(ifaceInst))
			{
				// Not base type
				BfInterfaceDecl ifaceDecl;
				ifaceDecl.mIFaceTypeInst = ifaceInst;
				ifaceDecl.mTypeRef = NULL;
				ifaceDecl.mDeclaringType = typeDef->GetDefinition();
				interfaces.Add(ifaceDecl);
			}
		}
	}

	if (_CheckTypeDone())
		return;

	if (resolvedTypeRef->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)resolvedTypeRef;

		if ((baseType != NULL) && (baseType->IsStruct()))
		{
			BfType* modifiedBaseType = baseType;
			if (boxedType->IsBoxedStructPtr())
				modifiedBaseType = CreatePointerType(modifiedBaseType);
			boxedType->mBoxedBaseType = CreateBoxedType(modifiedBaseType);

			PopulateType(boxedType->mBoxedBaseType);
			// Use derivedFrom for both the boxed base type and the unboxed type
			AddDependency(boxedType->mBoxedBaseType, typeInstance, BfDependencyMap::DependencyFlag_DerivedFrom);
		}

		AddDependency(boxedType->mElementType, typeInstance, BfDependencyMap::DependencyFlag_ValueTypeMemberData);

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

	if (auto genericTypeInst = typeInstance->ToGenericTypeInstance())
	{
		if ((genericTypeInst->IsSpecializedType()) && (!genericTypeInst->mGenericTypeInfo->mValidatedGenericConstraints) && (!typeInstance->IsBoxed()))
		{
			deferredTypeValidateList.Add({ NULL, genericTypeInst, true });
		}
	}

	if (!typeInstance->IsBoxed())
	{
		BfType* outerType = GetOuterType(typeInstance);
		if (outerType != NULL)
		{
			PopulateType(outerType, BfPopulateType_Identity);
			AddDependency(outerType, typeInstance, BfDependencyMap::DependencyFlag_OuterType);
		}
	}

	if ((typeInstance->IsInterface()) && (baseTypeInst != NULL))
	{
		Fail("Interfaces cannot declare base types", baseTypeRef, true);
		baseTypeInst = NULL;
	}

	if ((baseTypeInst != NULL) && (typeInstance->mBaseType == NULL))
	{
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
			baseTypeInst = defaultBaseTypeInst;
			typeInstance->mBaseType = baseTypeInst;
		}
		else if ((resolvedTypeRef->IsStruct()) && (!baseTypeInst->IsValueType()))
		{
			Fail("Struct can only derive from another struct", baseTypeRef, true);
			baseTypeInst = defaultBaseTypeInst;
			typeInstance->mBaseType = baseTypeInst;
		}

		if (!typeInstance->IsIncomplete())
		{
			// Re-entry may cause this type to be completed already
			return;
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

	typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags & ~BfTypeRebuildFlag_ResolvingBase);

	if (populateType <= BfPopulateType_BaseType)
		return;

	if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfTypeInstance*)typeInstance;
// 		if (!genericTypeInst->mGenericTypeInfo->mInitializedGenericParams)
// 			InitGenericParams(resolvedTypeRef);
		if (!genericTypeInst->mGenericTypeInfo->mFinishedGenericParams)
 			FinishGenericParams(resolvedTypeRef);
	}

	if (wantPopulateInterfaces)
	{
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

	if ((typeInstance->mBaseType != NULL) && (!typeInstance->IsTypedPrimitive()))
	{
		curFieldDataIdx++;
	}

	if (!interfaces.empty())
	{
		for (int iFaceIdx = 0; iFaceIdx < (int)interfaces.size(); iFaceIdx++)
		{
			auto checkInterface = interfaces[iFaceIdx].mIFaceTypeInst;

			SetAndRestoreValue<BfTypeDef*> prevTypeDef(mContext->mCurTypeState->mCurTypeDef, interfaces[iFaceIdx].mDeclaringType);
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurBaseTypeRef, interfaces[iFaceIdx].mTypeRef);

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

			AddDependency(checkInterface, typeInstance, BfDependencyMap::DependencyFlag_ImplementsInterface);

			// Interfaces can list other interfaces in their declaration, so pull those in too
			for (auto depIFace : checkInterface->mInterfaces)
			{
				auto depIFaceEntry = interfaces[iFaceIdx];
				depIFaceEntry.mIFaceTypeInst = depIFace.mInterfaceType;
				interfaces.push_back(depIFaceEntry);
			}
		}

		if (typeInstance->mTypeFailed)
		{
			// Circular references in interfaces - just clear them all out
			typeInstance->mInterfaces.Clear();
			interfaces.Clear();
		}

		if (_CheckTypeDone())
			return;
	}

	if ((mCompiler->mOptions.mAllowHotSwapping) &&
		(typeInstance->mDefineState < BfTypeDefineState_HasInterfaces_Direct) &&
		(typeInstance->mDefineState != BfTypeDefineState_ResolvingBaseType))
	{
		if (typeInstance->mHotTypeData == NULL)
		{
			typeInstance->mHotTypeData = new BfHotTypeData();
			BfLogSysM("Created HotTypeData %p created for type %p in DoPopulateType\n", typeInstance->mHotTypeData, typeInstance);
		}

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

		BfLogSysM("BfHotTypeVersion %p created for type %p\n", hotTypeVersion, typeInstance);
	}

	BF_ASSERT(!typeInstance->mNeedsMethodProcessing);
	if (typeInstance->mDefineState < BfTypeDefineState_HasInterfaces_Direct)
		typeInstance->mDefineState = BfTypeDefineState_HasInterfaces_Direct;

	for (auto& validateEntry : deferredTypeValidateList)
	{
		SetAndRestoreValue<BfTypeReference*> prevAttributeTypeRef(typeState.mCurAttributeTypeRef, validateEntry.mTypeRef);
		SetAndRestoreValue<bool> ignoreErrors(mIgnoreErrors, mIgnoreErrors | validateEntry.mIgnoreErrors);
		ValidateGenericConstraints(validateEntry.mTypeRef, validateEntry.mGenericType, false);
	}

	bool isRootSystemType = typeInstance->IsInstanceOf(mCompiler->mValueTypeTypeDef) ||
		typeInstance->IsInstanceOf(mCompiler->mAttributeTypeDef) ||
		typeInstance->IsInstanceOf(mCompiler->mEnumTypeDef);

	if (!typeInstance->IsBoxed())
	{
		if ((typeInstance->mCustomAttributes == NULL) && (typeDef->mTypeDeclaration != NULL) && (typeDef->HasCustomAttributes()))
		{
			BfAttributeTargets attrTarget;
			if ((typeDef->mIsDelegate) || (typeDef->mIsFunction))
				attrTarget = BfAttributeTargets_Delegate;
			else if (typeInstance->IsEnum())
				attrTarget = BfAttributeTargets_Enum;
			else if (typeInstance->IsInterface())
				attrTarget = BfAttributeTargets_Interface;
			else if ((typeInstance->IsStruct()) || (typeInstance->IsTypedPrimitive()))
				attrTarget = BfAttributeTargets_Struct;
			else
				attrTarget = BfAttributeTargets_Class;
			if (!typeInstance->mTypeFailed)
			{
				BfTypeState typeState;
				typeState.mPrevState = mContext->mCurTypeState;
				typeState.mResolveKind = BfTypeState::ResolveKind_Attributes;
				typeState.mType = typeInstance;
				SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

				// This allows us to avoid reentrancy when checking for inner types
				SetAndRestoreValue<bool> prevSkipTypeProtectionChecks(typeInstance->mSkipTypeProtectionChecks, true);
				if (typeDef->mIsCombinedPartial)
				{
					auto customAttributes = new BfCustomAttributes();

					for (auto partialTypeDef : typeDef->mPartials)
					{
						if (partialTypeDef->mTypeDeclaration->mAttributes == NULL)
							continue;

						typeState.mCurTypeDef = partialTypeDef;
						GetCustomAttributes(customAttributes, partialTypeDef->mTypeDeclaration->mAttributes, attrTarget);
					}

					if (typeInstance->mCustomAttributes == NULL)
						typeInstance->mCustomAttributes = customAttributes;
					else
						delete customAttributes;
				}
				else
				{
					auto customAttributes = new BfCustomAttributes();
					GetCustomAttributes(customAttributes, typeDef->mTypeDeclaration->mAttributes, attrTarget);
					if (typeInstance->mCustomAttributes == NULL)
						typeInstance->mCustomAttributes = customAttributes;
					else
						delete customAttributes;
				}
			}
		}
	}

	if (typeInstance->mTypeOptionsIdx == -2)
	{
		SetTypeOptions(typeInstance);
	}

	if (populateType <= BfPopulateType_AllowStaticMethods)
		return;

	prevSkipTypeProtectionChecks.Restore();
	typeInstance->mInstSize = std::max(0, typeInstance->mInstSize);
	typeInstance->mInstAlign = std::max(0, typeInstance->mInstAlign);

	ProcessCustomAttributeData();
	int packing = 0;
	bool isUnion = false;
	bool isCRepr = false;
	bool isOrdered = false;
	int alignOverride = 0;
	BfType* underlyingArrayType = NULL;
	int underlyingArraySize = -1;
	ProcessTypeInstCustomAttributes(packing, isUnion, isCRepr, isOrdered, alignOverride, underlyingArrayType, underlyingArraySize);
	if (underlyingArraySize > 0)
	{
		typeInstance->mHasUnderlyingArray = true;
		curFieldDataIdx = 0;
	}
	if (packing > 0) // Packed infers ordered
		isOrdered = true;
	typeInstance->mIsUnion = isUnion;
	if ((typeInstance->IsEnum()) && (typeInstance->IsStruct()))
		typeInstance->mIsUnion = true;
	typeInstance->mPacking = (uint8)packing;
	typeInstance->mIsCRepr = isCRepr;

	if (typeInstance->mTypeOptionsIdx >= 0)
	{
		auto typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);
		if (typeOptions != NULL)
		{
			typeInstance->mHasBeenInstantiated = typeOptions->Apply(typeInstance->HasBeenInstantiated(), BfOptionFlags_ReflectAssumeInstantiated);

			bool alwaysInclude = typeInstance->mAlwaysIncludeFlags != 0;
			if ((typeOptions->Apply(alwaysInclude, BfOptionFlags_ReflectAlwaysIncludeType)) ||
				(typeOptions->Apply(alwaysInclude, BfOptionFlags_ReflectAlwaysIncludeAll)))
			{
				typeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(typeInstance->mAlwaysIncludeFlags | BfAlwaysIncludeFlag_Type);
			}
			else
			{
				typeInstance->mAlwaysIncludeFlags = BfAlwaysIncludeFlag_None;
			}
		}
	}

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

		auto innerTypeInst = innerType->ToTypeInstance();
		if (innerTypeInst != NULL)
		{
			if (typeInstance->mTypeDef != innerTypeInst->mTypeDef)
			{
				// Rebuild with proper typedef (generally from inner type comptime emission)
				BfLogSysM("Boxed type %p overriding typeDef to %p from inner type %p\n", typeInstance, innerTypeInst->mTypeDef, innerType);
				typeInstance->mTypeDef = innerTypeInst->mTypeDef;
				DoPopulateType(resolvedTypeRef, populateType);
				return;
			}

			while (typeInstance->mInterfaces.mSize < innerTypeInst->mInterfaces.mSize)
			{
				auto ifaceEntry = innerTypeInst->mInterfaces[typeInstance->mInterfaces.mSize];
				typeInstance->mInterfaces.Add(ifaceEntry);
				AddDependency(ifaceEntry.mInterfaceType, typeInstance, BfDependencyMap::DependencyFlag_ImplementsInterface);
			}
		}

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
				SetAndRestoreValue<BfTypeState::ResolveKind> prevResolveKind(mContext->mCurTypeState->mResolveKind, BfTypeState::ResolveKind_FieldType);

				BfType* resolvedFieldType = NULL;

				auto initializer = field->GetInitializer();

				if ((field->mIsAppend) && (!resolvedTypeRef->IsObject()))
					Fail("Appended objects can only be declared in class types", field->GetFieldDeclaration()->mExternSpecifier, true);

				if ((field->mIsAppend) && (isUnion))
					Fail("Appended objects cannot be declared in unions", field->GetFieldDeclaration()->mExternSpecifier, true);

				if (field->IsEnumCaseEntry())
				{
					if (typeInstance->IsEnum())
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
					else
					{
						Fail("Enum cases can only be declared within enum types", field->GetRefNode(), true);
						resolvedFieldType = typeInstance;
					}
				}
				else if ((field->mTypeRef != NULL) && ((field->mTypeRef->IsExact<BfVarTypeReference>()) || (field->mTypeRef->IsExact<BfLetTypeReference>()) || (field->mTypeRef->IsExact<BfExprModTypeRef>())))
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
					BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_NoResolveGenericParam;
					if (initializer != NULL)
						resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_AllowInferredSizedArray);
					resolvedFieldType = ResolveTypeRef(field->mTypeRef, BfPopulateType_Declaration, resolveFlags);
					if (resolvedFieldType == NULL)
					{
						// Failed, just put in placeholder 'var'
						AssertErrorState();
						resolvedFieldType = GetPrimitiveType(BfTypeCode_Var);
					}
				}

				if (resolvedFieldType->IsUndefSizedArray())
				{
					if (auto arrayTypeRef = BfNodeDynCast<BfArrayTypeRef>(field->mTypeRef))
					{
						if (arrayTypeRef->IsInferredSize())
						{
							if (initializer != NULL)
							{
								DeferredResolveEntry resolveEntry;
								resolveEntry.mFieldDef = field;
								resolveEntry.mTypeArrayIdx = (int)llvmFieldTypes.size();
								deferredVarResolves.push_back(resolveEntry);

								fieldInstance->mIsInferredType = true;
							}
							else
							{
								AssertErrorState();
							}
						}
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
			return;
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
			return;

		bool hadSoftFail = false;

		for (auto& fieldInstanceRef : typeInstance->mFieldInstances)
		{
			auto fieldInstance = &fieldInstanceRef;
			auto fieldDef = fieldInstance->GetFieldDef();
			auto resolvedFieldType = fieldInstance->GetResolvedType();

			if (!fieldInstance->mFieldIncluded)
				continue;

			if (fieldInstance->mCustomAttributes != NULL)
			{
				// Already handled
			}
			else if ((fieldDef != NULL) && (fieldDef->GetFieldDeclaration() != NULL) && (fieldDef->GetFieldDeclaration()->mAttributes != NULL) && (!typeInstance->mTypeFailed) && (!isRootSystemType))
			{
				if (auto propDecl = BfNodeDynCast<BfPropertyDeclaration>(fieldDef->mFieldDeclaration))
				{
					// Handled elsewhere
				}
				else
				{
					SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);

					fieldInstance->mCustomAttributes = GetCustomAttributes(fieldDef->GetFieldDeclaration()->mAttributes, fieldDef->mIsStatic ? BfAttributeTargets_StaticField : BfAttributeTargets_Field);
					for (auto customAttr : fieldInstance->mCustomAttributes->mAttributes)
					{
						if (TypeToString(customAttr.mType) == "System.ThreadStaticAttribute")
						{
							if ((!fieldDef->mIsStatic) || (fieldDef->mIsConst))
							{
								Fail("ThreadStatic attribute can only be used on static fields", fieldDef->GetFieldDeclaration()->mAttributes);
							}
						}
					}
				}
			}

			if (resolvedFieldType == NULL)
			{
				if ((underlyingType != NULL) || (typeInstance->IsPayloadEnum()))
					continue;
			}

			if (fieldDef == NULL)
				continue;

			if ((!fieldDef->mIsStatic) && (resolvedFieldType->IsValueType()))
			{
				// We need that type finished up for alignment and data size
				//  But if the type has failed then we need to avoid stack overflow so we don't finish it
				SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);
				bool populateChildType = !typeInstance->mTypeFailed;
				//bool populateChildType = true;
				PopulateType(resolvedFieldType, populateChildType ? ((populateType == BfPopulateType_Data_Soft) ? BfPopulateType_Data_Soft : BfPopulateType_Data) : BfPopulateType_Declaration);

				if (populateType == BfPopulateType_Data_Soft)
				{
					if (resolvedFieldType->IsDataIncomplete())
						hadSoftFail = true;
				}
				else if (populateChildType)
				{
					if (resolvedFieldType->IsFinishingType())
					{
						AssertErrorState();
					}
					else
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

		if (hadSoftFail)
			return;

		bool tryCE = true;
 		if (typeInstance->mDefineState == BfTypeDefineState_CETypeInit)
 		{
 			if (populateType <= BfPopulateType_AllowStaticMethods)
 				return;

			int foundTypeCount = 0;
			auto typeState = mContext->mCurTypeState;
			while (typeState != NULL)
			{
				if (typeState->mType == typeInstance)
				{
					foundTypeCount++;
					if (foundTypeCount == 2)
						break;
				}
				typeState = typeState->mPrevState;
			}

			if ((foundTypeCount >= 2) || (typeInstance->mTypeDef->IsEmitted()))
			{
				String error = "OnCompile const evaluation creates a data dependency during TypeInit";
				if (mCompiler->mCeMachine->mCurBuilder != NULL)
				{
					error += StrFormat(" during const-eval generation of '%s'", MethodToString(mCompiler->mCeMachine->mCurBuilder->mCeFunction->mMethodInstance).c_str());
				}

				auto refNode = typeDef->GetRefNode();
				Fail(error, refNode);
				if ((mCompiler->mCeMachine->mCurContext != NULL) && (mCompiler->mCeMachine->mCurContext->mCurFrame != NULL))
					mCompiler->mCeMachine->mCurContext->Fail(*mCompiler->mCeMachine->mCurContext->mCurFrame, error);
				else if (mCompiler->mCeMachine->mCurContext != NULL)
					mCompiler->mCeMachine->mCurContext->Fail(error);
				tryCE = false;
			}
 		}

		if ((typeInstance->mDefineState < BfTypeDefineState_CEPostTypeInit) && (tryCE))
 		{
			BF_ASSERT(!typeInstance->mTypeDef->IsEmitted());

			if (typeInstance->mCeTypeInfo != NULL)
				typeInstance->mCeTypeInfo->mPendingInterfaces.Clear();

 			typeInstance->mDefineState = BfTypeDefineState_CETypeInit;
			bool hadNewMembers = false;
			DoCEEmit(typeInstance, hadNewMembers, underlyingTypeDeferred);

 			if (typeInstance->mDefineState < BfTypeDefineState_CEPostTypeInit)
 				typeInstance->mDefineState = BfTypeDefineState_CEPostTypeInit;

			if (typeInstance->mCeTypeInfo != NULL)
			{
				bool prevHadEmissions = !typeInstance->mCeTypeInfo->mEmitSourceMap.IsEmpty();

				if (typeInstance->mCeTypeInfo->mNext != NULL)
				{
					BfLogSysM("Type %p injecting next ceTypeInfo %p into ceTypeInfo %p\n", typeInstance, typeInstance->mCeTypeInfo->mNext, typeInstance->mCeTypeInfo);

					auto ceInfo = typeInstance->mCeTypeInfo->mNext;

					HashContext hashCtx;
					hashCtx.Mixin(ceInfo->mEmitSourceMap.mCount);
					for (auto& kv : ceInfo->mEmitSourceMap)
					{
						hashCtx.Mixin(kv.mKey);
						hashCtx.Mixin(kv.mValue.mKind);
						hashCtx.Mixin(kv.mValue.mSrcStart);
						hashCtx.Mixin(kv.mValue.mSrcEnd);
					}
					hashCtx.Mixin(ceInfo->mOnCompileMap.mCount);
					for (auto& kv : ceInfo->mOnCompileMap)
					{
						hashCtx.Mixin(kv.mKey);
						hashCtx.MixinStr(kv.mValue.mEmitData);
					}
					hashCtx.Mixin(ceInfo->mTypeIFaceMap.mCount);
					for (auto& kv : ceInfo->mTypeIFaceMap)
					{
						hashCtx.Mixin(kv.mKey);
						hashCtx.MixinStr(kv.mValue.mEmitData);
					}

					typeInstance->mCeTypeInfo->mNext->mHash = hashCtx.Finish128();

					if (!typeInstance->mCeTypeInfo->mNext->mFastFinished)
					{
						if ((typeInstance->mCeTypeInfo->mHash != typeInstance->mCeTypeInfo->mNext->mHash) && (!typeInstance->mCeTypeInfo->mHash.IsZero()))
							mContext->QueueMidCompileRebuildDependentTypes(typeInstance, "comptime hash changed");
						typeInstance->mCeTypeInfo->mEmitSourceMap = typeInstance->mCeTypeInfo->mNext->mEmitSourceMap;
						typeInstance->mCeTypeInfo->mOnCompileMap = typeInstance->mCeTypeInfo->mNext->mOnCompileMap;
						typeInstance->mCeTypeInfo->mTypeIFaceMap = typeInstance->mCeTypeInfo->mNext->mTypeIFaceMap;
						typeInstance->mCeTypeInfo->mHash = typeInstance->mCeTypeInfo->mNext->mHash;
					}

					delete typeInstance->mCeTypeInfo->mNext;
					typeInstance->mCeTypeInfo->mNext = NULL;
				}
				else
				{
					// Removed emissions
					if (!typeInstance->mCeTypeInfo->mHash.IsZero())
						mContext->QueueMidCompileRebuildDependentTypes(typeInstance, "comptime hash changed");
					typeInstance->mCeTypeInfo->mEmitSourceMap.Clear();
					typeInstance->mCeTypeInfo->mOnCompileMap.Clear();
					typeInstance->mCeTypeInfo->mTypeIFaceMap.Clear();
					typeInstance->mCeTypeInfo->mHash = Val128();
				}

				if (((typeInstance->mCeTypeInfo->mFailed) || (typeInstance->mTypeDef->HasParsingFailed())) &&
					(prevHadEmissions))
				{
					// Just add a marker to retain the previous open emits
					typeInstance->mCeTypeInfo->mEmitSourceMap[-1] = BfCeTypeEmitSource();
				}
				typeInstance->mCeTypeInfo->mFailed = false;
			}

			if ((typeInstance->mCeTypeInfo != NULL) && (!typeInstance->mCeTypeInfo->mPendingInterfaces.IsEmpty()))
				hadNewMembers = true;

			if ((typeInstance->mTypeDef->IsEmitted()) && (typeInstance->mCeTypeInfo == NULL))
			{
				BF_ASSERT(mCompiler->mCanceling);
				if (mCompiler->mCanceling)
				{
					TypeFailed(typeInstance);
					auto prevTypeDef = typeInstance->mTypeDef->mEmitParent;
					delete typeInstance->mTypeDef;
					typeInstance->mTypeDef = prevTypeDef;
					hadNewMembers = false;
				}
			}

			if (hadNewMembers)
			{
				// We need to avoid passing in BfPopulateType_Interfaces_All because it could cause us to miss out on new member processing,
				//  including resizing the method group table
				DoPopulateType(resolvedTypeRef, BF_MAX(populateType, BfPopulateType_Data));
				return;
			}

			if (_CheckTypeDone())
				return;
 		}
	}

	// Type now has interfaces added from CEInit
	if (typeInstance->mDefineState < BfTypeDefineState_HasInterfaces_All)
		typeInstance->mDefineState = BfTypeDefineState_HasInterfaces_All;

	if (_CheckTypeDone())
		return;

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
				SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, propDef);

				BfAttributeTargets target = BfAttributeTargets_Property;
				if (propDef->IsExpressionBodied())
					target = (BfAttributeTargets)(target | BfAttributeTargets_Method);

				if ((propDef->GetFieldDeclaration()->mAttributes != NULL) && (!typeInstance->mTypeFailed) && (!isRootSystemType))
				{
					auto customAttrs = GetCustomAttributes(propDef->GetFieldDeclaration()->mAttributes, target);
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
						if ((!interfaceFound) && (!typeInstance->mTypeFailed))
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
		dataMemberHashCtx.Mixin(typeInstance->mPacking);
		dataMemberHashCtx.Mixin(typeInstance->mIsCRepr);
		dataMemberHashCtx.Mixin(typeInstance->mIsUnion);

		int startDataPos = dataPos;
		int maxDataPos = dataPos;

		BfSizedVector<BfFieldInstance*, 16> dataFieldVec;

		bool allowInstanceFields = (underlyingType == NULL);
		if (typeInstance->IsTypedPrimitive())
			allowInstanceFields = false;

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
				AddDependency(resolvedFieldType, typeInstance, BfDependencyMap::DependencyFlag_ConstValue);
			}
			else if (fieldInstance->GetFieldDef() != NULL)
			{
				if (!fieldInstance->GetFieldDef()->mIsStatic)
					AddFieldDependency(typeInstance, fieldInstance, resolvedFieldType);
				else
					AddDependency(resolvedFieldType, typeInstance, BfDependencyMap::DependencyFlag_StaticValue);
			}

			auto fieldDef = fieldInstance->GetFieldDef();

			if (fieldInstance->mResolvedType != NULL)
			{
				auto resolvedFieldType = fieldInstance->GetResolvedType();
				if ((!typeInstance->IsBoxed()) && (fieldDef != NULL))
				{
					if ((fieldDef->mUsingProtection != BfProtection_Hidden) && (!resolvedFieldType->IsGenericParam()) && (!resolvedFieldType->IsObject()) && (!resolvedFieldType->IsStruct()))
						Warn(0, StrFormat("Field type '%s' is not applicable for 'using'", TypeToString(resolvedFieldType).c_str()), fieldDef->GetFieldDeclaration()->mConstSpecifier);

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
							Fail("Cannot declare instance member as 'extern'", fieldDef->GetFieldDeclaration()->mExternSpecifier, true);
						}

						BfAstNode* nameRefNode = NULL;
						if (auto fieldDecl = fieldDef->GetFieldDeclaration())
							nameRefNode = fieldDecl->mNameNode;
						else if (auto paramDecl = fieldDef->GetParamDeclaration())
							nameRefNode = paramDecl->mNameNode;
						if (nameRefNode == NULL)
							nameRefNode = fieldDef->mTypeRef;

						if (!allowInstanceFields)
						{
							if (typeInstance->IsEnum())
								Fail("Cannot declare instance members in an enum", nameRefNode, true);
							else if (typeInstance->IsFunction())
								Fail("Cannot declare instance members in a function", nameRefNode, true);
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

						if (fieldInstance->IsAppendedObject())
						{
							SetAndRestoreValue<BfFieldDef*> prevTypeRef(mContext->mCurTypeState->mCurFieldDef, fieldDef);
							SetAndRestoreValue<BfTypeState::ResolveKind> prevResolveKind(mContext->mCurTypeState->mResolveKind, BfTypeState::ResolveKind_FieldType);

							PopulateType(resolvedFieldType, BfPopulateType_Data);

							auto fieldTypeInst = resolvedFieldType->ToTypeInstance();
							dataSize = BF_MAX(fieldTypeInst->mInstSize, 0);
							alignSize = BF_MAX(fieldTypeInst->mInstAlign, 1);

							if (fieldTypeInst->mTypeFailed)
							{
								TypeFailed(fieldTypeInst);
								fieldInstance->mResolvedType = GetPrimitiveType(BfTypeCode_Var);
								continue;
							}

							if ((typeInstance != NULL) && (fieldTypeInst->mTypeDef->mIsAbstract))
							{
								Fail("Cannot create an instance of an abstract class", nameRefNode);
							}

							SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
							BfMethodState methodState;
							SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
							methodState.mTempKind = BfMethodState::TempKind_NonStatic;

							BfTypedValue appendIndexValue;
							BfExprEvaluator exprEvaluator(this);

							BfResolvedArgs resolvedArgs;

							auto fieldDecl = fieldDef->GetFieldDeclaration();
							if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(fieldDecl->mInitializer))
							{
								resolvedArgs.Init(invocationExpr->mOpenParen, &invocationExpr->mArguments, &invocationExpr->mCommas, invocationExpr->mCloseParen);
								exprEvaluator.ResolveArgValues(resolvedArgs, BfResolveArgsFlag_DeferParamEval);
							}

							BfFunctionBindResult bindResult;
							bindResult.mSkipThis = true;
							bindResult.mWantsArgs = true;
							SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(exprEvaluator.mFunctionBindResult, &bindResult);

							BfTypedValue emptyThis(mBfIRBuilder->GetFakeVal(), resolvedTypeRef, resolvedTypeRef->IsStruct());

							exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_Comptime;
							auto ctorResult = exprEvaluator.MatchConstructor(nameRefNode, NULL, emptyThis, fieldTypeInst, resolvedArgs, false, true);

							if ((bindResult.mMethodInstance != NULL) && (bindResult.mMethodInstance->mMethodDef->mHasAppend))
							{
								auto calcAppendMethodModule = GetMethodInstanceAtIdx(bindResult.mMethodInstance->GetOwner(), bindResult.mMethodInstance->mMethodDef->mIdx + 1, BF_METHODNAME_CALCAPPEND);

								SizedArray<BfIRValue, 2> irArgs;
								if (bindResult.mIRArgs.size() > 1)
									irArgs.Insert(0, &bindResult.mIRArgs[1], bindResult.mIRArgs.size() - 1);
								BfTypedValue appendSizeTypedValue = TryConstCalcAppend(calcAppendMethodModule.mMethodInstance, irArgs, true);
								if (appendSizeTypedValue)
								{
									int appendAlign = calcAppendMethodModule.mMethodInstance->mAppendAllocAlign;
									dataSize = BF_ALIGN(dataSize, appendAlign);
									alignSize = BF_MAX(alignSize, appendAlign);

									auto constant = mBfIRBuilder->GetConstant(appendSizeTypedValue.mValue);
									if (constant != NULL)
									{
										dataSize += constant->mInt32;
									}
								}
								else
								{
									Fail(StrFormat("Append constructor '%s' does not result in a constant size", MethodToString(bindResult.mMethodInstance).c_str()), nameRefNode);
								}
							}
						}
						else if (fieldDef->mIsAppend)
						{
							if (typeInstance->IsObject())
								Fail("Append fields can only be declared in classes", nameRefNode, true);
							else if ((!resolvedFieldType->IsObject()) && (!resolvedFieldType->IsGenericParam()))
								Fail("Append fields must be classes", nameRefNode, true);
						}

						BF_ASSERT(dataSize >= 0);
						fieldInstance->mDataSize = dataSize;
						if (!isUnion)
						{
							if (!resolvedFieldType->IsValuelessType())
							{
								dataFieldVec.push_back(fieldInstance);
							}
						}
						else
						{
							BF_ASSERT(resolvedFieldType->mSize >= 0);

							if (alignSize > 1)
								dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
							fieldInstance->mDataOffset = dataPos;

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
								if (populateType < BfPopulateType_Data)
								{
									// We don't actually need the data - bail out
									return;
								}

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
		{
			SetAndRestoreValue<BfTypeState::ResolveKind> prevResolveKind(typeState.mResolveKind, BfTypeState::ResolveKind_UnionInnerType);
			unionInnerType = typeInstance->GetUnionInnerType();
		}

		if (!isOrdered)
		{
			int dataFieldCount = (int)dataFieldVec.size();

			Array<Deque<BfFieldInstance*>> alignBuckets;
			for (auto fieldInst : dataFieldVec)
			{
				int alignBits = GetHighestBitSet(fieldInst->GetAlign(packing));
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
					curSize = BF_ALIGN(curSize, fieldInst->GetAlign(packing));
					curSize += fieldInst->mDataSize;
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
						 	curSize = BF_ALIGN(curSize, fieldInst->GetAlign(packing));
						 	curSize += fieldInst->mDataSize;
							break;
						}
					}
				}
			}
		}

		bool needsExplicitAlignment = true;

		for (int fieldIdx = 0; fieldIdx < (int)dataFieldVec.size(); fieldIdx++)
		{
			auto fieldInstance = dataFieldVec[fieldIdx];
			auto resolvedFieldType = fieldInstance->GetResolvedType();

			BF_ASSERT(resolvedFieldType->mSize >= 0);

			if (fieldInstance->mDataSize == 0)
				fieldInstance->mDataSize = resolvedFieldType->mSize;

			int dataSize = fieldInstance->mDataSize;
			int alignSize = fieldInstance->GetAlign(packing);

			int nextDataPos = dataPos;
			nextDataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);
			int padding = nextDataPos - dataPos;
			if ((alignSize > 1) && (needsExplicitAlignment) && (padding > 0))
			{
				curFieldDataIdx++;
			}
			dataPos = nextDataPos;
			fieldInstance->mDataOffset = dataPos;
			fieldInstance->mDataIdx = curFieldDataIdx++;

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

		if (alignOverride > 0)
			typeInstance->mInstAlign = alignOverride;
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
		{
			BF_ASSERT(typeInstance->mTypeFailed);
		}
		else
		{
			auto hotTypeVersion = typeInstance->mHotTypeData->mTypeVersions.back();

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
		}
	}

	if (_CheckTypeDone())
		return;

	if (typeInstance->mDefineState < BfTypeDefineState_Defined)
	{
		typeInstance->mDefineState = BfTypeDefineState_Defined;
		if (!typeInstance->IsBoxed())
		{
			ExecuteCEOnCompile(NULL, typeInstance, BfCEOnCompileKind_TypeDone, underlyingTypeDeferred);
			if (typeInstance->mDefineState == BfTypeDefineState_DefinedAndMethodsSlotted)
				return;
		}
	}

	if (typeInstance->mTypeFailed)
		mHadBuildError = true;

	CheckAddFailType();

	BF_ASSERT_REL(typeInstance->mDefineState != BfTypeDefineState_DefinedAndMethodsSlotting);
	BF_ASSERT_REL(typeInstance->mDefineState != BfTypeDefineState_DefinedAndMethodsSlotted);

	BfLogSysM("Setting mNeedsMethodProcessing=true on %p\n", typeInstance);
	typeInstance->mNeedsMethodProcessing = true;
	typeInstance->mIsFinishingType = false;

	///

	// 'Splattable' means that we can be passed via 3 or fewer primitive/pointer values
	if (typeInstance->mHasUnderlyingArray)
	{
		// Never splat
	}
	else if (typeInstance->IsStruct())
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
				if (checkType->IsValueType())
					PopulateType(checkType, BfPopulateType_Data);

				if (checkType->IsMethodRef())
				{
					// For simplicity, any methodRef inside a struct makes the struct non-splattable.  This reduces cases of needing to
					//  handle embedded methodRefs
					hadNonSplattable = true;
				}
				else if (checkType->IsOpaque())
				{
					hadNonSplattable = true;
				}
				else if (checkType->IsStruct())
				{
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
				if ((mCompiler->mOptions.mMachineType == BfMachineType_x86) && (mCompiler->mOptions.mPlatformType == BfPlatformType_Windows))
				{
					typeInstance->mIsSplattable = (dataCount <= 4) && (!hadNonSplattable) && (dataPos > 4);
				}
				else
					typeInstance->mIsSplattable = false;
			}
			else
				typeInstance->mIsSplattable = (dataCount <= 3) && (!hadNonSplattable);
		}
	}
	if (typeInstance->IsTypedPrimitive())
		typeInstance->mIsSplattable = true;
	if (typeInstance->mTypeDef->mIsOpaque)
		typeInstance->mIsSplattable = false;

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
			typeState.mType = typeInstance;
			SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

			if (BfNodeIsA<BfVarTypeReference>(propDef->mTypeRef))
			{
				// This is only valid for ConstEval properties
			}
			else
				ResolveTypeRef(propDef->mTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowRef);
		}
	}

	// Const handling
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

				// Check enum cases for duplicates
				if (mCurTypeInstance->IsEnum())
				{
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
	}

	if ((typeInstance->IsEnum()) && (!typeInstance->IsPayloadEnum()))
	{
		BfLogSysM("Setting underlying type %p %d\n", typeInstance, underlyingTypeDeferred);
	}

	DoPopulateType_FinishEnum(typeInstance, underlyingTypeDeferred, &dataMemberHashCtx, unionInnerType);

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

	auto _HandleTypeDeclaration = [&](BfTypeDeclaration* typeDeclaration)
	{
		if ((typeDeclaration != NULL) && (typeDeclaration->mNameNode != NULL))
		{
			auto typeRefSource = typeDeclaration->mNameNode->GetParserData();
			if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mIsClassifying) && (typeRefSource != NULL))
			{
				if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(typeDeclaration->mNameNode))
				{
					BfSourceElementType elemType = BfSourceElementType_Type;
					if (typeInstance->IsInterface())
						elemType = BfSourceElementType_Interface;
					else if (typeInstance->IsObject())
						elemType = BfSourceElementType_RefType;
					else if (typeInstance->IsStruct() || (typeInstance->IsTypedPrimitive() && !typeInstance->IsEnum()))
						elemType = BfSourceElementType_Struct;
					sourceClassifier->SetElementType(typeDeclaration->mNameNode, elemType);
				}
			}
		}
	};

	if (!typeInstance->IsBoxed())
	{
		_HandleTypeDeclaration(typeDef->mTypeDeclaration);
		for (auto partial : typeDef->mPartials)
			_HandleTypeDeclaration(partial->mTypeDeclaration);
	}

	if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfTypeInstance*)typeInstance;
		if (!genericTypeInst->mGenericTypeInfo->mFinishedGenericParams)
			FinishGenericParams(resolvedTypeRef);
	}

	if (populateType <= BfPopulateType_Data)
		return;

	disableYield.Release();
	prevTypeState.Restore();

	if (canDoMethodProcessing)
	{
		if (typeInstance->mNeedsMethodProcessing) // May have been handled by GetRawMethodInstanceAtIdx above
			DoTypeInstanceMethodProcessing(typeInstance);
	}
}

void BfModule::DoTypeInstanceMethodProcessing(BfTypeInstance* typeInstance)
{
	if (typeInstance->IsDeleting())
	{
		BF_ASSERT(typeInstance->IsOnDemand());
		return;
	}

	if (typeInstance->IsSpecializedByAutoCompleteMethod())
		return;

	if (typeInstance->mDefineState == BfTypeDefineState_DefinedAndMethodsSlotting)
	{
		BfLogSysM("DoTypeInstanceMethodProcessing %p re-entrancy exit\n", typeInstance);
		return;
	}

	//
	{
		BP_ZONE("DoTypeInstanceMethodProcessing:CheckStack");
		StackHelper stackHelper;
		if (!stackHelper.CanStackExpand(128 * 1024))
		{
			if (!stackHelper.Execute([&]()
				{
					DoTypeInstanceMethodProcessing(typeInstance);
				}))
			{
				Fail("Stack exhausted in DoPopulateType", typeInstance->mTypeDef->GetRefNode());
			}
			return;
		}
	}

	BF_ASSERT_REL(typeInstance->mNeedsMethodProcessing);
	BF_ASSERT_REL(typeInstance->mDefineState == BfTypeDefineState_Defined);
	typeInstance->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotting;

	BF_ASSERT(typeInstance->mModule == this);

	//TODO: This is new, make sure this is in the right place
	/*if (mAwaitingInitFinish)
		FinishInit();*/

	AutoDisallowYield disableYield(mSystem);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

	BfLogSysM("DoTypeInstanceMethodProcessing: %p %s Revision:%d DefineState:%d\n", typeInstance, TypeToString(typeInstance).c_str(), typeInstance->mRevision, typeInstance->mDefineState);

	auto typeDef = typeInstance->mTypeDef;

	BfTypeOptions* typeOptions = NULL;
	if (typeInstance->mTypeOptionsIdx >= 0)
		typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);

	// Generate all methods. Pass 0
	for (auto methodDef : typeDef->mMethods)
	{
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

		// Don't set these pointers during resolve pass because they may become invalid if it's just a temporary autocomplete method
		if (mCompiler->mResolvePassData == NULL)
		{
			if ((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mIsStatic))
			{
				typeInstance->mHasStaticInitMethod = true;
			}

			if ((methodDef->mMethodType == BfMethodType_Dtor) && (methodDef->mIsStatic))
			{
				typeInstance->mHasStaticDtorMethod = true;
			}

			if ((methodDef->mMethodType == BfMethodType_Normal) && (methodDef->mIsStatic) && (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))
			{
				typeInstance->mHasStaticMarkMethod = true;
			}

			if ((methodDef->mMethodType == BfMethodType_Normal) && (methodDef->mIsStatic) && (methodDef->mName == BF_METHODNAME_FIND_TLS_MEMBERS))
			{
				typeInstance->mHasTLSFindMethod = true;
			}
		}

		// Thsi MAY be generated already
		// This should still be set to the default value
		//BF_ASSERT((methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet) || (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude));
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
		// These may have been already added
		for (auto&& interfaceEntry : checkTypeInstance->mInterfaces)
			AddDependency(interfaceEntry.mInterfaceType, typeInstance, BfDependencyMap::DependencyFlag_ImplementsInterface);
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

	BfLogSysM("Setting mTypeIncomplete = false on %p\n", typeInstance);
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
		if ((typeInstance->IsSpecializedType()) || (typeInstance->IsUnspecializedTypeVariation()))
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

	if (TypeIsSubTypeOf(typeInstance, mCompiler->mAttributeTypeDef))
		wantsOnDemandMethods = false;

	if ((mCompiler->mResolvePassData != NULL) && (!mCompiler->mResolvePassData->mEmitEmbedEntries.IsEmpty()) && (typeInstance->IsSpecializedType()))
	{
		bool isCurrentEntry = false;

		auto _CheckEntry = [&](BfTypeDef* typeDef)
		{
			auto parser = typeDef->mTypeDeclaration->GetParser();
			if (parser != NULL)
				if (mCompiler->mResolvePassData->GetSourceClassifier(parser) != NULL)
					isCurrentEntry = true;
		};

		_CheckEntry(typeInstance->mTypeDef);
		for (auto& partial : typeInstance->mTypeDef->mPartials)
			_CheckEntry(partial);

		if (isCurrentEntry)
		{
			String typeName = TypeToString(typeInstance, BfTypeNameFlag_AddProjectName);
			if (mCompiler->mResolvePassData->mEmitEmbedEntries.ContainsKey(typeName))
			{
				wantsOnDemandMethods = false;
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

	bool isFailedType = mCurTypeInstance->mTypeFailed;
	if (auto genericTypeInst = mCurTypeInstance->ToGenericTypeInstance())
	{
		if (genericTypeInst->mGenericTypeInfo->mHadValidateErrors)
			isFailedType = true;
	}

	bool typeOptionsIncludeAll = false;
	bool typeOptionsIncludeFiltered = false;
	if (typeOptions != NULL)
	{
		typeOptionsIncludeAll = typeOptions->Apply(typeOptionsIncludeAll, BfOptionFlags_ReflectAlwaysIncludeAll);
		typeOptionsIncludeFiltered = typeOptions->Apply(typeOptionsIncludeAll, BfOptionFlags_ReflectAlwaysIncludeFiltered);
	}

	// Generate all methods. Pass 1
	for (auto methodDef : typeDef->mMethods)
	{
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

		if (typeOptions != NULL)
		{
			BfOptionFlags optionFlags = BfOptionFlags_ReflectNonStaticMethods;
			if (methodDef->mMethodType == BfMethodType_Ctor)
				optionFlags = BfOptionFlags_ReflectConstructors;
			else if (methodDef->mIsStatic)
				optionFlags = BfOptionFlags_ReflectStaticMethods;
			methodInstanceGroup->mExplicitlyReflected = typeOptions->Apply(false, optionFlags);
			methodInstanceGroup->mExplicitlyReflected = ApplyTypeOptionMethodFilters(methodInstanceGroup->mExplicitlyReflected, methodDef, typeOptions);
		}
		if ((typeInstance->mCustomAttributes != NULL) && (typeInstance->mCustomAttributes->Contains(mCompiler->mReflectAttributeTypeDef)))
			methodInstanceGroup->mExplicitlyReflected = true;

		if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude)
			continue;
		if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_InWorkList)
			continue;
		if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Referenced)
			continue;

		if (isFailedType)
		{
			// We don't want method decls from failed generic types to clog up our type system
			continue;
		}

		BF_ASSERT((methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet) ||
			(methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl) ||
			(methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference));

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
				{
					methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
					mOnDemandMethodCount++;
				}
				continue;
			}
		}

		if (methodDef->mMethodType == BfMethodType_Ignore)
			continue;

		if ((methodDef->mName == BF_METHODNAME_DYNAMICCAST) && (typeInstance->IsValueType()))
			continue; // This is just a placeholder for boxed types

		bool doAlwaysInclude = false;

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
					auto typeRefName = attributes->mAttributeTypeRef->ToCleanAttributeString();

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
			if (typeInstance->IncludeAllMethods())
				implRequired = true;

			// "AssumeInstantiated" also forces default ctor
			if (((typeInstance->mAlwaysIncludeFlags & BfAlwaysIncludeFlag_AssumeInstantiated) != 0) &&
				(methodDef->IsDefaultCtor()))
				implRequired = true;

			if ((typeOptionsIncludeAll || typeOptionsIncludeFiltered) && (ApplyTypeOptionMethodFilters(typeOptionsIncludeAll, methodDef, typeOptions)))
				implRequired = true;

// 			if ((typeOptions != NULL) && (CheckTypeOptionMethodFilters(typeOptions, methodDef)))
// 				implRequired = true;

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
				if ((methodDef->mName == BF_METHODNAME_DEFAULT_EQUALS) ||
					(methodDef->mName == BF_METHODNAME_DEFAULT_STRICT_EQUALS))
					declRequired = false;
			}

			if (methodDef->mMethodType == BfMethodType_Init)
			{
				declRequired = false;
				implRequired = false;
			}

			if (!implRequired)
			{
				if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
				{
					if (!mIsScratchModule)
						mOnDemandMethodCount++;
				}

				if (!declRequired)
				{
					if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
						methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
					continue;
				}
				else
				{
					if (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
						methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
				}

				VerifyOnDemandMethods();
			}
			else
			{
				doAlwaysInclude = true;
			}
		}
		else
			doAlwaysInclude = true;

		if (doAlwaysInclude)
		{
			bool wasDeclared = (methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl) ||
				(methodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference);

			methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;

			if (wasDeclared)
			{
				if (!mIsScratchModule)
					mOnDemandMethodCount--;
				if (methodInstanceGroup->mDefault != NULL)
					AddMethodToWorkList(methodInstanceGroup->mDefault);
			}
		}
	}

	BF_ASSERT_REL(typeInstance->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted);
	BfLogSysM("Starting DoTypeInstanceMethodProcessing %p GetMethodInstance pass.  OnDemandMethods: %d\n", typeInstance, mOnDemandMethodCount);

	// Def passes. First non-overrides then overrides (for in-place overrides in methods)
	for (int pass = 0; pass < 2; pass++)
	{
		for (auto methodDef : typeDef->mMethods)
		{
			if ((pass == 1) != (methodDef->mIsOverride))
				continue;

			bool doGetMethodInstance = true;

			auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodDef->mIdx];

			if ((methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude) &&
				(methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Decl_AwaitingDecl))
			{
				BfLogSysM("Skipping GetMethodInstance on MethodDef: %p OnDemandKind: %d\n", methodDef, methodInstanceGroup->mOnDemandKind);
				doGetMethodInstance = false;
			}

			if (methodDef->mMethodType == BfMethodType_Init)
				doGetMethodInstance = false;

			BfMethodInstance* methodInstance = NULL;

			if (doGetMethodInstance)
			{
				int prevWorklistSize = (int)mContext->mMethodWorkList.size();

				auto flags = ((methodDef->mGenericParams.size() != 0) || (typeInstance->IsUnspecializedType())) ? BfGetMethodInstanceFlag_UnspecializedPass : BfGetMethodInstanceFlag_None;

				if (methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude)
					flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_MethodInstanceOnly);

				auto moduleMethodInstance = GetMethodInstance(typeInstance, methodDef, BfTypeVector(), flags);

				methodInstance = moduleMethodInstance.mMethodInstance;
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
								if (attrAttr.mType->ToTypeInstance()->IsInstanceOf(mCompiler->mAttributeUsageAttributeTypeDef))
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

									if (attrTypeInst->mAttributeData == NULL)
										PopulateType(attrTypeInst);
									BF_ASSERT(attrTypeInst->mAttributeData != NULL);
									if (attrTypeInst->mAttributeData != NULL)
									{
										if ((attrTypeInst->mAttributeData->mAlwaysIncludeUser & BfAlwaysIncludeFlag_IncludeAllMethods) != 0)
											forceMethodImpl = true;

										// "AssumeInstantiated" also forces default ctor
										if (((attrTypeInst->mAttributeData->mAlwaysIncludeUser & BfAlwaysIncludeFlag_AssumeInstantiated) != 0) &&
											(methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mParams.IsEmpty()))
											forceMethodImpl = true;
									}
								}
							}
						}
					}

					if (methodInstance->mMethodDef->mDeclaringType->mProject->mTargetType == BfTargetType_BeefTest)
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
			}
			else
			{
				methodInstance = methodInstanceGroup->mDefault;
				if (methodInstance == NULL)
					continue;
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
				mContext->CheckLockYield();
				disableYield.Acquire();
			}
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
							// Assert on base type?
							//AssertErrorState();
						}
						else
						{
 							auto matchedMethodDef = matchedMethod->mMethodDef;
							if (!matchedMethod->mIsForeignMethodDef)
							{
								BfMethodInstanceGroup* boxedMethodInstanceGroup = &typeInstance->mMethodInstanceGroups[matchedMethod->mMethodDef->mIdx];
								if (boxedMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference)
								{
									boxedMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
									VerifyOnDemandMethods();
								}
							}

							auto methodFlags = matchedMethod->mIsForeignMethodDef ? BfGetMethodInstanceFlag_ForeignMethodDef : BfGetMethodInstanceFlag_None;
							methodFlags = (BfGetMethodInstanceFlags)(methodFlags | BfGetMethodInstanceFlag_MethodInstanceOnly);

							auto moduleMethodInstance = GetMethodInstance(typeInstance, matchedMethodDef, BfTypeVector(),
								methodFlags,
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

	if ((!typeInstance->IsInterface()) && (!typeInstance->IsUnspecializedTypeVariation()) && (!isBoxed) && (!isFailedType))
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
						if (typeInstance->mVirtualMethodTable[methodIdx].mDeclaringMethod.mTypeInstance == typeInstance)
						{
							Fail("Method is abstract but it is declared in non-abstract class", methodInstance->mMethodDef->GetRefNode());
						}
						else if (!typeInstance->IsUnspecializedTypeVariation())
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

				auto iReturnType = ifaceMethodInst->mReturnType;
				if (iReturnType->IsUnspecializedTypeVariation())
				{
					BfType* resolvedType = ResolveGenericType(iReturnType, NULL, NULL, mCurTypeInstance);
					if (resolvedType != NULL)
						iReturnType = resolvedType;
					else
						iReturnType = typeInstance;
				}

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
				bool hadStaticFailure = false;
				bool hadMutFailure = false;

				if (hadMatch)
				{
					if ((matchedMethod->GetExplicitInterface() == NULL) && (matchedMethod->mMethodDef->mProtection != BfProtection_Public))
					{
						hadMatch = false;
						hadPubFailure = true;
					}

					if (matchedMethod->mMethodDef->mIsStatic != ifaceMethodInst->mMethodDef->mIsStatic)
					{
						hadMatch = false;
						hadStaticFailure = true;
					}

					if (ifaceMethodInst->mVirtualTableIdx != -1)
					{
						if (matchedMethod->mReturnType != iReturnType)
							hadMatch = false;
					}
					else
					{
						// Concrete/generic
						if (!CanCast(GetFakeTypedValue(matchedMethod->mReturnType), iReturnType))
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
										bool cmpResult = CompareMethodSignatures(checkIFaceMethodInst, ifaceMethodInst);
										if (cmpResult)
										{
											bool isBetter = TypeIsSubTypeOf(checkIFaceInst, bestInterface);
											bool isWorse = TypeIsSubTypeOf(bestInterface, checkIFaceInst);
											if (isBetter == isWorse)
											{
												CompareDeclTypes(NULL, checkIFaceMethodInst->mMethodDef->mDeclaringType, bestMethodInst->mMethodDef->mDeclaringType, isBetter, isWorse);
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
								auto error = Fail(StrFormat("Default interface method '%s' cannot be used because it doesn't have the return type '%s'",
									MethodToString(bestMethodInst).c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), declTypeDef->mTypeDeclaration->mNameNode);
								if (error != NULL)
								{
									mCompiler->mPassInstance->MoreInfo("See original method declaration", ifaceMethodInst->mMethodDef->GetRefNode());
									mCompiler->mPassInstance->MoreInfo("See override method declaration", bestMethodInst->mMethodDef->GetRefNode());
								}
							}
						}

						bool hasDefaultImpl = bestMethodInst->mMethodDef->HasBody() || bestMethodInst->mMethodDef->mIsAbstract;
						if ((hasDefaultImpl) && (matchedMethod == NULL))
						{
							auto methodDef = bestMethodInst->mMethodDef;
							BfGetMethodInstanceFlags flags = (BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_ForeignMethodDef | BfGetMethodInstanceFlag_MethodInstanceOnly);
							if ((methodDef->mGenericParams.size() != 0) || (typeInstance->IsUnspecializedType()))
								flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_UnspecializedPass);
							auto methodInst = GetMethodInstance(typeInstance, methodDef, BfTypeVector(), flags, ifaceInst);
							if (methodInst)
							{
								*matchedMethodRef = methodInst.mMethodInstance;

								BfMethodInstance* newMethodInstance = methodInst.mMethodInstance;
								BF_ASSERT(newMethodInstance->mIsForeignMethodDef);
								if (newMethodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference)
								{
									if (!mIsScratchModule)
										mOnDemandMethodCount++;
								}

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
						else if ((typeInstance->mRebuildFlags & BfTypeRebuildFlag_ConstEvalCancelled) != 0)
						{
							// It's possible const eval was supposed to generate this method. We're rebuilding the type anyway.
						}
						else
						{
							String methodString;
							///
							{
								BfTypeState typeState;
								typeState.mPrevState = mContext->mCurTypeState;
								typeState.mForceActiveTypeDef = declTypeDef;
								SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

								SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, ifaceMethodInst);
								methodString = MethodToString(ifaceMethodInst);
							}

							BfTypeDeclaration* typeDecl = declTypeDef->mTypeDeclaration;
							BfError* error = Fail(StrFormat("'%s' does not implement interface member '%s'", TypeToString(typeInstance).c_str(), methodString.c_str()), typeDecl->mNameNode, true);
							if ((matchedMethod != NULL) && (error != NULL))
							{
								if (hadStaticFailure)
								{
									auto staticNodeRef = matchedMethod->mMethodDef->GetRefNode();
									if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(matchedMethod->mMethodDef->mMethodDeclaration))
										if (methodDecl->mStaticSpecifier != NULL)
											staticNodeRef = methodDecl->mStaticSpecifier;

									if (matchedMethod->mMethodDef->mIsStatic)
										mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it's static",
											methodString.c_str()), staticNodeRef);
									else
										mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it's not static",
											methodString.c_str()), staticNodeRef);
								}
								else if (hadPubFailure)
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it's not public",
										methodString.c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
								}
								else if (ifaceMethodInst->mReturnType->IsConcreteInterfaceType())
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it does not have a concrete return type that implements '%s'",
										methodString.c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
								}
								else if (hadMutFailure)
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it's market as 'mut' but interface method does not allow it",
										methodString.c_str()), matchedMethod->mMethodDef->GetMutNode());
									mCompiler->mPassInstance->MoreInfo(StrFormat("Declare the interface method as 'mut' to allow matching 'mut' implementations"), ifaceMethodInst->mMethodDef->mMethodDeclaration);
								}
								else
								{
									mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' cannot match because it does not have the return type '%s'",
										methodString.c_str(), TypeToString(ifaceMethodInst->mReturnType).c_str()), matchedMethod->mMethodDef->mReturnTypeRef);
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

	VerifyOnDemandMethods();

	ambiguityContext.Finish();
	CheckAddFailType();

	typeInstance->mDefineState = BfTypeDefineState_DefinedAndMethodsSlotted;
	mCompiler->mStats.mTypesPopulated++;
	mCompiler->UpdateCompletion();

	BF_ASSERT_REL(!typeInstance->mNeedsMethodProcessing);

	BfLogSysM("Finished DoTypeInstanceMethodProcessing %p.  OnDemandMethods: %d  Virtual Size: %d InterfaceMethodTableSize: %d\n", typeInstance, mOnDemandMethodCount, typeInstance->mVirtualMethodTable.size(), typeInstance->mInterfaceMethodTable.size());
}

void BfModule::RebuildMethods(BfTypeInstance* typeInstance)
{
	if (typeInstance->IsIncomplete())
		return;

	BfLogSysM("RebuildMethods setting mNeedsMethodProcessing=true on %p\n", typeInstance);

	BF_ASSERT_REL(typeInstance->mDefineState != BfTypeDefineState_DefinedAndMethodsSlotting);
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

	if (methodInstance->IsOrInUnspecializedVariation())
	{
		return;
	}

	BF_ASSERT(!methodInstance->GetOwner()->IsUnspecializedTypeVariation());

	BF_ASSERT(methodInstance->mMethodProcessRequest == NULL);

	auto defaultMethod = methodInstance->mMethodInstanceGroup->mDefault;
	if (defaultMethod != methodInstance)
	{
		BF_ASSERT(defaultMethod != NULL);
		if (methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference)
		{
			if ((defaultMethod->mIsReified) && (!defaultMethod->mDeclModule->mIsModuleMutable))
			{
				defaultMethod->mDeclModule->PrepareForIRWriting(methodInstance->GetOwner());
			}

			AddMethodToWorkList(defaultMethod);
			// This should put all the specialized methods in the worklist, including us
			return;
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

		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mWantsIRIgnoreWrites);

		BfIRValue func = CreateFunctionFrom(methodInstance, false, methodInstance->mAlwaysInline);
		if (func)
		{
			methodInstance->mIRFunction = func;
			mFuncReferences[methodInstance] = func;
		}
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
				auto onDemandModule = owningModule;
				if (owningModule->mParentModule != NULL)
					onDemandModule = owningModule->mParentModule;

				owningModule->VerifyOnDemandMethods();

				if (methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
					owningModule->mOnDemandMethodCount++;
				BF_ASSERT(onDemandModule->mOnDemandMethodCount > 0);

				VerifyOnDemandMethods();
			}
			methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_InWorkList;

			if (methodInstance->mMethodInstanceGroup->mMethodSpecializationMap != NULL)
			{
				for (auto& kv : *methodInstance->mMethodInstanceGroup->mMethodSpecializationMap)
				{
					auto specMethodInstance = kv.mValue;
					if ((!specMethodInstance->mDeclModule->mIsModuleMutable) && (!specMethodInstance->mDeclModule->mReifyQueued))
					{
						specMethodInstance->mDeclModule->PrepareForIRWriting(specMethodInstance->GetOwner());
					}
					specMethodInstance->mDeclModule->AddMethodToWorkList(specMethodInstance);
				}
			}
		}
	}
	else
	{
		BF_ASSERT(defaultMethod->mMethodInstanceGroup->IsImplemented());
	}

	BF_ASSERT(methodInstance->mDeclModule != NULL);

	auto typeInstance = methodInstance->GetOwner();

	BfMethodProcessRequest* methodProcessRequest = mContext->mMethodWorkList.Alloc();
	if (mCompiler->mCompileState == BfCompiler::CompileState_Unreified)
	{
		if (methodInstance->mIsReified)
		{
			BfLogSysM("Marking method %d as unreified due to CompileState_Unreified\n", methodInstance);
			methodInstance->mIsReified = false;
		}
	}
		//BF_ASSERT(!methodInstance->mIsReified);
	methodProcessRequest->mType = typeInstance;
	methodProcessRequest->mMethodInstance = methodInstance;
	methodProcessRequest->mRevision = typeInstance->mRevision;
	methodProcessRequest->mFromModuleRebuildIdx = mRebuildIdx;
	methodProcessRequest->mFromModule = this;

	if ((!mCompiler->mIsResolveOnly) && (methodInstance->mIsReified))
	{
		if ((!mIsModuleMutable) && (!mIsScratchModule))
		{
			BF_ASSERT(!mGeneratesCode);
			StartNewRevision(BfModule::RebuildKind_None);
		}

		BF_ASSERT(mIsModuleMutable || mReifyQueued);
	}

	BF_ASSERT((mBfIRBuilder != NULL) || (!methodInstance->mIsReified));

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
	BF_ASSERT(!resolvedType->IsIntUnknown());

	auto arrayTypeDef = mCompiler->GetArrayTypeDef(dimensions);
	if (arrayTypeDef == NULL)
		return NULL;
	auto arrayType = mContext->mArrayTypePool.Get();
	delete arrayType->mGenericTypeInfo;
	arrayType->mGenericTypeInfo = new BfGenericTypeInfo();
	arrayType->mContext = mContext;
	arrayType->mTypeDef = arrayTypeDef;
	arrayType->mDimensions = dimensions;
	arrayType->mGenericTypeInfo->mTypeGenericArguments.clear();
	arrayType->mGenericTypeInfo->mTypeGenericArguments.push_back(resolvedType);
	auto resolvedArrayType = ResolveType(arrayType);
	if (resolvedArrayType != arrayType)
	{
		arrayType->Dispose();
		mContext->mArrayTypePool.GiveBack(arrayType);
	}
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
	BF_ASSERT(!resolvedType->IsVar());
	BF_ASSERT_REL(!resolvedType->IsDeleting());

	auto pointerType = mContext->mPointerTypePool.Get();
	pointerType->mContext = mContext;
	pointerType->mElementType = resolvedType;
	auto resolvedPointerType = (BfPointerType*)ResolveType(pointerType);
	if (resolvedPointerType != pointerType)
		mContext->mPointerTypePool.GiveBack(pointerType);

	BF_ASSERT(resolvedPointerType->mElementType == resolvedType);

	return resolvedPointerType;
}

BfConstExprValueType* BfModule::CreateConstExprValueType(const BfTypedValue& typedValue, bool allowCreate)
{
	BfPopulateType populateType = allowCreate ? BfPopulateType_Data : BfPopulateType_Identity;
	BfResolveTypeRefFlags resolveFlags = allowCreate ? BfResolveTypeRefFlag_None : BfResolveTypeRefFlag_NoCreate;

	auto variant = TypedValueToVariant(NULL, typedValue);

	if (variant.mTypeCode == BfTypeCode_None)
	{
		if (auto constant = mBfIRBuilder->GetConstant(typedValue.mValue))
		{
			if (constant->mConstType == BfConstType_Undef)
			{
				variant.mTypeCode = BfTypeCode_Let;
			}
		}
	}

	if (variant.mTypeCode == BfTypeCode_None)
		return NULL;

	auto constExprValueType = mContext->mConstExprValueTypePool.Get();
	constExprValueType->mContext = mContext;
	constExprValueType->mType = typedValue.mType;
	constExprValueType->mValue = variant;

	auto resolvedConstExprValueType = (BfConstExprValueType*)ResolveType(constExprValueType, populateType, resolveFlags);
 	if (resolvedConstExprValueType != constExprValueType)
 		mContext->mConstExprValueTypePool.GiveBack(constExprValueType);

	if (resolvedConstExprValueType != NULL)
		BF_ASSERT(resolvedConstExprValueType->mValue.mInt64 == constExprValueType->mValue.mInt64);

	return resolvedConstExprValueType;
}

BfConstExprValueType* BfModule::CreateConstExprValueType(const BfVariant& variant, BfType* type, bool allowCreate)
{
	BfPopulateType populateType = allowCreate ? BfPopulateType_Data : BfPopulateType_Identity;
	BfResolveTypeRefFlags resolveFlags = allowCreate ? BfResolveTypeRefFlag_None : BfResolveTypeRefFlag_NoCreate;

	if (variant.mTypeCode == BfTypeCode_None)
		return NULL;

	auto constExprValueType = mContext->mConstExprValueTypePool.Get();
	constExprValueType->mContext = mContext;
	constExprValueType->mType = type;
	constExprValueType->mValue = variant;

	auto resolvedConstExprValueType = (BfConstExprValueType*)ResolveType(constExprValueType, populateType, resolveFlags);
	if (resolvedConstExprValueType != constExprValueType)
		mContext->mConstExprValueTypePool.GiveBack(constExprValueType);

	if (resolvedConstExprValueType != NULL)
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
			if (type->IsUnknownSizedArrayType())
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
		case BfTypeCode_Float:
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
			BFMODULE_FATAL(this, "Invalid use of StringId");
			break;
		default:
			BF_DBG_FATAL("Invalid type");
			break;
		}
		mContext->mPrimitiveTypes[typeCode] = primType;
	}
	return primType;
}

BfIRType BfModule::GetIRLoweredType(BfTypeCode loweredTypeCode, BfTypeCode loweredTypeCode2)
{
	BF_ASSERT(!mIsComptimeModule);

	BF_ASSERT(loweredTypeCode != BfTypeCode_None);
	if (loweredTypeCode2 == BfTypeCode_None)
		return mBfIRBuilder->GetPrimitiveType(loweredTypeCode);

	SizedArray<BfIRType, 2> types;
	types.push_back(mBfIRBuilder->GetPrimitiveType(loweredTypeCode));
	types.push_back(mBfIRBuilder->GetPrimitiveType(loweredTypeCode2));
	return mBfIRBuilder->CreateStructType(types);
}

BfMethodRefType* BfModule::CreateMethodRefType(BfMethodInstance* methodInstance, bool mustAlreadyExist)
{
	// Make sure we don't have a partially-formed local method or lambda coming in, because those may be replaced
	//  after the capture phase
	BF_ASSERT(!methodInstance->mDisallowCalling);

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
	BfResolvedTypeSet::EntryRef typeEntry;
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

void BfModule::FixIntUnknown(BfTypedValue& typedVal, BfType* matchType)
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

	if ((matchType != NULL) && (matchType->IsPrimitiveType()) && (mBfIRBuilder->IsInt(matchType->ToPrimitiveType()->mTypeDef->mTypeCode)))
	{
		auto wantTypeCode = matchType->ToPrimitiveType()->mTypeDef->mTypeCode;
		if (matchType->mSize < 8)
		{
			int64 minVal = -(1LL << (8 * matchType->mSize - 1));
			int64 maxVal = (1LL << (8 * matchType->mSize - 1)) - 1;
			if ((constant->mInt64 >= minVal) && (constant->mInt64 <= maxVal))
			{
				typedVal.mValue = mBfIRBuilder->CreateNumericCast(typedVal.mValue, mBfIRBuilder->IsSigned(wantTypeCode), wantTypeCode);
				typedVal.mType = GetPrimitiveType(wantTypeCode);
				return;
			}
		}
	}

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
	if ((lhs.mType != NULL) && (lhs.mType->IsIntUnknown()) && (rhs.mType != NULL) &&
		(rhs.mType->IsInteger()) && (!rhs.mType->IsIntUnknown()))
	{
		if (CanCast(lhs, rhs.mType))
		{
			lhs = Cast(NULL, lhs, rhs.mType, BfCastFlags_SilentFail);
			if (!lhs)
				lhs = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
			return;
		}
	}

	if ((rhs.mType != NULL) && (rhs.mType->IsIntUnknown()) && (lhs.mType != NULL) &&
		(lhs.mType->IsInteger()) && (!lhs.mType->IsIntUnknown()))
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

void BfModule::FixValueActualization(BfTypedValue& typedVal, bool force)
{
	if (!typedVal.mValue.IsConst())
		return;
	if ((mBfIRBuilder->mIgnoreWrites) && (!force))
		return;
	auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
	if (!HasUnactializedConstant(constant, mBfIRBuilder))
		return;
	typedVal.mValue = ConstantToCurrent(constant, mBfIRBuilder, typedVal.mType, false);
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
	case BfTypeCode_Float:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Float"), BfPopulateType_Identity)->ToTypeInstance(); break;
	case BfTypeCode_Double:
		typeInst = ResolveTypeDef(mSystem->FindTypeDef("System.Double"), BfPopulateType_Identity)->ToTypeInstance(); break;
	default:
		//BF_FATAL("not implemented");
		break;
	}
	return typeInst;
}

BfBoxedType* BfModule::CreateBoxedType(BfType* resolvedTypeRef, bool allowCreate)
{
	bool isStructPtr = false;
	BfPopulateType populateType = allowCreate ? BfPopulateType_Data : BfPopulateType_Identity;
	BfResolveTypeRefFlags resolveFlags = allowCreate ? BfResolveTypeRefFlag_None : BfResolveTypeRefFlag_NoCreate;

	if (resolvedTypeRef->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)resolvedTypeRef;
		resolvedTypeRef = GetPrimitiveStructType(primType->mTypeDef->mTypeCode);
		if (resolvedTypeRef == NULL)
			return NULL;
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
			resolvedTypeRef = ResolveTypeDef(mCompiler->mPointerTTypeDef, typeVector, populateType, resolveFlags);
			if (resolvedTypeRef == NULL)
				return NULL;
		}
	}
	else if (resolvedTypeRef->IsMethodRef())
	{
		BfMethodRefType* methodRefType = (BfMethodRefType*)resolvedTypeRef;
		BfTypeVector typeVector;
		typeVector.Add(methodRefType);
		resolvedTypeRef = ResolveTypeDef(mCompiler->mMethodRefTypeDef, typeVector, populateType, resolveFlags);
		if (resolvedTypeRef == NULL)
			return NULL;
	}
	else if (resolvedTypeRef->IsSizedArray())
	{
	 	BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)resolvedTypeRef;
	 	BfTypeVector typeVector;
	 	typeVector.Add(sizedArrayType->mElementType);
	 	auto sizeValue = BfTypedValue(GetConstValue(sizedArrayType->mElementCount), GetPrimitiveType(BfTypeCode_IntPtr));
		auto sizeValueType = CreateConstExprValueType(sizeValue, allowCreate);
		if (sizeValueType == NULL)
			return NULL;
	 	typeVector.Add(sizeValueType);
		resolvedTypeRef = ResolveTypeDef(mCompiler->mSizedArrayTypeDef, typeVector, populateType, resolveFlags);
		if (resolvedTypeRef == NULL)
			return NULL;
	}

	BfTypeInstance* typeInst = resolvedTypeRef->ToTypeInstance();
	if ((typeInst == NULL) && (!resolvedTypeRef->IsGenericParam()))
		return NULL;

	auto boxedType = mContext->mBoxedTypePool.Get();
	boxedType->mContext = mContext;
	boxedType->mElementType = resolvedTypeRef;
	if (typeInst != NULL)
		boxedType->mTypeDef = typeInst->mTypeDef->GetDefinition();
	else
		boxedType->mTypeDef = mCompiler->mValueTypeTypeDef;
	boxedType->mBoxedFlags = isStructPtr ? BfBoxedType::BoxedFlags_StructPtr : BfBoxedType::BoxedFlags_None;
	auto resolvedBoxedType = ResolveType(boxedType, populateType, resolveFlags);
	if (resolvedBoxedType != boxedType)
	{
		boxedType->Dispose();
		mContext->mBoxedTypePool.GiveBack(boxedType);
	}
	return (BfBoxedType*)resolvedBoxedType;
}

BfTypeInstance* BfModule::CreateTupleType(const BfTypeVector& fieldTypes, const Array<String>& fieldNames, bool allowVar)
{
	auto baseType = (BfTypeInstance*)ResolveTypeDef(mContext->mCompiler->mValueTypeTypeDef);

	BfTupleType* tupleType = NULL;

	auto actualTupleType = mContext->mTupleTypePool.Get();
	actualTupleType->Init(baseType->mTypeDef->mProject, baseType);

	bool isUnspecialzied = false;
	for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
	{
		String fieldName;
		if (fieldIdx < (int)fieldNames.size())
			fieldName = fieldNames[fieldIdx];
		if (fieldName.empty())
			fieldName = StrFormat("%d", fieldIdx);
		BfFieldDef* fieldDef = actualTupleType->AddField(fieldName);

		auto fieldType = fieldTypes[fieldIdx];
		if ((fieldType->IsUnspecializedType()) || (fieldType->IsVar()))
			isUnspecialzied = true;
	}
	tupleType = actualTupleType;

	tupleType->mContext = mContext;
	tupleType->mFieldInstances.Resize(fieldTypes.size());
	for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
	{
		BfFieldInstance* fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
		fieldInstance->mFieldIdx = fieldIdx;
		BfType* fieldType = fieldTypes[fieldIdx];
		if ((fieldType->IsVar()) && (!allowVar))
			fieldType = mContext->mBfObjectType;
		fieldInstance->SetResolvedType(fieldType);
		fieldInstance->mOwner = tupleType;
	}

	tupleType->mIsUnspecializedType = false;
	tupleType->mIsUnspecializedTypeVariation = false;
	if (isUnspecialzied)
	{
		tupleType->mIsUnspecializedType = true;
		tupleType->mIsUnspecializedTypeVariation = true;
	}

	auto resolvedTupleType = ResolveType(tupleType);
	if (resolvedTupleType != tupleType)
	{
		BF_ASSERT(tupleType->mContext != NULL);
		tupleType->Dispose();
		mContext->mTupleTypePool.GiveBack((BfTupleType*)tupleType);
	}

	return (BfTupleType*)resolvedTupleType;
}

BfTypeInstance* BfModule::SantizeTupleType(BfTypeInstance* tupleType)
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

BfModifiedTypeType* BfModule::CreateModifiedTypeType(BfType* resolvedTypeRef, BfToken modifiedKind)
{
	auto retTypeType = mContext->mModifiedTypeTypePool.Get();
	retTypeType->mContext = mContext;
	retTypeType->mModifiedKind = modifiedKind;
	retTypeType->mElementType = resolvedTypeRef;
	auto resolvedRetTypeType = ResolveType(retTypeType);
	if (resolvedRetTypeType != retTypeType)
		mContext->mModifiedTypeTypePool.GiveBack(retTypeType);
	return (BfModifiedTypeType*)resolvedRetTypeType;
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

BfType* BfModule::ResolveTypeDef(BfTypeDef* typeDef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Emitted);

	if (typeDef->mTypeDeclaration == NULL)
	{
		BF_ASSERT(!typeDef->mIsDelegate && !typeDef->mIsFunction);
	}

	//BF_ASSERT(typeDef->mTypeCode != BfTypeCode_Extension);
	BF_ASSERT(!typeDef->mIsPartial || typeDef->mIsCombinedPartial);

	BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Deleted);
	BF_ASSERT((typeDef->mOuterType == NULL) || (typeDef->mOuterType->mDefState != BfTypeDef::DefState_Deleted));

	if (typeDef->mGenericParamDefs.size() != 0)
		return ResolveTypeDef(typeDef, BfTypeVector(), populateType, resolveFlags);

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

// Get BaseClass even when we haven't populated the type yet
BfTypeInstance* BfModule::GetBaseType(BfTypeInstance* typeInst)
{
	if (typeInst->mBaseType == NULL)
	{
		auto checkTypeState = mContext->mCurTypeState;
		while (checkTypeState != NULL)
		{
			if (checkTypeState->mType == typeInst)
				return NULL;
			checkTypeState = checkTypeState->mPrevState;
		}
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

BfType* BfModule::ResolveInnerType(BfType* outerType, BfAstNode* typeRef, BfPopulateType populateType, bool ignoreErrors, int numGenericArgs, BfResolveTypeRefFlags resolveFlags)
{
	BfTypeDef* nestedTypeDef = NULL;

	if (outerType->IsBoxed())
		outerType = outerType->GetUnderlyingType();

	BfNamedTypeReference* namedTypeRef = NULL;
	BfGenericInstanceTypeRef* genericTypeRef = NULL;
	BfDirectStrTypeReference* directStrTypeRef = NULL;
	BfIdentifierNode* identifierNode = NULL;
	if ((namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef)))
	{
		//TYPEDEF nestedTypeDef = namedTypeRef->mTypeDef;
	}
	else if ((genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef)))
	{
		namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(genericTypeRef->mElementType);
		//TYPEDEF nestedTypeDef = namedTypeRef->mTypeDef;
	}
	else if ((identifierNode = BfNodeDynCast<BfIdentifierNode>(typeRef)))
	{
		//TYPEDEF nestedTypeDef = namedTypeRef->mTypeDef;
	}
	else if ((directStrTypeRef = BfNodeDynCast<BfDirectStrTypeReference>(typeRef)))
	{
		//
	}

	BF_ASSERT((identifierNode != NULL) || (namedTypeRef != NULL) || (directStrTypeRef != NULL));

	auto usedOuterType = outerType;
	if (nestedTypeDef == NULL)
	{
		StringView findName;
		if (namedTypeRef != NULL)
			findName = namedTypeRef->mNameNode->ToStringView();
		else if (identifierNode != NULL)
			findName = identifierNode->ToStringView();
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
						((mCurTypeInstance == outerTypeInstance) || TypeHasParentOrEquals(mCurTypeInstance->mTypeDef, outerTypeInstance->mTypeDef));
					bool allowProtected = allowPrivate;/*(mCurTypeInstance != NULL) &&
													   (allowPrivate || (mCurTypeInstance->mSkipTypeProtectionChecks) || TypeIsSubTypeOf(mCurTypeInstance, outerTypeInstance));*/

					auto checkOuterType = outerTypeInstance;
					while (checkOuterType != NULL)
					{
						for (auto checkType : checkOuterType->mTypeDef->mNestedTypes)
						{
							auto latestCheckType = checkType->GetLatest();

							if ((!isFailurePass) && ((resolveFlags & BfResolveTypeRefFlag_IgnoreProtection) == 0) &&
								(!CheckProtection(latestCheckType->mProtection, latestCheckType, allowProtected, allowPrivate)))
								continue;

							if (checkType->mProject != checkOuterType->mTypeDef->mProject)
							{
								auto visibleProjectSet = GetVisibleProjectSet();
								if ((visibleProjectSet == NULL) || (!visibleProjectSet->Contains(checkType->mProject)))
									continue;
							}

							if ((checkType->mName->mString == findName) && (checkType->GetSelfGenericParamCount() == numGenericArgs))
							{
								if (isFailurePass)
								{
									// This is the one error we don't ignore when ignoreErrors is set
									Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(checkOuterType).c_str(), BfTypeUtils::TypeToString(typeRef).c_str()), typeRef); // CS0122
								}

								usedOuterType = checkOuterType;
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

					if ((outerTypeInstance->IsEnum()) && (findName == "UnderlyingType"))
					{
						if (outerTypeInstance->IsDataIncomplete())
							PopulateType(outerTypeInstance);
						auto underlyingType = outerTypeInstance->GetUnderlyingType();
						if (underlyingType != NULL)
							return underlyingType;
					}
				}
			}
		}

		if (nestedTypeDef == NULL)
		{
			if ((!mIgnoreErrors) && (!ignoreErrors) && ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0))
			{
				StringT<64> name;
				name.Append(findName);
				Fail(StrFormat("'%s' does not contain a definition for '%s'", TypeToString(outerType).c_str(), name.c_str()), typeRef);
			}
			return NULL;
		}
	}

	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, ignoreErrors || mIgnoreErrors);
	if ((genericTypeRef != NULL) || (usedOuterType->IsGenericTypeInstance()))
	{
		BfTypeVector genericArgs;
		if (usedOuterType->IsGenericTypeInstance())
		{
			auto genericTypeInst = (BfTypeInstance*)usedOuterType;
			genericArgs = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments;
		}

		if (genericTypeRef != NULL)
		{
			for (auto genericArgTypeRef : genericTypeRef->mGenericArguments)
			{
				auto genericArgType = ResolveTypeRef(genericArgTypeRef, NULL, BfPopulateType_IdentityNoRemapAlias);
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
	auto result = mSystem->FindTypeDef(typeDef->mFullName, (int)typeDef->mGenericParamDefs.size(), NULL, {}, NULL, BfFindTypeDefFlag_None);
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
		auto genericType = (BfTypeInstance*)type;
		typeGenericArguments = genericType->mGenericTypeInfo->mTypeGenericArguments;
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
		if (outerType->GetDefinition() == checkOuterType->GetDefinition())
			return true;
		checkInnerType = checkInnerType->mOuterType;
	}
}

BfType* BfModule::ResolveTypeDef(BfTypeDef* typeDef, const BfTypeVector& genericArgs, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Emitted);

	if (typeDef->mGenericParamDefs.size() == 0)
		return ResolveTypeDef(typeDef, populateType, resolveFlags);

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
		delete arrayInstType->mGenericTypeInfo;
		arrayInstType->mGenericTypeInfo = new BfGenericTypeInfo();
		arrayInstType->mTypeDef = typeDef;
		arrayInstType->mGenericTypeInfo->mIsUnspecialized = false;
		arrayInstType->mGenericTypeInfo->mTypeGenericArguments.clear();
		for (auto genericArg : genericArgs)
		{
			arrayInstType->mGenericTypeInfo->mIsUnspecialized |= genericArg->IsGenericParam();
			arrayInstType->mGenericTypeInfo->mTypeGenericArguments.push_back(genericArg);
		}

		if (genericArgs.size() == 0)
		{
			for (int i = 0; i < (int)typeDef->mGenericParamDefs.size(); i++)
			{
				auto genericParamTypeRef = GetGenericParamType(BfGenericParamKind_Type, i);
				arrayInstType->mGenericTypeInfo->mTypeGenericArguments.push_back(genericParamTypeRef);
				arrayInstType->mGenericTypeInfo->mIsUnspecialized = true;
			}
		}

		auto resolvedType = ResolveType(arrayInstType, populateType, resolveFlags);
		if (resolvedType != arrayInstType)
		{
			delete arrayInstType->mGenericTypeInfo;
			arrayInstType->mGenericTypeInfo = NULL;
			arrayInstType->Dispose();
			mContext->mArrayTypeInstancePool.GiveBack(arrayInstType);
			mContext->mTypeDefTypeRefPool.GiveBack(typeRef);
		}
		BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
		return resolvedType;
	}

	BfTypeInstance* genericInstType;
	if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
		genericInstType = mContext->mAliasTypePool.Get();
	else
		genericInstType = mContext->mGenericTypeInstancePool.Get();
	delete genericInstType->mGenericTypeInfo;
	genericInstType->mGenericTypeInfo = new BfGenericTypeInfo();

	BF_ASSERT(genericInstType->mGenericTypeInfo->mGenericParams.size() == 0);
	BF_ASSERT((genericInstType->mRebuildFlags & BfTypeRebuildFlag_AddedToWorkList) == 0);
	genericInstType->mRebuildFlags = (BfTypeRebuildFlags)(genericInstType->mRebuildFlags & ~BfTypeRebuildFlag_InTempPool);
	genericInstType->mContext = mContext;
	auto typeRef = mContext->mTypeDefTypeRefPool.Get();
	typeRef->mTypeDef = typeDef;
	genericInstType->mTypeDef = typeDef;
	genericInstType->mGenericTypeInfo->mIsUnspecialized = false;
	genericInstType->mGenericTypeInfo->mTypeGenericArguments.clear();
	genericInstType->mTypeFailed = false;
	for (auto genericArg : genericArgs)
	{
		genericInstType->mGenericTypeInfo->mIsUnspecialized |= genericArg->IsGenericParam();
		genericInstType->mGenericTypeInfo->mTypeGenericArguments.push_back(genericArg);
	}

	if (genericArgs.size() == 0)
	{
		for (int i = 0; i < (int)typeDef->mGenericParamDefs.size(); i++)
		{
			auto genericParamTypeRef = GetGenericParamType(BfGenericParamKind_Type, i);
			genericInstType->mGenericTypeInfo->mTypeGenericArguments.push_back(genericParamTypeRef);
			genericInstType->mGenericTypeInfo->mIsUnspecialized = true;
		}
	}

	BfType* resolvedType = NULL;

	bool failed = false;
	resolvedType = ResolveType(genericInstType, populateType, resolveFlags);
	if (resolvedType != genericInstType)
	{
		BF_ASSERT(genericInstType->mGenericTypeInfo->mGenericParams.size() == 0);
		BF_ASSERT((genericInstType->mRebuildFlags & BfTypeRebuildFlag_AddedToWorkList) == 0);
		genericInstType->mRebuildFlags = (BfTypeRebuildFlags)(genericInstType->mRebuildFlags | BfTypeRebuildFlag_InTempPool);
		delete genericInstType->mGenericTypeInfo;
		genericInstType->mGenericTypeInfo = NULL;
		if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
			mContext->mAliasTypePool.GiveBack((BfTypeAliasType*)genericInstType);
		else
		{
			genericInstType->Dispose();
			mContext->mGenericTypeInstancePool.GiveBack(genericInstType);
		}
		mContext->mTypeDefTypeRefPool.GiveBack(typeRef);
	}
	BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
	return resolvedType;
}

int checkIdx = 0;

BfTypeDef* BfModule::ResolveGenericInstanceDef(BfGenericInstanceTypeRef* genericTypeRef, BfType** outType, BfResolveTypeRefFlags resolveFlags)
{
	if (outType != NULL)
		*outType = NULL;

	BfTypeReference* typeRef = genericTypeRef->mElementType;
	int numGenericParams = genericTypeRef->GetGenericArgCount();

	BfTypeDef* curTypeDef = NULL;
	if (mCurTypeInstance != NULL)
		curTypeDef = mCurTypeInstance->mTypeDef->GetDefinition();

	if (auto directTypeDef = BfNodeDynCast<BfDirectTypeReference>(typeRef))
	{
		auto typeInst = directTypeDef->mType->ToTypeInstance();
		return typeInst->mTypeDef->GetDefinition();
	}

	auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef);
	auto directStrTypeDef = BfNodeDynCastExact<BfDirectStrTypeReference>(typeRef);
	if ((namedTypeRef != NULL) || (directStrTypeDef != NULL))
	{
		BfTypeLookupError error;
		error.mRefNode = typeRef;
		BfTypeDef* typeDef = FindTypeDef(typeRef, NULL, &error, numGenericParams, resolveFlags);
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
				if ((resolveFlags & BfResolveTypeRefFlag_Attribute) != 0)
					findName += "Attribute";
				if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsGenericTypeInstance()))
				{
					auto genericTypeInst = (BfTypeInstance*)mCurTypeInstance;
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

			if ((wasGenericParam) && ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0))
				Fail("Cannot use generic param as generic instance type", typeRef);
		}

		if (typeDef == NULL)
		{
			if ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0)
				TypeRefNotFound(typeRef);
			return NULL;
		}
	}

	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
	{
		BfAutoParentNodeEntry autoParentNodeEntry(this, genericTypeRef);
		auto type = ResolveTypeRef(qualifiedTypeRef, BfPopulateType_TypeDef, resolveFlags, numGenericParams);
		if (type == NULL)
			return NULL;
		if (outType != NULL)
			*outType = type;
		auto typeInst = type->ToTypeInstance();
		if (typeInst != NULL)
			return typeInst->mTypeDef->GetDefinition();
	}

	if ((resolveFlags & BfResolveTypeRefFlag_IgnoreLookupError) == 0)
		Fail("Invalid generic type", typeRef);
	return NULL;
}

BfType* BfModule::ResolveGenericType(BfType* unspecializedType, BfTypeVector* typeGenericArguments, BfTypeVector* methodGenericArguments, BfType* selfType, bool allowFail)
{
	if (unspecializedType->IsGenericParam())
	{
		auto genericParam = (BfGenericParamType*)unspecializedType;
		if ((genericParam->mGenericParamKind == BfGenericParamKind_Type) && (typeGenericArguments != NULL))
		{
			if (genericParam->mGenericParamIdx < (int)typeGenericArguments->size())
				return FixIntUnknown((*typeGenericArguments)[genericParam->mGenericParamIdx]);
			BF_ASSERT(allowFail);
		}
		if ((genericParam->mGenericParamKind == BfGenericParamKind_Method) && (methodGenericArguments != NULL))
		{
			if (genericParam->mGenericParamIdx < (int)methodGenericArguments->size())
			{
				auto resolvedType = FixIntUnknown((*methodGenericArguments)[genericParam->mGenericParamIdx]);
				if ((resolvedType != NULL) && (resolvedType->IsGenericParam()))
				{
					auto genericParamType = (BfGenericParamType*)resolvedType;
					//BF_ASSERT(genericParamType->mGenericParamKind != BfGenericParamKind_Method);
				}
				return resolvedType;
			}
			BF_ASSERT(allowFail);
		}
		return unspecializedType;
	}

	if ((unspecializedType->IsSelf()) && (selfType != NULL))
		return selfType;

	if (!unspecializedType->IsUnspecializedType())
	{
		return unspecializedType;
	}

	if (unspecializedType->IsUnknownSizedArrayType())
	{
		auto* arrayType = (BfUnknownSizedArrayType*)unspecializedType;
		auto elementType = ResolveGenericType(arrayType->mElementType, typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		if (elementType->IsVar())
			return elementType;
		auto sizeType = ResolveGenericType(arrayType->mElementCountSource, typeGenericArguments, methodGenericArguments, selfType, allowFail);
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
		auto elementType = ResolveGenericType(arrayType->mElementType, typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		if (elementType->IsVar())
			return elementType;
		elementType = FixIntUnknown(elementType);
		return CreateSizedArrayType(elementType, (int)arrayType->mElementCount);
	}

	if (unspecializedType->IsRef())
	{
		auto refType = (BfRefType*)unspecializedType;
		auto elementType = ResolveGenericType(refType->GetUnderlyingType(), typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		if (elementType->IsVar())
			return elementType;
		elementType = FixIntUnknown(elementType);
		return CreateRefType(elementType, refType->mRefKind);
	}

	if (unspecializedType->IsPointer())
	{
		auto ptrType = (BfPointerType*)unspecializedType;
		auto elementType = ResolveGenericType(ptrType->GetUnderlyingType(), typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		if (elementType->IsVar())
			return elementType;
		elementType = FixIntUnknown(elementType);
		return CreatePointerType(elementType);
	}

	if (unspecializedType->IsConcreteInterfaceType())
	{
		auto concreteType = (BfConcreteInterfaceType*)unspecializedType;
		auto elementType = ResolveGenericType(concreteType->GetUnderlyingType(), typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		auto elementTypeInstance = elementType->ToTypeInstance();
		if (elementTypeInstance == NULL)
			return unspecializedType;
		return CreateConcreteInterfaceType(elementTypeInstance);
	}

	if (unspecializedType->IsArray())
	{
		auto arrayType = (BfArrayType*)unspecializedType;
		auto elementType = ResolveGenericType(arrayType->GetUnderlyingType(), typeGenericArguments, methodGenericArguments, selfType, allowFail);
		if (elementType == NULL)
			return NULL;
		if (elementType->IsVar())
			return elementType;
		elementType = FixIntUnknown(elementType);
		return CreateArrayType(elementType, arrayType->mDimensions);
	}

	if (unspecializedType->IsTuple())
	{
		bool wantGeneric = false;
		bool isUnspecialized = false;

		auto unspecializedTupleType = (BfTypeInstance*)unspecializedType;
		auto unspecializedGenericTupleType = unspecializedTupleType->ToGenericTypeInstance();
		Array<String> fieldNames;
		BfTypeVector fieldTypes;
		bool hadChange = false;

		for (auto& fieldInstance : unspecializedTupleType->mFieldInstances)
		{
			fieldNames.push_back(fieldInstance.GetFieldDef()->mName);
			auto origGenericArg = fieldInstance.mResolvedType;
			auto newGenericArg = ResolveGenericType(origGenericArg, typeGenericArguments, methodGenericArguments, selfType, allowFail);
			if (newGenericArg == NULL)
				return NULL;
			if (newGenericArg->IsVar())
				return newGenericArg;

			if (newGenericArg->IsTypeGenericParam())
				wantGeneric = true;
			if (newGenericArg->IsUnspecializedType())
				isUnspecialized = true;
			if (newGenericArg->IsVar())
				wantGeneric = mContext->mBfObjectType;
				//wantGeneric = true;

			if (newGenericArg != origGenericArg)
				hadChange = true;
			fieldTypes.push_back(newGenericArg);
		}

		if (!hadChange)
			return unspecializedType;

		if (unspecializedGenericTupleType == NULL)
			wantGeneric = false;

		//TODO:
		wantGeneric = false;

		auto baseType = (BfTypeInstance*)ResolveTypeDef(mContext->mCompiler->mValueTypeTypeDef);

		BfTupleType* tupleType = NULL;
		if (wantGeneric)
		{
 			Array<BfType*> genericArgs;
			for (int genericArgIdx = 0; genericArgIdx < (int)unspecializedGenericTupleType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
			{
				BfType* resolvedArg = unspecializedGenericTupleType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx];
				if (resolvedArg->IsUnspecializedType())
				{
					resolvedArg = ResolveGenericType(resolvedArg, typeGenericArguments, methodGenericArguments, selfType, allowFail);
					if (resolvedArg == NULL)
						return NULL;
					if (resolvedArg->IsVar())
						return resolvedArg;
				}
				genericArgs.push_back(resolvedArg);
			}

			auto actualTupleType = mContext->mTupleTypePool.Get();
			delete actualTupleType->mGenericTypeInfo;
			actualTupleType->mGenericDepth = 0;
			actualTupleType->mGenericTypeInfo = new BfGenericTypeInfo();
			actualTupleType->mGenericTypeInfo->mIsUnspecialized = false;
			actualTupleType->mGenericTypeInfo->mIsUnspecializedVariation = false;
			actualTupleType->mGenericTypeInfo->mTypeGenericArguments = genericArgs;
			for (int genericArgIdx = 0; genericArgIdx < (int)unspecializedGenericTupleType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
			{
				auto typeGenericArg = genericArgs[genericArgIdx];
				if ((typeGenericArg->IsGenericParam()) || (typeGenericArg->IsUnspecializedType()))
					actualTupleType->mGenericTypeInfo->mIsUnspecialized = true;
				actualTupleType->mGenericTypeInfo->mGenericParams.push_back(unspecializedGenericTupleType->mGenericTypeInfo->mGenericParams[genericArgIdx]->AddRef());
			}
			CheckUnspecializedGenericType(actualTupleType, BfPopulateType_Identity);
			if (isUnspecialized)
			{
				actualTupleType->mGenericTypeInfo->mIsUnspecialized = true;
				actualTupleType->mGenericTypeInfo->mIsUnspecializedVariation = true;
			}

			actualTupleType->mIsUnspecializedType = actualTupleType->mGenericTypeInfo->mIsUnspecialized;
			actualTupleType->mIsUnspecializedTypeVariation = actualTupleType->mGenericTypeInfo->mIsUnspecializedVariation;
			actualTupleType->Init(baseType->mTypeDef->mProject, baseType);

			for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
			{
				String fieldName = fieldNames[fieldIdx];
				BfFieldDef* fieldDef = actualTupleType->AddField(fieldName);
			}

			tupleType = actualTupleType;
		}
		else
		{
			auto actualTupleType = new BfTupleType();
			actualTupleType->mIsUnspecializedType = isUnspecialized;
			actualTupleType->mIsUnspecializedTypeVariation = isUnspecialized;

			actualTupleType->Init(baseType->mTypeDef->mProject, baseType);

			for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
			{
				String fieldName = fieldNames[fieldIdx];
				BfFieldDef* fieldDef = actualTupleType->AddField(fieldName);
			}

			tupleType = actualTupleType;
		}

		tupleType->mContext = mContext;
		tupleType->mFieldInstances.Resize(fieldTypes.size());
		for (int fieldIdx = 0; fieldIdx < (int)fieldTypes.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[fieldIdx];
			fieldInstance->mFieldIdx = fieldIdx;
			fieldInstance->SetResolvedType(fieldTypes[fieldIdx]);
			fieldInstance->mOwner = tupleType;
			tupleType->mGenericDepth = BF_MAX(tupleType->mGenericDepth, fieldInstance->mResolvedType->GetGenericDepth() + 1);
		}

		bool failed = false;
		BfType* resolvedType = NULL;
		if (!failed)
			resolvedType = ResolveType(tupleType, BfPopulateType_Identity);

		if (resolvedType != tupleType)
		{
			delete tupleType->mGenericTypeInfo;
			tupleType->mGenericTypeInfo = NULL;
			tupleType->Dispose();
			mContext->mTupleTypePool.GiveBack((BfTupleType*)tupleType);
		}
		BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
		return resolvedType;
	}

	if ((unspecializedType->IsDelegateFromTypeRef()) || (unspecializedType->IsFunctionFromTypeRef()))
	{
		BfTypeInstance* unspecializedDelegateType = (BfTypeInstance*)unspecializedType;
		BfTypeInstance* unspecializedGenericDelegateType = unspecializedType->ToGenericTypeInstance();
		BfDelegateInfo* unspecializedDelegateInfo = unspecializedType->GetDelegateInfo();

		bool wantGeneric = false;
		bool isUnspecialized = false;
		auto _CheckType = [&](BfType* type)
		{
			if (type->IsTypeGenericParam())
				wantGeneric = true;
			if (type->IsUnspecializedType())
				isUnspecialized = true;
		};

		bool failed = false;

		bool hasTypeGenerics = false;
		auto returnType = ResolveGenericType(unspecializedDelegateInfo->mReturnType, typeGenericArguments, methodGenericArguments, selfType, allowFail);

		if (returnType == NULL)
			return NULL;
		if (returnType->IsVar())
			return returnType;
		_CheckType(returnType);
		if (returnType->IsGenericParam())
			hasTypeGenerics |= ((BfGenericParamType*)returnType)->mGenericParamKind == BfGenericParamKind_Type;

		Array<BfType*> paramTypes;
		for (auto param : unspecializedDelegateInfo->mParams)
		{
			auto paramType = ResolveGenericType(param, typeGenericArguments, methodGenericArguments, selfType, allowFail);
			if (paramType == NULL)
				return NULL;
			if (paramType->IsVar())
				return paramType;
			paramTypes.Add(paramType);
			_CheckType(paramType);
		}

		if (unspecializedGenericDelegateType == NULL)
			wantGeneric = false;

		//TODO:
		wantGeneric = false;

		BfTypeInstance* delegateType = NULL;
		auto baseDelegateType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();

		if (wantGeneric)
		{
			Array<BfType*> genericArgs;
			for (int genericArgIdx = 0; genericArgIdx < (int)unspecializedGenericDelegateType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
			{
				BfType* resolvedArg = unspecializedGenericDelegateType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx];
				if (resolvedArg->IsUnspecializedType())
				{
					resolvedArg = ResolveGenericType(resolvedArg, typeGenericArguments, methodGenericArguments, selfType, allowFail);
					if (resolvedArg == NULL)
						return NULL;
					if (resolvedArg->IsVar())
						return resolvedArg;
				}
				genericArgs.push_back(resolvedArg);
			}

			auto dlgType = mContext->mDelegateTypePool.Get();
			delete dlgType->mGenericTypeInfo;
			dlgType->mGenericTypeInfo = new BfGenericTypeInfo();
			dlgType->mGenericTypeInfo->mFinishedGenericParams = true;
			dlgType->mGenericTypeInfo->mIsUnspecialized = false;
			dlgType->mGenericTypeInfo->mIsUnspecializedVariation = false;
			dlgType->mGenericTypeInfo->mTypeGenericArguments = genericArgs;
			for (int genericArgIdx = 0; genericArgIdx < (int)unspecializedGenericDelegateType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
			{
				auto typeGenericArg = genericArgs[genericArgIdx];
				if ((typeGenericArg->IsGenericParam()) || (typeGenericArg->IsUnspecializedType()))
					dlgType->mGenericTypeInfo->mIsUnspecialized = true;
				dlgType->mGenericTypeInfo->mGenericParams.push_back(unspecializedGenericDelegateType->mGenericTypeInfo->mGenericParams[genericArgIdx]->AddRef());
			}
			CheckUnspecializedGenericType(dlgType, BfPopulateType_Identity);
			if (isUnspecialized)
			{
				dlgType->mGenericTypeInfo->mIsUnspecialized = true;
				dlgType->mGenericTypeInfo->mIsUnspecializedVariation = true;
			}

			dlgType->mIsUnspecializedType = dlgType->mGenericTypeInfo->mIsUnspecialized;
			dlgType->mIsUnspecializedTypeVariation = dlgType->mGenericTypeInfo->mIsUnspecializedVariation;
			delegateType = dlgType;
		}
		else
		{
			auto dlgType = mContext->mDelegateTypePool.Get();
			dlgType->mIsUnspecializedType = isUnspecialized;
			dlgType->mIsUnspecializedTypeVariation = isUnspecialized;
			delegateType = dlgType;
		}

		delete delegateType->mTypeDef;
		delegateType->mTypeDef = NULL;
		BfDelegateInfo* delegateInfo = delegateType->GetDelegateInfo();
		delegateInfo->mParams.Clear();

		BfTypeDef* typeDef = new BfTypeDef();

		typeDef->mProject = baseDelegateType->mTypeDef->mProject;
		typeDef->mSystem = mCompiler->mSystem;
		typeDef->mName = mSystem->mEmptyAtom;
		typeDef->mTypeCode = unspecializedDelegateType->mTypeDef->mTypeCode;
		typeDef->mIsDelegate = unspecializedDelegateType->mTypeDef->mIsDelegate;
		typeDef->mIsFunction = unspecializedDelegateType->mTypeDef->mIsFunction;

		BfMethodDef* unspecializedInvokeMethodDef = unspecializedDelegateType->mTypeDef->GetMethodByName("Invoke");

		BfMethodDef* methodDef = new BfMethodDef();
		methodDef->mDeclaringType = typeDef;
		methodDef->mName = "Invoke";
		methodDef->mProtection = BfProtection_Public;
		methodDef->mIdx = 0;
		methodDef->mIsStatic = !typeDef->mIsDelegate && !unspecializedDelegateInfo->mHasExplicitThis;
		methodDef->mHasExplicitThis = unspecializedDelegateInfo->mHasExplicitThis;

		auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
		if (typeDef->mIsDelegate)
			directTypeRef->Init(delegateType);
		else
			directTypeRef->Init(ResolveTypeDef(mCompiler->mFunctionTypeDef));
		typeDef->mBaseTypes.push_back(directTypeRef);

		directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
		directTypeRef->Init(returnType);
		methodDef->mReturnTypeRef = directTypeRef;
		delegateInfo->mReturnType = returnType;
		delegateInfo->mHasExplicitThis = unspecializedDelegateInfo->mHasExplicitThis;
		delegateInfo->mHasVarArgs = unspecializedDelegateInfo->mHasVarArgs;

		int paramIdx = 0;
		for (int paramIdx = 0; paramIdx < (int)paramTypes.size(); paramIdx++)
		{
			auto paramType = paramTypes[paramIdx];

			BfParameterDef* unspecializedParamDef = unspecializedInvokeMethodDef->mParams[paramIdx];

			if (!paramType->IsReified())
				delegateType->mIsReified = false;

			auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
			delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
			directTypeRef->Init(paramType);

			BfParameterDef* paramDef = new BfParameterDef();
			paramDef->mParamKind = unspecializedParamDef->mParamKind;
			paramDef->mTypeRef = directTypeRef;
			paramDef->mName = unspecializedParamDef->mName;
			methodDef->mParams.push_back(paramDef);

			delegateInfo->mParams.Add(paramType);
		}

		typeDef->mMethods.push_back(methodDef);

		if (unspecializedInvokeMethodDef->mIsMutating)
		{
			if ((delegateInfo->mParams[0]->IsValueType()) || (delegateInfo->mParams[0]->IsGenericParam()))
				methodDef->mIsMutating = unspecializedInvokeMethodDef->mIsMutating;
		}

		//

		if (typeDef->mIsDelegate)
		{
			BfDefBuilder::AddMethod(typeDef, BfMethodType_Ctor, BfProtection_Public, false, "");
			BfDefBuilder::AddDynamicCastMethods(typeDef);
		}

		delegateType->mContext = mContext;
		delegateType->mTypeDef = typeDef;

		BfType* resolvedType = NULL;
		if (!failed)
			resolvedType = ResolveType(delegateType, BfPopulateType_Identity);
		if (resolvedType == delegateType)
		{
			AddDependency(directTypeRef->mType, delegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
			for (auto paramType : paramTypes)
				AddDependency(paramType, delegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		}
		else
		{
			delegateType->Dispose();
			mContext->mDelegateTypePool.GiveBack((BfDelegateType*)delegateType);
		}
		BF_ASSERT((resolvedType == NULL) || resolvedType->IsTypeInstance() || resolvedType->IsPrimitiveType());
		return resolvedType;
	}

	if (unspecializedType->IsGenericTypeInstance())
	{
		auto genericTypeInst = (BfTypeInstance*)unspecializedType;

		BfTypeVector genericArgs;
		for (auto genericArg : genericTypeInst->mGenericTypeInfo->mTypeGenericArguments)
		{
			if (genericArg->IsUnspecializedType())
			{
				auto resolvedArg = ResolveGenericType(genericArg, typeGenericArguments, methodGenericArguments, selfType, allowFail);
				if (resolvedArg == NULL)
					return NULL;
				if (resolvedArg->IsVar())
					return resolvedArg;
				genericArgs.push_back(resolvedArg);
			}
			else
				genericArgs.push_back(genericArg);
		}

		auto resolvedType = ResolveTypeDef(genericTypeInst->mTypeDef->GetDefinition(), genericArgs, BfPopulateType_BaseType);
		BfTypeInstance* specializedType = NULL;
		if (resolvedType != NULL)
			specializedType = resolvedType->ToGenericTypeInstance();
		if (specializedType != NULL)
		{
			if (specializedType->mGenericTypeInfo->mHadValidateErrors)
				return NULL;
		}

		return specializedType;
	}

	return unspecializedType;
}

BfType* BfModule::ResolveSelfType(BfType* type, BfType* selfType)
{
	if (!type->IsUnspecializedTypeVariation())
		return type;
	return ResolveGenericType(type, NULL, NULL, selfType);
}

BfType* BfModule::ResolveType(BfType* lookupType, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mModule = this;
	lookupCtx.mResolveFlags = resolveFlags;
	BfResolvedTypeSet::EntryRef resolvedEntry;
	bool inserted = mContext->mResolvedTypes.Insert(lookupType, &lookupCtx, &resolvedEntry);

	if (!resolvedEntry)
		return NULL;

	if (!inserted)
	{
		auto resolvedTypeRef = resolvedEntry->mValue;
		PopulateType(resolvedTypeRef, populateType);
		return resolvedTypeRef;
	}

	if (lookupType->IsGenericTypeInstance())
		CheckUnspecializedGenericType((BfTypeInstance*)lookupType, populateType);

	if (lookupType->IsTuple())
	{
		auto tupleType = (BfTupleType*)lookupType;
		tupleType->Finish();
	}

	resolvedEntry->mValue = lookupType;
	InitType(lookupType, populateType);
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
	//TODO: This caused MethodToString issues with interface "implementation method not found" errors
// 	if (mCurMethodInstance != NULL)
// 		curTypeInstance = mCurMethodInstance->mMethodInstanceGroup->mOwner;

	BfTypeInstance* genericTypeInst = curTypeInstance->ToGenericTypeInstance();
	if ((genericTypeInst->IsIncomplete()) && (genericTypeInst->mGenericTypeInfo->mGenericParams.size() == 0))
	{
		// Set this to NULL so we don't recurse infinitely
		SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mCurTypeInstance, NULL);
		PopulateType(genericTypeInst, BfPopulateType_Declaration);
	}

	if (genericParamIdx >= (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size())
	{
		if (genericParamIdx >= genericTypeInst->mGenericTypeInfo->mGenericParams.mSize)
			FatalError("Invalid GetGenericTypeParamInstance");

		// Extern constraints should always be directly used - they don't get extended
		return genericTypeInst->mGenericTypeInfo->mGenericParams[genericParamIdx];
	}

	if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
	{
		bool isAutocomplete = (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL);

		auto activeTypeDef = GetActiveTypeDef(NULL, true);
		if ((activeTypeDef->mTypeDeclaration != genericTypeInst->mTypeDef->mTypeDeclaration) && (activeTypeDef->IsExtension()) &&
			((genericTypeInst->mTypeDef->ContainsPartial(activeTypeDef)) || (isAutocomplete)))
		{
			BfTypeDef* lookupTypeDef = activeTypeDef;
			while (lookupTypeDef->mNestDepth > genericTypeInst->mTypeDef->mNestDepth)
				lookupTypeDef = lookupTypeDef->mOuterType;

			BfGenericExtensionEntry* genericExEntry;
			if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(lookupTypeDef, &genericExEntry))
			{
				return genericExEntry->mGenericParams[genericParamIdx];
			}
			else
			{
				if (!isAutocomplete)
				{
					FatalError("Invalid GetGenericParamInstance with extension");
				}
			}
		}
	}

	BF_ASSERT(genericTypeInst != NULL);
	return genericTypeInst->mGenericTypeInfo->mGenericParams[genericParamIdx];
}

void BfModule::GetActiveTypeGenericParamInstances(SizedArray<BfGenericParamInstance*, 4>& genericParamInstances)
{
	// When we're evaluating a method, make sure the params refer back to that method context
	auto curTypeInstance = mCurTypeInstance;
	if (mCurMethodInstance != NULL)
		curTypeInstance = mCurMethodInstance->mMethodInstanceGroup->mOwner;

	BfTypeInstance* genericTypeInst = curTypeInstance->ToGenericTypeInstance();
	if ((genericTypeInst->IsIncomplete()) && (genericTypeInst->mGenericTypeInfo->mGenericParams.size() == 0))
	{
		// Set this to NULL so we don't recurse infinitely
		SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mCurTypeInstance, NULL);
		PopulateType(genericTypeInst, BfPopulateType_Declaration);
	}

	if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
	{
		auto activeTypeDef = GetActiveTypeDef(NULL, true);
		if ((activeTypeDef->mTypeDeclaration != genericTypeInst->mTypeDef->mTypeDeclaration) && (activeTypeDef->IsExtension()))
		{
			BfTypeDef* lookupTypeDef = activeTypeDef;
			while (lookupTypeDef->mNestDepth > genericTypeInst->mTypeDef->mNestDepth)
				lookupTypeDef = lookupTypeDef->mOuterType;

			BfGenericExtensionEntry* genericExEntry;
			if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(lookupTypeDef, &genericExEntry))
			{
				for (auto entry : genericExEntry->mGenericParams)
					genericParamInstances.Add(entry);

				auto genericTypeInfo = genericTypeInst->mGenericTypeInfo;

				// Add root extern constraints - they don't get extended
				for (int genericParamIdx = (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size(); genericParamIdx < genericTypeInst->mGenericTypeInfo->mGenericParams.size(); genericParamIdx++)
					genericParamInstances.Add(genericTypeInst->mGenericTypeInfo->mGenericParams[genericParamIdx]);
				return;
			}
			else
			{
				if ((mCompiler->mResolvePassData == NULL) || (mCompiler->mResolvePassData->mAutoComplete == NULL))
				{
					BFMODULE_FATAL(this, "Invalid GetGenericParamInstance with extension");
				}
			}
		}
	}

	BF_ASSERT(genericTypeInst != NULL);
	for (auto entry : genericTypeInst->mGenericTypeInfo->mGenericParams)
		genericParamInstances.Add(entry);
}

BfGenericParamInstance* BfModule::GetMergedGenericParamData(BfGenericParamType* type, BfGenericParamFlags& outFlags, BfType*& outTypeConstraint)
{
	BfGenericParamInstance* genericParam = GetGenericParamInstance(type);
	outFlags = genericParam->mGenericParamFlags;
	outTypeConstraint = genericParam->mTypeConstraint;

	// Check method generic constraints
	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecialized) && (mCurMethodInstance->mMethodInfoEx != NULL))
	{
		for (int genericParamIdx = (int)mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
			genericParamIdx < mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParam = mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			if (genericParam->mExternType == type)
			{
				outFlags = (BfGenericParamFlags)(outFlags | genericParam->mGenericParamFlags);
				if (genericParam->mTypeConstraint != NULL)
					outTypeConstraint = genericParam->mTypeConstraint;
			}
		}
	}
	return genericParam;
}

BfGenericParamInstance* BfModule::GetGenericParamInstance(BfGenericParamType* type, bool checkMixinBind)
{
	if (type->mGenericParamKind == BfGenericParamKind_Method)
	{
		auto curGenericMethodInstance = mCurMethodInstance;
		if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
		{
			if ((checkMixinBind) || (mCurMethodState->mMixinState->mUseMixinGenerics))
				curGenericMethodInstance = mCurMethodState->mMixinState->mMixinMethodInstance;
		}

		if ((curGenericMethodInstance == NULL) || (curGenericMethodInstance->mMethodInfoEx == NULL) || (type->mGenericParamIdx >= curGenericMethodInstance->mMethodInfoEx->mGenericParams.mSize))
		{
			FatalError("Invalid GetGenericParamInstance method generic param");
			return NULL;
		}
		return curGenericMethodInstance->mMethodInfoEx->mGenericParams[type->mGenericParamIdx];
	}

	return GetGenericTypeParamInstance(type->mGenericParamIdx);
}

bool BfModule::ResolveTypeResult_Validate(BfAstNode* typeRef, BfType* resolvedTypeRef)
{
	if ((typeRef == NULL) || (resolvedTypeRef == NULL))
		return true;

	BfTypeInstance* genericTypeInstance = resolvedTypeRef->ToGenericTypeInstance();

	if ((genericTypeInstance != NULL) && (genericTypeInstance != mCurTypeInstance))
	{
		bool doValidate = (genericTypeInstance->mGenericTypeInfo->mHadValidateErrors) ||
			(!genericTypeInstance->mGenericTypeInfo->mValidatedGenericConstraints) ||
			(genericTypeInstance->mGenericTypeInfo->mIsUnspecializedVariation);
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->IsOrInUnspecializedVariation()))
			doValidate = false;
		if (mCurTypeInstance != NULL)
		{
			if (mCurTypeInstance->IsUnspecializedTypeVariation())
				doValidate = false;
			if (auto curGenericTypeInstance = mCurTypeInstance->ToGenericTypeInstance())
			{
				if ((curGenericTypeInstance->mDependencyMap.mMinDependDepth > 32) &&
					(genericTypeInstance->mDependencyMap.mMinDependDepth > 32))
				{
					Fail(StrFormat("Generic type dependency depth exceeded for type '%s'", TypeToString(genericTypeInstance).c_str()), typeRef);
					return false;
				}

				if (curGenericTypeInstance->mGenericTypeInfo->mHadValidateErrors)
					doValidate = false;
			}
			if ((mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mCurBaseTypeRef != NULL) && (!mContext->mCurTypeState->mType->IsTypeAlias())) // We validate constraints for base types later
				doValidate = false;
		}

		if (doValidate)
			ValidateGenericConstraints(typeRef, genericTypeInstance, false);
	}

	if (auto genericInstanceTypeRef = BfNodeDynCastExact<BfGenericInstanceTypeRef>(typeRef))
	{
		if (genericTypeInstance != NULL)
		{
			auto genericTypeInfo = genericTypeInstance->GetGenericTypeInfo();
			for (int argIdx = 0; argIdx < (int)genericInstanceTypeRef->mGenericArguments.size(); argIdx++)
			{
				ResolveTypeResult_Validate(genericInstanceTypeRef->mGenericArguments[argIdx], genericTypeInfo->mTypeGenericArguments[argIdx]);
			}
		}
	}
	else if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeRef))
	{
		return ResolveTypeResult_Validate(elementedTypeRef, resolvedTypeRef->GetUnderlyingType());
	}

	return true;
}

BfType* BfModule::SafeResolveAliasType(BfTypeAliasType* aliasType)
{
	int aliasDepth = 0;
	HashSet<BfType*> seenAliases;

	BfType* type = aliasType;
	while (type->IsTypeAlias())
	{
		aliasDepth++;
		if (aliasDepth > 8)
		{
			if (!seenAliases.Add(type))
				return NULL;
		}

		type = type->GetUnderlyingType();
		if (type == NULL)
			return NULL;
	}
	return type;
}

BfType* BfModule::ResolveTypeResult(BfTypeReference* typeRef, BfType* resolvedTypeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	if ((mCompiler->mIsResolveOnly) && (!IsInSpecializedSection()))
	{
		bool isGetDefinition = false;
		BfAutoComplete* autoComplete = NULL;
		if (mCompiler->IsAutocomplete())
		{
			autoComplete = mCompiler->mResolvePassData->mAutoComplete;
			isGetDefinition = autoComplete->mIsGetDefinition || (autoComplete->mResolveType == BfResolveType_GetResultString);
		}

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

		BfSourceClassifier* sourceClassifier = NULL;
		if ((mCompiler->mResolvePassData->mIsClassifying) && (typeRefSource != NULL))
		{
			auto parser = typeRefSource->ToParser();
			if (parser != NULL)
				sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(parser);
		}

		bool wantsFileNamespaceInfo = ((sourceClassifier != NULL) || (isGetDefinition) || (mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Namespace));

		bool wantsAllNamespaceInfo = (mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Namespace) && (mCompiler->mResolvePassData->mParsers.IsEmpty());

		if (wantsFileNamespaceInfo || wantsAllNamespaceInfo)
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
			if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(checkTypeRef))
				checkTypeRef = genericTypeRef->mElementType;
			auto headTypeRef = checkTypeRef;
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

			BfSourceElementType elemType = BfSourceElementType_Type;
			{
				auto type = resolvedTypeRef;

				if (type->IsTypeAlias())
				{
					type = SafeResolveAliasType((BfTypeAliasType*)type);
					if (type == NULL)
						type = resolvedTypeRef;
				}

				if (type->IsInterface())
					elemType = BfSourceElementType_Interface;
				else if (type->IsObject())
					elemType = BfSourceElementType_RefType;
				else if (type->IsGenericParam())
					elemType = BfSourceElementType_GenericParam;
				else if (type->IsPrimitiveType())
					elemType = BfSourceElementType_PrimitiveType;
				else if (type->IsStruct() || (type->IsTypedPrimitive() && !type->IsEnum()))
					elemType = BfSourceElementType_Struct;
			}

			while (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(checkTypeRef))
			{
				if ((sourceClassifier != NULL) && (checkTypeRef == headTypeRef) && (elemType != BfSourceElementType_Type))
					sourceClassifier->SetElementType(qualifiedTypeRef->mRight, elemType);

				StringView leftString = qualifiedTypeRef->mLeft->ToStringView();
				BfSizedAtomComposite leftComposite;
				bool isValid = mSystem->ParseAtomComposite(leftString, leftComposite);
				if (sourceClassifier != NULL)
					sourceClassifier->SetHighestElementType(qualifiedTypeRef->mRight, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_Type);
				if (resolvedTypeInstance == NULL)
				{
					if ((isValid) && (mCompiler->mSystem->ContainsNamespace(leftComposite, mCurTypeInstance->mTypeDef->mProject)))
						isNamespace = true;
				}
				else if ((isValid) && (resolvedTypeInstance->mTypeDef->mNamespace.EndsWith(leftComposite)) && (resolvedTypeInstance->mTypeDef->mOuterType == NULL))
				{
					if (autoComplete != NULL)
					{
						if (autoComplete->CheckFixit(typeRef))
							autoComplete->FixitCheckNamespace(GetActiveTypeDef(), qualifiedTypeRef->mLeft, qualifiedTypeRef->mDot);
						autoComplete->CheckNamespace(qualifiedTypeRef->mLeft, resolvedTypeInstance->mTypeDef->mNamespace);
					}
					mCompiler->mResolvePassData->HandleNamespaceReference(qualifiedTypeRef->mLeft, resolvedTypeInstance->mTypeDef->mNamespace);

					isNamespace = true;
				}
				checkTypeRef = qualifiedTypeRef->mLeft;
			}

			if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(checkTypeRef))
			{
				auto checkNameNode = namedTypeRef->mNameNode;
				bool setType = false;

				if ((sourceClassifier != NULL) && (checkTypeRef == headTypeRef) && (elemType != BfSourceElementType_Type))
				{
					if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNameNode))
					{
						sourceClassifier->SetElementType(qualifiedNameNode->mRight, elemType);
					}
					else
					{
						setType = true;
						sourceClassifier->SetElementType(checkNameNode, elemType);
					}
				}

				while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNameNode))
				{
					StringView leftString =  qualifiedNameNode->mLeft->ToStringView();
					BfSizedAtomComposite leftComposite;
					bool isValid = mSystem->ParseAtomComposite(leftString, leftComposite);
					if (sourceClassifier != NULL)
						sourceClassifier->SetHighestElementType(qualifiedNameNode->mRight, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_Type);
					if (resolvedTypeInstance == NULL)
					{
						if ((isValid) && (mCompiler->mSystem->ContainsNamespace(leftComposite, mCurTypeInstance->mTypeDef->mProject)))
							isNamespace = true;
					}
					else if ((isValid) && (resolvedTypeInstance->mTypeDef->mOuterType == NULL) && (resolvedTypeInstance->mTypeDef->mNamespace.EndsWith(leftComposite)))
					{
						if (autoComplete != NULL)
						{
							if (autoComplete->CheckFixit(typeRef))
								autoComplete->FixitCheckNamespace(GetActiveTypeDef(), qualifiedNameNode->mLeft, qualifiedNameNode->mDot);
							autoComplete->CheckNamespace(qualifiedNameNode->mLeft, resolvedTypeInstance->mTypeDef->mNamespace);
						}
						mCompiler->mResolvePassData->HandleNamespaceReference(qualifiedNameNode->mLeft, resolvedTypeInstance->mTypeDef->mNamespace);

						isNamespace = true;
					}
					checkNameNode = qualifiedNameNode->mLeft;
				}
				if ((sourceClassifier != NULL) &&
					((!setType) || (checkNameNode != namedTypeRef->mNameNode)))
					sourceClassifier->SetHighestElementType(checkNameNode, isNamespace ? BfSourceElementType_Namespace : BfSourceElementType_Type);
			}
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
						if ((isGetDefinition) && (autoComplete->IsAutocompleteNode(elementTypeRef)))
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
								else if (auto declTypeRef = BfNodeDynCast<BfExprModTypeRef>(baseNode))
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

								if ((autoComplete->mResolveType == BfResolveType_GetResultString) && (resolvedTypeRef != NULL))
								{
									autoComplete->SetResultStringType(resolvedTypeRef);
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

	if (mCurTypeInstance == NULL)
	{
		// No deps
	}
	else if (resolvedTypeRef->IsTuple())
	{
		// Add the fields from the tuple as references since those inner fields types would have been explicitly stated, so we need
		//  to make sure to record the current type instance as a referring type.  This mostly matters for symbol renaming.
		BfTypeInstance* payloadTupleType = (BfTypeInstance*)resolvedTypeRef;
		for (auto& payloadFieldInst : payloadTupleType->mFieldInstances)
		{
			auto payloadFieldType = payloadFieldInst.mResolvedType;
			AddDependency(payloadFieldType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		}
	}
	else if (resolvedTypeRef->IsDelegateFromTypeRef() || resolvedTypeRef->IsFunctionFromTypeRef())
	{
		auto delegateInfo = resolvedTypeRef->GetDelegateInfo();
// 		if (delegateInfo->mFunctionThisType != NULL)
// 			AddDependency(delegateInfo->mFunctionThisType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		AddDependency(delegateInfo->mReturnType, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
		for (auto& param : delegateInfo->mParams)
			AddDependency(param, mCurTypeInstance, BfDependencyMap::DependencyFlag_TypeReference);
	}

	BfTypeInstance* typeInstance = resolvedTypeRef->ToTypeInstance();
	BfTypeInstance* genericTypeInstance = resolvedTypeRef->ToGenericTypeInstance();

	auto populateModule = this;
	if ((resolveFlags & BfResolveTypeRefFlag_NoReify) != 0)
		populateModule = mContext->mUnreifiedModule;

	populateModule->PopulateType(resolvedTypeRef, populateType);

	if ((typeInstance != NULL) && (typeInstance->mTypeDef != NULL) && (typeInstance->mTypeDef->mProtection == BfProtection_Internal) &&
		(typeInstance != mCurTypeInstance) && (typeInstance->mTypeDef->mOuterType == NULL) && (!typeRef->IsTemporary()))
	{
		if (!CheckProtection(typeInstance->mTypeDef->mProtection, typeInstance->mTypeDef, false, false))
			Fail(StrFormat("'%s' is inaccessible due to its protection level", TypeToString(typeInstance).c_str()), typeRef); // CS0122
	}

	// If the inner type is definted in an extension then we need to make sure the constraints are good
	if ((typeInstance != NULL) && (typeInstance->mTypeDef != NULL) && (typeInstance->mTypeDef->mOuterType != NULL) &&
		(typeInstance->mTypeDef->mOuterType->mTypeCode == BfTypeCode_Extension))
	{
		auto outerType = GetOuterType(typeInstance);
		if ((outerType->mGenericTypeInfo != NULL) && (outerType->mGenericTypeInfo->mGenericExtensionInfo != NULL))
		{
			if (!outerType->mGenericTypeInfo->mGenericExtensionInfo->mConstraintsPassedSet.IsSet(typeInstance->mTypeDef->mOuterType->mPartialIdx))
			{
				Fail(StrFormat("'%s' is declared inside a type extension whose constraints were not met", TypeToString(typeInstance).c_str()), typeRef);
			}
		}
	}

	if (populateType > BfPopulateType_IdentityNoRemapAlias)
	{
		if (!ResolveTypeResult_Validate(typeRef, resolvedTypeRef))
			return NULL;
	}

	if ((populateType != BfPopulateType_TypeDef) && (populateType != BfPopulateType_IdentityNoRemapAlias))
	{
		int aliasDepth = 0;
		HashSet<BfType*> seenAliases;

		while ((resolvedTypeRef != NULL) && (resolvedTypeRef->IsTypeAlias()))
		{
			aliasDepth++;
			if (aliasDepth > 8)
			{
				if (!seenAliases.Add(resolvedTypeRef))
				{
					if ((typeRef != NULL) && (!typeRef->IsTemporary()))
						Fail(StrFormat("Type alias '%s' has a recursive definition", TypeToString(resolvedTypeRef).c_str()), typeRef);
					break;
				}
			}

			if (mCurTypeInstance != NULL)
				AddDependency(resolvedTypeRef, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
			if (resolvedTypeRef->mDefineState == BfTypeDefineState_Undefined)
				PopulateType(resolvedTypeRef);
			if ((typeInstance->mCustomAttributes != NULL) && (!typeRef->IsTemporary()))
				CheckErrorAttributes(typeInstance, NULL, typeInstance->mCustomAttributes, typeRef);
			resolvedTypeRef = resolvedTypeRef->GetUnderlyingType();
			if (resolvedTypeRef != NULL)
				typeInstance = resolvedTypeRef->ToTypeInstance();
			else
				typeInstance = NULL;
		}
	}

	if (typeInstance != NULL)
	{
		if ((!typeRef->IsTemporary()) && ((resolveFlags & BfResolveTypeRefFlag_FromIndirectSource) == 0))
		{
			if (typeInstance->mCustomAttributes != NULL)
				CheckErrorAttributes(typeInstance, NULL, typeInstance->mCustomAttributes, typeRef);
			else if ((typeInstance->mTypeDef->mTypeDeclaration != NULL) && (typeInstance->mTypeDef->mTypeDeclaration->mAttributes != NULL))
			{
				auto typeRefVerifyRequest = mContext->mTypeRefVerifyWorkList.Alloc();
				typeRefVerifyRequest->mCurTypeInstance = mCurTypeInstance;
				typeRefVerifyRequest->mRefNode = typeRef;
				typeRefVerifyRequest->mType = typeInstance;
				typeRefVerifyRequest->mFromModule = this;
				typeRefVerifyRequest->mFromModuleRevision = mRevision;
			}
		}

		if (typeInstance->IsTuple())
		{
			//TODO: This can cause circular reference issues. Is there a case this is needed?
			//if (typeInstance->mDefineState < BfTypeDefineState_Defined)
			//	PopulateType(typeInstance);
			if (typeInstance->mHasDeclError)
			{
				if (auto tupleTypeRef = BfNodeDynCast<BfTupleTypeRef>(typeRef))
				{
					HashSet<String> names;
					for (auto nameIdentifier : tupleTypeRef->mFieldNames)
					{
						if (nameIdentifier == NULL)
							continue;
						StringT<64> fieldName;
						nameIdentifier->ToString(fieldName);
						if (!names.Add(fieldName))
						{
							Fail(StrFormat("A field named '%s' has already been declared", fieldName.c_str()), nameIdentifier);
						}
					}
				}
			}
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

void BfModule::ShowGenericArgCountError(BfAstNode* typeRef, int wantedGenericParams)
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
	if ((mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mForceActiveTypeDef != NULL))
		return mContext->mCurTypeState->mForceActiveTypeDef;
	if (typeInstance != NULL)
		useTypeDef = typeInstance->mTypeDef->GetDefinition();
	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL) && (useMixinDecl))
		useTypeDef = mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType->GetDefinition();
	else if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mDeclaringType != NULL))
	{
		auto declTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;
		useTypeDef = declTypeDef->GetDefinition();
		if ((declTypeDef->IsEmitted()) && (useTypeDef->mIsCombinedPartial))
		{
			// Always consider methods to belong to the primary type declaration
			useTypeDef = useTypeDef->mPartials[0];
		}
	}
	else if (mContext->mCurTypeState != NULL)
	{
		if ((mContext->mCurTypeState->mCurFieldDef != NULL) && (mContext->mCurTypeState->mCurFieldDef->mDeclaringType != NULL))
			useTypeDef = mContext->mCurTypeState->mCurFieldDef->mDeclaringType->GetDefinition();
		else if (mContext->mCurTypeState->mCurTypeDef != NULL)
			useTypeDef = mContext->mCurTypeState->mCurTypeDef->GetDefinition();
	}
	return useTypeDef;
}

BfTypeDef* BfModule::FindTypeDefRaw(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstance, BfTypeDef* useTypeDef, BfTypeLookupError* error, BfTypeLookupResultCtx* lookupResultCtx, BfResolveTypeRefFlags resolveFlags)
{
	if ((findName.mSize == 1) && (findName.mParts[0]->mIsSystemType))
	{
		//BP_ZONE("BfModule::FindTypeDefRaw_1");
		return mSystem->FindTypeDef(findName, 0, useTypeDef->mProject);
	}

	BfTypeInstance* skipCheckBaseType = NULL;
	if (mContext->mCurTypeState != NULL)
	{
		if (mContext->mCurTypeState->mCurBaseTypeRef != NULL)
			skipCheckBaseType = mContext->mCurTypeState->mType->ToTypeInstance();
		if (mContext->mCurTypeState->mResolveKind == BfTypeState::ResolveKind_BuildingGenericParams)
			skipCheckBaseType = mContext->mCurTypeState->mType->ToTypeInstance();
	}

	BfTypeDefLookupContext lookupCtx;
	bool allowPrivate = true;

	int curPri = 1000;
	auto checkTypeInst = typeInstance;

	BfTypeDef* protErrorTypeDef = NULL;
	BfTypeInstance* protErrorOuterType = NULL;

	BfTypeDef* foundInnerType = NULL;

	if ((lookupResultCtx != NULL) && (lookupResultCtx->mIsVerify))
	{
		if (lookupResultCtx->mResult->mFoundInnerType)
			return lookupCtx.mBestTypeDef;
	}
	else
	{
		if ((!lookupCtx.HasValidMatch()) && (typeInstance != NULL))
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
							foundInnerType = lookupCtx.mBestTypeDef;

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

	if (!lookupCtx.HasValidMatch())
	{
		auto staticSearch = GetStaticSearch();
		if (staticSearch != NULL)
		{
			for (auto staticTypeInstance : staticSearch->mStaticTypes)
			{
				if (mSystem->FindTypeDef(findName, numGenericArgs, useTypeDef->mProject, staticTypeInstance->mTypeDef->mFullNameEx, false, &lookupCtx))
				{
					if (lookupCtx.HasValidMatch())
						break;

					if (lookupCtx.mBestTypeDef->mProtection < BfProtection_Public)
					{
						protErrorTypeDef = lookupCtx.mBestTypeDef;
						protErrorOuterType = staticTypeInstance;
					}
				}
			}
		}
	}

	if ((!lookupCtx.HasValidMatch()) && (typeInstance == NULL))
	{
		if (useTypeDef->mOuterType != NULL)
			return FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef->mOuterType, error);
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

	if ((lookupResultCtx != NULL) && (lookupResultCtx->mResult != NULL) && (!lookupResultCtx->mIsVerify) && (foundInnerType != NULL) && (foundInnerType == lookupCtx.mBestTypeDef))
		lookupResultCtx->mResult->mFoundInnerType = true;

	if (((resolveFlags & BfResolveTypeRefFlag_AllowGlobalContainer) == 0) && (lookupCtx.mBestTypeDef != NULL) && (lookupCtx.mBestTypeDef->IsGlobalsContainer()))
		return NULL;

	return lookupCtx.mBestTypeDef;
}

BfTypeDef* BfModule::FindTypeDef(const BfAtomComposite& findName, int numGenericArgs, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error, BfResolveTypeRefFlags resolveFlags)
{
	//BP_ZONE("BfModule::FindTypeDef_1");

	BfTypeInstance* typeInstance = (typeInstanceOverride != NULL) ? typeInstanceOverride : mCurTypeInstance;
	auto useTypeDef = GetActiveTypeDef(typeInstanceOverride, true);

	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
		typeInstance = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();

	if (useTypeDef != NULL)
		useTypeDef = useTypeDef->GetDefinition();

	if ((typeInstance == NULL) && (useTypeDef == NULL))
	{
		BfProject* project = NULL;
		if ((mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mActiveProject != NULL))
			project = mContext->mCurTypeState->mActiveProject;
		else if ((mCompiler->mResolvePassData != NULL) && (!mCompiler->mResolvePassData->mParsers.IsEmpty()))
			project = mCompiler->mResolvePassData->mParsers[0]->mProject;

		//BP_ZONE("System.FindTypeDef_2");
		Array<BfAtomComposite> namespaceSearch;
		if (mContext->mCurNamespaceNodes != NULL)
		{
			String checkNamespace;
 			for (auto namespaceNode : *mContext->mCurNamespaceNodes)
 			{
				if (namespaceNode->mNameNode != NULL)
				{
					if (!checkNamespace.IsEmpty())
						checkNamespace += ".";
					namespaceNode->mNameNode->ToString(checkNamespace);
				}
 			}

			if (!checkNamespace.IsEmpty())
			{
				BfAtomCompositeT<16> atomComposite;
				if (mSystem->ParseAtomComposite(checkNamespace, atomComposite))
					namespaceSearch.Add(atomComposite);
			}
		}

		BfFindTypeDefFlags findDefFlags = BfFindTypeDefFlag_None;
		if ((resolveFlags & BfResolveTypeRefFlag_AllowGlobalContainer) != 0)
			findDefFlags = (BfFindTypeDefFlags)(findDefFlags | BfFindTypeDefFlag_AllowGlobal);

		BfTypeDef* ambiguousTypeDef = NULL;
		BfTypeDef *result = mSystem->FindTypeDef(findName, numGenericArgs, project, namespaceSearch, &ambiguousTypeDef, findDefFlags);
		if ((ambiguousTypeDef != NULL) && (error != NULL))
		{
			error->mErrorKind = BfTypeLookupError::BfErrorKind_Ambiguous;
			error->mAmbiguousTypeDef = ambiguousTypeDef;
			if (error->mRefNode != NULL)
				ShowAmbiguousTypeError(error->mRefNode, result, ambiguousTypeDef);
		}

		return result;
	}

	if ((mCompiler->mResolvePassData != NULL) && (typeInstance != NULL))
	{
		if (mCompiler->mResolvePassData->mAutoCompleteTempTypes.Contains(useTypeDef))
			return FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, error, NULL, resolveFlags);
	}

	BfTypeLookupEntry typeLookupEntry;
	typeLookupEntry.mName.Reference(findName);
	typeLookupEntry.mNumGenericParams = numGenericArgs;
	typeLookupEntry.mUseTypeDef = useTypeDef;

	BfTypeLookupEntry* typeLookupEntryPtr = NULL;
	BfTypeLookupResult* resultPtr = NULL;
	if ((typeInstance != NULL) && (typeInstance->mLookupResults.TryAdd(typeLookupEntry, &typeLookupEntryPtr, &resultPtr)))
	{
		BF_ASSERT(!useTypeDef->IsEmitted());

		bool isValid;
		if (useTypeDef->mIsPartial)
			isValid = typeInstance->mTypeDef->GetDefinition()->ContainsPartial(useTypeDef);
		else
			isValid = typeInstance->mTypeDef->GetDefinition() == useTypeDef;

		if ((!isValid) && (mCurMethodInstance != NULL) && (mCurMethodInstance->mIsForeignMethodDef))
		{
			BF_ASSERT(mCurMethodInstance->mIsForeignMethodDef);
			isValid = mCurMethodInstance->mMethodDef->mDeclaringType == useTypeDef;
		}

		BF_ASSERT(isValid);

		typeLookupEntryPtr->mAtomUpdateIdx = typeLookupEntry.mName.GetAtomUpdateIdx();

		// FindTypeDefRaw may re-enter when finding base types, so we need to expect that resultPtr can change
		resultPtr->mForceLookup = true;
		resultPtr->mTypeDef = NULL;
		int prevAllocSize = (int)typeInstance->mLookupResults.size();

		BfTypeLookupError localError;
		BfTypeLookupError* errorPtr = (error != NULL) ? error : &localError;

		BfTypeLookupResultCtx lookupResultCtx;
		lookupResultCtx.mResult = resultPtr;
		auto typeDef = FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, errorPtr, &lookupResultCtx, resolveFlags);

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
		if ((resultPtr == NULL) || (resultPtr->mForceLookup))
			return FindTypeDefRaw(findName, numGenericArgs, typeInstance, useTypeDef, error, NULL, resolveFlags);
		else
			return resultPtr->mTypeDef;
	}
}

BfTypeDef* BfModule::FindTypeDef(const StringImpl& typeName, int numGenericArgs, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error, BfResolveTypeRefFlags resolveFlags)
{
	//BP_ZONE("BfModule::FindTypeDef_4");

	BfSizedAtomComposite findName;
	if (!mSystem->ParseAtomComposite(typeName, findName))
		return NULL;
	auto result = FindTypeDef(findName, numGenericArgs, typeInstanceOverride, error, resolveFlags);
	// Don't allow just finding extensions here. This can happen in some 'using static' cases but generally shouldn't happen
	if ((result != NULL) && (result->mTypeCode == BfTypeCode_Extension))
		return NULL;
	return result;
}

BfTypeDef* BfModule::FindTypeDef(BfTypeReference* typeRef, BfTypeInstance* typeInstanceOverride, BfTypeLookupError* error, int numGenericParams, BfResolveTypeRefFlags resolveFlags)
{
	//BP_ZONE("BfModule::FindTypeDef_5");

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
			BFMODULE_FATAL(this, "Error?");
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
	if ((resolveFlags & BfResolveTypeRefFlag_Attribute) != 0)
	{
		String attributeName;
		attributeName += findNameStr;
		attributeName += "Attribute";
		if (!mSystem->ParseAtomComposite(attributeName, findName))
			return NULL;
	}
	else
	{
		if (!mSystem->ParseAtomComposite(findNameStr, findName))
			return NULL;
	}

#ifdef BF_AST_HAS_PARENT_MEMBER
	if (auto parentGenericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef->mParent))
	{
		if (parentGenericTypeRef->mElementType == typeRef)
			BF_ASSERT(numGenericParams == parentGenericTypeRef->GetGenericArgCount());
	}
#endif

	auto typeDef = FindTypeDef(findName, numGenericParams, typeInstanceOverride, error, resolveFlags);
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
		mSystem->FindFixitNamespaces(typeName, -1, mCompiler->mResolvePassData->mParsers[0]->mProject, fixitNamespaces);

		int insertLoc = 0;

		BfUsingFinder usingFinder;
		usingFinder.mFromIdx = typeRef->mSrcStart;
		usingFinder.VisitMembers(typeRef->GetSourceData()->mRootNode);

		for (auto& namespaceStr : fixitNamespaces)
		{
			BfParserData* parser = typeRef->GetSourceData()->ToParserData();
			if (parser != NULL)
				mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("using %s;\t.using|%s|%d||using %s;", namespaceStr.c_str(), parser->mFileName.c_str(), usingFinder.mLastIdx, namespaceStr.c_str()).c_str()));
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

	if (PreFail())
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

bool BfModule::ValidateTypeWildcard(BfAstNode* typeRef, bool isAttributeRef)
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

		if (auto wildcardTypeRef = BfNodeDynCast<BfWildcardTypeReference>(qualifiedTypeRef->mRight))
		{
			StringT<128> leftNameStr;

			BfType* leftType = NULL;
			BfAtomCompositeT<16> leftComposite;

			qualifiedTypeRef->mLeft->ToString(leftNameStr);
			if (!mSystem->ParseAtomComposite(leftNameStr, leftComposite))
				return false;

			if (mSystem->ContainsNamespace(leftComposite, NULL))
				return true;

			return ValidateTypeWildcard(qualifiedTypeRef->mLeft, false);
		}
	}

	if (!BfNodeIsA<BfGenericInstanceTypeRef>(typeRef))
	{
		if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeRef))
		{
			return ValidateTypeWildcard(elementedTypeRef->mElementType, false);
		}
	}

	BfAstNode* origTypeRef = typeRef;

	String name;
	String nameEx;
	int genericCount = 0;
	std::function<bool(BfAstNode*, bool)> _ToString = [&](BfAstNode* typeRef, bool isLast)
	{
		if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
		{
			_ToString(qualifiedTypeRef->mLeft, false);
			name.Append(".");
			nameEx.Append(".");
			_ToString(qualifiedTypeRef->mRight, typeRef == origTypeRef);
			return true;
		}

		if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
		{
			_ToString(genericTypeRef->mElementType, false);
			genericCount += genericTypeRef->mCommas.mSize + 1;
			for (auto genericArg : genericTypeRef->mGenericArguments)
				if (!ValidateTypeWildcard(genericArg, false))
					return false;
		}
		else
		{
			typeRef->ToString(name);
			typeRef->ToString(nameEx);
		}

		if (genericCount > 0)
		{
			if (!isLast)
				name += StrFormat("`%d", genericCount);
			nameEx += StrFormat("`%d", genericCount);
		}

		return true;
	};

	if (!_ToString(typeRef, true))
		return false;

	BfAtomCompositeT<16> composite;
	if (!mSystem->ParseAtomComposite(name, composite))
		return false;

	BfAtomCompositeT<16> compositeEx;
	if (!mSystem->ParseAtomComposite(nameEx, compositeEx))
		return false;

	auto itr = mSystem->mTypeDefs.TryGet(composite);
	while (itr != mSystem->mTypeDefs.end())
	{
		auto typeDef = *itr;
		if (typeDef->mFullName != composite)
			break;
		if (typeDef->mFullNameEx == compositeEx)
			return true;
		++itr;
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

		bool doUndefVal = false;
		if (genericCheckTypeInstance->IsUnspecializedTypeVariation())
		{
			genericCheckTypeInstance = GetUnspecializedTypeInstance(genericCheckTypeInstance);
			doUndefVal = true;
		}

		BfGenericParamDef* genericParamDef = NULL;
		BfGenericParamDef* origGenericParamDef = NULL;
		BfType* genericParamResult = NULL;
		BfType* genericTypeConstraint = NULL;
		bool disallowConstExprValue = false;
		if ((genericCheckTypeInstance != NULL) && (genericCheckTypeInstance->IsGenericTypeInstance()))
		{
			auto genericTypeInst = (BfTypeInstance*)genericCheckTypeInstance;
			auto* genericParams = &curTypeDef->mGenericParamDefs;
			auto* origGenericParams = &curTypeDef->mGenericParamDefs;

			if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
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
					origGenericParamDef = (*origGenericParams)[genericParamIdx];
					genericParamResult = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[genericParamIdx];
					genericTypeConstraint = genericTypeInst->mGenericTypeInfo->mGenericParams[genericParamIdx]->mTypeConstraint;

					if (contextTypeInstance != genericCheckTypeInstance)
					{
						// Don't allow an 'unspecialized variation' generic param
						auto checkResult = contextTypeInstance->mGenericTypeInfo->mTypeGenericArguments[genericParamIdx];
						if (!checkResult->IsGenericParam())
							genericParamResult = checkResult;
					}

					HandleTypeGenericParamRef(identifierNode, genericTypeInst->mTypeDef, genericParamIdx);
				}
			}
		}

		if ((contextMethodInstance != NULL) && (genericParamResult == NULL))
		{
			auto checkMethodInstance = contextMethodInstance;
			if (checkMethodInstance->mIsUnspecializedVariation)
				checkMethodInstance = GetUnspecializedMethodInstance(checkMethodInstance);

			for (int genericParamIdx = (int)checkMethodInstance->mMethodDef->mGenericParams.size() - 1; genericParamIdx >= 0; genericParamIdx--)
			{
				auto checkGenericParamDef = checkMethodInstance->mMethodDef->mGenericParams[genericParamIdx];
				String genericName = checkGenericParamDef->mName;
				if (genericName == findName)
				{
					genericParamDef = checkGenericParamDef;
					origGenericParamDef = checkGenericParamDef;
					genericParamResult = checkMethodInstance->mMethodInfoEx->mMethodGenericArguments[genericParamIdx];
					genericTypeConstraint = checkMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx]->mTypeConstraint;

					if (contextMethodInstance != checkMethodInstance)
					{
						// Don't allow an 'unspecialized variation' generic param
						auto checkResult = contextMethodInstance->mMethodInfoEx->mMethodGenericArguments[genericParamIdx];
						if (!checkResult->IsGenericParam())
							genericParamResult = checkResult;
					}

					HandleMethodGenericParamRef(identifierNode, contextMethodInstance->GetOwner()->mTypeDef, checkMethodInstance->mMethodDef, genericParamIdx);
				}
			}
		}

		if (genericParamResult != NULL)
		{
			auto typeRefSource = identifierNode->GetSourceData();
			if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mIsClassifying) && (typeRefSource != NULL))
			{
				if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(identifierNode))
					sourceClassifier->SetElementType(identifierNode, BfSourceElementType_GenericParam);
			}

			if (genericParamResult->IsConstExprValue())
			{
				BfConstExprValueType* constExprValueType = (BfConstExprValueType*)genericParamResult;

				auto constType = genericTypeConstraint;
				if (constType == NULL)
					constType = GetPrimitiveType(BfTypeCode_IntPtr);

				BfExprEvaluator exprEvaluator(this);
				exprEvaluator.mExpectingType = constType;
				exprEvaluator.GetLiteral(identifierNode, constExprValueType->mValue);

				if (exprEvaluator.mResult)
				{
					auto castedVal = CastToValue(identifierNode, exprEvaluator.mResult, constType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail));
					if (castedVal)
						return BfTypedValue(castedVal, constType);
				}

				return exprEvaluator.mResult;
			}
			else if (genericParamResult->IsGenericParam())
			{
				if ((doUndefVal) && (genericTypeConstraint != NULL))
				{
					return GetDefaultTypedValue(genericTypeConstraint, false, BfDefaultValueKind_Undef);
				}

				if (((genericParamDef->mGenericParamFlags | origGenericParamDef->mGenericParamFlags) & BfGenericParamFlag_Const) != 0)
				{
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

void BfModule::GetDelegateTypeRefAttributes(BfDelegateTypeRef* delegateTypeRef, BfCallingConvention& callingConvention)
{
	if (delegateTypeRef->mAttributes == NULL)
		return;

	BfCaptureInfo captureInfo;
	auto customAttributes = GetCustomAttributes(delegateTypeRef->mAttributes, (BfAttributeTargets)(BfAttributeTargets_DelegateTypeRef | BfAttributeTargets_FunctionTypeRef), BfGetCustomAttributesFlags_KeepConstsInModule);
	if (customAttributes != NULL)
	{
		auto linkNameAttr = customAttributes->Get(mCompiler->mCallingConventionAttributeTypeDef);
		if (linkNameAttr != NULL)
		{
			if (linkNameAttr->mCtorArgs.size() == 1)
			{
				auto constant = mBfIRBuilder->GetConstant(linkNameAttr->mCtorArgs[0]);
				if (constant != NULL)
					callingConvention = (BfCallingConvention)constant->mInt32;
			}
		}
		delete customAttributes;
	}
}

BfType* BfModule::ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags, int numGenericArgs)
{
	//BP_ZONE("BfModule::ResolveTypeRef");

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
			return ResolveTypeRef(refTypeRef->mElementType, populateType, resolveFlags, numGenericArgs);
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

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsForeignMethodDef))
		contextTypeInstance = mCurMethodInstance->mMethodInfoEx->mForeignType;

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
			StringView findName;
			if (namedTypeRef != NULL)
				findName = namedTypeRef->mNameNode->ToStringView();
			else
				findName = directStrTypeRef->mTypeName;
			if (findName == "Self")
			{
				BfType* selfType = mCurTypeInstance;
				if (selfType->IsTypeAlias())
					selfType = GetOuterType(selfType);
				if (selfType != NULL)
				{
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
				}
				if (selfType != NULL)
				{
					auto selfTypeInst = selfType->ToTypeInstance();
					if ((selfTypeInst != NULL) && (selfTypeInst->mTypeDef->IsGlobalsContainer()) && ((resolveFlags & BfResolveTypeRefFlag_AllowGlobalsSelf) == 0))
						selfType = NULL;
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
				if (selfType->IsTypeAlias())
					selfType = GetOuterType(selfType);
				if (selfType != NULL)
				{
					resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_FromIndirectSource);
					if (selfType->IsBoxed())
						selfType = selfType->GetUnderlyingType();
					if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
					{
						if ((selfType->IsSpecializedType()) || (selfType->IsUnspecializedTypeVariation()))
							selfType = ResolveTypeDef(selfType->ToTypeInstance()->mTypeDef, populateType);
					}
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
			else if (findName == "SelfOuter")
			{
				BfType* selfType = mCurTypeInstance;
				if (selfType->IsTypeAlias())
					selfType = GetOuterType(selfType);
				if (selfType != NULL)
				{
					resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_FromIndirectSource);
					if (selfType->IsBoxed())
						selfType = selfType->GetUnderlyingType();
					if ((resolveFlags & BfResolveTypeRefFlag_NoResolveGenericParam) != 0)
					{
						if ((selfType->IsSpecializedType()) || (selfType->IsUnspecializedTypeVariation()))
							selfType = ResolveTypeDef(selfType->ToTypeInstance()->mTypeDef, populateType);
					}
					selfType = GetOuterType(selfType->ToTypeInstance());
				}
				if (selfType == NULL)
					Fail("'SelfOuter' type is not usable here", typeRef);
				return ResolveTypeResult(typeRef, selfType, populateType, resolveFlags);
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
							if ((mContext->mCurConstraintState != NULL) && (mContext->mCurConstraintState->mMethodInstance == checkMethodInstance) &&
								(mContext->mCurConstraintState->mMethodGenericArgsOverride != NULL))
							{
								return ResolveTypeResult(typeRef, (*mContext->mCurConstraintState->mMethodGenericArgsOverride)[genericParamIdx], populateType, resolveFlags);
							}

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

			if ((genericCheckTypeInstance != NULL) && (genericCheckTypeInstance->IsGenericTypeInstance()) && (genericParamResult == NULL))
			{
				auto genericTypeInst = (BfTypeInstance*)genericCheckTypeInstance;
				auto* genericParams = &curTypeDef->mGenericParamDefs;

				if (genericTypeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL)
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
							genericParamResult = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[genericParamIdx];
							if ((genericParamResult != NULL) &&
								(genericParamResult->IsConstExprValue()) &&
								((resolveFlags & BfResolveTypeRefFlag_AllowGenericTypeParamConstValue) == 0))
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
		typeDef = FindTypeDef(typeRef, contextTypeInstance, &error, 0, resolveFlags);

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
			auto staticSearch = GetStaticSearch();
			if (staticSearch != NULL)
			{
				for (auto staticTypeInst : staticSearch->mStaticTypes)
				{
					auto resolvedType = ResolveInnerType(staticTypeInst, typeRef, populateType, true);
					if (resolvedType != NULL)
					{
						if (mCurTypeInstance != NULL)
							AddDependency(staticTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);
						return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
					}
				}
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
				TypeRefNotFound(typeRef, ((resolveFlags & Beefy::BfResolveTypeRefFlag_Attribute) != 0) ? "Attribute" : NULL);
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
				if ((resolveFlags & BfResolveTypeRefFlag_Attribute) != 0)
					findName += "Attribute";
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
				int wantNumGenericArgs = numGenericArgs;
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

				BfTypeLookupError lookupError;
				auto typeDef = FindTypeDef(findName, wantNumGenericArgs, NULL, &lookupError, resolveFlags);
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
								auto genericParam = ResolveTypeRef(genericParamTypeRef, NULL, BfPopulateType_Declaration);
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

			auto leftPopulateType = BfPopulateType_Identity;
			if ((resolveFlags & BfResolveTypeRefFlag_AllowUnboundGeneric) == 0)
			{
				// We can't just pass 'Identity' here because it won't validate a generic type ref on the left
				leftPopulateType = BfPopulateType_Declaration;
			}

			leftType = ResolveTypeRef(qualifiedTypeRef->mLeft, leftPopulateType,
				(BfResolveTypeRefFlags)((resolveFlags | BfResolveTypeRefFlag_IgnoreLookupError) & ~BfResolveTypeRefFlag_Attribute)); // We throw an error below if we can't find the type
		}

		if (leftType == NULL)
		{
			mIgnoreErrors = prevIgnoreErrors.mPrevVal;
			BfTypeReference* errorRefNode = qualifiedTypeRef->mLeft;
			if ((leftIsValid) && (mCurTypeInstance != NULL) && (mSystem->ContainsNamespace(leftComposite, curTypeDef->mProject)))
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

		if (leftType->IsGenericParam())
		{
			auto genericParam = GetGenericParamInstance((BfGenericParamType*)leftType);
			if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
				return ResolveTypeResult(typeRef, GetPrimitiveType(BfTypeCode_Var), populateType, resolveFlags);
			if ((genericParam->IsEnum()) && (qualifiedTypeRef->mRight != NULL))
			{
				StringView findNameRight = qualifiedTypeRef->mRight->ToStringView();
				if (findNameRight == "UnderlyingType")
					return ResolveTypeResult(typeRef, GetPrimitiveType(BfTypeCode_Var), populateType, resolveFlags);
			}
		}

		auto resolvedType = ResolveInnerType(leftType, qualifiedTypeRef->mRight, populateType, false, numGenericArgs, resolveFlags);
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

	if (auto retTypeTypeRef = BfNodeDynCastExact<BfModifiedTypeRef>(typeRef))
	{
		if (retTypeTypeRef->mRetTypeToken->mToken == BfToken_RetType)
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
							if ((resolvedType != NULL) && (resolvedType->IsRef()))
								resolvedType = resolvedType->GetUnderlyingType();
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
								else if ((genericParamInstance->mTypeConstraint->IsInstanceOf(mCompiler->mDelegateTypeDef)) ||
									(genericParamInstance->mTypeConstraint->IsInstanceOf(mCompiler->mFunctionTypeDef)))
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
						if ((resolvedType != NULL) && (resolvedType->IsRef()))
							resolvedType = resolvedType->GetUnderlyingType();
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
		else if (retTypeTypeRef->mRetTypeToken->mToken == BfToken_AllocType)
		{
			BfType* resolvedType = NULL;
			if (retTypeTypeRef->mElementType != NULL)
			{
				resolvedType = ResolveTypeRef(retTypeTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
				if (resolvedType != NULL)
				{
					if (resolvedType->IsGenericParam())
					{
						auto genericParam = GetGenericParamInstance((BfGenericParamType*)resolvedType);
						if (((genericParam->mTypeConstraint != NULL) && (genericParam->mTypeConstraint->IsValueType())) ||
							((genericParam->mGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Enum)) != 0))
						{
							resolvedType = CreatePointerType(resolvedType);
						}
						else if (((genericParam->mTypeConstraint != NULL) && (!genericParam->mTypeConstraint->IsValueType())) ||
							((genericParam->mGenericParamFlags & (BfGenericParamFlag_Class)) != 0))
						{
							// Leave as 'T'
						}
						else
							resolvedType = CreateModifiedTypeType(resolvedType, BfToken_AllocType);
					}
					else if (resolvedType->IsValueType())
						resolvedType = CreatePointerType(resolvedType);
				}
			}

			return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
		}
		else if (retTypeTypeRef->mRetTypeToken->mToken == BfToken_Nullable)
		{
			bool allowThrough = false;
			BfType* resolvedType = NULL;
			if (retTypeTypeRef->mElementType != NULL)
			{
				resolvedType = ResolveTypeRef(retTypeTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
			}

			if ((resolvedType != NULL) && (resolvedType->IsGenericParam()))
			{
				//resolvedType = CreateModifiedTypeType(resolvedType, BfToken_Nullable);
				BfTypeVector typeVec;
				typeVec.push_back(resolvedType);
				resolvedType = ResolveTypeDef(mCompiler->mNullableTypeDef, typeVec, BfPopulateType_Declaration);
			}
			else if (resolvedType != NULL)
			{
				if (resolvedType->IsValueType())
				{
					if (InDefinitionSection())
						Warn(0, StrFormat("Consider using '%s?' instead of nullable modifier", TypeToString(resolvedType).c_str()), retTypeTypeRef);

					BfTypeVector typeVec;
					typeVec.push_back(resolvedType);
					resolvedType = ResolveTypeDef(mCompiler->mNullableTypeDef, typeVec, BfPopulateType_Declaration);
				}
				else
				{
					if (InDefinitionSection())
						Warn(0, StrFormat("Unneeded nullable modifier, %s is already nullable", TypeToString(resolvedType).c_str()), retTypeTypeRef->mRetTypeToken);
				}
			}
			if (resolvedType != NULL)
				PopulateType(resolvedType, populateType);

			return resolvedType;
		}
		else
			BFMODULE_FATAL(this, "Unhandled");
	}

	if (auto refTypeRef = BfNodeDynCastExact<BfRefTypeRef>(typeRef))
	{
		if ((refTypeRef->mRefToken != NULL) && (refTypeRef->mRefToken->GetToken() == BfToken_Mut) && (refTypeRef->mElementType != NULL))
		{
			bool needsRefWrap = false;

 			auto resolvedType = ResolveTypeRef(refTypeRef->mElementType, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
 			if (resolvedType != NULL)
 			{
				if ((resolvedType->IsValueType()) || (resolvedType->IsGenericParam()))
					needsRefWrap = true;

				if ((InDefinitionSection()) && (!resolvedType->IsGenericParam()) && ((resolveFlags & BfResolveTypeRefFlag_NoWarnOnMut) == 0))
				{
					if (!resolvedType->IsValueType())
						Warn(0, StrFormat("Specified 'mut' has no effect on '%s' since reference types are always mutable", TypeToString(resolvedType).c_str()), refTypeRef->mRefToken);
					else
						Warn(0, "Use 'mut' for generic arguments which may or may not be reference types. Consider using 'ref' here, instead.", refTypeRef->mRefToken);
				}
 			}

			if (!needsRefWrap)
			{
				// Non-composites (including pointers) don't actually need ref-wrapping for 'mut'
				return ResolveTypeResult(typeRef, resolvedType, populateType, resolveFlags);
			}
		}
	}

	static int sCallIdx = 0;
	int callIdx = sCallIdx++;
	if (callIdx == 0x00006CA4)
	{
		NOP;
	}

	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mResolveFlags = (BfResolveTypeRefFlags)(resolveFlags &
		(BfResolveTypeRefFlag_NoCreate | BfResolveTypeRefFlag_IgnoreLookupError | BfResolveTypeRefFlag_DisallowComptime |
			BfResolveTypeRefFlag_AllowInferredSizedArray | BfResolveTypeRefFlag_Attribute | BfResolveTypeRefFlag_AllowUnboundGeneric |
			BfResolveTypeRefFlag_ForceUnboundGeneric | BfResolveTypeRefFlag_AllowGenericParamConstValue |
			BfResolveTypeRefFlag_AllowImplicitConstExpr));
	lookupCtx.mRootTypeRef = typeRef;
	lookupCtx.mRootTypeDef = typeDef;
	lookupCtx.mModule = this;
	BfResolvedTypeSet::EntryRef resolvedEntry;
	if (auto delegateTypeRef = BfNodeDynCastExact<BfDelegateTypeRef>(typeRef))
		GetDelegateTypeRefAttributes(delegateTypeRef, lookupCtx.mCallingConvention);

	auto inserted = mContext->mResolvedTypes.Insert(typeRef, &lookupCtx, &resolvedEntry);

	if (!resolvedEntry)
	{
		if (lookupCtx.mHadVar)
			return ResolveTypeResult(typeRef, GetPrimitiveType(BfTypeCode_Var), populateType, resolveFlags);
		return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
	}

	if (!inserted)
	{
		BF_ASSERT(resolvedEntry->mValue != NULL);
		BF_ASSERT(!resolvedEntry->mValue->IsDeleting());
		return ResolveTypeResult(typeRef, resolvedEntry->mValue, populateType, resolveFlags);
	}

	if ((lookupCtx.mIsUnboundGeneric) && (lookupCtx.mRootTypeDef != NULL))
	{
		mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
		return ResolveTypeResult(typeRef, ResolveTypeDef(lookupCtx.mRootTypeDef), populateType, resolveFlags);
	}

	if ((resolveFlags & BfResolveTypeRefFlag_NoCreate) != 0)
	{
		mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
		return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
	}

	BfModule* populateModule = this;
	if ((resolveFlags & BfResolveTypeRefFlag_NoReify) != 0)
		populateModule = mContext->mUnreifiedModule;

	if (typeRef->IsTypeDefTypeReference())
	{
		//BF_ASSERT(typeDefTypeRef->mTypeDef != NULL); // Resolved higher up
		//auto typeDef = typeDefTypeRef->mTypeDef;
		if ((typeDef->mTypeCode >= BfTypeCode_None) && (typeDef->mTypeCode <= BfTypeCode_Double))
		{
			BfPrimitiveType* primType = new BfPrimitiveType();
			primType->mTypeDef = typeDef;
			resolvedEntry->mValue = primType;
			BF_ASSERT(BfResolvedTypeSet::Hash(primType, &lookupCtx, false) == resolvedEntry->mHashCode);
			populateModule->InitType(primType, populateType);
			return ResolveTypeResult(typeRef, primType, populateType, resolveFlags);
		}

		BfTypeInstance* outerTypeInstance = lookupCtx.mRootOuterTypeInstance;
		if (outerTypeInstance == NULL)
			outerTypeInstance = mCurTypeInstance;
		if ((outerTypeInstance != NULL) && (typeDef->mGenericParamDefs.size() != 0))
		{
			// Try to inherit generic params from current parent

			BfTypeDef* outerType = mSystem->GetCombinedPartial(typeDef->mOuterType);
			BF_ASSERT(!outerType->mIsPartial);
			if (TypeHasParentOrEquals(outerTypeInstance->mTypeDef, outerType))
			{
				BfType* checkCurType = outerTypeInstance;
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

				auto parentGenericTypeInstance = (BfTypeInstance*)checkCurType;

				BfTypeInstance* genericTypeInst;
				if (typeDef->mTypeCode == BfTypeCode_TypeAlias)
				{
					auto typeAliasType = new BfTypeAliasType();
					genericTypeInst = typeAliasType;
				}
				else
					genericTypeInst = new BfTypeInstance();
				genericTypeInst->mGenericTypeInfo = new BfGenericTypeInfo();
				genericTypeInst->mTypeDef = typeDef;

				if (parentGenericTypeInstance->mGenericTypeInfo->mGenericParams.IsEmpty())
					PopulateType(parentGenericTypeInstance, BfPopulateType_Declaration);

				for (int i = 0; i < numParentGenericParams; i++)
				{
					genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(parentGenericTypeInstance->mGenericTypeInfo->mGenericParams[i]->AddRef());
					genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.push_back(parentGenericTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
				}

				CheckUnspecializedGenericType(genericTypeInst, populateType);
				resolvedEntry->mValue = genericTypeInst;
				populateModule->InitType(genericTypeInst, populateType);
#ifdef _DEBUG
				if (BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) != resolvedEntry->mHashCode)
				{
					int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
					int typeHash = BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx);
					BF_ASSERT(refHash == typeHash);
				}
#endif
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

		if (((resolveFlags & BfResolveTypeRefFlag_NoReify) != 0) && (mCompiler->mOptions.mCompileOnDemandKind != BfCompileOnDemandKind_AlwaysInclude))
		{
			typeInst->mIsReified = false;
		}

		if (typeInst->mTypeDef->mGenericParamDefs.size() != 0)
		{
			Fail("Generic type arguments expected", typeRef);
			delete typeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}
		resolvedEntry->mValue = typeInst;

#ifdef _DEBUG
		int typeRefash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
#endif

		populateModule->InitType(typeInst, populateType);

		if (BfResolvedTypeSet::Hash(typeInst, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
			int typeHash = BfResolvedTypeSet::Hash(typeInst, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
		}
		{
			BF_ASSERT(BfResolvedTypeSet::Hash(typeInst, &lookupCtx) == resolvedEntry->mHashCode);
		}
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
		auto arrayTypeDef = mCompiler->GetArrayTypeDef(arrayTypeRef->mDimensions);
		if ((elementType == NULL) || (arrayTypeDef == NULL))
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
				BfType* intType = GetPrimitiveType(BfTypeCode_IntPtr);
				BfTypedValue typedVal;
				lookupCtx.mResolvedValueMap.TryGetValue(sizeExpr, &typedVal);
				if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
				{
					BfUnknownSizedArrayType* arrayType = new BfUnknownSizedArrayType();
					arrayType->mContext = mContext;
					arrayType->mElementType = elementType;
					arrayType->mElementCount = -1;
					arrayType->mElementCountSource = typedVal.mType;
					resolvedEntry->mValue = arrayType;

					BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHashCode);
					populateModule->InitType(arrayType, populateType);
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
							elementCount = (intptr)constant->mInt64;
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
			arrayType->mGenericDepth = elementType->GetGenericDepth() + 1;
			resolvedEntry->mValue = arrayType;

			BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHashCode);
			populateModule->InitType(arrayType, populateType);
			return ResolveTypeResult(typeRef, arrayType, populateType, resolveFlags);
		}

		BfArrayType* arrayType = new BfArrayType();
		arrayType->mGenericTypeInfo = new BfGenericTypeInfo();
		arrayType->mContext = mContext;
		arrayType->mDimensions = arrayTypeRef->mDimensions;
		arrayType->mTypeDef = arrayTypeDef;
		arrayType->mGenericTypeInfo->mTypeGenericArguments.push_back(elementType);
		resolvedEntry->mValue = arrayType;

		CheckUnspecializedGenericType(arrayType, populateType);

		BF_ASSERT(BfResolvedTypeSet::Hash(arrayType, &lookupCtx) == resolvedEntry->mHashCode);
		populateModule->InitType(arrayType, populateType);
		return ResolveTypeResult(typeRef, arrayType, populateType, resolveFlags);
	}
	else if (auto genericTypeInstRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
	{
		BfTypeReference* outerTypeRef = NULL;

		Array<BfAstNode*> genericArguments;

		BfTypeReference* checkTypeRef = genericTypeInstRef;
		int checkIdx = 0;

		while (checkTypeRef != NULL)
		{
			checkIdx++;
			if (checkIdx >= 3)
			{
				outerTypeRef = checkTypeRef;
				break;
			}

			if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(checkTypeRef))
			{
				for (auto genericArg : genericTypeRef->mGenericArguments)
					genericArguments.push_back(genericArg);
				checkTypeRef = genericTypeRef->mElementType;
				continue;
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

		BfTypeVector genericArgs;

		BfType* type = NULL;
		BfTypeDef* typeDef = ResolveGenericInstanceDef(genericTypeInstRef, &type, resolveFlags);
		if (typeDef == NULL)
		{
			Fail("Unable to resolve type", typeRef);
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		BfTypeInstance* outerTypeInstance = NULL;
		BfTypeDef* commonOuterType = NULL;
		int startDefGenericParamIdx = 0;

		if (outerTypeRef != NULL)
		{
			BfType* outerType = lookupCtx.GetCachedResolvedType(outerTypeRef);
			if (outerType != NULL)
			{
				outerTypeInstance = outerType->ToTypeInstance();
				commonOuterType = outerTypeInstance->mTypeDef;
			}
		}
		else
		{
			outerTypeInstance = mCurTypeInstance;
			auto outerType = typeDef->mOuterType;
			commonOuterType = BfResolvedTypeSet::FindRootCommonOuterType(outerType, &lookupCtx, outerTypeInstance);
		}

		if ((commonOuterType) && (outerTypeInstance->IsGenericTypeInstance()))
		{
			startDefGenericParamIdx = (int)commonOuterType->mGenericParamDefs.size();
			auto parentTypeInstance = outerTypeInstance;
			if (parentTypeInstance->IsTypeAlias())
				parentTypeInstance = (BfTypeInstance*)GetOuterType(parentTypeInstance)->ToTypeInstance();
			for (int i = 0; i < startDefGenericParamIdx; i++)
				genericArgs.push_back(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
		}

		for (auto genericArgRef : genericArguments)
		{
			BfType* genericArg = NULL;
			lookupCtx.mResolvedTypeMap.TryGetValue(genericArgRef, &genericArg);
			if (genericArg == NULL)
				genericArg = ResolveTypeRef(genericArgRef, NULL, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_AllowGenericTypeParamConstValue | BfResolveTypeRefFlag_AllowGenericMethodParamConstValue));
			if ((genericArg == NULL) || (genericArg->IsVar()))
			{
				mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
				return ResolveTypeResult(typeRef, ((genericArg != NULL) && (genericArg->IsVar())) ? genericArg : NULL, populateType, resolveFlags);
			}
			genericArgs.Add(genericArg);
		}

		BfTypeInstance* genericTypeInst;
		if ((type != NULL) &&
			((type->IsDelegateFromTypeRef()) || (type->IsFunctionFromTypeRef())))
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveGenericType(type, &genericArgs, NULL, mCurTypeInstance);
		}
		else if ((type != NULL) && (type->IsTuple()))
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveGenericType(type, &genericArgs, NULL, mCurTypeInstance);
		}
		else if ((typeDef != NULL) && (typeDef->mTypeCode == BfTypeCode_TypeAlias))
		{
			auto typeAliasType = new BfTypeAliasType();
			genericTypeInst = typeAliasType;
		}
		else
			genericTypeInst = new BfTypeInstance();
		genericTypeInst->mContext = mContext;
		genericTypeInst->mGenericTypeInfo = new BfGenericTypeInfo();

		BF_ASSERT(typeDef->mDefState != BfTypeDef::DefState_Deleted);

		int genericParamCount = (int)typeDef->mGenericParamDefs.size();
		if ((type != NULL) && (type->IsGenericTypeInstance()))
		{
			// Is a generic type for sure...
			// We need this case for generic methods
			genericParamCount = (int)((BfTypeInstance*)type)->mGenericTypeInfo->mTypeGenericArguments.size();
		}
		else if (typeDef->mGenericParamDefs.size() == 0)
		{
			Fail("Not a generic type", typeRef);
			delete genericTypeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		genericTypeInst->mTypeDef = typeDef;

		if (commonOuterType != NULL)
		{
			auto parentTypeInstance = outerTypeInstance;
			if ((parentTypeInstance != NULL) && (parentTypeInstance->IsTypeAlias()))
				parentTypeInstance = (BfTypeInstance*)GetOuterType(parentTypeInstance)->ToTypeInstance();
			if (parentTypeInstance->mDefineState < BfTypeDefineState_Declared)
				PopulateType(parentTypeInstance, BfPopulateType_Declaration);
			if ((parentTypeInstance != NULL) && (parentTypeInstance->IsGenericTypeInstance()))
			{
				genericTypeInst->mGenericTypeInfo->mMaxGenericDepth = BF_MAX(genericTypeInst->mGenericTypeInfo->mMaxGenericDepth, parentTypeInstance->mGenericTypeInfo->mMaxGenericDepth);
				for (int i = 0; i < startDefGenericParamIdx; i++)
				{
					genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(parentTypeInstance->mGenericTypeInfo->mGenericParams[i]->AddRef());
					genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.push_back(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
					auto typeGenericArg = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[i];
					genericTypeInst->mGenericTypeInfo->mIsUnspecialized |= typeGenericArg->IsGenericParam() || typeGenericArg->IsUnspecializedType();
				}
			}
		}

		int wantedGenericParams = genericParamCount - startDefGenericParamIdx;
		int genericArgDiffCount = (int)genericArguments.size() - wantedGenericParams;
		if (genericArgDiffCount != 0)
		{
			int innerWantedGenericParams = genericParamCount;
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
			auto genericArg = genericArgs[genericParamIdx + startDefGenericParamIdx];

			genericTypeInst->mGenericTypeInfo->mMaxGenericDepth = BF_MAX(genericTypeInst->mGenericTypeInfo->mMaxGenericDepth, genericArg->GetGenericDepth() + 1);
			genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.push_back(genericArg);

			genericParamIdx++;
		}

		if (genericTypeInst->mGenericTypeInfo->mMaxGenericDepth > 64)
		{
			Fail("Maximum generic depth exceeded", typeRef);
			delete genericTypeInst;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		resolvedEntry->mValue = genericTypeInst;

		CheckUnspecializedGenericType(genericTypeInst, populateType);
		populateModule->InitType(genericTypeInst, populateType);

#ifdef _DEBUG
		if (BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
			int typeHash = BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
			BF_ASSERT(refHash == resolvedEntry->mHashCode);
		}
		if (!BfResolvedTypeSet::Equals(genericTypeInst, typeRef, &lookupCtx))
		{
			BF_ASSERT(BfResolvedTypeSet::Equals(genericTypeInst, typeRef, &lookupCtx));
		}

		BfLogSysM("Generic type %p typeHash: %8X\n", genericTypeInst, resolvedEntry->mHashCode);
#endif

		BF_ASSERT(BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) == resolvedEntry->mHashCode);
		return ResolveTypeResult(typeRef, genericTypeInst, populateType, resolveFlags);
	}
	else if (auto tupleTypeRef = BfNodeDynCast<BfTupleTypeRef>(typeRef))
	{
		Array<BfType*> types;
		Array<String> names;

		bool wantGeneric = false;
		bool isUnspecialized = false;

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

			if (type->IsTypeGenericParam())
				wantGeneric = true;
			if (type->IsUnspecializedType())
				isUnspecialized = true;
			if (type->IsVar())
			{
				mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
				return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
			}
			types.push_back(type);
			names.push_back(fieldName);
		}

		if ((mCurTypeInstance == NULL) || (!mCurTypeInstance->IsGenericTypeInstance()))
			wantGeneric = false;

		//TODO:
		wantGeneric = false;

		auto baseType = (BfTypeInstance*)ResolveTypeDef(mContext->mCompiler->mValueTypeTypeDef, BfPopulateType_Identity);
		BfTupleType* tupleType = NULL;
		if (wantGeneric)
		{
			BfTupleType* actualTupleType = new BfTupleType();
			actualTupleType->mGenericTypeInfo = new BfGenericTypeInfo();
			actualTupleType->mGenericTypeInfo->mFinishedGenericParams = true;
			actualTupleType->Init(baseType->mTypeDef->mProject, baseType);
			for (int fieldIdx = 0; fieldIdx < (int)types.size(); fieldIdx++)
			{
				BfFieldDef* fieldDef = actualTupleType->AddField(names[fieldIdx]);
				fieldDef->mProtection = (names[fieldIdx][0] == '_') ? BfProtection_Private : BfProtection_Public;
			}
			actualTupleType->Finish();
			auto parentTypeInstance = (BfTypeInstance*)mCurTypeInstance;
			for (int i = 0; i < parentTypeInstance->mGenericTypeInfo->mGenericParams.size(); i++)
			{
				actualTupleType->mGenericTypeInfo->mGenericParams.push_back(parentTypeInstance->mGenericTypeInfo->mGenericParams[i]->AddRef());
			}

			for (int i = 0; i < parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments.size(); i++)
			{
				actualTupleType->mGenericTypeInfo->mTypeGenericArguments.push_back(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
				auto typeGenericArg = actualTupleType->mGenericTypeInfo->mTypeGenericArguments[i];
				actualTupleType->mGenericTypeInfo->mIsUnspecialized |= typeGenericArg->IsGenericParam() || typeGenericArg->IsUnspecializedType();
			}

			CheckUnspecializedGenericType(actualTupleType, populateType);
			if (isUnspecialized)
			{
				actualTupleType->mGenericTypeInfo->mIsUnspecialized = true;
				actualTupleType->mGenericTypeInfo->mIsUnspecializedVariation = true;
			}
			actualTupleType->mIsUnspecializedType = actualTupleType->mGenericTypeInfo->mIsUnspecialized;
			actualTupleType->mIsUnspecializedTypeVariation = actualTupleType->mGenericTypeInfo->mIsUnspecializedVariation;
			tupleType = actualTupleType;
		}
		else
		{
			BfTupleType* actualTupleType = new BfTupleType();
			actualTupleType->Init(baseType->mTypeDef->mProject, baseType);
			for (int fieldIdx = 0; fieldIdx < (int)types.size(); fieldIdx++)
			{
				BfFieldDef* fieldDef = actualTupleType->AddField(names[fieldIdx]);
				fieldDef->mProtection = (names[fieldIdx][0] == '_') ? BfProtection_Private : BfProtection_Public;
			}
			actualTupleType->Finish();
			tupleType = actualTupleType;
			actualTupleType->mIsUnspecializedType = isUnspecialized;
			actualTupleType->mIsUnspecializedTypeVariation = isUnspecialized;
		}

		tupleType->mFieldInstances.Resize(types.size());
		for (int fieldIdx = 0; fieldIdx < (int)types.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			fieldInstance->mFieldIdx = fieldIdx;
			fieldInstance->SetResolvedType(types[fieldIdx]);
			BF_ASSERT(!types[fieldIdx]->IsVar());
			fieldInstance->mOwner = tupleType;
			tupleType->mGenericDepth = BF_MAX(tupleType->mGenericDepth, fieldInstance->mResolvedType->GetGenericDepth() + 1);
		}

		resolvedEntry->mValue = tupleType;
		BF_ASSERT(BfResolvedTypeSet::Hash(tupleType, &lookupCtx) == resolvedEntry->mHashCode);
		populateModule->InitType(tupleType, populateType);

		return ResolveTypeResult(typeRef, tupleType, populateType, resolveFlags);
	}
	else if (auto nullableTypeRef = BfNodeDynCast<BfNullableTypeRef>(typeRef))
	{
		BfTypeReference* elementTypeRef = nullableTypeRef->mElementType;
		auto typeDef = mCompiler->mNullableTypeDef;

		auto elementType = ResolveTypeRef(elementTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if ((elementType == NULL) || (elementType->IsVar()))
		{
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, elementType, populateType, resolveFlags);
		}

		BfTypeInstance* genericTypeInst = new BfTypeInstance();
		genericTypeInst->mGenericTypeInfo = new BfGenericTypeInfo();
		genericTypeInst->mContext = mContext;
		genericTypeInst->mTypeDef = typeDef;

		auto genericParamInstance = new BfGenericTypeParamInstance(typeDef, 0);
		genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(genericParamInstance);
		genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.push_back(elementType);
		//genericTypeInst->mIsUnspecialized = elementType->IsGenericParam() || elementType->IsUnspecializedType();

		CheckUnspecializedGenericType(genericTypeInst, populateType);

		resolvedEntry->mValue = genericTypeInst;
#ifdef _DEBUG
		if (BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
			int typeHash = BfResolvedTypeSet::Hash(genericTypeInst, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
		}
#endif
		populateModule->InitType(genericTypeInst, populateType);
		return ResolveTypeResult(typeRef, genericTypeInst, populateType, resolveFlags);
	}
	else if (auto pointerTypeRef = BfNodeDynCast<BfPointerTypeRef>(typeRef))
	{
		BfPointerType* pointerType = new BfPointerType();
		auto elementType = ResolveTypeRef(pointerTypeRef->mElementType, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if ((elementType == NULL) || (elementType->IsVar()))
		{
			delete pointerType;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, elementType, populateType, resolveFlags);
		}

		pointerType->mGenericDepth = elementType->GetGenericDepth() + 1;
		pointerType->mElementType = elementType;
		pointerType->mContext = mContext;
		resolvedEntry->mValue = pointerType;

		//int hashVal = mContext->mResolvedTypes.Hash(typeRef, &lookupCtx);
		BF_ASSERT(BfResolvedTypeSet::Hash(pointerType, &lookupCtx) == resolvedEntry->mHashCode);

		populateModule->InitType(pointerType, populateType);
		return ResolveTypeResult(typeRef, pointerType, populateType, resolveFlags);
	}
	else if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(typeRef))
	{
		BfRefType* refType = new BfRefType();
		refType->mRefKind = BfRefType::RefKind_Ref;
		if (refTypeRef->mRefToken == NULL)
			refType->mRefKind = BfRefType::RefKind_Ref;
		else if (refTypeRef->mRefToken->GetToken() == BfToken_In)
			refType->mRefKind = BfRefType::RefKind_In;
		else if (refTypeRef->mRefToken->GetToken() == BfToken_Out)
			refType->mRefKind = BfRefType::RefKind_Out;
		else if (refTypeRef->mRefToken->GetToken() == BfToken_Mut)
			refType->mRefKind = BfRefType::RefKind_Mut;
		auto elementType = ResolveTypeRef(refTypeRef->mElementType, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		if ((elementType == NULL) || (elementType->IsVar()))
		{
			delete refType;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, elementType, populateType, resolveFlags);
		}

		refType->mElementType = elementType;
		resolvedEntry->mValue = refType;

#ifdef _DEBUG
		if (BfResolvedTypeSet::Hash(refType, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx, BfResolvedTypeSet::BfHashFlag_AllowRef);
			int typeHash = BfResolvedTypeSet::Hash(refType, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
		}
		BF_ASSERT(BfResolvedTypeSet::Equals(refType, typeRef, &lookupCtx));
#endif
		populateModule->InitType(refType, populateType);
		return ResolveTypeResult(typeRef, refType, populateType, resolveFlags);
	}
	else if (auto delegateTypeRef = BfNodeDynCast<BfDelegateTypeRef>(typeRef))
	{
		bool wantGeneric = false;
		bool isUnspecialized = false;
		auto _CheckType = [&](BfType* type)
		{
			if (type->IsTypeGenericParam())
				wantGeneric = true;
			if (type->IsUnspecializedType())
				isUnspecialized = true;
		};

		bool failed = false;

		auto returnType = ResolveTypeRef(delegateTypeRef->mReturnType, NULL, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowRef);
		if (returnType == NULL)
		{
			failed = true;
			returnType = GetPrimitiveType(BfTypeCode_Var);
		}
		_CheckType(returnType);

		BfType* functionThisType = NULL;
		bool hasMutSpecifier = false;
		bool isFirst = true;
		bool isDelegate = delegateTypeRef->mTypeToken->GetToken() == BfToken_Delegate;
		bool hasVarArgs = false;

		Array<BfType*> paramTypes;
		for (int paramIdx = 0; paramIdx < delegateTypeRef->mParams.size(); paramIdx++)
		{
			auto param = delegateTypeRef->mParams[paramIdx];
			BfResolveTypeRefFlags resolveTypeFlags = BfResolveTypeRefFlag_AllowRef;
			if ((param->mNameNode != NULL) && (param->mNameNode->Equals("this")))
				resolveTypeFlags = (BfResolveTypeRefFlags)(resolveTypeFlags | BfResolveTypeRefFlag_NoWarnOnMut);

			if (paramIdx == delegateTypeRef->mParams.size() - 1)
			{
				if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(param->mTypeRef))
				{
					if (dotTypeRef->mDotToken->mToken == BfToken_DotDotDot)
					{
						hasVarArgs = true;
						continue;
					}
				}
			}

			auto paramType = ResolveTypeRef(param->mTypeRef, BfPopulateType_Declaration, resolveTypeFlags);
			if (paramType == NULL)
			{
				failed = true;
				paramType = GetPrimitiveType(BfTypeCode_Var);
			}

			if ((!isDelegate) && (isFirst) && (param->mNameNode != NULL) && (param->mNameNode->Equals("this")))
			{
				functionThisType = paramType;
				if (functionThisType->IsRef())
				{
					auto refType = (BfRefType*)functionThisType;
					if (refType->mRefKind != BfRefType::RefKind_Mut)
					{
						if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(param->mTypeRef))
						{
							failed = true;
							Fail("Only 'mut' is allowed here", refTypeRef->mRefToken);
						}
					}
					hasMutSpecifier = true;
					functionThisType = refType->mElementType;
				}
				paramTypes.Add(functionThisType);
				_CheckType(functionThisType);
			}
			else
			{
				paramTypes.Add(paramType);
				_CheckType(paramType);
			}

			isFirst = false;
		}

		if ((mCurTypeInstance == NULL) || (!mCurTypeInstance->IsGenericTypeInstance()))
			wantGeneric = false;

		//TODO:
		wantGeneric = false;

		BfTypeInstance* baseDelegateType = NULL;
		if (mCompiler->mDelegateTypeDef != NULL)
			baseDelegateType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();
		else
			failed = true;

		BfDelegateInfo* delegateInfo = NULL;
		BfDelegateType* delegateType = NULL;
		if (wantGeneric)
		{
			BfDelegateType* genericTypeInst = new BfDelegateType();
			genericTypeInst->mGenericTypeInfo = new BfGenericTypeInfo();
			genericTypeInst->mGenericTypeInfo->mFinishedGenericParams = true;
			delegateType = genericTypeInst;
			delegateInfo = delegateType->GetDelegateInfo();
			auto parentTypeInstance = (BfTypeInstance*)mCurTypeInstance;
			for (int i = 0; i < parentTypeInstance->mGenericTypeInfo->mGenericParams.size(); i++)
			{
				genericTypeInst->mGenericTypeInfo->mGenericParams.push_back(parentTypeInstance->mGenericTypeInfo->mGenericParams[i]->AddRef());
			}
			for (int i = 0; i < parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments.size(); i++)
			{
				genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.push_back(parentTypeInstance->mGenericTypeInfo->mTypeGenericArguments[i]);
				auto typeGenericArg = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[i];
				genericTypeInst->mGenericTypeInfo->mIsUnspecialized |= typeGenericArg->IsGenericParam() || typeGenericArg->IsUnspecializedType();
			}

			CheckUnspecializedGenericType(genericTypeInst, populateType);

			// We don't ever need to do an actual pass over generic delegate methods, so it's safe to set the 'unspecialized variation' flag
			if (isUnspecialized)
			{
				genericTypeInst->mGenericTypeInfo->mIsUnspecialized = true;
				genericTypeInst->mGenericTypeInfo->mIsUnspecializedVariation = true;
			}

			genericTypeInst->mIsUnspecializedType = genericTypeInst->mGenericTypeInfo->mIsUnspecialized;
			genericTypeInst->mIsUnspecializedTypeVariation = genericTypeInst->mGenericTypeInfo->mIsUnspecializedVariation;
		}
		else
		{
			auto dlgType = new BfDelegateType();
			delegateInfo = dlgType->GetDelegateInfo();
			dlgType->mIsUnspecializedType = isUnspecialized;
			dlgType->mIsUnspecializedTypeVariation = isUnspecialized;
			delegateType = dlgType;
		}

		delegateInfo->mCallingConvention = lookupCtx.mCallingConvention;

		Val128 hashContext;

		BfTypeDef* typeDef = new BfTypeDef();
		if (baseDelegateType != NULL)
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
		methodDef->mIsStatic = !typeDef->mIsDelegate && (functionThisType == NULL);
		methodDef->mHasExplicitThis = functionThisType != NULL;

		if ((functionThisType != NULL) && (hasMutSpecifier))
		{
			if ((functionThisType->IsValueType()) || (functionThisType->IsGenericParam()))
				methodDef->mIsMutating = true;
		}

		auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
		if (typeDef->mIsDelegate)
			directTypeRef->Init(delegateType);
		else if (mCompiler->mFunctionTypeDef == NULL)
			failed = true;
		else
			directTypeRef->Init(ResolveTypeDef(mCompiler->mFunctionTypeDef));
		if (!failed)
			typeDef->mBaseTypes.push_back(directTypeRef);

		directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
		delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
		directTypeRef->Init(returnType);
		methodDef->mReturnTypeRef = directTypeRef;
		delegateInfo->mReturnType = returnType;
		delegateInfo->mHasExplicitThis = functionThisType != NULL;
		delegateInfo->mHasVarArgs = hasVarArgs;

		delegateType->mGenericDepth = BF_MAX(delegateType->mGenericDepth, returnType->GetGenericDepth() + 1);

		auto hashVal = mContext->mResolvedTypes.Hash(typeRef, &lookupCtx);

		//int paramSrcOfs = (functionThisType != NULL) ? 1 : 0;
		int paramSrcOfs = 0;
		for (int paramIdx = 0; paramIdx < (int)paramTypes.size(); paramIdx++)
		{
			auto param = delegateTypeRef->mParams[paramIdx + paramSrcOfs];
			auto paramType = paramTypes[paramIdx];
			if (paramType == NULL)
				paramType = GetPrimitiveType(BfTypeCode_Var);

			String paramName;
			if (param->mNameNode != NULL)
				paramName = param->mNameNode->ToString();

			if (!paramType->IsReified())
				delegateType->mIsReified = false;

			auto directTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeReference>();
			delegateInfo->mDirectAllocNodes.push_back(directTypeRef);
			directTypeRef->Init(paramType);

			BfParameterDef* paramDef = new BfParameterDef();

			paramDef->mTypeRef = directTypeRef;
			paramDef->mName = paramName;
			if ((paramIdx == 0) && (functionThisType != NULL))
				paramDef->mParamKind = BfParamKind_ExplicitThis;
			methodDef->mParams.push_back(paramDef);
			if ((param->mModToken != NULL) && (param->mModToken->mToken == BfToken_Params))
			{
				if (paramIdx == (int)paramTypes.size() - 1)
					paramDef->mParamKind = BfParamKind_Params;
				else
				{
					failed = true;
					Fail("Params parameter must be the last parameter", param);
				}
			}

			if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(paramDef->mTypeRef))
			{
				if (dotTypeRef->mDotToken->mToken == BfToken_DotDotDot)
				{
					if (paramIdx == (int)paramTypes.size() - 1)
						paramDef->mParamKind = BfParamKind_VarArgs;
					else
					{
						failed = true;
						Fail("Varargs specifier must be the last parameter", param);
					}
				}
			}

			delegateInfo->mParams.Add(paramType);

			delegateType->mGenericDepth = BF_MAX(delegateType->mGenericDepth, paramType->GetGenericDepth() + 1);
		}

		if (delegateInfo->mHasVarArgs)
		{
			BfParameterDef* paramDef = new BfParameterDef();
			paramDef->mParamKind = BfParamKind_VarArgs;
			methodDef->mParams.push_back(paramDef);
		}

		typeDef->mMethods.push_back(methodDef);

		if (failed)
		{
			delete delegateType;
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		//

		if (typeDef->mIsDelegate)
		{
			BfDefBuilder::AddMethod(typeDef, BfMethodType_Ctor, BfProtection_Public, false, "");
			BfDefBuilder::AddDynamicCastMethods(typeDef);
		}

		delegateType->mContext = mContext;
		delegateType->mTypeDef = typeDef;
		populateModule->InitType(delegateType, populateType);
		resolvedEntry->mValue = delegateType;

		AddDependency(directTypeRef->mType, delegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
// 		if (delegateInfo->mFunctionThisType != NULL)
// 			AddDependency(delegateInfo->mFunctionThisType, delegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		for (auto paramType : paramTypes)
			AddDependency(paramType, delegateType, BfDependencyMap::DependencyFlag_ParamOrReturnValue);

#ifdef _DEBUG
		if (BfResolvedTypeSet::Hash(delegateType, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
			int typeHash = BfResolvedTypeSet::Hash(delegateType, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
		}
		BF_ASSERT(BfResolvedTypeSet::Equals(delegateType, typeRef, &lookupCtx));
#endif

		BF_ASSERT(BfResolvedTypeSet::Hash(delegateType, &lookupCtx) == resolvedEntry->mHashCode);

		return ResolveTypeResult(typeRef, delegateType, populateType, resolveFlags);
	}
	else if (auto genericParamTypeRef = BfNodeDynCast<BfGenericParamTypeRef>(typeRef))
	{
		auto genericParamType = GetGenericParamType(genericParamTypeRef->mGenericParamKind, genericParamTypeRef->mGenericParamIdx);
		resolvedEntry->mValue = genericParamType;
		BF_ASSERT(BfResolvedTypeSet::Hash(genericParamType, &lookupCtx) == resolvedEntry->mHashCode);
		return ResolveTypeResult(typeRef, genericParamType, populateType, resolveFlags);
	}
	else if (auto retTypeTypeRef = BfNodeDynCast<BfModifiedTypeRef>(typeRef))
	{
		auto retTypeType = new BfModifiedTypeType();
		retTypeType->mModifiedKind = retTypeTypeRef->mRetTypeToken->mToken;
		retTypeType->mElementType = ResolveTypeRef(retTypeTypeRef->mElementType, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericParamConstValue);
		// We know this is a generic param type, it can't fail to resolve
		BF_ASSERT(retTypeType->mElementType);

		resolvedEntry->mValue = retTypeType;
		BF_ASSERT(BfResolvedTypeSet::Hash(retTypeType, &lookupCtx) == resolvedEntry->mHashCode);

		populateModule->InitType(retTypeType, populateType);
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
		if ((mCurTypeInstance != NULL) && (mCurTypeInstance->mDependencyMap.mMinDependDepth > 32))
		{
			Fail("Generic type dependency depth exceeded", typeRef);
			mContext->mResolvedTypes.RemoveEntry(resolvedEntry);
			return ResolveTypeResult(typeRef, NULL, populateType, resolveFlags);
		}

		BfVariant result;
		BfType* resultType = NULL;
		if (constExprTypeRef->mConstExpr != NULL)
		{
			result = mContext->mResolvedTypes.EvaluateToVariant(&lookupCtx, constExprTypeRef->mConstExpr, resultType);
			BF_ASSERT(resultType != NULL);
		}

		auto constExprType = new BfConstExprValueType();
		constExprType->mContext = mContext;

		constExprType->mType = resultType;
		BF_ASSERT(constExprType->mType != NULL);
		if (constExprType->mType == NULL)
			constExprType->mType = GetPrimitiveType(BfTypeCode_Let);
		constExprType->mValue = result;

		resolvedEntry->mValue = constExprType;
#ifdef _DEBUG
		if (BfResolvedTypeSet::Hash(constExprType, &lookupCtx) != resolvedEntry->mHashCode)
		{
			int refHash = BfResolvedTypeSet::Hash(typeRef, &lookupCtx);
			int typeHash = BfResolvedTypeSet::Hash(constExprType, &lookupCtx);
			BF_ASSERT(refHash == typeHash);
		}
		BF_ASSERT(BfResolvedTypeSet::Equals(constExprType, typeRef, &lookupCtx));
#endif

		populateModule->InitType(constExprType, populateType);

		return constExprType;
	}
	else
	{
		BFMODULE_FATAL(this, "Not implemented!");
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
			auto result = ResolveTypeDef(genericTypeDef, typeVector, populateType);
			if ((result != NULL) && (genericTypeRef->mCommas.size() + 1 != genericTypeDef->mGenericParamDefs.size()))
			{
				SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, result->ToTypeInstance());
				SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
				Fail(StrFormat("Type '%s' requires %d generic arguments", TypeToString(result).c_str(), genericTypeDef->mGenericParamDefs.size()), typeRef);
			}
			return result;
		}
	}

	return ResolveTypeRef(typeRef, populateType, resolveGenericParam ? (BfResolveTypeRefFlags)0 : BfResolveTypeRefFlag_NoResolveGenericParam);
}

// This finds non-default unspecialized generic type instances and converts them into a BfUnspecializedGenericTypeVariation
BfType* BfModule::CheckUnspecializedGenericType(BfTypeInstance* genericTypeInst, BfPopulateType populateType)
{
	int argCount = (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size();

	bool isDefaultUnspecialized = true;
	for (int argIdx = 0; argIdx < argCount; argIdx++)
	{
		auto argType = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[argIdx];
		if (argType->IsGenericParam())
		{
			auto genericParamType = (BfGenericParamType*)argType;
			if ((genericParamType->mGenericParamKind != BfGenericParamKind_Type) || (genericParamType->mGenericParamIdx != argIdx))
				isDefaultUnspecialized = false;
			genericTypeInst->mGenericTypeInfo->mIsUnspecialized = true;
		}
		else if (argType->IsUnspecializedType())
		{
			isDefaultUnspecialized = false;
			genericTypeInst->mGenericTypeInfo->mIsUnspecialized = true;
		}
		else
			isDefaultUnspecialized = false;
	}

	if (genericTypeInst->mGenericTypeInfo->mIsUnspecialized)
		genericTypeInst->mGenericTypeInfo->mIsUnspecializedVariation = !isDefaultUnspecialized;
	return genericTypeInst;
}

BfTypeInstance* BfModule::GetUnspecializedTypeInstance(BfTypeInstance* typeInst)
{
	if (!typeInst->IsGenericTypeInstance())
		return typeInst;

	BF_ASSERT((!typeInst->IsDelegateFromTypeRef()) && (!typeInst->IsFunctionFromTypeRef()));

	auto genericTypeInst = (BfTypeInstance*)typeInst;
	auto result = ResolveTypeDef(genericTypeInst->mTypeDef->GetDefinition(), BfPopulateType_Declaration);
	BF_ASSERT((result != NULL) && (result->IsUnspecializedType()));
	if (result == NULL)
		return NULL;
	return result->ToTypeInstance();
}

BfType* BfModule::ResolveTypeRef_Type(BfAstNode* astNode, const BfSizedArray<BfAstNode*>* genericArgs, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
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

	std::function<BfTypeReference* (BfAstNode*)> _ConvType = [&](BfAstNode* astNode) -> BfTypeReference*
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

		BfDeferredAstSizedArray<BfAstNode*> arguments(genericInstanceTypeRef->mGenericArguments, &alloc);

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

BfType* BfModule::ResolveTypeRef(BfAstNode* astNode, const BfSizedArray<BfAstNode*>* genericArgs, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	if (astNode == NULL)
	{
		AssertErrorState();
		return NULL;
	}

	if (auto typeRef = BfNodeDynCast<BfTypeReference>(astNode))
		return ResolveTypeRef(typeRef, populateType, resolveFlags);

	if ((resolveFlags & BfResolveTypeRefFlag_AllowImplicitConstExpr) != 0)
	{
		if (auto expr = BfNodeDynCast<BfExpression>(astNode))
		{
			auto checkType = ResolveTypeRef_Type(astNode, genericArgs, populateType, (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_IgnoreLookupError));
			if (checkType != NULL)
				return checkType;

			BfResolvedTypeSet::LookupContext lookupCtx;
			lookupCtx.mModule = this;
			BfResolvedTypeSet::Entry* typeEntry = NULL;

			BfType* resultType = NULL;
			auto result = mContext->mResolvedTypes.EvaluateToVariant(&lookupCtx, expr, resultType);
			if (resultType != NULL)
			{
				auto constExprValue = CreateConstExprValueType(result, resultType);
				return constExprValue;
			}
		}
	}

	return ResolveTypeRef_Type(astNode, genericArgs, populateType, resolveFlags);
}

// This flow should mirror CastToValue
bool BfModule::CanCast(BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags)
{
	BP_ZONE("BfModule::CanCast");

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
	return CastToValue(NULL, typedVal, toType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail | BfCastFlags_IsCastCheck));
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

BfType* BfModule::GetClosestNumericCastType(const BfTypedValue& typedVal, BfType* wantType)
{
	BfType* toType = wantType;
	if ((toType == NULL) ||
		((!toType->IsFloat()) && (!toType->IsIntegral())))
		toType = NULL;

	BfType* bestReturnType = NULL;

	if (typedVal.mType->IsTypedPrimitive())
		return NULL;

	auto checkType = typedVal.mType->ToTypeInstance();
	while (checkType != NULL)
	{
		for (auto operatorDef : checkType->mTypeDef->mOperators)
		{
			if (operatorDef->mOperatorDeclaration->mIsConvOperator)
			{
				if (operatorDef->IsExplicit())
					continue;

				auto returnType = CheckOperator(checkType, operatorDef, typedVal, BfTypedValue());
				if ((returnType != NULL) &&
					((returnType->IsIntegral()) || (returnType->IsFloat())))
				{
					bool canCastTo = true;

					if ((toType != NULL) && (!CanCast(GetFakeTypedValue(returnType), toType)))
						canCastTo = false;

					if (canCastTo)
					{
						if (bestReturnType == NULL)
						{
							bestReturnType = returnType;
						}
						else
						{
							if (CanCast(GetFakeTypedValue(bestReturnType), returnType))
							{
								bestReturnType = returnType;
							}
						}
					}
				}
			}
		}

		checkType = checkType->mBaseType;
	}

	if ((toType == NULL) && (bestReturnType != NULL))
	{
		auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
		if (!CanCast(GetFakeTypedValue(bestReturnType), intPtrType))
		{
			// If no 'wantType' is specified, try to get closest one to an intptr
			auto otherType = GetClosestNumericCastType(typedVal, intPtrType);
			if (otherType != NULL)
				return otherType;
		}
	}

	return bestReturnType;
}

BfIRValue BfModule::CastToFunction(BfAstNode* srcNode, const BfTypedValue& targetValue, BfMethodInstance* methodInstance, BfType* toType, BfCastFlags castFlags, BfIRValue irFunc)
{
	auto invokeMethodInstance = GetDelegateInvokeMethod(toType->ToTypeInstance());

	bool methodsThisMatch = true;
	if (invokeMethodInstance->mMethodDef->mIsStatic != methodInstance->mMethodDef->mIsStatic)
		methodsThisMatch = false;
	else
	{
		if (!methodInstance->mMethodDef->mIsStatic)
		{
			BfType* thisType = methodInstance->GetThisType();
			if (thisType->IsPointer())
				thisType = thisType->GetUnderlyingType();
			BfType* invokeThisType = invokeMethodInstance->GetThisType();
			if (invokeThisType->IsPointer())
				invokeThisType = invokeThisType->GetUnderlyingType();
			if (!TypeIsSubTypeOf(thisType->ToTypeInstance(), invokeThisType->ToTypeInstance()))
				methodsThisMatch = false;
		}
	}

	bool methodMatches = methodsThisMatch;
	if (methodMatches)
		methodMatches = invokeMethodInstance->IsExactMatch(methodInstance, false, false);

	if (methodMatches)
	{
		if (methodInstance->GetOwner()->IsFunction())
		{
			BF_ASSERT(targetValue);
			return targetValue.mValue;
		}

		BfIRFunction bindFuncVal = irFunc;
		if (!bindFuncVal)
		{
			BfModuleMethodInstance methodRefMethod;
			if (methodInstance->mDeclModule == this)
				methodRefMethod = methodInstance;
			else
				methodRefMethod = ReferenceExternalMethodInstance(methodInstance);
			auto dataType = GetPrimitiveType(BfTypeCode_IntPtr);
			if (!methodRefMethod.mFunc)
			{
				if ((!methodInstance->mIsUnspecialized) && (HasCompiledOutput()))
					AssertErrorState();
				return GetDefaultValue(dataType);
			}
			bindFuncVal = methodRefMethod.mFunc;
		}
		if ((mCompiler->mOptions.mAllowHotSwapping) && (!mIsComptimeModule))
			bindFuncVal = mBfIRBuilder->RemapBindFunction(bindFuncVal);
		return mBfIRBuilder->CreatePtrToInt(bindFuncVal, BfTypeCode_IntPtr);
	}

	if ((castFlags & BfCastFlags_SilentFail) == 0)
	{
		if ((methodsThisMatch) && (invokeMethodInstance->IsExactMatch(methodInstance, true, false)))
		{
			Fail(StrFormat("Non-static method '%s' cannot match '%s' because it contains captured variables, consider using a delegate or removing captures", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str()), srcNode);
		}
		else if (invokeMethodInstance->IsExactMatch(methodInstance, false, false))
		{
			bool handled = false;
			if (methodInstance->HasThis())
			{
				auto thisType = methodInstance->GetThisType();
				if (invokeMethodInstance->HasExplicitThis())
				{
					auto invokeThisType = invokeMethodInstance->GetThisType();

					bool thisWasPtr = false;
					if (thisType->IsPointer())
					{
						thisType = thisType->GetUnderlyingType();
						thisWasPtr = true;
					}

					bool invokeThisWasPtr = false;
					if (invokeThisType->IsPointer())
					{
						invokeThisType = invokeThisType->GetUnderlyingType();
						invokeThisWasPtr = true;
					}

					if (TypeIsSubTypeOf(thisType->ToTypeInstance(), invokeThisType->ToTypeInstance()))
					{
						if (invokeThisWasPtr != thisWasPtr)
						{
							if (invokeThisWasPtr)
								Fail(StrFormat("Non-static method '%s' cannot match '%s', consider removing 'mut' from 'mut %s this' in the function parameters", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str(), TypeToString(thisType).c_str()), srcNode);
							else
								Fail(StrFormat("Non-static method '%s' cannot match '%s', consider adding 'mut' specifier to '%s this' in the function parameters", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str(), TypeToString(thisType).c_str()), srcNode);
							handled = true;
						}
					}
				}
			}

			if ((!methodInstance->mMethodDef->mIsStatic) && (!invokeMethodInstance->HasExplicitThis()))
			{
				handled = true;
				auto thisType = methodInstance->GetParamType(-1);
				Fail(StrFormat("Non-static method '%s' cannot match '%s', consider adding '%s this' to the function parameters", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str(), TypeToString(thisType).c_str()), srcNode);
			}

			if (!handled)
			{
				if (invokeMethodInstance->mMethodDef->mIsStatic)
					Fail(StrFormat("Static method '%s' cannot match '%s'", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str()).c_str(), srcNode);
				else
					Fail(StrFormat("Non-static method '%s' cannot match '%s'", MethodToString(methodInstance).c_str(), TypeToString(toType).c_str()).c_str(), srcNode);
			}
		}
	}
	return BfIRValue();
}

BfIRValue BfModule::CastToValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType, BfCastFlags castFlags, BfCastResultFlags* resultFlags)
{
	bool silentFail = ((castFlags & BfCastFlags_SilentFail) != 0);
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
		bool isRef = false;

		if (((typedVal.mType->IsRef()) && (toType->IsRef())))
		{
			isRef = true;
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

			if ((fromInner->IsTuple()) && (toInner->IsTuple()))
			{
				auto fromTuple = (BfTupleType*)fromInner;
				auto toTuple = (BfTupleType*)toInner;
				if (fromTuple->mFieldInstances.size() == toTuple->mFieldInstances.size())
				{
					bool matches = true;
					for (int fieldIdx = 0; fieldIdx < (int)fromTuple->mFieldInstances.size(); fieldIdx++)
					{
						if (fromTuple->mFieldInstances[fieldIdx].mResolvedType != toTuple->mFieldInstances[fieldIdx].mResolvedType)
						{
							matches = false;
							break;
						}
					}
					if (matches)
					{
						// This is either a ref or a ptr so we don't need to set the "IsAddr" flag
						typedVal = MakeAddressable(typedVal);
						return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
					}
				}
			}

			if ((isRef) && (fromInner->IsStruct()) && (toInner->IsStruct()))
			{
				if (TypeIsSubTypeOf(fromInner->ToTypeInstance(), toInner->ToTypeInstance()))
				{
					if (toInner->IsValuelessType())
						return mBfIRBuilder->GetFakeVal();
					// Is this valid?
					typedVal = MakeAddressable(typedVal);
					return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
				}
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
		((toType->IsObjectOrInterface()) || (toType->IsPointer() || (toType->IsFunction()) || (toType->IsAllocType()))))
	{
		return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
	}

	// Func -> void*
	if ((typedVal.mType->IsFunction()) && (toType->IsVoidPtr()))
	{
		typedVal = LoadValue(typedVal);
		return mBfIRBuilder->CreateIntToPtr(typedVal.mValue, mBfIRBuilder->MapType(toType));
	}

	if (explicitCast)
	{
		// void* -> Func
		if ((typedVal.mType->IsVoidPtr()) && (toType->IsFunction()))
		{
			typedVal = LoadValue(typedVal);
			return mBfIRBuilder->CreatePtrToInt(typedVal.mValue, BfTypeCode_IntPtr);
		}

		// * -> Valueless
		if (toType->IsVoid())
			return mBfIRBuilder->GetFakeVal();

		// void* -> intptr
		if ((typedVal.mType->IsPointer()) && (toType->IsIntPtr()))
		{
			if ((!typedVal.mType->GetUnderlyingType()->IsVoid()) && ((castFlags & BfCastFlags_FromCompiler) == 0))
			{
				if (!ignoreErrors)
					Fail(StrFormat("Unable to cast directly from '%s' to '%s', consider casting to void* first", TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
				else if (!silentFail)
					SetFail();
			}

			auto toPrimitive = (BfPrimitiveType*)toType;
			return mBfIRBuilder->CreatePtrToInt(typedVal.mValue, toPrimitive->mTypeDef->mTypeCode);
		}

		// intptr -> void*
		if ((typedVal.mType->IsIntPtr()) && (toType->IsPointer()))
		{
			if ((!toType->GetUnderlyingType()->IsVoid()) && ((castFlags & BfCastFlags_FromCompiler) == 0))
			{
				if (!ignoreErrors)
					Fail(StrFormat("Unable to cast directly from '%s' to '%s', consider casting to void* first", TypeToString(typedVal.mType).c_str(), TypeToString(toType).c_str()), srcNode);
				else if (!silentFail)
					SetFail();
			}

			return mBfIRBuilder->CreateIntToPtr(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}
	}

	// * <-> Var
	if ((typedVal.mType->IsVar()) || (toType->IsVar()))
	{
		return mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(toType));
	}

	// Generic param -> *
	if (typedVal.mType->IsGenericParam())
	{
		if (toType->IsGenericParam())
		{
			auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)typedVal.mType);
			if (genericParamInst->mTypeConstraint == toType)
				return typedVal.mValue;
		}
		else
		{
			if ((typedVal.mKind != Beefy::BfTypedValueKind_GenericConstValue) && (toType == mContext->mBfObjectType))
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
							return mBfIRBuilder->GetFakeVal();
				}

				if (genericParamInst->mTypeConstraint != NULL)
				{
					SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
					auto constraintTypeInst = genericParamInst->mTypeConstraint->ToTypeInstance();

					if ((constraintTypeInst != NULL) && (constraintTypeInst->IsDelegateOrFunction()))
					{
						// Could be a methodref - can't cast to anything else
					}
					else
					{
						if ((constraintTypeInst != NULL) && (constraintTypeInst->IsInstanceOf(mCompiler->mEnumTypeDef)) && (explicitCast))
						{
							// Enum->int
							if ((explicitCast) && (toType->IsInteger()))
								return typedVal.mValue;
						}

						BfTypedValue fromTypedValue;
						if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
							fromTypedValue = GetDefaultTypedValue(genericParamInst->mTypeConstraint, false, BfDefaultValueKind_Undef);
						else
							fromTypedValue = BfTypedValue(mBfIRBuilder->GetFakeVal(), genericParamInst->mTypeConstraint, genericParamInst->mTypeConstraint->IsValueType());

						auto result = CastToValue(srcNode, fromTypedValue, toType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));
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
				}

				// Generic constrained with class or pointer type -> void*
				if (toType->IsVoidPtr())
				{
					if (((genericParamInst->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Interface)) != 0) ||
						((genericParamInst->mTypeConstraint != NULL) &&
							((genericParamInst->mTypeConstraint->IsPointer()) ||
								(genericParamInst->mTypeConstraint->IsInstanceOf(mCompiler->mFunctionTypeDef)) ||
								(genericParamInst->mTypeConstraint->IsObjectOrInterface()))))
					{
						return typedVal.mValue;
					}
				}

				if ((toType->IsInteger()) && (explicitCast))
				{
					if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Enum) != 0)
					{
						return typedVal.mValue;
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
			bool allowCast = (genericParamInst->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Interface)) != 0;
			if ((!allowCast) && (genericParamInst->mTypeConstraint != NULL))
				allowCast = genericParamInst->mTypeConstraint->IsObject() || genericParamInst->mTypeConstraint->IsPointer();
			if (allowCast)
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
		}

		if (genericParamInst->mTypeConstraint != NULL)
		{
			if (genericParamInst->mTypeConstraint->IsInstanceOf(mCompiler->mEnumTypeDef))
			{
				// int->Enum
				if ((explicitCast) && (typedVal.mType->IsInteger()))
					return mBfIRBuilder->GetFakeVal();
			}

			if ((genericParamInst->mTypeConstraint == toType) && (toType->IsUnspecializedType()))
				return mBfIRBuilder->GetFakeVal();

			auto castedVal = CastToValue(srcNode, typedVal, genericParamInst->mTypeConstraint, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));
			if (castedVal)
				return castedVal;
		}

		if (explicitCast)
		{
			if (((genericParamInst->mGenericParamFlags & BfGenericParamFlag_StructPtr) != 0) ||
				((genericParamInst->mTypeConstraint != NULL) && genericParamInst->mTypeConstraint->IsInstanceOf(mCompiler->mFunctionTypeDef)))
			{
				auto voidPtrType = CreatePointerType(GetPrimitiveType(BfTypeCode_None));
				auto castedVal = CastToValue(srcNode, typedVal, voidPtrType, (BfCastFlags)(castFlags | BfCastFlags_SilentFail));
				if (castedVal)
					return castedVal;
			}
		}

		if ((typedVal.mType->IsIntegral()) && ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Enum) != 0))
		{
			bool allowCast = explicitCast;
			if ((!allowCast) && (typedVal.mType->IsIntegral()))
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
				return mBfIRBuilder->GetFakeVal();
			}
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
				PopulateType(toType);
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

			if (((castFlags & BfCastFlags_NoInterfaceImpl) != 0) && (toTypeInstance->IsInterface()))
			{
				// Don't allow
			}
			else if (TypeIsSubTypeOf(fromTypeInstance, toTypeInstance))
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
				if ((ignoreWrites) && (!typedVal.mValue.IsConst()))
					return mBfIRBuilder->GetFakeVal();
				return mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(toType));
			}
		}
	}

	// MethodRef -> Function
	if ((typedVal.mType->IsMethodRef()) && (toType->IsFunction()))
	{
		BfMethodInstance* methodInstance = ((BfMethodRefType*)typedVal.mType)->mMethodRef;
		auto result = CastToFunction(srcNode, BfTypedValue(), methodInstance, toType, castFlags);
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
				if ((ignoreWrites) && (!typedVal.mValue.IsConst()))
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
		if ((!typedVal.mType->IsRef()) && (!typedVal.mType->IsModifiedTypeType()))
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

		auto zeroNullable = mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(toType));
		return zeroNullable;
	}

	// Nullable<A> -> Nullable<B>
	if ((typedVal.mType->IsNullable()) && (toType->IsNullable()))
	{
		auto fromNullableType = (BfTypeInstance*)typedVal.mType;
		auto toNullableType = (BfTypeInstance*)toType;

		if (ignoreWrites)
		{
			auto toVal = CastToValue(srcNode, BfTypedValue(mBfIRBuilder->GetFakeVal(), fromNullableType->mGenericTypeInfo->mTypeGenericArguments[0]),
				toNullableType->mGenericTypeInfo->mTypeGenericArguments[0], ignoreErrors ? BfCastFlags_SilentFail : BfCastFlags_None);
			if (!toVal)
				return BfIRValue();
			return mBfIRBuilder->GetFakeVal();
		}

		BfIRValue srcPtr = typedVal.mValue;
		if (!typedVal.IsAddr())
		{
			auto srcAlloca = CreateAllocaInst(fromNullableType);
			mBfIRBuilder->CreateStore(typedVal.mValue, srcAlloca);
			srcPtr = srcAlloca;
		}

		auto srcAddr = mBfIRBuilder->CreateInBoundsGEP(srcPtr, 0, 1); // mValue
		auto srcVal = mBfIRBuilder->CreateLoad(srcAddr);

		auto toVal = CastToValue(srcNode, BfTypedValue(srcVal, fromNullableType->mGenericTypeInfo->mTypeGenericArguments[0]),
			toNullableType->mGenericTypeInfo->mTypeGenericArguments[0], ignoreErrors ? BfCastFlags_SilentFail : BfCastFlags_None);
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
		auto fromTupleType = (BfTypeInstance*)typedVal.mType;
		auto toTupleType = (BfTypeInstance*)toType;

		PopulateType(fromTupleType);
		PopulateType(toTupleType);

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

			auto variantVal = TypedValueToVariant(srcNode, typedVal, true);
			if ((mBfIRBuilder->IsIntable(variantVal.mTypeCode)) && (mBfIRBuilder->IsIntable(toConstExprValueType->mValue.mTypeCode)))
			{
				if (variantVal.mInt64 == toConstExprValueType->mValue.mInt64)
					return typedVal.mValue;
			}
			else if ((mBfIRBuilder->IsFloat(variantVal.mTypeCode)) && (mBfIRBuilder->IsFloat(toConstExprValueType->mValue.mTypeCode)))
			{
				if (variantVal.ToDouble() == toConstExprValueType->mValue.ToDouble())
					return typedVal.mValue;
			}

			if (toConstExprValueType->mValue.mTypeCode == BfTypeCode_StringId)
			{
				int stringIdx = GetStringPoolIdx(typedVal.mValue, mBfIRBuilder);
				if ((stringIdx != -1) && (stringIdx == toConstExprValueType->mValue.mInt32))
					return typedVal.mValue;
			}

			if ((toConstExprValueType->mValue.mTypeCode == BfTypeCode_Let) && (constant->mConstType == BfConstType_Undef))
			{
				return typedVal.mValue;
			}

			if (!ignoreErrors)
			{
				String valStr;
				VariantToString(valStr, variantVal);
				Fail(StrFormat("Unable to cast '%s %s' to '%s'", TypeToString(typedVal.mType).c_str(), valStr.c_str(), TypeToString(toType).c_str()), srcNode);
			}
			else if (!silentFail)
				SetFail();
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
					if (mIsComptimeModule)
						return mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(toType));

					BF_ASSERT(mBfIRBuilder->mIgnoreWrites);

					auto undefConst = (BfConstantUndef*)constant;

					BfType* bfType = NULL;
					if (undefConst->mType.mKind == BfIRTypeData::TypeKind_TypeCode)
					{
						bfType = GetPrimitiveType((BfTypeCode)undefConst->mType.mId);
					}
					else
					{
						BF_ASSERT(undefConst->mType.mKind == BfIRTypeData::TypeKind_TypeId);
						if (undefConst->mType.mKind == BfIRTypeData::TypeKind_TypeId)
							bfType = mContext->FindTypeById(undefConst->mType.mId);
					}

					if (bfType == NULL)
						return BfIRValue();

					auto fakeVal = GetFakeTypedValue(bfType);
					auto val = CastToValue(srcNode, fakeVal, toType, castFlags);
					if (val)
						return mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(toType));
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
		case BfTypeCode_Float:
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
			case BfTypeCode_Float:
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
			if (typedVal.IsAddr())
				typedVal = LoadValue(typedVal);
			return mBfIRBuilder->CreateNumericCast(typedVal.mValue, typedVal.mType->IsSigned(), toTypeCode);
		}
	}

	if (typedVal.mValue.IsConst())
	{
		if ((toType->IsPointer()) && (toType->GetUnderlyingType() == GetPrimitiveType(BfTypeCode_Char8)) && (typedVal.mType->IsInstanceOf(mCompiler->mStringTypeDef)))
		{
			int stringId = GetStringPoolIdx(typedVal.mValue, mBfIRBuilder);
			if (stringId >= 0)
				return GetStringCharPtr(stringId);
		}
		else if ((toType->IsInstanceOf(mCompiler->mStringViewTypeDef)))
		{
			int stringId = GetStringPoolIdx(typedVal.mValue, mBfIRBuilder);
			bool isNull = false;

			auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
			if (constant->mTypeCode == BfTypeCode_NullPtr)
				isNull = true;

			if ((stringId >= 0) || (isNull))
			{
				int strLen = 0;
				String str;
				BfStringPoolEntry* entry = NULL;
				if ((isNull) || (mContext->mStringObjectIdMap.TryGetValue(stringId, &entry)))
				{
					auto svTypeInst = toType->ToTypeInstance();

					PopulateType(svTypeInst);
					PopulateType(svTypeInst->mBaseType);
					mBfIRBuilder->PopulateType(svTypeInst);

					SizedArray<BfIRValue, 2> spanFieldVals;
					spanFieldVals.Add(mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(svTypeInst->mBaseType->mBaseType)));

					if (isNull)
					{
						spanFieldVals.Add(mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(CreatePointerType(GetPrimitiveType(BfTypeCode_Char8)))));
						spanFieldVals.Add(mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0));
					}
					else
					{
						auto stringCharPtr = GetStringCharPtr(stringId);
						spanFieldVals.Add(stringCharPtr);
						spanFieldVals.Add(mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, entry->mString.mLength));
					}

					SizedArray<BfIRValue, 2> svFieldVals;
					svFieldVals.Add(mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapType(svTypeInst->mBaseType), spanFieldVals));
					return mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapType(svTypeInst), svFieldVals);
				}
			}
		}
	}

	// Check user-defined operators
	if (((castFlags & BfCastFlags_NoConversionOperator) == 0) && (toType != mContext->mBfObjectType))
	{
		BfType* walkFromType = typedVal.mType;
		if (walkFromType->IsWrappableType())
			walkFromType = GetWrappedStructType(walkFromType);
		BfType* walkToType = toType;
		if (walkToType->IsWrappableType())
			walkToType = GetWrappedStructType(walkToType);

		SizedArray<BfResolvedArg, 1> args;
		BfResolvedArg resolvedArg;
		resolvedArg.mTypedValue = typedVal;
		if (resolvedArg.mTypedValue.IsParams())
		{
			resolvedArg.mTypedValue = LoadOrAggregateValue(resolvedArg.mTypedValue);
			resolvedArg.mTypedValue.mKind = BfTypedValueKind_Value;
		}
		args.push_back(resolvedArg);
		BfMethodMatcher methodMatcher(srcNode, this, "", args, BfMethodGenericArguments());
		methodMatcher.mCheckReturnType = toType;
		methodMatcher.mBfEvalExprFlags = (BfEvalExprFlags)(BfEvalExprFlags_NoAutoComplete | BfEvalExprFlags_FromConversionOp);
		if ((castFlags & BfCastFlags_Explicit) != 0)
			methodMatcher.mBfEvalExprFlags = (BfEvalExprFlags)(methodMatcher.mBfEvalExprFlags | BfEvalExprFlags_FromConversionOp_Explicit);
		methodMatcher.mAllowImplicitRef = true;
		methodMatcher.mAllowImplicitWrap = true;
		BfBaseClassWalker baseClassWalker(walkFromType, walkToType, this);

		bool isConstraintCheck = ((castFlags & BfCastFlags_IsConstraintCheck) != 0);

		BfType* bestSelfType = NULL;
		while (true)
		{
			auto entry = baseClassWalker.Next();
			auto checkType = entry.mTypeInstance;
			if (checkType == NULL)
				break;
			for (auto operatorDef : checkType->mTypeDef->mOperators)
			{
				if (operatorDef->mOperatorDeclaration->mIsConvOperator)
				{
					if ((!explicitCast) && (operatorDef->IsExplicit()))
						continue;

					if (!methodMatcher.IsMemberAccessible(checkType, operatorDef->mDeclaringType))
						continue;

					int prevArgSize = (int)args.mSize;
					if (!operatorDef->mIsStatic)
					{
						// Try without arg
						args.mSize = 0;
					}

					if (isConstraintCheck)
					{
						auto returnType = CheckOperator(checkType, operatorDef, typedVal, BfTypedValue());
						if (returnType != NULL)
						{
							auto result = BfTypedValue(mBfIRBuilder->GetFakeVal(), returnType);
							if (result)
							{
								if (result.mType != toType)
								{
									auto castedResult = CastToValue(srcNode, result, toType, (BfCastFlags)(castFlags | BfCastFlags_Explicit | BfCastFlags_NoConversionOperator), resultFlags);
									if (castedResult)
										return castedResult;
								}
								else
									return result.mValue;
							}
						}
					}
					else
					{
						if (methodMatcher.CheckMethod(NULL, checkType, operatorDef, false))
							methodMatcher.mSelfType = entry.mSrcType;
					}

					args.mSize = prevArgSize;
				}
			}
		}

		if (methodMatcher.mBestMethodDef != NULL)
		{
			if (mayBeBox)
			{
				if (!ignoreErrors)
				{
					if (Fail("Ambiguous cast, may be conversion operator or may be boxing request", srcNode) != NULL)
						mCompiler->mPassInstance->MoreInfo("See conversion operator", methodMatcher.mBestMethodDef->GetRefNode());
				}
				else if (!silentFail)
					SetFail();
			}
		}

		if (methodMatcher.mBestMethodDef == NULL)
		{
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
							if (CanCast(typedVal, opConstraint.mRightType, BfCastFlags_NoConversionOperator))
							{
								// .. and we can convert the constraint's toType to OUR toType then we're good
								auto opToVal = genericParam->mExternType;
								if (CanCast(BfTypedValue(BfIRValue::sValueless, opToVal), toType, BfCastFlags_NoConversionOperator))
								{
									if (mBfIRBuilder->IsConstValue(typedVal.mValue))
									{
										// Retain constness
										return mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(toType));
									}
									else
										return mBfIRBuilder->GetFakeVal();
								}
							}
						}
					}
				}
			}

			// Check type generic constraints
			if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsGenericTypeInstance()) && (mCurTypeInstance->IsUnspecializedType()))
			{
				SizedArray<BfGenericParamInstance*, 4> genericParams;
				GetActiveTypeGenericParamInstances(genericParams);
				for (auto genericParam : genericParams)
				{
					for (auto& opConstraint : genericParam->mOperatorConstraints)
					{
						if ((opConstraint.mCastToken == BfToken_Implicit) ||
							((explicitCast) && (opConstraint.mCastToken == BfToken_Explicit)))
						{
							// If we can convert OUR fromVal to the constraint's fromVal then we may match
							if (CanCast(typedVal, opConstraint.mRightType, BfCastFlags_NoConversionOperator))
							{
								// .. and we can convert the constraint's toType to OUR toType then we're good
								auto opToVal = genericParam->mExternType;
								if (CanCast(BfTypedValue(BfIRValue::sValueless, opToVal), toType, BfCastFlags_NoConversionOperator))
								{
									if (mBfIRBuilder->IsConstValue(typedVal.mValue))
									{
										// Retain constness
										return mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(toType));
									}
									else
										return mBfIRBuilder->GetFakeVal();
								}
							}
						}
					}
				}
			}
		}
		else
		{
			BfTypedValue result;
			BfExprEvaluator exprEvaluator(this);
			exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_FromConversionOp;
			if ((castFlags & BfCastFlags_WantsConst) != 0)
				exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_Comptime);

			auto methodDeclaration = BfNodeDynCast<BfMethodDeclaration>(methodMatcher.mBestMethodDef->mMethodDeclaration);
			if ((methodDeclaration != NULL) && (methodDeclaration->mBody == NULL))
			{
				auto fromType = typedVal.mType;

				// Handle the typedPrim<->underlying part implicitly
				if (fromType->IsTypedPrimitive())
				{
					typedVal = LoadValue(typedVal);
					auto convTypedValue = BfTypedValue(typedVal.mValue, fromType->GetUnderlyingType());
					return CastToValue(srcNode, convTypedValue, toType, (BfCastFlags)(castFlags & ~BfCastFlags_Explicit), NULL);
				}
				else if (toType->IsTypedPrimitive())
				{
					auto castedVal = CastToValue(srcNode, typedVal, toType->GetUnderlyingType(), (BfCastFlags)(castFlags & ~BfCastFlags_Explicit), NULL);
					return castedVal;
				}
			}

			bool doCall = true;

			auto moduleMethodInstance = exprEvaluator.GetSelectedMethod(methodMatcher);
			if (moduleMethodInstance.mMethodInstance != NULL)
			{
				auto returnType = moduleMethodInstance.mMethodInstance->mReturnType;
				auto paramType = moduleMethodInstance.mMethodInstance->GetParamType(0);

				BfCastFlags implicitCastFlags = (BfCastFlags)(castFlags & ~BfCastFlags_Explicit | BfCastFlags_NoConversionOperator);

				// Check typedPrimitive->underlying cast
				if ((explicitCast) && (typedVal.mType->IsTypedPrimitive()))
				{
					auto underlyingType = typedVal.mType->GetUnderlyingType();
					if ((returnType == underlyingType) && (explicitCast))
					{
						doCall = false;
					}
					else if ((CanCast(GetFakeTypedValue(underlyingType), toType, (BfCastFlags)(castFlags | BfCastFlags_NoConversionOperator))))
					{
						float underlyingCanCast = CanCast(GetFakeTypedValue(underlyingType), toType, implicitCastFlags);
						float returnCanCast = CanCast(GetFakeTypedValue(returnType), toType, implicitCastFlags);

						if ((underlyingCanCast) &&
							(!returnCanCast))
						{
							doCall = false;
						}
						else if ((returnCanCast) &&
							(!underlyingCanCast))
						{
							// Can do
						}
						else if ((CanCast(GetFakeTypedValue(underlyingType), returnType, implicitCastFlags)) &&
							(!CanCast(GetFakeTypedValue(returnType), underlyingType, implicitCastFlags)))
						{
							// Can do
						}
						else
							doCall = false;
					}
				}

				// Check underlying->typedPrimitive cast
				if ((explicitCast) && (toType->IsTypedPrimitive()))
				{
					auto underlyingType = toType->GetUnderlyingType();
					if ((paramType == underlyingType) && (explicitCast))
					{
						doCall = false;
					}
					else if (CanCast(typedVal, underlyingType, (BfCastFlags)(castFlags | BfCastFlags_NoConversionOperator)))
					{
						float underlyingCanCast = CanCast(typedVal, underlyingType, implicitCastFlags);
						float paramCanCast = CanCast(typedVal, paramType, implicitCastFlags);

						if ((underlyingType) &&
							(!paramCanCast))
						{
							doCall = false;
						}
						else if ((paramCanCast) &&
							(!underlyingCanCast))
						{
							// Can do
						}
						else if ((CanCast(GetFakeTypedValue(underlyingType), paramType, implicitCastFlags)) &&
							(!CanCast(GetFakeTypedValue(paramType), underlyingType, implicitCastFlags)))
						{
							// Can do
						}
						else
							doCall = false;
					}
				}

				if (doCall)
				{
					if (!silentFail)
						methodMatcher.FlushAmbiguityError();

					auto wantType = paramType;
					if (wantType->IsRef())
						wantType = wantType->GetUnderlyingType();
					auto convTypedVal = methodMatcher.mArguments[0].mTypedValue;
					if (wantType != convTypedVal.mType)
					{
						if ((convTypedVal.mType->IsWrappableType()) && (wantType == GetWrappedStructType(convTypedVal.mType)))
						{
							convTypedVal = MakeAddressable(convTypedVal);
							methodMatcher.mArguments[0].mTypedValue = BfTypedValue(mBfIRBuilder->CreateBitCast(convTypedVal.mValue, mBfIRBuilder->MapTypeInstPtr(wantType->ToTypeInstance())),
								paramType, paramType->IsRef() ? BfTypedValueKind_Value : BfTypedValueKind_Addr);
						}
						else
						{
							methodMatcher.mArguments[0].mTypedValue = Cast(srcNode, convTypedVal, wantType, (BfCastFlags)(castFlags | BfCastFlags_Explicit | BfCastFlags_NoConversionOperator));
							if (paramType->IsRef())
							{
								convTypedVal = MakeAddressable(convTypedVal);
								convTypedVal.mKind = BfTypedValueKind_Addr;
							}
						}
					}
				}
			}

			if (doCall)
			{
				if ((castFlags & BfCastFlags_IsCastCheck) != 0)
				{
					// We've already verified that we can cast from the return type to toType in MethodMatcher.CheckMethod
					return mBfIRBuilder->GetFakeVal();
				}

				result = exprEvaluator.CreateCall(&methodMatcher, BfTypedValue());
				if (result.mType != toType)
					return CastToValue(srcNode, result, toType, (BfCastFlags)(castFlags | BfCastFlags_Explicit | BfCastFlags_NoConversionOperator), resultFlags);

				if (result)
				{
					if (resultFlags != NULL)
					{
						if (result.IsAddr())
							*resultFlags = (BfCastResultFlags)(*resultFlags | BfCastResultFlags_IsAddr);
						if (result.mKind == BfTypedValueKind_TempAddr)
							*resultFlags = (BfCastResultFlags)(*resultFlags | BfCastResultFlags_IsTemp);
					}
					else if (result.IsAddr())
						result = LoadValue(result);

					return result.mValue;
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

	if ((mayBeBox) && ((castFlags & BfCastFlags_NoBox) == 0))
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
		auto value = BoxValue(srcNode, typedVal, toType, scopeData, castFlags);
		if (value)
			return value.mValue;
	}

	if (!ignoreErrors)
	{
		const char* errStrF = explicitCast ?
			"Unable to cast '%s' to '%s'" :
			"Unable to implicitly cast '%s' to '%s'";

		if ((castFlags & BfCastFlags_FromComptimeReturn) != 0)
			errStrF = "Comptime return unable to cast '%s' to '%s'";

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
	else if (!silentFail)
		SetFail();
	return BfIRValue();
}

BfTypedValue BfModule::Cast(BfAstNode* srcNode, const BfTypedValue& typedVal, BfType* toType, BfCastFlags castFlags)
{
	bool explicitCast = (castFlags & BfCastFlags_Explicit) != 0;

	if (typedVal.mType == toType)
		return typedVal;

	if ((toType->IsSizedArray()) && (typedVal.mType->IsSizedArray()))
	{
		// Retain our type if we're casting from a known-sized array to an unknown-sized arrays
		if ((toType->IsUndefSizedArray()) && ((typedVal.mType->GetUnderlyingType()) == (toType->GetUnderlyingType())))
		{
			return typedVal;
		}
	}

	if ((castFlags & BfCastFlags_Force) != 0)
	{
		PopulateType(toType, BfPopulateType_Data);
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
		PopulateType(toType);

		auto fromTupleType = (BfTypeInstance*)typedVal.mType;
		auto toTupleType = (BfTypeInstance*)toType;
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
					else if (typedVal.IsSplat())
					{
						BfTypedValue retTypedValue = typedVal;
						retTypedValue.mType = tupleType;
						return retTypedValue;
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
							castedElementVal = LoadValue(castedElementVal);
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

	// Function->Function and Delegate->Delegate where type is compatible but not exact
	if (((typedVal.mType->IsDelegate()) || (typedVal.mType->IsFunction())) &&
		(typedVal.mType != toType) && // Don't bother to check for exact match, let CastToValue handle this
		((typedVal.mType->IsDelegate()) == (toType->IsDelegate())) &&
		((typedVal.mType->IsFunction()) == (toType->IsFunction())))
	{
		auto fromTypeInst = typedVal.mType->ToTypeInstance();
		auto toTypeInst = toType->ToTypeInstance();

		auto fromMethodInst = GetRawMethodByName(fromTypeInst, "Invoke", -1, true);
		auto toMethodInst = GetRawMethodByName(toTypeInst, "Invoke", -1, true);

		auto toDelegateInfo = toTypeInst->GetDelegateInfo();

		if ((fromMethodInst != NULL) && (toMethodInst != NULL) &&
			(fromMethodInst->mCallingConvention == toMethodInst->mCallingConvention) &&
			(fromMethodInst->mMethodDef->mIsMutating == toMethodInst->mMethodDef->mIsMutating) &&
			(fromMethodInst->mReturnType == toMethodInst->mReturnType) &&
			(fromMethodInst->GetParamCount() == toMethodInst->GetParamCount()))
		{
 			bool matched = true;

			StringT<64> fromParamName;
			StringT<64> toParamName;

			if (fromMethodInst->HasExplicitThis() != toMethodInst->HasExplicitThis())
			{
				matched = false;
			}
			else
			{
				for (int paramIdx = 0; paramIdx < (int)fromMethodInst->GetParamCount(); paramIdx++)
				{
					bool nameMatches = true;

					if (!explicitCast)
					{
						int fromNamePrefixCount = 0;
						int toNamePrefixCount = 0;
						fromMethodInst->GetParamName(paramIdx, fromParamName, fromNamePrefixCount);
						toMethodInst->GetParamName(paramIdx, toParamName, toNamePrefixCount);
						if ((!fromParamName.IsEmpty()) && (!toParamName.IsEmpty()))
							nameMatches = fromParamName == toParamName;
					}

					if ((fromMethodInst->GetParamKind(paramIdx) == toMethodInst->GetParamKind(paramIdx)) &&
						(fromMethodInst->GetParamType(paramIdx) == toMethodInst->GetParamType(paramIdx)) &&
						(nameMatches))
					{
						// Matched, required for implicit/explicit
					}
					else
					{
						matched = false;
						break;
					}
				}
			}

			if (matched)
			{
				BfTypedValue loadedVal = LoadValue(typedVal);
				return BfTypedValue(mBfIRBuilder->CreateBitCast(loadedVal.mValue, mBfIRBuilder->MapType(toType)), toType);
			}
		}
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
		if (constant->mConstType == BfConstType_Agg)
		{
			uint64 intVal = 0;

			auto constantArray = (BfConstantAgg*)constant;
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

bool BfModule::TypeHasParentOrEquals(BfTypeDef* checkChildTypeDef, BfTypeDef* checkParentTypeDef)
{
	BfTypeDef* checkType = checkChildTypeDef;

	if (checkType->mNestDepth < checkParentTypeDef->mNestDepth)
		return false;

	while (checkType->mNestDepth > checkParentTypeDef->mNestDepth)
		checkType = checkType->mOuterType;

	if (checkType->GetDefinition() == checkParentTypeDef->GetDefinition())
		return true;
	if (checkType->mNameEx != checkParentTypeDef->mNameEx)
		return false;

	if (checkType->mIsPartial)
	{
		for (auto partial : checkParentTypeDef->mPartials)
			if (partial == checkType)
				return true;
	}

	return false;
}

BfTypeDef* BfModule::FindCommonOuterType(BfTypeDef* type, BfTypeDef* type2)
{
	if ((type == NULL) || (type2 == NULL))
		return NULL;
	int curNestDepth = BF_MIN(type->mNestDepth, type2->mNestDepth);
	while (type->mNestDepth > curNestDepth)
		type = type->mOuterType;
	while (type2->mNestDepth > curNestDepth)
		type2 = type2->mOuterType;

	while (curNestDepth >= 0)
	{
		if ((!type->mIsPartial) && (!type2->mIsPartial))
		{
			if (type->GetDefinition() == type2->GetDefinition())
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

	if (srcType->mDefineState < BfTypeDefineState_HasInterfaces_Direct)
	{
		if (srcType->mDefineState == BfTypeDefineState_ResolvingBaseType)
		{
			auto typeState = mContext->mCurTypeState;
			while (typeState != NULL)
			{
				if ((typeState->mType == srcType) && (typeState->mCurBaseType != NULL))
				{
					return TypeIsSubTypeOf(typeState->mCurBaseType, wantType, checkAccessibility);
				}
				typeState = typeState->mPrevState;
			}
		}

		// Type is incomplete.  We don't do the IsIncomplete check here because of re-entry
		//  While handling 'var' resolution, we don't want to force a PopulateType reentry
		//  but we do have enough information for TypeIsSubTypeOf
		PopulateType(srcType, BfPopulateType_Interfaces_Direct);
	}

	if (wantType->IsInterface())
	{
		if (wantType->mDefineState < BfTypeDefineState_HasInterfaces_All)
			PopulateType(srcType, BfPopulateType_Interfaces_All);

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
							((mCurMethodInstance != NULL) && (mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mResolveKind != BfTypeState::ResolveKind_BuildingGenericParams)))
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
			if ((checkType != NULL) && (checkType->mDefineState < BfTypeDefineState_CETypeInit))
			{
				// We check BfTypeDefineState_CETypeInit so we don't cause a populate loop during interface checking during CETypeInit
				PopulateType(checkType, BfPopulateType_Interfaces_All);
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

bool BfModule::TypeIsSubTypeOf(BfTypeInstance* srcType, BfTypeDef* wantType)
{
	if ((srcType == NULL) || (wantType == NULL))
		return false;
	if (srcType->IsInstanceOf(wantType))
		return true;

	if (srcType->mDefineState < BfTypeDefineState_HasInterfaces_Direct)
	{
		if (srcType->mDefineState == BfTypeDefineState_ResolvingBaseType)
		{
			auto typeState = mContext->mCurTypeState;
			while (typeState != NULL)
			{
				if ((typeState->mType == srcType) && (typeState->mCurBaseType != NULL))
				{
					return TypeIsSubTypeOf(typeState->mCurBaseType, wantType);
				}
				typeState = typeState->mPrevState;
			}
		}

		// Type is incomplete.  We don't do the IsIncomplete check here because of re-entry
		//  While handling 'var' resolution, we don't want to force a PopulateType reentry
		//  but we do have enough information for TypeIsSubTypeOf
		PopulateType(srcType, BfPopulateType_Interfaces_Direct);
	}

	if (wantType->mTypeCode == BfTypeCode_Interface)
	{
		if (srcType->mDefineState < BfTypeDefineState_HasInterfaces_All)
			PopulateType(srcType, BfPopulateType_Interfaces_All);

		BfTypeDef* checkActiveTypeDef = NULL;

		auto checkType = srcType;
		while (checkType != NULL)
		{
			for (auto ifaceInst : checkType->mInterfaces)
			{
				if (ifaceInst.mInterfaceType->IsInstanceOf(wantType))
					return true;
			}
			checkType = checkType->GetImplBaseType();
			if ((checkType != NULL) && (checkType->mDefineState < BfTypeDefineState_HasInterfaces_All))
			{
				PopulateType(checkType, BfPopulateType_Interfaces_All);
			}
		}

		if (srcType->IsTypedPrimitive())
		{
			BfType* underlyingType = srcType->GetUnderlyingType();
			if (underlyingType->IsWrappableType())
			{
				BfTypeInstance* wrappedType = GetWrappedStructType(underlyingType);
				if ((wrappedType != NULL) && (wrappedType != srcType))
					return TypeIsSubTypeOf(wrappedType, wantType);
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

		auto leftGenericType = (BfTypeInstance*)leftType;
		auto rightGenericType = (BfTypeInstance*)rightType;

		if (leftGenericType->mTypeDef != rightGenericType->mTypeDef)
			return false;

		bool isBetter = false;
		bool isWorse = false;
		for (int argIdx = 0; argIdx < (int)leftGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); argIdx++)
		{
			if (IsTypeMoreSpecific(leftGenericType->mGenericTypeInfo->mTypeGenericArguments[argIdx], rightGenericType->mGenericTypeInfo->mTypeGenericArguments[argIdx]))
				isBetter = true;
			if (IsTypeMoreSpecific(rightGenericType->mGenericTypeInfo->mTypeGenericArguments[argIdx], leftGenericType->mGenericTypeInfo->mTypeGenericArguments[argIdx]))
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
	case BfTypeCode_Boolean:
		if (variant.mUInt64 == 0)
			str += "false";
		else if (variant.mUInt64 == 1)
			str += "true";
		else
			str += StrFormat("%lld", variant.mInt64);
		break;
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Int32:
		str += StrFormat("%d", variant.mInt32);
		break;
	case BfTypeCode_Char8:
	case BfTypeCode_Char16:
	case BfTypeCode_Char32:
		if ((variant.mUInt32 >= 32) && (variant.mUInt32 <= 0x7E))
			str += StrFormat("'%c'", (char)variant.mUInt32);
		else if (variant.mUInt32 <= 0xFF)
			str += StrFormat("'\\x%2X'", variant.mUInt32);
		else
			str += StrFormat("'\\u{%X}'", variant.mUInt32);
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
	case BfTypeCode_Float:
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
	case BfTypeCode_StringId:
		{
			int stringId = variant.mInt32;
			auto stringPoolEntry = mContext->mStringObjectIdMap[stringId];
			str += '"';
			str += SlashString(stringPoolEntry.mString, false, false, true);
			str += '"';
		}
		break;
	case BfTypeCode_Let:
		str += "?";
		break;
	default: break;
	}
}

void BfModule::DoTypeToString(StringImpl& str, BfType* resolvedType, BfTypeNameFlags typeNameFlags, Array<String>* genericMethodNameOverrides)
{
	BP_ZONE("BfModule::DoTypeToString");

	if ((typeNameFlags & BfTypeNameFlag_AddProjectName) != 0)
	{
		BfProject* defProject = NULL;

		auto typeInst = resolvedType->ToTypeInstance();
		if (typeInst != NULL)
		{
			defProject = typeInst->mTypeDef->mProject;
			str += defProject->mName;
			str += ":";
		}

		SizedArray<BfProject*, 4> projectList;
		BfTypeUtils::GetProjectList(resolvedType, &projectList, 0);
		if (!projectList.IsEmpty())
		{
			if (defProject != projectList[0])
			{
				str += projectList[0]->mName;
				str += ":";
			}
		}
		typeNameFlags = (BfTypeNameFlags)(typeNameFlags & ~BfTypeNameFlag_AddProjectName);
	}

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
		DoTypeToString(str, arrayType->mGenericTypeInfo->mTypeGenericArguments[0], typeNameFlags, genericMethodNameOverrides);
		str += "[";
		for (int i = 1; i < arrayType->mDimensions; i++)
			str += ",";
		str += "]";
		return;
	}
	else if (resolvedType->IsNullable())
	{
		auto genericType = (BfTypeInstance*)resolvedType;
		auto elementType = genericType->mGenericTypeInfo->mTypeGenericArguments[0];
		DoTypeToString(str, elementType, typeNameFlags, genericMethodNameOverrides);
		str += "?";
		return;
	}
	else if (resolvedType->IsTuple())
	{
		BfTypeInstance* tupleType = (BfTypeInstance*)resolvedType;

		str += "(";
		for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
		{
			if (fieldIdx > 0)
				str += ", ";

			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			BfTypeNameFlags innerFlags = (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType | BfTypeNameFlag_ExtendedInfo));
			DoTypeToString(str, fieldInstance->GetResolvedType(), innerFlags, genericMethodNameOverrides);

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

		auto delegateType = (BfTypeInstance*)resolvedType;
		auto delegateInfo = resolvedType->GetDelegateInfo();

		if (mCurTypeInstance == delegateType)
		{
			// Don't try to use ourselves for generic param resolution.  This should only happen for debug printings from
			//  within InitType and such, not actual user-facing display
			mCurTypeInstance = NULL;
		}

		auto methodDef = delegateType->mTypeDef->mMethods[0];

		switch (methodDef->mCallingConvention)
		{
		case BfCallingConvention_Stdcall:
			str += "[StdCall] ";
			break;
		case BfCallingConvention_Fastcall:
			str += "[FastCall] ";
			break;
		default:
			break;
		}

		if (resolvedType->IsDelegateFromTypeRef())
			str += "delegate ";
		else
			str += "function ";

		if (delegateInfo->mCallingConvention != BfCallingConvention_Unspecified)
		{
			str += "[CallingConvention(";
			switch (delegateInfo->mCallingConvention)
			{
			case BfCallingConvention_Cdecl:
				str += ".Cdecl";
				break;
			case BfCallingConvention_Stdcall:
				str += ".Stdcall";
				break;
			case BfCallingConvention_Fastcall:
				str += ".Fastcall";
				break;
			}
			str += ")] ";
		}

		DoTypeToString(str, delegateInfo->mReturnType, typeNameFlags, genericMethodNameOverrides);
		str += "(";

		bool isFirstParam = true;//
		for (int paramIdx = 0; paramIdx < methodDef->mParams.size(); paramIdx++)
		{
			if (!isFirstParam)
				str += ", ";
			auto paramDef = methodDef->mParams[paramIdx];
			BfTypeNameFlags innerFlags = (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType | BfTypeNameFlag_ExtendedInfo));

			if (paramDef->mParamKind == BfParamKind_VarArgs)
			{
				str += "...";
				continue;
			}

			auto paramType = delegateInfo->mParams[paramIdx];
			if ((paramIdx == 0) && (delegateInfo->mHasExplicitThis))
			{
				if ((methodDef->mIsMutating) && (paramType->IsValueType()))
					str += "mut ";
			}

			DoTypeToString(str, paramType, innerFlags, genericMethodNameOverrides);

			if (!paramDef->mName.IsEmpty())
			{
				str += " ";
				str += paramDef->mName;
			}

			isFirstParam = false;
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
			str += "method reference NULL";
			return;
		}
		str += "method reference ";
		str += MethodToString(methodInstance, BfMethodNameFlag_NoAst);
		return;
	}
	else if (resolvedType->IsTypeInstance())
	{
		BfTypeInstance* typeInstance = (BfTypeInstance*)resolvedType;

		if ((typeNameFlags & BfTypeNameFlag_ExtendedInfo) != 0)
		{
			if (typeInstance->mTypeDef->mIsDelegate)
				str += "delegate ";
			else if (typeInstance->mTypeDef->mIsFunction)
				str += "function ";
			else if (typeInstance->mTypeDef->mTypeCode == BfTypeCode_Object)
				str += "class ";
			else if (typeInstance->mTypeDef->mTypeCode == BfTypeCode_Enum)
				str += "enum ";
			else if (typeInstance->mTypeDef->mTypeCode == BfTypeCode_Struct)
				str += "struct ";
			else if (typeInstance->mTypeDef->mTypeCode == BfTypeCode_TypeAlias)
				str += "typealias ";
		}

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
				auto genericTypeInst = (BfTypeInstance*)resolvedType;
				if (prevGenericParamCount != (int)checkTypeDef->mGenericParamDefs.size())
				{
					str += '<';
					for (int i = prevGenericParamCount; i < (int)checkTypeDef->mGenericParamDefs.size(); i++)
					{
						BfType* typeGenericArg = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[i];
						if (typeGenericArg->IsGenericParam())
						{
							if ((typeNameFlags & BfTypeNameFlag_ResolveGenericParamNames) == 0)
							{
								// We don't want the param names, just the commas (this is an unspecialized type reference)
								if (i > prevGenericParamCount)
									str += ',';

								if ((typeNameFlags & BfTypeNameFlag_UseUnspecializedGenericParamNames) != 0)
								{
									str += checkTypeDef->mGenericParamDefs[i]->mName;
								}

								continue;
							}
						}

						if (i > prevGenericParamCount)
							str += ", ";
						DoTypeToString(str, typeGenericArg, (BfTypeNameFlags)((typeNameFlags | BfTypeNameFlag_ShortConst) & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType | BfTypeNameFlag_ExtendedInfo)), genericMethodNameOverrides);
					}
					str += '>';
				}
			}

			if (depth > 0)
				str += '.';
		};

		if (typeInstance->IsTypeAlias())
		{
			if ((typeNameFlags & BfTypeNameFlag_ExtendedInfo) != 0)
			{
				auto underlyingType = typeInstance->GetUnderlyingType();
				if (underlyingType != NULL)
				{
					str += " = ";
					DoTypeToString(str, underlyingType, (BfTypeNameFlags)(typeNameFlags & ~(BfTypeNameFlag_OmitNamespace | BfTypeNameFlag_OmitOuterType | BfTypeNameFlag_ExtendedInfo)));
				}
			}
		}

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
		if (genericParamInstance == NULL)
		{
			str += StrFormat("@M%d", genericParam->mGenericParamIdx);
			return;
		}

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
		else if (refType->mRefKind == BfRefType::RefKind_In)
		{
			str += "in ";
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
	else if (resolvedType->IsModifiedTypeType())
	{
		auto retTypeType = (BfModifiedTypeType*)resolvedType;
		str += BfTokenToString(retTypeType->mModifiedKind);
		str += "(";
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
	else if (resolvedType->IsUnknownSizedArrayType())
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
		SizedArray<intptr, 4> sizes;

		auto checkType = resolvedType;
		while (true)
		{
			if (checkType->IsSizedArray())
			{
				auto arrayType = (BfSizedArrayType*)checkType;
				sizes.Add(arrayType->mElementCount);
				checkType = arrayType->mElementType;
				continue;
			}

			DoTypeToString(str, checkType, typeNameFlags, genericMethodNameOverrides);
			break;
		}

		for (int i = 0; i < (int)sizes.mSize; i++)
		{
			if (sizes[i] == -1)
				str += "[?]";
			else
				str += StrFormat("[%d]", sizes[i]);
		}
		return;
	}
	else if (resolvedType->IsConstExprValue())
	{
 		auto constExprValueType = (BfConstExprValueType*)resolvedType;
		if ((typeNameFlags & BfTypeNameFlag_ShortConst) == 0)
		{
			str += "const ";

			DoTypeToString(str, constExprValueType->mType, typeNameFlags, genericMethodNameOverrides);
			str += " ";
		}

		VariantToString(str, constExprValueType->mValue);

		return;
	}
	BFMODULE_FATAL(this, "Not implemented");
	str += "???";
	return;
}