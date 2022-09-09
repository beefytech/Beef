#pragma warning(push)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4146)
#pragma warning(disable:4267)
#pragma warning(disable:4291)

#include "BfExprEvaluator.h"
#include "BfConstResolver.h"
#include "BfAutoComplete.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BfParser.h"
#include "BfMangler.h"
#include "BfDemangler.h"
#include "BfResolvePass.h"
#include "BfUtil.h"
#include "BfDeferEvalChecker.h"
#include "BfVarDeclChecker.h"
#include "BfFixits.h"
#include "CeMachine.h"
#include "BfDefBuilder.h"
#include "CeMachine.h"
#include "CeDebugger.h"

#pragma warning(pop)
#pragma warning(disable:4996)

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;
using namespace llvm;

//////////////////////////////////////////////////////////////////////////

DeferredTupleAssignData::~DeferredTupleAssignData()
{
	for (auto entry : mChildren)
	{
		delete entry.mExprEvaluator;
		delete entry.mInnerTuple;
	}
}

//////////////////////////////////////////////////////////////////////////

BfBaseClassWalker::BfBaseClassWalker(BfType* typeA, BfType* typeB, BfModule* module)
{
	mMayBeFromInterface = false;

	if (typeB == typeA)
		typeB = NULL;

	if ((typeA != NULL) && (!typeA->IsInterface()))
		mTypes[0] = typeA->ToTypeInstance();
	else
		mTypes[0] = NULL;

	if ((typeB != NULL) && (!typeB->IsInterface()))
		mTypes[1] = typeB->ToTypeInstance();
	else
		mTypes[1] = NULL;

	if ((typeA != NULL) && (typeA->IsGenericParam()))
	{
		mMayBeFromInterface = true;
		AddConstraints(typeA, module->GetGenericParamInstance((BfGenericParamType*)typeA));
	}

	if ((typeB != NULL) && (typeB->IsGenericParam()))
	{
		mMayBeFromInterface = true;
		AddConstraints(typeB, module->GetGenericParamInstance((BfGenericParamType*)typeB));
	}
}

/*BfBaseClassWalker::BfBaseClassWalker(BfTypeInstance* typeA, BfTypeInstance* typeB)
{
	mTypes[0] = typeA;
	mTypes[1] = typeB;
}*/

BfBaseClassWalker::BfBaseClassWalker()
{
	mMayBeFromInterface = false;
	mTypes[0] = NULL;
	mTypes[1] = NULL;
}

void BfBaseClassWalker::AddConstraints(BfType* srcType, BfGenericParamInstance* genericParam)
{
	if (genericParam->mTypeConstraint != NULL)
	{
		auto typeInst = genericParam->mTypeConstraint->ToTypeInstance();
		{
			Entry entry(srcType, typeInst);
			if ((typeInst != NULL) && (!mManualList.Contains(entry)))
				mManualList.Add(entry);
		}
	}

	for (auto typeInst : genericParam->mInterfaceConstraints)
	{
		Entry entry(srcType, typeInst);
		if ((typeInst != NULL) && (!mManualList.Contains(entry)))
			mManualList.Add(entry);
	}
}

BfBaseClassWalker::Entry BfBaseClassWalker::Next()
{
	if (!mManualList.IsEmpty())
	{
		auto entry = mManualList.back();
		mManualList.pop_back();
		return entry;
	}

	// Check the most specific type instance first (highest inheritance level)
	auto checkInstance = mTypes[0];
	if (mTypes[0] == NULL)
		checkInstance = mTypes[1];
	else if ((mTypes[1] != NULL) && (mTypes[1]->mInheritDepth > mTypes[0]->mInheritDepth))
		checkInstance = mTypes[1];
	if (checkInstance == NULL)
		return Entry();

	// Do it this was so if we reach the same base class for both types that we only handle each base type once
	if (checkInstance == mTypes[0])
		mTypes[0] = checkInstance->mBaseType;
	if (checkInstance == mTypes[1])
		mTypes[1] = checkInstance->mBaseType;

	Entry entry;
	entry.mSrcType = checkInstance;
	entry.mTypeInstance = checkInstance;
	return entry;
}

//////////////////////////////////////////////////////////////////////////

BfMethodMatcher::BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, const StringImpl& methodName, SizedArrayImpl<BfResolvedArg>& arguments, const BfMethodGenericArguments& methodGenericArguments) :
	mArguments(arguments)
{
	mTargetSrc = targetSrc;
	mModule = module;
	mMethodName = methodName;
	Init(/*arguments, */methodGenericArguments);
}

BfMethodMatcher::BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* interfaceMethodInstance, SizedArrayImpl<BfResolvedArg>& arguments, const BfMethodGenericArguments& methodGenericArguments) :
	mArguments(arguments)
{
	mTargetSrc = targetSrc;
	mModule = module;
	Init(/*arguments, */methodGenericArguments);
	mInterfaceMethodInstance = interfaceMethodInstance;
	mMethodName = mInterfaceMethodInstance->mMethodDef->mName;
}

void BfMethodMatcher::Init(const BfMethodGenericArguments& methodGenericArguments)
{
	//mArguments = arguments;
	mUsingLists = NULL;
	mActiveTypeDef = NULL;
	mBestMethodDef = NULL;
	mBackupMethodDef = NULL;
	mBackupMatchKind = BackupMatchKind_None;
	mBestRawMethodInstance = NULL;
	mBestMethodTypeInstance = NULL;
	mExplicitInterfaceCheck = NULL;
	mSelfType = NULL;
	mMethodType = BfMethodType_Normal;
	mCheckReturnType = NULL;
	mHasArgNames = false;
	mHadExplicitGenericArguments = false;
	mHadOpenGenericArguments = methodGenericArguments.mIsOpen;
	mHadPartialGenericArguments = methodGenericArguments.mIsPartial;
	mHasVarArguments = false;
	mInterfaceMethodInstance = NULL;
	mFakeConcreteTarget = false;
	mBypassVirtual = false;
	mAllowStatic = true;
	mAllowNonStatic = true;
	mSkipImplicitParams = false;
	mAllowImplicitThis = false;
	mAllowImplicitRef = false;
	mAllowImplicitWrap = false;
	mHadVarConflictingReturnType = false;
	mAutoFlushAmbiguityErrors = true;
	mMethodCheckCount = 0;
	mCheckedKind = BfCheckedKind_NotSet;
	mMatchFailKind = MatchFailKind_None;
	mBfEvalExprFlags = BfEvalExprFlags_None;

	for (auto& arg : mArguments)
	{
		auto bfType = arg.mTypedValue.mType;
		if (bfType != NULL)
		{
			mHasVarArguments |= bfType->IsVar();
			if (bfType->IsGenericParam())
			{
				auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)bfType);
				if ((genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
					mHasVarArguments = true;
			}
		}

		if (arg.mNameNode != NULL)
			mHasArgNames = true;
	}

	if (methodGenericArguments.mArguments != NULL)
	{
		for (BfAstNode* genericArg : *(methodGenericArguments.mArguments))
		{
			BfType* genericArgType = NULL;
			if (BfNodeIsA<BfUninitializedExpression>(genericArg))
			{
				// Allow a null here
				BF_ASSERT(mHadPartialGenericArguments);
			}
			else
			{
				genericArgType = mModule->ResolveTypeRef(genericArg, NULL, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowImplicitConstExpr);
				if ((genericArgType != NULL) && (genericArgType->IsGenericParam()))
				{
					auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)genericArgType);
					if ((genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
						mHasVarArguments = true;
				}
			}
			mExplicitMethodGenericArguments.push_back(genericArgType);
		}
		mHadExplicitGenericArguments = true;
	}
}

bool BfMethodMatcher::IsMemberAccessible(BfTypeInstance* typeInst, BfTypeDef* declaringType)
{
	if (!declaringType->mIsPartial)
		return true;

	if (mActiveTypeDef == NULL)
		mActiveTypeDef = mModule->GetActiveTypeDef();

	// Note that mActiveTypeDef does not pose a constraint here
	if (!typeInst->IsTypeMemberIncluded(declaringType, mActiveTypeDef, mModule))
		return false;

	auto visibleProjectSet = mModule->GetVisibleProjectSet();
	if ((visibleProjectSet != NULL) && (!typeInst->IsTypeMemberAccessible(declaringType, visibleProjectSet)))
	{
		return false;
	}

	return true;
}

bool BfGenericInferContext::AddToCheckedSet(BfType* argType, BfType* wantType)
{
	int64 idPair = ((int64)argType->mTypeId << 32) | (wantType->mTypeId);
	return mCheckedTypeSet.Add(idPair);
}

bool BfGenericInferContext::InferGenericArgument(BfMethodInstance* methodInstance, BfType* argType, BfType* wantType, BfIRValue argValue, bool checkCheckedSet)
{
	if (argType == NULL)
		return false;

	if (!wantType->IsUnspecializedType())
		return true;

	if (checkCheckedSet)
	{
		if (!AddToCheckedSet(argType, wantType))
			return true;
	}

	if (wantType->IsGenericParam())
	{
		auto wantGenericParam = (BfGenericParamType*)wantType;

		BfType* methodGenericTypeConstraint = NULL;
		auto _SetGeneric = [&]()
		{
			if (argType != NULL)
			{
				// Disallow illegal types
				if (argType->IsRef())
					return;
				if (argType->IsNull())
					return;
			}

			if ((*mCheckMethodGenericArguments)[wantGenericParam->mGenericParamIdx] != argType)
			{
				if (methodGenericTypeConstraint != NULL)
				{
					if (methodGenericTypeConstraint->IsGenericTypeInstance())
					{
						auto wantGenericType = (BfTypeInstance*)methodGenericTypeConstraint;

						auto checkArgType = argType;
						while (checkArgType != NULL)
						{
							if (checkArgType->IsGenericTypeInstance())
							{
								auto argGenericType = (BfTypeInstance*)checkArgType;
								if (argGenericType->mTypeDef->GetLatest() == wantGenericType->mTypeDef->GetLatest())
								{
									for (int genericArgIdx = 0; genericArgIdx < (int)argGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
										InferGenericArgument(methodInstance, argGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx], wantGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx], BfIRValue(), true);
								}
							}
							else if (checkArgType->IsSizedArray())
							{
								auto sizedArrayType = (BfSizedArrayType*)checkArgType;
								if (wantGenericType->IsInstanceOf(mModule->mCompiler->mSizedArrayTypeDef))
								{
									InferGenericArgument(methodInstance, sizedArrayType->mElementType, wantGenericType->mGenericTypeInfo->mTypeGenericArguments[0], BfIRValue());
									auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
									BfTypedValue arraySize = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)sizedArrayType->mElementCount), intType);
									InferGenericArgument(methodInstance, mModule->CreateConstExprValueType(arraySize), wantGenericType->mGenericTypeInfo->mTypeGenericArguments[1], BfIRValue(), true);
								}
							}
							else if (checkArgType->IsPointer())
							{
								auto pointerType = (BfPointerType*)checkArgType;
								if (wantGenericType->IsInstanceOf(mModule->mCompiler->mPointerTTypeDef))
								{
									InferGenericArgument(methodInstance, pointerType->mElementType, wantGenericType->mGenericTypeInfo->mTypeGenericArguments[0], BfIRValue(), true);
								}
							}

							auto checkTypeInst = checkArgType->ToTypeInstance();
							if ((checkTypeInst == NULL) || (!checkTypeInst->mHasParameterizedBase))
								break;
							checkArgType = checkTypeInst->mBaseType;
						}
					}
				}
			}
			if ((*mCheckMethodGenericArguments)[wantGenericParam->mGenericParamIdx] == NULL)
				mInferredCount++;
			(*mCheckMethodGenericArguments)[wantGenericParam->mGenericParamIdx] = argType;
			if (!mPrevArgValues.IsEmpty())
				mPrevArgValues[wantGenericParam->mGenericParamIdx] = argValue;
		};

		if (argType->IsVar())
		{
			_SetGeneric();
			return true;
		}

		if (wantGenericParam->mGenericParamKind == BfGenericParamKind_Method)
		{
			auto genericParamInst = methodInstance->mMethodInfoEx->mGenericParams[wantGenericParam->mGenericParamIdx];
			methodGenericTypeConstraint = genericParamInst->mTypeConstraint;
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
			{
				if (argValue.IsConst())
				{
					argType = mModule->CreateConstExprValueType(BfTypedValue(argValue, argType));
				}
				else if (!argType->IsConstExprValue())
				{
					return false;
				}
			}

			if (argType == mModule->mContext->mBfObjectType)
			{
				if ((genericParamInst->mTypeConstraint != NULL) && (genericParamInst->mTypeConstraint->IsDelegate()))
				{
					argType = mModule->ResolveGenericType(genericParamInst->mTypeConstraint, NULL, mCheckMethodGenericArguments, mModule->mCurTypeInstance);
					if (argType == NULL)
						return true;
				}
			}

			if (mPrevArgValues.IsEmpty())
			{
				_SetGeneric();
				return true;
			}

			auto prevGenericMethodArg = (*mCheckMethodGenericArguments)[wantGenericParam->mGenericParamIdx];
			auto prevArgValue = mPrevArgValues[wantGenericParam->mGenericParamIdx];
			if (prevGenericMethodArg == NULL)
			{
				_SetGeneric();
				return true;
			}

			if ((prevGenericMethodArg->IsIntUnknown()) && (!argType->IsIntUnknown()))
			{
				// Old int fits into new argType, that's good
				if (mModule->CanCast(BfTypedValue(prevArgValue, prevGenericMethodArg), argType))
				{
					_SetGeneric();
					return true;
				}
				// Doesn't fit, upgrade type to 'int'
				argType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				if (mModule->CanCast(BfTypedValue(prevArgValue, prevGenericMethodArg), argType))
				{
					_SetGeneric();
					return true;
				}
			}

			if (argType->IsIntUnknown())
			{
				// New int fits into previous arg type, that's good
				if (mModule->CanCast(BfTypedValue(argValue, argType), prevGenericMethodArg))
					return true;
				// Doesn't fit, upgrade type to 'int'
				argType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			}
			else
			{
				// Prev is already best
				if (mModule->CanCast(mModule->GetFakeTypedValue(argType), prevGenericMethodArg))
					return true;
			}

			// New best?
			if (mModule->CanCast(mModule->GetFakeTypedValue(prevGenericMethodArg), argType))
			{
				_SetGeneric();
				return true;
			}

			// No implicit conversion, FAIL!
			(*mCheckMethodGenericArguments)[wantGenericParam->mGenericParamIdx] = NULL;
			return false;
		}
		return true;
	}

	if (wantType->IsTuple())
	{
		if (argType->IsTuple())
		{
			auto wantTupleType = (BfTupleType*)wantType;
			auto argTupleType = (BfTupleType*)argType;

			if (wantTupleType->mFieldInstances.size() == argTupleType->mFieldInstances.size())
			{
				for (int fieldIdx = 0; fieldIdx < (int)wantTupleType->mFieldInstances.size(); fieldIdx++)
				{
					InferGenericArgument(methodInstance, argTupleType->mFieldInstances[fieldIdx].mResolvedType,
						wantTupleType->mFieldInstances[fieldIdx].mResolvedType, BfIRValue());
				}
			}
		}
	}

	if ((wantType->IsGenericTypeInstance()) && (wantType->IsUnspecializedTypeVariation()))
	{
		auto wantGenericType = (BfTypeInstance*)wantType;
		if (argType->IsGenericParam())
		{
			auto genericParam = mModule->GetGenericParamInstance((BfGenericParamType*)argType);
			if ((genericParam->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				InferGenericArgument(methodInstance, mModule->GetPrimitiveType(BfTypeCode_Var), wantType, BfIRValue());
				return true;
			}

			if ((genericParam->mTypeConstraint != NULL) && (genericParam->mTypeConstraint->IsGenericTypeInstance()))
				InferGenericArgument(methodInstance, genericParam->mTypeConstraint, wantType, BfIRValue());
		}

		if (argType->IsVar())
		{
			for (int genericArgIdx = 0; genericArgIdx < (int)wantGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
			{
				BfType* wantGenericArgument = wantGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx];
				if (!wantGenericArgument->IsUnspecializedType())
					continue;
				InferGenericArgument(methodInstance, mModule->GetPrimitiveType(BfTypeCode_Var), wantGenericArgument, BfIRValue());
			}
			return true;
		}

		auto typeInstance = argType->ToTypeInstance();
		if (typeInstance == NULL)
			return true;
		if (wantGenericType->IsInterface())
		{
			for (auto& ifaceEntry : typeInstance->mInterfaces)
				InferGenericArgument(methodInstance, ifaceEntry.mInterfaceType, wantType, BfIRValue());
		}
		else if (typeInstance->mBaseType != NULL)
			InferGenericArgument(methodInstance, typeInstance->mBaseType, wantType, BfIRValue());

		if (!argType->IsGenericTypeInstance())
			return true;
		auto argGenericType = (BfTypeInstance*)argType;
		if (argGenericType->mTypeDef->GetDefinition() != wantGenericType->mTypeDef->GetDefinition())
			return true;

		for (int genericArgIdx = 0; genericArgIdx < (int)argGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
		{
			BfType* wantGenericArgument = wantGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx];
			if (!wantGenericArgument->IsUnspecializedType())
				continue;
			InferGenericArgument(methodInstance, argGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx], wantGenericArgument, BfIRValue(), true);
		}
		return true;
	}

	if (wantType->IsRef())
	{
		auto wantRefType = (BfRefType*)wantType;
		if (!argType->IsRef())
		{
			// Match to non-ref
			InferGenericArgument(methodInstance, argType, wantRefType->mElementType, BfIRValue());
			return true;
		}
		auto argRefType = (BfRefType*)argType;
		//TODO: We removed this check so we still infer even if we have the wrong ref kind
		//if (wantRefType->mRefKind != argRefType->mRefKind)
			//return true;
		return InferGenericArgument(methodInstance, argRefType->mElementType, wantRefType->mElementType, BfIRValue());
	}

	if (wantType->IsPointer())
	{
		if (!argType->IsPointer())
			return true;
		auto wantPointerType = (BfPointerType*) wantType;
		auto argPointerType = (BfPointerType*) argType;
		return InferGenericArgument(methodInstance, argPointerType->mElementType, wantPointerType->mElementType, BfIRValue());
	}

	if (wantType->IsUnknownSizedArrayType())
	{
		auto wantArrayType = (BfUnknownSizedArrayType*)wantType;
		if (argType->IsUnknownSizedArrayType())
		{
			auto argArrayType = (BfUnknownSizedArrayType*)argType;
			InferGenericArgument(methodInstance, argArrayType->mElementCountSource, wantArrayType->mElementCountSource, BfIRValue());
		}
		else if (argType->IsSizedArray())
		{
			auto argArrayType = (BfSizedArrayType*)argType;
			BfTypedValue sizeValue(mModule->GetConstValue(argArrayType->mElementCount), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			auto sizedType = mModule->CreateConstExprValueType(sizeValue);
			InferGenericArgument(methodInstance, sizedType, wantArrayType->mElementCountSource, BfIRValue());
		}
	}

	if (wantType->IsSizedArray())
	{
		if (argType->IsSizedArray())
		{
			auto wantArrayType = (BfSizedArrayType*)wantType;
			auto argArrayType = (BfSizedArrayType*)argType;
			InferGenericArgument(methodInstance, argArrayType->mElementType, wantArrayType->mElementType, BfIRValue());
		}
	}

	if ((wantType->IsDelegate()) || (wantType->IsFunction()))
	{
		if (((argType->IsDelegate()) || (argType->IsFunction())) &&
			(wantType->IsDelegate() == argType->IsDelegate()))
		{
			if (!AddToCheckedSet(argType, wantType))
				return true;

			auto argInvokeMethod = mModule->GetRawMethodByName(argType->ToTypeInstance(), "Invoke");
			auto wantInvokeMethod = mModule->GetRawMethodByName(wantType->ToTypeInstance(), "Invoke");

			if ((argInvokeMethod != NULL) && (wantInvokeMethod != NULL) && (argInvokeMethod->GetParamCount() == wantInvokeMethod->GetParamCount()))
			{
				InferGenericArgument(methodInstance, argInvokeMethod->mReturnType, wantInvokeMethod->mReturnType, BfIRValue());
				for (int argIdx = 0; argIdx < (int)argInvokeMethod->GetParamCount(); argIdx++)
					InferGenericArgument(methodInstance, argInvokeMethod->GetParamType(argIdx), wantInvokeMethod->GetParamType(argIdx), BfIRValue());
			}
		}
		else if (argType->IsMethodRef())
		{
 			auto methodTypeRef = (BfMethodRefType*)argType;
			if (!AddToCheckedSet(argType, wantType))
				return true;

			auto argInvokeMethod = methodTypeRef->mMethodRef;
			auto delegateInfo = wantType->GetDelegateInfo();
			auto wantInvokeMethod = mModule->GetRawMethodByName(wantType->ToTypeInstance(), "Invoke");

			if ((delegateInfo->mHasExplicitThis) && (argInvokeMethod->HasThis()))
				InferGenericArgument(methodInstance, argInvokeMethod->GetParamType(-1), delegateInfo->mParams[0], BfIRValue());

			int wantInvokeOffset = delegateInfo->mHasExplicitThis ? 1 : 0;
			if ((argInvokeMethod != NULL) && (wantInvokeMethod != NULL) && (argInvokeMethod->GetParamCount() == wantInvokeMethod->GetParamCount() - wantInvokeOffset))
			{
				InferGenericArgument(methodInstance, argInvokeMethod->mReturnType, wantInvokeMethod->mReturnType, BfIRValue());
				for (int argIdx = 0; argIdx < (int)argInvokeMethod->GetParamCount(); argIdx++)
					InferGenericArgument(methodInstance, argInvokeMethod->GetParamType(argIdx), wantInvokeMethod->GetParamType(argIdx + wantInvokeOffset), BfIRValue());
			}
		}
	}

	return true;
}

bool BfGenericInferContext::InferGenericArguments(BfMethodInstance* methodInstance, int srcGenericIdx)
{
	auto& srcGenericArg = (*mCheckMethodGenericArguments)[srcGenericIdx];
	if (srcGenericArg == NULL)
		return false;

	int startInferCount = mInferredCount;

	auto srcGenericParam = methodInstance->mMethodInfoEx->mGenericParams[srcGenericIdx];
	for (auto ifaceConstraint : srcGenericParam->mInterfaceConstraints)
	{
		if ((ifaceConstraint->IsUnspecializedTypeVariation()) && (ifaceConstraint->IsGenericTypeInstance()))
		{
			InferGenericArgument(methodInstance, srcGenericArg, ifaceConstraint, BfIRValue());
			auto typeInstance = srcGenericArg->ToTypeInstance();
			if ((typeInstance == NULL) && (srcGenericArg->IsWrappableType()))
				typeInstance = mModule->GetWrappedStructType(srcGenericArg);

			if (typeInstance != NULL)
			{
				for (auto ifaceEntry : typeInstance->mInterfaces)
					InferGenericArgument(methodInstance, ifaceEntry.mInterfaceType, ifaceConstraint, BfIRValue());
			}

			if (srcGenericArg->IsGenericParam())
			{
				auto genericParamType = (BfGenericParamType*)srcGenericArg;

				BfGenericParamInstance* genericParam = NULL;
				if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
					genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
				else
					genericParam = mModule->GetGenericParamInstance(genericParamType);

				if (genericParam->mTypeConstraint != NULL)
					InferGenericArgument(methodInstance, genericParam->mTypeConstraint, ifaceConstraint, BfIRValue());
				for (auto argIfaceConstraint : genericParam->mInterfaceConstraints)
					InferGenericArgument(methodInstance, argIfaceConstraint, ifaceConstraint, BfIRValue());
			}
		}
	}

	return mInferredCount != startInferCount;
}

void BfGenericInferContext::InferGenericArguments(BfMethodInstance* methodInstance)
{
	// Attempt to infer from other generic args
	for (int srcGenericIdx = 0; srcGenericIdx < (int)mCheckMethodGenericArguments->size(); srcGenericIdx++)
	{
		InferGenericArguments(methodInstance, srcGenericIdx);
	}
}

int BfMethodMatcher::GetMostSpecificType(BfType* lhs, BfType* rhs)
{
	if ((lhs->IsRef()) && (rhs->IsRef()))
		return GetMostSpecificType(lhs->GetUnderlyingType(), rhs->GetUnderlyingType());

	if ((lhs->IsPointer()) && (rhs->IsPointer()))
		return GetMostSpecificType(lhs->GetUnderlyingType(), rhs->GetUnderlyingType());

	if ((lhs->IsSizedArray()) && (rhs->IsSizedArray()))
		return GetMostSpecificType(lhs->GetUnderlyingType(), rhs->GetUnderlyingType());

	if (lhs->IsGenericParam())
		return rhs->IsGenericParam() ? -1 : 1;
	if (rhs->IsGenericParam())
		return 0;

	if ((lhs->IsUnspecializedType()) && (lhs->IsGenericTypeInstance()))
	{
		if ((!rhs->IsUnspecializedType()) || (!rhs->IsGenericTypeInstance()))
			return 1;

		auto lhsTypeInst = lhs->ToTypeInstance();
		auto rhsTypeInst = rhs->ToTypeInstance();

		if ((rhsTypeInst == NULL) || (lhsTypeInst == NULL) || (lhsTypeInst->mTypeDef != rhsTypeInst->mTypeDef))
			return -1;

		bool hadLHSMoreSpecific = false;
		bool hadRHSMoreSpecific = false;

		for (int generigArgIdx = 0; generigArgIdx < (int)lhsTypeInst->mGenericTypeInfo->mTypeGenericArguments.size(); generigArgIdx++)
		{
			int argMoreSpecific = GetMostSpecificType(lhsTypeInst->mGenericTypeInfo->mTypeGenericArguments[generigArgIdx],
				rhsTypeInst->mGenericTypeInfo->mTypeGenericArguments[generigArgIdx]);
			if (argMoreSpecific == 0)
				hadLHSMoreSpecific = true;
			else if (argMoreSpecific == 1)
				hadRHSMoreSpecific = true;
		}

		if ((hadLHSMoreSpecific) && (!hadRHSMoreSpecific))
			return 0;
		if ((hadRHSMoreSpecific) && (!hadLHSMoreSpecific))
			return 1;
		return -1;
	}
	if (rhs->IsUnspecializedType())
		return 0;

	return -1;
}

void BfMethodMatcher::CompareMethods(BfMethodInstance* prevMethodInstance, BfTypeVector* prevGenericArgumentsSubstitute,
	BfMethodInstance* newMethodInstance, BfTypeVector* genericArgumentsSubstitute,
	bool* outNewIsBetter, bool* outNewIsWorse, bool allowSpecializeFail)
{
	if (prevMethodInstance == newMethodInstance)
	{
		*outNewIsBetter = false;
		*outNewIsWorse = true;
		return;
	}

	#define SET_BETTER_OR_WORSE(lhs, rhs) \
		if ((!isBetter) && (lhs) && !(rhs)) isBetter = true; \
		if ((!isWorse) && !(lhs) && (rhs)) isWorse = true;

	#define RETURN_BETTER_OR_WORSE(lhs, rhs) \
 		if ((!isBetter) && (lhs) && !(rhs)) { *outNewIsBetter = true; *outNewIsWorse = false; return; } \
 		if ((!isWorse) && !(lhs) && (rhs)) { *outNewIsBetter = false; *outNewIsWorse = true; return; };

	#define RETURN_RESULTS \
		*outNewIsBetter = isBetter; \
		*outNewIsWorse = isWorse; \
		return;

 	int numUsedParams = 0;
	int prevNumUsedParams = 0;
	bool usedExtendedForm = false;
	bool prevUsedExtendedForm = false;

	bool isBetter = false;
	bool isWorse = false;
	int argIdx;

	BfMethodDef* prevMethodDef = prevMethodInstance->mMethodDef;
	BfMethodDef* newMethodDef = newMethodInstance->mMethodDef;

	if (prevMethodDef == mBackupMethodDef)
	{
		// This can happen for extension methods and such
		*outNewIsBetter = true;
		*outNewIsWorse = false;
		return;
	}

	if (newMethodDef->mExplicitInterface != prevMethodDef->mExplicitInterface)
	{
		if (mModule->CompareMethodSignatures(newMethodInstance, prevMethodInstance))
		{
			SET_BETTER_OR_WORSE(newMethodDef->mExplicitInterface != NULL, prevMethodDef->mExplicitInterface != NULL);
			*outNewIsBetter = isBetter;
			*outNewIsWorse = isWorse;
			return;
		}
	}

	bool anyIsExtension = false;

	int newImplicitParamCount = newMethodInstance->GetImplicitParamCount();
	if (newMethodInstance->mMethodDef->mHasAppend)
		newImplicitParamCount++;
	if (newMethodInstance->mMethodDef->mMethodType == BfMethodType_Extension)
	{
		newImplicitParamCount++;
		anyIsExtension = true;
	}

	int prevImplicitParamCount = prevMethodInstance->GetImplicitParamCount();
	if (prevMethodInstance->mMethodDef->mHasAppend)
		prevImplicitParamCount++;
	if (prevMethodInstance->mMethodDef->mMethodType == BfMethodType_Extension)
	{
		prevImplicitParamCount++;
		anyIsExtension = true;
	}

	BfCastFlags implicitCastFlags = ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) ? BfCastFlags_NoConversionOperator : BfCastFlags_None;

	int newMethodParamCount = newMethodInstance->GetParamCount();
	int prevMethodParamCount = prevMethodInstance->GetParamCount();

	bool hadEnoughArgs = newMethodParamCount - newImplicitParamCount < (int)mArguments.size();
	bool prevHadEnoughArgs = prevMethodParamCount - prevImplicitParamCount < (int)mArguments.size();
	RETURN_BETTER_OR_WORSE(hadEnoughArgs, prevHadEnoughArgs);

	bool chainSkip = (newMethodInstance->mChainType == BfMethodChainType_ChainMember) || (newMethodInstance->mChainType == BfMethodChainType_ChainSkip);
	bool prevChainSkip = (prevMethodInstance->mChainType == BfMethodChainType_ChainMember) || (prevMethodInstance->mChainType == BfMethodChainType_ChainSkip);
	RETURN_BETTER_OR_WORSE(!chainSkip, !prevChainSkip);

	if ((!isBetter) && (!isWorse))
	{
		bool betterByGenericParam = false;
		bool worseByGenericParam = false;

		bool betterByConstExprParam = false;
		bool worseByConstExprParam = false;

		bool someArgWasBetter = false;
		bool someArgWasWorse = false;

		for (argIdx = anyIsExtension ? -1 : 0; argIdx < (int)mArguments.size(); argIdx++)
		{
			BfTypedValue arg;
			BfResolvedArg* resolvedArg = NULL;
			bool wasArgDeferred = false;

			if (argIdx == -1)
			{
				arg = mTarget;
			}
			else
			{
				resolvedArg = &mArguments[argIdx];
				wasArgDeferred = resolvedArg->mArgFlags != 0;
				arg = resolvedArg->mTypedValue;
			}

			int newArgIdx = argIdx + newImplicitParamCount;
			int prevArgIdx = argIdx + prevImplicitParamCount;

			if (newArgIdx >= newMethodParamCount)
				break;
			if (prevArgIdx >= prevMethodParamCount)
				break;

 			bool wasGenericParam = (newArgIdx >= 0) && newMethodInstance->WasGenericParam(newArgIdx);
 			bool prevWasGenericParam = (prevArgIdx >= 0) && prevMethodInstance->WasGenericParam(prevArgIdx);

			BfType* paramType = newMethodInstance->GetParamType(newArgIdx, true);
			BfType* prevParamType = prevMethodInstance->GetParamType(prevArgIdx, true);

			numUsedParams++;
			prevNumUsedParams++;

			BfType* origParamType = paramType;
			BfType* origPrevParamType = prevParamType;

			bool paramWasConstExpr = false;
			bool prevParamWasConstExpr = false;

			bool paramWasUnspecialized = paramType->IsUnspecializedType();
			if ((genericArgumentsSubstitute != NULL) && (paramWasUnspecialized))
			{
				paramType = mModule->ResolveGenericType(paramType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance, allowSpecializeFail);
				paramType = mModule->FixIntUnknown(paramType);
			}
			if (paramType->IsConstExprValue())
			{
				paramWasConstExpr = true;
				paramType = ((BfConstExprValueType*)paramType)->mType;
			}

			bool prevParamWasUnspecialized = prevParamType->IsUnspecializedType();
			if ((prevGenericArgumentsSubstitute != NULL) && (prevParamWasUnspecialized))
			{
				prevParamType = mModule->ResolveGenericType(prevParamType, NULL, prevGenericArgumentsSubstitute, mModule->mCurTypeInstance, allowSpecializeFail);
				prevParamType = mModule->FixIntUnknown(prevParamType);
			}
			if (prevParamType->IsConstExprValue())
			{
				prevParamWasConstExpr = true;
				prevParamType = ((BfConstExprValueType*)prevParamType)->mType;
			}

			bool paramsEquivalent = paramType == prevParamType;

			if ((prevParamType == NULL) || (paramType == NULL))
			{
				SET_BETTER_OR_WORSE(paramType != NULL, prevParamType != NULL);
			}
			else if (paramType != prevParamType)
			{
				bool isUnspecializedParam = paramType->IsUnspecializedType();
				bool isPrevUnspecializedParam = prevParamType->IsUnspecializedType();
				SET_BETTER_OR_WORSE((!isUnspecializedParam) && (!paramType->IsVar()),
					(!isPrevUnspecializedParam) && (!prevParamType->IsVar()));

				if ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp_Explicit) != 0)
				{
					// Pick the one that can implicitly cast
					SET_BETTER_OR_WORSE(
						mModule->CanCast(arg, paramType, implicitCastFlags),
						mModule->CanCast(arg, prevParamType, implicitCastFlags));
				}

				// Why did we have this !isUnspecializedParam check? We need the 'canCast' logic still
				if ((!isBetter) && (!isWorse) /*&& (!isUnspecializedParam) && (!isPrevUnspecializedParam)*/)
				{
					SET_BETTER_OR_WORSE((paramType != NULL) && (!paramType->IsUnspecializedType()),
						(prevParamType != NULL) && (!prevParamType->IsUnspecializedType()));
					if ((!isBetter) && (!isWorse))
					{
						// The resolved argument type may actually match for both considered functions. IE:
						// Method(int8 val) and Method(int16 val) called with Method(0) will create arguments that match their param types
						if ((!wasArgDeferred) && (!wasGenericParam) && (IsType(arg, paramType)) && ((resolvedArg == NULL) || (prevParamType != resolvedArg->mBestBoundType)))
							isBetter = true;
						//else if ((!prevWasGenericParam) && (IsType(arg, prevParamType)) && (!IsType(arg, paramType)))
						else if ((!wasArgDeferred) && (!prevWasGenericParam) && (IsType(arg, prevParamType)) && ((resolvedArg == NULL) || (paramType != resolvedArg->mBestBoundType)))
							isWorse = true;
						else
						{
							bool canCastFromCurToPrev = mModule->CanCast(mModule->GetFakeTypedValue(paramType), prevParamType, implicitCastFlags);
							bool canCastFromPrevToCur = mModule->CanCast(mModule->GetFakeTypedValue(prevParamType), paramType, implicitCastFlags);

							if ((canCastFromCurToPrev) && (canCastFromPrevToCur))
								paramsEquivalent = true;

							if ((canCastFromCurToPrev) && (!canCastFromPrevToCur))
								isBetter = true;
							else if ((canCastFromPrevToCur) && (!canCastFromCurToPrev))
								isWorse = true;
							else if ((paramType->IsIntegral()) && (prevParamType->IsIntegral()))
							{
								if (paramType == arg.mType)
									isBetter = true;
								else if (prevParamType == arg.mType)
									isWorse = true;
								else
								{
									if (paramType->mSize < prevParamType->mSize)
										isBetter = true;
									else if (paramType->mSize > prevParamType->mSize)
										isWorse = true;
									else if (paramType->IsSigned())
										isBetter = true;
									else
										isWorse = true;
								}
							}
							else if ((wasArgDeferred) && ((paramType->IsIntegral()) || (prevParamType->IsIntegral())))
							{
								SET_BETTER_OR_WORSE(paramType->IsIntegral(), prevParamType->IsIntegral());
							}
						}
					}
				}
			}

			if ((!isBetter) && (!isWorse) && (paramsEquivalent))
			{
				if ((origParamType != origPrevParamType) && (paramWasConstExpr) && (!prevParamWasConstExpr))
					betterByConstExprParam = true;
				else if ((origParamType != origPrevParamType) && (!paramWasConstExpr) && (prevParamWasConstExpr))
					worseByConstExprParam = true;
				else if (((paramWasUnspecialized) || (prevParamWasUnspecialized)))
				{
					int origTypeMoreSpecific = GetMostSpecificType(origParamType, origPrevParamType);
					if (origTypeMoreSpecific == 0)
						betterByGenericParam = true;
					else if (origTypeMoreSpecific == 1)
						worseByGenericParam = true;
				}
			}

			if ((newArgIdx >= 0) && (newMethodInstance->GetParamKind(newArgIdx) == BfParamKind_Params))
				usedExtendedForm = true;
			if ((prevArgIdx >= 0) && (prevMethodInstance->GetParamKind(prevArgIdx) == BfParamKind_Params))
				prevUsedExtendedForm = true;

			if ((usedExtendedForm) || (prevUsedExtendedForm))
				break;

			someArgWasBetter |= isBetter;
			someArgWasWorse |= isWorse;
			isBetter = false;
			isWorse = false;
		}
		isBetter |= someArgWasBetter;
		isWorse |= someArgWasWorse;

		if ((!isBetter) && (!isWorse))
		{
			// Don't allow ambiguity
			if ((betterByGenericParam && !worseByGenericParam) ||
				(!betterByGenericParam && worseByGenericParam))
			{
				isBetter = betterByGenericParam;
				isWorse = worseByGenericParam;
			}
		}

		if ((!isBetter) && (!isWorse))
		{
			// Don't allow ambiguity
			if ((betterByConstExprParam && !worseByConstExprParam) ||
				(!betterByConstExprParam && worseByConstExprParam))
			{
				isBetter = betterByConstExprParam;
				isWorse = worseByConstExprParam;
			}
		}

		if ((isBetter) || (isWorse))
		{
			RETURN_RESULTS;
		}
	}

	// Choose by return type for conversion operators
	if (((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) && (!isBetter) && (!isWorse))
	{
		auto returnType = newMethodInstance->mReturnType;
		auto prevReturnType = prevMethodInstance->mReturnType;

		if ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp_Explicit) != 0)
		{
			// Pick the one that can implicitly cast
			SET_BETTER_OR_WORSE(
				mModule->CanCast(mModule->GetFakeTypedValue(returnType), mCheckReturnType, implicitCastFlags),
				mModule->CanCast(mModule->GetFakeTypedValue(prevReturnType), mCheckReturnType, implicitCastFlags));
		}

		if ((!isBetter) && (!isWorse))
		{
			bool canCastFromCurToPrev = mModule->CanCast(mModule->GetFakeTypedValue(returnType), prevReturnType, implicitCastFlags);
			bool canCastFromPrevToCur = mModule->CanCast(mModule->GetFakeTypedValue(prevReturnType), returnType, implicitCastFlags);

			if ((canCastFromCurToPrev) && (!canCastFromPrevToCur))
				isWorse = true;
			else if ((canCastFromPrevToCur) && (!canCastFromCurToPrev))
				isBetter = true;
		}

		if ((isBetter) || (isWorse))
		{
			RETURN_RESULTS;
		}
	}

	// Check for unused extended params as next param - that still counts as using extended form
	usedExtendedForm = newMethodInstance->HasParamsArray();
	prevUsedExtendedForm = prevMethodInstance->HasParamsArray();

	RETURN_BETTER_OR_WORSE(newMethodInstance->GetNumGenericArguments() == 0, prevMethodInstance->GetNumGenericArguments() == 0);

	// Not using generic delegate params is better
	RETURN_BETTER_OR_WORSE(!newMethodInstance->mHadGenericDelegateParams, !prevMethodInstance->mHadGenericDelegateParams);

	// Normal form trumps extended form
	RETURN_BETTER_OR_WORSE(!usedExtendedForm, !prevUsedExtendedForm);

	// More used params trumps less params
	int paramDiff = (int) numUsedParams - (int) prevNumUsedParams;
	RETURN_BETTER_OR_WORSE(paramDiff > 0, paramDiff < 0);

	// Fewer defaults trumps more defaults
	// Since we know the number of used params is the same (previous check), we infer that the rest are defaults
	paramDiff = (int) newMethodInstance->GetParamCount() - (int) prevMethodInstance->GetParamCount();
	RETURN_BETTER_OR_WORSE(paramDiff < 0, paramDiff > 0);

	BfMethodInstance* typeUnspecNewMethodInstance = mModule->GetUnspecializedMethodInstance(newMethodInstance, true);
	BfMethodInstance* typeUnspecPrevMethodInstance = mModule->GetUnspecializedMethodInstance(prevMethodInstance, true);

	// Check specificity of args

	std::function<void(BfType*, BfType*)> _CompareParamTypes = [&](BfType* newType, BfType* prevType)
	{
		if ((newType->IsGenericParam()) && (prevType->IsGenericParam()))
		{
			auto newGenericParamType = (BfGenericParamType*)newType;
			auto prevGenericParamType = (BfGenericParamType*)prevType;
			if ((newGenericParamType->mGenericParamKind == BfGenericParamKind_Method) && (prevGenericParamType->mGenericParamKind == BfGenericParamKind_Method))
			{
				auto newMethodGenericParam = typeUnspecNewMethodInstance->mMethodInfoEx->mGenericParams[newGenericParamType->mGenericParamIdx];
				auto prevMethodGenericParam = typeUnspecPrevMethodInstance->mMethodInfoEx->mGenericParams[prevGenericParamType->mGenericParamIdx];
				SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
			}
		}
		else if (newType == prevType)
		{
			if ((newType->IsUnspecializedType()) && (newType->IsGenericTypeInstance()))
			{
				BfTypeInstance* newGenericType = (BfTypeInstance*)newType;
				BfTypeInstance* prevGenericType = (BfTypeInstance*)prevType;

				for (int genericArgIdx = 0; genericArgIdx < (int)newGenericType->mGenericTypeInfo->mTypeGenericArguments.size(); genericArgIdx++)
				{
					_CompareParamTypes(newGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx], prevGenericType->mGenericTypeInfo->mTypeGenericArguments[genericArgIdx]);
				}
			}
		}
		else
		{
			//TODO: Why did we need this?
			//isBetter |= mModule->IsTypeMoreSpecific(newType, prevType);
			//isWorse |= mModule->IsTypeMoreSpecific(prevType, newType);
		}
	};

	int paramCheckCount = (int)BF_MIN(newMethodInstance->GetParamCount() - newImplicitParamCount, prevMethodInstance->GetParamCount() - prevImplicitParamCount);
	for (argIdx = 0; argIdx < (int)paramCheckCount/*mArguments.size()*/; argIdx++)
	{
		int newArgIdx = argIdx + newImplicitParamCount;
		int prevArgIdx = argIdx + prevImplicitParamCount;
		_CompareParamTypes(typeUnspecNewMethodInstance->GetParamType(newArgIdx), typeUnspecPrevMethodInstance->GetParamType(prevArgIdx));
	}

	// Do generic constraint subset test directly to handle cases like "NotDisposed<T>()" vs "NotDisposed<T>() where T : IDisposable"
	if ((newMethodInstance->GetNumGenericArguments() > 0) && (newMethodInstance->GetNumGenericArguments() == prevMethodInstance->GetNumGenericArguments()))
	{
		for (int genericParamIdx = 0; genericParamIdx < (int)newMethodInstance->GetNumGenericArguments(); genericParamIdx++)
		{
			auto newMethodGenericParam = newMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			auto prevMethodGenericParam = prevMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
		}

		if ((!isBetter) && (!isWorse))
		{
			SET_BETTER_OR_WORSE(newMethodInstance->HasExternConstraints(), prevMethodInstance->HasExternConstraints());
		}
	}

	if ((isBetter) || (isWorse))
	{
		RETURN_RESULTS;
	}

	// For operators, prefer explicit comparison over '<=>' comparison operator
	if ((!isBetter) && (!isWorse))
	{
		if ((prevMethodDef->mIsOperator) && (newMethodDef->mIsOperator))
		{
			bool newIsComparison = ((BfOperatorDeclaration*)newMethodDef->mMethodDeclaration)->mBinOp == BfBinaryOp_Compare;
			bool prevIsComparison = ((BfOperatorDeclaration*)prevMethodDef->mMethodDeclaration)->mBinOp == BfBinaryOp_Compare;
			RETURN_BETTER_OR_WORSE(!newIsComparison, !prevIsComparison);
		}
	}

	if ((newMethodInstance->mMethodDef->mExternalConstraints.size() != 0) || (prevMethodInstance->mMethodDef->mExternalConstraints.size() != 0))
	{
		struct GenericParamPair
		{
			BfGenericMethodParamInstance* mParams[2];
			GenericParamPair()
			{
				mParams[0] = NULL;
				mParams[1] = NULL;
			}
		};

		Dictionary<BfType*, GenericParamPair> externConstraints;

		auto _GetParams = [&](int idx, BfMethodInstance* methodInstance)
		{
			for (int externConstraintIdx = 0; externConstraintIdx < (int)methodInstance->mMethodDef->mExternalConstraints.size(); externConstraintIdx++)
			{
				auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[methodInstance->mMethodDef->mGenericParams.size() + externConstraintIdx];
				BF_ASSERT(genericParam->mExternType != NULL);
				GenericParamPair* pairPtr = NULL;
				externConstraints.TryAdd(genericParam->mExternType, NULL, &pairPtr);
				pairPtr->mParams[idx] = genericParam;
			}
		};

		_GetParams(0, newMethodInstance);
		_GetParams(1, prevMethodInstance);

		for (auto kv : externConstraints)
		{
			SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(kv.mValue.mParams[1], kv.mValue.mParams[0]), mModule->AreConstraintsSubset(kv.mValue.mParams[0], kv.mValue.mParams[1]));
		}

		if ((isBetter) || (isWorse))
		{
			RETURN_RESULTS;
		}
	}

	// Does one have a body and one doesn't?  Obvious!
	isBetter = prevMethodDef->IsEmptyPartial();
	isWorse = newMethodDef->IsEmptyPartial();
	if ((isBetter) && (isWorse))
	{
		// If both are empty partials then just bind to either
		isWorse = true;
		RETURN_RESULTS;
	}

	// For extensions, select the version in the most-specific project (only applicable for ctors)
	if ((!isBetter) && (!isWorse))
	{
		auto newProject = newMethodDef->mDeclaringType->mProject;
		auto prevProject = prevMethodDef->mDeclaringType->mProject;
		if (newProject != prevProject)
		{
			RETURN_BETTER_OR_WORSE(newProject->ContainsReference(prevProject), prevProject->ContainsReference(newProject));
		}
	}

	// If we have conditional type extensions that both define an implementation for a method, use the most-specific conditional extension constraints
	auto owner = newMethodInstance->GetOwner();
	if ((newMethodDef->mDeclaringType != prevMethodDef->mDeclaringType) && (owner->IsGenericTypeInstance()))
	{
		auto genericOwner = (BfTypeInstance*)owner;
		if (genericOwner->mGenericTypeInfo->mGenericExtensionInfo != NULL)
		{
			BfGenericExtensionEntry* newGenericExtesionEntry = NULL;
			BfGenericExtensionEntry* prevGenericExtesionEntry = NULL;
			if ((genericOwner->mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(newMethodDef->mDeclaringType, &newGenericExtesionEntry)) &&
				(genericOwner->mGenericTypeInfo->mGenericExtensionInfo->mExtensionMap.TryGetValue(prevMethodDef->mDeclaringType, &prevGenericExtesionEntry)))
			{
				if ((newGenericExtesionEntry->mGenericParams.size() == prevGenericExtesionEntry->mGenericParams.size()))
				{
					for (int genericParamIdx = 0; genericParamIdx < (int)newGenericExtesionEntry->mGenericParams.size(); genericParamIdx++)
					{
						auto newMethodGenericParam = newGenericExtesionEntry->mGenericParams[genericParamIdx];
						auto prevMethodGenericParam = prevGenericExtesionEntry->mGenericParams[genericParamIdx];
						SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
					}
				}

				if ((isBetter) || (isWorse))
				{
					RETURN_RESULTS;
				}
			}
		}
	}

	RETURN_BETTER_OR_WORSE(newMethodDef->mCheckedKind == mCheckedKind, prevMethodDef->mCheckedKind == mCheckedKind);
	RETURN_BETTER_OR_WORSE(newMethodDef->mCommutableKind != BfCommutableKind_Reverse, prevMethodDef->mCommutableKind != BfCommutableKind_Reverse);

	// If one of these methods is local to the current extension then choose that one
	auto activeDef = mModule->GetActiveTypeDef();
	RETURN_BETTER_OR_WORSE(newMethodDef->mDeclaringType == activeDef, prevMethodDef->mDeclaringType == activeDef);
	RETURN_BETTER_OR_WORSE(newMethodDef->mDeclaringType->IsExtension(), prevMethodDef->mDeclaringType->IsExtension());
	RETURN_BETTER_OR_WORSE(newMethodDef->mIsMutating, prevMethodDef->mIsMutating);
	if (newMethodDef->mHasComptime != prevMethodDef->mHasComptime)
	{
		bool isComptime = (mModule->mIsComptimeModule) || ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0);
		RETURN_BETTER_OR_WORSE(newMethodDef->mHasComptime == isComptime, prevMethodDef->mHasComptime == isComptime);
	}

	RETURN_RESULTS;
}

BfTypedValue BfMethodMatcher::ResolveArgTypedValue(BfResolvedArg& resolvedArg, BfType* checkType, BfTypeVector* genericArgumentsSubstitute, BfType *origCheckType, BfResolveArgFlags flags)
{
	BfTypedValue argTypedValue = resolvedArg.mTypedValue;
	if ((resolvedArg.mArgFlags & BfArgFlag_DelegateBindAttempt) != 0)
	{
		BfExprEvaluator exprEvaluator(mModule);
		exprEvaluator.mExpectingType = checkType;
		BF_ASSERT(resolvedArg.mExpression->IsA<BfDelegateBindExpression>());
		auto delegateBindExpr = BfNodeDynCast<BfDelegateBindExpression>(resolvedArg.mExpression);
		BfMethodInstance* boundMethodInstance = NULL;

		auto bindType = checkType;
		if ((bindType == NULL) && (origCheckType != NULL) && (!origCheckType->IsUnspecializedTypeVariation()))
			bindType = checkType;
		if (exprEvaluator.CanBindDelegate(delegateBindExpr, &boundMethodInstance, bindType, genericArgumentsSubstitute))
		{
			if (delegateBindExpr->mNewToken == NULL)
			{
				if (boundMethodInstance->GetOwner()->IsFunction())
				{
					return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), boundMethodInstance->GetOwner());
				}
				else if ((boundMethodInstance->mDisallowCalling) || ((flags & BfResolveArgFlag_FromGeneric) == 0))
				{
					argTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), checkType);
				}
				else
				{
					resolvedArg.mExpectedType = checkType;
					auto methodRefType = mModule->CreateMethodRefType(boundMethodInstance);
					mModule->AddDependency(methodRefType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
					mModule->AddCallDependency(boundMethodInstance);
					argTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), methodRefType);
				}
			}
			else
				argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
		}
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_LambdaBindAttempt) != 0)
	{
		if ((argTypedValue) && (argTypedValue.mType->IsMethodRef()) &&
			((checkType == NULL) || (!checkType->IsMethodRef())))
		{
			// This may be from a previous checkMethod, clear it out
			argTypedValue = BfTypedValue();
		}

		BfExprEvaluator exprEvaluator(mModule);
		exprEvaluator.mExpectingType = checkType;
		BF_ASSERT(resolvedArg.mExpression->IsA<BfLambdaBindExpression>());
		auto lambdaBindExpr = (BfLambdaBindExpression*)resolvedArg.mExpression;

		if ((checkType != NULL) && (checkType->IsDelegate()))
		{
			BfMethodInstance* methodInstance = mModule->GetRawMethodInstanceAtIdx(checkType->ToTypeInstance(), 0, "Invoke");
			if (methodInstance != NULL)
			{
				if (methodInstance->GetParamCount() == (int)lambdaBindExpr->mParams.size())
				{
					if (lambdaBindExpr->mNewToken == NULL)
					{
						if (!resolvedArg.mTypedValue)
						{
							// Resolve for real
							resolvedArg.mTypedValue = mModule->CreateValueFromExpression(lambdaBindExpr, checkType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_NoAutoComplete));
						}
						argTypedValue = resolvedArg.mTypedValue;
					}
					else
						argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
						//argTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), checkType);
				}
			}
		}
		else if ((checkType == NULL) && (origCheckType != NULL) && (origCheckType->IsUnspecializedTypeVariation()) && (genericArgumentsSubstitute != NULL) && (origCheckType->IsDelegateOrFunction()))
		{
			BfMethodInstance* methodInstance = mModule->GetRawMethodInstanceAtIdx(origCheckType->ToTypeInstance(), 0, "Invoke");
			if (methodInstance != NULL)
			{
				if ((methodInstance->mReturnType->IsGenericParam()) && (((BfGenericParamType*)methodInstance->mReturnType)->mGenericParamKind == BfGenericParamKind_Method))
				{
					bool isValid = true;

					int returnMethodGenericArgIdx = ((BfGenericParamType*)methodInstance->mReturnType)->mGenericParamIdx;
					if ((*genericArgumentsSubstitute)[returnMethodGenericArgIdx] != NULL)
					{
						isValid = false;
					}

					if (methodInstance->mParams.size() != (int)lambdaBindExpr->mParams.size())
						isValid = false;

					for (auto& param : methodInstance->mParams)
					{
						if (param.mResolvedType->IsGenericParam())
						{
							auto genericParamType = (BfGenericParamType*)param.mResolvedType;
							if ((genericParamType->mGenericParamKind == BfGenericParamKind_Method) && ((*genericArgumentsSubstitute)[genericParamType->mGenericParamIdx] == NULL))
							{
								isValid = false;
							}
						}
					}

					if (isValid)
					{
						bool success = false;

						(*genericArgumentsSubstitute)[returnMethodGenericArgIdx] = mModule->GetPrimitiveType(BfTypeCode_None);
						auto tryType = mModule->ResolveGenericType(origCheckType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance);
						if (tryType != NULL)
						{
							auto inferredReturnType = mModule->CreateValueFromExpression(lambdaBindExpr, tryType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_InferReturnType | BfEvalExprFlags_NoAutoComplete));
							if (inferredReturnType.mType != NULL)
							{
								(*genericArgumentsSubstitute)[returnMethodGenericArgIdx] = inferredReturnType.mType;

								if (((flags & BfResolveArgFlag_FromGenericParam) != 0) && (lambdaBindExpr->mNewToken == NULL))
								{
									auto resolvedType = mModule->ResolveGenericType(origCheckType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance);
									if (resolvedType != NULL)
									{
										// Resolve for real
										resolvedArg.mTypedValue = mModule->CreateValueFromExpression(lambdaBindExpr, resolvedType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_NoAutoComplete));
										argTypedValue = resolvedArg.mTypedValue;
									}
								}

								success = true;
							}
						}

						if (!success)
						{
							// Put back
							(*genericArgumentsSubstitute)[returnMethodGenericArgIdx] = NULL;
						}
					}
				}
			}
		}
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_UnqualifiedDotAttempt) != 0)
	{
		if ((checkType != NULL) && (checkType->IsPayloadEnum()))
		{
			// Should we actually check the member name?
			argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
		}
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_UntypedDefault) != 0)
	{
		if (checkType != NULL)
			argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_DeferredEval) != 0)
	{
		if (resolvedArg.mExpression != NULL)
		{
			if ((resolvedArg.mExpectedType != checkType) || (resolvedArg.mExpectedType == NULL)) // Did our last check match for this type?
			{
				SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
				SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);

				bool prevNoBind = false;
				if (mModule->mCurMethodState != NULL)
				{
					prevNoBind = mModule->mCurMethodState->mNoBind;
					mModule->mCurMethodState->mNoBind = true;
				}

				auto prevBlock = mModule->mBfIRBuilder->GetInsertBlock();

				BfExprEvaluator exprEvaluator(mModule);
				exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags);
				exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowIntUnknown | BfEvalExprFlags_NoAutoComplete);
				if ((resolvedArg.mArgFlags & BfArgFlag_ParamsExpr) != 0)
					exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowParamsExpr);

				auto expectType = checkType;
				if (expectType != NULL)
				{
					if (expectType->IsRef())
					{
						auto refType = (BfRefType*)expectType;
						expectType = refType->mElementType;
					}

					if ((genericArgumentsSubstitute != NULL) && (expectType->IsUnspecializedType()))
						expectType = mModule->ResolveGenericType(expectType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance, true);
				}

				exprEvaluator.mExpectingType = expectType;
				exprEvaluator.Evaluate(resolvedArg.mExpression);
				argTypedValue = exprEvaluator.GetResult();
				if ((argTypedValue) && (argTypedValue.mType->IsVar()))
					argTypedValue = BfTypedValue();

				if (mModule->mCurMethodState != NULL)
					mModule->mCurMethodState->mNoBind = prevNoBind;

				if ((argTypedValue) && (!argTypedValue.mType->IsVar()))
				{
					if (checkType != NULL)
						resolvedArg.mExpectedType = checkType;

					auto storeTypedValue = argTypedValue;
					if ((storeTypedValue.mValue) & (!storeTypedValue.mValue.IsFake()))
					{
						// We actually want to ensure that this cached value is a fake val.  There are potential cases where a fake val
						//  won't be generated but we should throw an error, so we need to make sure we actually re-evaluate when the call
						//  is generated
						//storeTypedValue.mValue = mModule->mBfIRBuilder->GetFakeVal();

						resolvedArg.mWantsRecalc = true;
					}
					resolvedArg.mTypedValue = storeTypedValue;

					//BF_ASSERT(argTypedValue.mValue.mId != -1);
				}

				mModule->mBfIRBuilder->SetInsertPoint(prevBlock);
			}
		}
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_VariableDeclaration) != 0)
	{
		if ((checkType != NULL) && (checkType->IsRef()))
			argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
	}

	return argTypedValue;
}

bool BfMethodMatcher::WantsCheckMethod(BfProtectionCheckFlags& flags, BfTypeInstance* startTypeInstance, BfTypeInstance* checkTypeInstance, BfMethodDef* checkMethod)
{
	MatchFailKind matchFailKind = MatchFailKind_None;
	if (!mModule->CheckProtection(flags, checkTypeInstance, checkMethod->mDeclaringType->mProject, checkMethod->mProtection, startTypeInstance))
	{
		if ((mBypassVirtual) &&
			((checkMethod->mProtection == BfProtection_Protected) || (checkMethod->mProtection == BfProtection_ProtectedInternal)) &&
			(mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, startTypeInstance)))
		{
			// Allow explicit 'base' call
		}
		else
		{
			return false;
		}
	}

	if (mCheckedKind != checkMethod->mCheckedKind)
	{
		bool passes = true;
		if (mCheckedKind != BfCheckedKind_NotSet)
		{
			passes = false;
		}
		else
		{
			auto defaultCheckedKind = mModule->GetDefaultCheckedKind();
			if (defaultCheckedKind != checkMethod->mCheckedKind)
				passes = false;
		}
		if (!passes)
		{
			return false;
		}
	}

	return true;
}

bool BfMethodMatcher::InferFromGenericConstraints(BfMethodInstance* methodInstance, BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs)
{
	if (!genericParamInst->mExternType->IsGenericParam())
		return false;

	auto genericParamType = (BfGenericParamType*)genericParamInst->mExternType;
	if (genericParamType->mGenericParamKind != BfGenericParamKind_Method)
		return false;

	BfType* checkArgType = NULL;

	for (auto& checkOpConstraint : genericParamInst->mOperatorConstraints)
	{
		auto leftType = checkOpConstraint.mLeftType;
		if ((leftType != NULL) && (leftType->IsUnspecializedType()))
			leftType = mModule->ResolveGenericType(leftType, NULL, methodGenericArgs, mModule->mCurTypeInstance);
		if (leftType != NULL)
			leftType = mModule->FixIntUnknown(leftType);

		auto rightType = checkOpConstraint.mRightType;
		if ((rightType != NULL) && (rightType->IsUnspecializedType()))
			rightType = mModule->ResolveGenericType(rightType, NULL, methodGenericArgs, mModule->mCurTypeInstance);
		if (rightType != NULL)
			rightType = mModule->FixIntUnknown(rightType);

		BfConstraintState constraintSet;
		constraintSet.mPrevState = mModule->mContext->mCurConstraintState;
		constraintSet.mGenericParamInstance = genericParamInst;
		constraintSet.mLeftType = leftType;
		constraintSet.mRightType = rightType;
		SetAndRestoreValue<BfConstraintState*> prevConstraintSet(mModule->mContext->mCurConstraintState, &constraintSet);
		if (!mModule->CheckConstraintState(NULL))
			return false;

		if (checkOpConstraint.mBinaryOp != BfBinaryOp_None)
		{
			if ((leftType == NULL) || (rightType == NULL))
				continue;

			BfExprEvaluator exprEvaluator(mModule);

			BfTypedValue leftValue(mModule->mBfIRBuilder->GetFakeVal(), leftType);
			BfTypedValue rightValue(mModule->mBfIRBuilder->GetFakeVal(), rightType);

			//
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
				SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
				exprEvaluator.PerformBinaryOperation(NULL, NULL, checkOpConstraint.mBinaryOp, NULL, BfBinOpFlag_NoClassify, leftValue, rightValue);
			}

			if (exprEvaluator.mResult)
				checkArgType = exprEvaluator.mResult.mType;
		}
		else
		{
			if (rightType == NULL)
				continue;

			BfTypedValue rightValue(mModule->mBfIRBuilder->GetFakeVal(), rightType);

			StringT<128> failedOpName;

			if (checkOpConstraint.mCastToken == BfToken_Implicit)
			{
			}
			else
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
				SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);

				if (checkOpConstraint.mCastToken == BfToken_Explicit)
				{
				}
				else
				{
					BfExprEvaluator exprEvaluator(mModule);
					exprEvaluator.mResult = rightValue;
					exprEvaluator.PerformUnaryOperation(NULL, checkOpConstraint.mUnaryOp, NULL, BfUnaryOpFlag_IsConstraintCheck);

					if (exprEvaluator.mResult)
						checkArgType = exprEvaluator.mResult.mType;
				}
			}
		}
	}

	if ((checkArgType == NULL) && ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_ComptypeExpr) != 0))
	{
		for (auto comptypeConstraint : genericParamInst->mComptypeConstraint)
		{
			checkArgType = mModule->ResolveGenericMethodTypeRef(comptypeConstraint, methodInstance, genericParamInst, methodGenericArgs);
			if (checkArgType == NULL)
				return false;
		}
	}

	if (checkArgType == NULL)
		return false;
	if (checkArgType->IsVar())
		return false;

	(*methodGenericArgs)[genericParamType->mGenericParamIdx] = checkArgType;
	return true;
}

bool BfMethodMatcher::CheckMethod(BfTypeInstance* targetTypeInstance, BfTypeInstance* typeInstance, BfMethodDef* checkMethod, bool isFailurePass)
{
	BP_ZONE("BfMethodMatcher::CheckMethod");

	BackupMatchKind curMatchKind = BackupMatchKind_None;
	bool hadMatch = false;

	// Never consider overrides - they only get found at original method declaration
	//  mBypassVirtual gets set when we are doing an explicit "base" call, or when we are a struct --
	//  because on structs we know the exact type
	if ((checkMethod->mIsOverride) && (!mBypassVirtual) && (!typeInstance->IsValueType()))
		return false;

	mMethodCheckCount++;

	BfMethodInstance* methodInstance = mModule->GetRawMethodInstance(typeInstance, checkMethod);
	if (methodInstance == NULL)
		return false;
	BfMethodInstance* typeUnspecMethodInstance = mModule->GetUnspecializedMethodInstance(methodInstance, true);
	BfTypeVector* typeGenericArguments = NULL;
	if (typeInstance->mGenericTypeInfo != NULL)
		typeGenericArguments = &typeInstance->mGenericTypeInfo->mTypeGenericArguments;

	if ((mInterfaceMethodInstance != NULL) && (methodInstance->GetExplicitInterface() != NULL))
	{
		BfTypeInstance* wantInterface = mInterfaceMethodInstance->mMethodInstanceGroup->mOwner;
		if (wantInterface != methodInstance->GetExplicitInterface())
			return false;
	}

	if ((checkMethod->mIsVirtual) && (!checkMethod->mIsOverride) && (!mBypassVirtual) &&
		(targetTypeInstance != NULL) && (targetTypeInstance->IsObject()))
	{
		mModule->PopulateType(targetTypeInstance, BfPopulateType_DataAndMethods);
		if ((methodInstance->mVirtualTableIdx < targetTypeInstance->mVirtualMethodTable.mSize) && (methodInstance->mVirtualTableIdx >= 0))
		{
			BfVirtualMethodEntry& vEntry = targetTypeInstance->mVirtualMethodTable[methodInstance->mVirtualTableIdx];
			if ((vEntry.mImplementingMethod.mTypeInstance != NULL) && (vEntry.mImplementingMethod.mTypeInstance->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted) &&
				(mModule->mCompiler->IsAutocomplete()))
			{
				// Silently ignore
			}
			else
			{
				auto implMethod = (BfMethodInstance*)vEntry.mImplementingMethod;
				if ((implMethod != methodInstance) && (implMethod != NULL))
				{
					SetAndRestoreValue<bool> prevBypassVirtual(mBypassVirtual, true);
					return CheckMethod(targetTypeInstance, implMethod->GetOwner(), implMethod->mMethodDef, isFailurePass);
				}
			}
		}
		else
		{
			// Being in autocomplete mode is the only excuse for not having the virtual method table slotted
			if ((!mModule->mCompiler->IsAutocomplete()) && (!targetTypeInstance->mTypeFailed) && (!targetTypeInstance->IsUnspecializedTypeVariation()))
			{
				mModule->AssertErrorState();
			}
		}
	}

	BfGenericInferContext genericInferContext;
	genericInferContext.mModule = mModule;
	genericInferContext.mCheckMethodGenericArguments = &mCheckMethodGenericArguments;

	HashSet<int> allowEmptyGenericSet;
	BfAutoComplete* autoComplete = NULL;
	if ((mModule->mCompiler->mResolvePassData != NULL) && (!isFailurePass) && ((mBfEvalExprFlags & BfEvalExprFlags_NoAutoComplete) == 0))
		autoComplete = mModule->mCompiler->mResolvePassData->mAutoComplete;

	if (checkMethod->mMethodType != BfMethodType_Extension)
	{
		if (((checkMethod->mIsStatic) && (!mAllowStatic)) ||
			((!checkMethod->mIsStatic) && (!mAllowNonStatic)))
		{
			if (!typeInstance->IsFunction())
				autoComplete = NULL;
		}
	}

	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		BfAutoComplete::MethodMatchEntry methodMatchEntry;
		methodMatchEntry.mMethodDef = checkMethod;
		methodMatchEntry.mTypeInstance = typeInstance;
		methodMatchEntry.mCurMethodInstance = mModule->mCurMethodInstance;
 		autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);
	}

	BfTypeVector* genericArgumentsSubstitute = NULL;
	int argIdx = 0;
	int argMatchCount = 0;

	bool needInferGenericParams = (checkMethod->mGenericParams.size() != 0) &&
		((!mHadExplicitGenericArguments) || (mHadPartialGenericArguments));
	int paramIdx = 0;
	BfType* paramsElementType = NULL;
	if (checkMethod->mHasAppend)
		paramIdx++;

	int uniqueGenericStartIdx = mModule->GetLocalInferrableGenericArgCount(checkMethod);

	if (mHadExplicitGenericArguments)
	{
		int genericArgDelta = (int)(checkMethod->mGenericParams.size() - (mExplicitMethodGenericArguments.size() + uniqueGenericStartIdx));
		if (mHadOpenGenericArguments)
		{
			if (genericArgDelta <= 0)
				goto NoMatch;
		}
		else
		{
			if (genericArgDelta != 0)
				goto NoMatch;
		}
	}

	for (auto& checkGenericArgRef : mCheckMethodGenericArguments)
		checkGenericArgRef = NULL;

	mCheckMethodGenericArguments.resize(checkMethod->mGenericParams.size());

	for (auto& genericArgRef : mCheckMethodGenericArguments)
		genericArgRef = NULL;

	if (mHadExplicitGenericArguments)
	{
		if (uniqueGenericStartIdx > 0)
		{
			genericArgumentsSubstitute = &mCheckMethodGenericArguments;
			mCheckMethodGenericArguments.clear();

			mCheckMethodGenericArguments.reserve(mExplicitMethodGenericArguments.size() + uniqueGenericStartIdx);
			for (int i = 0; i < uniqueGenericStartIdx; i++)
				mCheckMethodGenericArguments.Add(NULL);
			for (int i = 0; i < (int)mExplicitMethodGenericArguments.size(); i++)
				mCheckMethodGenericArguments.Add(mExplicitMethodGenericArguments[i]);
		}
		else
		{
			genericArgumentsSubstitute = &mExplicitMethodGenericArguments;
		}

		if ((mHadPartialGenericArguments) && (needInferGenericParams))
		{
			genericArgumentsSubstitute = &mCheckMethodGenericArguments;
			for (int i = 0; i < (int)mExplicitMethodGenericArguments.mSize; i++)
				mCheckMethodGenericArguments[i] = mExplicitMethodGenericArguments[i];
		}
	}
	else if (needInferGenericParams)
		genericArgumentsSubstitute = &mCheckMethodGenericArguments;

	if (mHasArgNames)
	{
		checkMethod->BuildParamNameMap();

		bool prevWasNull = false;
		for (int argIdx = (int)mArguments.mSize - 1; argIdx >= 0; argIdx--)
		{
			auto& arg = mArguments[argIdx];
			if (arg.mNameNode != NULL)
			{
				if (prevWasNull)
				{
					if (argIdx >= checkMethod->mParams.mSize)
						goto NoMatch;
					if (checkMethod->mParams[argIdx]->mName != arg.mNameNode->ToStringView())
						goto NoMatch;
				}
				else
				{
					if (!checkMethod->mParamNameMap->ContainsKey(arg.mNameNode->ToStringView()))
						goto NoMatch;
				}

				prevWasNull = false;
			}
			else
			{
				prevWasNull = true;
			}
		}
	}

	if ((checkMethod->mIsMutating) && (targetTypeInstance != NULL) && (targetTypeInstance->IsValueType()) &&
		((mTarget.IsReadOnly()) || (!mTarget.IsAddr())) &&
		(!targetTypeInstance->IsValuelessType()))
	{
		goto NoMatch;
	}

	if (mSkipImplicitParams)
	{
		//paramOfs = methodInstance->GetImplicitParamCount();
		//paramIdx += paramOfs;
	}

	if (needInferGenericParams)
	{
		genericInferContext.mPrevArgValues.resize(checkMethod->mGenericParams.size());

		int paramOfs = methodInstance->GetImplicitParamCount();
		int paramCount = methodInstance->GetParamCount();
		SizedArray<int, 8> deferredArgs;

		int argIdx = 0;
		int paramIdx = 0;

		if (checkMethod->mHasAppend)
			paramIdx++;

		if (checkMethod->mMethodType == BfMethodType_Extension)
		{
			argIdx--;
		}
		paramIdx += paramOfs;

		bool hadInferFailure = false;
		int inferParamOffset = paramOfs - argIdx;
		int paramsParamIdx = -1;

		enum ResultKind
		{
			ResultKind_Ok,
			ResultKind_Failed,
			ResultKind_Deferred,
		};

		auto _CheckArg = [&](int argIdx)
		{
			paramIdx = argIdx + inferParamOffset;
			if ((paramsParamIdx != -1) && (paramIdx > paramsParamIdx))
				paramIdx = paramsParamIdx;

			auto wantType = methodInstance->GetParamType(paramIdx);

			auto checkType = wantType;
			auto origCheckType = checkType;

			if (checkType->IsGenericParam())
			{
				BfGenericParamInstance* genericParamInstance = NULL;
				auto genericParamType = (BfGenericParamType*)checkType;
				checkType = NULL;

				if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
				{
					if ((genericArgumentsSubstitute != NULL) && (genericParamType->mGenericParamIdx < (int)genericArgumentsSubstitute->size()))
						checkType = (*genericArgumentsSubstitute)[genericParamType->mGenericParamIdx];
					genericParamInstance = methodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
				}
				else
					genericParamInstance = mModule->GetGenericParamInstance(genericParamType);
				if (checkType == NULL)
				{
					checkType = genericParamInstance->mTypeConstraint;
					origCheckType = checkType; // We can do "ResolveGenericType" on this type
				}
			}

			bool attemptedGenericResolve = false;
			if ((checkType != NULL) && (genericArgumentsSubstitute != NULL) && (checkType->IsUnspecializedType()))
			{
				attemptedGenericResolve = true;
				checkType = mModule->ResolveGenericType(origCheckType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance);
			}

			if (wantType->IsUnspecializedType())
			{
				BfTypedValue argTypedValue;
				if (argIdx == -1)
				{
					if (mOrigTarget)
						argTypedValue = mOrigTarget;
					else
						argTypedValue = mTarget;
				}
				else
				{
					BfResolveArgFlags flags = BfResolveArgFlag_FromGeneric;
					if (wantType->IsGenericParam())
						flags = (BfResolveArgFlags)(flags | BfResolveArgFlag_FromGenericParam);
					argTypedValue = ResolveArgTypedValue(mArguments[argIdx], checkType, genericArgumentsSubstitute, origCheckType, flags);
				}
				if (!argTypedValue.IsUntypedValue())
				{
					auto type = argTypedValue.mType;
					if (!argTypedValue)
					{
						if ((checkType == NULL) && (attemptedGenericResolve) && (genericInferContext.GetUnresolvedCount() >= 2))
						{
							deferredArgs.Add(argIdx);
							return ResultKind_Deferred;
						}

						return ResultKind_Failed;
					}

					if (type->IsVar())
						mHasVarArguments = true;

					if (methodInstance->GetParamKind(paramIdx) == BfParamKind_Params)
					{
						paramsParamIdx = paramIdx;
						if ((wantType->IsArray()) || (wantType->IsSizedArray()) || (wantType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef)))
							wantType = wantType->GetUnderlyingType();
					}

					if (!genericInferContext.InferGenericArgument(methodInstance, type, wantType, argTypedValue.mValue))
						return ResultKind_Failed;
				}
			}
			return ResultKind_Ok;
		};

		for (; argIdx < (int)mArguments.size(); argIdx++)
		{
			paramIdx = argIdx + inferParamOffset;
			if ((paramIdx >= paramCount) && (paramsParamIdx == -1))
				break; // Possible for delegate 'params' type methods

			auto resultKind = _CheckArg(argIdx);
			if (resultKind == ResultKind_Failed)
				goto NoMatch;
		}

		if ((methodInstance->mParams.mSize > 0) && (methodInstance->GetParamKind(methodInstance->mParams.mSize - 1) == BfParamKind_Params))
		{
			// Handle `params int[C]` generic sized array params case
			auto paramsType = methodInstance->GetParamType(methodInstance->mParams.mSize - 1);
			if (paramsType->IsUnknownSizedArrayType())
			{
				auto unknownSizedArray = (BfUnknownSizedArrayType*)paramsType;
				if (unknownSizedArray->mElementCountSource->IsMethodGenericParam())
				{
					auto genericParam = (BfGenericParamType*)unknownSizedArray->mElementCountSource;
					if ((*genericArgumentsSubstitute)[genericParam->mGenericParamIdx] == NULL)
					{
						int paramsCount = (int)mArguments.mSize - inferParamOffset;
						(*genericArgumentsSubstitute)[genericParam->mGenericParamIdx] = mModule->CreateConstExprValueType(
							BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, paramsCount), mModule->GetPrimitiveType(BfTypeCode_IntPtr)));
					}
				}
			}
		}

		if (!deferredArgs.IsEmpty())
		{
			genericInferContext.InferGenericArguments(methodInstance);
		}

		while (!deferredArgs.IsEmpty())
		{
			int prevDeferredSize = (int)deferredArgs.size();
			for (int i = 0; i < prevDeferredSize; i++)
			{
				auto resultKind = _CheckArg(deferredArgs[i]);
				if (resultKind == ResultKind_Failed)
					goto NoMatch;
			}
			deferredArgs.RemoveRange(0, prevDeferredSize);
			if (prevDeferredSize == deferredArgs.size())
			{
				// No progress
				goto NoMatch;
			}
		}

		//
		{
			int paramIdx = (int)mArguments.size() + paramOfs;
			while (paramIdx < checkMethod->mParams.size())
			{
				if ((paramIdx < methodInstance->mDefaultValues.size()) && (methodInstance->mDefaultValues[paramIdx]))
				{
					auto wantType = methodInstance->GetParamType(paramIdx);
					auto checkType = wantType;
					if (checkType->IsGenericParam())
					{
						BfGenericParamInstance* genericParamInstance = NULL;
						auto genericParamType = (BfGenericParamType*)checkType;
						if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
						{
							if ((genericArgumentsSubstitute != NULL) && (genericParamType->mGenericParamIdx < (int)genericArgumentsSubstitute->size()))
								checkType = (*genericArgumentsSubstitute)[genericParamType->mGenericParamIdx];
							genericParamInstance = methodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
							if ((genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
							{
								if (mCheckMethodGenericArguments[genericParamType->mGenericParamIdx] == NULL)
									allowEmptyGenericSet.Add(genericParamType->mGenericParamIdx);
							}
						}
					}
				}

				paramIdx++;
			}
		}

		if ((mCheckReturnType != NULL) && (methodInstance->mReturnType->IsUnspecializedType()))
		{
			if (!genericInferContext.InferGenericArgument(methodInstance, mCheckReturnType, methodInstance->mReturnType, BfIRValue()))
				return ResultKind_Failed;
		}

		bool failed = false;
		bool inferredAllGenericArguments = false;
		for (int pass = 0; true; pass++)
		{
			bool madeProgress = false;
			bool hasUninferred = false;
			failed = false;

			for (int genericArgIdx = uniqueGenericStartIdx; genericArgIdx < (int)checkMethod->mGenericParams.size(); genericArgIdx++)
			{
				auto& genericArg = mCheckMethodGenericArguments[genericArgIdx];
				if (genericArg == NULL)
				{
					auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[genericArgIdx];
					InferFromGenericConstraints(methodInstance, genericParam, &mCheckMethodGenericArguments);
					if (genericArg != NULL)
					{
						if (inferredAllGenericArguments)
							genericInferContext.InferGenericArguments(methodInstance, genericArgIdx);
						madeProgress = true;
					}
					hasUninferred = true;
					if (!allowEmptyGenericSet.Contains(genericArgIdx))
						failed = true;
				}
			}

			if (!hasUninferred)
				break;
			if (inferredAllGenericArguments)
			{
				if (!madeProgress)
					break;
			}
			genericInferContext.InferGenericArguments(methodInstance);
			inferredAllGenericArguments = true;
		}
		if (failed)
			goto NoMatch;
	}

	if (checkMethod->mMethodType == BfMethodType_Extension)
		argIdx--;

	// Iterate through params
	while (true)
	{
		// Too many arguments
		if (paramIdx >= (int)methodInstance->GetParamCount())
		{
			break;
		}

		bool isDeferredEval = false;

		if ((argIdx >= 0) && (methodInstance->GetParamKind(paramIdx) == BfParamKind_Params) && (paramsElementType == NULL))
		{
			if (argIdx >= (int) mArguments.size())
				break; // No params

			BfTypedValue argTypedValue = ResolveArgTypedValue(mArguments[argIdx], NULL, genericArgumentsSubstitute);
			if (!argTypedValue)
				goto NoMatch;

			if ((!argTypedValue.HasType()) && (!mArguments[argIdx].IsDeferredEval()))
				goto NoMatch;

			auto paramsArrayType = methodInstance->GetParamType(paramIdx);
			paramsArrayType = mModule->ResolveGenericType(paramsArrayType, NULL, genericArgumentsSubstitute, mModule->mCurTypeInstance);

			if (paramsArrayType == NULL)
				goto NoMatch;

			if ((mArguments[argIdx].mArgFlags & BfArgFlag_ParamsExpr) != 0)
			{
				// Direct-pass params
				if ((argTypedValue.IsUntypedValue()) || (mModule->CanCast(argTypedValue, paramsArrayType)))
				{
					argIdx++;
					argMatchCount++;
					paramIdx++;
					break;
				}

				goto NoMatch;
			}

			if ((paramsArrayType->IsArray()) || (paramsArrayType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef)))
			{
				paramsElementType = paramsArrayType->GetUnderlyingType();

				while (argIdx < (int)mArguments.size())
				{
					argTypedValue = ResolveArgTypedValue(mArguments[argIdx], paramsElementType, genericArgumentsSubstitute);
					if (!argTypedValue.HasType())
						goto NoMatch;
					BfCastFlags castFlags = ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) ? BfCastFlags_NoConversionOperator : BfCastFlags_None;
					if ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp_Explicit) != 0)
						castFlags = (BfCastFlags)(castFlags | BfCastFlags_Explicit);
					if (!mModule->CanCast(argTypedValue, paramsElementType, castFlags))
						goto NoMatch;
					argIdx++;
					argMatchCount++;
				}
			}
			else
				goto NoMatch;
			break;
		}

		if (methodInstance->IsImplicitCapture(paramIdx))
		{
			paramIdx++;
			continue;
		}

		if (argIdx >= (int) mArguments.size())
		{
			// We have defaults the rest of the way, so that's cool
			if (methodInstance->GetParamInitializer(paramIdx) != NULL)
				break;

			// We have unused params left over
			goto NoMatch;
		}

		auto wantType = methodInstance->GetParamType(paramIdx);
		if ((genericArgumentsSubstitute != NULL) && (wantType->IsUnspecializedType()))
		{
			wantType = typeUnspecMethodInstance->GetParamType(paramIdx);
			auto resolvedType = mModule->ResolveGenericType(wantType, typeGenericArguments, genericArgumentsSubstitute, mModule->mCurTypeInstance, false);
			if (resolvedType == NULL)
				goto NoMatch;
			wantType = resolvedType;
		}
		wantType = mModule->ResolveSelfType(wantType, typeInstance);

		if ((argIdx >= 0) && ((mArguments[argIdx].mArgFlags & BfArgFlag_ParamsExpr) != 0))
		{
			// We had a 'params' expression but this method didn't have a params slot in this parameter
			goto NoMatch;
		}

		BfTypedValue argTypedValue;
		if (argIdx == -1)
			argTypedValue = mTarget;
		else
			argTypedValue = ResolveArgTypedValue(mArguments[argIdx], wantType, genericArgumentsSubstitute);

		if (!argTypedValue.IsUntypedValue())
		{
			if (!argTypedValue.HasType())
			{
				// Check to see if this is the last argument and that it's a potential enum match
				if ((wantType->IsEnum()) && (argIdx == mArguments.size() - 1))
				{
					if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(mArguments[argIdx].mExpression))
					{
						if (memberRefExpr->mTarget == NULL)
						{
							// Is dot expression
							curMatchKind = BackupMatchKind_PartialLastArgMatch;
						}
					}
				}

				bool matches = false;
				if (wantType->IsOut())
				{
					if (auto memberRefExpr = BfNodeDynCast<BfUninitializedExpression>(mArguments[argIdx].mExpression))
						matches = true;
				}

				if (!matches)
					goto NoMatch;
			}
			else
			{
				if ((wantType->IsRef()) && (!argTypedValue.mType->IsRef()) &&
					((mAllowImplicitRef) || (wantType->IsIn())))
					wantType = wantType->GetUnderlyingType();

				BfCastFlags castFlags = ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) ? BfCastFlags_NoConversionOperator : BfCastFlags_None;
				if ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp_Explicit) != 0)
					castFlags = (BfCastFlags)(castFlags | BfCastFlags_Explicit);

				if ((mCheckReturnType != NULL) && (wantType->IsVar()))
				{
					// If we allowed this then it would allow too many matches (and allow conversion from any type during CastToValue)
					goto NoMatch;
				}

				if (!mModule->CanCast(argTypedValue, wantType, castFlags))
				{
					if ((mAllowImplicitWrap) && (argTypedValue.mType->IsWrappableType()) && (mModule->GetWrappedStructType(argTypedValue.mType) == wantType))
					{
						// Is wrapped type
					}
					else
						goto NoMatch;
				}
			}
		}

		paramIdx++;
		argIdx++;
		argMatchCount++;

		if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
		{
			auto methodMatchInfo = autoComplete->mMethodMatchInfo;
			if (!methodMatchInfo->mHadExactMatch)
			{
				bool isBetter = false;
				bool isWorse = false;
				int methodIdx = (int)methodMatchInfo->mInstanceList.size() - 1;

				if ((methodMatchInfo->mBestIdx != -1) && (methodMatchInfo->mBestIdx < (int)methodMatchInfo->mInstanceList.size()))
				{
					auto prevMethodMatchEntry = &methodMatchInfo->mInstanceList[methodMatchInfo->mBestIdx];
					if (checkMethod->mParams.size() < mArguments.size())
					{
						isWorse = true;
					}
					else if ((prevMethodMatchEntry->mMethodDef != NULL) && (prevMethodMatchEntry->mMethodDef->mParams.size() < (int) mArguments.size()))
					{
						isBetter = true;
					}
				}
			}
		}
	}

	// Too many arguments (not all incoming arguments processed)
	if (argIdx < (int)mArguments.size())
	{
		if (!methodInstance->IsVarArgs())
			goto NoMatch;
	}

	if (mCheckReturnType != NULL)
	{
		auto returnType = methodInstance->mReturnType;
		if (returnType->IsVar())
		{
			// If we allowed this then it would allow too many matches (and allow conversion to any type during CastToValue)
			goto NoMatch;
		}
		if ((genericArgumentsSubstitute != NULL) && (returnType->IsUnspecializedType()))
		{
			auto resolvedType = mModule->ResolveGenericType(returnType, typeGenericArguments, genericArgumentsSubstitute, mModule->mCurTypeInstance, false);
			if (resolvedType == NULL)
				goto NoMatch;
			returnType = resolvedType;
		}
		returnType = mModule->ResolveSelfType(returnType, typeInstance);

		BfCastFlags castFlags = ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) ? (BfCastFlags)(BfCastFlags_NoConversionOperator | BfCastFlags_NoInterfaceImpl) : BfCastFlags_None;
		if ((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp_Explicit) != 0)
			castFlags = (BfCastFlags)(castFlags | BfCastFlags_Explicit);
		if (!mModule->CanCast(mModule->GetFakeTypedValue(returnType), mCheckReturnType, castFlags))
			goto NoMatch;
	}

	if ((genericArgumentsSubstitute != NULL) && (genericArgumentsSubstitute->size() != 0))
	{
		for (int checkGenericIdx = uniqueGenericStartIdx; checkGenericIdx < (int)genericArgumentsSubstitute->size(); checkGenericIdx++)
		{
			auto& genericParams = methodInstance->mMethodInfoEx->mGenericParams;
			auto genericArg = (*genericArgumentsSubstitute)[checkGenericIdx];
			if (genericArg == NULL)
			{
				if (allowEmptyGenericSet.Contains(checkGenericIdx))
					continue;
				goto NoMatch;
			}

			if (genericArg == NULL)
				goto NoMatch;

			if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), genericArg, NULL, genericParams[checkGenericIdx], genericArgumentsSubstitute, NULL))
				goto NoMatch;
		}
	}

	for (int externConstraintIdx = 0; externConstraintIdx < (int)checkMethod->mExternalConstraints.size(); externConstraintIdx++)
	{
		auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[checkMethod->mGenericParams.size() + externConstraintIdx];
		BF_ASSERT(genericParam->mExternType != NULL);
		auto externType = genericParam->mExternType;
		BfTypeVector* externGenericArgumentsSubstitute = genericArgumentsSubstitute;

		if (externType->IsVar())
		{
			auto& externConstraint = checkMethod->mExternalConstraints[externConstraintIdx];
			if (externConstraint.mTypeRef != NULL)
			{
				externType = mModule->ResolveGenericMethodTypeRef(externConstraint.mTypeRef, methodInstance, genericParam, genericArgumentsSubstitute);
				if (externType == NULL)
					goto NoMatch;
			}
		}

		if (externType->IsGenericParam())
		{
			auto genericParamType = (BfGenericParamType*)externType;
			if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
			{
				if (genericArgumentsSubstitute != NULL)
				{
					auto genericArg = (*genericArgumentsSubstitute)[genericParamType->mGenericParamIdx];
					if (genericArg == NULL)
					{
						if (allowEmptyGenericSet.Contains(genericParamType->mGenericParamIdx))
							continue;
						goto NoMatch;
					}
					externType = genericArg;
				}
			}
		}

 		if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), externType, NULL, genericParam, externGenericArgumentsSubstitute, NULL))
 			goto NoMatch;
	}

// 	if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(checkMethod->mMethodDeclaration))
// 	{
// 		if ((methodDecl->mGenericConstraintsDeclaration != NULL) && (methodDecl->mGenericConstraintsDeclaration->mHasExpressions))
// 		{
// 			for (auto genericConstraint : methodDecl->mGenericConstraintsDeclaration->mGenericConstraints)
// 			{
// 				if (auto genericConstraintExpr = BfNodeDynCast<BfGenericConstraintExpression>(genericConstraint))
// 				{
// 					if (genericConstraintExpr->mExpression == NULL)
// 						continue;
// 					BfConstResolver constResolver(mModule);
// 					constResolver.mExpectingType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
// 					constResolver.Resolve(genericConstraintExpr->mExpression, constResolver.mExpectingType);
// 				}
// 			}
// 		}
// 	}

	// Method is applicable, check to see which method is better
	if (mBestMethodDef != NULL)
	{
		bool isBetter = false;
		bool isWorse = false;
		BfMethodInstance* prevMethodInstance = mModule->GetRawMethodInstance(mBestMethodTypeInstance, mBestMethodDef);
		bool allowSpecializeFail = mModule->mCurTypeInstance->IsUnspecializedType();
		if (mModule->mCurMethodInstance != NULL)
			allowSpecializeFail = mModule->mCurMethodInstance->mIsUnspecialized;

		CompareMethods(prevMethodInstance, &mBestMethodGenericArguments, methodInstance, genericArgumentsSubstitute, &isBetter, &isWorse, allowSpecializeFail);

		// If we had both a 'better' and 'worse', that's ambiguous because the methods are each better in different ways (not allowed)
		//  And if neither better nor worse then they are equally good, which is not allowed either
		if (((!isBetter) && (!isWorse)) || ((isBetter) && (isWorse)))
		{
			if (!mHasVarArguments)
			{
				// If we are ambiguous based on a subset of an extern 'var' constraint then don't throw an error
				auto _CheckMethodInfo = [&](BfMethodInstance* checkMethodInstance)
				{
					if (checkMethodInstance->mMethodInfoEx == NULL)
						return;
					for (auto genericParam : checkMethodInstance->mMethodInfoEx->mGenericParams)
					{
						if ((genericParam->mExternType == NULL) || (!genericParam->mExternType->IsGenericParam()))
							continue;
						auto genericParamType = (BfGenericParamType*)genericParam->mExternType;
						if (genericParamType->mGenericParamKind != BfGenericParamKind_Type)
							continue;
						auto externGenericParam = mModule->GetGenericParamInstance(genericParamType);
						if ((externGenericParam->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
							mHasVarArguments = true;
					}
				};
				_CheckMethodInfo(methodInstance);
				_CheckMethodInfo(prevMethodInstance);
			}

			if (mHasVarArguments)
			{
				if (methodInstance->mReturnType != prevMethodInstance->mReturnType)
					mHadVarConflictingReturnType = true;
			}
			else
			{
				BfAmbiguousEntry ambiguousEntry;
				ambiguousEntry.mMethodInstance = methodInstance;
				if (genericArgumentsSubstitute != NULL)
					ambiguousEntry.mBestMethodGenericArguments = *genericArgumentsSubstitute;
				if (methodInstance->mMethodDef->mGenericParams.size() != 0)
				{
					BF_ASSERT(!ambiguousEntry.mBestMethodGenericArguments.empty());
				}
				mAmbiguousEntries.push_back(ambiguousEntry);
				goto Done;
			}
		}

		if (!isBetter)
			goto Done;
	}

	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		auto methodMatchInfo = autoComplete->mMethodMatchInfo;
		// Try to persist with previous partial match, if we have one - this keeps us from locking onto
		//  an incorrect method just because it had the current number of params that we've typed so far
		if (methodMatchInfo->mPrevBestIdx == -1)
		{
			methodMatchInfo->mHadExactMatch = true;
			methodMatchInfo->mBestIdx = (int) methodMatchInfo->mInstanceList.size() - 1;
		}
	}

	mAmbiguousEntries.Clear();
	hadMatch = true;
	mBestMethodDef = checkMethod;
	mBestRawMethodInstance = methodInstance;

	for (auto& arg : mArguments)
		arg.mBestBoundType = arg.mTypedValue.mType;

NoMatch:
	if (!hadMatch)
	{
		if (mBestMethodDef != NULL)
			return false;

		if (checkMethod->mMethodType == BfMethodType_Extension)
		{
			auto thisParam = methodInstance->GetParamType(0);
			auto resolveThisParam = mModule->ResolveGenericType(thisParam, NULL, &mCheckMethodGenericArguments, mModule->mCurTypeInstance);
			if (resolveThisParam == NULL)
				return false;
			if (!mModule->CanCast(mTarget, resolveThisParam,
				((mBfEvalExprFlags & BfEvalExprFlags_FromConversionOp) != 0) ? (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_NoConversionOperator) : BfCastFlags_Explicit))
				return false;
		}

		if (mBackupMethodDef != NULL)
		{
			int prevParamDiff = (int)mBackupMethodDef->GetExplicitParamCount() - (int)mArguments.size();
			int paramDiff = (int)checkMethod->GetExplicitParamCount() - (int)mArguments.size();
			if ((prevParamDiff < 0) && (prevParamDiff > paramDiff))
				return false;
			if ((prevParamDiff >= 0) && (paramDiff < 0))
				return false;

			if (argMatchCount < mBackupArgMatchCount)
				return false;
			else if (argMatchCount == mBackupArgMatchCount)
			{
				if (curMatchKind < mBackupMatchKind)
					return false;

				// We search from the most specific type, so don't prefer a less specific type
				if (mBestMethodTypeInstance != typeInstance)
					return false;
			}
		}

		if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
		{
			auto methodMatchInfo = autoComplete->mMethodMatchInfo;
			if ((methodMatchInfo->mPrevBestIdx == -1) && (!methodMatchInfo->mHadExactMatch))
			{
				methodMatchInfo->mBestIdx = (int)methodMatchInfo->mInstanceList.size() - 1;
			}
		}

		mBackupMatchKind = curMatchKind;
		mBackupMethodDef = checkMethod;
		mBackupArgMatchCount = argMatchCount;
		// Lie temporarily to store at least one candidate (but mBestMethodDef is still NULL)
		hadMatch = true;
	}

	if (hadMatch)
	{
		mBestMethodTypeInstance = typeInstance;
		if (genericArgumentsSubstitute != &mBestMethodGenericArguments)
		{
			if (genericArgumentsSubstitute != NULL)
			{
				for (auto& genericArg : *genericArgumentsSubstitute)
					genericArg = mModule->FixIntUnknown(genericArg);
				mBestMethodGenericArguments = *genericArgumentsSubstitute;
// #ifdef _DEBUG
// 				for (auto arg : mBestMethodGenericArguments)
// 					BF_ASSERT((arg == NULL) || (!arg->IsVar()));
// #endif
			}
			else
				mBestMethodGenericArguments.clear();
		}
	}

Done:
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo) && (genericArgumentsSubstitute != NULL))
	{
		auto methodMatchInfo = autoComplete->mMethodMatchInfo;
		if (!methodMatchInfo->mInstanceList.IsEmpty())
		{
			methodMatchInfo->mInstanceList[methodMatchInfo->mInstanceList.size() - 1].mGenericArguments = *genericArgumentsSubstitute;
		}
	}

	return mBestMethodDef == checkMethod;
}

void BfMethodMatcher::FlushAmbiguityError()
{
	if (!mAmbiguousEntries.empty())
	{
		if (mModule->PreFail())
		{
			BfError* error;
			if (!mMethodName.empty())
				error = mModule->Fail(StrFormat("Ambiguous method call for '%s'", mMethodName.c_str()), mTargetSrc);
			else
				error = mModule->Fail("Ambiguous method call", mTargetSrc);
			if (error != NULL)
			{
				BfMethodInstance* bestMethodInstance = mModule->GetUnspecializedMethodInstance(mBestRawMethodInstance, true);
				BfTypeVector* typeGenericArguments = NULL;
				if (mBestMethodTypeInstance->mGenericTypeInfo != NULL)
					typeGenericArguments = &mBestMethodTypeInstance->mGenericTypeInfo->mTypeGenericArguments;

				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(bestMethodInstance, BfMethodNameFlag_ResolveGenericParamNames,
					typeGenericArguments, mBestMethodGenericArguments.empty() ? NULL : &mBestMethodGenericArguments).c_str()),
					bestMethodInstance->mMethodDef->GetRefNode());

				for (auto& ambiguousEntry : mAmbiguousEntries)
				{
					auto typeInstance = ambiguousEntry.mMethodInstance->GetOwner();
					auto unspecTypeMethodInstance = mModule->GetUnspecializedMethodInstance(ambiguousEntry.mMethodInstance, true);

					BfTypeVector* typeGenericArguments = NULL;
					if (typeInstance->mGenericTypeInfo != NULL)
						typeGenericArguments = &typeInstance->mGenericTypeInfo->mTypeGenericArguments;

					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(unspecTypeMethodInstance, BfMethodNameFlag_ResolveGenericParamNames,
						typeGenericArguments, ambiguousEntry.mBestMethodGenericArguments.empty() ? NULL : &ambiguousEntry.mBestMethodGenericArguments).c_str()),
						ambiguousEntry.mMethodInstance->mMethodDef->GetRefNode());
				}
			}
		}

		mAmbiguousEntries.Clear();
	}
}

bool BfMethodMatcher::IsType(BfTypedValue& typedVal, BfType* type)
{
	if (typedVal.mType == type)
		return true;

	if (!typedVal)
		return false;

	if (!typedVal.mType->IsPrimitiveType())
		return false;
	if (!type->IsPrimitiveType())
		return false;

	auto fromPrimType = typedVal.mType->ToPrimitiveType();
	if ((fromPrimType->mTypeDef->mTypeCode != BfTypeCode_IntUnknown) &&
		(fromPrimType->mTypeDef->mTypeCode != BfTypeCode_UIntUnknown))
		return false;

	auto constant = mModule->mBfIRBuilder->GetConstant(typedVal.mValue);
	if (constant == NULL)
		return false;

	auto toPrimType = type->ToPrimitiveType();
	if (!mModule->mBfIRBuilder->IsInt(toPrimType->mTypeDef->mTypeCode))
		return false;

	if (type->mSize == 8)
		return false;

	int64 minVal = -(1LL << (8 * type->mSize - 1));
	int64 maxVal = (1LL << (8 * type->mSize - 1)) - 1;
	if ((constant->mInt64 >= minVal) && (constant->mInt64 <= maxVal))
		return true;

	return false;
}

// This method checks all base classes before checking interfaces.  Is that correct?
bool BfMethodMatcher::CheckType(BfTypeInstance* typeInstance, BfTypedValue target, bool isFailurePass, bool forceOuterCheck)
{
	BfMethodDef* prevBestMethodDef = mBestMethodDef;
	auto curTypeInst = typeInstance;
	auto curTypeDef = typeInstance->mTypeDef;

	int checkInterfaceIdx = 0;

	bool targetIsBase = target.IsBase();
	bool checkExtensionBase = false;
	if (targetIsBase)
	{
		if ((curTypeInst == mModule->mCurTypeInstance) && (curTypeInst->mTypeDef->mIsCombinedPartial))
		{
			checkExtensionBase = true;
		}
		else
		{
			curTypeInst = curTypeInst->mBaseType;
		}
	}

	BfTypeInstance* targetTypeInstance = NULL;
	if (target.mType != NULL)
		targetTypeInstance = target.mType->ToTypeInstance();

	while (true)
	{
		bool doSearch = true;
		if ((mMethodType == BfMethodType_Extension) && (!curTypeDef->mHasExtensionMethods))
			doSearch = false;

		BfMethodDef* nextMethodDef = NULL;
		if (doSearch)
		{
			curTypeDef->PopulateMemberSets();
			BfMemberSetEntry* entry;
			if (curTypeDef->mMethodSet.TryGetWith(mMethodName, &entry))
				nextMethodDef = (BfMethodDef*)entry->mMemberDef;
		}

		BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
		if (target)
		{
			if (mBypassVirtual)
			{
				// Not an "instance lookup"
			}
			else
			{
				protectionCheckFlags = (BfProtectionCheckFlags)(protectionCheckFlags | BfProtectionCheckFlag_InstanceLookup);
			}
		}

		while (nextMethodDef != NULL)
		{
			bool allowExplicitInterface = curTypeInst->IsInterface() && mBypassVirtual;
			auto activeTypeDef = mModule->GetActiveTypeDef();
			auto visibleProjectSet = mModule->GetVisibleProjectSet();
			bool isDelegate = typeInstance->IsDelegate();

			auto checkMethod = nextMethodDef;
			nextMethodDef = nextMethodDef->mNextWithSameName;

			if (mModule->mContext->mResolvingVarField)
			{
				bool isResolvingVarField = false;

				auto checkTypeState = mModule->mContext->mCurTypeState;
				while (checkTypeState != NULL)
				{
					if ((checkTypeState->mResolveKind == BfTypeState::ResolveKind_ResolvingVarType) &&
						(checkTypeState->mType == typeInstance))
						isResolvingVarField = true;
					checkTypeState = checkTypeState->mPrevState;
				}

				if (isResolvingVarField)
				{
					BF_ASSERT(mModule->mBfIRBuilder->mIgnoreWrites);

					// Don't even consider - we can't do method calls on ourselves when we are resolving var fields, because
					// we are not allowed to generate methods when our field types are unknown. We may fix this in the future,
					// but currently it breaks out expected order of operations. One issue is that our call signatures change
					// depending on whether we are valueless or splattable, which depend on underlying type information
					break;
				}
			}

			if ((checkExtensionBase) && (curTypeInst == mModule->mCurTypeInstance))
			{
				// Accept either a method in the same project but that's the root definition, OR a method that's in a dependent project
				bool accept = false;
				if (activeTypeDef->mProject == checkMethod->mDeclaringType->mProject)
					accept = (activeTypeDef->IsExtension()) && (!checkMethod->mDeclaringType->IsExtension());
				else
					accept = activeTypeDef->mProject->ContainsReference(checkMethod->mDeclaringType->mProject);

				if (!accept)
					continue;
			}

			if ((!allowExplicitInterface) && (checkMethod->mExplicitInterface != NULL) && (mInterfaceMethodInstance == NULL))
			{
				continue;
			}

			if (checkMethod->mMethodType != mMethodType)
				continue;

// 			if (checkMethod->mName != mMethodName)
// 				continue;

			if ((checkMethod->mDeclaringType->IsExtension()) && (mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) &&
				(mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mNoExtensionAttributeTypeDef)))
			{
				mModule->mAttributeState->mUsed = true;
				continue;
			}

			if (!isDelegate)
			{
				if ((!curTypeInst->IsTypeMemberIncluded(checkMethod->mDeclaringType, activeTypeDef, mModule)) ||
					(!curTypeInst->IsTypeMemberAccessible(checkMethod->mDeclaringType, visibleProjectSet)))
					continue;
			}

			MatchFailKind matchFailKind = MatchFailKind_None;
			if (!mModule->CheckProtection(protectionCheckFlags, curTypeInst, checkMethod->mDeclaringType->mProject, checkMethod->mProtection, typeInstance))
			{
				if ((mBypassVirtual) &&
					((checkMethod->mProtection == BfProtection_Protected) || (checkMethod->mProtection == BfProtection_ProtectedInternal)) &&
					(mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, typeInstance)))
				{
					// Allow explicit 'base' call
				}
				else
				{
					if (!isFailurePass)
						continue;
					matchFailKind = MatchFailKind_Protection;
				}
			}

			if (mCheckedKind != checkMethod->mCheckedKind)
			{
				bool passes = true;
				if (mCheckedKind != BfCheckedKind_NotSet)
				{
					passes = false;
				}
				else
				{
					auto defaultCheckedKind = mModule->GetDefaultCheckedKind();
					if (defaultCheckedKind != checkMethod->mCheckedKind)
						passes = false;
				}
				if (!passes)
				{
					if (!isFailurePass)
						continue;
					matchFailKind = MatchFailKind_CheckedMismatch;
				}
			}

			CheckMethod(targetTypeInstance, curTypeInst, checkMethod, isFailurePass);
			if ((isFailurePass) &&
				((mBestMethodDef == checkMethod) || (mBackupMethodDef == checkMethod)))
				mMatchFailKind = matchFailKind;
		}

		if ((mBestMethodDef != NULL) && (mMethodType != BfMethodType_Extension))
		{
			if ((mUsingLists != NULL) && (mUsingLists->mSize != 0))
				mUsingLists->Clear();

			if (mAutoFlushAmbiguityErrors)
				FlushAmbiguityError();
			return true;
		}

		if ((mUsingLists != NULL) && (curTypeInst->mTypeDef->mHasUsingFields))
			mModule->PopulateUsingFieldData(curTypeInst);

		if (mUsingLists != NULL)
		{
			auto _CheckUsingData = [&](BfUsingFieldData* usingData)
			{
				BfUsingFieldData::Entry* entry = NULL;
				if (!usingData->mMethods.TryGetValue(mMethodName, &entry))
					return;

				for (int listIdx = 0; listIdx < entry->mLookups.mSize; listIdx++)
				{
					bool passesProtection = true;
					auto& entryList = entry->mLookups[listIdx];
					for (int entryIdx = 0; entryIdx < entryList.mSize; entryIdx++)
					{
						auto& entry = entryList[entryIdx];
						BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
						if (!mModule->CheckProtection(protectionCheckFlags, entry.mTypeInstance, entry.GetDeclaringType(mModule)->mProject,
							(entryIdx < entryList.mSize - 1) ? entry.GetUsingProtection() : entry.GetProtection(), curTypeInst))
						{
							passesProtection = false;
							break;
						}
					}
					if (!passesProtection)
						continue;

					auto& entry = entryList.back();
					BF_ASSERT(entry.mKind == BfUsingFieldData::MemberRef::Kind_Method);
					auto methodDef = entry.mTypeInstance->mTypeDef->mMethods[entry.mIdx];
					CheckMethod(curTypeInst, entry.mTypeInstance, methodDef, isFailurePass);
					if ((mBestMethodDef != methodDef) && (mBackupMethodDef != methodDef))
					{
						bool foundAmbiguous = false;
						for (int checkIdx = 0; checkIdx < mAmbiguousEntries.mSize; checkIdx++)
						{
							if (mAmbiguousEntries[checkIdx].mMethodInstance->mMethodDef == methodDef)
							{
								mAmbiguousEntries.RemoveAt(checkIdx);
								foundAmbiguous = true;
								break;
							}
						}

						if (!foundAmbiguous)
							continue;
					}

					if (mUsingLists->mSize == 0)
					{
						mUsingLists->Add(&entryList);
					}
					else
					{
						if (entryList.mSize < (*mUsingLists)[0]->mSize)
						{
							// New is shorter
							mUsingLists->Clear();
							mUsingLists->Add(&entryList);
						}
						else if (entryList.mSize > (*mUsingLists)[0]->mSize)
						{
							// Ignore longer
						}
						else
						{
							mUsingLists->Add(&entryList);
						}
					}
				}
			};

			if ((curTypeInst->mTypeInfoEx != NULL) && (curTypeInst->mTypeInfoEx->mUsingFieldData != NULL))
				_CheckUsingData(curTypeInst->mTypeInfoEx->mUsingFieldData);

			if (mBestMethodDef != NULL)
				break;
		}

		auto baseType = curTypeInst->mBaseType;
		if (baseType == NULL)
		{
			//TODO: Why were we doing the interface checking?
			if ((curTypeInst->IsInterface()) && (curTypeInst == target.mType))
			{
				// When we are directly calling on interfaces rather than indirectly matching through binding
				baseType = mModule->mContext->mBfObjectType;
			}
			else if ((curTypeInst != mModule->mContext->mBfObjectType) && (!curTypeInst->IsInterface()))
			{
				// This can happen for structs
				baseType = mModule->mContext->mBfObjectType;
			}
			else if ((typeInstance->IsInterface()) && (checkInterfaceIdx < (int)typeInstance->mInterfaces.size()))
			{
				baseType = typeInstance->mInterfaces[checkInterfaceIdx].mInterfaceType;
				checkInterfaceIdx++;
			}
			else
			{
				break;
			}
		}
		curTypeDef = baseType->mTypeDef;
		curTypeInst = baseType;

		if ((isFailurePass) && (mBackupMethodDef != NULL))
			break;
	}

	if (mBestMethodDef == NULL)
	{
		// FAILED, but select the first method which will fire an actual error on param type matching
		mBestMethodDef = mBackupMethodDef;
	}

	if (((mBestMethodDef == NULL) && (!target) && (mAllowImplicitThis)) ||
		(forceOuterCheck))
	{
		// No explicit target - maybe this was a static call in the outer type?
		auto outerType = mModule->GetOuterType(typeInstance);
		if (outerType != NULL)
			CheckOuterTypeStaticMethods(outerType, isFailurePass);
	}

	if (mAutoFlushAmbiguityErrors)
		FlushAmbiguityError();

	return mBestMethodDef != prevBestMethodDef;
}

void BfMethodMatcher::TryDevirtualizeCall(BfTypedValue target, BfTypedValue* origTarget, BfTypedValue* staticResult)
{
	if ((mBestMethodDef == NULL) || (target.mType == NULL))
		return;

	if ((mModule->mCompiler->IsAutocomplete()) || (mModule->mContext->mResolvingVarField))
		return;

	if ((mModule->mBfIRBuilder->mIgnoreWrites) && (!mBestMethodDef->mIsConcrete) && (!mBestMethodTypeInstance->IsInterface()))
		return;

	if (mBestMethodTypeInstance->IsInterface())
	{
		mModule->PopulateType(mBestMethodTypeInstance, BfPopulateType_DataAndMethods);

		auto activeTypeDef = mModule->GetActiveTypeDef();

		// Statically map this call
		auto checkType = target.mType;
		if (checkType->IsPointer())
			checkType = ((BfPointerType*)checkType)->mElementType;
		if (checkType->IsWrappableType())
			checkType = mModule->GetWrappedStructType(checkType);
		if ((checkType != NULL) && (checkType->IsTypeInstance()) && (!checkType->IsInterface()))
		{
			BfTypeInterfaceEntry* bestIFaceEntry = NULL;
			auto checkTypeInst = checkType->ToTypeInstance();

			if (mBestMethodTypeInstance->IsInstanceOf(mModule->mCompiler->mIHashableTypeDef))
			{
				if ((origTarget != NULL) && (origTarget->mType->IsPointer()) && (staticResult != NULL))
				{
					BfTypedValue ptrVal = mModule->LoadValue(*origTarget);
					*staticResult = BfTypedValue(mModule->mBfIRBuilder->CreatePtrToInt(ptrVal.mValue, BfTypeCode_IntPtr), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
					return;
				}
			}

			while (checkTypeInst != NULL)
			{
				mModule->PopulateType(checkTypeInst, BfPopulateType_DataAndMethods);
				if (checkTypeInst->mDefineState >= BfTypeDefineState_DefinedAndMethodsSlotted)
				{
					for (auto&& iface : checkTypeInst->mInterfaces)
					{
						//TODO: Why did we have this check?  This caused Dictionary to not be able to devirtualize
						//  calls to TKey GetHashCode when TKey was from a user's project...
						/*if (!checkTypeInst->IsTypeMemberAccessible(iface.mDeclaringType, activeTypeDef))
							continue;*/

						if (iface.mInterfaceType == mBestMethodTypeInstance)
						{
							if (bestIFaceEntry == NULL)
							{
								bestIFaceEntry = &iface;
								continue;
							}

							bool isBetter;
							bool isWorse;
							mModule->CompareDeclTypes(NULL, iface.mDeclaringType, bestIFaceEntry->mDeclaringType, isBetter, isWorse);
							if (isBetter == isWorse)
							{
								// Failed
							}
							else
							{
								if (isBetter)
									bestIFaceEntry = &iface;
							}
						}
					}
				}

				if (bestIFaceEntry != NULL)
					break;
				checkTypeInst = checkTypeInst->mBaseType;

				if ((checkTypeInst == NULL) && (checkType->HasWrappedRepresentation()))
				{
					auto underlyingType = checkType->GetUnderlyingType();
					if ((underlyingType != NULL) && (underlyingType->IsWrappableType()))
						checkTypeInst = mModule->GetWrappedStructType(underlyingType);
					if (checkTypeInst == checkType)
						break;
				}
			}

			if (bestIFaceEntry != NULL)
			{
				auto ifaceMethodEntry = checkTypeInst->mInterfaceMethodTable[bestIFaceEntry->mStartInterfaceTableIdx + mBestMethodDef->mIdx];
				BfMethodInstance* bestMethodInstance = ifaceMethodEntry.mMethodRef;
				if (bestMethodInstance != NULL)
				{
					bool isMissingArg = false;
					for (auto genericArg : mBestMethodGenericArguments)
					{
						if (genericArg == NULL)
							isMissingArg = true;
					}

					if (!isMissingArg)
					{
						// Assert error state?
						mBestMethodTypeInstance = ifaceMethodEntry.mMethodRef.mTypeInstance;
						mBestMethodDef = bestMethodInstance->mMethodDef;
						mBestMethodInstance = mModule->GetMethodInstance(mBestMethodTypeInstance, bestMethodInstance->mMethodDef, mBestMethodGenericArguments,
							bestMethodInstance->mIsForeignMethodDef ? BfGetMethodInstanceFlag_ForeignMethodDef : BfGetMethodInstanceFlag_None,
							bestMethodInstance->GetForeignType());
					}
				}
				else
				{
					// Failed
					mFakeConcreteTarget = true;
				}
			}
		}
	}

	if ((target.mType->IsValueType()) && (mBestMethodTypeInstance->IsObject()) && (mBestMethodDef->mIsVirtual))
	{
		auto structType = target.mType->ToTypeInstance();

		auto virtualMethodInstance = mModule->GetMethodInstance(mBestMethodTypeInstance, mBestMethodDef, BfTypeVector());
		BF_ASSERT(virtualMethodInstance.mMethodInstance->mVirtualTableIdx != -1);

		BfTypeInstance* boxedType;

		if (structType->HasOverrideMethods())
		{
			// We don't actually need this boxed type, so just resolve it unreified
			auto useModule = mModule->mContext->mUnreifiedModule;
			boxedType = useModule->CreateBoxedType(target.mType);
			useModule->PopulateType(boxedType, BfPopulateType_DataAndMethods);
			useModule->AddDependency(boxedType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_WeakReference);
		}
		else
		{
			boxedType = mModule->mContext->mBfObjectType;
		}

		auto methodRef = boxedType->mVirtualMethodTable[virtualMethodInstance.mMethodInstance->mVirtualTableIdx];
		if (methodRef.mImplementingMethod.mTypeInstance->IsBoxed())
		{
			auto useModule = mModule->mContext->mUnreifiedModule;
			auto boxedMethodInstance = useModule->ReferenceExternalMethodInstance(methodRef.mImplementingMethod);

			BfBoxedType* vBoxedType = (BfBoxedType*)methodRef.mImplementingMethod.mTypeInstance;
			mBestMethodTypeInstance = vBoxedType->mElementType->ToTypeInstance();
			mBestMethodInstance = mModule->GetMethodInstance(mBestMethodTypeInstance, boxedMethodInstance.mMethodInstance->mMethodDef, BfTypeVector());
			mBestMethodDef = mBestMethodInstance.mMethodInstance->mMethodDef;
		}
		else
		{
			mBestMethodTypeInstance = methodRef.mImplementingMethod.mTypeInstance;
			mBestMethodInstance = mModule->ReferenceExternalMethodInstance(methodRef.mImplementingMethod);
			mBestMethodDef = mBestMethodInstance.mMethodInstance->mMethodDef;
		}
		mBypassVirtual = true;
	}
}

bool BfMethodMatcher::HasVarGenerics()
{
	for (auto genericArg : mBestMethodGenericArguments)
		if (genericArg->IsVar())
			return true;
	for (auto genericArg : mExplicitMethodGenericArguments)
		if (genericArg->IsVar())
			return true;
	return false;
}

void BfMethodMatcher::CheckOuterTypeStaticMethods(BfTypeInstance* typeInstance, bool isFailurePass)
{
	bool allowPrivate = true;
	bool allowProtected = true;

	auto curTypeInst = typeInstance;
	auto curTypeDef = typeInstance->mTypeDef;

	while (true)
	{
		curTypeDef->PopulateMemberSets();
		BfMethodDef* nextMethodDef = NULL;
		BfMemberSetEntry* entry;
		if (curTypeDef->mMethodSet.TryGetWith(mMethodName, &entry))
			nextMethodDef = (BfMethodDef*)entry->mMemberDef;

		while (nextMethodDef != NULL)
		{
			auto checkMethod = nextMethodDef;
			nextMethodDef = nextMethodDef->mNextWithSameName;

			// These can only be invoked when the target itself is the interface
			if (checkMethod->mExplicitInterface != NULL)
				continue;
			if ((checkMethod->mMethodType != mMethodType) || (!checkMethod->mIsStatic))
				continue;
			if (checkMethod->mName != mMethodName)
				continue;

			if ((!isFailurePass) && (!mModule->CheckProtection(checkMethod->mProtection, NULL, allowProtected, allowPrivate)))
				continue;

			CheckMethod(typeInstance, curTypeInst, checkMethod, isFailurePass);
		}

		if (mBestMethodDef != NULL)
			return;

		auto baseType = curTypeInst->mBaseType;
		if (baseType == NULL)
			break;
		curTypeDef = baseType->mTypeDef;
		curTypeInst = baseType;

		allowPrivate = false;

		if ((isFailurePass) && (mBackupMethodDef != NULL))
			break;
	}

	if (mBestMethodDef == NULL)
	{
		// FAILED, but select the first method which will fire an actual error on param type matching
		mBestMethodDef = mBackupMethodDef;
	}

	if (mBestMethodDef == NULL)
	{
		// No explicit target - maybe this was a static call in the outer type?
		auto outerType = mModule->GetOuterType(typeInstance);
		if (outerType != NULL)
			CheckOuterTypeStaticMethods(outerType, isFailurePass);
	}
}

//////////////////////////////////////////////////////////////////////////

void BfResolvedArgs::HandleFixits(BfModule* module)
{
	auto compiler = module->mCompiler;
	if ((!compiler->IsAutocomplete()) || (compiler->mResolvePassData->mResolveType != BfResolveType_GetFixits))
		return;

	SetAndRestoreValue<bool> ignoreErrors(module->mIgnoreErrors, true);
	for (int argIdx = 0; argIdx < (int)mResolvedArgs.size(); argIdx++)
	{
		auto& resolvedArg = mResolvedArgs[argIdx];
		auto expr = BfNodeDynCast<BfExpression>(resolvedArg.mExpression);
		if (expr != NULL)
		{
			module->CreateValueFromExpression(expr, resolvedArg.mExpectedType);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

BfExprEvaluator::BfExprEvaluator(BfModule* module)
{
	mBfEvalExprFlags = BfEvalExprFlags_None;
	mModule = module;
	mPropDef = NULL;
	mPropSrc = NULL;
	mPropGetMethodFlags = BfGetMethodInstanceFlag_None;
	mPropCheckedKind = BfCheckedKind_NotSet;
	mUsedAsStatement = false;
	mPropDefBypassVirtual = false;
	mExpectingType = NULL;
	mFunctionBindResult = NULL;
	mExplicitCast = false;
	mDeferCallRef = NULL;
	mDeferScopeAlloc = NULL;
	mPrefixedAttributeState = NULL;
	mResolveGenericParam = true;
	mNoBind = false;
	mResultLocalVar = NULL;
	mResultFieldInstance = NULL;
	mResultLocalVarField = 0;
	mResultLocalVarFieldCount = 0;
	mResultLocalVarRefNode = NULL;
	mIsVolatileReference = false;
	mIsHeapReference = false;
	mResultIsTempComposite = false;
	mAllowReadOnlyReference = false;
	mInsidePendingNullable = false;
	mReceivingValue = NULL;
}

BfExprEvaluator::~BfExprEvaluator()
{
}

BfAutoComplete* BfExprEvaluator::GetAutoComplete()
{
	if (mModule->mCompiler->mResolvePassData == NULL)
		return NULL;
	if ((mBfEvalExprFlags & BfEvalExprFlags_NoAutoComplete) != 0)
		return NULL;

	// For local methods- only process autocomplete on capture phase
	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (!mModule->mCurMethodState->mClosureState->mCapturing))
		return NULL;

// 	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mMethodDef->mIsLocalMethod))
// 		return NULL;
	return mModule->mCompiler->mResolvePassData->mAutoComplete;
}

bool BfExprEvaluator::IsComptime()
{
	return (mModule->mIsComptimeModule) || ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0);
}

bool BfExprEvaluator::IsConstEval()
{
	return ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0);
}

bool BfExprEvaluator::IsComptimeEntry()
{
	if (mModule->mIsComptimeModule)
		return false;
	return ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0);
}

int BfExprEvaluator::GetStructRetIdx(BfMethodInstance* methodInstance, bool forceStatic)
{
	if (IsComptime())
		return -1;
	return methodInstance->GetStructRetIdx(forceStatic);
}

BfType* BfExprEvaluator::BindGenericType(BfAstNode* node, BfType* bindType)
{
	if ((mModule->mCurMethodState == NULL) || (mModule->mCurMethodInstance == NULL) || (bindType == NULL))
		return bindType;

	if ((mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
		return bindType;

	if ((mBfEvalExprFlags & BfEvalExprFlags_DeclType) != 0)
		return bindType;

	BF_ASSERT((!mModule->mCurMethodInstance->mIsUnspecializedVariation) || (mModule->mIsComptimeModule));

	auto parser = node->GetSourceData()->ToParserData();
	if (parser == NULL)
		return bindType;
	int64 nodeId = ((int64)parser->mDataId << 32) + node->GetSrcStart();

	auto genericTypeBindings = mModule->mCurMethodState->GetRootMethodState()->mGenericTypeBindings;
	auto methodInstance = mModule->mCurMethodInstance;

	bool isMixinBind = false;
	if (mModule->mCurMethodState->mMixinState != NULL)
	{
		auto mixinMethodInstance = mModule->mCurMethodState->mMixinState->mMixinMethodInstance;
		if (!mixinMethodInstance->mMethodDef->mGenericParams.IsEmpty())
		{
			auto unspecMixinMethodInstance = mModule->GetUnspecializedMethodInstance(mixinMethodInstance, false);

			if (!unspecMixinMethodInstance->mHasBeenProcessed)
			{
				// Make sure the unspecialized method is processed so we can take its bindings
				// Clear mCurMethodState so we don't think we're in a local method
				SetAndRestoreValue<BfMethodState*> prevMethodState_Unspec(mModule->mCurMethodState, NULL);
				if (unspecMixinMethodInstance->mMethodProcessRequest == NULL)
					unspecMixinMethodInstance->mDeclModule->mIncompleteMethodCount++;
				mModule->mContext->ProcessMethod(unspecMixinMethodInstance);
			}

			isMixinBind = true;
			methodInstance = mixinMethodInstance;
			genericTypeBindings = &unspecMixinMethodInstance->mMethodInfoEx->mGenericTypeBindings;
		}
	}

	if ((methodInstance->mIsUnspecialized) && (!methodInstance->mIsUnspecializedVariation))
	{
		if (isMixinBind)
			return bindType;

		if (!bindType->IsGenericParam())
			return bindType;

		if (genericTypeBindings == NULL)
			return bindType;

		(*genericTypeBindings)[nodeId] = bindType;
		return bindType;
	}
	else
	{
		if (genericTypeBindings == NULL)
			return bindType;

		BfType** typePtr = NULL;
		if (genericTypeBindings->TryGetValue(nodeId, &typePtr))
			return *typePtr;

		return bindType;
	}
}

BfType * BfExprEvaluator::ResolveTypeRef(BfTypeReference* typeRef, BfPopulateType populateType, BfResolveTypeRefFlags resolveFlags)
{
	if (mExpectingType != NULL)
	{
		if (auto namedTypeRef = BfNodeDynCastExact<BfNamedTypeReference>(typeRef))
		{
			if (namedTypeRef->ToString() == "ExpectedType")
			{
				return mModule->ResolveTypeResult(typeRef, mExpectingType, populateType, resolveFlags);
			}
		}
	}

	return mModule->ResolveTypeRef(typeRef, populateType, resolveFlags);
}

void BfExprEvaluator::ResolveGenericType()
{
	if (mResult)
	{
		if (mModule->IsUnboundGeneric(mResult.mType))
			mResult.mType = mModule->GetPrimitiveType(BfTypeCode_Var);
		//mResult.mType = mModule->ResolveGenericType(mResult.mType, true);
	}
}

void BfExprEvaluator::Evaluate(BfAstNode* astNode, bool propogateNullConditional, bool ignoreNullConditional, bool allowSplat)
{
	BP_ZONE("BfExprEvaluator::Evaluate");

	// ParenthesizedExpression breaks null conditional chain
	if (astNode->IsExact<BfParenthesizedExpression>())
		propogateNullConditional = false;

	bool scopeWasConditional = false;

	BfPendingNullConditional* pendingNullCond = NULL;
	mInsidePendingNullable = false;
	if (mModule->mCurMethodState != NULL)
	{
		scopeWasConditional = mModule->mCurMethodState->mCurScope->mIsConditional;
		pendingNullCond = mModule->mCurMethodState->mPendingNullConditional;
		if (!propogateNullConditional)
			mModule->mCurMethodState->mPendingNullConditional = NULL;
		if (pendingNullCond != NULL)
		{
			mInsidePendingNullable = true;
			mModule->mCurMethodState->mCurScope->mIsConditional = true;
		}
	}

	astNode->Accept(this);
	GetResult();

	if ((mResultIsTempComposite) && (mResult.IsAddr()))
		mResult.mKind = BfTypedValueKind_TempAddr;

	if ((!allowSplat) && (mResult.IsSplat()))
		mResult = mModule->AggregateSplat(mResult);

	if ((mBfEvalExprFlags & BfEvalExprFlags_AllowIntUnknown) == 0)
		mModule->FixIntUnknown(mResult);

	if (!mModule->mBfIRBuilder->mIgnoreWrites)
	{
		if (mResult.mValue.IsConst())
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(mResult.mValue);
			if (constant->mConstType == BfConstType_TypeOf)
			{
				auto typeOfConst = (BfTypeOf_Const*)constant;
				mResult.mValue = mModule->CreateTypeDataRef(typeOfConst->mType);
			}
		}
	}

	if (mModule->mCurMethodState != NULL)
	{
		if (mInsidePendingNullable)
			mModule->mCurMethodState->mCurScope->mIsConditional = scopeWasConditional;

		if (!propogateNullConditional)
		{
			if (mModule->mCurMethodState->mPendingNullConditional != NULL)
				mResult = mModule->FlushNullConditional(mResult, ignoreNullConditional);
			mModule->mCurMethodState->mPendingNullConditional = pendingNullCond;
		}
	}
}

void BfExprEvaluator::Visit(BfErrorNode* errorNode)
{
	mModule->Fail("Invalid token", errorNode);

	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(errorNode->mRefNode))
			return;
		autoComplete->CheckIdentifier(errorNode->mRefNode, true);
	}
}

void BfExprEvaluator::Visit(BfTypeReference* typeRef)
{
	mResult.mType = ResolveTypeRef(typeRef, BfPopulateType_Declaration);
}

void BfExprEvaluator::Visit(BfAttributedExpression* attribExpr)
{
	BfAttributeState attributeState;
	attributeState.mSrc = attribExpr->mAttributes;
	attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_Invocation | BfAttributeTargets_MemberAccess);
	if (auto block = BfNodeDynCast<BfBlock>(attribExpr->mExpression))
		attributeState.mTarget = BfAttributeTargets_Block;

	attributeState.mCustomAttributes = mModule->GetCustomAttributes(attribExpr->mAttributes, attributeState.mTarget);
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);

	if (auto ignoreErrorsAttrib = attributeState.mCustomAttributes->Get(mModule->mCompiler->mIgnoreErrorsAttributeTypeDef))
	{
		SetAndRestoreValue<bool> ignoreErrors(mModule->mIgnoreErrors, true);
		if (!ignoreErrorsAttrib->mCtorArgs.IsEmpty())
		{
			auto constant = mModule->mCurTypeInstance->mConstHolder->GetConstant(ignoreErrorsAttrib->mCtorArgs[0]);
			if (constant->mBool)
				attributeState.mFlags = BfAttributeState::Flag_StopOnError;
		}
		VisitChild(attribExpr->mExpression);
		attributeState.mUsed = true;

		if ((!mResult) ||
			((mResult) && (mResult.mType->IsVar())))
		{
			if (!mResult)
				mModule->Fail("Expression did not result in a value", attribExpr->mExpression);

			// Make empty or 'var' resolve as 'false' because var is only valid if we threw errors
			mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Boolean));
		}
	}
	else if (attributeState.mCustomAttributes->Contains(mModule->mCompiler->mConstSkipAttributeTypeDef))
	{
		if ((mModule->mCurMethodState == NULL) || (mModule->mCurMethodState->mCurScope == NULL) || (!mModule->mCurMethodState->mCurScope->mInConstIgnore))
		{
			VisitChild(attribExpr->mExpression);
		}
		else
		{
			BF_ASSERT(mModule->mBfIRBuilder->mIgnoreWrites);
			mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		}
		attributeState.mUsed = true;
	}
	else
	{
		VisitChild(attribExpr->mExpression);
	}

	mModule->FinishAttributeState(&attributeState);
}

void BfExprEvaluator::Visit(BfNamedExpression* namedExpr)
{
	if (namedExpr->mExpression != NULL)
		VisitChild(namedExpr->mExpression);
}

void BfExprEvaluator::Visit(BfBlock* blockExpr)
{
	if (mModule->mCurMethodState == NULL)
	{
		mModule->Fail("Illegal use of block expression", blockExpr);
		return;
	}

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->mMethodMatchInfo != NULL) && (autoComplete->IsAutocompleteNode(blockExpr)))
	{
		// Don't show outer method match info when our cursor is inside a block (being passed as a parameter)
		autoComplete->RemoveMethodMatchInfo();
	}

	if (blockExpr->mChildArr.IsEmpty())
	{
		mModule->Fail("An empty block cannot be used as an expression", blockExpr);
		return;
	}

	bool lastWasResultExpr = false;
	if (auto lastExpr = BfNodeDynCast<BfExpressionStatement>(blockExpr->mChildArr.GetLast()))
	{
		if (!lastExpr->IsMissingSemicolon())
			mModule->Fail("Expression blocks must end in an expression which is missing its terminating semicolon", lastExpr->mTrailingSemicolon);
	}
	else if (blockExpr->mChildArr.GetLast()->IsExpression())
	{
		// Expression
	}
	else
	{
		mModule->Fail("Expression blocks must end with an expression", blockExpr);
	}

	mModule->VisitEmbeddedStatement(blockExpr, this, BfNodeIsA<BfUnscopedBlock>(blockExpr) ? BfEmbeddedStatementFlags_Unscoped : BfEmbeddedStatementFlags_None);
}

bool BfExprEvaluator::CheckVariableDeclaration(BfAstNode* checkNode, bool requireSimpleIfExpr, bool exprMustBeTrue, bool silentFail)
{
	if (BfNodeIsA<BfUninitializedExpression>(checkNode))
		return true;

	BfAstNode* checkChild = checkNode;
	bool childWasAndRHS = false;
	bool foundIf = false;

	auto parentNodeEntry = mModule->mParentNodeEntry;
	if (parentNodeEntry != NULL)
	{
		if (BfNodeIsA<BfInvocationExpression>(parentNodeEntry->mNode))
		{
			checkChild = parentNodeEntry->mNode;
			parentNodeEntry = parentNodeEntry->mPrev;
		}
	}

	auto _Fail = [&](const StringImpl& errorStr, BfAstNode* node)
	{
		if (!silentFail)
		{
			auto error = mModule->Fail(errorStr, node);
			if ((error != NULL) && (node != checkNode))
			{
				mModule->mCompiler->mPassInstance->MoreInfo("See variable declaration", checkNode);
			}
		}
		return false;
	};

	while (parentNodeEntry != NULL)
	{
		BfAstNode* checkParent = parentNodeEntry->mNode;

		if (auto binOpExpr = BfNodeDynCastExact<BfBinaryOperatorExpression>(checkParent))
		{
			if (binOpExpr->mOp == BfBinaryOp_ConditionalAnd)
			{
				// This is always okay
				childWasAndRHS = (binOpExpr->mRight != NULL) && (binOpExpr->mRight->Contains(checkChild));
			}
			else if ((binOpExpr->mOp == BfBinaryOp_ConditionalOr) && (!exprMustBeTrue))
			{
				if ((binOpExpr->mRight != NULL) && (binOpExpr->mRight->Contains(checkChild)))
				{
					return _Fail("Conditional short-circuiting may skip variable initialization", binOpExpr->mOpToken);
				}
			}
			else
			{
				if (exprMustBeTrue)
				{
					return _Fail("Operator cannot be used with variable initialization", binOpExpr->mOpToken);
				}
			}
		}
		else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(checkParent))
		{
			// This is okay
		}
		else if (auto unaryOp = BfNodeDynCast<BfUnaryOperatorExpression>(checkParent))
		{
			if (exprMustBeTrue)
			{
				return _Fail("Operator cannot be used with variable initialization", unaryOp->mOpToken);
				return false;
			}

			if (childWasAndRHS)
			{
				return _Fail("Operator may allow conditional short-circuiting to skip variable initialization", unaryOp->mOpToken);
			}
		}
		else if (auto ifStmt = BfNodeDynCast<BfIfStatement>(checkParent))
		{
			// Done
			foundIf = true;
			break;
		}
		else
		{
			if (requireSimpleIfExpr)
			{
				return _Fail("Variable declaration expression can only be contained in simple 'if' expressions", checkNode);
			}
			break;
		}

		checkChild = parentNodeEntry->mNode;
		parentNodeEntry = parentNodeEntry->mPrev;
	}
	return foundIf;
}

bool BfExprEvaluator::HasVariableDeclaration(BfAstNode* checkNode)
{
	BfVarDeclChecker checker;
	checker.VisitChild(checkNode);
	return checker.mHasVarDecl;
}

void BfExprEvaluator::Visit(BfVariableDeclaration* varDecl)
{
	mModule->UpdateExprSrcPos(varDecl);

	if ((mModule->mCurMethodState == NULL) || (!mModule->mCurMethodState->mCurScope->mAllowVariableDeclarations))
	{
		mModule->Fail("Variable declarations are not allowed in this context", varDecl);
		if (varDecl->mInitializer != NULL)
		{
			VisitChild(varDecl->mInitializer);
		}
		return;
	}

	CheckVariableDeclaration(varDecl, true, false, false);
	if (varDecl->mInitializer == NULL)
	{
		mModule->Fail("Variable declarations used as expressions must have an initializer", varDecl);
	}

	BfTupleExpression* tupleVariableDeclaration = BfNodeDynCast<BfTupleExpression>(varDecl->mNameNode);
	if (tupleVariableDeclaration != NULL)
	{
		mModule->Fail("Tuple variable declarations cannot be used as expressions", varDecl);
		mModule->HandleTupleVariableDeclaration(varDecl);
	}
	else
		mModule->HandleVariableDeclaration(varDecl, this);
}

void BfExprEvaluator::Visit(BfCaseExpression* caseExpr)
{
	if (caseExpr->mEqualsNode != NULL)
	{
		mModule->Warn(0, "Deprecated case syntax", caseExpr->mEqualsNode);
	}

	BfTypedValue caseValAddr;
	if (caseExpr->mValueExpression != NULL)
    	caseValAddr = mModule->CreateValueFromExpression(caseExpr->mValueExpression, NULL, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));

	if ((caseValAddr.mType != NULL) && (caseValAddr.mType->IsPointer()))
	{
		caseValAddr = mModule->LoadValue(caseValAddr);
		caseValAddr = BfTypedValue(caseValAddr.mValue, caseValAddr.mType->GetUnderlyingType(), true);
	}

	if (caseValAddr.mType != NULL)
		mModule->mBfIRBuilder->PopulateType(caseValAddr.mType);

	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mDeferredLocalAssignData != NULL))
		mModule->mCurMethodState->mDeferredLocalAssignData->BreakExtendChain();

	if (auto bindExpr = BfNodeDynCast<BfEnumCaseBindExpression>(caseExpr->mCaseExpression))
	{
		if (caseValAddr)
		{
			BfTypedValue enumTagVal;
			if (caseValAddr.mType->IsPayloadEnum())
			{
				int dscrDataIdx;
				auto dscrType = caseValAddr.mType->ToTypeInstance()->GetDiscriminatorType(&dscrDataIdx);
				enumTagVal = BfTypedValue(mModule->ExtractValue(caseValAddr, dscrDataIdx), dscrType);
			}
			else
				enumTagVal = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Int32));

			mResult = mModule->HandleCaseBind(caseValAddr, enumTagVal, bindExpr);
			return;
		}
	}

	bool isPayloadEnum = (caseValAddr.mType != NULL) && (caseValAddr.mType->IsPayloadEnum());
	auto tupleExpr = BfNodeDynCast<BfTupleExpression>(caseExpr->mCaseExpression);

	if ((caseValAddr) &&
		((isPayloadEnum) || (tupleExpr != NULL)))
	{
		bool hasVariable = false;
		bool hasOut = false;
		bool clearOutOnMismatch = false;
		if (auto invocateExpr = BfNodeDynCast<BfInvocationExpression>(caseExpr->mCaseExpression))
		{
			for (auto arg : invocateExpr->mArguments)
			{
				if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(arg))
				{
					hasVariable = true;
				}
				else if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(arg))
				{
					if ((unaryOpExpr->mOpToken != NULL) && (unaryOpExpr->mOpToken->mToken == BfToken_Out))
					{
						hasOut = true;
					}
				}
			}
		}

		if (hasVariable)
		{
			CheckVariableDeclaration(caseExpr, false, true, false);
		}

		// We can avoid clearing on mismatch if we can be sure we ONLY enter the true block on a match.
		// An example of requiring clearing is: if ((result case .Ok(out val)) || (force))
		if (hasOut)
			clearOutOnMismatch = !CheckVariableDeclaration(caseExpr, true, true, true);

		bool hadConditional = false;
		if (isPayloadEnum)
		{
			int dscrDataIdx;
			auto dscrType = caseValAddr.mType->ToTypeInstance()->GetDiscriminatorType(&dscrDataIdx);
			auto enumTagVal = mModule->LoadValue(mModule->ExtractValue(caseValAddr, NULL, 2));
			int uncondTagId = -1;
			mResult = mModule->TryCaseEnumMatch(caseValAddr, enumTagVal, caseExpr->mCaseExpression, NULL, NULL, NULL, uncondTagId, hadConditional, clearOutOnMismatch, false);
		}
		else
		{
			mResult = mModule->TryCaseTupleMatch(caseValAddr, tupleExpr, NULL, NULL, NULL, hadConditional, clearOutOnMismatch, false);
		}

		if (mResult)
			return;
	}

	if ((caseValAddr) && (IsVar(caseValAddr.mType)))
	{
		auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(caseExpr->mCaseExpression);
		if (invocationExpr != NULL)
		{
			for (auto expr : invocationExpr->mArguments)
			{
				if (expr == NULL)
					continue;

				if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(expr))
				{
					auto localVar = mModule->HandleVariableDeclaration(varDecl, BfTypedValue());
					if (localVar != NULL)
						localVar->mReadFromId = 0;
				}
			}
		}

		auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
		mResult = mModule->GetDefaultTypedValue(boolType);
		return;
	}

	auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
	BfTypedValue caseMatch;
	if (caseExpr->mCaseExpression != NULL)
    	caseMatch = mModule->CreateValueFromExpression(caseExpr->mCaseExpression, caseValAddr.mType, BfEvalExprFlags_AllowEnumId);
	if ((!caseMatch) || (!caseValAddr))
	{
		mResult = mModule->GetDefaultTypedValue(boolType);
		return;
	}

	if (caseValAddr.mType == caseMatch.mType)
	{
		if (((caseValAddr.mType->IsEnum()) && (caseValAddr.mType->IsStruct())) &&
			((caseMatch) && (caseMatch.mType->IsPayloadEnum()) && (caseMatch.mValue.IsConst())))
		{
			BfTypedValue enumTagVal = mModule->LoadValue(mModule->ExtractValue(caseValAddr, NULL, 2));
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(enumTagVal.mValue, caseMatch.mValue), boolType);
			return;
		}
	}
	else
	{
		// We need to get rid of the int-const for the 'scalar match'.  We get a full payload enum value so we can
		//  possibly use it in a user-defined comparison operator
		if ((caseMatch.mType->IsStruct()) && (caseMatch.mValue.IsConst()))
		{
			// Is it possible this could throw an error twice?  Hope not.
			caseMatch = mModule->CreateValueFromExpression(caseExpr->mCaseExpression, NULL, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
		}
	}

	PerformBinaryOperation(caseExpr->mCaseExpression, caseExpr->mValueExpression, BfBinaryOp_Equality, caseExpr->mEqualsNode, BfBinOpFlag_None, caseValAddr, caseMatch);
}

void BfExprEvaluator::Visit(BfTypedValueExpression* typedValueExpr)
{
	mResult = typedValueExpr->mTypedValue;
}

static bool IsCharType(BfTypeCode typeCode)
{
	switch (typeCode)
	{
	case BfTypeCode_Char8:
	case BfTypeCode_Char16:
	case BfTypeCode_Char32:
		return true;
	default:
		return false;
	}
}

bool BfExprEvaluator::CheckForMethodName(BfAstNode* refNode, BfTypeInstance* typeInst, const StringImpl& findName)
{
	BF_ASSERT((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0);

	auto autoComplete = GetAutoComplete();

	while (typeInst != NULL)
	{
		auto typeDef = typeInst->mTypeDef;
		typeDef->PopulateMemberSets();

		BfMemberSetEntry* memberSetEntry;
		if (typeDef->mMethodSet.TryGetWith(findName, &memberSetEntry))
		{
			if (mModule->mCompiler->mResolvePassData != NULL)
				mModule->mCompiler->mResolvePassData->HandleMethodReference(refNode, typeDef, (BfMethodDef*)memberSetEntry->mMemberDef);

			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(refNode)))
			{
				autoComplete->SetDefinitionLocation(((BfMethodDef*)memberSetEntry->mMemberDef)->GetRefNode());
				if ((autoComplete->mResolveType == BfResolveType_GetSymbolInfo) && (autoComplete->mDefType == NULL))
				{
					autoComplete->mDefType = typeDef;
					autoComplete->mDefMethod = (BfMethodDef*)memberSetEntry->mMemberDef;
				}
			}

			if (mModule->mCompiler->mResolvePassData != NULL)
			{
				if (auto sourceClassifier = mModule->mCompiler->mResolvePassData->GetSourceClassifier(refNode))
					sourceClassifier->SetElementType(refNode, BfSourceElementType_Method);
			}

			mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NameOfSuccess);
			return true;
		}

		typeInst = typeInst->mBaseType;
	}

	return false;
}

bool BfExprEvaluator::IsVar(BfType* type, bool forceIgnoreWrites)
{
	if (type->IsVar())
		return true;
	if ((type->IsGenericParam()) && (!forceIgnoreWrites) && (!mModule->mBfIRBuilder->mIgnoreWrites))
	{
		BF_ASSERT(mModule->mIsComptimeModule);
		return true;
	}
	return false;
}

void BfExprEvaluator::GetLiteral(BfAstNode* refNode, const BfVariant& variant)
{
	switch (variant.mTypeCode)
	{
	case BfTypeCode_NullPtr:
	{
		auto nullType = mModule->ResolveTypeDef(mModule->mSystem->mTypeNullPtr);
		/*mResult = BfTypedValue(ConstantPointerNull::get((PointerType*) nullType->mIRType), nullType);*/
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConstNull(), nullType);
	}
	break;
	case BfTypeCode_CharPtr:
	{
		if ((mExpectingType != NULL) && (mExpectingType->IsSizedArray()))
		{
			auto sizedArray = (BfSizedArrayType*)mExpectingType;
			if (sizedArray->mElementType == mModule->GetPrimitiveType(BfTypeCode_Char8))
			{
				if (sizedArray->IsUndefSizedArray())
					sizedArray = mModule->CreateSizedArrayType(sizedArray->mElementType, variant.mString->GetLength());

				if (variant.mString->GetLength() > sizedArray->mElementCount)
				{
					mModule->Fail(StrFormat("String literal is too long to fit into '%s'", mModule->TypeToString(sizedArray).c_str()), refNode);
				}

				Array<BfIRValue> charValues;
				for (int i = 0; i < (int)BF_MIN(variant.mString->GetLength(), sizedArray->mElementCount); i++)
				{
					char c = (*variant.mString)[i];
					charValues.Add(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Char8, (int)(uint8)c));
				}

				if (sizedArray->mElementCount > charValues.size())
					charValues.Add(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Char8, 0));

				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(sizedArray), charValues), sizedArray);
				return;
			}
		}

		if ((mExpectingType == NULL) || (!mExpectingType->IsPointer()))
		{
			mResult = BfTypedValue(mModule->GetStringObjectValue(*variant.mString),
				mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
		}
		else
		{
			auto charType = mModule->GetPrimitiveType(BfTypeCode_Char8);
			auto charPtrType = mModule->CreatePointerType(charType);
			mResult = BfTypedValue(mModule->GetStringCharPtr(*variant.mString),
				charPtrType);
		}
	}
	break;
	case BfTypeCode_Boolean:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mUInt64), mModule->GetPrimitiveType(variant.mTypeCode));
		break;
	case BfTypeCode_Char8:
	case BfTypeCode_Char16:
	case BfTypeCode_Char32:
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:
	case BfTypeCode_IntPtr:
	case BfTypeCode_UIntPtr:
	case BfTypeCode_IntUnknown:
	case BfTypeCode_UIntUnknown:
		if ((mExpectingType != NULL) && (mExpectingType->IsIntegral()) && (mExpectingType->IsChar() == IsCharType(variant.mTypeCode)))
		{
			auto primType = (BfPrimitiveType*)mExpectingType;
			if (mModule->mSystem->DoesLiteralFit(primType->mTypeDef->mTypeCode, variant.mUInt64))
			{
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, variant.mUInt64), mExpectingType);
				break;
			}
		}

		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mUInt64), mModule->GetPrimitiveType(variant.mTypeCode));
		break;

	case BfTypeCode_Float:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mSingle), mModule->GetPrimitiveType(variant.mTypeCode));
		break;
	case BfTypeCode_Double:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mDouble), mModule->GetPrimitiveType(variant.mTypeCode));
		break;
	case BfTypeCode_StringId:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mUInt64), mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
		break;
	case BfTypeCode_Let:
		if (mExpectingType != NULL)
		{
			mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->MapType(mExpectingType)), mExpectingType);
			break;
		}
		mModule->Fail("Invalid undef literal", refNode);
		break;
	default:
		mModule->Fail("Invalid literal", refNode);
		break;
	}
}

void BfExprEvaluator::Visit(BfLiteralExpression* literalExpr)
{
	switch (literalExpr->mValue.mWarnType)
	{
	case BfWarning_BF4201_Only7Hex:
		mModule->Warn(BfWarning_BF4201_Only7Hex, "Only 7 hex digits specified. Add a leading zero to clarify intention.", literalExpr);
		break;
	case BfWarning_BF4202_TooManyHexForInt:
		mModule->Warn(BfWarning_BF4202_TooManyHexForInt, "Nine hex digits specified. If an 8-digit hex literal was not intended then add a leading zero to clarify.", literalExpr);
		break;
	}

	GetLiteral(literalExpr, literalExpr->mValue);
}

void BfExprEvaluator::Visit(BfStringInterpolationExpression* stringInterpolationExpression)
{
	if ((mBfEvalExprFlags & BfEvalExprFlags_StringInterpolateFormat) != 0)
	{
		BfVariant variant;
		variant.mTypeCode = BfTypeCode_CharPtr;
		variant.mString = stringInterpolationExpression->mString;
		GetLiteral(stringInterpolationExpression, variant);
		return;
	}

	//
	{
		SetAndRestoreValue<BfEvalExprFlags> prevEvalExprFlag(mBfEvalExprFlags);
		if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mConstEvalAttributeTypeDef)))
		{
			mModule->mAttributeState->mUsed = true;
			mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);
		}

		if (IsConstEval())
		{
			auto stringType = mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef);
			if (stringType != NULL)
			{
				SetAndRestoreValue<bool> prevUsedAsStatement(mUsedAsStatement, true);
				SizedArray<BfExpression*, 2> argExprs;
				argExprs.Add(stringInterpolationExpression);
				BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
				BfResolvedArgs argValues(&sizedArgExprs);
				ResolveArgValues(argValues, BfResolveArgsFlag_InsideStringInterpolationAlloc);
				auto result = MatchMethod(stringInterpolationExpression, NULL, BfTypedValue(stringType), false, false, "ConstF", argValues, BfMethodGenericArguments());
				if (result.mType == stringType)
				{
					mResult = result;
					return;
				}
			}

			mModule->Fail("Const evaluation of string interpolation not allowed", stringInterpolationExpression);
		}
	}

	if (stringInterpolationExpression->mAllocNode != NULL)
	{
		auto stringType = mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef)->ToTypeInstance();

		BfTokenNode* newToken = NULL;
		BfAllocTarget allocTarget;
		ResolveAllocTarget(allocTarget, stringInterpolationExpression->mAllocNode, newToken);
		//
		{
			SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NoAutoComplete));
			CreateObject(NULL, stringInterpolationExpression->mAllocNode, stringType);
		}
		BfTypedValue newString = mResult;
		BF_ASSERT(newString);

		SetAndRestoreValue<bool> prevUsedAsStatement(mUsedAsStatement, true);
		SizedArray<BfExpression*, 2> argExprs;
		argExprs.Add(stringInterpolationExpression);
		BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
		BfResolvedArgs argValues(&sizedArgExprs);
		ResolveArgValues(argValues, BfResolveArgsFlag_InsideStringInterpolationAlloc);
		MatchMethod(stringInterpolationExpression, NULL, newString, false, false, "AppendF", argValues, BfMethodGenericArguments());
		mResult = newString;

		return;
	}

	mModule->Fail("Invalid use of string interpolation expression. Consider adding an allocation specifier such as 'scope'.", stringInterpolationExpression);

	for (auto block : stringInterpolationExpression->mExpressions)
	{
		VisitChild(block);
	}
}

BfTypedValue BfExprEvaluator::LoadLocal(BfLocalVariable* varDecl, bool allowRef)
{
 	if (!mModule->mIsInsideAutoComplete)
		varDecl->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;

	// The Beef backend prefers readonly addrs since that reduces register pressure, whereas
	//  LLVM prefers values to avoid memory loads. This only applies to primitive types...
	bool preferValue = (varDecl->mResolvedType->IsPrimitiveType()) && (!mModule->IsTargetingBeefBackend());

	BfTypedValue localResult;
	if (varDecl->mIsThis)
	{
		return mModule->GetThis();
	}
	else if (varDecl->mConstValue)
	{
		localResult = BfTypedValue(varDecl->mConstValue, varDecl->mResolvedType, false);
	}
	else if (varDecl->mIsSplat)
	{
		if (!varDecl->mResolvedType->IsValuelessType())
			localResult = BfTypedValue(varDecl->mValue, varDecl->mResolvedType, BfTypedValueKind_SplatHead);
		else if ((varDecl->mResolvedType->IsRef()) && (!allowRef))
		{
			BF_ASSERT(varDecl->mResolvedType->IsValuelessType());
			localResult = BfTypedValue(varDecl->mValue, varDecl->mResolvedType->GetUnderlyingType());
		}
		else
			localResult = BfTypedValue(varDecl->mValue, varDecl->mResolvedType);
		//BF_ASSERT(varDecl->mValue.IsArg());
	}
	else if ((varDecl->mValue) && ((varDecl->mIsReadOnly && preferValue) || (!varDecl->mAddr)))
	{
		if ((varDecl->mResolvedType->IsRef()) && (!allowRef))
		{
			BfRefType* refType = (BfRefType*)varDecl->mResolvedType;
			BfType* innerType = refType->mElementType;

			if (innerType->IsGenericParam())
			{
				if (refType->mRefKind == BfRefType::RefKind_Mut)
				{
					localResult = BfTypedValue(varDecl->mValue, innerType, BfTypedValueKind_MutableValue);
					return localResult;
				}
				else
				{
					localResult = BfTypedValue(varDecl->mValue, innerType, BfTypedValueKind_Addr);
					return localResult;
				}
			}

			localResult = BfTypedValue(varDecl->mValue, innerType, varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
		}
		else
		{
			BfTypedValueKind kind;
			if ((varDecl->mResolvedType->IsComposite()) && (varDecl->mValue.IsArg()))
			{
				kind = varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr;
			}
			else
				kind = BfTypedValueKind_Value;
			localResult = BfTypedValue(varDecl->mValue, varDecl->mResolvedType, kind);
		}
	}
	else if (varDecl->mAddr)
	{
		if ((varDecl->mResolvedType->IsRef()) && (!allowRef))
		{
			BfRefType* refType = (BfRefType*)varDecl->mResolvedType;
			BfType* innerType = refType->mElementType;

			if (innerType->IsValuelessType())
			{
				if (refType->mRefKind == BfRefType::RefKind_Mut)
					return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), innerType, BfTypedValueKind_MutableValue);
				return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), innerType, varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
			}

			if (refType->mRefKind == BfRefType::RefKind_Mut)
			{
				if (innerType->IsGenericParam())
				{
					localResult = BfTypedValue(mModule->mBfIRBuilder->CreateAlignedLoad(varDecl->mAddr, varDecl->mResolvedType->mAlign), innerType, BfTypedValueKind_MutableValue);
					return localResult;
				}
			}

			localResult = BfTypedValue(mModule->mBfIRBuilder->CreateAlignedLoad(varDecl->mAddr, varDecl->mResolvedType->mAlign), innerType, varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
		}
		else if (varDecl->mHasLocalStructBacking)
		{
			// varDecl->mAddr is a "struct**"
			localResult = BfTypedValue(mModule->mBfIRBuilder->CreateAlignedLoad(varDecl->mAddr, varDecl->mResolvedType->mAlign), varDecl->mResolvedType, varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
		}
		else
			localResult = BfTypedValue(varDecl->mAddr, varDecl->mResolvedType, varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
	}
	else if (varDecl->mResolvedType->IsValuelessType())
	{
		if ((varDecl->mResolvedType->IsRef()) && (!allowRef))
		{
			BfRefType* refType = (BfRefType*)varDecl->mResolvedType;
			BfType* innerType = refType->mElementType;
			localResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), innerType, true);
			return localResult;
		}
		localResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), varDecl->mResolvedType, true);
	}
	else if (varDecl->mCompositeCount >= 0)
	{
		localResult = BfTypedValue(BfIRValue(), mModule->GetPrimitiveType(BfTypeCode_None));
	}
	else
	{
		BF_ASSERT((mModule->mCurMethodState->mClosureState != NULL) || (mModule->mBfIRBuilder->mIgnoreWrites));
		// Just temporary
		auto varType = varDecl->mResolvedType;
		auto allocType = varType;
		if (varType->IsRef())
		{
			BfRefType* refType = (BfRefType*)varType;
			allocType = refType->mElementType;
		}

		auto declType = varDecl->mResolvedType;
		if (declType->IsRef())
		{
			BfRefType* refType = (BfRefType*)declType;
			declType = refType->mElementType;
		}

		mModule->PopulateType(allocType);
		varDecl->mAddr = mModule->mBfIRBuilder->CreateAlloca(mModule->mBfIRBuilder->MapType(allocType));
		localResult = BfTypedValue(varDecl->mAddr, declType, true);
		return localResult;
	}

	if ((varDecl->mIsThis) && (localResult.mKind == BfTypedValueKind_Value))
		localResult.mKind = BfTypedValueKind_ThisValue;

	return localResult;
}

BfTypedValue BfExprEvaluator::LookupIdentifier(BfAstNode* refNode, const StringImpl& findName, bool ignoreInitialError, bool* hadError)
{
	int varSkipCount = 0;
	StringT<128> wantName;
	wantName.Reference(findName);
	if (findName.StartsWith('@'))
	{
		wantName = findName;
		while (wantName.StartsWith("@"))
		{
			if (wantName != "@return")
				varSkipCount++;
			wantName.Remove(0);
		}
	}

	if (wantName.IsEmpty())
	{
		mModule->Fail("Shadowed variable name expected after '@'", refNode);
	}

	if ((mModule->mCompiler->mCeMachine != NULL) && (mModule->mCompiler->mCeMachine->mDebugger != NULL) && (mModule->mCompiler->mCeMachine->mDebugger->mCurDbgState != NULL))
	{
		auto ceDebugger = mModule->mCompiler->mCeMachine->mDebugger;
		auto ceContext = ceDebugger->mCurDbgState->mCeContext;
		auto activeFrame = ceDebugger->mCurDbgState->mActiveFrame;
		if (activeFrame->mFunction->mDbgInfo != NULL)
		{
			int varSkipCountLeft = varSkipCount;
			int instIdx = activeFrame->GetInstIdx();

			for (int i = activeFrame->mFunction->mDbgInfo->mVariables.mSize - 1; i >= 0; i--)
			{
				auto& dbgVar = activeFrame->mFunction->mDbgInfo->mVariables[i];
				if (dbgVar.mName == wantName)
				{
					if (varSkipCountLeft > 0)
					{
						varSkipCountLeft--;
					}
					else if ((dbgVar.mValue.mKind == CeOperandKind_AllocaAddr) || (dbgVar.mValue.mKind == CeOperandKind_FrameOfs))
					{
						if ((instIdx >= dbgVar.mStartCodePos) && (instIdx < dbgVar.mEndCodePos))
						{
							return BfTypedValue(mModule->mBfIRBuilder->CreateConstAggCE(mModule->mBfIRBuilder->MapType(dbgVar.mType), activeFrame->mFrameAddr + dbgVar.mValue.mFrameOfs),
								dbgVar.mType, dbgVar.mIsConst ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
						}
					}
				}
			}
		}

		if (findName == "FR")
		{
			auto ptrType = mModule->CreatePointerType(mModule->GetPrimitiveType(BfTypeCode_UInt8));
			auto intVal = mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, activeFrame->mFrameAddr);
			auto ptrVal = mModule->mBfIRBuilder->CreateIntToPtr(intVal, mModule->mBfIRBuilder->MapType(ptrType));
			return BfTypedValue(ptrVal, ptrType);
		}
	}

	auto identifierNode = BfNodeDynCast<BfIdentifierNode>(refNode);
	if (mModule->mCurMethodState != NULL)
	{
		auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();

		auto checkMethodState = mModule->mCurMethodState;
		bool isMixinOuterVariablePass = false;

		while (checkMethodState != NULL)
		{
			BP_ZONE("LookupIdentifier:LocalVar");

			BfTypeInstance* closureTypeInst = NULL;
			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mClosureType != NULL) && (!checkMethodState->mClosureState->mCapturing))
			{
				closureTypeInst = mModule->mCurMethodState->mClosureState->mClosureType;
			}

			int varSkipCountLeft = varSkipCount;

			BfLocalVarEntry* entry;
			if (checkMethodState->mLocalVarSet.TryGetWith<StringImpl&>(wantName, &entry))
			{
				auto varDecl = entry->mLocalVar;

				if (varDecl != NULL)
					varSkipCountLeft -= varDecl->mNamePrefixCount;

				while ((varSkipCountLeft > 0) && (varDecl != NULL))
				{
					varDecl = varDecl->mShadowedLocal;
					varSkipCountLeft--;
				}

				if ((varDecl != NULL) && (varDecl->mNotCaptured))
				{
					mModule->Fail("Local variable is not captured", refNode);
				}

				if ((varSkipCountLeft == 0) && (varDecl != NULL))
				{
					if ((closureTypeInst != NULL) && (wantName == "this"))
						break;

					if ((varDecl->mCompositeCount >= 0) && ((mBfEvalExprFlags & BfEvalExprFlags_AllowParamsExpr) == 0))
					{
						mModule->Fail("Invalid use of 'params' parameter", refNode);
					}

					if (varDecl->mResolvedType->IsVoid())
					{
						if ((varDecl->mIsReadOnly) && (varDecl->mParamIdx == -2) && (varDecl->mParamFailed))
						{
							BF_ASSERT(mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend);
							mModule->Fail("The result of append allocations cannot be used in append size calculation methods. Consider moving all append allocations to the start of this method body.", refNode);
						}
					}

					BfTypedValue localResult = LoadLocal(varDecl);
					auto autoComplete = GetAutoComplete();
					if (identifierNode != NULL)
					{
						if (autoComplete != NULL)
						 	autoComplete->CheckLocalRef(identifierNode, varDecl);
						if (((mModule->mCurMethodState->mClosureState == NULL) || (mModule->mCurMethodState->mClosureState->mCapturing)) &&
						 	(mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCurMethodInstance != NULL) && (!mModule->mCurMethodState->IsTemporary()))
						 	mModule->mCompiler->mResolvePassData->HandleLocalReference(identifierNode, varDecl->mNameNode, mModule->mCurTypeInstance->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, varDecl->mLocalVarId);
					}

					if (!isMixinOuterVariablePass)
					{
 						mResultLocalVar = varDecl;
						mResultFieldInstance = NULL;
						mResultLocalVarRefNode = identifierNode;
					}
					return localResult;
				}
			}

			// Check for the captured locals.  It's important we do it here so we get local-first precedence still
			if (closureTypeInst != NULL)
			{
				int varSkipCountLeft = varSkipCount;

				closureTypeInst->mTypeDef->PopulateMemberSets();
				BfMemberSetEntry* memberSetEntry = NULL;
				if (closureTypeInst->mTypeDef->mFieldSet.TryGetWith((StringImpl&)findName, &memberSetEntry))
				{
					auto fieldDef = (BfFieldDef*)memberSetEntry->mMemberDef;
					auto& field = closureTypeInst->mFieldInstances[fieldDef->mIdx];
					if (!field.mResolvedType->IsValuelessType())
					{
						if (mModule->mCurMethodState->mClosureState->mCapturing)
						{
							mModule->mCurMethodState->mClosureState->mReferencedOuterClosureMembers.Add(&field);
							return mModule->GetDefaultTypedValue(field.mResolvedType);
						}

						auto localVar = mModule->mCurMethodState->mLocals[0];
						auto thisValue = localVar->mValue;
						mModule->mBfIRBuilder->PopulateType(localVar->mResolvedType);
						BfTypedValue result = BfTypedValue(mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue, 0, field.mDataIdx), field.mResolvedType, true);
						if (field.mResolvedType->IsRef())
						{
							auto refType = (BfRefType*)field.mResolvedType;
							auto underlyingType = refType->GetUnderlyingType();
							result = BfTypedValue(mModule->mBfIRBuilder->CreateLoad(result.mValue), underlyingType, true);
						}
						else if (fieldDef->mIsReadOnly)
							result = mModule->LoadValue(result);

						mResultLocalVar = localVar;
						mResultFieldInstance = &field;
						mResultLocalVarField = -(field.mMergedDataIdx + 1);

						return result;
					}
				}
			}

			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mCapturing) /*&& (checkMethodState->mClosureState->mIsLocalMethod)*/)
			{
				checkMethodState = checkMethodState->mPrevMethodState;
				continue;
			}

			// Allow local mixin to see outside variables during its processing -- since we don't actually "capture" those into params
			bool isLocalMixinProcessing = false;
			if ((checkMethodState->mClosureState != NULL) && (!checkMethodState->mClosureState->mCapturing) && (closureTypeInst == NULL) &&
				(mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Mixin))
				isLocalMixinProcessing = true;
			if (!isLocalMixinProcessing)
				break;

			isMixinOuterVariablePass = true;
			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	if ((mModule->mCurMethodInstance == NULL) && (mModule->mCurTypeInstance != NULL) && (mModule->mCurTypeInstance->IsEnum()))
	{
		if (findName == "_")
		{
			BfFieldDef* resolvingFieldDef = NULL;
			auto checkTypeState = mModule->mContext->mCurTypeState;
			while (checkTypeState != NULL)
			{
				if (checkTypeState->mCurFieldDef != NULL)
				{
					if (checkTypeState->mType == mModule->mCurTypeInstance)
						resolvingFieldDef = checkTypeState->mCurFieldDef;
				}
				checkTypeState = checkTypeState->mPrevState;
			}

			if ((resolvingFieldDef != NULL) &&
				(mModule->mCompiler->mResolvePassData != NULL) &&
				(!mModule->mCompiler->mResolvePassData->mParsers.IsEmpty()) &&
				(mModule->mCompiler->mResolvePassData->mParsers[0] == resolvingFieldDef->mFieldDeclaration->GetParser()) &&
				(GetAutoComplete() != NULL))
			{
				return mModule->GetDefaultTypedValue(mModule->mCurTypeInstance);
			}
			else if ((resolvingFieldDef != NULL) && (resolvingFieldDef->mIdx > 0))
			{
				auto enumType = mModule->mCurTypeInstance;
				if (!enumType->mFieldInstances.IsEmpty())
				{
					auto fieldInstance = &mModule->mCurTypeInstance->mFieldInstances[resolvingFieldDef->mIdx - 1];
					if ((fieldInstance->mConstIdx != -1) &&
						(fieldInstance->mResolvedType == mModule->mCurTypeInstance))
					{
						auto foreignConst = enumType->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
						auto retVal = mModule->ConstantToCurrent(foreignConst, enumType->mConstHolder, enumType);
						return BfTypedValue(retVal, enumType);
					}
				}
			}
		}
	}

	BfTypedValue thisValue = mModule->GetThis(false);

	bool forcedIFaceLookup = false;
	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsForeignMethodDef))
	{
		thisValue.mType = mModule->mCurMethodInstance->GetForeignType();
		forcedIFaceLookup = true;
	}

	if (thisValue)
	{
		if (findName == "this")
		{
			mModule->MarkUsingThis();
			return thisValue;
		}
		if (findName == "base")
		{
			auto baseValue = thisValue;
			if (baseValue.IsThis())
				baseValue.ToBase();

			if (mModule->GetActiveTypeDef()->mTypeCode != BfTypeCode_Extension)
			{
				MakeBaseConcrete(baseValue);
			}
			mModule->MarkUsingThis();
			return baseValue;
		}

		if (!mModule->mCurMethodState->HasNonStaticMixin())
		{
			mResultLocalVar = mModule->GetThisVariable();
			mResultFieldInstance = NULL;
			mResultLocalVarRefNode = identifierNode;
		}
	}

	if (!thisValue.HasType())
	{
		if ((mModule->mContext->mCurTypeState != NULL) && (mModule->mContext->mCurTypeState->mType == mModule->mCurTypeInstance) &&
			(mModule->mContext->mCurTypeState->mResolveKind == BfTypeState::ResolveKind_Attributes))
		{
			// Can't do static lookups yet
		}
		else
			thisValue = BfTypedValue(mModule->mCurTypeInstance);
	}

	BfTypedValue result;
	if (thisValue.HasType())
	{
		result = LookupField(identifierNode, thisValue, findName, BfLookupFieldFlag_IsImplicitThis);
		if (mResultFieldInstance == NULL)
		{
			mResultLocalVar = NULL;
			mResultLocalVarRefNode = NULL;
		}
	}

	if (mPropDef != NULL)
	{
		if (forcedIFaceLookup)
		{
			if (mPropTarget == thisValue)
			{
				mPropDefBypassVirtual = true;
				mOrigPropTarget = mModule->GetThis();
			}
		}
	}
	if ((!result) && (mPropDef == NULL))
	{
		if (mModule->mContext->mCurTypeState != NULL)
		{
			// This is not necessarily true since we changed ConstResolver
			//BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mTypeInstance);

			BfGlobalLookup globalLookup;
			globalLookup.mKind = BfGlobalLookup::Kind_Field;
			globalLookup.mName = findName;
			mModule->PopulateGlobalContainersList(globalLookup);
			for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
			{
				if (globalContainer.mTypeInst == NULL)
					continue;
				thisValue = BfTypedValue(globalContainer.mTypeInst);
				result = LookupField(identifierNode, thisValue, findName);
				if ((result) || (mPropDef != NULL))
					return result;
			}
		}

		auto staticSearch = mModule->GetStaticSearch();
		if (staticSearch != NULL)
		{
			for (auto typeInst : staticSearch->mStaticTypes)
			{
				thisValue = BfTypedValue(typeInst);
				result = LookupField(identifierNode, thisValue, findName);
				if ((result) || (mPropDef != NULL))
					return result;
			}
		}
	}

	if ((!result) && (identifierNode != NULL))
	{
		result = mModule->TryLookupGenericConstVaue(identifierNode, mExpectingType);
		if ((mBfEvalExprFlags & (BfEvalExprFlags_Comptime | BfEvalExprFlags_AllowGenericConstValue)) == BfEvalExprFlags_Comptime)
		{
			if (result.mKind == BfTypedValueKind_GenericConstValue)
			{
				auto genericParamDef = mModule->GetGenericParamInstance((BfGenericParamType*)result.mType);
				if ((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
				{
					auto genericTypeConstraint = genericParamDef->mTypeConstraint;
					if (genericTypeConstraint != NULL)
					{
						auto primType = genericTypeConstraint->ToPrimitiveType();
						if (primType != NULL)
						{
							BfTypedValue result;
							result.mKind = BfTypedValueKind_Value;
							result.mType = genericTypeConstraint;
							result.mValue = mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->GetPrimitiveType(primType->mTypeDef->mTypeCode));
							return result;
						}
					}
					else
					{
						BF_FATAL("Error");
					}
				}
			}
		}
		mModule->FixValueActualization(result);
	}

	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (findName == "@this"))
	{
		if (mModule->mCurMethodState->mClosureState->mCapturing)
		{
			if (mModule->mCurMethodState->mClosureState->mDelegateType != NULL)
			{
				mModule->mCurMethodState->mClosureState->mCapturedDelegateSelf = true;
				return mModule->GetDefaultTypedValue(mModule->mCurMethodState->mClosureState->mDelegateType);
			}
		}
		else
		{
			if (!mModule->mCurMethodState->mLocals.IsEmpty())
			{
				auto thisLocal = mModule->mCurMethodState->mLocals[0];
				if (thisLocal->mIsThis)
					return BfTypedValue(mModule->mBfIRBuilder->CreateLoad(thisLocal->mAddr), thisLocal->mResolvedType);
			}
		}
	}

	return result;
}

BfTypedValue BfExprEvaluator::LookupIdentifier(BfIdentifierNode* identifierNode, bool ignoreInitialError, bool* hadError)
{
	auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(identifierNode);
	if (qualifiedNameNode != NULL)
	{
		LookupQualifiedName(qualifiedNameNode, ignoreInitialError, hadError);
		auto qualifiedResult = mResult;
		mResult = BfTypedValue();
		return qualifiedResult;
	}

	StringT<128> identifierStr;
	identifierNode->ToString(identifierStr);
	return LookupIdentifier(identifierNode, identifierStr, ignoreInitialError, hadError);
}

void BfExprEvaluator::Visit(BfIdentifierNode* identifierNode)
{
	auto autoComplete = GetAutoComplete();

	if (autoComplete != NULL)
		autoComplete->CheckIdentifier(identifierNode, true);

	mResult = LookupIdentifier(identifierNode);
	if ((!mResult) && (mPropDef == NULL))
	{
		mModule->CheckTypeRefFixit(identifierNode);
		if ((autoComplete != NULL) && (autoComplete->CheckFixit(identifierNode)))
		{
			if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
			{
				// During this phase we don't have lambda and local method params available so they would result in erroneous fixits
			}
			else if (mModule->mCurMethodInstance != NULL)
			{
				BfType* fieldType = mExpectingType;
				if (fieldType == NULL)
					fieldType = mModule->mContext->mBfObjectType;

				autoComplete->FixitAddMember(mModule->mCurTypeInstance, fieldType, identifierNode->ToString(), true, mModule->mCurTypeInstance);
				if (!mModule->mCurMethodInstance->mMethodDef->mIsStatic)
					autoComplete->FixitAddMember(mModule->mCurTypeInstance, fieldType, identifierNode->ToString(), false, mModule->mCurTypeInstance);

				for (auto typeDef : mModule->mSystem->mTypeDefs)
				{
					if (!typeDef->mIsCombinedPartial)
						continue;
					if (!typeDef->IsGlobalsContainer())
						continue;

					typeDef->PopulateMemberSets();

					String findName = identifierNode->ToString();
					BfMemberSetEntry* memberSetEntry;
					if ((typeDef->mMethodSet.TryGetWith(findName, &memberSetEntry)) ||
						(typeDef->mFieldSet.TryGetWith(findName, &memberSetEntry)) ||
						(typeDef->mPropertySet.TryGetWith(findName, &memberSetEntry)))
					{
						if (mModule->GetActiveTypeDef()->mProject->ContainsReference(typeDef->mProject))
							autoComplete->FixitAddNamespace(identifierNode, typeDef->mNamespace.ToString());
					}
				}
			}
		}

		if (((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0) && (mModule->mCurTypeInstance != NULL) && (CheckForMethodName(identifierNode, mModule->mCurTypeInstance, identifierNode->ToString())))
			return;

		if ((mBfEvalExprFlags & BfEvalExprFlags_NoLookupError) == 0)
			mModule->Fail("Identifier not found", identifierNode);
	}
}

void BfExprEvaluator::Visit(BfAttributedIdentifierNode* attrIdentifierNode)
{
	if ((mModule->mAttributeState != NULL))
	{
		mModule->mAttributeState->mCustomAttributes = mModule->GetCustomAttributes(attrIdentifierNode->mAttributes, mModule->mAttributeState->mTarget);
		VisitChild(attrIdentifierNode->mIdentifier);
	}
	else
	{
		BfAttributeState attributeState;
		attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_MemberAccess);
		SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);
		mModule->mAttributeState->mCustomAttributes = mModule->GetCustomAttributes(attrIdentifierNode->mAttributes, mModule->mAttributeState->mTarget);
		VisitChild(attrIdentifierNode->mIdentifier);
	}
}

static int gPropIdx = 0;

void BfExprEvaluator::FixitAddMember(BfTypeInstance* typeInst, BfType* fieldType, const StringImpl& fieldName, bool isStatic)
{
	if (fieldType == NULL)
	{
		fieldType = mExpectingType;
	}

	if (fieldType != NULL)
	{
		if (fieldType->IsRef())
			fieldType = fieldType->GetUnderlyingType();
	}

	mModule->mCompiler->mResolvePassData->mAutoComplete->FixitAddMember(typeInst, fieldType, fieldName, isStatic, mModule->mCurTypeInstance);
}

BfTypedValue BfExprEvaluator::TryArrowLookup(BfTypedValue typedValue, BfTokenNode* arrowToken)
{
	auto arrowValue = PerformUnaryOperation_TryOperator(typedValue, NULL, BfUnaryOp_Arrow, arrowToken, BfUnaryOpFlag_None);
	if (arrowValue)
		return arrowValue;
	if (mModule->PreFail())
		mModule->Fail(StrFormat("Type '%s' does not contain a '->' operator", mModule->TypeToString(typedValue.mType).c_str()), arrowToken);
	return typedValue;
}

BfTypedValue BfExprEvaluator::LoadProperty(BfAstNode* targetSrc, BfTypedValue target, BfTypeInstance* typeInstance, BfPropertyDef* prop, BfLookupFieldFlags flags, BfCheckedKind checkedKind, bool isInlined)
{
	if ((flags & BfLookupFieldFlag_IsAnonymous) == 0)
		mModule->SetElementType(targetSrc, BfSourceElementType_Method);

	if ((!prop->mIsStatic) && ((flags & BfLookupFieldFlag_IsImplicitThis) != 0))
		mModule->MarkUsingThis();

	mPropSrc = targetSrc;
	mPropDef = prop;
	mPropCheckedKind = checkedKind;
	if (isInlined)
		mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_ForceInline);

	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
	{
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mFriendAttributeTypeDef))
		{
			mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_Friend);
			mModule->mAttributeState->mUsed = true;
		}
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mDisableObjectAccessChecksAttributeTypeDef))
		{
			mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_DisableObjectAccessChecks);
			mModule->mAttributeState->mUsed = true;
		}
	}

	if (mPropDef->mIsStatic)
	{
		if ((target) && ((flags & BfLookupFieldFlag_IsImplicitThis) == 0) && (!typeInstance->mTypeDef->IsGlobalsContainer()))
		{
			//CS0176: Member 'Program.sVal' cannot be accessed with an instance reference; qualify it with a type name instead
			mModule->Fail(StrFormat("Property '%s.%s' cannot be accessed with an instance reference; qualify it with a type name instead",
				mModule->TypeToString(typeInstance).c_str(), mPropDef->mName.c_str()), targetSrc);
		}
	}

	bool isBaseLookup = (target.mType) && (typeInstance != target.mType);
	if ((isBaseLookup) && (target.mType->IsWrappableType()))
		isBaseLookup = false;

	if (prop->mIsStatic)
		mPropTarget = BfTypedValue(typeInstance);
	else if (isBaseLookup)
	{
		if (target.mValue.IsFake())
		{
			mPropTarget = BfTypedValue(target.mValue, typeInstance, target.mKind);
		}
		else
		{
			mPropTarget = mModule->Cast(targetSrc, target, typeInstance);
			BF_ASSERT(mPropTarget);
		}
	}
	else
		mPropTarget = target;

	if (mPropTarget.mType->IsStructPtr())
	{
		mPropTarget = mModule->LoadValue(mPropTarget);
		mPropTarget = BfTypedValue(mPropTarget.mValue, mPropTarget.mType->GetUnderlyingType(), mPropTarget.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
	}

	mOrigPropTarget = mPropTarget;
	if (prop->mIsStatic)
		mOrigPropTarget = target;

	if ((flags & BfLookupFieldFlag_IsAnonymous) == 0)
	{
		auto autoComplete = GetAutoComplete();
		auto resolvePassData = mModule->mCompiler->mResolvePassData;
		if (((autoComplete != NULL) && (autoComplete->mIsGetDefinition)) ||
			((resolvePassData != NULL) && (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Property)))
		{
			BfPropertyDef* basePropDef = mPropDef;
			BfTypeInstance* baseTypeInst = typeInstance;
			mModule->GetBasePropertyDef(basePropDef, baseTypeInst);
			resolvePassData->HandlePropertyReference(targetSrc, baseTypeInst->mTypeDef, basePropDef);
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(targetSrc)))
			{
				if (autoComplete->mIsGetDefinition)
				{
					//NOTE: passing 'force=true' in here causes https://github.com/beefytech/Beef/issues/1064
					autoComplete->SetDefinitionLocation(basePropDef->GetRefNode());
				}
				autoComplete->mDefProp = basePropDef;
				autoComplete->mDefType = baseTypeInst->mTypeDef;
			}
		}

		if ((autoComplete != NULL) && (autoComplete->mResolveType == BfResolveType_GetResultString) && (autoComplete->IsAutocompleteNode(targetSrc)))
		{
			BfPropertyDef* basePropDef = mPropDef;
			BfTypeInstance* baseTypeInst = typeInstance;
			mModule->GetBasePropertyDef(basePropDef, baseTypeInst);

			autoComplete->mResultString = ":";
			autoComplete->mResultString += mModule->TypeToString(baseTypeInst);
			autoComplete->mResultString += ".";
			autoComplete->mResultString += basePropDef->mName;
		}
	}

	// Check for direct auto-property access
	if ((target.mType == mModule->mCurTypeInstance) && ((flags & BfLookupFieldFlag_BindOnly) == 0))
	{
		if (auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(mPropDef->mFieldDeclaration))
		{
			if ((typeInstance->mTypeDef->HasAutoProperty(propertyDeclaration)) && (propertyDeclaration->mVirtualSpecifier == NULL))
			{
				BfMethodDef* getter = GetPropertyMethodDef(mPropDef, BfMethodType_PropertyGetter, BfCheckedKind_NotSet, mPropTarget);
				BfMethodDef* setter = GetPropertyMethodDef(mPropDef, BfMethodType_PropertySetter, BfCheckedKind_NotSet, mPropTarget);

				bool optAllowed = true;
				if ((getter != NULL) && (getter->mBody != NULL))
					optAllowed = false;
				if ((setter != NULL) && (setter->mBody != NULL))
					optAllowed = false;

				if (optAllowed)
				{
					auto autoFieldName = typeInstance->mTypeDef->GetAutoPropertyName(propertyDeclaration);
					auto result = LookupField(targetSrc, target, autoFieldName, (BfLookupFieldFlags)(BfLookupFieldFlag_IgnoreProtection | BfLookupFieldFlag_IsImplicitThis));
					if (result)
					{
						bool needsCopy = true;

						if (BfNodeIsA<BfRefTypeRef>(prop->mTypeRef))
						{
							// Allow full ref
						}
						else
						{
							if (setter == NULL)
							{
								if (((mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)) &&
									(target.mType == mModule->mCurTypeInstance))
								{
									// Allow writing inside ctor
								}
								else
								{
									result.MakeReadOnly();
									needsCopy = false;
								}
							}

							if (result.mKind == BfTypedValueKind_Addr)
								result.mKind = BfTypedValueKind_CopyOnMutateAddr;
						}

						mPropDef = NULL;
						mPropSrc = NULL;
						mOrigPropTarget = BfTypedValue();
						return result;
					}
				}
			}
		}
	}

	SetAndRestoreValue<BfTypedValue> prevResult(mResult, target);
	CheckResultForReading(mResult);
	return BfTypedValue();
}

BfTypedValue BfExprEvaluator::LoadField(BfAstNode* targetSrc, BfTypedValue target, BfTypeInstance* typeInstance, BfFieldDef* fieldDef, BfLookupFieldFlags flags)
{
	if (fieldDef->mIsProperty)
	{
		BfPropertyDef* propDef = (BfPropertyDef*)fieldDef;
		return LoadProperty(targetSrc, target, typeInstance, propDef, flags, BfCheckedKind_NotSet, false);
	}

	bool isFailurePass = (flags & BfLookupFieldFlag_IsFailurePass) != 0;

	auto fieldInstance = &typeInstance->mFieldInstances[fieldDef->mIdx];

	bool isResolvingFields = typeInstance->mResolvingConstField || typeInstance->mResolvingVarField;

	if (fieldDef->mIsVolatile)
		mIsVolatileReference = true;

	if (fieldInstance->mCustomAttributes != NULL)
		mModule->CheckErrorAttributes(fieldInstance->mOwner, NULL, fieldInstance, fieldInstance->mCustomAttributes, targetSrc);

	if (isFailurePass)
	{
		if (mModule->GetCeDbgState() == NULL)
			mModule->Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", mModule->TypeToString(typeInstance).c_str(), fieldDef->mName.c_str()), targetSrc);
	}

	auto resolvePassData = mModule->mCompiler->mResolvePassData;
	if ((resolvePassData != NULL) && (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field) && ((flags & BfLookupFieldFlag_IsAnonymous) == 0))
	{
		resolvePassData->HandleFieldReference(targetSrc, typeInstance->mTypeDef, fieldDef);
	}

	if ((!typeInstance->mTypeFailed) && (!isResolvingFields) && (typeInstance->IsIncomplete()))
	{
		if ((fieldInstance->mResolvedType == NULL) ||
			(!fieldDef->mIsStatic))
			mModule->PopulateType(typeInstance, BfPopulateType_Data);
	}

	if (fieldInstance->mResolvedType == NULL)
	{
		if (mModule->mCompiler->EnsureCeUnpaused(typeInstance))
		{
			BF_ASSERT((typeInstance->mTypeFailed) || (isResolvingFields));
		}
		return BfTypedValue();
	}

	if (fieldInstance->mFieldIdx == -1)
	{
		mModule->AssertErrorState();
		return BfTypedValue();
	}

	if ((!fieldDef->mIsStatic) && ((flags & BfLookupFieldFlag_IsImplicitThis) != 0))
		mModule->MarkUsingThis();

	mResultFieldInstance = fieldInstance;

	// Are we accessing a 'var' field that has not yet been resolved?
	if (IsVar(fieldInstance->mResolvedType))
	{
		// This can happen if we have one var field referencing another var field
		fieldInstance->mResolvedType = mModule->ResolveVarFieldType(typeInstance, fieldInstance, fieldDef);
		if (fieldInstance->mResolvedType == NULL)
			return BfTypedValue();
	}

	auto resolvedFieldType = fieldInstance->mResolvedType;
	if (fieldInstance->mIsEnumPayloadCase)
	{
		resolvedFieldType = typeInstance;
	}

	mModule->PopulateType(resolvedFieldType, BfPopulateType_Data);
	mModule->AddDependency(typeInstance, mModule->mCurTypeInstance, fieldDef->mIsConst ? BfDependencyMap::DependencyFlag_ConstValue : BfDependencyMap::DependencyFlag_ReadFields);
	if (fieldInstance->mHadConstEval)
	{
		mModule->AddDependency(typeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ConstEvalConstField);
		if ((mModule->mContext->mCurTypeState != NULL) && (mModule->mContext->mCurTypeState->mCurFieldDef != NULL))
		{
			// If we're initializing another const field then also set it as having const eval
			auto resolvingFieldInstance = &mModule->mContext->mCurTypeState->mType->ToTypeInstance()->mFieldInstances[mModule->mContext->mCurTypeState->mCurFieldDef->mIdx];
			if (resolvingFieldInstance->GetFieldDef()->mIsConst)
				resolvingFieldInstance->mHadConstEval = true;
		}
	}

	if ((flags & BfLookupFieldFlag_IsAnonymous) == 0)
	{
		auto autoComplete = GetAutoComplete();
		if (autoComplete != NULL)
		{
			autoComplete->CheckFieldRef(BfNodeDynCast<BfIdentifierNode>(targetSrc), fieldInstance);
			if ((autoComplete->mResolveType == BfResolveType_GetResultString) && (autoComplete->IsAutocompleteNode(targetSrc)))
			{
				autoComplete->mResultString = ":";
				autoComplete->mResultString += mModule->TypeToString(fieldInstance->mResolvedType);
				autoComplete->mResultString += " ";
				autoComplete->mResultString += mModule->TypeToString(typeInstance);
				autoComplete->mResultString += ".";
				autoComplete->mResultString += fieldDef->mName;

				if (fieldInstance->mConstIdx != -1)
				{
					String constStr = autoComplete->ConstantToString(typeInstance->mConstHolder, BfIRValue(BfIRValueFlags_Const, fieldInstance->mConstIdx));
					if (!constStr.IsEmpty())
					{
						autoComplete->mResultString += " = ";
						if (constStr.StartsWith(':'))
							autoComplete->mResultString.Append(StringView(constStr, 1, constStr.mLength - 1));
						else
							autoComplete->mResultString += constStr;
					}
				}

				auto fieldDecl = fieldInstance->GetFieldDef()->GetFieldDeclaration();
				if ((fieldDecl != NULL) && (fieldDecl->mDocumentation != NULL))
				{
					String docString;
					fieldDecl->mDocumentation->GetDocString(docString);
					autoComplete->mResultString += "\x03";
					autoComplete->mResultString += docString;
				}
			}
		}
	}

	if (fieldDef->mIsStatic)
	{
		if ((target) && ((flags & BfLookupFieldFlag_IsImplicitThis) == 0) && (!typeInstance->mTypeDef->IsGlobalsContainer()))
		{
			//CS0176: Member 'Program.sVal' cannot be accessed with an instance reference; qualify it with a type name instead
			mModule->Fail(StrFormat("Member '%s.%s' cannot be accessed with an instance reference; qualify it with a type name instead",
				mModule->TypeToString(typeInstance).c_str(), fieldDef->mName.c_str()), targetSrc);
		}

		// Target must be an implicit 'this', or an error (accessing a static with a non-static target).
		//  Not actually needed in either case since this is a static lookup.
		mResultLocalVar = NULL;
	}

	bool isConst = false;
	if (fieldDef->mIsConst)
	{
		isConst = true;
		auto fieldDef = fieldInstance->GetFieldDef();
		if ((resolvedFieldType->IsPointer()) && (fieldDef->mIsExtern))
			isConst = false;
	}

	if (isConst)
	{
		if (fieldInstance->mIsEnumPayloadCase)
		{
			auto dscrType = typeInstance->GetDiscriminatorType();

			mModule->mBfIRBuilder->PopulateType(typeInstance);
			int tagIdx = -fieldInstance->mDataIdx - 1;
			if ((mBfEvalExprFlags & BfEvalExprFlags_AllowEnumId) != 0)
			{
				return BfTypedValue(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), fieldInstance->mOwner);
			}
			mModule->PopulateType(fieldInstance->mOwner, BfPopulateType_Data);

			if (auto fieldTypeInstance = fieldInstance->mResolvedType->ToTypeInstance())
			{
				bool hasFields = false;
				for (auto& fieldInstance : fieldTypeInstance->mFieldInstances)
				{
					if (!fieldInstance.mResolvedType->IsVoid())
						hasFields = true;
				}
				if (hasFields)
					mModule->FailAfter("Enum payload arguments expected", targetSrc);
			}

			SizedArray<BfIRValue, 3> values;
			values.Add(mModule->mBfIRBuilder->CreateConstAggZero(mModule->mBfIRBuilder->MapType(typeInstance->mBaseType)));
			values.Add(mModule->GetDefaultValue(typeInstance->GetUnionInnerType()));
			values.Add(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx));
			return BfTypedValue(mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(typeInstance), values), typeInstance);
		}

		if (fieldInstance->mConstIdx == -1)
		{
			if ((mBfEvalExprFlags & BfEvalExprFlags_DeclType) != 0)
			{
				// We don't need a real value
				return BfTypedValue(mModule->GetDefaultValue(resolvedFieldType), resolvedFieldType);
			}

			typeInstance->mModule->ResolveConstField(typeInstance, fieldInstance, fieldDef);
			if (fieldInstance->mConstIdx == -1)
				return BfTypedValue(mModule->GetDefaultValue(resolvedFieldType), resolvedFieldType);
		}

		BF_ASSERT(fieldInstance->mConstIdx != -1);

		auto foreignConst = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
		auto retVal = mModule->ConstantToCurrent(foreignConst, typeInstance->mConstHolder, resolvedFieldType);
		return BfTypedValue(retVal, resolvedFieldType);
	}
	else if (fieldDef->mIsStatic)
	{
		mModule->CheckStaticAccess(typeInstance);
		auto retVal = mModule->ReferenceStaticField(fieldInstance);
		bool isStaticCtor = (mModule->mCurMethodInstance != NULL) &&
			(mModule->mCurMethodInstance->mMethodDef->IsCtorOrInit()) &&
			(mModule->mCurMethodInstance->mMethodDef->mIsStatic);

		if ((mModule->mCompiler->mOptions.mRuntimeChecks) && (fieldInstance->IsAppendedObject()) && (!mModule->mBfIRBuilder->mIgnoreWrites) &&
			(!mModule->IsSkippingExtraResolveChecks()))
		{
			auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			auto intPtrType = mModule->CreatePointerType(intType);
			auto intPtrVal = mModule->mBfIRBuilder->CreateBitCast(retVal.mValue, mModule->mBfIRBuilder->MapType(intPtrType));
			auto intVal = mModule->mBfIRBuilder->CreateLoad(intPtrVal);

			auto oobBlock = mModule->mBfIRBuilder->CreateBlock("oob", true);
			auto contBlock = mModule->mBfIRBuilder->CreateBlock("cont", true);

			auto cmpRes = mModule->mBfIRBuilder->CreateCmpEQ(intVal, mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0));
			mModule->mBfIRBuilder->CreateCondBr(cmpRes, oobBlock, contBlock);

			mModule->mBfIRBuilder->SetInsertPoint(oobBlock);
			auto internalType = mModule->ResolveTypeDef(mModule->mCompiler->mInternalTypeDef);
			auto oobFunc = mModule->GetMethodByName(internalType->ToTypeInstance(), "ThrowObjectNotInitialized");
			if (oobFunc.mFunc)
			{
				if (mModule->mIsComptimeModule)
					mModule->mCompiler->mCeMachine->QueueMethod(oobFunc.mMethodInstance, oobFunc.mFunc);

				SizedArray<BfIRValue, 1> args;
				args.push_back(mModule->GetConstValue(0));
				mModule->mBfIRBuilder->CreateCall(oobFunc.mFunc, args);
				mModule->mBfIRBuilder->CreateUnreachable();
			}
			else
			{
				mModule->Fail("System.Internal class must contain method 'ThrowObjectNotInitialized'", fieldDef->GetRefNode());
			}

			mModule->mBfIRBuilder->SetInsertPoint(contBlock);
		}

		if ((fieldDef->mIsReadOnly) && (!isStaticCtor))
		{
			if (retVal.IsAddr())
				retVal.mKind = BfTypedValueKind_ReadOnlyAddr;
		}
		else
			mIsHeapReference = true;
		return retVal;
	}
	else if (!target)
	{
		if (((mBfEvalExprFlags & BfEvalExprFlags_NameOf) == 0) && (mModule->PreFail()))
		{
			if ((flags & BfLookupFieldFlag_CheckingOuter) != 0)
				mModule->Fail(StrFormat("An instance reference is required to reference non-static outer field '%s.%s'", mModule->TypeToString(typeInstance).c_str(), fieldDef->mName.c_str()),
					targetSrc);
			else if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend))
			{
				if ((mBfEvalExprFlags & BfEvalExprFlags_DeclType) == 0)
					mModule->Fail(StrFormat("Cannot reference field '%s' before append allocations", fieldDef->mName.c_str()), targetSrc);
			}
			else
				mModule->Fail(StrFormat("Cannot reference non-static field '%s' from a static method", fieldDef->mName.c_str()), targetSrc);
		}
		return mModule->GetDefaultTypedValue(resolvedFieldType, false, BfDefaultValueKind_Addr);
	}

	if (resolvedFieldType->IsValuelessType())
	{
		return BfTypedValue(BfIRValue::sValueless, resolvedFieldType, true);
	}

	if ((mResultLocalVar != NULL) && (fieldInstance->mMergedDataIdx != -1))
	{
		if (mResultLocalVarFieldCount != 1)
		{
			fieldInstance->GetDataRange(mResultLocalVarField, mResultLocalVarFieldCount);
			mResultLocalVarRefNode = targetSrc;
		}
	}

	if ((typeInstance->IsIncomplete()) && (!typeInstance->mNeedsMethodProcessing))
	{
		BF_ASSERT(typeInstance->mTypeFailed || (mModule->mCurMethodState == NULL) || (mModule->mCurMethodState->mTempKind != BfMethodState::TempKind_None) || (mModule->mCompiler->IsAutocomplete()));
		return mModule->GetDefaultTypedValue(resolvedFieldType);
	}

	bool isTemporary = target.IsTempAddr();
	bool wantsLoadValue = false;
	bool wantsReadOnly = false;
	if ((fieldDef->mIsReadOnly) && (mModule->mCurMethodInstance != NULL) && ((!mModule->mCurMethodInstance->mMethodDef->IsCtorOrInit()) || (!target.IsThis())))
		wantsReadOnly = true;

	bool isComposite = target.mType->IsComposite();
	if ((isComposite) && (!target.mType->IsTypedPrimitive()) && (!target.IsAddr()))
		isTemporary = true;
	if ((isComposite) && (!target.IsAddr()))
		wantsLoadValue = true;

	if ((target.mType->IsWrappableType()) && (!target.mType->IsPointer()))
	{
		BfTypeInstance* primStructType = mModule->GetWrappedStructType(target.mType);
		BfIRValue allocaInst = mModule->CreateAlloca(primStructType, true, "tmpStruct");
		BfIRValue elementAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1);
		mModule->mBfIRBuilder->CreateStore(target.mValue, elementAddr);
		target = BfTypedValue(allocaInst, primStructType, true);
	}

	BfTypedValue targetValue;
	if ((target.mType != typeInstance) && (!target.IsSplat()))
	{
		if ((!isComposite) || (target.IsAddr()))
			targetValue = BfTypedValue(mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->MapTypeInstPtr(typeInstance)), typeInstance);
		else
		{
			BfIRValue curVal = target.mValue;
			auto baseCheckType = target.mType->ToTypeInstance();
			while (baseCheckType != typeInstance)
			{
				curVal = mModule->mBfIRBuilder->CreateExtractValue(curVal, 0);
				baseCheckType = baseCheckType->mBaseType;
			}
			targetValue = BfTypedValue(curVal, typeInstance);
		}
	}
	else
		targetValue = target;

	bool doAccessCheck = true;

	if ((flags & BfLookupFieldFlag_BindOnly) != 0)
		doAccessCheck = false;
	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mDisableObjectAccessChecksAttributeTypeDef)))
		doAccessCheck = false;

	if (target.IsThis())
	{
		if (!mModule->mCurMethodState->mMayNeedThisAccessCheck)
			doAccessCheck = false;
	}
	if (doAccessCheck)
		mModule->EmitObjectAccessCheck(target);

	if (fieldInstance->mDataIdx < 0)
	{
		BF_ASSERT(typeInstance->mTypeFailed);
		return mModule->GetDefaultTypedValue(resolvedFieldType);
	}

	BfTypedValue retVal;
	if (targetValue.IsSplat())
	{
		retVal = mModule->ExtractValue(targetValue, fieldInstance, fieldInstance->mDataIdx);
	}
	else if ((target.mType->IsStruct()) && (!target.IsAddr()))
	{
		mModule->mBfIRBuilder->PopulateType(targetValue.mType);
		retVal = BfTypedValue(mModule->mBfIRBuilder->CreateExtractValue(targetValue.mValue, fieldInstance->mDataIdx/*, fieldDef->mName*/),
			resolvedFieldType);
	}
	else
	{
		mModule->mBfIRBuilder->PopulateType(typeInstance);

		if ((targetValue.IsAddr()) && (!typeInstance->IsValueType()))
			targetValue = mModule->LoadValue(targetValue);

		if (fieldInstance->IsAppendedObject())
		{
			auto elemPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(targetValue.mValue, 0, fieldInstance->mDataIdx);
			retVal = BfTypedValue(mModule->mBfIRBuilder->CreateBitCast(elemPtr, mModule->mBfIRBuilder->MapType(resolvedFieldType)), resolvedFieldType);
		}
		else
		{
			retVal = BfTypedValue(mModule->mBfIRBuilder->CreateInBoundsGEP(targetValue.mValue, 0, fieldInstance->mDataIdx/*, fieldDef->mName*/),
				resolvedFieldType, target.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
		}
	}
	
	if (typeInstance->mIsUnion)
	{
		auto unionInnerType = typeInstance->GetUnionInnerType();
		if (unionInnerType != resolvedFieldType)
		{
			if (!retVal.IsAddr())
				retVal = mModule->MakeAddressable(retVal);
			BfIRType llvmPtrType = mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->MapType(resolvedFieldType));
			retVal.mValue = mModule->mBfIRBuilder->CreateBitCast(retVal.mValue, llvmPtrType);
		}
	}	

	if ((fieldDef->mIsVolatile) && (retVal.IsAddr()))
		retVal.mKind = BfTypedValueKind_VolatileAddr;

	if (wantsLoadValue)
		retVal = mModule->LoadValue(retVal, NULL, mIsVolatileReference);
	else
	{
		if ((wantsReadOnly) && (retVal.IsAddr()) && (!retVal.IsReadOnly()))
			retVal.mKind = BfTypedValueKind_ReadOnlyAddr;
		else if ((target.IsCopyOnMutate()) && (retVal.IsAddr()))
			retVal.mKind = BfTypedValueKind_CopyOnMutateAddr_Derived;

		mIsHeapReference = true;
	}

	if (isTemporary)
	{
		if (retVal.IsAddr())
			retVal.mKind = BfTypedValueKind_TempAddr;
	}

	return retVal;
}

BfTypedValue BfExprEvaluator::LookupField(BfAstNode* targetSrc, BfTypedValue target, const StringImpl& fieldName, BfLookupFieldFlags flags)
{
	if ((target.mType != NULL && (target.mType->IsGenericParam())))
	{
		auto genericParamType = (BfGenericParamType*)target.mType;
		auto genericParamInst = mModule->GetGenericParamInstance(genericParamType);

		if (target.mValue)
		{
			for (auto iface : genericParamInst->mInterfaceConstraints)
			{
				auto result = LookupField(targetSrc, BfTypedValue(target.mValue, iface), fieldName, flags);
				if ((result) || (mPropDef != NULL))
				{
					return result;
				}
			}
		}

		bool isUnspecializedSection = false;
		if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
			isUnspecializedSection = (mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized);
		else
			isUnspecializedSection = (mModule->mCurTypeInstance != NULL) && (mModule->mCurTypeInstance->IsUnspecializedType());

		if (isUnspecializedSection)
		{
			auto origTarget = target;

			if (genericParamInst->mTypeConstraint != NULL)
			{
				target.mType = genericParamInst->mTypeConstraint;
			}
			else
				target.mType = mModule->mContext->mBfObjectType;

			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				target.mType = mModule->GetPrimitiveType(BfTypeCode_Var);
			}

			if (origTarget.mType->IsTypeGenericParam())
			{
				// Check for extern constraint in method generic params
				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (auto genericParam : mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams)
					{
						if (genericParam->mExternType == origTarget.mType)
						{
							if (genericParam->mTypeConstraint != NULL)
							{
								target.mType = genericParam->mTypeConstraint;
								break;
							}
						}
					}
				}
			}

			if (target.mType->IsWrappableType())
				target.mType = mModule->GetWrappedStructType(target.mType);
		}
		else
		{
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				//target.mType = mModule->ResolveGenericType(mResult.mType);
			}
			else if (genericParamInst->mTypeConstraint != NULL)
			{
				target = mModule->Cast(targetSrc, target, genericParamInst->mTypeConstraint);
				BF_ASSERT(target);
			}
			else
			{
				// This shouldn't occur - this would infer that we are accessing a member of Object or something...
				//target.mType = mModule->ResolveGenericType(mResult.mType);
			}
		}
	}

	if ((target.mType != NULL) && (IsVar(target.mType, (flags & BfLookupFieldFlag_BindOnly) != 0)))
		return BfTypedValue(mModule->GetDefaultValue(target.mType), target.mType, true);

	BfTypeInstance* startCheckType = mModule->mCurTypeInstance;
	mPropDef = NULL;
	mPropDefBypassVirtual = false;

	if (target)
	{
		if ((!target.mType->IsValueType()) && (target.IsAddr()))
			target = mModule->LoadValue(target);

		if (target.mType->IsPrimitiveType())
		{
			auto primType = (BfPrimitiveType*)target.mType;
			startCheckType = mModule->GetPrimitiveStructType(primType->mTypeDef->mTypeCode);
		}
		else
		{
			startCheckType = target.mType->ToTypeInstance();
			if ((startCheckType == NULL) && (target.mType->IsPointer()))
			{
				startCheckType = ((BfPointerType*)target.mType)->mElementType->ToTypeInstance();
				if ((startCheckType != NULL) && (!startCheckType->IsValueType()))
					return BfTypedValue();
			}
		}
		//BF_ASSERT(startCheckType != NULL);
	}
	else if (target.mType != NULL)
	{
		startCheckType = target.mType->ToTypeInstance();
	}

	if ((startCheckType != NULL) && (startCheckType->mBaseType == NULL))
	{
		if (startCheckType->mDefineState == BfTypeDefineState_ResolvingBaseType)
		{
			// Fixes cases where we have something like 'Base[Value]' as a base typeref
			return BfTypedValue();
		}
		mModule->PopulateType(startCheckType, BfPopulateType_BaseType);
	}

	String findName;
	int varSkipCount = 0;
	if (fieldName.StartsWith('@'))
	{
		findName = fieldName;
		while (findName.StartsWith('@'))
		{
			findName.Remove(0);
			varSkipCount++;
		}
	}
	else
		findName.Reference(fieldName);

	auto activeTypeDef = mModule->GetActiveTypeDef();
	for (int pass = 0; pass < 2; pass++)
	{
		auto curCheckType = startCheckType;
		bool isFailurePass = pass == 1;

		if (isFailurePass)
			flags = (BfLookupFieldFlags)(flags | BfLookupFieldFlag_IsFailurePass);

		bool isBaseLookup = false;
		while (curCheckType != NULL)
		{			
			if (((flags & BfLookupFieldFlag_CheckingOuter) != 0) && ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0))
			{
				// Don't fully populateType for CheckingOuter - it carries a risk of an inadvertent data cycle
				// Avoiding this could cause issues finding emitted statics/constants
			}
			else
			{
				bool isPopulatingType = false;

				auto checkTypeState = mModule->mContext->mCurTypeState;
				while (checkTypeState != NULL)
				{
					if (curCheckType == checkTypeState->mType)
					{
						isPopulatingType = true;
						if (checkTypeState->mResolveKind == BfTypeState::ResolveKind_Attributes)
						{
							// Don't allow lookups yet
							return BfTypedValue();
						}
					}
					checkTypeState = checkTypeState->mPrevState;
				}
				if ((!isPopulatingType) && (curCheckType->mDefineState < Beefy::BfTypeDefineState_Defined))
				{
					// We MAY have emitted fields so we need to do this
					mModule->mContext->mUnreifiedModule->PopulateType(curCheckType, Beefy::BfPopulateType_Interfaces_All);
				}
			}

			curCheckType->mTypeDef->PopulateMemberSets();
			BfFieldDef* nextField = NULL;
			BfMemberSetEntry* entry;
			if (curCheckType->mTypeDef->mFieldSet.TryGetWith(findName, &entry))
				nextField = (BfFieldDef*)entry->mMemberDef;

			if (nextField != NULL)
				varSkipCount -= nextField->mNamePrefixCount;

			while ((varSkipCount > 0) && (nextField != NULL))
			{
				nextField = nextField->mNextWithSameName;
				varSkipCount--;
			}

			BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
			if (target)
			{
				if ((flags & (BfLookupFieldFlag_IsImplicitThis | BfLookupFieldFlag_BaseLookup)) != 0)
				{
					// Not an "instance lookup"
				}
				else
				{
					protectionCheckFlags = (BfProtectionCheckFlags)(protectionCheckFlags | BfProtectionCheckFlag_InstanceLookup);
				}
			}

			BfFieldDef* matchedField = NULL;
			while (nextField != NULL)
			{
				if ((flags & BfLookupFieldFlag_BindOnly) != 0)
					break;

				auto field = nextField;
				nextField = nextField->mNextWithSameName;
				auto checkProtection = field->mProtection;
				if (checkProtection == BfProtection_Hidden)
				{
					// Allow acessing hidden fields
					checkProtection = BfProtection_Private;
				}

				if (((flags & BfLookupFieldFlag_IgnoreProtection) == 0) &&  (!isFailurePass) &&
					(!mModule->CheckProtection(protectionCheckFlags, curCheckType, field->mDeclaringType->mProject, checkProtection, startCheckType)))
				{
					continue;
				}

				bool isResolvingFields = curCheckType->mResolvingConstField || curCheckType->mResolvingVarField;

				if (curCheckType->mFieldInstances.IsEmpty())
				{
					mModule->PopulateType(curCheckType, BfPopulateType_Data);
				}

				if (field->mIdx >= (int)curCheckType->mFieldInstances.size())
				{
					if (mModule->mCompiler->EnsureCeUnpaused(curCheckType))
					{
						BF_DBG_FATAL("OOB in DoLookupField");
					}
					return mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
				}

				BF_ASSERT(field->mIdx < (int)curCheckType->mFieldInstances.size());
				auto fieldInstance = &curCheckType->mFieldInstances[field->mIdx];
				if (!fieldInstance->mFieldIncluded)
					continue;

				if (curCheckType->IsUnspecializedType())
				{
					// The check for non-unspecialized types is already handled in mFieldIncluded
					if (!curCheckType->IsTypeMemberIncluded(field->mDeclaringType, activeTypeDef, mModule))
						continue;
				}

				if (!mModule->IsInSpecializedSection())
				{
					if (!curCheckType->IsTypeMemberAccessible(field->mDeclaringType, activeTypeDef))
						continue;
				}

				if (matchedField != NULL)
				{
					auto error = mModule->Fail(StrFormat("Ambiguous reference to field '%s.%s'", mModule->TypeToString(curCheckType).c_str(), fieldName.c_str()), targetSrc);
					if (error != NULL)
					{
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See field declaration in project '%s'", matchedField->mDeclaringType->mProject->mName.c_str()), matchedField->mFieldDeclaration);
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See field declaration in project '%s'", field->mDeclaringType->mProject->mName.c_str()), field->mFieldDeclaration);
						break;
					}
				}

				matchedField = field;
			}

			if (matchedField != NULL)
				return LoadField(targetSrc, target, curCheckType, matchedField, flags);

			BfPropertyDef* nextProp = NULL;
			if (curCheckType->mTypeDef->mPropertySet.TryGetWith(fieldName, &entry))
				nextProp = (BfPropertyDef*)entry->mMemberDef;

			if (nextProp != NULL)
			{
				BfCheckedKind checkedKind = BfCheckedKind_NotSet;

				bool isInlined = false;
				if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
				{
					if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mInlineAttributeTypeDef))
					{
						isInlined = true;
						mModule->mAttributeState->mUsed = true;
					}
					if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mCheckedAttributeTypeDef))
					{
						checkedKind = BfCheckedKind_Checked;
						mModule->mAttributeState->mUsed = true;
					}
					if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mUncheckedAttributeTypeDef))
					{
						checkedKind = BfCheckedKind_Unchecked;
						mModule->mAttributeState->mUsed = true;
					}
				}

				BfPropertyDef* matchedProp = NULL;
				while (nextProp != NULL)
				{
					auto prop = nextProp;
					nextProp = nextProp->mNextWithSameName;

					if ((!isFailurePass) && ((mBfEvalExprFlags & BfEvalExprFlags_NameOf) == 0) && (!mModule->CheckProtection(protectionCheckFlags, curCheckType, prop->mDeclaringType->mProject, prop->mProtection, startCheckType)))
					{
						continue;
					}

					if (!prop->mMethods.IsEmpty())
					{
						BfMethodDef* checkMethod = prop->mMethods[0];
						// Properties with explicit interfaces or marked as overrides can only be called indirectly
						if ((checkMethod->mExplicitInterface != NULL) || (checkMethod->mIsOverride))
							continue;
					}

					if ((!target.IsStatic()) || (prop->mIsStatic) || ((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0))
					{
						if (!mModule->IsInSpecializedSection())
						{
							if ((!curCheckType->IsTypeMemberIncluded(prop->mDeclaringType, activeTypeDef, mModule)) ||
								(!curCheckType->IsTypeMemberAccessible(prop->mDeclaringType, mModule->GetVisibleProjectSet())))
								continue;
						}

						if (matchedProp != NULL)
						{
							if ((matchedProp->mDeclaringType->IsExtension()) && (!prop->mDeclaringType->IsExtension()))
							{
								// Prefer non-extension
								continue;
							}
							else
							{
								if (mModule->PreFail())
								{
									auto error = mModule->Fail(StrFormat("Ambiguous reference to property '%s.%s'", mModule->TypeToString(curCheckType).c_str(), fieldName.c_str()), targetSrc);
									if (error != NULL)
									{
										mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See property declaration in project '%s'", matchedProp->mDeclaringType->mProject->mName.c_str()), matchedProp->mFieldDeclaration);
										mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See property declaration in project '%s'", prop->mDeclaringType->mProject->mName.c_str()), prop->mFieldDeclaration);
										break;
									}
								}
							}
						}

						matchedProp = prop;
					}
				}

				if (matchedProp != NULL)
					return LoadProperty(targetSrc, target, curCheckType, matchedProp, flags, checkedKind, isInlined);
			}

			if (curCheckType->mTypeDef->mHasUsingFields)
				mModule->PopulateUsingFieldData(curCheckType);

			///
			{
				Array<SizedArray<BfUsingFieldData::MemberRef, 1>*> foundLists;

				auto _CheckUsingData = [&](BfUsingFieldData* usingData)
				{
					BfUsingFieldData::Entry* entry = NULL;
					if (!usingData->mEntries.TryGetValue(findName, &entry))
						return;

					for (int listIdx = 0; listIdx < entry->mLookups.mSize; listIdx++)
					{
						bool passesProtection = true;
						auto& entryList = entry->mLookups[listIdx];
						for (int entryIdx = 0; entryIdx < entryList.mSize; entryIdx++)
						{
							auto& entry = entryList[entryIdx];
							BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
							if (!mModule->CheckProtection(protectionCheckFlags, entry.mTypeInstance, entry.GetDeclaringType(mModule)->mProject,
								(entryIdx < entryList.mSize - 1) ? entry.GetUsingProtection() : entry.GetProtection(), curCheckType))
							{
								passesProtection = false;
								break;
							}
						}
						if (!passesProtection)
							continue;

						if (foundLists.mSize == 0)
						{
							foundLists.Add(&entryList);
						}
						else
						{
							if (entryList.mSize < foundLists[0]->mSize)
							{
								// New is shorter
								foundLists.Clear();
								foundLists.Add(&entryList);
							}
							else if (entryList.mSize > foundLists[0]->mSize)
							{
								// Ignore longer
							}
							else
							{
								foundLists.Add(&entryList);
							}
						}
					}
				};

				if ((curCheckType->mTypeInfoEx != NULL) && (curCheckType->mTypeInfoEx->mUsingFieldData != NULL))
					_CheckUsingData(curCheckType->mTypeInfoEx->mUsingFieldData);

				if (!foundLists.IsEmpty())
				{
					auto foundList = foundLists[0];

					if (foundLists.mSize > 1)
					{
						BfError* error = mModule->Fail("Ambiguous 'using' field reference", targetSrc);
						if (error != NULL)
						{
							for (auto checkList : foundLists)
							{
								String errorStr = "'";
								for (int entryIdx = 0; entryIdx < checkList->mSize; entryIdx++)
								{
									if (entryIdx == 0)
										errorStr += (*checkList)[entryIdx].GetFullName(mModule);
									else
									{
										errorStr += ".";
										errorStr += (*checkList)[entryIdx].GetName(mModule);
									}
								}
								errorStr += "' is a candidate";
								mModule->mCompiler->mPassInstance->MoreInfo(errorStr, (*checkList)[0].GetRefNode(mModule));
							}
						}
					}

					BfTypedValue curResult = target;
					for (int entryIdx = 0; entryIdx < foundList->mSize; entryIdx++)
					{
						if ((entryIdx == 0) && (foundList->back().IsStatic()))
							entryIdx = (int)foundList->mSize - 1;

						auto& entry = (*foundList)[entryIdx];
						if (mPropDef != NULL)
						{
							SetAndRestoreValue<BfTypedValue> prevResult(mResult, BfTypedValue());
							mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_Friend);
							curResult = GetResult();
							if (!curResult)
								return curResult;
						}

						auto useFlags = flags;
						if (entryIdx < foundList->mSize - 1)
							useFlags = (BfLookupFieldFlags)(flags | BfLookupFieldFlag_IsAnonymous);

						if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Field)
						{
							curResult = LoadField(targetSrc, curResult, entry.mTypeInstance, entry.mTypeInstance->mTypeDef->mFields[entry.mIdx], useFlags);
						}
						else if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Property)
						{
							curResult = LoadProperty(targetSrc, curResult, entry.mTypeInstance, entry.mTypeInstance->mTypeDef->mProperties[entry.mIdx], useFlags, BfCheckedKind_NotSet, false);
						}
						else if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Local)
						{
							auto localDef = mModule->mCurMethodState->mLocals[entry.mIdx];
							curResult = LoadLocal(localDef);
						}
						if ((!curResult) && (mPropDef == NULL))
							return curResult;

						if (entryIdx == foundList->mSize - 1)
						{
							auto autoComplete = GetAutoComplete();
							if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(targetSrc)))
								autoComplete->SetDefinitionLocation(entry.GetRefNode(mModule));

							if ((autoComplete != NULL) && (autoComplete->CheckFixit(targetSrc)))
								autoComplete->FixitAddFullyQualify(targetSrc, findName, *foundList);
						}
					}

					return curResult;
				}
			}

			isBaseLookup = true;
			curCheckType = curCheckType->mBaseType;
		}
	}

	auto outerTypeDef = mModule->GetOuterType(startCheckType);
	if (outerTypeDef != NULL)
	{
		// Check statics in outer type
		return LookupField(targetSrc, BfTypedValue(outerTypeDef), fieldName, BfLookupFieldFlag_CheckingOuter);
	}

	return BfTypedValue();
}

void BfExprEvaluator::ResolveArgValues(BfResolvedArgs& resolvedArgs, BfResolveArgsFlags flags)
{
	static int idx = 0;
	idx++;
	int curIdx = idx;

	if (resolvedArgs.mArguments == NULL)
		return;

	int argCount = (int)resolvedArgs.mArguments->size();
	if ((resolvedArgs.mCommas != NULL) && (resolvedArgs.mCommas->size() != 0) && (resolvedArgs.mCommas->size() >= resolvedArgs.mArguments->size()))
	{
		//mModule->FailAfter("Expression expected", resolvedArgs.mCommas->back());
		argCount++;
	}

	BfAutoComplete* autoComplete = GetAutoComplete();
	bool hadIgnoredFixits = false;
	if (autoComplete != NULL)
	{
		hadIgnoredFixits = autoComplete->mIgnoreFixits;
		if (flags & BfResolveArgsFlag_DeferFixits)
			autoComplete->mIgnoreFixits = true;
	}

	int deferredArgIdx = 0;
	SizedArray<BfExpression*, 8> deferredArgs;

	int argIdx = 0;

	while (true)
	{
		//printf("Args: %p %p %d\n", resolvedArgs.mArguments, resolvedArgs.mArguments->mVals, resolvedArgs.mArguments->mSize);

		BfExpression* argExpr = NULL;
		bool isDeferredArg = false;

		int curArgIdx = -1;

		if (deferredArgIdx < deferredArgs.size())
		{
			argExpr = deferredArgs[deferredArgIdx++];
			isDeferredArg = true;
		}
		else if (argIdx >= argCount)
		{
			break;
		}
		else
		{
			curArgIdx = argIdx++;
			if (curArgIdx < resolvedArgs.mArguments->size())
				argExpr = (*resolvedArgs.mArguments)[curArgIdx];
		}

		if (argExpr == NULL)
		{
			if (curArgIdx == 0)
			{
				if (resolvedArgs.mOpenToken != NULL)
					mModule->FailAfter("Expression expected", resolvedArgs.mOpenToken);
			}
			else if (resolvedArgs.mCommas != NULL)
				mModule->FailAfter("Expression expected", (*resolvedArgs.mCommas)[curArgIdx - 1]);
		}

		if (auto typedValueExpr = BfNodeDynCast<BfTypedValueExpression>(argExpr))
		{
			BfResolvedArg resolvedArg;
			resolvedArg.mTypedValue = typedValueExpr->mTypedValue;
			resolvedArg.mExpression = typedValueExpr->mRefNode;
			resolvedArgs.mResolvedArgs.push_back(resolvedArg);
			continue;
		}

		BfResolvedArg resolvedArg;
		if (isDeferredArg)
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_StringInterpolateArg);

		BfExprEvaluator exprEvaluator(mModule);
		exprEvaluator.mResolveGenericParam = (flags & BfResolveArgsFlag_AllowUnresolvedTypes) == 0;
		exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_AllowOutExpr |
			(mBfEvalExprFlags & (BfEvalExprFlags_Comptime)));

		bool handled = false;
		bool evaluated = false;

		if (auto namedExpression = BfNodeDynCastExact<BfNamedExpression>(argExpr))
		{
			resolvedArg.mNameNode = namedExpression->mNameNode;
			argExpr = namedExpression->mExpression;
		}

		if (auto interpolateExpr = BfNodeDynCastExact<BfStringInterpolationExpression>(argExpr))
		{
			if ((interpolateExpr->mAllocNode == NULL) || ((flags & BfResolveArgsFlag_InsideStringInterpolationAlloc) != 0))
			{
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_StringInterpolateFormat);
				for (auto innerExpr : interpolateExpr->mExpressions)
					deferredArgs.Add(innerExpr);
			}
		}

		if (auto unaryOpExpr = BfNodeDynCastExact<BfUnaryOperatorExpression>(argExpr))
		{
			if ((unaryOpExpr->mOp == BfUnaryOp_Cascade) && ((flags & BfResolveArgsFlag_FromIndexer) == 0))
			{
				if ((mBfEvalExprFlags & BfEvalExprFlags_InCascade) != 0)
					mModule->Fail("Cascade already specified on call target", unaryOpExpr->mOpToken);

				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_Cascade);
				argExpr = unaryOpExpr->mExpression;
			}
		}

		bool deferParamEval = false;
		if ((flags & BfResolveArgsFlag_DeferParamEval) != 0)
		{
			if (argExpr != NULL)
			{
				BfDeferEvalChecker deferEvalChecker;
				deferEvalChecker.mDeferDelegateBind = false;
				deferEvalChecker.Check(argExpr);
				deferParamEval = deferEvalChecker.mNeedsDeferEval;
			}
		}

		if (deferParamEval)
		{
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_DeferredEval);
			handled = true;
		}
		else if (auto delegateBindExpression = BfNodeDynCast<BfDelegateBindExpression>(argExpr))
		{
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_DelegateBindAttempt);
			handled = true;
		}
		else if (auto lambdaBindExpression = BfNodeDynCast<BfLambdaBindExpression>(argExpr))
		{
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_LambdaBindAttempt);
			handled = true;
		}
		else if (auto memberRef = BfNodeDynCast<BfMemberReferenceExpression>(argExpr))
		{
			if (memberRef->mTarget == NULL)
			{
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_UnqualifiedDotAttempt);
				handled = true;
			}
		}
		else if (auto invokeExpr = BfNodeDynCast<BfInvocationExpression>(argExpr))
		{
			if (auto memberRef = BfNodeDynCast<BfMemberReferenceExpression>(invokeExpr->mTarget))
			{
				if (memberRef->mTarget == NULL)
				{
					resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_UnqualifiedDotAttempt);
					handled = true;
				}
			}
		}
		else if (auto defaultExpr = BfNodeDynCast<BfDefaultExpression>(argExpr))
		{
			if (defaultExpr->mTypeRef == NULL)
			{
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_UntypedDefault);
				handled = true;
			}
		}
		else if (auto varDeclExpr = BfNodeDynCast<BfVariableDeclaration>(argExpr))
		{
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_VariableDeclaration);
			handled = true;
		}
		else if (auto unaryExpr = BfNodeDynCast<BfUnaryOperatorExpression>(argExpr))
		{
			if (unaryExpr->mOp == BfUnaryOp_Params)
			{
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_ParamsExpr);
			}
		}
		else if (auto uninitExpr = BfNodeDynCast<BfUninitializedExpression>(argExpr))
		{
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_UninitializedExpr);
			handled = true;
		}

		/*else if (auto castExpr = BfNodeDynCast<BfCastExpression>(argExpr))
		{
			if (auto namedTypeRef = BfNodeDynCastExact<BfNamedTypeReference>(castExpr->mTypeRef))
			{
				if (namedTypeRef->ToString() == "ExpectedType")
				{
					resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_ExpectedTypeCast);
					handled = true;
				}
			}
		}*/

		if ((argExpr != NULL) && (!handled))
		{
			bool deferParamValues = (flags & BfResolveArgsFlag_DeferParamValues) != 0;
			SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, mModule->mBfIRBuilder->mIgnoreWrites || deferParamValues);
			auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
			if (deferParamValues)
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_DeferredValue);

			if (!evaluated)
			{
				exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowParamsExpr);

				if ((resolvedArg.mArgFlags & BfArgFlag_StringInterpolateFormat) != 0)
					exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_StringInterpolateFormat);

				int lastLocalVarIdx = mModule->mCurMethodState->mLocals.mSize;
				exprEvaluator.Evaluate(argExpr, false, false, true);
				if ((deferParamValues) && (mModule->mCurMethodState->mLocals.mSize > lastLocalVarIdx))
				{
					// Remove any ignored locals
					mModule->RestoreScoreState_LocalVariables(lastLocalVarIdx);
				}
			}

			if ((mModule->mCurMethodState != NULL) && (exprEvaluator.mResultLocalVar != NULL) && (exprEvaluator.mResultLocalVarRefNode != NULL))
			{
				auto localVar = exprEvaluator.mResultLocalVar;
				int fieldIdx = mResultLocalVarField - 1;

				auto methodState = mModule->mCurMethodState->GetMethodStateForLocal(localVar);
				if (localVar->mCompositeCount >= 0)
				{
					if ((resolvedArg.mArgFlags & BfArgFlag_ParamsExpr) == 0)
						mModule->Warn(0, "'params' token expected", argExpr);

					for (int compositeIdx = 0; compositeIdx < localVar->mCompositeCount; compositeIdx++)
					{
						BfResolvedArg compositeResolvedArg;
						auto compositeLocalVar = methodState->mLocals[localVar->mLocalVarIdx + compositeIdx + 1];
						auto argValue = exprEvaluator.LoadLocal(compositeLocalVar, true);
						if (argValue)
						{
							if (!argValue.mType->IsStruct())
								argValue = mModule->LoadValue(argValue, NULL, exprEvaluator.mIsVolatileReference);
						}
						resolvedArg.mTypedValue = argValue;
						resolvedArg.mExpression = argExpr;
						resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_FromParamComposite);
						resolvedArgs.mResolvedArgs.push_back(resolvedArg);
					}

					continue;
				}
			}

			exprEvaluator.CheckResultForReading(exprEvaluator.mResult);
			auto argValue = exprEvaluator.mResult;
			if (argValue)
			{
				mModule->mBfIRBuilder->PopulateType(argValue.mType);
				resolvedArg.mResolvedType = argValue.mType;
				if (resolvedArg.mResolvedType->IsRef())
					argValue.mKind = BfTypedValueKind_Value;
				if (exprEvaluator.mIsVolatileReference)
					resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_Volatile);
			}
			resolvedArg.mUncastedTypedValue = argValue;
			resolvedArg.mTypedValue = argValue;

			if (deferParamValues)
				mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
		}
		resolvedArg.mExpression = argExpr;
		resolvedArgs.mResolvedArgs.push_back(resolvedArg);
	}

	if (autoComplete != NULL)
		autoComplete->mIgnoreFixits = hadIgnoredFixits;
}

void BfExprEvaluator::PerformCallChecks(BfMethodInstance* methodInstance, BfAstNode* targetSrc)
{
	BfCustomAttributes* customAttributes = methodInstance->GetCustomAttributes();
	if (customAttributes != NULL)
		mModule->CheckErrorAttributes(methodInstance->GetOwner(), methodInstance, NULL, customAttributes, targetSrc);
}

void BfExprEvaluator::CheckSkipCall(BfAstNode* targetSrc, SizedArrayImpl<BfResolvedArg>& argValues)
{
	for (auto& argValue : argValues)
	{
		if ((!argValue.IsDeferredValue()) && (argValue.mExpression != NULL))
		{
			mModule->Fail("Illegal SkipCall invocation", targetSrc);
			return;
		}
	}
}

BfTypedValue BfExprEvaluator::CreateCall(BfAstNode* targetSrc, BfMethodInstance* methodInstance, BfIRValue func, bool bypassVirtual, SizedArrayImpl<BfIRValue>& irArgs, BfTypedValue* sret, BfCreateCallFlags callFlags, BfType* origTargetType)
{
// 	static int sCallIdx = 0;
// 	if (!mModule->mCompiler->mIsResolveOnly)
// 		sCallIdx++;
// 	int callIdx = sCallIdx;
// 	if (callIdx == 0x000000E8)
// 	{
// 		NOP;
// 	}

	auto methodDef = methodInstance->mMethodDef;
	BfIRValue funcCallInst = func;

	auto importCallKind = methodInstance->GetImportCallKind();
	if ((funcCallInst) && (importCallKind != BfImportCallKind_None))
	{
		if ((funcCallInst.IsFake()) && (!mModule->mBfIRBuilder->mIgnoreWrites))
		{
			mModule->mFuncReferences.TryGetValue(methodInstance, &funcCallInst);
		}

		if ((importCallKind == BfImportCallKind_GlobalVar) &&
			(methodInstance->mHotMethod == NULL) &&
			(mModule->mCompiler->IsHotCompile()))
		{
			// This may actually be a BfImportCallKind_GlobalVar_Hot, so check it...
			mModule->CheckHotMethod(methodInstance, "");
			importCallKind = methodInstance->GetImportCallKind();
		}

		if (importCallKind == BfImportCallKind_GlobalVar_Hot)
		{
			//TODO: Check against NULL for calling BfLoadSharedLibraries
			auto checkVal = mModule->mBfIRBuilder->CreateLoad(funcCallInst);

			BfIRBlock nullBlock = mModule->mBfIRBuilder->CreateBlock("importNull");
			BfIRBlock doneBlock = mModule->mBfIRBuilder->CreateBlock("importLoad");

			auto condVal = mModule->mBfIRBuilder->CreateIsNull(checkVal);
			mModule->mBfIRBuilder->CreateCondBr(condVal, nullBlock, doneBlock);

			mModule->mBfIRBuilder->AddBlock(nullBlock);
			mModule->mBfIRBuilder->SetInsertPoint(nullBlock);
			auto loadSharedLibsFunc = mModule->GetBuiltInFunc(BfBuiltInFuncType_LoadSharedLibraries);
			mModule->mBfIRBuilder->CreateCall(loadSharedLibsFunc, SizedArray<BfIRValue, 0>());
			mModule->mBfIRBuilder->CreateBr(doneBlock);

			mModule->mBfIRBuilder->AddBlock(doneBlock);
			mModule->mBfIRBuilder->SetInsertPoint(doneBlock);
			funcCallInst = mModule->mBfIRBuilder->CreateLoad(funcCallInst);
		}
		else
		{
			funcCallInst = mModule->mBfIRBuilder->CreateLoad(funcCallInst);
		}
	}

	if ((methodInstance->GetOwner()->IsInstanceOf(mModule->mCompiler->mDeferredCallTypeDef)) &&
		(methodInstance->mMethodDef->mName == "Cancel"))
	{
		if (mModule->mCurMethodState != NULL)
			mModule->mCurMethodState->mCancelledDeferredCall = true;
	}

	if (methodDef->mIsNoReturn)
	{
		mModule->mCurMethodState->SetHadReturn(true);
		mModule->mCurMethodState->mLeftBlockUncond = true;
		mModule->MarkScopeLeft(&mModule->mCurMethodState->mHeadScope, true);
	}

	if (mModule->mCurTypeInstance != NULL)
	{
		bool isVirtual = (methodInstance->mVirtualTableIdx != -1) && (!bypassVirtual);
		mModule->AddDependency(methodInstance->mMethodInstanceGroup->mOwner, mModule->mCurTypeInstance, isVirtual ? BfDependencyMap::DependencyFlag_VirtualCall : BfDependencyMap::DependencyFlag_Calls);
		mModule->AddCallDependency(methodInstance, bypassVirtual);
	}

	if (methodInstance->GetOwner()->IsUnspecializedType())
	{
		BF_ASSERT((!methodInstance->mIRFunction) || (methodInstance == mModule->mCurMethodInstance) || (methodInstance->mMethodDef->mIsLocalMethod));
	}

	if (mFunctionBindResult != NULL)
	{
		BF_ASSERT(mFunctionBindResult->mMethodInstance == NULL);
		mFunctionBindResult->mMethodInstance = methodInstance;

		for (auto arg : irArgs)
			mFunctionBindResult->mIRArgs.push_back(arg);
	}

	BfType* origReturnType = methodInstance->mReturnType;
	/*if (origReturnType->IsSelf())
	{
		origReturnType = methodInstance->GetOwner();
		BF_ASSERT(origReturnType->IsInterface());
	}*/
	BfType* returnType = origReturnType;
	BfTypedValue sretVal;

	auto _GetDefaultReturnValue = [&]()
	{
		if (methodInstance->mVirtualTableIdx == -1)
		{
			if (methodInstance->GetOwner()->IsInterface())
			{
				// We're attempting to directly invoke a non-virtual interface method, if we're return an interface then
				//  it is a concrete interface
				if (returnType->IsInterface())
					returnType = mModule->CreateConcreteInterfaceType(returnType->ToTypeInstance());
			}
		}

		if ((returnType->IsVar()) && (mExpectingType != NULL))
			returnType = mExpectingType;
		if (returnType->IsRef())
		{
			auto result = mModule->GetDefaultTypedValue(returnType->GetUnderlyingType(), true, BfDefaultValueKind_Addr);
			if (methodDef->mIsReadOnly)
				result.mKind = BfTypedValueKind_ReadOnlyAddr;
			return result;
		}
		else
		{
			auto val = mModule->GetDefaultTypedValue(returnType, true, (GetStructRetIdx(methodInstance) != -1) ? BfDefaultValueKind_Addr : BfDefaultValueKind_Value);
			if (val.mKind == BfTypedValueKind_Addr)
				val.mKind = BfTypedValueKind_RestrictedTempAddr;
			return val;
		}
	};

	mModule->PopulateType(origReturnType, BfPopulateType_Data);
	if (GetStructRetIdx(methodInstance) != -1)
	{
		// We need to ensure that mReceivingValue has the correct type, otherwise it's possible that a conversion operator needs to be applied
		//  This happens for returning Result<T>'s with a 'T' value
		if ((sret == NULL) && (mReceivingValue != NULL) && (mReceivingValue->mType == returnType))
		{
			sretVal = *mReceivingValue;
			sret = &sretVal;

			auto ptrType = mModule->CreatePointerType(returnType);
			if (returnType != sret->mType)
			{
				sret->mValue = mModule->mBfIRBuilder->CreateBitCast(sret->mValue, mModule->mBfIRBuilder->MapType(ptrType));
				sret->mType = returnType;
			}

			mReceivingValue = NULL;
		}

		if (sret == NULL)
		{
			sretVal = BfTypedValue(mModule->CreateAlloca(returnType), returnType, BfTypedValueKind_RestrictedTempAddr);
			sret = &sretVal;
		}
	}
	else
	{
		BF_ASSERT(sret == NULL);
	}

	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
	{
		if ((methodInstance->mComptimeFlags & BfComptimeFlag_ConstEval) != 0)
		{
			// We need to perform call such as Compiler.Mixin and String.ConstF
		}
		else
			return _GetDefaultReturnValue();
	}

	BF_ASSERT(!methodInstance->mDisallowCalling);

	if ((mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		bool wantQuickEval = true;

		if (IsComptime())
		{
			auto autoComplete = mModule->mCompiler->mResolvePassData->mAutoComplete;
			wantQuickEval =
				((autoComplete->mResolveType != BfResolveType_Autocomplete) &&
				(autoComplete->mResolveType != BfResolveType_Autocomplete_HighPri) &&
				(autoComplete->mResolveType != BfResolveType_GetResultString));

			for (auto& entry : mModule->mCompiler->mResolvePassData->mEmitEmbedEntries)
			{
				if (entry.mValue.mCursorIdx >= 0)
				{
					// Needed for Go To Definition in Compiler.Mixin
					wantQuickEval = false;
				}
			}
		}

		if (wantQuickEval)
		{
			// In an autocomplete pass we may have stale method references that need to be resolved
			//  in the full classify pass, and in the full classify pass while just refreshing internals, we
			//  may have NULL funcs temporarily.  We simply skip generating the method call here.
			if ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0)
			{
				if (methodInstance->mReturnType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef))
				{
					if ((mExpectingType != NULL) && (mExpectingType->IsSizedArray()))
					{
						return BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->MapType(mExpectingType)), mExpectingType);
					}
				}
				auto returnType = methodInstance->mReturnType;
				if ((returnType->IsVar()) && (mExpectingType != NULL))
					returnType = mExpectingType;
				if (methodInstance->mReturnType->IsValuelessType())
					return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), returnType);
				return BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->MapType(returnType)), returnType);
			}

			BfTypedValue result;
			if (sret != NULL)
				result = *sret;
			else
				result = _GetDefaultReturnValue();
			return result;
		}
	}

	bool forceBind = false;

	if (mModule->mCompiler->mCeMachine != NULL)
	{
		bool doConstReturn = false;

		if ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0)
		{
			if (mFunctionBindResult != NULL)
			{
				forceBind = true;
			}
			else if ((mBfEvalExprFlags & BfEvalExprFlags_InCascade) != 0)
			{
				mModule->Fail("Const evaluation not allowed with cascade operator", targetSrc);
			}
			else if (((methodInstance->mComptimeFlags & BfComptimeFlag_OnlyFromComptime) != 0) && (!mModule->mIsComptimeModule))
			{
				// This either generated an error already or this is just the non-const type check pass for a comptime-only method
				doConstReturn = true;
			}
			else if ((mBfEvalExprFlags & BfEvalExprFlags_DisallowComptime) != 0)
			{
				doConstReturn = true;
			}
			else if (((callFlags & BfCreateCallFlags_GenericParamThis) != 0) && (methodInstance->GetOwner()->IsInterface()))
			{
				mModule->Warn(0, "Concrete method may fail to comptime during specialization", targetSrc);
				doConstReturn = true;
			}
			else if (methodDef->mIsVirtual)
			{
				// This could only really be the case for a Type, since no other 'this' could qualify as const
			}
			else
			{
				CeEvalFlags evalFlags = CeEvalFlags_None;
				if ((mBfEvalExprFlags & BfEvalExprFlags_NoCeRebuildFlags) != 0)
					evalFlags = (CeEvalFlags)(evalFlags | CeEvalFlags_NoRebuild);

				if ((mModule->mIsComptimeModule) && (mModule->mCompiler->mCeMachine->mDebugger != NULL) && (mModule->mCompiler->mCeMachine->mDebugger->mCurDbgState != NULL))
				{
					auto ceDbgState = mModule->mCompiler->mCeMachine->mDebugger->mCurDbgState;
					if ((ceDbgState->mDbgExpressionFlags & DwEvalExpressionFlag_AllowCalls) != 0)
					{
						ceDbgState->mHadSideEffects = true;

						//SetAndRestoreValue<CeDebugger*> prevDebugger(mModule->mCompiler->mCeMachine->mDebugger, NULL);

						evalFlags = (CeEvalFlags)(evalFlags | CeEvalFlags_DbgCall);
						auto result = ceDbgState->mCeContext->Call(targetSrc, mModule, methodInstance, irArgs, evalFlags, mExpectingType);
						if (result)
							return result;
					}
					else
					{
						ceDbgState->mBlockedSideEffects = true;
					}
				}
				else
				{
					CeCallSource ceCallSource(targetSrc);
					ceCallSource.mOrigCalleeType = origTargetType;
					auto constRet = mModule->mCompiler->mCeMachine->Call(ceCallSource, mModule, methodInstance, irArgs, evalFlags, mExpectingType);
					if (constRet)
					{
						auto constant = mModule->mBfIRBuilder->GetConstant(constRet.mValue);
						BF_ASSERT(!constRet.mType->IsVar());
						return constRet;
					}

					if (mModule->mCompiler->mFastFinish)
					{
						if ((mModule->mCurMethodInstance == NULL) || (!mModule->mCurMethodInstance->mIsAutocompleteMethod))
						{
							// We didn't properly resolve this so queue for a rebuild later
							mModule->DeferRebuildType(mModule->mCurTypeInstance);
						}
					}
					doConstReturn = true;
				}
			}
		}
		else if (mModule->mIsComptimeModule)
 		{
			//TODO: This meant that unspecialized types were not even allowed to have Init methods that called into themselves
// 			if (methodInstance->mIsUnspecialized)
// 			{
// 				doConstReturn = true;
// 			}
// 			else
			{
				mModule->mCompiler->mCeMachine->QueueMethod(methodInstance, func);
			}
 		}

		if (doConstReturn)
		{
			if ((returnType->IsVar()) && (mExpectingType != NULL))
				returnType = mExpectingType;
			if (returnType->IsRef())
			{
				return _GetDefaultReturnValue();
			}
			else
			{
				if (returnType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef))
				{
					if ((mExpectingType != NULL) && (mExpectingType->IsUndefSizedArray()))
					{
						if (returnType->GetUnderlyingType() == mExpectingType->GetUnderlyingType())
							return mModule->GetDefaultTypedValue(mExpectingType, true, BfDefaultValueKind_Undef);
					}
				}

				if (methodInstance->mReturnType->IsValuelessType())
					return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), returnType);
				return mModule->GetDefaultTypedValue(returnType, true, BfDefaultValueKind_Undef);
			}

			return _GetDefaultReturnValue();
		}
	}

	if (!forceBind)
	{
		if (((!func) && (methodInstance->mIsUnspecialized)) || (mModule->mBfIRBuilder->mIgnoreWrites))
		{
			// We don't actually submit method calls for unspecialized methods
			//  - this includes all methods in unspecialized types
			return _GetDefaultReturnValue();
		}
	}

	if (methodInstance->mVirtualTableIdx != -1)
	{
		if ((!bypassVirtual) && (mDeferCallRef == NULL))
		{
			if ((methodDef->mIsOverride) && (mModule->mCurMethodInstance->mIsReified))
			{
				// Ensure that declaring method gets referenced
				auto typeInstance = methodInstance->GetOwner();
				auto& vEntry = typeInstance->mVirtualMethodTable[methodInstance->mVirtualTableIdx];
				BfMethodInstance* declaringMethodInstance = vEntry.mDeclaringMethod;
				if ((declaringMethodInstance->mMethodInstanceGroup->mOnDemandKind < BfMethodOnDemandKind_InWorkList) || (!declaringMethodInstance->mIsReified))
					mModule->GetMethodInstance(declaringMethodInstance);
			}

			auto funcType = mModule->mBfIRBuilder->MapMethod(methodInstance);
			auto funcPtrType1 = mModule->mBfIRBuilder->GetPointerTo(funcType);
			auto funcPtrType2 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType1);
			auto funcPtrType3 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType2);
			auto funcPtrType4 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType3);

			if (methodInstance->mMethodInstanceGroup->mOwner->IsInterface())
			{
				if (mModule->mIsComptimeModule)
				{
					funcCallInst = mModule->mBfIRBuilder->Comptime_GetInterfaceFunc(irArgs[0], methodInstance->mMethodInstanceGroup->mOwner->mTypeId, methodInstance->mMethodDef->mIdx, funcPtrType1);
				}
				else
				{
					// IFace dispatch
					auto ifaceTypeInst = methodInstance->mMethodInstanceGroup->mOwner;
					BfIRValue slotOfs = mModule->GetInterfaceSlotNum(methodInstance->mMethodInstanceGroup->mOwner);

					auto vDataPtrPtr = mModule->mBfIRBuilder->CreateBitCast(irArgs[0], funcPtrType4);
					auto vDataPtr = mModule->FixClassVData(mModule->mBfIRBuilder->CreateLoad(vDataPtrPtr/*, "vtable"*/));

					auto ifacePtrPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(vDataPtr, slotOfs/*, "iface"*/);
					auto ifacePtr = mModule->mBfIRBuilder->CreateLoad(ifacePtrPtr);

					auto funcPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(ifacePtr, methodInstance->mVirtualTableIdx/*, "vfn"*/);
					funcCallInst = mModule->mBfIRBuilder->CreateLoad(funcPtr);
				}
			}
			else if (mModule->mIsComptimeModule)
			{
				funcCallInst = mModule->mBfIRBuilder->Comptime_GetVirtualFunc(irArgs[0], methodInstance->mVirtualTableIdx, funcPtrType1);
			}
			else
			{
				mModule->HadSlotCountDependency();

				// Virtual dispatch
// 				int vDataIdx = 0;
// 				vDataIdx += 1 + methodInstance->GetOwner()->GetDynCastVDataCount() + mModule->mCompiler->mMaxInterfaceSlots;

				BfIRValue vDataPtr;
				BfIRValue vDataIdx;

				if ((mModule->mCompiler->mOptions.mHasVDataExtender) && (mModule->mCompiler->IsHotCompile()))
				{
					auto typeInst = methodInstance->mMethodInstanceGroup->mOwner;

					int extMethodIdx = (methodInstance->mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) - typeInst->GetOrigSelfVTableSize();
					if (extMethodIdx >= 0)
					{
						BF_ASSERT(mModule->mCompiler->IsHotCompile());

						// We have grown outside our original virtual table, Load the new vdataPtr from the mHasVDataExtender
						//  vDataPtr = obj.vtable.extension.entry[curVersion]
						BfIRValue vDataPtrPtr = mModule->mBfIRBuilder->CreateBitCast(irArgs[0], funcPtrType4);
						vDataPtr = mModule->FixClassVData(mModule->mBfIRBuilder->CreateLoad(vDataPtrPtr));
						// The offset of the vExt is one past the base vtable.  When a base class extends its virtual table, an entry
						//  for that new method is inserted in mVirtualMethodTable (thus increasing the index relative to GetBaseVTableSize()),
						//  but the actual written vtable position doesn't change, hence offsetting from GetOrigBaseVTableSize()
#ifdef _DEBUG
						int vExtIdx = typeInst->GetImplBaseVTableSize();
						BF_ASSERT(typeInst->mVirtualMethodTable[vExtIdx].mDeclaringMethod.mMethodNum == -1); // A type entry with a -1 mMethodNum means it's a vtable ext slot
#endif
						int vExtOfs = typeInst->GetOrigImplBaseVTableSize();

						vDataIdx = mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 1 + mModule->mCompiler->GetDynCastVDataCount() + mModule->mCompiler->mMaxInterfaceSlots);
						vDataIdx = mModule->mBfIRBuilder->CreateAdd(vDataIdx, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, vExtOfs));
						BfIRValue extendPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(vDataPtr, vDataIdx);
						vDataPtr = mModule->mBfIRBuilder->CreateLoad(extendPtr);
						vDataIdx = mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, extMethodIdx);
					}
					else
					{
						// Map this new virtual index back to the original index
						//vDataIdx += (methodInstance->mVirtualTableIdx - typeInst->GetBaseVTableSize()) + typeInst->GetOrigBaseVTableSize();

						//vDataIdx = mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 1 + methodInstance->GetOwner()->GetDynCastVDataCount() + mModule->mCompiler->mMaxInterfaceSlots);

						vDataIdx = mModule->mBfIRBuilder->GetConfigConst(BfIRConfigConst_VirtualMethodOfs, BfTypeCode_Int32);
						vDataIdx = mModule->mBfIRBuilder->CreateAdd(vDataIdx, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32,
							(methodInstance->mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) + typeInst->GetOrigImplBaseVTableSize()));
					}
				}
				else
				{
					//vDataIdx += methodInstance->mVirtualTableIdx;

					//vDataIdx = mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 1 + methodInstance->GetOwner()->GetDynCastVDataCount() + mModule->mCompiler->mMaxInterfaceSlots);
					vDataIdx = mModule->mBfIRBuilder->GetConfigConst(BfIRConfigConst_VirtualMethodOfs, BfTypeCode_Int32);
					vDataIdx = mModule->mBfIRBuilder->CreateAdd(vDataIdx, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, methodInstance->mVirtualTableIdx));
				}

				if (!vDataPtr)
				{
					BfIRValue vDataPtrPtr = mModule->mBfIRBuilder->CreateBitCast(irArgs[0], funcPtrType3);
					vDataPtr = mModule->FixClassVData(mModule->mBfIRBuilder->CreateLoad(vDataPtrPtr/*, "vtable"*/));
				}

				auto funcPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(vDataPtr, vDataIdx/*, "vfn"*/);
				funcCallInst = mModule->mBfIRBuilder->CreateLoad(funcPtr);
			}
		}
	}
	else // non-virtual
	{
		if (methodInstance->GetOwner()->IsInterface())
		{
			// We're attempting to directly invoke a non-virtual interface method, this will happen during the unspecialized pass
			//  OR if we had an error and didn't find an implementing member in the actual target
			if ((!mModule->mCurMethodInstance->mIsUnspecialized) && (!mModule->mCurTypeInstance->IsInterface()))
				mModule->AssertErrorState();

			if (returnType->IsInterface())
				returnType = mModule->CreateConcreteInterfaceType(returnType->ToTypeInstance());

			return _GetDefaultReturnValue();
		}
	}

	if (mFunctionBindResult != NULL)
	{
		mFunctionBindResult->mFunc = funcCallInst;
		if (irArgs.size() != 0)
		{
			auto targetType = methodInstance->mMethodInstanceGroup->mOwner;
			if ((targetType->IsValueType()) && (targetType->IsSplattable()) && (!methodDef->HasNoThisSplat()) && (!IsComptime()))
				mFunctionBindResult->mTarget = BfTypedValue(irArgs[0], targetType, BfTypedValueKind_SplatHead);
			else
				mFunctionBindResult->mTarget = BfTypedValue(irArgs[0], targetType, targetType->IsComposite() ? BfTypedValueKind_Addr : BfTypedValueKind_Value);
		}
		else
		{
			mFunctionBindResult->mTarget = BfTypedValue();
		}

		return BfTypedValue();
	}

	if (methodInstance->mReturnType == NULL)
	{
		mModule->AssertErrorState();
		return BfTypedValue();
	}

	if (((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized)) && (mModule->mBfIRBuilder->mIgnoreWrites))
	{
		// Don't actually do the call - our target may be a generic param
		return _GetDefaultReturnValue();
	}

	if (mDeferCallRef != NULL)
	{
		mModule->AddDeferredCall(BfModuleMethodInstance(methodInstance, func), irArgs, mDeferScopeAlloc, mDeferCallRef, bypassVirtual);
		return mModule->GetFakeTypedValue(returnType);
	}

	if (!funcCallInst)
	{
		if ((mModule->HasCompiledOutput()) || (mModule->mCompiler->mOptions.mExtraResolveChecks))
		{
			// This can happen either from an error, or from the resolver while doing Internals_Changed processing
			mModule->AssertErrorState();
		}
		return _GetDefaultReturnValue();
	}

	bool hasResult = !methodInstance->mReturnType->IsValuelessType();

	BfIRValue firstArg;
	if (irArgs.size() != 0)
		firstArg = irArgs[0];

	auto methodInstOwner = methodInstance->GetOwner();
	auto expectCallingConvention = mModule->GetIRCallingConvention(methodInstance);
	if ((methodInstOwner->IsFunction()) && (methodInstance->GetParamCount() > 0) && (methodInstance->GetParamName(0) == "this"))
	{
		auto paramType = methodInstance->GetParamType(0);
		if (!paramType->IsValueType())
			expectCallingConvention = BfIRCallingConv_ThisCall;
	}

	if ((methodInstance->mAlwaysInline) && (mModule->mCompiler->mOptions.mEmitLineInfo))
	{
		// Emit a NOP so we always have a "step over" point
		mModule->EmitEnsureInstructionAt();
	}

	if (returnType->IsComposite())
		mModule->mBfIRBuilder->PopulateType(returnType);

	methodInstance->mMethodInstanceGroup->mHasEmittedReference = true;

	BfIRValue callInst;
	int callIRArgCount = (int)irArgs.size();
	if (sret != NULL)
	{
		SizedArray<BfIRValue, 8> sretIRArgs;
		int sretIdx = GetStructRetIdx(methodInstance);
		int inIdx = 0;
		for (int outIdx = 0; outIdx < irArgs.size() + 1; outIdx++)
		{
			if (outIdx == sretIdx)
			{
				sretIRArgs.Add(sret->mValue);
				continue;
			}
			sretIRArgs.Add(irArgs[inIdx++]);
		}
		callInst = mModule->mBfIRBuilder->CreateCall(funcCallInst, sretIRArgs);
		callIRArgCount++;
	}
	else
	{
		callInst = mModule->mBfIRBuilder->CreateCall(funcCallInst, irArgs);

		if ((hasResult) && (!methodDef->mName.IsEmpty()) && (!methodInstance->mIsIntrinsic))
			mModule->mBfIRBuilder->SetName(callInst, methodDef->mName);
	}
	if ((expectCallingConvention != BfIRCallingConv_CDecl) && (!methodInstance->mIsIntrinsic))
		mModule->mBfIRBuilder->SetCallCallingConv(callInst, expectCallingConvention);

	if ((methodDef->mIsNoReturn) && (!methodInstance->mIsIntrinsic))
		mModule->mBfIRBuilder->Call_AddAttribute(callInst, -1, BfIRAttribute_NoReturn);

	bool hadAttrs = false;
	int paramIdx = 0;
	bool doingThis = methodInstance->HasThis();
	int argIdx = 0;

	if (methodDef->mHasExplicitThis)
		paramIdx++;

	int paramCount = methodInstance->GetParamCount();

	for ( ; argIdx < callIRArgCount ; )
	{
		if (methodInstance->mIsIntrinsic)
			break;

		if (argIdx == GetStructRetIdx(methodInstance))
		{
			mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_StructRet);
			argIdx++;
			continue;
		}

		auto _HandleParamType = [&] (BfType* paramType)
		{
			if (paramType->IsStruct())
			{
				if ((!doingThis) || (!methodDef->mIsMutating && methodInstance->AllowsSplatting(paramIdx)))
				{
					BfTypeCode loweredTypeCode = BfTypeCode_None;
					BfTypeCode loweredTypeCode2 = BfTypeCode_None;
					if (paramType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
					{
						argIdx++;
						if (loweredTypeCode2 != BfTypeCode_None)
							argIdx++;
						return; // Lowering never requires attributes
					}
				}
			}

			int addDeref = -1;
			if (paramType->IsRef())
			{
				auto refType = (BfRefType*)paramType;
				auto elementType = refType->mElementType;
				mModule->PopulateType(elementType, BfPopulateType_Data);
				addDeref = elementType->mSize;
				if ((addDeref <= 0) && (!elementType->IsValuelessType()))
					mModule->AssertErrorState();
			}

			if ((paramType->IsComposite()) && (!paramType->IsTypedPrimitive()))
			{
				if (mModule->mCompiler->mOptions.mAllowStructByVal)
				{
					//byval
				}
				else
				{
					mModule->PopulateType(paramType, BfPopulateType_Data);

					auto typeInst = paramType->ToTypeInstance();
					if ((typeInst != NULL) && (typeInst->mIsCRepr) && (typeInst->IsSplattable()) && (!IsComptime()))
					{
						// We're splatting
					}
					else
					{
						if (doingThis)
						{
							mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_NoCapture);
							addDeref = paramType->mSize;
						}
						else if (methodInstance->WantsStructsAttribByVal(paramType))
						{
							mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_ByVal, mModule->mSystem->mPtrSize);
						}
					}
				}
			}
			else if (paramType->IsPrimitiveType())
			{
				auto primType = (BfPrimitiveType*)paramType;
				if ((primType->mTypeDef->mTypeCode == BfTypeCode_Boolean) && (!methodInstance->mIsIntrinsic))
					mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_ZExt);
			}

			if (addDeref >= 0)
			{
				mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_Dereferencable, addDeref);
			}

			argIdx++;
		};

		BfType* paramType = NULL;
		if (doingThis)
		{
			int thisIdx = methodInstance->GetThisIdx();
			paramType = methodInstance->GetThisType();
			if (paramType->IsValuelessType())
			{
				doingThis = false;
				continue;
			}
			bool isSplatted = methodInstance->GetParamIsSplat(thisIdx) && (!IsComptime()); // (resolvedTypeRef->IsSplattable()) && (!methodDef->mIsMutating);
			if (isSplatted)
			{
				BfTypeUtils::SplatIterate(_HandleParamType, paramType);
				doingThis = false;
				continue;
			}
			else
				_HandleParamType(paramType);
		}
		else
		{
			if (paramIdx >= methodInstance->GetParamCount())
				break;

			paramType = methodInstance->GetParamType(paramIdx);

			BfTypeCode loweredTypeCode = BfTypeCode_None;
			BfTypeCode loweredTypeCode2 = BfTypeCode_None;
			if (paramType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
			{
				argIdx++;
				paramIdx++;
				if (loweredTypeCode2 != BfTypeCode_None)
					argIdx++;
				continue;
			}

			if ((paramType->IsValuelessType()) && (!paramType->IsMethodRef()))
			{
				paramIdx++;
				continue;
			}

			if ((methodInstance->GetParamIsSplat(paramIdx)) && (!IsComptime()))
			{
				BfTypeUtils::SplatIterate(_HandleParamType, paramType);
				paramIdx++;
				continue;
			}
			else
				_HandleParamType(methodInstance->GetParamType(paramIdx));
		}

		if (doingThis)
			doingThis = false;
		else
			paramIdx++;
		//argIdx++;
	}

	if ((callFlags & BfCreateCallFlags_TailCall) != 0)
		mModule->mBfIRBuilder->SetTailCall(callInst);

	if (methodDef->mIsNoReturn)
	{
		mModule->mBfIRBuilder->CreateUnreachable();
		// For debuggability when looking back at stack trace
		//mModule->ExtendLocalLifetimes(0);

		if (mModule->IsTargetingBeefBackend())
		{
			auto checkScope = mModule->mCurMethodState->mCurScope;
			while (checkScope != NULL)
			{
				mModule->EmitLifetimeEnds(checkScope);
				checkScope = checkScope->mPrevScope;
			}

			// This 'fake' branch extends lifetimes of outer variable scopes
			if (mModule->mCurMethodState->mIRExitBlock)
				mModule->mBfIRBuilder->CreateBr_Fake(mModule->mCurMethodState->mIRExitBlock);
		}
	}
	else
	{
		if (mModule->mCurMethodState != NULL)
			mModule->mCurMethodState->mMayNeedThisAccessCheck = true;
	}

	BfTypedValue result;
	if (sret != NULL)
		result = *sret;
	else if (hasResult)
	{
		BfTypeCode loweredRetType = BfTypeCode_None;
		BfTypeCode loweredRetType2 = BfTypeCode_None;
		if ((!IsComptime()) && (methodInstance->GetLoweredReturnType(&loweredRetType, &loweredRetType2)) && (loweredRetType != BfTypeCode_None))
		{
			auto retVal = mModule->CreateAlloca(methodInstance->mReturnType);
			BfIRType loweredIRType = mModule->GetIRLoweredType(loweredRetType, loweredRetType2);
			loweredIRType = mModule->mBfIRBuilder->GetPointerTo(loweredIRType);
			auto castedRetVal = mModule->mBfIRBuilder->CreateBitCast(retVal, loweredIRType);
			mModule->mBfIRBuilder->CreateStore(callInst, castedRetVal);
			result = BfTypedValue(retVal, methodInstance->mReturnType, BfTypedValueKind_RestrictedTempAddr);
		}
		else
			result = BfTypedValue(callInst, methodInstance->mReturnType);
	}
	else
		result = mModule->GetFakeTypedValue(methodInstance->mReturnType);
	if (result.mType->IsRef())
	{
		result = mModule->RemoveRef(result);
		if (methodDef->mIsReadOnly)
		{
			if (result.mKind == BfTypedValueKind_Addr)
				result.mKind = BfTypedValueKind_ReadOnlyAddr;
		}
	}

	return result;
}

BfTypedValue BfExprEvaluator::CreateCall(BfMethodMatcher* methodMatcher, BfTypedValue target)
{
	auto moduleMethodInstance = GetSelectedMethod(*methodMatcher);
	if (moduleMethodInstance.mMethodInstance == NULL)
		return BfTypedValue();
	if ((target) && (target.mType != moduleMethodInstance.mMethodInstance->GetOwner()))
	{
		auto castedTarget = mModule->Cast(methodMatcher->mTargetSrc, target, moduleMethodInstance.mMethodInstance->GetOwner());
		BF_ASSERT(castedTarget);
		target = castedTarget;
	}

	PerformCallChecks(moduleMethodInstance.mMethodInstance, methodMatcher->mTargetSrc);

	BfCreateCallFlags callFlags = BfCreateCallFlags_None;
	if (methodMatcher->mAllowImplicitRef)
		callFlags = (BfCreateCallFlags)(callFlags | BfCreateCallFlags_AllowImplicitRef);
	return CreateCall(methodMatcher->mTargetSrc, target, BfTypedValue(), methodMatcher->mBestMethodDef, moduleMethodInstance, callFlags, methodMatcher->mArguments);
}

void BfExprEvaluator::MakeBaseConcrete(BfTypedValue& typedValue)
{
	if (typedValue.IsBase())
	{
		auto baseType = mModule->mCurTypeInstance->mBaseType;
		if (baseType == NULL)
			baseType = mModule->mContext->mBfObjectType;
		mModule->PopulateType(baseType, BfPopulateType_Data);
		typedValue = mModule->Cast(NULL, typedValue, baseType, BfCastFlags_Explicit);
	}
}

void BfExprEvaluator::SplatArgs(BfTypedValue value, SizedArrayImpl<BfIRValue>& irArgs)
{
	if (value.IsSplat())
	{
		int componentIdx = 0;
		BfTypeUtils::SplatIterate([&](BfType* checkType) { irArgs.push_back(mModule->ExtractSplatValue(value, componentIdx++, checkType)); }, value.mType);

		return;
	}

	mModule->mBfIRBuilder->PopulateType(value.mType);
	std::function<void(BfTypedValue)> checkTypeLambda = [&](BfTypedValue curValue)
	{
		BfType* checkType = curValue.mType;
		if (checkType->IsStruct())
		{
			auto checkTypeInstance = checkType->ToTypeInstance();
			if ((checkTypeInstance->mBaseType != NULL) && (!checkTypeInstance->mBaseType->IsValuelessType()))
			{
				BfTypedValue baseValue;
				if (curValue.IsAddr())
					baseValue = BfTypedValue((!curValue.mValue) ? BfIRValue() : mModule->mBfIRBuilder->CreateInBoundsGEP(curValue.mValue, 0, 0), checkTypeInstance->mBaseType, true);
				else
					baseValue = BfTypedValue((!curValue.mValue) ? BfIRValue() : mModule->mBfIRBuilder->CreateExtractValue(curValue.mValue, 0), checkTypeInstance->mBaseType);
				checkTypeLambda(baseValue);
			}

			if (checkTypeInstance->mIsUnion)
			{
				auto unionInnerType = checkTypeInstance->GetUnionInnerType();
				if (!unionInnerType->IsValuelessType())
				{
					BfTypedValue unionValue = mModule->ExtractValue(curValue, NULL, 1);
					checkTypeLambda(unionValue);
				}

				if (checkTypeInstance->IsEnum())
				{
					BfTypedValue dscrValue = mModule->ExtractValue(curValue, NULL, 2);
					checkTypeLambda(dscrValue);
				}
			}
			else
			{
				for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
				{
					auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
					if (fieldInstance->mDataIdx >= 0)
					{
						BfTypedValue fieldValue = mModule->ExtractValue(curValue, fieldInstance, fieldInstance->mDataIdx);
						checkTypeLambda(fieldValue);
					}
				}
			}
		}
		else if (checkType->IsMethodRef())
		{
			BF_ASSERT(curValue.IsAddr());

			BfMethodRefType* methodRefType = (BfMethodRefType*)checkType;
			for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
			{
				auto checkType = methodRefType->GetCaptureType(dataIdx);
				if (methodRefType->WantsDataPassedAsSplat(dataIdx))
				{
					BF_ASSERT(dataIdx == 0);

					auto ptrType = mModule->CreatePointerType(checkType);
					auto elemPtr = mModule->mBfIRBuilder->CreateBitCast(curValue.mValue, mModule->mBfIRBuilder->MapType(ptrType));
					checkTypeLambda(BfTypedValue(elemPtr, checkType, BfTypedValueKind_Addr));

					//BfTypedValue fieldValue = mModule->ExtractValue(curValue, fieldInstance, fieldInstance->mDataIdx);
					//checkTypeLambda(fieldValue);
				}
				else
				{
					auto elemVal = mModule->mBfIRBuilder->CreateInBoundsGEP(curValue.mValue, 0, dataIdx);
					if (!checkType->IsComposite())
						elemVal = mModule->mBfIRBuilder->CreateLoad(elemVal);
					irArgs.Add(elemVal);
					//irArgs.Add(mModule->ExtractValue(curValue, dataIdx));
				}
			}
		}
		else if (!checkType->IsValuelessType())
		{
			auto loadedVal = mModule->LoadValue(curValue);
			loadedVal = mModule->PrepareConst(loadedVal);
			irArgs.push_back(loadedVal.mValue);
		}
	};

	checkTypeLambda(value);
}

void BfExprEvaluator::PushArg(BfTypedValue argVal, SizedArrayImpl<BfIRValue>& irArgs, bool disableSplat, bool disableLowering, bool isIntrinsic, bool createCompositeCopy)
{
	MakeBaseConcrete(argVal);

	if (IsVar(argVal.mType))
	{
		argVal = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
	}

	if (argVal.mType->IsValuelessType())
		return;
	bool wantSplat = false;
	if ((argVal.mType->IsSplattable()) && (!disableSplat) && (!IsComptime()))
	{
		disableLowering = true;
		auto argTypeInstance = argVal.mType->ToTypeInstance();
		if (!disableSplat)
		{
			if ((argTypeInstance != NULL) && (argTypeInstance->mIsCRepr))
				wantSplat = true;
			else if ((int)irArgs.size() + argVal.mType->GetSplatCount() <= mModule->mCompiler->mOptions.mMaxSplatRegs)
				wantSplat = true;
		}
	}

	if (wantSplat)
	{
		SplatArgs(argVal, irArgs);
	}
	else
	{
		if (argVal.mType->IsComposite())
		{
			if ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0)
			{
				// Const eval entry - we want any incoming consts as they are
			}
			else if (isIntrinsic)
			{
				// We can handle composites either by value or not
			}
			else
				argVal = mModule->MakeAddressable(argVal);

			if ((!IsComptime()) && (!disableLowering) && (!isIntrinsic))
			{
				BfTypeCode loweredTypeCode = BfTypeCode_None;
				BfTypeCode loweredTypeCode2 = BfTypeCode_None;
				if (argVal.mType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
				{
					BfIRValue argPtrVal = argVal.mValue;

					int loweredSize = mModule->mBfIRBuilder->GetSize(loweredTypeCode) + mModule->mBfIRBuilder->GetSize(loweredTypeCode2);
					if (argVal.mType->mSize < loweredSize)
					{
						auto allocaVal = mModule->CreateAlloca(mModule->GetPrimitiveType(BfTypeCode_UInt8), true, NULL, mModule->GetConstValue(loweredSize));
						mModule->mBfIRBuilder->SetAllocaAlignment(allocaVal,
							BF_MAX(argVal.mType->mAlign,
								BF_MAX(mModule->mBfIRBuilder->GetSize(loweredTypeCode), mModule->mBfIRBuilder->GetSize(loweredTypeCode2))));
						auto castedPtr = mModule->mBfIRBuilder->CreateBitCast(allocaVal, mModule->mBfIRBuilder->MapType(mModule->CreatePointerType(argVal.mType)));
						auto argIRVal = mModule->mBfIRBuilder->CreateAlignedLoad(argVal.mValue, argVal.mType->mAlign);
						mModule->mBfIRBuilder->CreateAlignedStore(argIRVal, castedPtr, argVal.mType->mAlign);
						argPtrVal = castedPtr;
					}

					auto primType = mModule->mBfIRBuilder->GetPrimitiveType(loweredTypeCode);
					auto ptrType = mModule->mBfIRBuilder->GetPointerTo(primType);
					BfIRValue primPtrVal = mModule->mBfIRBuilder->CreateBitCast(argPtrVal, ptrType);
					auto primVal = mModule->mBfIRBuilder->CreateLoad(primPtrVal);
					irArgs.push_back(primVal);

					if (loweredTypeCode2 != BfTypeCode_None)
					{
						auto primType2 = mModule->mBfIRBuilder->GetPrimitiveType(loweredTypeCode2);
						auto ptrType2 = mModule->mBfIRBuilder->GetPointerTo(primType2);
						BfIRValue primPtrVal2;
						if (mModule->mBfIRBuilder->GetSize(loweredTypeCode) < mModule->mBfIRBuilder->GetSize(loweredTypeCode2))
							primPtrVal2 = mModule->mBfIRBuilder->CreateInBoundsGEP(mModule->mBfIRBuilder->CreateBitCast(primPtrVal, ptrType2), 1);
						else
							primPtrVal2 = mModule->mBfIRBuilder->CreateBitCast(mModule->mBfIRBuilder->CreateInBoundsGEP(primPtrVal, 1), ptrType2);

						auto primVal2 = mModule->mBfIRBuilder->CreateLoad(primPtrVal2);
						irArgs.Add(primVal2);
					}
					return;
				}
			}

			if ((createCompositeCopy) && (!argVal.IsTempAddr()))
			{
				// Non-Beef calling conventions require copies of composites
				auto copyAddr = mModule->CreateAlloca(argVal.mType);
				mModule->mBfIRBuilder->CreateStore(mModule->LoadValue(argVal).mValue, copyAddr);
				argVal = BfTypedValue(copyAddr, argVal.mType, BfTypedValueKind_TempAddr);
			}
		}
		else
			argVal = mModule->LoadValue(argVal);
		irArgs.Add(argVal.mValue);
	}
}

void BfExprEvaluator::PushThis(BfAstNode* targetSrc, BfTypedValue argVal, BfMethodInstance* methodInstance, SizedArrayImpl<BfIRValue>& irArgs, bool skipMutCheck)
{
	MakeBaseConcrete(argVal);

	auto methodDef = methodInstance->mMethodDef;
	if (methodInstance->IsSkipCall())
		return;

	if (!argVal)
	{
		//BF_ASSERT(mFunctionBindResult != NULL);
		return;
	}

	if (methodDef->mIsMutating)
	{
		bool checkMut = false;
		if (argVal.mType->IsGenericParam())
		{
			// For capturing mutability
			if (mResultLocalVar != NULL)
				mResultLocalVar->mWrittenToId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}

		if (((argVal.mType->IsComposite()) || (argVal.mType->IsTypedPrimitive())))
		{
			if (argVal.IsCopyOnMutate())
				argVal = mModule->CopyValue(argVal);

			if ((argVal.IsReadOnly()) || (!argVal.IsAddr()))
			{
				if (!skipMutCheck)
				{
					String err = StrFormat("call mutating method '%s' on", mModule->MethodToString(methodInstance).c_str());
					CheckModifyResult(argVal, targetSrc, err.c_str());
				}

				if (argVal.IsSplat())
				{
					argVal = mModule->AggregateSplat(argVal);
					argVal = mModule->MakeAddressable(argVal);
				}
			}
			else
			{
				if (mResultLocalVar != NULL)
				{
					// When we are capturing, we need to note that we require capturing by reference here
					mResultLocalVar->mWrittenToId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
				}
			}
		}
	}

	if (argVal.mType->IsValuelessType())
		return;

	auto owner = methodInstance->GetOwner();

	bool allowThisSplatting;
	if (mModule->mIsComptimeModule)
		allowThisSplatting = owner->IsTypedPrimitive() || owner->IsValuelessType();
	else
		allowThisSplatting = methodInstance->AllowsSplatting(-1);

	if ((!allowThisSplatting) || (methodDef->mIsMutating))
	{
		argVal = mModule->MakeAddressable(argVal);
		irArgs.push_back(argVal.mValue);
		return;
	}

	auto thisType = methodInstance->GetThisType();
	PushArg(argVal, irArgs, !methodInstance->AllowsSplatting(-1), thisType->IsPointer());
}

void BfExprEvaluator::FinishDeferredEvals(SizedArrayImpl<BfResolvedArg>& argValues)
{
	for (int argIdx = 0; argIdx < argValues.size(); argIdx++)
	{
		auto& argValue = argValues[argIdx].mTypedValue;
		if ((argValues[argIdx].mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt | BfArgFlag_DeferredEval)) != 0)
		{
			if (!argValue)
			{
				auto expr = BfNodeDynCast<BfExpression>(argValues[argIdx].mExpression);
				if (expr != NULL)
					argValue = mModule->CreateValueFromExpression(expr);
			}
		}
	}
}

void BfExprEvaluator::FinishDeferredEvals(BfResolvedArgs& argValues)
{
	for (int argIdx = 0; argIdx < (int)argValues.mResolvedArgs.size(); argIdx++)
	{
		auto& argValue = argValues.mResolvedArgs[argIdx].mTypedValue;
		if ((argValues.mResolvedArgs[argIdx].mArgFlags & (BfArgFlag_VariableDeclaration)) != 0)
		{
			auto variableDeclaration = BfNodeDynCast<BfVariableDeclaration>((*argValues.mArguments)[argIdx]);
			if ((variableDeclaration != NULL) && (variableDeclaration->mNameNode != NULL))
			{
				if (mModule->mCurMethodState == NULL)
				{
					mModule->Fail("Illegal local variable", variableDeclaration);
				}
				else
				{
					BfLocalVariable* localVar = new BfLocalVariable();
					localVar->mName = variableDeclaration->mNameNode->ToString();
					localVar->mResolvedType = mModule->GetPrimitiveType(BfTypeCode_Var);
					localVar->mAddr = mModule->mBfIRBuilder->GetFakeVal();
					localVar->mReadFromId = 0;
					localVar->mWrittenToId = 0;
					localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
					mModule->CheckVariableDef(localVar);
					localVar->Init();
					mModule->AddLocalVariableDef(localVar, true);
				}
			}
		}
	}

	FinishDeferredEvals(argValues.mResolvedArgs);
}

void BfExprEvaluator::AddCallDependencies(BfMethodInstance* methodInstance)
{
	if (methodInstance->mReturnType != NULL)
		mModule->AddDependency(methodInstance->mReturnType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
	for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
	{
		auto paramType = methodInstance->GetParamType(paramIdx);
		mModule->AddDependency(paramType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
	}
	if (methodInstance->mMethodInfoEx != NULL)
	{
		for (auto genericArg : methodInstance->mMethodInfoEx->mMethodGenericArguments)
		{
			if (genericArg->IsWrappableType())
				genericArg = mModule->GetWrappedStructType(genericArg);
			if (genericArg != NULL)
				mModule->AddDependency(genericArg, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
		}
	}
}

BfTypedValue BfExprEvaluator::CreateCall(BfAstNode* targetSrc, const BfTypedValue& inTarget, const BfTypedValue& origTarget, BfMethodDef* methodDef, BfModuleMethodInstance moduleMethodInstance, BfCreateCallFlags callFlags, SizedArrayImpl<BfResolvedArg>& argValues, BfTypedValue* argCascade)
{
	SetAndRestoreValue<BfEvalExprFlags> prevEvalExprFlag(mBfEvalExprFlags);

	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mConstEvalAttributeTypeDef)))
	{
		mModule->mAttributeState->mUsed = true;
		mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);
	}
	else if ((moduleMethodInstance.mMethodInstance->mComptimeFlags & BfComptimeFlag_ConstEval) != 0)
	{
		mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);
	}
	else if (((moduleMethodInstance.mMethodInstance->mComptimeFlags & BfComptimeFlag_Comptime) != 0) && (!mModule->mIsComptimeModule))
	{
		if ((mModule->mCurMethodInstance == NULL) || (mModule->mCurMethodInstance->mComptimeFlags == BfComptimeFlag_None))
			mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);
	}

	if (((moduleMethodInstance.mMethodInstance->mComptimeFlags & BfComptimeFlag_OnlyFromComptime) != 0) &&
		((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) &&
		((mModule->mCurMethodInstance == NULL) || (mModule->mCurMethodInstance->mComptimeFlags == BfComptimeFlag_None)) &&
		(!mModule->mIsComptimeModule))
	{
		mModule->Fail(StrFormat("Method '%s' can only be invoked at comptime. Consider adding [Comptime] to the current method.", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
	}

	bool bypassVirtual = (callFlags & BfCreateCallFlags_BypassVirtual) != 0;
	bool skipThis = (callFlags & BfCreateCallFlags_SkipThis) != 0;;

	static int sCallIdx = 0;
 	if (!mModule->mCompiler->mIsResolveOnly)
		sCallIdx++;
	int callIdx = sCallIdx;
	if (callIdx == 0x000015CB)
	{
		NOP;
	}

	// Temporarily disable so we don't capture calls in params
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);
	SetAndRestoreValue<bool> prevAllowVariableDeclarations;
	if (mModule->mCurMethodState != NULL)
		prevAllowVariableDeclarations.Init(mModule->mCurMethodState->mCurScope->mAllowVariableDeclarations, false);

	BfMethodInstance* methodInstance = moduleMethodInstance.mMethodInstance;

	SizedArray<BfIRValue, 4> irArgs;

	if ((methodDef->mIsAbstract) && (bypassVirtual))
	{
		mModule->Fail(StrFormat("Abstract base method '%s' cannot be invoked", mModule->MethodToString(methodInstance).c_str()), targetSrc);
	}

	bool isSkipCall = moduleMethodInstance.mMethodInstance->IsSkipCall(bypassVirtual);
	BfType* returnType = methodInstance->mReturnType;
	/*if (returnType->IsSelf())
	{
		returnType = methodInstance->GetOwner();
		BF_ASSERT(returnType->IsInterface());
	}*/	

	Array<BfTypedValue> argCascades;
	BfTypedValue target = inTarget;

	if (!skipThis)
	{
		if ((target) && (target.mType->IsFunction()) && (methodInstance->GetOwner() == target.mType))
		{
			CheckResultForReading(target);
			target = mModule->LoadValue(target);
			auto funcType = mModule->mBfIRBuilder->MapMethod(moduleMethodInstance.mMethodInstance);
			auto funcPtrType = mModule->mBfIRBuilder->GetPointerTo(funcType);
			moduleMethodInstance.mFunc = mModule->mBfIRBuilder->CreateIntToPtr(target.mValue, funcPtrType);
		}
		else if (!methodDef->mIsStatic)
		{
			if ((!target) && (prevBindResult.mPrevVal != NULL))
			{
				auto bindResult = prevBindResult.mPrevVal;
				if (bindResult->mBindType != NULL)
				{
					// Allow binding a function to a 'this' type even if no target is specified
					auto delegateInfo = bindResult->mBindType->GetDelegateInfo();
					if (delegateInfo != NULL)
					{
						if (delegateInfo->mHasExplicitThis)
						{
							target = mModule->GetDefaultTypedValue(delegateInfo->mParams[0], false, BfDefaultValueKind_Addr);
							bypassVirtual = true;
						}
					}
					else if (bindResult->mBindType->IsFunction())
					{
						BfMethodInstance* invokeMethodInstance = mModule->GetRawMethodInstanceAtIdx(bindResult->mBindType->ToTypeInstance(), 0, "Invoke");
						if (!invokeMethodInstance->mMethodDef->mIsStatic)
						{
							target = mModule->GetDefaultTypedValue(invokeMethodInstance->GetThisType(), false, BfDefaultValueKind_Addr);
						}
					}
				}
			}

			if (!target)
			{
				FinishDeferredEvals(argValues);
				auto error = mModule->Fail(StrFormat("An instance reference is required to %s the non-static method '%s'",
					(prevBindResult.mPrevVal != NULL) ? "bind" : "invoke",
					mModule->MethodToString(methodInstance).c_str()), targetSrc);
				if ((error != NULL) && (methodInstance->mMethodDef->GetRefNode() != NULL))
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
				return mModule->GetDefaultTypedValue(returnType);
			}

			auto prevResult = mResult;
			mResult = target;
			CheckResultForReading(mResult);
			mResult = prevResult;

			if (methodDef->mMethodType != BfMethodType_Ctor)
			{
				bool doAccessCheck = true;
				if (target.IsThis())
				{
					if (!mModule->mCurMethodState->mMayNeedThisAccessCheck)
						doAccessCheck = false;
				}
				if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mDisableObjectAccessChecksAttributeTypeDef)))
					doAccessCheck = false;
				if ((doAccessCheck) && (!isSkipCall) && (prevBindResult.mPrevVal == NULL))
					mModule->EmitObjectAccessCheck(target);
			}

			if (((prevBindResult.mPrevVal == NULL) || (!prevBindResult.mPrevVal->mSkipThis)) &&
				(!isSkipCall))
			{
				bool skipMutCheck = false;
				if ((prevBindResult.mPrevVal != NULL) && (prevBindResult.mPrevVal->mSkipMutCheck))
				{
					// If we are binding a delegate, then we will end up making a copy of the target anyway
					//  so we don't need to do a mutability check
					skipMutCheck = true;
				}

				if (methodDef->mMethodType == BfMethodType_Extension)
					PushArg(target, irArgs);
				else
					PushThis(targetSrc, target, moduleMethodInstance.mMethodInstance, irArgs, skipMutCheck);
			}
		}
		else if (methodDef->mMethodType == BfMethodType_Extension)
		{
			// Handled in args
		}
		else
		{
			mModule->CheckStaticAccess(methodInstance->mMethodInstanceGroup->mOwner);

			if (target)
			{
				FinishDeferredEvals(argValues);
				mModule->Fail(StrFormat("Method '%s' cannot be accessed with an instance reference; qualify it with a type name instead",
					mModule->MethodToString(methodInstance).c_str()), targetSrc);
				return mModule->GetDefaultTypedValue(returnType);
			}
		}
	}

	if (isSkipCall)
	{
		CheckSkipCall(targetSrc, argValues);
		FinishDeferredEvals(argValues);
		mModule->EmitEnsureInstructionAt();
		return mModule->GetDefaultTypedValue(returnType);
	}

	bool hasNamedArgs = false;
	for (auto& arg : argValues)
		if (arg.mNameNode != NULL)
			hasNamedArgs = true;
	if (hasNamedArgs)
	{
		methodDef->BuildParamNameMap();

		BfIdentifierNode* outOfPlaceName = NULL;
		int curParamIdx = 0;

		SizedArrayImpl<BfResolvedArg> origArgValues = argValues;
		argValues.Clear();

		for (int argIdx = 0; argIdx < origArgValues.mSize; argIdx++)
		{
			int paramIdx = curParamIdx;

			auto& argValue = origArgValues[argIdx];
			if (argValue.mNameNode != NULL)
			{
				int namedParamIdx = -1;
				if (methodDef->mParamNameMap->TryGetValue(argValue.mNameNode->ToStringView(), &namedParamIdx))
				{
					paramIdx = namedParamIdx;
				}
				else
				{
					if (mModule->PreFail())
					{
						mModule->Fail(StrFormat("The best overload for '%s' does not have a parameter named '%s'", methodInstance->mMethodDef->mName.c_str(),
							argValue.mNameNode->ToString().c_str()), argValue.mNameNode);
					}
				}

				if (paramIdx != curParamIdx)
					outOfPlaceName = argValue.mNameNode;
			}
			else if (outOfPlaceName != NULL)
			{
				if (mModule->PreFail())
					mModule->Fail(StrFormat("Named argument '%s' is used out-of-position but is followed by an unnamed argument", outOfPlaceName->ToString().c_str()), outOfPlaceName);
				outOfPlaceName = NULL;
			}

			if ((paramIdx < methodInstance->GetParamCount()) && (paramIdx != argIdx))
			{
				if (methodInstance->GetParamKind(paramIdx) == BfParamKind_Normal)
				{
					auto wantType = methodInstance->GetParamType(paramIdx);
					auto resolvedValue = ResolveArgValue(argValue, wantType);
					if (resolvedValue)
					{
						argValue.mTypedValue = resolvedValue;
						argValue.mArgFlags = (BfArgFlags)(argValue.mArgFlags | BfArgFlag_Finalized);
					}
				}
			}

			while (paramIdx >= argValues.mSize)
				argValues.Add(BfResolvedArg());
			if (argValues[paramIdx].mExpression != NULL)
			{
				if (argValue.mNameNode != NULL)
				{
					if (mModule->PreFail())
						mModule->Fail(StrFormat("Named argument '%s' cannot be specified multiple times", argValue.mNameNode->ToString().c_str()), argValue.mNameNode);
				}
			}
			argValues[paramIdx] = argValue;

			curParamIdx++;
		}
	}

	int argIdx = 0;
	int paramIdx = 0;

	BfIRValue expandedParamAlloca;
	BfTypedValue expandedParamsArray;
	BfType* expandedParamsElementType = NULL;
	int extendedParamIdx = 0;

	AddCallDependencies(methodInstance);

	bool wasCapturingMatchInfo = false;
	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		// Set to false to make sure we don't capture method match info from 'params' array creation
		wasCapturingMatchInfo = autoComplete->mIsCapturingMethodMatchInfo;
		autoComplete->mIsCapturingMethodMatchInfo = false;
	}
	defer(
		{
			if (autoComplete != NULL)
				autoComplete->mIsCapturingMethodMatchInfo = wasCapturingMatchInfo;
		});

	BfScopeData* boxScopeData = mDeferScopeAlloc;
	if ((boxScopeData == NULL) && (mModule->mCurMethodState != NULL))
		boxScopeData = mModule->mCurMethodState->mCurScope;

	bool failed = false;
	while (true)
	{
		int argExprIdx = argIdx;
		if (methodDef->mMethodType == BfMethodType_Extension)
			argExprIdx--;

		bool isThis = (paramIdx == -1) || ((methodDef->mHasExplicitThis) && (paramIdx == 0));
		bool isDirectPass = false;

		if (paramIdx >= (int)methodInstance->GetParamCount())
		{
			if (methodInstance->IsVarArgs())
			{
				if (argExprIdx >= (int)argValues.size())
					break;

				BfTypedValue argValue = ResolveArgValue(argValues[argExprIdx], NULL);
				if (argValue)
				{
					auto typeInst = argValue.mType->ToTypeInstance();

					if (argValue.mType == mModule->GetPrimitiveType(BfTypeCode_Float))
						argValue = mModule->Cast(argValues[argExprIdx].mExpression, argValue, mModule->GetPrimitiveType(BfTypeCode_Double));

					if ((typeInst != NULL) && (typeInst->IsInstanceOf(mModule->mCompiler->mStringTypeDef)))
					{
						BfType* charType = mModule->GetPrimitiveType(BfTypeCode_Char8);
						BfType* charPtrType = mModule->CreatePointerType(charType);
						argValue = mModule->Cast(argValues[argExprIdx].mExpression, argValue, charPtrType);
					}
					PushArg(argValue, irArgs, true, false);
				}
				argIdx++;
				continue;
			}

			if (argExprIdx < (int)argValues.size())
			{
				if (mModule->PreFail())
				{
					BfAstNode* errorRef = argValues[argExprIdx].mExpression;
					if (errorRef == NULL)
						errorRef = targetSrc;

					BfError* error;
					if ((argValues[argExprIdx].mArgFlags & BfArgFlag_StringInterpolateArg) != 0)
					{
						int checkIdx = argExprIdx - 1;
						while (checkIdx >= 0)
						{
							if ((argValues[checkIdx].mArgFlags & BfArgFlag_StringInterpolateFormat) != 0)
							{
								errorRef = argValues[checkIdx].mExpression;
								break;
							}
							checkIdx--;
						}
						error = mModule->Fail("Expanded string interpolation generates too many arguments. If string allocation was intended then consider adding a specifier such as 'scope'.", errorRef);
					}
					else if ((prevBindResult.mPrevVal != NULL) && (prevBindResult.mPrevVal->mBindType != NULL))
						error = mModule->Fail(StrFormat("Method '%s' has too few parameters to bind to '%s'.", mModule->MethodToString(methodInstance).c_str(), mModule->TypeToString(prevBindResult.mPrevVal->mBindType).c_str()), errorRef);
					else
						error = mModule->Fail(StrFormat("Too many arguments, expected %d fewer.", (int)argValues.size() - argExprIdx), errorRef);
					if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
				}
				failed = true;
				break;
			}
			break;
		}

		// Only create actual params if we're not just trying to bind the function
		if ((prevBindResult.mPrevVal != NULL) && (!prevBindResult.mPrevVal->mWantsArgs))
			break;

		BfType* wantType = NULL;
		bool wantsSplat = false;
		if (expandedParamsElementType != NULL)
		{
			wantType = expandedParamsElementType;
		}
		else
		{
			wantsSplat = methodInstance->GetParamIsSplat(paramIdx) && (!IsComptime());
			if (methodInstance->IsImplicitCapture(paramIdx))
			{
				auto paramType = methodInstance->GetParamType(paramIdx);
				if (mModule->mCurMethodInstance->IsMixin())
				{
					// Don't bother, also- can fail on captures
				}
				else
				{
// 					static int captureIdx = 0;
// 					captureIdx++;
// 					int curCaptureIdx = captureIdx;
//
// 					if (curCaptureIdx == 0x91)
// 					{
// 						NOP;
// 					}

					auto lookupVal = DoImplicitArgCapture(targetSrc, methodInstance, paramIdx, failed, BfImplicitParamKind_General, origTarget);
					if (lookupVal)
					{
						if (wantsSplat)
						{
							SplatArgs(lookupVal, irArgs);
						}
						else if (paramType->IsRef())
						{
							irArgs.push_back(lookupVal.mValue);
						}
						else
							PushArg(lookupVal, irArgs, true);
					}
				}
				paramIdx++;
				continue;
			}

			wantType = methodInstance->GetParamType(paramIdx);
			if (!mModule->mCurTypeInstance->IsInterface())
			{
				// Resolve `Self` types
				if (wantType->IsUnspecializedTypeVariation())
				{
					wantType = mModule->ResolveSelfType(wantType, methodInstance->GetOwner());
				}
			}

			if (IsVar(wantType))
			{
				// Case happens when we can't find the argument type
				failed = true;
			}
			BfParamKind paramKind = methodInstance->GetParamKind(paramIdx);

			if (paramKind == BfParamKind_Params)
			{
				//TODO: Check to see if it's a direct array pass

				if (argIdx < (int)argValues.size())
				{
					auto argValue = argValues[argIdx].mTypedValue;
					if ((argValue.IsParams()) /*&& (mModule->CanCast(argValue, wantType))*/)
						isDirectPass = true;
				}

				if (!isDirectPass)
				{
					int numElements = BF_MAX((int)argValues.size() - argIdx, 0);
					if (methodDef->mMethodType == BfMethodType_Extension)
						numElements++;

					if (IsConstEval())
					{
						if ((wantType->IsArray()) || (wantType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef)))
						{
							auto genericTypeInst = wantType->ToGenericTypeInstance();
							expandedParamsElementType = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[0];

							auto irSizedArrayType = mModule->mBfIRBuilder->GetSizedArrayType(mModule->mBfIRBuilder->MapType(expandedParamsElementType), numElements);

							Array<BfIRValue> values;
							for (int i = 0; i < numElements; i++)
								values.Add(mModule->mBfIRBuilder->GetFakeVal());
							expandedParamsArray = BfTypedValue(mModule->mBfIRBuilder->CreateConstAgg(irSizedArrayType, values), wantType);

							PushArg(expandedParamsArray, irArgs);
						}
						continue;
					}
					else if (wantType->IsArray())
					{
						BfArrayType* arrayType = (BfArrayType*)wantType;
						mModule->PopulateType(arrayType, BfPopulateType_DataAndMethods);
						expandedParamsElementType = arrayType->mGenericTypeInfo->mTypeGenericArguments[0];

						int arrayClassSize = arrayType->mInstSize - expandedParamsElementType->mSize;

						expandedParamsArray = BfTypedValue(mModule->AllocFromType(arrayType->GetUnderlyingType(), boxScopeData, BfIRValue(), mModule->GetConstValue(numElements), 1, BfAllocFlags_None),
							arrayType, false);

						BfResolvedArgs resolvedArgs;
						MatchConstructor(targetSrc, NULL, expandedParamsArray, arrayType, resolvedArgs, false, false);

						//TODO: Assert 'length' var is at slot 1
						auto arrayBits = mModule->mBfIRBuilder->CreateBitCast(expandedParamsArray.mValue, mModule->mBfIRBuilder->MapType(arrayType->mBaseType));
						int arrayLengthBitCount = arrayType->GetLengthBitCount();
						if (arrayLengthBitCount == 0)
						{
							mModule->Fail("INTERNAL ERROR: Unable to find array 'length' field", targetSrc);
							return BfTypedValue();
						}

						auto& fieldInstance = arrayType->mBaseType->mFieldInstances[0];
						auto addr = mModule->mBfIRBuilder->CreateInBoundsGEP(arrayBits, 0, fieldInstance.mDataIdx);
						if (arrayLengthBitCount == 64)
							mModule->mBfIRBuilder->CreateAlignedStore(mModule->GetConstValue64(numElements), addr, 8);
						else
							mModule->mBfIRBuilder->CreateAlignedStore(mModule->GetConstValue32(numElements), addr, 4);

						PushArg(expandedParamsArray, irArgs);
						continue;
					}
					else if (wantType->IsInstanceOf(mModule->mCompiler->mSpanTypeDef))
					{
						mModule->PopulateType(wantType);
						mModule->mBfIRBuilder->PopulateType(wantType);
						auto genericTypeInst = wantType->ToGenericTypeInstance();
						expandedParamsElementType = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[0];

						expandedParamsArray = BfTypedValue(mModule->CreateAlloca(wantType), wantType, true);
						expandedParamAlloca = mModule->CreateAlloca(genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[0], true, NULL, mModule->GetConstValue(numElements));
						mModule->mBfIRBuilder->CreateAlignedStore(expandedParamAlloca, mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamsArray.mValue, 0, 1), mModule->mSystem->mPtrSize);
						mModule->mBfIRBuilder->CreateAlignedStore(mModule->GetConstValue(numElements), mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamsArray.mValue, 0, 2), mModule->mSystem->mPtrSize);

						PushArg(expandedParamsArray, irArgs, !wantsSplat);
						continue;
					}
					else if (wantType->IsSizedArray())
					{
						mModule->PopulateType(wantType);
						mModule->mBfIRBuilder->PopulateType(wantType);
						BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)wantType;
						expandedParamsElementType = wantType->GetUnderlyingType();

						if (numElements != sizedArrayType->mElementCount)
						{
							BfAstNode* refNode = targetSrc;
							if (argExprIdx < (int)argValues.size())
								refNode = argValues[argExprIdx].mExpression;
							mModule->Fail(StrFormat("Incorrect number of arguments to match params type '%s'", mModule->TypeToString(wantType).c_str()), refNode);
						}

						expandedParamsArray = BfTypedValue(mModule->CreateAlloca(wantType), wantType, true);
						expandedParamAlloca = mModule->mBfIRBuilder->CreateBitCast(expandedParamsArray.mValue, mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->MapType(expandedParamsElementType)));
						PushArg(expandedParamsArray, irArgs, !wantsSplat);
						continue;
					}
				}
			}
		}

		BfAstNode* arg = NULL;
		bool hadMissingArg = false;
		if (argExprIdx == -1)
			arg = targetSrc;
		if (argExprIdx >= 0)
		{
			if (argExprIdx < (int)argValues.size())
			{
				arg = argValues[argExprIdx].mExpression;
				if (((argValues[argExprIdx].mArgFlags & BfArgFlag_StringInterpolateArg) != 0) && (!expandedParamsArray))
				{
					BfAstNode* errorRef = arg;
					int checkIdx = argExprIdx - 1;
					while (checkIdx >= 0)
					{
						if ((argValues[checkIdx].mArgFlags & BfArgFlag_StringInterpolateFormat) != 0)
						{
							errorRef = argValues[checkIdx].mExpression;
							break;
						}
						checkIdx--;
					}
					mModule->Warn(BfWarning_BF4205_StringInterpolationParam, "Expanded string interpolation argument not used as 'params'. If string allocation was intended then consider adding a specifier such as 'scope'.", errorRef);
				}

// 				if ((arg == NULL) && (argValues[argExprIdx].mExpression != NULL))
// 					hadMissingArg = true;

				if ((arg == NULL) && (!argValues[argExprIdx].mTypedValue))
					hadMissingArg = true;
			}
			else
				hadMissingArg = true;
		}

		BfTypedValue argValue;

		if (hadMissingArg)
		{
			if (expandedParamsArray)
				break;

			if ((argIdx >= (int) methodInstance->mDefaultValues.size()) || (!methodInstance->mDefaultValues[argIdx]))
			{
				BfAstNode* refNode = targetSrc;
				if (argValues.size() > 0)
				{
					auto checkExpr = argValues.back().mExpression;
					if (checkExpr != NULL)
						refNode = checkExpr;
				}

				BfAstNode* prevNode = NULL;
 				if (targetSrc == NULL)
 				{
 					// We must be in BfModule::EmitCtorBody
 				}
 				else if (auto tupleExpr = BfNodeDynCastExact<BfTupleExpression>(targetSrc))
 				{
 					if (tupleExpr->mCommas.size() > 0)
 						prevNode = tupleExpr->mCommas.back();
 					else
 						prevNode = tupleExpr->mOpenParen;
 					if (tupleExpr->mCloseParen != NULL)
 						refNode = tupleExpr->mCloseParen;
 				}
 				else if (mModule->mParentNodeEntry != NULL)
				{
					if (auto objectCreateExpr = BfNodeDynCast<BfObjectCreateExpression>(mModule->mParentNodeEntry->mNode))
					{
						if (objectCreateExpr->mCommas.size() > 0)
							prevNode = objectCreateExpr->mCommas.back();
						else
							prevNode = objectCreateExpr->mOpenToken;
						if (objectCreateExpr->mCloseToken != NULL)
							refNode = objectCreateExpr->mCloseToken;

						if (auto newNode = BfNodeDynCast<BfNewNode>(objectCreateExpr->mNewNode))
						{
							if (newNode->mAllocNode == targetSrc)
								refNode = targetSrc;
						}
					}
					else if (auto invokeExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
					{
						if (invokeExpr->mCommas.size() > 0)
							prevNode = invokeExpr->mCommas.back();
						else
							prevNode = invokeExpr->mOpenParen;
						if (invokeExpr->mCloseParen != NULL)
							refNode = invokeExpr->mCloseParen;
					}
				}

				if ((autoComplete != NULL) && (prevNode != NULL))
					autoComplete->CheckEmptyStart(prevNode, wantType);

				BfError* error = NULL;

				if (mModule->mParentNodeEntry != NULL)
				{
					bool showCtorError = false;

					if (auto ctorDeclaration = BfNodeDynCast<BfConstructorDeclaration>(mModule->mParentNodeEntry->mNode))
					{
						if (ctorDeclaration->mInitializer == NULL)
							showCtorError = true;
					}

					if (auto typerDecl = BfNodeDynCast<BfTypeDeclaration>(mModule->mParentNodeEntry->mNode))
						showCtorError = true;

					if (showCtorError)
					{
						if (mModule->PreFail())
						{
							error = mModule->Fail(StrFormat("No parameterless constructor is available for base class. Consider calling base constructor '%s'.",
								mModule->MethodToString(methodInstance).c_str()), refNode);
						}

						auto srcNode = mModule->mCurMethodInstance->mMethodDef->GetRefNode();
						if ((autoComplete != NULL) && (autoComplete->CheckFixit(srcNode)))
							autoComplete->FixitAddConstructor(mModule->mCurTypeInstance);
					}
				}

				if (mModule->PreFail())
				{
					if (error == NULL)
					{
						if (hasNamedArgs)
							error = mModule->Fail(StrFormat("There is no argument given that corresponds to the required formal parameter '%s' of '%s'.",
								methodInstance->GetParamName(paramIdx).c_str(), mModule->MethodToString(methodInstance).c_str()), refNode);
						else
							error = mModule->Fail(StrFormat("Not enough parameters specified, expected %d more.", methodInstance->GetParamCount() - paramIdx), refNode);
					}
					if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
				}
				failed = true;
				break;
			}

			auto foreignDefaultVal = methodInstance->mDefaultValues[argIdx];
			auto foreignConst = methodInstance->GetOwner()->mConstHolder->GetConstant(foreignDefaultVal.mValue);

			if (foreignConst->mConstType == BfConstType_AggZero)
			{
				// Allow this
			}
			else if (foreignConst->mTypeCode == BfTypeCode_NullPtr)
			{
				if (wantType->IsNullable())
				{
					argValue = mModule->GetDefaultTypedValue(wantType, false, BfDefaultValueKind_Addr);
				}
			}
			else if (foreignConst->mConstType == BfConstType_GlobalVar)
			{
				auto globalVar = (BfGlobalVar*)foreignConst;
				if (globalVar->mName[0] == '#')
				{
					if (strcmp(globalVar->mName, "#CallerLineNum") == 0)
					{
						argValue = BfTypedValue(mModule->GetConstValue(mModule->mCurFilePosition.mCurLine + 1), mModule->GetPrimitiveType(BfTypeCode_Int32));
					}
					else if (strcmp(globalVar->mName, "#CallerFilePath") == 0)
					{
						String filePath = "";
						if (mModule->mCurFilePosition.mFileInstance != NULL)
							filePath = mModule->mCurFilePosition.mFileInstance->mParser->mFileName;
						argValue = BfTypedValue(mModule->GetStringObjectValue(filePath),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#CallerFileName") == 0)
					{
						String filePath = "";
						if (mModule->mCurFilePosition.mFileInstance != NULL)
							filePath = mModule->mCurFilePosition.mFileInstance->mParser->mFileName;
						argValue = BfTypedValue(mModule->GetStringObjectValue(GetFileName(filePath)),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#CallerFileDir") == 0)
					{
						String filePath = "";
						if (mModule->mCurFilePosition.mFileInstance != NULL)
							filePath = mModule->mCurFilePosition.mFileInstance->mParser->mFileName;
						argValue = BfTypedValue(mModule->GetStringObjectValue(GetFileDir(filePath)),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#CallerTypeName") == 0)
					{
						String typeName = "";
						if (mModule->mCurTypeInstance != NULL)
							typeName = mModule->TypeToString(mModule->mCurTypeInstance);
						argValue = BfTypedValue(mModule->GetStringObjectValue(typeName),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#CallerType") == 0)
					{
						auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);
						BfType* type = mModule->mCurTypeInstance;
						if (type != NULL)
						{
							mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
							argValue = BfTypedValue(mModule->CreateTypeDataRef(type), typeType);
						}
					}
					else if (strcmp(globalVar->mName, "#CallerMemberName") == 0)
					{
						String memberName = "";
						if (mModule->mCurMethodInstance != NULL)
							memberName = mModule->MethodToString(mModule->mCurMethodInstance);
						argValue = BfTypedValue(mModule->GetStringObjectValue(memberName),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#CallerProject") == 0)
					{
						String projectName = "";
						if (mModule->mCurMethodInstance != NULL)
							projectName = mModule->mCurMethodInstance->mMethodDef->mDeclaringType->mProject->mName;
						argValue = BfTypedValue(mModule->GetStringObjectValue(projectName),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else if (strcmp(globalVar->mName, "#ProjectName") == 0)
					{
						String projectName = methodInstance->mMethodDef->mDeclaringType->mProject->mName;
						argValue = BfTypedValue(mModule->GetStringObjectValue(projectName),
							mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
					}
					else
					{
						argValue = mModule->GetCompilerFieldValue(globalVar->mName);
					}
				}
			}
			else if (foreignConst->mConstType == BfConstType_GEP32_2)
			{
				auto constGep32_2 = (BfConstantGEP32_2*)foreignConst;
				auto gepTarget = methodInstance->GetOwner()->mConstHolder->GetConstantById(constGep32_2->mTarget);
				if (gepTarget->mConstType == BfConstType_GlobalVar)
				{
					auto globalVar = (BfGlobalVar*)gepTarget;
					if (globalVar->mName[0] == '#')
					{
						if (strcmp(globalVar->mName, "#CallerExpression") == 0)
						{
							int exprIdx = constGep32_2->mIdx1;
							if ((exprIdx >= 0) && (exprIdx < (int)argValues.size()))
							{
								auto expr = argValues[exprIdx].mExpression;
								if (expr != NULL)
								{
									argValue = BfTypedValue(mModule->GetStringObjectValue(expr->ToString()),
										mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
								}
							}
							else
							{
								mModule->Fail("CallerExpression index out of bounds", targetSrc);
								argValue = BfTypedValue(mModule->GetStringObjectValue(""),
									mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
							}
						}
					}
				}
			}

			if (!argValue)
			{
				argValue = mModule->GetTypedValueFromConstant(foreignConst, methodInstance->GetOwner()->mConstHolder, foreignDefaultVal.mType);
				if (!argValue)
					mModule->Fail("Default parameter value failed", targetSrc);
				mModule->mBfIRBuilder->PopulateType(foreignDefaultVal.mType);
			}
		}
		else
		{
			if (argExprIdx == -1)
				argValue = target;
			else
				argValue = argValues[argExprIdx].mTypedValue;

			if ((argValue.IsParams()) && (!isDirectPass))
			{
				BfAstNode* refNode = arg;
				if (auto unaryOperatorExpr = BfNodeDynCast<BfUnaryOperatorExpression>(refNode))
					refNode = unaryOperatorExpr->mOpToken;
				mModule->Warn(0, "Unused 'params' expression", refNode);
			}

			if (wantType->IsMethodRef())
			{
				auto expr = argValues[argExprIdx].mExpression;
				if (expr != NULL)
					SetMethodElementType(expr);

				if (!argValue)
 					argValue = mModule->CreateValueFromExpression(BfNodeDynCast<BfExpression>(arg), wantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast));

				// Add any implicit captures now
				auto methodRefType = (BfMethodRefType*)wantType;
				BfMethodInstance* useMethodInstance = methodRefType->mMethodRef;

				for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
				{
					int paramIdx = methodRefType->GetParamIdxFromDataIdx(dataIdx);
					auto lookupVal = DoImplicitArgCapture(arg, useMethodInstance, paramIdx, failed, BfImplicitParamKind_General, argValue);
					if (lookupVal)
					{
					 	if (methodRefType->WantsDataPassedAsSplat(dataIdx))
					 		SplatArgs(lookupVal, irArgs);
						else
						{
							if (lookupVal.mType->IsComposite())
								lookupVal = mModule->MakeAddressable(lookupVal, false);
							irArgs.push_back(lookupVal.mValue);
						}
					}
				}

				paramIdx++;
				argIdx++;
				continue;
			}
			else if (argExprIdx >= 0)
			{
				BfParamKind paramKind = BfParamKind_Normal;
				BfIdentifierNode* paramNameNode = NULL;
				if (paramIdx < methodInstance->GetParamCount())
				{
					paramKind = methodInstance->GetParamKind(paramIdx);
					paramNameNode = methodInstance->GetParamNameNode(paramIdx);
				}

				argValues[argExprIdx].mExpectedType = wantType;
				argValue = ResolveArgValue(argValues[argExprIdx], wantType, NULL, paramKind, paramNameNode);
			}
		}

		if (!argValue)
		{
			failed = true;
		}

		if ((arg != NULL) && (autoComplete != NULL) && (autoComplete->mResolveType == BfResolveType_GetResultString) && (autoComplete->IsAutocompleteNode(arg)))
		{
			if (!autoComplete->mResultString.Contains('\r'))
			{
				String str;
				str += methodInstance->GetParamName(paramIdx);
				str += " @ ";
				bool isCtor = methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor;
				if (isCtor)
					str += methodInstance->GetOwner()->mTypeDef->mName->ToString();
				else
					str += methodInstance->mMethodDef->mName;
				str += "(";
				for (int i = isCtor ? 1 : 0; i < methodInstance->GetParamCount(); i++)
				{
					if (i > (isCtor ? 1 : 0))
						str += ",";
					if (i == paramIdx)
					{
						if (methodInstance->GetParamKind(paramIdx) == BfParamKind_Params)
							str += "params ";
						str += mModule->TypeToString(methodInstance->GetParamType(paramIdx));
					}
				}
				str += ")";

				if (!autoComplete->mResultString.StartsWith(":"))
					autoComplete->mResultString.Clear();

				int crPos = (int)autoComplete->mResultString.IndexOf('\n');
				if (crPos == -1)
					crPos = autoComplete->mResultString.mLength;
				int insertPos = BF_MAX(1, crPos);

				if (autoComplete->mResultString.IsEmpty())
					autoComplete->mResultString += ":";
				if (insertPos > 1)
					str.Insert(0, '\r');
				autoComplete->mResultString.Insert(insertPos, str);
			}
		}

		if (argValue)
		{
			if ((isThis) && (argValue.mType->IsRef()))
			{
				// Convert a 'ref this' to a 'this*'
				argValue.mType = mModule->CreatePointerType(argValue.mType->GetUnderlyingType());
			}

			BfAstNode* refNode = arg;
			if (refNode == NULL)
				refNode = targetSrc;

			if ((wantType->IsRef()) && (!argValue.mType->IsRef()) &&
				(((callFlags & BfCreateCallFlags_AllowImplicitRef) != 0) || (wantType->IsIn())))
			{
				auto underlyingType = wantType->GetUnderlyingType();
				if (mModule->mCurMethodState != NULL)
				{
					SetAndRestoreValue<BfScopeData*> prevScopeData(mModule->mCurMethodState->mOverrideScope, boxScopeData);
					argValue = mModule->Cast(refNode, argValue, underlyingType);
				}
				else
					argValue = mModule->Cast(refNode, argValue, underlyingType, ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) ? BfCastFlags_WantsConst : BfCastFlags_None);
				if (argValue)
					argValue = mModule->ToRef(argValue, (BfRefType*)wantType);
			}
			else
			{
				if (mModule->mCurMethodState != NULL)
				{
					SetAndRestoreValue<BfScopeData*> prevScopeData(mModule->mCurMethodState->mOverrideScope, boxScopeData);
					argValue = mModule->Cast(refNode, argValue, wantType, ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) ? BfCastFlags_WantsConst : BfCastFlags_None);
				}
				else
					argValue = mModule->Cast(refNode, argValue, wantType, ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) ? BfCastFlags_WantsConst : BfCastFlags_None);
			}

			if (!argValue)
			{
				if ((argExprIdx < (int)argValues.size()) && ((argValues[argExprIdx].mArgFlags & BfArgFlag_StringInterpolateArg) != 0))
				{
					BfAstNode* errorRef = NULL;
					int checkIdx = argExprIdx - 1;
					while (checkIdx >= 0)
					{
						if ((argValues[checkIdx].mArgFlags & BfArgFlag_StringInterpolateFormat) != 0)
						{
							errorRef = argValues[checkIdx].mExpression;
							break;
						}
						checkIdx--;
					}
					if (errorRef != NULL)
						mModule->Warn(0, "If string allocation was intended then consider adding a specifier such as 'scope'.", errorRef);
				}

				failed = true;
			}
			else if ((wantType->IsComposite()) && (!expandedParamsArray))
			{
				if (methodInstance->mIsIntrinsic)
				{
					// Intrinsics can handle structs either by value or address
				}
				else
				{
					// We need to make a temp and get the addr of that
					if ((!wantsSplat) && (!argValue.IsValuelessType()) && (!argValue.IsAddr()) && (!IsConstEval()))
					{
						argValue = mModule->MakeAddressable(argValue);
					}
				}
			}
			else if (!wantType->IsRef())
				argValue = mModule->LoadValue(argValue);
		}

		if ((argExprIdx != -1) && (argExprIdx < (int)argValues.size()) && ((argValues[argExprIdx].mArgFlags & BfArgFlag_Cascade) != 0))
		{
			mUsedAsStatement = true;
			if (argValues[argExprIdx].mUncastedTypedValue)
				argCascades.Add(argValues[argExprIdx].mUncastedTypedValue);
			else
				argCascades.Add(argValue);
		}

		if (expandedParamsArray)
		{
			if (argValue)
			{
				if (IsConstEval())
				{
					auto constant = mModule->mBfIRBuilder->GetConstant(expandedParamsArray.mValue);
					BF_ASSERT(constant->mConstType == BfConstType_Agg);
					auto constAgg = (BfConstantAgg*)constant;
					constAgg->mValues[extendedParamIdx] = argValue.mValue;
				}
				else if (expandedParamAlloca)
				{
					argValue = mModule->LoadValue(argValue);
					auto addr = mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamAlloca, extendedParamIdx);
					auto storeInst = mModule->mBfIRBuilder->CreateAlignedStore(argValue.mValue, addr, argValue.mType->mAlign);
				}
				else
				{
					auto firstElem = mModule->GetFieldByName(expandedParamsArray.mType->ToTypeInstance(), "mFirstElement");
					if (firstElem != NULL)
					{
						argValue = mModule->LoadValue(argValue);
						auto firstAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamsArray.mValue, 0, firstElem->mDataIdx);
						auto indexedAddr = mModule->CreateIndexedValue(argValue.mType, firstAddr, extendedParamIdx);
						if (argValue.IsSplat())
							mModule->AggregateSplatIntoAddr(argValue, indexedAddr);
						else
							mModule->mBfIRBuilder->CreateAlignedStore(argValue.mValue, indexedAddr, argValue.mType->mAlign);
					}
				}
			}
			extendedParamIdx++;
		}
		else
		{
			if ((paramIdx == 0) && (methodInstance->GetParamName(paramIdx) == "this") && (wantType->IsPointer()))
			{
				auto underlyingType = wantType->GetUnderlyingType();
				mModule->PopulateType(underlyingType, BfPopulateType_Data);
				if ((underlyingType->IsValuelessType()) && (!underlyingType->IsVoid()))
				{
					// We don't actually pass a 'this' pointer for mut methods on valueless structs
					argIdx++;
					paramIdx++;
					continue;
				}
			}

			if (argValue)
			{
				if (isThis)
					PushThis(targetSrc, argValue, methodInstance, irArgs);
				else if (wantsSplat)
					SplatArgs(argValue, irArgs);
				else
					PushArg(argValue, irArgs, true, false, methodInstance->mIsIntrinsic, methodInstance->mCallingConvention != BfCallingConvention_Unspecified);
			}
			paramIdx++;
		}
		argIdx++;
	}

	if (failed)
	{
		// Process the other unused arguments
		while (argIdx < argValues.size())
		{
			mModule->AssertErrorState();
			auto argValue = argValues[argIdx].mTypedValue;
			if ((argValues[argIdx].mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt | BfArgFlag_DeferredEval | BfArgFlag_VariableDeclaration | BfArgFlag_UninitializedExpr)) != 0)
			{
				if (!argValue)
				{
					auto expr = BfNodeDynCast<BfExpression>(argValues[argIdx].mExpression);
					if (expr != NULL)
						argValue = mModule->CreateValueFromExpression(expr);
				}
			}
			argIdx++;
		}

		return mModule->GetDefaultTypedValue(returnType, false, BfDefaultValueKind_Addr);
	}

	prevBindResult.Restore();

	if (!methodDef->mIsStatic)
	{
		bool ignoreVirtualError = (mModule->mBfIRBuilder->mIgnoreWrites) && (mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsForeignMethodDef);

		if ((methodInstance->GetOwner()->IsInterface()) && (!target.mType->IsGenericParam()) && (!target.mType->IsConcreteInterfaceType()) &&
			(!mModule->mCurTypeInstance->IsInterface()) && (!ignoreVirtualError))
		{
			if (methodInstance->mVirtualTableIdx == -1)
			{
				mModule->PopulateType(methodInstance->GetOwner(), BfPopulateType_DataAndMethods);
			}

			if (methodInstance->mVirtualTableIdx == -1)
			{
				if (methodInstance->mMethodDef->mIsConcrete)
				{
					mModule->Fail(StrFormat("The method '%s' cannot be invoked from an interface reference because its return value is declared as 'concrete'", mModule->MethodToString(methodInstance).c_str()), targetSrc);
				}
				else if (methodInstance->HasSelf())
				{
					mModule->Fail(StrFormat("The method '%s' cannot be invoked from an interface reference because it contains 'Self' type references", mModule->MethodToString(methodInstance).c_str()), targetSrc);
				}
				else
				{
					if ((bypassVirtual) && (mModule->mCurTypeInstance->IsInterface()))
					{
						// Allow a base call to be defined
					}
					else if ((methodInstance->IsSpecializedGenericMethod()) && (origTarget) && (!origTarget.mType->IsInterface()))
					{
						if (!mModule->mBfIRBuilder->mIgnoreWrites)
							mModule->AssertErrorState();
					}
					else if ((!mModule->mCurMethodInstance->mIsUnspecialized))
					{
						// Compiler error?

						String errorString = "Unable to dynamically dispatch '%s'";
						if (methodInstance->IsSpecializedGenericMethod())
							errorString = "Unable to dynamically dispatch '%s' because generic methods can only be directly dispatched";
						if (methodInstance->mReturnType->IsConcreteInterfaceType())
							errorString = "Unable to dynamically dispatch '%s' because the concrete return type is unknown";

						mModule->Fail(StrFormat(errorString.c_str(), mModule->MethodToString(methodInstance).c_str()), targetSrc);
					}

					//BF_ASSERT(mModule->mCurMethodInstance->mIsUnspecialized);
				}

				if (mFunctionBindResult != NULL)
				{
					mFunctionBindResult->mMethodInstance = methodInstance;
					mFunctionBindResult->mTarget = target;
					mFunctionBindResult->mFunc = moduleMethodInstance.mFunc;
					for (auto arg : irArgs)
						mFunctionBindResult->mIRArgs.push_back(arg);
				}

				return mModule->GetDefaultTypedValue(returnType);
			}
		}
	}
	else
	{
		//BF_ASSERT(!methodInstance->GetOwner()->IsInterface());
	}

	if (target.mType != NULL)
	{
		// When we call a method from a static ctor, that method could access static fields so we need to make sure
		//  the type has been initialized
		auto targetTypeInst = target.mType->ToTypeInstance();
		if (targetTypeInst != NULL)
			mModule->CheckStaticAccess(targetTypeInst);
	}

	if (methodInstance->mReturnType == NULL)
	{
		mModule->AssertErrorState();
		mModule->Fail("Circular reference in method instance", targetSrc);
		return BfTypedValue();
	}

	BfCreateCallFlags physCallFlags = BfCreateCallFlags_None;
	if ((origTarget.mType != NULL) && (origTarget.mType->IsGenericParam()))
		physCallFlags = (BfCreateCallFlags)(physCallFlags | BfCreateCallFlags_GenericParamThis);

	auto func = moduleMethodInstance.mFunc;
	BfTypedValue callResult = CreateCall(targetSrc, methodInstance, func, bypassVirtual, irArgs, NULL, physCallFlags, origTarget.mType);

	if ((methodInstance->mMethodDef->mIsNoReturn) && ((mBfEvalExprFlags & BfEvalExprFlags_IsExpressionBody) != 0) &&
		(mExpectingType != NULL) && (callResult.mType != mExpectingType))
	{
		callResult = mModule->GetDefaultTypedValue(mExpectingType);
	}

	// This gets triggered for non-sret (ie: comptime) composite returns so they aren't considered readonly
	if ((callResult.mKind == BfTypedValueKind_Value) && (!callResult.mValue.IsConst()) &&
		(!callResult.mType->IsValuelessType()) && (callResult.mType->IsComposite()) && (!methodInstance->GetLoweredReturnType()))
	{
		bool makeAddressable = true;
		auto typeInstance = callResult.mType->ToTypeInstance();
		if ((typeInstance != NULL) && (typeInstance->mHasUnderlyingArray))
			makeAddressable = false;
		if (makeAddressable)
		{
			callResult = mModule->MakeAddressable(callResult, true);
		}
	}

	if (argCascades.mSize == 1)
	{
		if (argCascade == NULL)
			return argCascades[0];
		*argCascade = argCascades[0];
	}
	if (argCascades.mSize > 1)
	{
		if (argCascade == NULL)
			return mModule->CreateTuple(argCascades, {});
		*argCascade = mModule->CreateTuple(argCascades, {});
	}

	return callResult;
}

BfTypedValue BfExprEvaluator::MatchConstructor(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, BfTypeInstance* targetType, BfResolvedArgs& argValues, bool callCtorBodyOnly, bool allowAppendAlloc, BfTypedValue* appendIndexValue)
{
	// Temporarily disable so we don't capture calls in params
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);

	static int sCtorCount = 0;
	sCtorCount++;

	BfMethodMatcher methodMatcher(targetSrc, mModule, "", argValues.mResolvedArgs, BfMethodGenericArguments());
	methodMatcher.mBfEvalExprFlags = mBfEvalExprFlags;

	BfTypeVector typeGenericArguments;

	auto curTypeInst = targetType;
	auto curTypeDef = targetType->mTypeDef;

	BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;

	auto activeTypeDef = mModule->GetActiveTypeDef();
	auto visibleProjectSet = mModule->GetVisibleProjectSet();
	bool isFailurePass = false;
	for (int pass = 0; pass < 2; pass++)
	{
		isFailurePass = pass == 1;

		curTypeDef->PopulateMemberSets();
		BfMethodDef* nextMethodDef = NULL;
		BfMemberSetEntry* entry;
		if (curTypeDef->mMethodSet.TryGetWith(String("__BfCtor"), &entry))
			nextMethodDef = (BfMethodDef*)entry->mMemberDef;
		while (nextMethodDef != NULL)
		{
			auto checkMethod = nextMethodDef;
			nextMethodDef = nextMethodDef->mNextWithSameName;

			if ((isFailurePass) && (checkMethod->mMethodDeclaration == NULL))
				continue; // Don't match private default ctor if there's a user-defined one

			if (checkMethod->mIsStatic)
				continue;

			if ((checkMethod->mDeclaringType->IsExtension()) && (mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) &&
				(mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mNoExtensionAttributeTypeDef)))
			{
				mModule->mAttributeState->mUsed = true;
				continue;
			}

			if (!mModule->IsInSpecializedSection())
			{
				if ((!curTypeInst->IsTypeMemberIncluded(checkMethod->mDeclaringType, activeTypeDef, mModule)) ||
					(!curTypeInst->IsTypeMemberAccessible(checkMethod->mDeclaringType, visibleProjectSet)))
					continue;
			}

			auto checkProt = checkMethod->mProtection;

			if (!isFailurePass)
			{
				if (callCtorBodyOnly)
				{
					if (curTypeDef != mModule->mCurTypeInstance->mTypeDef)
					{
						// We're calling the base class's ctor from a derived class
						if (checkProt <= BfProtection_Private)
							continue;
					}
				}
				else
				{
					if ((checkProt == BfProtection_Protected) || (checkProt == BfProtection_ProtectedInternal)) // Treat protected constructors as private
						checkProt = BfProtection_Private;
					if (!mModule->CheckProtection(protectionCheckFlags, curTypeInst, checkMethod->mDeclaringType->mProject, checkProt, curTypeInst))
						continue;
				}
			}

			methodMatcher.CheckMethod(NULL, curTypeInst, checkMethod, isFailurePass);
		}

		if ((methodMatcher.mBestMethodDef != NULL) || (methodMatcher.mBackupMethodDef != NULL))
			break;
	}

	if (methodMatcher.mBestMethodDef == NULL)
 		methodMatcher.mBestMethodDef = methodMatcher.mBackupMethodDef;

	if (methodMatcher.mBestMethodDef == NULL)
	{
		mModule->Fail("No constructor available", targetSrc);
		return BfTypedValue();
	}

	auto methodDef = methodMatcher.mBestMethodDef;
	if (mModule->mCompiler->mResolvePassData != NULL)
		mModule->mCompiler->mResolvePassData->HandleMethodReference(targetSrc, curTypeInst->mTypeDef->GetDefinition(), methodDef);

	// There should always be a constructor
	BF_ASSERT(methodMatcher.mBestMethodDef != NULL);

	auto moduleMethodInstance = mModule->GetMethodInstance(methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher.mBestMethodGenericArguments);
	if (!mModule->CheckUseMethodInstance(moduleMethodInstance.mMethodInstance, targetSrc))
	{
		return BfTypedValue();
	}

	BfAutoComplete* autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		BfTypeInstance* resolvedTypeInstance = target.mType->ToTypeInstance();
		auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDef->mMethodDeclaration);
		if ((autoComplete->mIsGetDefinition) && (autoComplete->IsAutocompleteNode(targetSrc)) && (!BfNodeIsA<BfDelegateBindExpression>(targetSrc)))
		{
			if ((autoComplete->mDefMethod == NULL) && (autoComplete->mDefField == NULL) &&
				(autoComplete->mDefProp == NULL)
                && ((autoComplete->mDefType == NULL) || (autoComplete->mDefType == resolvedTypeInstance->mTypeDef)))
			{
				// Do we need to do this mDefType setting?  If we do, then make sure we only get the element type of generics and such
				//autoComplete->mDefType = resolvedTypeInstance->mTypeDef;
				if (ctorDecl != NULL)
					autoComplete->SetDefinitionLocation(ctorDecl->mThisToken, true);
				else if (resolvedTypeInstance->mTypeDef->mTypeDeclaration != NULL)
					autoComplete->SetDefinitionLocation(resolvedTypeInstance->mTypeDef->mTypeDeclaration->mNameNode, true);
			}
		}
		else if ((autoComplete->mResolveType == BfResolveType_GetResultString) && (autoComplete->IsAutocompleteNode(targetSrc)) &&
			(moduleMethodInstance.mMethodInstance != NULL))
		{
			autoComplete->mResultString = ":";
			autoComplete->mResultString += mModule->MethodToString(moduleMethodInstance.mMethodInstance);
		}
	}

	BfConstructorDeclaration* ctorDecl = (BfConstructorDeclaration*)methodMatcher.mBestMethodDef->mMethodDeclaration;
	if ((methodMatcher.mBestMethodDef->mHasAppend) && (targetType->IsObject()))
	{
		if (!allowAppendAlloc)
		{
			if (mModule->mCurMethodInstance->mMethodDef->mMethodDeclaration == NULL)
				mModule->Fail("Constructors with append allocations cannot be called from a default constructor. Considering adding an explicit default constructor with the [AllowAppend] specifier.", targetSrc);
			else
				mModule->Fail("Constructors with append allocations cannot be called from a constructor without [AllowAppend] specified.", targetSrc);
		}
		else
		{
			BfResolvedArg resolvedArg;
 			if (appendIndexValue != NULL)
 			{
				resolvedArg.mTypedValue = *appendIndexValue;
 			}
			else
			{
				auto intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				auto intPtrRefType = mModule->CreateRefType(intPtrType);
				if (target.mValue.IsFake())
				{
					resolvedArg.mTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), intPtrRefType);
				}
				else
				{
					BFMODULE_FATAL(mModule, "Bad");
				}
			}
			methodMatcher.mArguments.Insert(0, resolvedArg);
		}
	}	

	if (isFailurePass)
		mModule->Fail(StrFormat("'%s' is inaccessible due to its protection level", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
	prevBindResult.Restore();
	return CreateCall(methodMatcher.mTargetSrc, target, BfTypedValue(), methodMatcher.mBestMethodDef, moduleMethodInstance, BfCreateCallFlags_None, methodMatcher.mArguments);
}

static int sInvocationIdx = 0;

BfTypedValue BfExprEvaluator::ResolveArgValue(BfResolvedArg& resolvedArg, BfType* wantType, BfTypedValue* receivingValue, BfParamKind paramKind, BfIdentifierNode* paramNameNode)
{
	BfTypedValue argValue = resolvedArg.mTypedValue;
	if ((resolvedArg.mArgFlags & BfArgFlag_Finalized) != 0)
		return argValue;

	if ((resolvedArg.mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt | BfArgFlag_DeferredEval)) != 0)
	{
		if ((!argValue) || (argValue.mValue.IsFake()) || (resolvedArg.mWantsRecalc))
		{
			resolvedArg.mWantsRecalc = false;
			auto expr = BfNodeDynCast<BfExpression>(resolvedArg.mExpression);
			if (expr != NULL)
			{
				BfExprEvaluator exprEvaluator(mModule);
				exprEvaluator.mReceivingValue = receivingValue;
				BfEvalExprFlags flags = (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast);
				if ((paramKind == BfParamKind_Params) || (paramKind == BfParamKind_DelegateParam))
					flags = (BfEvalExprFlags)(flags | BfEvalExprFlags_AllowParamsExpr);

				argValue = mModule->CreateValueFromExpression(exprEvaluator, expr, wantType, flags);

				if ((argValue) && (argValue.mType != wantType) && (wantType != NULL))
				{
					if ((mDeferScopeAlloc != NULL) && (wantType == mModule->mContext->mBfObjectType))
					{
						BfAllocTarget allocTarget(mDeferScopeAlloc);
						argValue = mModule->BoxValue(expr, argValue, wantType, allocTarget, ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) ? BfCastFlags_WantsConst : BfCastFlags_None);
					}
					else
						argValue = mModule->Cast(expr, argValue, wantType, ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) ? BfCastFlags_WantsConst : BfCastFlags_None);
				}
			}
		}
	}
	else if ((resolvedArg.mArgFlags & (BfArgFlag_DeferredValue)) != 0)
	{
		// We should have already had an error on the first call
		SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, mModule->mHadBuildError);
		auto expr = BfNodeDynCast<BfExpression>(resolvedArg.mExpression);
		BF_ASSERT(expr != NULL);
		argValue = mModule->CreateValueFromExpression(expr, wantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_AllowOutExpr));
		resolvedArg.mUncastedTypedValue = argValue;
		if ((argValue) && (wantType != NULL))
			argValue = mModule->Cast(expr, argValue, wantType);
	}
	else if ((resolvedArg.mArgFlags & (BfArgFlag_UntypedDefault)) != 0)
	{
		argValue = mModule->GetDefaultTypedValue(wantType);
	}
	else if ((resolvedArg.mArgFlags & (BfArgFlag_VariableDeclaration | BfArgFlag_UninitializedExpr)) != 0)
	{
		auto variableDeclaration = BfNodeDynCast<BfVariableDeclaration>(resolvedArg.mExpression);

		auto variableType = wantType;

		bool isLet = (variableDeclaration != NULL) && (variableDeclaration->mTypeRef->IsExact<BfLetTypeReference>());
		bool isVar = (variableDeclaration == NULL) || (variableDeclaration->mTypeRef->IsExact<BfVarTypeReference>());

		if (mModule->mCurMethodState->mPendingNullConditional != NULL)
		{
			mModule->Fail("Variables cannot be declared in method arguments inside null conditional expressions", variableDeclaration);
		}

		if ((!isLet) && (!isVar))
		{
			if (variableType->IsVar())
			{
				auto resolvedType = mModule->ResolveTypeRef(variableDeclaration->mTypeRef);
				if (resolvedType != NULL)
					variableType = resolvedType;
			}
			else
			{
				mModule->Fail("Only 'ref' or 'var' variables can be declared in method arguments", variableDeclaration);

				auto autoComplete = GetAutoComplete();
				if (autoComplete != NULL)
					autoComplete->CheckTypeRef(variableDeclaration->mTypeRef, true, true);
			}
		}
		else
		{
			if (variableType->IsVar())
			{
				mModule->Fail("Variable type required for 'var' parameter types", variableDeclaration);
			}
		}

		if (wantType->IsRef())
		{
			auto refType = (BfRefType*)wantType;
			variableType = refType->mElementType;
		}

		if ((variableDeclaration != NULL) && (variableDeclaration->mInitializer != NULL))
		{
			mModule->Fail("Initializers cannot be used when declaring variables for 'out' parameters", variableDeclaration->mEqualsNode);
			mModule->CreateValueFromExpression(variableDeclaration->mInitializer, variableType, BfEvalExprFlags_NoCast);
		}

		BfLocalVariable* localVar = new BfLocalVariable();
		if ((variableDeclaration != NULL) && (variableDeclaration->mNameNode != NULL))
		{
			localVar->mName = variableDeclaration->mNameNode->ToString();
			localVar->mNameNode = BfNodeDynCast<BfIdentifierNode>(variableDeclaration->mNameNode);
		}
		else
		{
			if (paramNameNode != NULL)
			{
				localVar->mName = "__";
				paramNameNode->ToString(localVar->mName);
				localVar->mName += "_";
				localVar->mName += StrFormat("%d", mModule->mCurMethodState->GetRootMethodState()->mCurLocalVarId);
			}
			else
				localVar->mName = "__" + StrFormat("%d", mModule->mCurMethodState->GetRootMethodState()->mCurLocalVarId);
		}

		localVar->mResolvedType = variableType;
		if (!variableType->IsValuelessType())
			localVar->mAddr = mModule->CreateAlloca(variableType);
		localVar->mIsReadOnly = isLet;
		localVar->mReadFromId = 0;
		localVar->mWrittenToId = 0;
		localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		mModule->CheckVariableDef(localVar);
		localVar->Init();
		mModule->AddLocalVariableDef(localVar, true);

		CheckVariableDeclaration(resolvedArg.mExpression, false, false, false);

		argValue = BfTypedValue(localVar->mAddr, mModule->CreateRefType(variableType, BfRefType::RefKind_Out));

		auto curScope = mModule->mCurMethodState->mCurScope;
		if (curScope->mScopeKind == BfScopeKind_StatementTarget)
		{
			// Move this variable into the parent scope
			curScope->mLocalVarStart = (int)mModule->mCurMethodState->mLocals.size();
		}
	}
	return argValue;
}

BfTypedValue BfExprEvaluator::CheckEnumCreation(BfAstNode* targetSrc, BfTypeInstance* enumType, const StringImpl& caseName, BfResolvedArgs& argValues)
{
	auto activeTypeDef = mModule->GetActiveTypeDef();

	mModule->PopulateType(enumType);
	mModule->mBfIRBuilder->PopulateType(enumType);

	auto resolvePassData = mModule->mCompiler->mResolvePassData;
// 	if (resolvePassData != NULL)
// 	{
// 		if (mModule->mParentNodeEntry != NULL)
// 		{
// 			if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
// 			{
// 				if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(invocationExpr->mTarget))
// 				{
// 					BfAstNode* dotNode = memberRefExpr->mDotToken;
// 					BfAstNode* nameNode = targetSrc;
// 					String filter;
// 					auto autoComplete = resolvePassData->mAutoComplete;
// 					if ((autoComplete != NULL) && (autoComplete->InitAutocomplete(dotNode, nameNode, filter)))
// 						autoComplete->AddEnumTypeMembers(enumType, caseName, false, enumType == mModule->mCurTypeInstance);
// 				}
// 			}
// 		}
// 	}

	for (int fieldIdx = 0; fieldIdx < (int)enumType->mFieldInstances.size(); fieldIdx++)
	{
		auto fieldInstance = &enumType->mFieldInstances[fieldIdx];
		auto fieldDef = fieldInstance->GetFieldDef();
		if (fieldDef == NULL)
			continue;
		if ((fieldInstance->mIsEnumPayloadCase) && (fieldDef->mName == caseName))
		{
			if ((!enumType->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, mModule)) ||
				(!enumType->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
				continue;

			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
			{
				BfAutoComplete::MethodMatchEntry methodMatchEntry;
				methodMatchEntry.mPayloadEnumField = fieldInstance;
				methodMatchEntry.mTypeInstance = enumType;
				methodMatchEntry.mCurMethodInstance = mModule->mCurMethodInstance;
				autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);
			}

			if (resolvePassData != NULL)
			{
				BfAstNode* nameNode = targetSrc;
				if (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field)
					resolvePassData->HandleFieldReference(nameNode, enumType->mTypeDef, fieldDef);
			}

			BfIRValue enumValue;
			BfTypedValue result;

			bool wantConst = IsConstEval();

			if (wantConst)
			{
				NOP;
			}
			else if ((mReceivingValue != NULL) && (mReceivingValue->mType == enumType) && (mReceivingValue->IsAddr()))
			{
				result = *mReceivingValue;
				mReceivingValue = NULL;
				enumValue = result.mValue;
			}
			else
			{
				mResultIsTempComposite = true;
				enumValue = mModule->CreateAlloca(enumType);
				result = BfTypedValue(enumValue, fieldInstance->mOwner, BfTypedValueKind_TempAddr);
			}

			BF_ASSERT(fieldInstance->mResolvedType->IsTuple());
			auto tupleType = (BfTypeInstance*)fieldInstance->mResolvedType;
			mModule->mBfIRBuilder->PopulateType(tupleType);

			bool constFailed = false;
			SizedArray<BfIRValue, 8> constTupleMembers;
			BfIRValue fieldPtr;
			BfIRValue tuplePtr;

			if (wantConst)
			{
				constTupleMembers.Add(mModule->mBfIRBuilder->CreateConstAggZero(mModule->mBfIRBuilder->MapType(tupleType->mBaseType)));
			}
			else if (!tupleType->IsValuelessType())
			{
				fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(enumValue, 0, 1);
				auto tuplePtrType = mModule->CreatePointerType(tupleType);
				auto mappedPtrType = mModule->mBfIRBuilder->MapType(tuplePtrType);
				tuplePtr = mModule->mBfIRBuilder->CreateBitCast(fieldPtr, mappedPtrType);
			}

			for (int tupleFieldIdx = 0; tupleFieldIdx < (int)tupleType->mFieldInstances.size(); tupleFieldIdx++)
			{
				auto tupleFieldInstance = &tupleType->mFieldInstances[tupleFieldIdx];
				auto resolvedFieldType = tupleFieldInstance->GetResolvedType();

				if (tupleFieldIdx >= argValues.mResolvedArgs.size())
				{
					BfAstNode* refNode = targetSrc;
					BfAstNode* prevNode = NULL;
					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invokeExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							if (invokeExpr->mCloseParen != NULL)
								refNode = invokeExpr->mCloseParen;
						}
					}
					BfError* error = mModule->Fail(StrFormat("Not enough parameters specified, expected %d more.", tupleType->mFieldInstances.size() - (int)argValues.mArguments->size()), refNode);
					if (error != NULL)
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See enum declaration"), fieldDef->mFieldDeclaration);
					if (wantConst)
						constFailed = true;
					break;
				}

				BfTypedValue receivingValue;
				BfIRValue tupleFieldPtr;
				if (tuplePtr)
				{
					tupleFieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(tuplePtr, 0, tupleFieldInstance->mDataIdx);
					receivingValue = BfTypedValue(tupleFieldPtr, tupleFieldInstance->mResolvedType, true);
				}

				auto argValue = ResolveArgValue(argValues.mResolvedArgs[tupleFieldIdx], resolvedFieldType, &receivingValue);

				if (!argValue)
				{
					if (wantConst)
						constFailed = true;
					continue;
				}
				if (resolvedFieldType->IsValuelessType())
					continue;

				// Used receiving value?
				if (argValue.mValue == receivingValue.mValue)
					continue;

				argValue = mModule->AggregateSplat(argValue);
				argValues.mResolvedArgs[tupleFieldIdx].mExpectedType = resolvedFieldType;
				if ((argValues.mResolvedArgs[tupleFieldIdx].mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt)) != 0)
				{
					auto expr = BfNodeDynCast<BfExpression>(argValues.mResolvedArgs[tupleFieldIdx].mExpression);
					BF_ASSERT(expr != NULL);
					argValue = mModule->CreateValueFromExpression(expr, resolvedFieldType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
				}

				if (argValue)
				{
					if ((argValue.mType->IsRef()) && (argValue.mType->GetUnderlyingType() == resolvedFieldType))
					{
						if (auto unaryOperator = BfNodeDynCast<BfUnaryOperatorExpression>(argValues.mResolvedArgs[tupleFieldIdx].mExpression))
						{
							mModule->Fail(StrFormat("Invalid use of '%s'. Enum payloads can only be retrieved through 'case' expressions.", BfGetOpName(unaryOperator->mOp)), unaryOperator->mOpToken);
							argValue = mModule->GetDefaultTypedValue(resolvedFieldType);
						}
						else if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(argValues.mResolvedArgs[tupleFieldIdx].mExpression))
						{
							mModule->Fail("Invalid variable declaration. Enum payloads can only be retrieved through 'case' expressions.", varDecl);
							argValue = mModule->GetDefaultTypedValue(resolvedFieldType);
						}
					}

					// argValue can have a value even if tuplePtr does not have a value. This can happen if we are assigning to a (void) tuple,
					//  but we have a value that needs to be attempted to be casted to void
					argValue = mModule->Cast(argValues.mResolvedArgs[tupleFieldIdx].mExpression, argValue, resolvedFieldType, wantConst ? BfCastFlags_WantsConst : BfCastFlags_None);
					if (wantConst)
					{
						if (!argValue.mValue.IsConst())
						{
							mModule->Fail("Field not const", argValues.mResolvedArgs[tupleFieldIdx].mExpression);
							constFailed = true;
						}
						constTupleMembers.Add(argValue.mValue);
					}
					else if (tupleFieldPtr)
					{
						argValue = mModule->LoadValue(argValue);
						if (argValue)
							mModule->mBfIRBuilder->CreateAlignedStore(argValue.mValue, tupleFieldPtr, resolvedFieldType->mAlign);
					}
				}
				else if (wantConst)
					constFailed = true;
			}

			if ((intptr)argValues.mResolvedArgs.size() > tupleType->mFieldInstances.size())
			{
				BfAstNode* errorRef = argValues.mResolvedArgs[tupleType->mFieldInstances.size()].mExpression;
				if (errorRef == NULL)
					errorRef = targetSrc;

				BfError* error = mModule->Fail(StrFormat("Too many arguments, expected %d fewer.", argValues.mResolvedArgs.size() - tupleType->mFieldInstances.size()), errorRef);
				if (error != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See enum declaration"), fieldDef->mFieldDeclaration);

				if (wantConst)
					constFailed = true;
			}

			auto dscrType = enumType->GetDiscriminatorType();
			auto dscrField = &enumType->mFieldInstances.back();
			int tagIdx = -fieldInstance->mDataIdx - 1;

			if ((wantConst) && (!constFailed))
			{
				auto unionType = enumType->GetUnionInnerType();
				auto constTuple = mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(tupleType, BfIRPopulateType_Full), constTupleMembers);

				Array<uint8> memArr;
				memArr.Resize(unionType->mSize);
				if (!mModule->mBfIRBuilder->WriteConstant(constTuple, memArr.mVals, tupleType))
				{
					constFailed = true;
				}
				else
				{
					auto unionValue = mModule->mBfIRBuilder->ReadConstant(memArr.mVals, unionType);
					if (!unionValue)
					{
						constFailed = true;
					}
					else
					{
						SizedArray<BfIRValue, 3> constEnumMembers;
						constEnumMembers.Add(mModule->mBfIRBuilder->CreateConstAggZero(mModule->mBfIRBuilder->MapType(enumType->mBaseType, BfIRPopulateType_Full)));
						constEnumMembers.Add(unionValue);
						constEnumMembers.Add(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx));
						return BfTypedValue(mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(enumType, BfIRPopulateType_Full), constEnumMembers), enumType);
					}
				}
			}

			if (constFailed)
			{
				return mModule->GetDefaultTypedValue(enumType, false, BfDefaultValueKind_Addr);
			}

			auto dscFieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(enumValue, 0, dscrField->mDataIdx);
			mModule->mBfIRBuilder->CreateAlignedStore(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), dscFieldPtr, 4);
			return result;
		}
	}

	return BfTypedValue();
}

bool BfExprEvaluator::CheckGenericCtor(BfGenericParamType* genericParamType, BfResolvedArgs& argValues, BfAstNode* targetSrc)
{
	BfGenericParamFlags genericParamFlags = BfGenericParamFlag_None;
	BfType* typeConstraint = NULL;
	auto genericParam = mModule->GetMergedGenericParamData((BfGenericParamType*)genericParamType, genericParamFlags, typeConstraint);

	bool success = true;

	if ((argValues.mArguments != NULL) && (argValues.mArguments->size() != 0))
	{
		mModule->Fail(StrFormat("Only default parameterless constructors can be called on generic argument '%s'", genericParam->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}
	else if ((genericParamFlags & (BfGenericParamFlag_New | BfGenericParamFlag_Struct | BfGenericParamFlag_Var)) == 0)
	{
		mModule->Fail(StrFormat("Must add 'where %s : new, struct' constraint to generic parameter to instantiate type", genericParam->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}
	else if ((genericParamFlags & (BfGenericParamFlag_New | BfGenericParamFlag_Var)) == 0)
	{
		mModule->Fail(StrFormat("Must add 'where %s : new' constraint to generic parameter to instantiate type", genericParam->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}
	else if ((genericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_Var)) == 0)
	{
		mModule->Fail(StrFormat("Must add 'where %s : struct' constraint to generic parameter to instantiate type without allocator", genericParam->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}

	return success;
}

BfTypedValue BfExprEvaluator::MatchMethod(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, bool allowImplicitThis, bool bypassVirtual, const StringImpl& methodName,
	BfResolvedArgs& argValues, const BfMethodGenericArguments& methodGenericArgs, BfCheckedKind checkedKind)
{
	BP_ZONE("MatchMethod");

	auto methodGenericArguments = methodGenericArgs.mArguments;

	if (bypassVirtual)
	{
		// "bypassVirtual" means that we know for sure that the target is EXACTLY the specified target type,
		//  not derived from (or implementing) the target type.  This cannot apply to interfaces.
		BF_ASSERT(!target.mType->IsInterface());
	}

	auto origTarget = target;
	if (mFunctionBindResult != NULL)
	{
		BF_ASSERT(!mFunctionBindResult->mOrigTarget);
		mFunctionBindResult->mOrigTarget = origTarget;
	}

	if (target)
	{
		if (target.mType->IsConcreteInterfaceType())
			target.mType = target.mType->GetUnderlyingType();

		// Turn T* into a T, if we can
		if ((target.mType->IsPointer()) && (target.mType->GetUnderlyingType()->IsGenericParam()))
		{
			auto underlyingType = target.mType->GetUnderlyingType();
			auto genericParam = mModule->GetGenericParamInstance((BfGenericParamType*)underlyingType);
			if (((genericParam->mTypeConstraint != NULL) && (genericParam->mTypeConstraint->IsValueType())) ||
				((genericParam->mGenericParamFlags & (BfGenericParamFlag_Struct)) != 0))
			{
				target.mType = underlyingType;
			}
		}

		if ((!target.mType->IsGenericParam()) &&
			((!target.IsSplat()) || (target.mType->IsWrappableType())) &&
			(!IsVar(target.mType)))
			target = MakeCallableTarget(targetSrc, target);
	}

// 	static int sCallIdx = 0;
// 	if (!mModule->mCompiler->mIsResolveOnly)
// 		sCallIdx++;
// 	int callIdx = sCallIdx;
// 	if (callIdx == 118)
// 	{
// 		NOP;
// 	}

	bool prevAllowVariableDeclarations = true;
	if (mModule->mCurMethodState != NULL)
	{
		// Don't allow variable declarations in arguments for this method call
		prevAllowVariableDeclarations = mModule->mCurMethodState->mCurScope->mAllowVariableDeclarations;
		mModule->mCurMethodState->mCurScope->mAllowVariableDeclarations = false;
	}
	defer
	(
		if (mModule->mCurMethodState != NULL)
			mModule->mCurMethodState->mCurScope->mAllowVariableDeclarations = prevAllowVariableDeclarations;
	);

	// Temporarily disable so we don't capture calls in params
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);

	sInvocationIdx++;

	bool wantCtor = methodName.IsEmpty();

	BfAutoComplete::MethodMatchInfo* restoreCapturingMethodMatchInfo = NULL;

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		if ((!targetSrc->IsFromParser(mModule->mCompiler->mResolvePassData->mParsers[0])) ||
			((autoComplete->mMethodMatchInfo->mInvocationSrcIdx != -1) && (autoComplete->mMethodMatchInfo->mInvocationSrcIdx != targetSrc->GetSrcStart())))
		{
			autoComplete->mIsCapturingMethodMatchInfo = false;
			restoreCapturingMethodMatchInfo = autoComplete->mMethodMatchInfo;
		}
	}

	defer(
		{
			if ((restoreCapturingMethodMatchInfo != NULL) && (autoComplete->mMethodMatchInfo == restoreCapturingMethodMatchInfo))
				autoComplete->mIsCapturingMethodMatchInfo = true;
		});

	/*if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo) && (autoComplete->mMethodMatchInfo->mInstanceList.size() != 0))
		autoComplete->mIsCapturingMethodMatchInfo = false;*/

	bool isUnboundCall = false;
	if (target.mType != NULL)
	{
		if (target.mType->IsGenericParam())
		{
			auto genericParamTarget = (BfGenericParamType*) target.mType;
			auto genericParamInstance = mModule->GetGenericParamInstance(genericParamTarget);
			isUnboundCall = (genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Var) != 0;
			if (isUnboundCall)
			{
				if (mModule->mCurMethodInstance->mIsUnspecialized)
				{
					auto varType = mModule->GetPrimitiveType(BfTypeCode_Var);
					target.mType = varType;
				}
			}
		}
		else if (IsVar(target.mType))
			isUnboundCall = true;
	}

	/*if (mPrefixedAttributeState != NULL)
	{
		auto customAttr = mPrefixedAttributeState->mCustomAttributes->Get(mModule->mCompiler->mUnboundAttributeTypeDef);
		if (customAttr != NULL)
		{
			if (!mModule->IsInGeneric())
			{
				mModule->Fail("'Unbound' can only be used within generics");
			}

			mPrefixedAttributeState->mUsed = true;
			isUnboundCall = true;
		}
	}*/

	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
	{
		auto customAttr = mModule->mAttributeState->mCustomAttributes->Get(mModule->mCompiler->mUnboundAttributeTypeDef);
		if (customAttr != NULL)
		{
			if (!mModule->IsInGeneric())
			{
				mModule->Fail("'Unbound' can only be used within generics");
			}

			mModule->mAttributeState->mUsed = true;
			isUnboundCall = true;
		}
	}

	if (isUnboundCall)
	{
		if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized))
		{
			auto varType = mModule->GetPrimitiveType(BfTypeCode_Var);

			for (int argIdx = 0; argIdx < (int)argValues.mResolvedArgs.size(); argIdx++)
			{
				if ((argValues.mResolvedArgs[argIdx].mArgFlags & BfArgFlag_DeferredEval) != 0)
				{
					mModule->CreateValueFromExpression((*argValues.mArguments)[argIdx], varType);
				}
			}

			return BfTypedValue(mModule->GetDefaultValue(varType), varType);
		}
	}

	SetAndRestoreValue<bool> prevNoBind(mNoBind, mNoBind || isUnboundCall);

	bool wantsExtensionCheck = target;
	auto targetType = target.mType;
	BfTypeDef* curTypeDef = NULL;
	BfType* selfType = NULL;
	BfTypeInstance* targetTypeInst = NULL;
	bool checkNonStatic = true;

	if (target)
	{
		if (IsVar(targetType))
			return mModule->GetDefaultTypedValue(targetType);
		targetTypeInst = targetType->ToTypeInstance();
		if (targetTypeInst != NULL)
			curTypeDef = targetTypeInst->mTypeDef;
	}
	else if (targetType != NULL) // Static targeted
	{
		if (targetType->IsWrappableType())
		{
			if ((targetType->IsPrimitiveType()) && (methodName.IsEmpty()))
			{
				if (argValues.mArguments->IsEmpty())
				{
					return mModule->GetDefaultTypedValue(targetType);
				}
				else if (argValues.mArguments->mSize == 1)
				{
					FinishDeferredEvals(argValues);

					// This is just a primitive cast
					auto& resolvedArg = argValues.mResolvedArgs[0];
					BfTypedValue castedValue;
					BfTypedValue castTarget = resolvedArg.mTypedValue;
					if (resolvedArg.mTypedValue)
					{
						castTarget = mModule->LoadValue(castTarget);
						castedValue = mModule->Cast(targetSrc, castTarget, targetType, BfCastFlags_Explicit);
					}
					if (!castedValue)
						castedValue = mModule->GetDefaultTypedValue(targetType);
					return castedValue;
				}
			}
			targetTypeInst = mModule->GetWrappedStructType(targetType);
		}
		else if (targetType->IsGenericParam())
		{
			auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)targetType);
			if (genericParamInstance->mTypeConstraint != NULL)
				targetTypeInst = genericParamInstance->mTypeConstraint->ToTypeInstance();

			if (genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Var)
			{
				auto varType = mModule->GetPrimitiveType(BfTypeCode_Var);
				return BfTypedValue(mModule->GetDefaultValue(varType), varType);
			}
		}
		else
			targetTypeInst = targetType->ToTypeInstance();
			if (targetTypeInst == NULL)
			{
				//mModule->Fail("No static methods available", targetSrc);
				//return BfTypedValue();
			}
			else
			{
				curTypeDef = targetTypeInst->mTypeDef;
				checkNonStatic = false;
			}
	}
	else // Current scopeData
	{
		if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsForeignMethodDef) && (mModule->mCurMethodInstance->mMethodInfoEx->mForeignType->IsInterface()))
		{
			targetTypeInst = mModule->mCurMethodInstance->mMethodInfoEx->mForeignType;
			curTypeDef = targetTypeInst->mTypeDef;
			checkNonStatic = true;
			target = mModule->GetThis();
			selfType = mModule->mCurTypeInstance;
			//target.mType = targetTypeInst;
		}
		else
		{
			curTypeDef = mModule->mCurTypeInstance->mTypeDef;
			targetTypeInst = mModule->mCurTypeInstance;
			if (mModule->mCurMethodState == NULL)
			{
				checkNonStatic = false;
			}
			else
			{
				if (mModule->mCurMethodState->mMixinState != NULL)
				{
					targetTypeInst = mModule->mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
					curTypeDef = targetTypeInst->mTypeDef;
				}

				if (mModule->mCurMethodState->mTempKind != BfMethodState::TempKind_None)
				{
					checkNonStatic = mModule->mCurMethodState->mTempKind == BfMethodState::TempKind_NonStatic;
				}
				else
					checkNonStatic = !mModule->mCurMethodInstance->mMethodDef->mIsStatic;
			}
		}
	}

	bool isIndirectMethodCall = false;
	BfType* lookupType = targetType;
	BfTypeInstance* lookupTypeInst = targetTypeInst;
	if (targetType != NULL)
	{
		lookupType = BindGenericType(targetSrc, targetType);
		if (lookupType->IsGenericParam())
			lookupTypeInst = NULL;
	}

	if ((mModule->mIsReified) && (targetTypeInst != NULL) && (!targetTypeInst->mIsReified) && (!targetTypeInst->mModule->mReifyQueued))
		mModule->PopulateType(targetTypeInst);

	BfMethodDef* methodDef = NULL;
	BfTypeVector checkMethodGenericArguments;

	BfTypeInstance* curTypeInst = targetTypeInst;

	Array<SizedArray<BfUsingFieldData::MemberRef, 1>*> methodUsingLists;
	BfMethodMatcher methodMatcher(targetSrc, mModule, methodName, argValues.mResolvedArgs, methodGenericArgs);
	methodMatcher.mUsingLists = &methodUsingLists;
	methodMatcher.mOrigTarget = origTarget;
	methodMatcher.mTarget = target;
	methodMatcher.mCheckedKind = checkedKind;
	methodMatcher.mAllowImplicitThis = allowImplicitThis;
	methodMatcher.mAllowStatic = !target.mValue;
	methodMatcher.mAllowNonStatic = !methodMatcher.mAllowStatic;
	methodMatcher.mAutoFlushAmbiguityErrors = !wantsExtensionCheck;
	if (allowImplicitThis)
	{
		if (mModule->mCurMethodState == NULL)
		{
			methodMatcher.mAllowStatic = true;
			methodMatcher.mAllowNonStatic = false;
		}
		else if (mModule->mCurMethodState->mTempKind != BfMethodState::TempKind_None)
		{
			methodMatcher.mAllowNonStatic = mModule->mCurMethodState->mTempKind == BfMethodState::TempKind_NonStatic;
			methodMatcher.mAllowStatic = true;
		}
		else
		{
			if (!mModule->mCurMethodInstance->mMethodDef->mIsStatic)
				methodMatcher.mAllowNonStatic = true;
			methodMatcher.mAllowStatic = true;

			if (mModule->mCurMethodInstance->mMethodDef->mIsLocalMethod)
			{
				auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
				methodMatcher.mAllowNonStatic = !rootMethodState->mMethodInstance->mMethodDef->mIsStatic;
			}
		}
	}
	if (methodName == BF_METHODNAME_CALCAPPEND)
		methodMatcher.mMethodType = BfMethodType_CtorCalcAppend;

	BF_ASSERT(methodMatcher.mBestMethodDef == NULL);

	BfLocalMethod* matchedLocalMethod = NULL;
	if (target.mType == NULL)
	{
		CheckLocalMethods(targetSrc, curTypeInst, methodName, methodMatcher, BfMethodType_Normal);

		if (methodMatcher.mBestMethodDef == NULL)
			methodMatcher.mBestMethodDef = methodMatcher.mBackupMethodDef;

		if (methodMatcher.mBestMethodDef != NULL)
		{
			auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
			auto methodDef = methodMatcher.mBestMethodDef;
			if (mModule->mCompiler->mResolvePassData != NULL)
			{
				auto identifierNode = BfNodeDynCast<BfIdentifierNode>(targetSrc);
				mModule->mCompiler->mResolvePassData->HandleLocalReference(identifierNode, curTypeInst->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, ~methodDef->mIdx);
				auto autoComplete = GetAutoComplete();
				if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(identifierNode)))
				{
					autoComplete->SetDefinitionLocation(methodDef->GetRefNode(), true);
					if (autoComplete->mDefType == NULL)
					{
						autoComplete->mDefMethod = mModule->mCurMethodState->GetRootMethodState()->mMethodInstance->mMethodDef;
						autoComplete->mDefType = curTypeDef;
						autoComplete->mReplaceLocalId = ~methodDef->mIdx;
					}
				}

				if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
				{
					BfAutoComplete::MethodMatchEntry methodMatchEntry;
					methodMatchEntry.mMethodDef = methodDef;
					methodMatchEntry.mTypeInstance = mModule->mCurTypeInstance;
					methodMatchEntry.mCurMethodInstance = mModule->mCurMethodInstance;
					autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);
				}
			}

			if ((mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
			{
				auto methodInstance = mModule->GetRawMethodInstance(methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef);

				if (methodInstance->mReturnType == NULL)
				{
					FinishDeferredEvals(argValues);

					// If we are recursive then we won't even have a completed methodInstance yet to look at
					return mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
				}
			}
		}
	}

	if (methodMatcher.mBestMethodDef == NULL)
	{
		if (lookupTypeInst == NULL)
		{
			if ((lookupType != NULL) && (lookupType->IsConcreteInterfaceType()))
			{
				auto concreteInterfaceType = (BfConcreteInterfaceType*)lookupType;
				lookupTypeInst = concreteInterfaceType->mInterface;
			}
			else if (isUnboundCall)
			{
				//auto resolvedType = mModule->ResolveGenericType(lookupType);
			}
			else if (lookupType->IsGenericParam())
			{
				auto genericParamTarget = (BfGenericParamType*)lookupType;

				auto _HandleGenericParamInstance = [&](BfGenericParamInstance* genericParamInstance)
				{
					if (genericParamInstance->mTypeConstraint != NULL)
						lookupTypeInst = genericParamInstance->mTypeConstraint->ToTypeInstance();
					else
						lookupTypeInst = mModule->mContext->mBfObjectType;

					for (BfType* ifaceInst : genericParamInstance->mInterfaceConstraints)
					{
						BfTypeInstance* typeInst = ifaceInst->ToTypeInstance();
						BF_ASSERT(typeInst != NULL);
						if (methodMatcher.CheckType(typeInst, target, false))
							methodMatcher.mSelfType = lookupType;
					}
				};

				auto genericParamInstance = mModule->GetGenericParamInstance(genericParamTarget, true);
				_HandleGenericParamInstance(genericParamInstance);

				// Check method generic constraints
				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (int genericParamIdx = (int)mModule->mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
						genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
						if (genericParam->mExternType == lookupType)
							_HandleGenericParamInstance(genericParam);
					}
				}
			}
		}

		if ((lookupTypeInst != NULL) && (!wantCtor))
		{
			methodMatcher.mBypassVirtual = bypassVirtual;
			methodMatcher.CheckType(lookupTypeInst, target, false);
		}

		if ((lookupType != NULL) && (lookupType->IsGenericParam()))
		{
			//lookupType = mModule->ResolveGenericType(lookupType);
			if (!lookupType->IsGenericParam())
			{
				target = MakeCallableTarget(targetSrc, target);
				if (target)
				{
					lookupTypeInst = lookupType->ToTypeInstance();
				}
			}
		}
	}

	bool isFailurePass = false;
	if (methodMatcher.mBestMethodDef == NULL)
	{
		isFailurePass = true;
		if (lookupTypeInst != NULL)
			methodMatcher.CheckType(lookupTypeInst, target, true);
	}

	BfTypedValue staticResult;
	//
	{
		auto devirtTarget = target;
		if ((devirtTarget.mType == NULL) && (selfType != NULL))
			devirtTarget.mType = selfType;
		methodMatcher.TryDevirtualizeCall(devirtTarget, &origTarget, &staticResult);
	}
	if (staticResult)
		return staticResult;
	bypassVirtual |= methodMatcher.mBypassVirtual;

	if (methodMatcher.mBestMethodDef != NULL)
	{
		curTypeInst = methodMatcher.mBestMethodTypeInstance;
		methodDef = methodMatcher.mBestMethodDef;
	}

	if ((methodDef) && (!methodDef->mIsStatic) && (!target) && (allowImplicitThis))
	{
		target = mModule->GetThis();
	}

	if (!methodUsingLists.IsEmpty())
	{
		auto foundList = methodUsingLists[0];

		if (methodUsingLists.mSize > 1)
		{
			BfError* error = mModule->Fail("Ambiguous 'using' method reference", targetSrc);
			if (error != NULL)
			{
			 	for (auto checkList : methodUsingLists)
			 	{
			 		String errorStr = "'";
			 		for (int entryIdx = 0; entryIdx < checkList->mSize; entryIdx++)
			 		{
			 			if (entryIdx == 0)
			 				errorStr += (*checkList)[entryIdx].GetFullName(mModule);
			 			else
			 			{
			 				errorStr += ".";
			 				errorStr += (*checkList)[entryIdx].GetName(mModule);
			 			}
			 		}
			 		errorStr += "' is a candidate";
			 		mModule->mCompiler->mPassInstance->MoreInfo(errorStr, (*checkList)[0].GetRefNode(mModule));
			 	}
			}
		}

		BfTypedValue curResult = target;
		for (int entryIdx = 0; entryIdx < foundList->mSize; entryIdx++)
		{
			if ((entryIdx == 0) && (foundList->back().IsStatic()))
				entryIdx = (int)foundList->mSize - 1;

			auto& entry = (*foundList)[entryIdx];
			if (mPropDef != NULL)
			{
			 	SetAndRestoreValue<BfTypedValue> prevResult(mResult, BfTypedValue());
			 	mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_Friend);
			 	curResult = GetResult();
			 	if (!curResult)
			 		break;
			}

			auto useFlags = BfLookupFieldFlag_None;
			if (entryIdx < foundList->mSize - 1)
			 	useFlags = (BfLookupFieldFlags)(useFlags | BfLookupFieldFlag_IsAnonymous);

			if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Field)
			{
			 	curResult = LoadField(targetSrc, curResult, entry.mTypeInstance, entry.mTypeInstance->mTypeDef->mFields[entry.mIdx], useFlags);
			}
			else if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Property)
			{
			 	curResult = LoadProperty(targetSrc, curResult, entry.mTypeInstance, entry.mTypeInstance->mTypeDef->mProperties[entry.mIdx], useFlags, BfCheckedKind_NotSet, false);
			}
			else if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Local)
			{
			 	auto localDef = mModule->mCurMethodState->mLocals[entry.mIdx];
			 	curResult = LoadLocal(localDef);
			}
			else if (entry.mKind == BfUsingFieldData::MemberRef::Kind_Local)
			{
				auto checkMethodDef = entry.mTypeInstance->mTypeDef->mMethods[entry.mIdx];
				BF_ASSERT(methodDef == checkMethodDef);
				break;
			}

			if ((!curResult) && (mPropDef == NULL))
			 	break;

			if (entryIdx == foundList->mSize - 1)
			{
			 	auto autoComplete = GetAutoComplete();
			 	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(targetSrc)))
			 		autoComplete->SetDefinitionLocation(entry.GetRefNode(mModule));

			 	if ((autoComplete != NULL) && (autoComplete->CheckFixit(targetSrc)))
					autoComplete->FixitAddFullyQualify(targetSrc, methodName, *foundList);
			}
		}

		if (methodDef->mIsStatic)
			target = BfTypedValue(curTypeInst);
		else if (curResult)
			target = curResult;
		else if ((!methodDef->mIsStatic) && (curTypeInst != NULL))
			target = mModule->GetDefaultTypedValue(curTypeInst);
	}

	// If we call "GetType" on a value type, statically determine the type rather than boxing and then dispatching
	if ((methodDef) && (target) && (curTypeInst == mModule->mContext->mBfObjectType) &&
		(methodDef->mName == "GetType") && (target.mType->IsValueType()) && (argValues.mArguments->IsEmpty()))
	{
		BfType* targetType = target.mType;
		if (origTarget)
			targetType = origTarget.mType;

		auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);
		mModule->AddDependency(targetType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
		return BfTypedValue(mModule->CreateTypeDataRef(targetType), typeType);
	}

	bool skipThis = false;

	BfTypedValue fieldVal;
	bool hasFieldVal = false;

	// Fail, check for delegate field invocation
	if ((methodDef == NULL) && ((methodGenericArguments == NULL) || (methodGenericArguments->size() == 0)))
	{
		// Check for payload enum initialization first
		BfTypedValue enumResult;
		BfTypeInstance* enumType = NULL;
		if ((!target) && (target.HasType()) && (targetType->IsPayloadEnum()))
		{
			enumType = targetType->ToTypeInstance();
		}
		else if ((!target) && (!target.HasType()) && (mModule->mCurTypeInstance->IsPayloadEnum()))
		{
			enumType = mModule->mCurTypeInstance;
		}

		if (enumType != NULL)
		{
			enumResult = CheckEnumCreation(targetSrc, enumType, methodName, argValues);
		}

		if (enumResult)
		{
			if (mModule->mCompiler->mResolvePassData != NULL)
			{
				if (auto sourceClassifier = mModule->mCompiler->mResolvePassData->GetSourceClassifier(targetSrc))
				{
					sourceClassifier->SetElementType(targetSrc, BfSourceElementType_Normal);
				}
			}
			return enumResult;
		}

		if (allowImplicitThis)
		{
			auto identifierNode = BfNodeDynCast<BfIdentifierNode>(targetSrc);
			if (identifierNode != NULL)
				fieldVal = LookupIdentifier(identifierNode);
		}
		else
		{
			SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags & ~BfEvalExprFlags_AllowEnumId));
			fieldVal = LookupField(targetSrc, target, methodName);
		}

		if (fieldVal)
		{
			hasFieldVal = true;
			wantsExtensionCheck = !fieldVal.mType->IsDelegate() && !fieldVal.mType->IsFunction();
		}
		else if (mPropDef != NULL)
		{
			hasFieldVal = true;

			BfMethodDef* matchedMethod = GetPropertyMethodDef(mPropDef, BfMethodType_PropertyGetter, mPropCheckedKind, mPropTarget);
			if (matchedMethod != NULL)
			{
				auto getMethodInstance = mModule->GetRawMethodInstance(mPropTarget.mType->ToTypeInstance(), matchedMethod);
				if ((getMethodInstance != NULL) &&
					((getMethodInstance->mReturnType->IsDelegate()) || (getMethodInstance->mReturnType->IsFunction())))
					wantsExtensionCheck = false;
			}
		}
	}

	if ((!methodDef) && (!target.mValue))
	{
		// Check to see if we're constructing a struct via a call like: "Struct structVal = Struct()"
		int wantNumGenericArgs = 0;
		if ((methodGenericArguments != NULL) && (methodGenericArguments->size() > 0))
			wantNumGenericArgs = (int)methodGenericArguments->size();
		BfTypeInstance* resolvedTypeInstance = NULL;
		if (wantCtor)
		{
			resolvedTypeInstance = targetTypeInst;
		}
		else if (targetType != NULL)
		{
			if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(methodBoundExpr))
			{
				auto resolvedType = mModule->ResolveTypeRef(invocationExpr->mTarget, methodGenericArguments);

				if (resolvedType != NULL)
					resolvedTypeInstance = resolvedType->ToTypeInstance();
			}
		}
		else
		{
			BfIdentifierNode* identifierNode = BfNodeDynCast<BfIdentifierNode>(targetSrc);
			if (identifierNode != NULL)
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);

				BfType* refType;
				if (methodGenericArguments != NULL)
				{
					refType = mModule->ResolveTypeRef(identifierNode, methodGenericArguments);
				}
				else
					refType = mModule->ResolveTypeRef(identifierNode, NULL);
				prevIgnoreErrors.Restore();

				if ((refType != NULL) && (refType->IsPrimitiveType()))
				{
					FinishDeferredEvals(argValues);

					if (argValues.mResolvedArgs.IsEmpty())
						return mModule->GetDefaultTypedValue(refType);

					if (argValues.mResolvedArgs.size() != 1)
					{
						mModule->Fail("Cast requires one parameter", targetSrc);
						return BfTypedValue();
					}

					// This is just a primitive cast
					auto& resolvedArg = argValues.mResolvedArgs[0];
					BfTypedValue castedValue;
					BfTypedValue castTarget = resolvedArg.mTypedValue;
					if (resolvedArg.mTypedValue)
					{
						castTarget = mModule->LoadValue(castTarget);
						castedValue = mModule->Cast(targetSrc, castTarget, refType, BfCastFlags_Explicit);
					}
					if (!castedValue)
						castedValue = mModule->GetDefaultTypedValue(refType);
					return castedValue;
				}

				if (refType != NULL)
				{
					resolvedTypeInstance = refType->ToTypeInstance();

					if (refType->IsGenericParam())
					{
						CheckGenericCtor((BfGenericParamType*)refType, argValues, targetSrc);
						return mModule->GetDefaultTypedValue(refType);
					}
				}
			}
		}

		if (resolvedTypeInstance != NULL)
		{
			if ((mBfEvalExprFlags & BfEvalExprFlags_AppendFieldInitializer) == 0)
			{
				if ((!resolvedTypeInstance->IsStruct()) && (!resolvedTypeInstance->IsTypedPrimitive()))
				{
					if (mModule->PreFail())
						mModule->Fail("Objects must be allocated through 'new' or 'scope'", targetSrc);
					return BfTypedValue();
				}
			}

			if (auto identifier = BfNodeDynCastExact<BfIdentifierNode>(targetSrc))
			{
				auto elementType = resolvedTypeInstance->IsEnum() ? BfSourceElementType_Type : BfSourceElementType_Struct;
				if (resolvedTypeInstance->IsObject())
					elementType = BfSourceElementType_RefType;
				mModule->SetElementType(identifier, elementType);
			}

			if (mModule->mCompiler->mResolvePassData != NULL)
			{
				if (!BfNodeIsA<BfMemberReferenceExpression>(targetSrc))
					mModule->mCompiler->mResolvePassData->HandleTypeReference(targetSrc, resolvedTypeInstance->mTypeDef);
			}

			BfTypedValue structInst;
			mModule->PopulateType(resolvedTypeInstance);
			if (!resolvedTypeInstance->IsValuelessType())
			{
				if ((mReceivingValue != NULL) && (mReceivingValue->mType == resolvedTypeInstance) && (mReceivingValue->IsAddr()))
				{
					structInst = *mReceivingValue;
					mReceivingValue = NULL;
				}
				else
				{
					auto allocaInst = mModule->CreateAlloca(resolvedTypeInstance);
					structInst = BfTypedValue(allocaInst, resolvedTypeInstance, true);
				}
				mResultIsTempComposite = true;
			}
			else
			{
				structInst = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), resolvedTypeInstance, true);
			}

			mResultLocalVar = NULL;
			mResultFieldInstance = NULL;
			mResultLocalVarRefNode = NULL;
			auto result = MatchConstructor(targetSrc, methodBoundExpr, structInst, resolvedTypeInstance, argValues, false, resolvedTypeInstance->IsObject());
			if ((result) && (!result.mType->IsVoid()))
				return result;
			mModule->ValidateAllocation(resolvedTypeInstance, targetSrc);

			mModule->AddDependency(resolvedTypeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

			if (mUsedAsStatement)
			{
				mModule->Warn(0, "Struct constructor being used as a statement", targetSrc);
			}

			return structInst;
		}
	}

	// For for extensions in current type
	if (wantsExtensionCheck)
	{
		auto checkTypeInst = mModule->mCurTypeInstance;
		methodMatcher.mMethodType = BfMethodType_Extension;
		if (methodMatcher.CheckType(checkTypeInst, BfTypedValue(), false, true))
		{
			isFailurePass = false;
			curTypeInst = methodMatcher.mBestMethodTypeInstance;
			methodDef = methodMatcher.mBestMethodDef;
		}
	}

	// Look in globals. Always check for extension methods.
	if ((((methodDef == NULL) && (!target.HasType())) ||
		 (wantsExtensionCheck)))
	{
		if (mModule->mContext->mCurTypeState != NULL)
		{
			BfGlobalLookup globalLookup;
			globalLookup.mKind = BfGlobalLookup::Kind_Method;
			globalLookup.mName = methodName;
			mModule->PopulateGlobalContainersList(globalLookup);
			for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
			{
				if (globalContainer.mTypeInst == NULL)
					continue;
				methodMatcher.mMethodType = wantsExtensionCheck ? BfMethodType_Extension : BfMethodType_Normal;
				if (methodMatcher.CheckType(globalContainer.mTypeInst, BfTypedValue(), false))
				{
					isFailurePass = false;
					curTypeInst = methodMatcher.mBestMethodTypeInstance;
					methodDef = methodMatcher.mBestMethodDef;
				}
			}
		}
	}

	// Look in static search. Always check for extension methods.
	if ((methodDef == NULL) || (wantsExtensionCheck))
	{
		BfStaticSearch* staticSearch = mModule->GetStaticSearch();
		if (staticSearch != NULL)
		{
			for (auto typeInst : staticSearch->mStaticTypes)
			{
				methodMatcher.mMethodType = wantsExtensionCheck ? BfMethodType_Extension : BfMethodType_Normal;
				if (methodMatcher.CheckType(typeInst, BfTypedValue(), false))
				{
					isFailurePass = false;
					curTypeInst = methodMatcher.mBestMethodTypeInstance;
					methodDef = methodMatcher.mBestMethodDef;
				}
			}
		}
	}

	if ((methodDef == NULL) && (hasFieldVal))
	{
		if (mPropDef != NULL)
			fieldVal = GetResult();

		if (fieldVal)
		{
			if (fieldVal.mType->IsGenericParam())
			{
				bool delegateFailed = true;
				auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)fieldVal.mType);
				BfTypeInstance* typeInstConstraint = NULL;
				if (genericParamInstance->mTypeConstraint != NULL)
					typeInstConstraint = genericParamInstance->mTypeConstraint->ToTypeInstance();
				if ((typeInstConstraint != NULL) &&
					((typeInstConstraint->IsInstanceOf(mModule->mCompiler->mDelegateTypeDef)) || (typeInstConstraint->IsInstanceOf(mModule->mCompiler->mFunctionTypeDef))))
				{
					MarkResultUsed();

					if (argValues.mArguments->size() == 1)
					{
						BfExprEvaluator exprEvaluator(mModule);
						exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_AllowParamsExpr;
						auto argExpr = (*argValues.mArguments)[0];
						if (argExpr != NULL)
							exprEvaluator.Evaluate(argExpr);
						if ((mModule->mCurMethodState != NULL) && (exprEvaluator.mResultLocalVar != NULL) && (exprEvaluator.mResultLocalVarRefNode != NULL))
						{
							auto localVar = exprEvaluator.mResultLocalVar;
							if ((localVar->mCompositeCount >= 0) && (localVar->mResolvedType == fieldVal.mType))
							{
								delegateFailed = false;
								if (mModule->mCurMethodInstance->mIsUnspecialized)
								{
									auto retTypeType = mModule->CreateModifiedTypeType(fieldVal.mType, BfToken_RetType);
									return mModule->GetFakeTypedValue(retTypeType);
								}
							}
						}
					}

					if (delegateFailed)
					{
						mModule->Fail(StrFormat("Generic delegates can only be invoked with 'params %s' composite parameters", mModule->TypeToString(fieldVal.mType).c_str()), targetSrc);
						return BfTypedValue();
					}
				}
			}

			if (fieldVal.mType->IsTypeInstance())
			{
				prevBindResult.Restore();
				auto fieldTypeInst = fieldVal.mType->ToTypeInstance();
				MarkResultUsed();
				if (mFunctionBindResult != NULL)
				{
					mFunctionBindResult->mOrigTarget = BfTypedValue();
				}
				return MatchMethod(targetSrc, NULL, fieldVal, false, false, "Invoke", argValues, methodGenericArgs, checkedKind);
			}
			if (IsVar(fieldVal.mType))
			{
				FinishDeferredEvals(argValues);
				return BfTypedValue(mModule->GetDefaultValue(fieldVal.mType), fieldVal.mType);
			}
			if (fieldVal.mType->IsGenericParam())
			{
				auto genericParam = mModule->GetGenericParamInstance((BfGenericParamType*)fieldVal.mType);
				BfType* typeConstraint = genericParam->mTypeConstraint;

				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (int genericParamIdx = (int)mModule->mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
						genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
						if ((genericParam->mExternType == fieldVal.mType) && (genericParam->mTypeConstraint != NULL))
							typeConstraint = genericParam->mTypeConstraint;
					}
				}

				if ((typeConstraint != NULL) &&
					((typeConstraint->IsDelegate()) || (typeConstraint->IsFunction())))
				{
					BfMethodInstance* invokeMethodInstance = mModule->GetRawMethodInstanceAtIdx(typeConstraint->ToTypeInstance(), 0, "Invoke");

					methodDef = invokeMethodInstance->mMethodDef;
					methodMatcher.mBestMethodInstance = invokeMethodInstance;
					methodMatcher.mBestMethodTypeInstance = invokeMethodInstance->GetOwner();
					methodMatcher.mBestMethodDef = invokeMethodInstance->mMethodDef;
					target = mModule->GetDefaultTypedValue(methodMatcher.mBestMethodTypeInstance);
					isFailurePass = false;
					isIndirectMethodCall = true;
				}
			}
			else if (fieldVal.mType->IsMethodRef())
			{
				auto functionBindResults = prevBindResult.mPrevVal;
				if (functionBindResults != NULL)
				{
					functionBindResults->mOrigTarget = fieldVal;
				}
				origTarget = fieldVal;

				auto methodRefType = (BfMethodRefType*)fieldVal.mType;
				BfMethodInstance* methodInstance = methodRefType->mMethodRef;
				methodDef = methodInstance->mMethodDef;
				if (methodDef->mIsLocalMethod)
				{
					methodMatcher.mBestMethodInstance = mModule->ReferenceExternalMethodInstance(methodInstance);
				}
				else
				{
					BfTypeVector methodGenericArguments;
					if (methodInstance->mMethodInfoEx != NULL)
						methodGenericArguments = methodInstance->mMethodInfoEx->mMethodGenericArguments;
					methodMatcher.mBestMethodInstance = mModule->GetMethodInstance(methodInstance->GetOwner(), methodInstance->mMethodDef, methodGenericArguments);
				}
				methodMatcher.mBestMethodTypeInstance = methodInstance->GetOwner();
				if (methodInstance->HasThis())
				{
					bool failed = false;
					target = DoImplicitArgCapture(targetSrc, methodInstance, -1, failed, BfImplicitParamKind_General, origTarget);
				}
				else if (!methodDef->mIsStatic)
				{
					auto thisType = methodInstance->GetParamType(-1);
					BF_ASSERT(thisType->IsValuelessType());
					target = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), methodMatcher.mBestMethodTypeInstance);
				}
				else
					target = BfTypedValue(methodMatcher.mBestMethodTypeInstance);
				methodMatcher.mBypassVirtual = true;
				bypassVirtual = true;
				isFailurePass = false;
				isIndirectMethodCall = true;
			}

			if (methodDef == NULL)
			{
				mModule->Fail(StrFormat("Cannot perform invocation on type '%s'", mModule->TypeToString(fieldVal.mType).c_str()), targetSrc);
				return BfTypedValue();
			}
		}
	}

	if (methodDef == NULL)
	{
	}

	// This will flush out any new ambiguity errors from extension methods
	methodMatcher.FlushAmbiguityError();

	if (methodDef == NULL)
	{
		FinishDeferredEvals(argValues);
		auto compiler = mModule->mCompiler;
		if ((autoComplete != NULL) && (autoComplete->CheckFixit(targetSrc)))
		{
			mModule->CheckTypeRefFixit(targetSrc);
			bool wantStatic = !target.mValue;
			if ((targetType == NULL) && (allowImplicitThis))
			{
				targetType = mModule->mCurTypeInstance;
				if (mModule->mCurMethodInstance != NULL)
					wantStatic = mModule->mCurMethodInstance->mMethodDef->mIsStatic;
			}

			if (targetType != NULL)
			{
				auto typeInst = targetType->ToTypeInstance();
				if ((typeInst != NULL) && (!methodName.IsEmpty()))
				{
					BfTypeVector paramTypes;
					for (int argIdx = 0; argIdx < (int)argValues.mResolvedArgs.size(); argIdx++)
					{
						auto& resolvedArg = argValues.mResolvedArgs[argIdx];
						paramTypes.Add(resolvedArg.mTypedValue.mType);
					}

					autoComplete->FixitAddMethod(typeInst, methodName, mExpectingType, paramTypes, wantStatic);
				}
			}
		}

		if (methodName.IsEmpty())
		{
			// Would have caused a parsing error
		}
		else if (target.mType != NULL)
		{
			if (mModule->PreFail())
			{
			 	if ((origTarget.mType->IsPointer()) && (origTarget.mType->GetUnderlyingType()->IsObjectOrInterface()))
			 	{
			 		mModule->Fail(StrFormat("Methods cannot be called on type '%s' because the type is a pointer to a reference type (ie: a double-reference).",
			 			mModule->TypeToString(origTarget.mType).c_str()), targetSrc);
			 	}
				else
					mModule->Fail(StrFormat("Method '%s' does not exist in type '%s'", methodName.c_str(), mModule->TypeToString(target.mType).c_str()), targetSrc);
			}
		}
		else
		{
			if (mModule->PreFail())
				mModule->Fail(StrFormat("Method '%s' does not exist", methodName.c_str()), targetSrc);
		}
		return BfTypedValue();
	}

	if ((prevBindResult.mPrevVal != NULL) && (methodMatcher.mMethodCheckCount > 1))
		prevBindResult.mPrevVal->mCheckedMultipleMethods = true;

	BfType* overrideReturnType = NULL;
	BfModuleMethodInstance moduleMethodInstance = GetSelectedMethod(targetSrc, curTypeInst, methodDef, methodMatcher, &overrideReturnType);

	if ((mModule->mAttributeState != NULL) && ((mModule->mAttributeState->mFlags & (BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)) ==
		(BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)))
	{
		FinishDeferredEvals(argValues);
		return BfTypedValue();
	}

	if ((moduleMethodInstance.mMethodInstance != NULL) && (!mModule->CheckUseMethodInstance(moduleMethodInstance.mMethodInstance, targetSrc)))
	{
		FinishDeferredEvals(argValues);
		return BfTypedValue();
	}

	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized))
	{
		if (methodMatcher.mHasVarArguments)
		{
			BfType* retType = mModule->GetPrimitiveType(BfTypeCode_Var);

			if ((!methodMatcher.mHadVarConflictingReturnType) && (methodMatcher.mBestRawMethodInstance != NULL) && (!methodMatcher.mBestRawMethodInstance->mReturnType->IsUnspecializedTypeVariation()) &&
				(prevBindResult.mPrevVal == NULL))
			{
				if ((!methodMatcher.mBestRawMethodInstance->mReturnType->IsGenericParam()) ||
					(((BfGenericParamType*)methodMatcher.mBestRawMethodInstance->mReturnType)->mGenericParamKind != BfGenericParamKind_Method))
					retType = methodMatcher.mBestRawMethodInstance->mReturnType;
			}

			return mModule->GetDefaultTypedValue(retType, true, BfDefaultValueKind_Addr);
		}
	}

	if ((bypassVirtual) && (!target.mValue) && (target.mType->IsInterface()))
	{
		target = mModule->GetThis();
	}
	if (!moduleMethodInstance)
	{
		FinishDeferredEvals(argValues);
		if (mModule->IsInUnspecializedGeneric())
			return mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		return BfTypedValue();
	}

	bool isSkipCall = moduleMethodInstance.mMethodInstance->IsSkipCall(bypassVirtual);

	if ((moduleMethodInstance.mMethodInstance->IsOrInUnspecializedVariation()) && (!mModule->mBfIRBuilder->mIgnoreWrites))
	{
		// Invalid methods such as types with a HasVar tuple generic arg will be marked as mIsUnspecializedVariation and shouldn't actually be called
		FinishDeferredEvals(argValues);
		return mModule->GetDefaultTypedValue(moduleMethodInstance.mMethodInstance->mReturnType, true, BfDefaultValueKind_Addr);
	}

	if (methodDef->IsEmptyPartial())
	{
		// Ignore call
		return mModule->GetDefaultTypedValue(moduleMethodInstance.mMethodInstance->mReturnType, true, BfDefaultValueKind_Addr);
	}

	if ((moduleMethodInstance.mMethodInstance->mMethodDef->mIsStatic) && (moduleMethodInstance.mMethodInstance->GetOwner()->IsInterface()))
	{
		bool isConstrained = false;
		if (target.mType != NULL)
			isConstrained = target.mType->IsGenericParam();
		if ((target.mType == NULL) && ((mModule->mCurMethodInstance->mIsForeignMethodDef) || (mModule->mCurTypeInstance->IsInterface())))
			isConstrained = true;

		// If mIgnoreWrites is off then we skip devirtualization, so allow this
		if ((target.mType != NULL) && (!target.mType->IsInterface()) && (mModule->mBfIRBuilder->mIgnoreWrites))
			isConstrained = true;

		if (!isConstrained)
		{
			if (mModule->mCurTypeInstance->IsInterface())
			{
				if (methodDef->mBody == NULL)
					mModule->Fail(StrFormat("Interface method '%s' must provide a body to be explicitly invoked", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
			}
			else
				mModule->Fail(StrFormat("Static interface method '%s' can only be dispatched from a concrete type, consider using this interface as a generic constraint", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
			FinishDeferredEvals(argValues);
			return mModule->GetDefaultTypedValue(moduleMethodInstance.mMethodInstance->mReturnType, true, BfDefaultValueKind_Addr);
		}
	}

	// 'base' could really mean 'this' if we're in an extension
	if ((target.IsBase()) && (targetTypeInst == mModule->mCurTypeInstance))
		target.ToThis();
	else
		MakeBaseConcrete(target);

	BfType* callTargetType = curTypeInst;
	if (methodDef->mMethodType == BfMethodType_Extension)
	{
		callTargetType = moduleMethodInstance.mMethodInstance->GetParamType(0);
		if ((callTargetType->IsRef()) && (target.IsAddr()) && (!target.IsReadOnly()) && (target.mType->IsValueType()))
		{
			target = BfTypedValue(target.mValue, mModule->CreateRefType(target.mType));
		}
	}

	BfTypedValue callTarget;
	if (isSkipCall)
	{
		// Just a fake value so we can continue on without generating any code (boxing, conversion, etc)
		if (target)
		{
			if (((target.mType->IsComposite()) || (target.mType->IsTypedPrimitive())))
			{
				if ((moduleMethodInstance.mMethodInstance->mMethodDef->mIsMutating) &&
					((target.IsReadOnly()) || (!target.IsAddr())))
				{
					String err = StrFormat("call mutating method '%s' on", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str());
					CheckModifyResult(target, targetSrc, err.c_str());
				}
			}

			callTarget = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), targetTypeInst);
		}
	}
	else if (targetTypeInst == callTargetType)
	{
		if ((target) && (methodDef->HasNoThisSplat()))
		{
			callTarget = mModule->MakeAddressable(target);
		}
		else
			callTarget = target;

		if ((callTarget) && (moduleMethodInstance.mMethodInstance->GetOwner()->IsInterface()))
		{
			auto wantThis = moduleMethodInstance.mMethodInstance->GetParamType(-1);
			if ((callTarget.mType != wantThis) && (wantThis->IsInterface()))
				callTarget = mModule->Cast(targetSrc, callTarget, wantThis, BfCastFlags_Explicit);
		}
	}
	else if (target)
	{
		if (methodMatcher.mFakeConcreteTarget)
		{
			BF_ASSERT(callTargetType->IsInterface());
			callTarget = mModule->GetDefaultTypedValue(mModule->CreateConcreteInterfaceType(callTargetType->ToTypeInstance()));
		}
		else if (((target.mType->IsGenericParam()) || (target.mType->IsConcreteInterfaceType())) && (callTargetType->IsInterface()))
		{
			// Leave as generic
			callTarget = target;
		}
		else
		{
			bool handled = false;

			if ((target.mType->IsTypedPrimitive()) && (callTargetType->IsTypedPrimitive()))
			{
				handled = true;
				callTarget = target;
			}
			else if ((target.mType->IsStructOrStructPtr()) || (target.mType->IsTypedPrimitive()))
			{
				//BF_ASSERT(target.IsAddr());
				if (callTargetType->IsObjectOrInterface())
				{
					// Box it
					callTarget = mModule->Cast(targetSrc, target, callTargetType, BfCastFlags_Explicit);
					handled = true;
				}
				else
				{
					//BF_ASSERT(target.IsAddr() || target.IsSplat() || target.mType->IsPointer());
				}
			}
			else
				target = mModule->LoadValue(target);

			if (!handled)
			{
				// Could we have just unconditionally done this?
				callTarget = mModule->Cast(targetSrc, target, callTargetType, BfCastFlags_Explicit);
			}
		}
	}

	if (isFailurePass)
	{
		BfError* error;
		if (methodMatcher.mMatchFailKind == BfMethodMatcher::MatchFailKind_CheckedMismatch)
			error = mModule->Fail(StrFormat("'%s' cannot be used because its 'checked' specifier does not match the requested specifier", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
		else
			error = mModule->Fail(StrFormat("'%s' is inaccessible due to its protection level", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);
		if (error != NULL)
		{
			if ((error != NULL) && (moduleMethodInstance.mMethodInstance->mMethodDef->mMethodDeclaration != NULL))
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), moduleMethodInstance.mMethodInstance->mMethodDef->GetRefNode());
		}
	}

	if ((mModule->mCompiler->mResolvePassData != NULL) && (methodDef != NULL))
	{
		auto identifierNode = BfNodeDynCast<BfIdentifierNode>(targetSrc);
		while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(identifierNode))
			identifierNode = qualifiedNameNode->mRight;
		if ((identifierNode != NULL) && (methodDef->mIdx >= 0) && (!isIndirectMethodCall) &&
			((targetTypeInst == NULL) || (!targetTypeInst->IsDelegateOrFunction())))
		{
			auto refMethodInstance = moduleMethodInstance.mMethodInstance;
			if (refMethodInstance->mVirtualTableIdx != -1)
			{
				auto& virtualEntry = refMethodInstance->GetOwner()->mVirtualMethodTable[refMethodInstance->mVirtualTableIdx];
				refMethodInstance = virtualEntry.mDeclaringMethod;
			}

			mModule->mCompiler->mResolvePassData->HandleMethodReference(identifierNode, refMethodInstance->GetOwner()->mTypeDef, refMethodInstance->mMethodDef);
			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(identifierNode)))
			{
				autoComplete->SetDefinitionLocation(methodDef->GetRefNode(), true);

				int virtualIdx = moduleMethodInstance.mMethodInstance->mVirtualTableIdx;
				if ((autoComplete->mResolveType == BfResolveType_GoToDefinition) &&
					(virtualIdx != -1) && (targetTypeInst != NULL) && (!targetTypeInst->IsStruct()) && (!targetTypeInst->IsTypedPrimitive()) && (!targetTypeInst->IsInterface()) &&
					// VirtualMethodTable can be empty if the non-autocomplete classifier hasn't completed yet.  Allow failure, a PopulateType here can't force the method table to fill out
					(targetTypeInst->mVirtualMethodTable.size() != 0))
				{
					auto methodEntry = targetTypeInst->mVirtualMethodTable[virtualIdx];
					if (methodEntry.mImplementingMethod.mMethodNum != -1)
					{
						BfMethodInstance* callingMethodInstance = methodEntry.mImplementingMethod;
						if (callingMethodInstance != NULL)
						{
							auto callingMethodDef = callingMethodInstance->mMethodDef;
							auto callingMethodDeclaration = callingMethodDef->GetMethodDeclaration();
							if ((callingMethodDeclaration != NULL) && (callingMethodDeclaration->mNameNode != NULL))
								autoComplete->SetDefinitionLocation(callingMethodDeclaration->mNameNode, true);
						}
					}
				}

				if (autoComplete->mDefType == NULL)
				{
					autoComplete->mDefMethod = refMethodInstance->mMethodDef;
					autoComplete->mDefType = refMethodInstance->GetOwner()->mTypeDef;
				}

				if (autoComplete->mResolveType == BfResolveType_GetResultString)
				{
					autoComplete->mResultString = ":";
					autoComplete->mResultString += mModule->MethodToString(moduleMethodInstance.mMethodInstance);

					auto methodDecl = moduleMethodInstance.mMethodInstance->mMethodDef->GetMethodDeclaration();
					if ((methodDecl != NULL) && (methodDecl->mDocumentation != NULL))
					{
						autoComplete->mResultString += "\x03";
						methodDecl->mDocumentation->GetDocString(autoComplete->mResultString);
					}
				}
			}
		}
	}

	// This was causing forward-backward-forward steps when we just used 'targetSrc'.  Using closeParen is undesirable because most of the time
	//  we DO just want it to just go to the targetSrc...  do we even need this?
	/*if (auto invokeExpr = BfNodeDynCast<BfInvocationExpression>(targetSrc->mParent))
	{
		// We set the srcPos to the close paren right before the call so we keep a forward progression of src positions in the case
		//  where some of the params are method calls and such
		if (invokeExpr->mCloseParen != NULL)
			mModule->UpdateExprSrcPos(invokeExpr->mCloseParen);
		else
			mModule->UpdateExprSrcPos(targetSrc);
	}
	else
		mModule->UpdateExprSrcPos(targetSrc);*/

	if (!mModule->mBfIRBuilder->mIgnoreWrites)
	{
		//BF_ASSERT(!callTarget.mValue.IsFake());
	}

	prevBindResult.Restore();

	// Check mut
	if ((callTarget.mType != NULL) &&
		(callTarget.mType->IsGenericParam()) &&
		((!callTarget.IsAddr()) || (callTarget.IsReadOnly())) &&
		(callTarget.mKind != BfTypedValueKind_MutableValue) &&
		(moduleMethodInstance.mMethodInstance->mMethodDef->mIsMutating))
	{
		auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)callTarget.mType);

		bool needsMut = true;
		if ((genericParamInstance->mGenericParamFlags & (BfGenericParamFlag_StructPtr | BfGenericParamFlag_Class | BfGenericParamFlag_Var)) != 0)
			needsMut = false;
		if (genericParamInstance->mTypeConstraint != NULL)
		{
			auto typeConstaintTypeInstance = genericParamInstance->mTypeConstraint->ToTypeInstance();
			if ((typeConstaintTypeInstance != NULL) && (!typeConstaintTypeInstance->IsComposite()))
				needsMut = false;
		}

		if ((mFunctionBindResult != NULL) && (mFunctionBindResult->mSkipMutCheck))
			needsMut = false;

		if (needsMut)
		{
			String err = StrFormat("call mutating method '%s' on", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str());
			CheckModifyResult(callTarget, targetSrc, err.c_str(), true);
		}
	}

	// Check mut on interface
	if ((callTarget.mType != NULL) &&
		(callTarget.mType->IsInterface()) &&
		(target.IsThis()) &&
		(mModule->mCurTypeInstance) &&
		(!mModule->mCurMethodInstance->mMethodDef->mIsMutating) &&
		(methodDef->mIsMutating))
	{
		mModule->Fail(StrFormat("Cannot call mutating method '%s' within default interface method '%s'. Consider adding 'mut' specifier to this method.",
			mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str(), mModule->MethodToString(mModule->mCurMethodInstance).c_str()), targetSrc);
	}

	BfTypedValue result;
	BfTypedValue argCascade;

	BfCreateCallFlags subCallFlags = BfCreateCallFlags_None;
	if (bypassVirtual)
		subCallFlags = (BfCreateCallFlags)(subCallFlags | BfCreateCallFlags_BypassVirtual);
	if (skipThis)
		subCallFlags = (BfCreateCallFlags)(subCallFlags | BfCreateCallFlags_SkipThis);
	result = CreateCall(targetSrc, callTarget, origTarget, methodDef, moduleMethodInstance, subCallFlags, argValues.mResolvedArgs, &argCascade);

	if (overrideReturnType != NULL)
	{
		BF_ASSERT(moduleMethodInstance.mMethodInstance->mIsUnspecializedVariation);
		result = mModule->GetDefaultTypedValue(overrideReturnType, false, BfDefaultValueKind_Addr);
	}

	if (result)
	{
		if (result.mType->IsRef())
			result = mModule->RemoveRef(result);
	}

	PerformCallChecks(moduleMethodInstance.mMethodInstance, targetSrc);

	if (result)
	{
		bool discardedReturnValue = mUsedAsStatement;
		if (discardedReturnValue)
		{
			auto _ShowDiscardWaring = [&](const String& text, BfCustomAttributes* customAttributes, BfIRConstHolder* constHolder, BfAstNode* refNode)
			{
				if (customAttributes != NULL)
				{
					auto customAttribute = customAttributes->Get(mModule->mCompiler->mNoDiscardAttributeTypeDef);
					if (!customAttribute->mCtorArgs.IsEmpty())
					{
						String* str = mModule->GetStringPoolString(customAttribute->mCtorArgs[0], constHolder);
						if ((str != NULL) && (!str->IsEmpty()))
						{
							mModule->Warn(0, text + ": " + *str, targetSrc);
							return;
						}
					}
				}

				mModule->Warn(0, text, targetSrc);
			};

			bool showedWarning = false;
			if (moduleMethodInstance.mMethodInstance->mMethodDef->mIsNoDiscard)
			{
				_ShowDiscardWaring("Discarding return value of method with [NoDiscard] attribute", moduleMethodInstance.mMethodInstance->GetCustomAttributes(),
					moduleMethodInstance.mMethodInstance->GetOwner()->mConstHolder, targetSrc);
				showedWarning = true;
			}

			auto typeInst = result.mType->ToTypeInstance();
			if (typeInst != NULL)
			{
				if ((typeInst->mTypeDef->mIsNoDiscard) && (!showedWarning))
				{
					_ShowDiscardWaring("Discarding return value whose type has [NoDiscard] attribute", typeInst->mCustomAttributes, typeInst->mConstHolder, targetSrc);
				}

				BfModuleMethodInstance moduleMethodInstance = mModule->GetMethodByName(typeInst, "ReturnValueDiscarded", 0, true);
				if (moduleMethodInstance)
				{
					bool wasCapturingMethodMatchInfo = false;
					if (autoComplete != NULL)
					{
						wasCapturingMethodMatchInfo = autoComplete->mIsCapturingMethodMatchInfo;
						autoComplete->mIsCapturingMethodMatchInfo = false;
					}

					defer
					(
						if (autoComplete != NULL)
							autoComplete->mIsCapturingMethodMatchInfo = wasCapturingMethodMatchInfo;
					);

					auto wasGetDefinition = (autoComplete != NULL) && (autoComplete->mIsGetDefinition);
					if (wasGetDefinition)
						autoComplete->mIsGetDefinition = false;
					result = mModule->MakeAddressable(result);
					BfResolvedArgs resolvedArgs;
					MatchMethod(targetSrc, NULL, result, false, false, "ReturnValueDiscarded", resolvedArgs, BfMethodGenericArguments());
					if (wasGetDefinition)
						autoComplete->mIsGetDefinition = true;
				}
			}
		}
	}

	if (argCascade)
	{
		if (argCascade.mType->IsRef())
			argCascade = mModule->RemoveRef(argCascade);
		result = argCascade;
	}

	if (result)
	{
		if ((result.mType->IsUnspecializedTypeVariation()) && (moduleMethodInstance.mMethodInstance->GetOwner()->IsInterface()))
		{
			BfType* selfType = NULL;

			if (methodMatcher.mSelfType != NULL)
			{
				BF_ASSERT(mModule->IsInGeneric());
				selfType = methodMatcher.mSelfType;
			}
			else
			{
				// Will be an error
				selfType = methodMatcher.mBestMethodTypeInstance;
			}

			if ((selfType != NULL) && (!selfType->IsInterface()))
			{
				auto resolvedType = mModule->ResolveSelfType(result.mType, selfType);
				if ((resolvedType != NULL) && (resolvedType != result.mType))
					result = mModule->GetDefaultTypedValue(resolvedType);
			}
		}
	}

	return result;
}

void BfExprEvaluator::LookupQualifiedName(BfQualifiedNameNode* nameNode, bool ignoreInitialError, bool* hadError)
{
	BfIdentifierNode* nameLeft = nameNode->mLeft;
	BfIdentifierNode* nameRight = nameNode->mRight;

	StringT<64> fieldName;
	if (nameNode->mRight != NULL)
		nameNode->mRight->ToString(fieldName);

	bool wasBaseLookup = false;
	if (auto qualifiedLeftName = BfNodeDynCast<BfQualifiedNameNode>(nameNode->mLeft))
	{
		if (CheckIsBase(qualifiedLeftName->mRight))
		{
			wasBaseLookup = true;
			auto type = mModule->ResolveTypeRef(qualifiedLeftName->mLeft, NULL);
			if (type == NULL)
				return;

			auto fieldName = nameNode->mRight->ToString();
			auto target = mModule->GetThis();
			target.mType = type;
			mResult = LookupField(nameNode->mRight, target, fieldName);
			if ((mPropDef != NULL) && (mPropDef->IsVirtual()))
				mPropDefBypassVirtual = true;
			return;
		}
	}

	if (!wasBaseLookup)
		mResult = LookupIdentifier(nameNode->mLeft, ignoreInitialError, hadError);
	GetResult();

	if (!mResult)
	{
		if (!ignoreInitialError)
			mModule->Fail("Identifier not found", nameNode->mLeft);
		return;
	}

	if (mResult.mType->IsObject())
	{
		mResult = mModule->LoadValue(mResult, 0, mIsVolatileReference);
	}
	else if ((mResult.mType->IsPointer()) && mResult.mType->IsStructOrStructPtr())
	{
		BfPointerType* structPtrType = (BfPointerType*)mResult.mType;
		mResult = mModule->LoadValue(mResult, 0, mIsVolatileReference);
		mResult.mType = structPtrType->mElementType;
		if (mResult.mKind == BfTypedValueKind_ThisValue)
			mResult.mKind = BfTypedValueKind_ThisAddr;
		else
			mResult.mKind = BfTypedValueKind_Addr;
	}
	mIsVolatileReference = false;
	mIsHeapReference = false;

	if (!mResult)
		return;

	auto origResult = mResult;
	auto lookupType = BindGenericType(nameNode, mResult.mType);

	if (IsVar(mResult.mType))
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(mResult.mType), mResult.mType, true);
		return;
	}

	if ((!mResult.mType->IsTypeInstance()) && (!mResult.mType->IsGenericParam()))
	{
		if (hadError != NULL)
			*hadError = true;
		mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameNode->mLeft);
		mResult = BfTypedValue();
		return;
	}

	BfTypedValue lookupVal = mResult;
	mResult = LookupField(nameNode->mRight, lookupVal, fieldName);
	if ((!mResult) && (mPropDef == NULL) && (lookupType->IsGenericParam()))
	{
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)lookupType, true);
		SizedArray<BfTypeInstance*, 8> searchConstraints;
		for (auto ifaceConstraint : genericParamInst->mInterfaceConstraints)
		{
			if (!searchConstraints.Contains(ifaceConstraint))
			{
				searchConstraints.push_back(ifaceConstraint);

				for (auto& innerIFaceEntry : ifaceConstraint->mInterfaces)
				{
					auto innerIFace = innerIFaceEntry.mInterfaceType;
					if (!searchConstraints.Contains(innerIFace))
					{
						searchConstraints.push_back(innerIFace);
					}
				}
			}
		}

		BfTypedValue prevTarget;
		BfPropertyDef* prevDef = NULL;
		for (auto ifaceConstraint : searchConstraints)
		{
			BfTypedValue lookupVal = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), ifaceConstraint);

			mResult = LookupField(nameNode->mRight, lookupVal, fieldName);
			if (mPropDef != NULL)
			{
				if (prevDef != NULL)
				{
					bool isBetter = mModule->TypeIsSubTypeOf(mPropTarget.mType->ToTypeInstance(), prevTarget.mType->ToTypeInstance());
					bool isWorse = mModule->TypeIsSubTypeOf(prevTarget.mType->ToTypeInstance(), mPropTarget.mType->ToTypeInstance());

					if ((isWorse) && (!isBetter))
						continue;

					if (isBetter == isWorse)
					{
						auto error = mModule->Fail("Ambiguous property reference", nameNode->mRight);
						if (error != NULL)
						{
							mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' has a candidate", mModule->TypeToString(prevTarget.mType).c_str()), prevDef->GetRefNode());
							mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' has a candidate", mModule->TypeToString(mPropTarget.mType).c_str()), mPropDef->GetRefNode());
						}
					}
				}
				prevDef = mPropDef;
				prevTarget = mPropTarget;
			}
		}
		/*if ((mResult) || (mPropDef != NULL))
			break;*/
	}

	if (mPropDef != NULL)
	{
		mOrigPropTarget = origResult;
		if (((CheckIsBase(nameNode->mLeft)) || (wasBaseLookup))	&& (mPropDef->IsVirtual()))
			mPropDefBypassVirtual = true;
	}
	if ((mResult) || (mPropDef != NULL))
		return;

	if (hadError != NULL)
		*hadError = true;

	BfTypeInstance* typeInst = lookupType->ToTypeInstance();
	auto compiler = mModule->mCompiler;
	if ((typeInst != NULL) && (compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(nameNode->mRight)))
	{
		FixitAddMember(typeInst, mExpectingType, nameNode->mRight->ToString(), false);
	}
	mModule->Fail(StrFormat("Unable to find member '%s' in '%s'", fieldName.c_str(), mModule->TypeToString(lookupType).c_str()), nameNode->mRight);
}

void BfExprEvaluator::LookupQualifiedName(BfAstNode* nameNode, BfIdentifierNode* nameLeft, BfIdentifierNode* nameRight, bool ignoreInitialError, bool* hadError)
{
	String fieldName;
	if (nameRight != NULL)
		fieldName = nameRight->ToString();

	bool wasBaseLookup = false;
	if (auto qualifiedLeftName = BfNodeDynCast<BfQualifiedNameNode>(nameLeft))
	{
		if (CheckIsBase(qualifiedLeftName->mRight))
		{
			wasBaseLookup = true;
			auto type = mModule->ResolveTypeRef(qualifiedLeftName->mLeft, NULL);
			if (type == NULL)
				return;

			auto fieldName = nameRight->ToString();
			auto target = mModule->GetThis();
			target.mType = type;
			mResult = LookupField(nameRight, target, fieldName);
			if ((mPropDef != NULL) && (mPropDef->IsVirtual()))
				mPropDefBypassVirtual = true;
			return;
		}
	}

	if (!wasBaseLookup)
	{
		mResult = LookupIdentifier(nameLeft, ignoreInitialError, hadError);
		if ((mResult) && (!mResult.mType->IsComposite()))
			CheckResultForReading(mResult);
	}
	GetResult();

	if (!mResult)
	{
		if (!ignoreInitialError)
			mModule->Fail("Identifier not found", nameLeft);
		return;
	}

	if (mResult.mType->IsObject())
	{
		mResult = mModule->LoadValue(mResult, 0, mIsVolatileReference);
	}
	else if ((mResult.mType->IsPointer()) && mResult.mType->IsStructOrStructPtr())
	{
		BfPointerType* structPtrType = (BfPointerType*)mResult.mType;
		mResult = mModule->LoadValue(mResult, 0, mIsVolatileReference);
		mResult.mType = structPtrType->mElementType;
		if (mResult.mKind == BfTypedValueKind_ThisValue)
			mResult.mKind = BfTypedValueKind_ThisAddr;
		else
			mResult.mKind = BfTypedValueKind_Addr;
	}
	mIsVolatileReference = false;
	mIsHeapReference = false;

	if (!mResult)
		return;

	if (mResult.mType->IsAllocType())
		mResult.mType = mResult.mType->GetUnderlyingType();

	auto origResult = mResult;
	if (IsVar(mResult.mType))
	{
		auto varType = mModule->GetPrimitiveType(BfTypeCode_Var);
		mResult = BfTypedValue(mModule->GetDefaultValue(varType), varType, true);
		return;
	}

	if ((!mResult.mType->IsTypeInstance()) && (!mResult.mType->IsGenericParam()))
	{
		if (mResult.mType->IsSizedArray())
		{
			if (mResult.mType->IsValuelessType())
			{
				mResult.mType = mModule->GetWrappedStructType(mResult.mType);
				mResult.mValue = mModule->mBfIRBuilder->GetFakeVal();
			}
			else
			{
				mResult = mModule->MakeAddressable(mResult);
				mResult.mType = mModule->GetWrappedStructType(mResult.mType);
				mResult.mValue = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapTypeInstPtr(mResult.mType->ToTypeInstance()));
			}
		}
		else if (mResult.mType->IsWrappableType())
		{
			mResult.mType = mModule->GetWrappedStructType(mResult.mType);
		}
		else
		{
			if (hadError != NULL)
				*hadError = true;
			mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameLeft);
			mResult = BfTypedValue();
			return;
		}
	}

	BfTypedValue lookupVal = mResult;
	auto lookupType = BindGenericType(nameNode, mResult.mType);
	if ((lookupType->IsGenericParam()) && (!mResult.mType->IsGenericParam()))
	{
		bool prevUseMixinGenerics = false;
		if (mModule->mCurMethodState->mMixinState != NULL)
		{
			prevUseMixinGenerics = mModule->mCurMethodState->mMixinState->mUseMixinGenerics;
			mModule->mCurMethodState->mMixinState->mUseMixinGenerics = true;
		}

		// Try to lookup from generic binding
		mResult = LookupField(nameRight, BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), lookupType), fieldName, BfLookupFieldFlag_BindOnly);

		if (mModule->mCurMethodState->mMixinState != NULL)
			mModule->mCurMethodState->mMixinState->mUseMixinGenerics = prevUseMixinGenerics;

		if (mPropDef != NULL)
		{
			mOrigPropTarget = lookupVal;
			return;
		}
	}

	if (mPropDef == NULL)
		mResult = LookupField(nameRight, lookupVal, fieldName, CheckIsBase(nameLeft) ? BfLookupFieldFlag_BaseLookup : BfLookupFieldFlag_None);

	if ((!mResult) && (mPropDef == NULL) && (lookupType->IsGenericParam()))
	{
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)lookupType, true);
		SizedArray<BfTypeInstance*, 8> searchConstraints;
		for (auto ifaceConstraint : genericParamInst->mInterfaceConstraints)
		{
			if (!searchConstraints.Contains(ifaceConstraint))
			{
				searchConstraints.push_back(ifaceConstraint);

				for (auto& innerIFaceEntry : ifaceConstraint->mInterfaces)
				{
					auto innerIFace = innerIFaceEntry.mInterfaceType;
					if (!searchConstraints.Contains(innerIFace))
					{
						searchConstraints.push_back(innerIFace);
					}
				}
			}
		}

		BfTypedValue prevTarget;
		BfPropertyDef* prevDef = NULL;
		for (auto ifaceConstraint : searchConstraints)
		{
			BfTypedValue lookupVal = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), ifaceConstraint);

			mResult = LookupField(nameRight, lookupVal, fieldName);
			if (mPropDef != NULL)
			{
				if (prevDef != NULL)
				{
					bool isBetter = mModule->TypeIsSubTypeOf(mPropTarget.mType->ToTypeInstance(), prevTarget.mType->ToTypeInstance());
					bool isWorse = mModule->TypeIsSubTypeOf(prevTarget.mType->ToTypeInstance(), mPropTarget.mType->ToTypeInstance());

					if ((isWorse) && (!isBetter))
						continue;

					if (isBetter == isWorse)
					{
						auto error = mModule->Fail("Ambiguous property reference", nameRight);
						if (error != NULL)
						{
							mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' has a candidate", mModule->TypeToString(prevTarget.mType).c_str()), prevDef->GetRefNode());
							mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' has a candidate", mModule->TypeToString(mPropTarget.mType).c_str()), mPropDef->GetRefNode());
						}
					}
				}
				prevDef = mPropDef;
				prevTarget = mPropTarget;
			}
		}

		if ((mPropDef == NULL) && (genericParamInst->IsEnum()))
		{
			if ((fieldName == "Underlying") || (fieldName == "UnderlyingRef"))
			{
				mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
				return;
			}
		}
	}

	if (mPropDef != NULL)
	{
		mOrigPropTarget = origResult;
		if ((CheckIsBase(nameLeft)) || (wasBaseLookup))
		{
			if (mPropDef->IsVirtual())
				mPropDefBypassVirtual = true;
		}
	}
	if ((mResult) || (mPropDef != NULL))
		return;

	if (hadError != NULL)
		*hadError = true;

	BfTypeInstance* typeInst = lookupType->ToTypeInstance();
	auto compiler = mModule->mCompiler;
	if ((typeInst != NULL) && (compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(nameRight)))
	{
		FixitAddMember(typeInst, mExpectingType, nameRight->ToString(), false);
	}

	if (((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0) && (typeInst != NULL) && (CheckForMethodName(nameRight, typeInst, fieldName)))
		return;

	mModule->Fail(StrFormat("Unable to find member '%s' in '%s'", fieldName.c_str(), mModule->TypeToString(lookupType).c_str()), nameRight);
}

void BfExprEvaluator::LookupQualifiedStaticField(BfQualifiedNameNode* nameNode, bool ignoreIdentifierNotFoundError)
{
	// Lookup left side as a type
	{
 		BfType* type = NULL;
		{
			type = mModule->ResolveTypeRef(nameNode->mLeft, NULL, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);
			mModule->CheckTypeRefFixit(nameNode->mLeft);
		}
		if (type != NULL)
		{
			BfTypedValue lookupType;
			if (type->IsWrappableType())
				lookupType = BfTypedValue(mModule->GetWrappedStructType(type));
			else
				lookupType = BfTypedValue(type);
			auto findName = nameNode->mRight->ToString();
			/*if (findName == "base")
			{
				mResult = BfTypedValue(lookupType);
				return;
			}*/
			mResult = LookupField(nameNode->mRight, lookupType, findName);
			if ((mResult) || (mPropDef != NULL))
				return;

			if (lookupType.mType != NULL)
			{
				BfTypeInstance* typeInst = lookupType.mType->ToTypeInstance();
				auto compiler = mModule->mCompiler;
				if ((typeInst != NULL) && (compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(nameNode->mRight)))
				{
					FixitAddMember(typeInst, mExpectingType, nameNode->mRight->ToString(), true);
				}
			}

			if ((mBfEvalExprFlags & BfEvalExprFlags_NoLookupError) == 0)
				mModule->Fail("Field not found", nameNode->mRight);
			return;
		}
	}

	String fieldName = nameNode->mRight->ToString();

	if (auto qualifiedLeftName = BfNodeDynCast<BfQualifiedNameNode>(nameNode->mLeft))
		LookupQualifiedStaticField(qualifiedLeftName, true);
	else if (auto leftName = BfNodeDynCast<BfIdentifierNode>(nameNode->mLeft))
	{
		mResult = LookupIdentifier(leftName);
	}
	GetResult();

	if (!mResult)
	{
		if (!ignoreIdentifierNotFoundError)
			mModule->Fail("Identifier not found", nameNode->mLeft);
		return;
	}

	if (mResult.mType->IsObject())
	{
		mResult = mModule->LoadValue(mResult);
	}
	else if ((mResult.mType->IsPointer()) && mResult.mType->IsStructOrStructPtr())
	{
		BfPointerType* structPtrType = (BfPointerType*) mResult.mType;
		mResult = mModule->LoadValue(mResult);
		mResult.mType = structPtrType->mElementType;
	}

	if (!mResult)
		return;

	if (!mResult.mType->IsTypeInstance())
	{
		mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameNode->mLeft);
		return;
	}

	auto leftResult = mResult;
	mResult = LookupField(nameNode->mRight, leftResult, fieldName);
	if ((mResult) || (mPropDef != NULL))
		return;

	mModule->Fail(StrFormat("Unable to find member '%s' in '%s'", fieldName.c_str(), mModule->TypeToString(leftResult.mType).c_str()), nameNode->mRight);
}

void BfExprEvaluator::LookupQualifiedStaticField(BfAstNode* nameNode, BfIdentifierNode* nameLeft, BfIdentifierNode* nameRight, bool ignoreIdentifierNotFoundError)
{
	// Lookup left side as a type
	{
		BfType* type = mModule->ResolveTypeRef(nameLeft, NULL, BfPopulateType_Declaration, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_IgnoreLookupError | BfResolveTypeRefFlag_AllowGlobalContainer));
		if ((type != NULL) && (type->IsVar()) && (nameLeft->Equals("var")))
			type = NULL;

		if (type != NULL)
		{
			BfTypedValue lookupType;
			if (type->IsWrappableType())
				lookupType = BfTypedValue(mModule->GetWrappedStructType(type));
			else
				lookupType = BfTypedValue(type);
			auto findName = nameRight->ToString();

			if ((lookupType.mType != NULL) && (lookupType.mType->IsGenericParam()))
			{
				auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)lookupType.mType);
				if (genericParamInstance->mTypeConstraint != NULL)
				{
					mResult = LookupField(nameRight, BfTypedValue(genericParamInstance->mTypeConstraint), findName);
					if ((mResult) || (mPropDef != NULL))
					{
						mOrigPropTarget = lookupType;
						return;
					}
				}
				for (auto constraint : genericParamInstance->mInterfaceConstraints)
				{
					mResult = LookupField(nameRight, BfTypedValue(constraint), findName);
					if ((mResult) || (mPropDef != NULL))
					{
						mOrigPropTarget = lookupType;
						return;
					}
				}
			}

			/*if (findName == "base")
			{
			mResult = BfTypedValue(lookupType);
			return;
			}*/
			mResult = LookupField(nameRight, lookupType, findName);
			if ((mResult) || (mPropDef != NULL))
				return;

			if (lookupType.mType != NULL)
			{
				BfTypeInstance* typeInst = lookupType.mType->ToTypeInstance();
				auto compiler = mModule->mCompiler;
				if ((typeInst != NULL) && (compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(nameRight)))
				{
					FixitAddMember(typeInst, mExpectingType, nameRight->ToString(), true);
				}
			}

			if ((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0)
			{
				auto typeInst = lookupType.mType->ToTypeInstance();
				if ((typeInst != NULL) && (CheckForMethodName(nameRight, typeInst, findName)))
					return;
			}

			if ((mBfEvalExprFlags & BfEvalExprFlags_NoLookupError) == 0)
				mModule->Fail("Field not found", nameRight);
			return;
		}
	}

	String fieldName = nameRight->ToString();

	if (auto qualifiedLeftName = BfNodeDynCast<BfQualifiedNameNode>(nameLeft))
		LookupQualifiedStaticField(qualifiedLeftName, qualifiedLeftName->mLeft, qualifiedLeftName->mRight,  true);
	else if (auto leftName = BfNodeDynCast<BfIdentifierNode>(nameLeft))
	{
		mResult = LookupIdentifier(leftName);
	}
	GetResult();

	if (!mResult)
	{
		if (!ignoreIdentifierNotFoundError)
			mModule->Fail("Identifier not found", nameLeft);
		return;
	}

	if (mResult.mType->IsObject())
	{
		mResult = mModule->LoadValue(mResult);
	}
	else if ((mResult.mType->IsPointer()) && mResult.mType->IsStructOrStructPtr())
	{
		BfPointerType* structPtrType = (BfPointerType*)mResult.mType;
		mResult = mModule->LoadValue(mResult);
		mResult = BfTypedValue(mResult.mValue, structPtrType->mElementType, true);
	}

	if (!mResult)
		return;

	if (!mResult.mType->IsTypeInstance())
	{
		if (mResult.mType->IsSizedArray())
		{
			mResult.mType = mModule->GetWrappedStructType(mResult.mType);
			mResult.mValue = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapTypeInstPtr(mResult.mType->ToTypeInstance()));
		}
		else if (mResult.mType->IsWrappableType())
		{
			mResult.mType = mModule->GetWrappedStructType(mResult.mType);
		}
		else
		{
			mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameLeft);
			return;
		}
	}

	auto leftResult = mResult;
	mResult = LookupField(nameRight, leftResult, fieldName);
	if ((mResult) || (mPropDef != NULL))
		return;

	mModule->CheckTypeRefFixit(nameLeft);
	mModule->Fail(StrFormat("Unable to find member '%s' in '%s'", fieldName.c_str(), mModule->TypeToString(leftResult.mType).c_str()), nameRight);
}

void BfExprEvaluator::Visit(BfQualifiedNameNode* nameNode)
{
	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		autoComplete->CheckMemberReference(nameNode->mLeft, nameNode->mDot, nameNode->mRight);
	}

	bool hadError = false;
	LookupQualifiedName(nameNode, nameNode->mLeft, nameNode->mRight, true, &hadError);
	if ((mResult) || (mPropDef != NULL))
		return;
	if (hadError)
		return;

	LookupQualifiedStaticField(nameNode, nameNode->mLeft, nameNode->mRight, false);
}

void BfExprEvaluator::Visit(BfThisExpression* thisExpr)
{
  	mResult = mModule->GetThis();
	if (!mResult)
	{
		mModule->Fail("Static methods don't have 'this'", thisExpr);
		return;
	}

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(thisExpr)))
		autoComplete->SetDefinitionLocation(mModule->mCurTypeInstance->mTypeDef->GetRefNode());

	mResultLocalVar = mModule->GetThisVariable();
	mResultFieldInstance = NULL;
}

void BfExprEvaluator::Visit(BfBaseExpression* baseExpr)
{
	mResult = mModule->GetThis();
	if (!mResult)
	{
		mModule->Fail("Static methods don't have 'base'", baseExpr);
		return;
	}

	if ((mBfEvalExprFlags & BfEvalExprFlags_AllowBase) == 0)
		mModule->Fail("Use of keyword 'base' is not valid in this context", baseExpr);

	auto baseType = mModule->mCurTypeInstance->mBaseType;
	if (baseType == NULL)
		baseType = mModule->mContext->mBfObjectType;

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(baseExpr)))
		autoComplete->SetDefinitionLocation(baseType->mTypeDef->GetRefNode());

	mModule->PopulateType(baseType, BfPopulateType_Data);
	mResult = mModule->Cast(baseExpr, mResult, baseType, BfCastFlags_Explicit);
	if (mResult.IsSplat())
		mResult.mKind = BfTypedValueKind_BaseSplatHead;
	else if (mResult.IsAddr())
		mResult.mKind = BfTypedValueKind_BaseAddr;
	else if (mResult)
		mResult.mKind = BfTypedValueKind_BaseValue;
}

void BfExprEvaluator::Visit(BfMixinExpression* mixinExpr)
{
	if (mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Mixin)
	{
		auto varType = mModule->GetPrimitiveType(BfTypeCode_Var);
		auto newVar = mModule->AllocFromType(varType, mModule->mCurMethodState->mCurScope);
		mResult = BfTypedValue(newVar, varType, true);
		return;
	}

	auto curMethodState = mModule->mCurMethodState;
	if (curMethodState->mMixinState == NULL)
	{
		mModule->Fail("Mixin references can only be used within mixins", mixinExpr);
		return;
	}

	int localIdx = GetMixinVariable();
	if (localIdx != -1)
	{
		auto varDecl = curMethodState->mLocals[localIdx];
		if (varDecl != NULL)
		{
			BfTypedValue localResult = LoadLocal(varDecl);
			mResult = localResult;
			mResultLocalVar = varDecl;
			mResultFieldInstance = NULL;
			mResultLocalVarRefNode = mixinExpr;
			return;
		}
	}

	if (mModule->mCurMethodInstance->mIsUnspecialized)
	{
		mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		return;
	}

	mModule->Fail("Mixin value cannot be inferred", mixinExpr);
}

void BfExprEvaluator::Visit(BfSizedArrayCreateExpression* createExpr)
{
	auto type = mModule->ResolveTypeRef(createExpr->mTypeRef);
	if (type == NULL)
		return;

	if (type->IsArray())
	{
		// If we have a case like 'int[] (1, 2)' then we infer the sized array size from the initializer
		auto arrayType = (BfArrayType*)type;
		if (arrayType->mDimensions == 1)
		{
			int arraySize = 0;
			if (auto arrayInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(createExpr->mInitializer))
			{
				arraySize = (int)arrayInitExpr->mValues.size();
			}

			type = mModule->CreateSizedArrayType(arrayType->GetUnderlyingType(), arraySize);
		}
	}

	if (!type->IsSizedArray())
	{
		mModule->Fail("Sized array expected", createExpr->mTypeRef);
		return;
	}

	BfSizedArrayType* arrayType = (BfSizedArrayType*)type;

	if (createExpr->mInitializer == NULL)
	{
		mModule->AssertErrorState();
		mResult = mModule->GetDefaultTypedValue(arrayType);
		return;
	}

	InitializedSizedArray(arrayType, createExpr->mInitializer->mOpenBrace, createExpr->mInitializer->mValues, createExpr->mInitializer->mCommas, createExpr->mInitializer->mCloseBrace);
}

void BfExprEvaluator::Visit(BfInitializerExpression* initExpr)
{
	uint64 unassignedFieldFlags = 0;

	if (auto typeRef = BfNodeDynCast<BfTypeReference>(initExpr->mTarget))
	{
		BfType* type = NULL;
		if (auto typeRef = BfNodeDynCast<BfDotTypeReference>(initExpr->mTarget))
		{
			type = mExpectingType;
		}
		if (type == NULL)
			type = mModule->ResolveTypeRef(typeRef);
		if (type != NULL)
		{
			if (type->IsValueType())
			{
				if (mReceivingValue != NULL)
				{
					mResult = *mReceivingValue;
					mReceivingValue = NULL;
				}
				else
				{
					mResult = BfTypedValue(mModule->CreateAlloca(type), type, true);
				}
				auto typeInstance = type->ToTypeInstance();
				if (typeInstance != NULL)
					unassignedFieldFlags = (1 << typeInstance->mMergedFieldDataCount) - 1;
			}
			else
			{
				mModule->Fail("Initializer expressions can only be used on value types or allocated values", initExpr->mTarget);
			}
		}
	}
	else
		VisitChild(initExpr->mTarget);
	if (!mResult)
		mResult = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);

	BfIRBlock initBlock = BfIRBlock();
	if (unassignedFieldFlags != 0)
	{
		initBlock = mModule->mBfIRBuilder->CreateBlock("initStart", true);
		mModule->mBfIRBuilder->CreateBr(initBlock);
		mModule->mBfIRBuilder->SetInsertPoint(initBlock);
	}

	BfTypedValue initValue = GetResult(true);
	bool isFirstAdd = true;
	BfFunctionBindResult addFunctionBindResult;
	addFunctionBindResult.mWantsArgs = true;

	for (auto elementExpr : initExpr->mValues)
	{
		if ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0)
		{
			mModule->Fail("Comptime cannot evaluate initializer expressions", elementExpr);
			break;
		}

		bool wasValidInitKind = false;

		if (auto assignExpr = BfNodeDynCast<BfAssignmentExpression>(elementExpr))
		{
			BfTypedValue fieldResult;
			if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(assignExpr->mLeft))
			{
				StringT<128> findName;
				identifierNode->ToString(findName);
				mResultFieldInstance = NULL;
				fieldResult = LookupField(identifierNode, initValue, findName, BfLookupFieldFlag_IsImplicitThis);
				if ((fieldResult.mKind == BfTypedValueKind_TempAddr) || (fieldResult.mKind == BfTypedValueKind_RestrictedTempAddr))
					fieldResult.mKind = BfTypedValueKind_Addr;

				if ((mResultFieldInstance != NULL) && (mResultFieldInstance->mMergedDataIdx != -1))
				{
					int resultLocalVarField = 0;
					int resultLocalVarFieldCount = 0;
					mResultFieldInstance->GetDataRange(resultLocalVarField, resultLocalVarFieldCount);

					for (int i = 0; i < resultLocalVarFieldCount; i++)
						unassignedFieldFlags &= ~((int64)1 << (resultLocalVarField - 1 + i));
				}

				wasValidInitKind = true;

				if ((fieldResult) || (mPropDef != NULL))
				{
					if (mResultFieldInstance != NULL)
					{
						auto autoComplete = GetAutoComplete();
						if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(identifierNode)))
						{
							auto fieldDef = mResultFieldInstance->GetFieldDef();
							if (fieldDef != NULL)
								autoComplete->SetDefinitionLocation(fieldDef->GetRefNode());
						}
					}

					mResult = fieldResult;
					PerformAssignment(assignExpr, true, BfTypedValue());
					mResult = BfTypedValue();
				}
				else
				{
					mModule->Fail(StrFormat("'%s' does not contain a definition for '%s'", mModule->TypeToString(initValue.mType).c_str(),
						findName.c_str()), identifierNode);
				}
			}
		}
		else
		{
			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(elementExpr)))
			{
				if (auto identiferNode = BfNodeDynCast<BfIdentifierNode>(elementExpr))
				{
					auto typeInstance = initValue.mType->ToTypeInstance();
					if (typeInstance != NULL)
					{
						String filter;
						identiferNode->ToString(filter);
						autoComplete->AddTypeMembers(typeInstance, false, true, filter, typeInstance, false, true, false);
					}
				}
			}

			bool wasFirstAdd = isFirstAdd;
			if (isFirstAdd)
			{
				BfExprEvaluator exprEvaluator(mModule);
				exprEvaluator.mFunctionBindResult = &addFunctionBindResult;
				SizedArray<BfExpression*, 2> argExprs;
				argExprs.push_back(elementExpr);
				BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
				BfResolvedArgs argValues(&sizedArgExprs);
				exprEvaluator.ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);
				exprEvaluator.MatchMethod(elementExpr, NULL, initValue, false, false, "Add", argValues, BfMethodGenericArguments());

				if (addFunctionBindResult.mMethodInstance != NULL)
					CreateCall(initExpr, addFunctionBindResult.mMethodInstance, addFunctionBindResult.mFunc, true, addFunctionBindResult.mIRArgs);

				isFirstAdd = false;
			}
			else if ((addFunctionBindResult.mMethodInstance == NULL) || (addFunctionBindResult.mMethodInstance->GetParamCount() == 0))
			{
				mModule->CreateValueFromExpression(elementExpr, NULL, (BfEvalExprFlags)(mBfEvalExprFlags& BfEvalExprFlags_InheritFlags));
			}
			else
			{
				auto argValue = mModule->CreateValueFromExpression(elementExpr, addFunctionBindResult.mMethodInstance->GetParamType(0), (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
				if ((argValue) && (!mModule->mBfIRBuilder->mIgnoreWrites))
				{
					SizedArray<BfIRValue, 2> irArgs;
					PushThis(elementExpr, initValue, addFunctionBindResult.mMethodInstance, irArgs);
					PushArg(argValue, irArgs);
					for (int argIdx = (int)irArgs.size(); argIdx < (int)addFunctionBindResult.mIRArgs.size(); argIdx++)
						irArgs.Add(addFunctionBindResult.mIRArgs[argIdx]);
					CreateCall(initExpr, addFunctionBindResult.mMethodInstance, addFunctionBindResult.mFunc, true, irArgs);
				}
			}

			wasValidInitKind = true;
		}

		if (!wasValidInitKind)
		{
			mModule->Fail("Invalid initializer member declarator", initExpr);
		}
	}

	if (unassignedFieldFlags != 0)
	{
		auto curBlock = mModule->mBfIRBuilder->GetInsertBlock();
		mModule->mBfIRBuilder->SetInsertPointAtStart(initBlock);
		mModule->mBfIRBuilder->CreateMemSet(initValue.mValue, mModule->GetConstValue(0, mModule->GetPrimitiveType(BfTypeCode_Int8)),
			mModule->GetConstValue(initValue.mType->mSize), initValue.mType->mAlign);
		mModule->mBfIRBuilder->SetInsertPoint(curBlock);
	}

	mResult = initValue;
}

void BfExprEvaluator::Visit(BfCollectionInitializerExpression* arrayInitExpr)
{
	mModule->Fail("Collection initializer not usable here", arrayInitExpr);
}

void BfExprEvaluator::Visit(BfTypeOfExpression* typeOfExpr)
{
	auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (typeOfExpr->mTypeRef != NULL))
	{
		autoComplete->CheckTypeRef(typeOfExpr->mTypeRef, false, true);
	}

	BfType* type;
	if ((typeOfExpr->mTypeRef != NULL) && (typeOfExpr->mTypeRef->IsA<BfVarTypeReference>()))
	{
		type = mModule->GetPrimitiveType(BfTypeCode_Var);
	}
	else
	{
		type = ResolveTypeRef(typeOfExpr->mTypeRef, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_AllowGlobalsSelf | BfResolveTypeRefFlag_AllowUnboundGeneric));
	}

	if (type == NULL)
	{
		mResult = mModule->GetDefaultTypedValue(mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef));
		return;
	}

	mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
	mResult = BfTypedValue(mModule->CreateTypeDataRef(type), typeType);
}

bool BfExprEvaluator::LookupTypeProp(BfTypeOfExpression* typeOfExpr, BfIdentifierNode* propName)
{
	auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);

	BfType* type;
	//
	{
		// We ignore errors because we go through the normal Visit(BfTypeOfExpression) if this fails, which will throw the error again
		SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
		if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeOfExpr->mTypeRef))
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
			type = mModule->ResolveTypeRefAllowUnboundGenerics(typeOfExpr->mTypeRef, BfPopulateType_Identity);
		}
		else
		{
			type = ResolveTypeRef(typeOfExpr->mTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_IgnoreLookupError);
		}
	}

	if (type == NULL)
	{
		mResult = mModule->GetDefaultTypedValue(mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef));
		return false;
	}

	mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
	mModule->PopulateType(type);
	auto typeInstance = type->ToTypeInstance();

	auto _BoolResult = [&](bool val)
	{
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, val ? 1 : 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
	};

	auto _Int32Result = [&](int32 val)
	{
		mResult = BfTypedValue(mModule->GetConstValue32(val), mModule->GetPrimitiveType(BfTypeCode_Int32));
	};

	String memberName;
	propName->ToString(memberName);

	bool handled = true;
	if (memberName == "IsTypedPrimitive")
		_BoolResult(type->IsPrimitiveType());
	else if (memberName == "IsObject")
		_BoolResult(type->IsObject());
	else if (memberName == "IsValueType")
		_BoolResult(type->IsValueType());
	else if (memberName == "IsPrimitive")
		_BoolResult(type->IsPrimitiveType());
	else if (memberName == "IsInteger")
		_BoolResult(type->IsInteger());
	else if (memberName == "IsIntegral")
		_BoolResult(type->IsIntegral());
	else if (memberName == "IsSigned")
		_BoolResult(type->IsSigned());
	else if (memberName == "IsFloatingPoint")
		_BoolResult(type->IsFloat());
	else if (memberName == "IsPointer")
		_BoolResult(type->IsPointer());
	else if (memberName == "IsStruct")
		_BoolResult(type->IsStruct());
	else if (memberName == "IsSplattable")
		_BoolResult(type->IsSplattable());
	else if (memberName == "IsUnion")
		_BoolResult(type->IsUnion());
	else if (memberName == "IsBoxed")
		_BoolResult(type->IsBoxed());
	else if (memberName == "IsEnum")
		_BoolResult(type->IsEnum());
	else if (memberName == "IsTuple")
		_BoolResult(type->IsTuple());
	else if (memberName == "IsNullable")
		_BoolResult(type->IsNullable());
	else if (memberName == "IsGenericType")
		_BoolResult(type->IsGenericTypeInstance());
	else if (memberName == "IsGenericParam")
		_BoolResult(type->IsGenericParam());
	else if (memberName == "IsArray")
		_BoolResult(type->IsArray());
	else if (memberName == "IsSizedArray")
		_BoolResult(type->IsSizedArray());
	else if (memberName == "TypeId")
	{
		_Int32Result(type->mTypeId);
		mResult.mType = mModule->ResolveTypeDef(mModule->mCompiler->mReflectTypeIdTypeDef);
	}
	else if (memberName == "GenericParamCount")
	{
		auto genericTypeInst = type->ToGenericTypeInstance();
		_Int32Result((genericTypeInst != NULL) ? (int)genericTypeInst->mGenericTypeInfo->mTypeGenericArguments.size() : 0);
	}
	else if (memberName == "Size")
		_Int32Result(type->mSize);
	else if (memberName == "Align")
		_Int32Result(type->mAlign);
	else if (memberName == "Stride")
		_Int32Result(type->GetStride());
	else if (memberName == "InstanceSize")
		_Int32Result((typeInstance != NULL) ? typeInstance->mInstSize : type->mSize);
	else if (memberName == "InstanceAlign")
		_Int32Result((typeInstance != NULL) ? typeInstance->mInstAlign : type->mSize);
	else if (memberName == "InstanceStride")
		_Int32Result((typeInstance != NULL) ? typeInstance->GetInstStride() : type->GetStride());
	else if (memberName == "UnderlyingType")
	{
		auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);
		if (type->IsGenericParam())
		{
			auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)type);
			if (genericParamInstance->IsEnum())
				mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->MapType(typeType)), typeType);
		}
		else if (type->IsEnum())
		{
			if (type->IsDataIncomplete())
				mModule->PopulateType(type);
			auto underlyingType = type->GetUnderlyingType();
			if (underlyingType != NULL)
			{
				mModule->AddDependency(underlyingType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
				mResult = BfTypedValue(mModule->CreateTypeDataRef(underlyingType), typeType);
			}
		}
	}
	else if (memberName == "BitSize")
	{
		auto int32Type = mModule->GetPrimitiveType(BfTypeCode_Int32);

		BfType* checkType = type;
		if (checkType->IsTypedPrimitive())
			checkType = checkType->GetUnderlyingType();

		if (checkType->IsGenericParam())
		{
			mResult = mModule->GetDefaultTypedValue(int32Type, false, Beefy::BfDefaultValueKind_Undef);
			return true;
		}

		if ((typeInstance != NULL) && (typeInstance->IsEnum()))
		{
			if (typeInstance->mTypeInfoEx != NULL)
			{
				int64 minValue = typeInstance->mTypeInfoEx->mMinValue;
				if (minValue < 0)
					minValue = ~minValue;
				int64 maxValue = typeInstance->mTypeInfoEx->mMaxValue;
				if (maxValue < 0)
					maxValue = ~maxValue;
				uint64 value = (uint64)minValue | (uint64)maxValue;

				int bitCount = 1;
				if (typeInstance->mTypeInfoEx->mMinValue < 0)
					bitCount++;

				while (value >>= 1)
					bitCount++;

				mModule->AddDependency(typeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, bitCount), int32Type);
				return true;
			}
		}

		int bitSize = checkType->mSize * 8;
		if (checkType->GetTypeCode() == BfTypeCode_Boolean)
			bitSize = 1;
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, bitSize), int32Type);
		return true;
	}
	else if ((memberName == "MinValue") || (memberName == "MaxValue"))
	{
		bool isMin = memberName == "MinValue";
		bool isBitSize = memberName == "BitSize";

		BfType* checkType = type;
		if (checkType->IsTypedPrimitive())
			checkType = checkType->GetUnderlyingType();

		if (checkType->IsGenericParam())
		{
			bool foundMatch = false;

			auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)checkType);
			if (((genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Enum) != 0) ||
				((genericParamInstance->mTypeConstraint != NULL) && (genericParamInstance->mTypeConstraint->IsInstanceOf(mModule->mCompiler->mEnumTypeDef))))
				foundMatch = true;

			else
			{
				for (auto constraint : genericParamInstance->mInterfaceConstraints)
				{
					if (constraint->IsInstanceOf(mModule->mCompiler->mIIntegerTypeDef))
						foundMatch = true;
				}
			}

			if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
			{
				for (int genericParamIdx = (int)mModule->mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
					genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
				{
					genericParamInstance = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
					if (genericParamInstance->mExternType == type)
					{
						if (((genericParamInstance->mGenericParamFlags & BfGenericParamFlag_Enum) != 0) ||
							((genericParamInstance->mTypeConstraint != NULL) && (genericParamInstance->mTypeConstraint->IsInstanceOf(mModule->mCompiler->mEnumTypeDef))))
							foundMatch = true;
					}
				}
			}

			if (foundMatch)
			{
				mResult = mModule->GetDefaultTypedValue(type, false, Beefy::BfDefaultValueKind_Undef);
				return true;
			}
		}

		if (checkType->IsPrimitiveType())
		{
			auto primType = (BfPrimitiveType*)checkType;

			if ((typeInstance != NULL) && (typeInstance->IsEnum()))
			{
				if (typeInstance->mTypeInfoEx != NULL)
				{
					mModule->AddDependency(typeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)typeInstance->mTypeInfoEx->mMinValue : (uint64)typeInstance->mTypeInfoEx->mMaxValue), typeInstance);
					return true;
				}
			}
			else
			{
				switch (primType->mTypeDef->mTypeCode)
				{
				case BfTypeCode_Int8:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? -0x80 : 0x7F), primType);
					return true;
				case BfTypeCode_Int16:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? -0x8000 : 0x7FFF), primType);
					return true;
				case BfTypeCode_Int32:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x80000000LL : 0x7FFFFFFF), primType);
					return true;
				case BfTypeCode_Int64:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x8000000000000000LL : (uint64)0x7FFFFFFFFFFFFFFFLL), primType);
					return true;
				case BfTypeCode_UInt8:
				case BfTypeCode_Char8:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : 0xFF), primType);
					return true;
				case BfTypeCode_UInt16:
				case BfTypeCode_Char16:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : 0xFFFF), primType);
					return true;
				case BfTypeCode_UInt32:
				case BfTypeCode_Char32:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFLL), primType);
					return true;
				case BfTypeCode_UInt64:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFFFFFFFFFLL), primType);
					return true;
				case BfTypeCode_IntPtr:
					if (mModule->mSystem->mPtrSize == 8)
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x8000000000000000LL : (uint64)0x7FFFFFFFFFFFFFFFLL), primType);
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x80000000LL : 0x7FFFFFFF), primType);
					return true;
				case BfTypeCode_UIntPtr:
					if (mModule->mSystem->mPtrSize == 8)
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFFFFFFFFFLL), primType);
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFLL), primType);
					return true;
				default: break;
				}
			}
		}

		if (type->IsEnum())
		{
			mModule->Fail(StrFormat("'MinValue' cannot be used on enum with payload '%s'", mModule->TypeToString(type).c_str()), propName);
		}
		else
		{
			mModule->Fail(StrFormat("'%s' cannot be used on type '%s'", memberName.c_str(), mModule->TypeToString(type).c_str()), propName);
		}
	}
	else
		return false;

	if ((type->IsGenericParam()) && (!mModule->mIsComptimeModule))
	{
		if (mResult.mType != NULL)
			mResult = mModule->GetDefaultTypedValue(mResult.mType, false, Beefy::BfDefaultValueKind_Undef);
	}

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (typeOfExpr->mTypeRef != NULL))
	{
		autoComplete->CheckTypeRef(typeOfExpr->mTypeRef, false, true);
	}

	return true;
}

void BfExprEvaluator::DoTypeIntAttr(BfTypeReference* typeRef, BfTokenNode* commaToken, BfIdentifierNode* memberName, BfToken token)
{
	auto autoComplete = GetAutoComplete();

	auto type = mModule->ResolveTypeRef(typeRef, BfPopulateType_Data, BfResolveTypeRefFlag_AutoComplete);
	if (type == NULL)
		return;
	mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage); // Local usage ensures it changes when the type data changes

	auto typeInst = type->ToTypeInstance();
	if ((typeInst != NULL) && (typeInst->mTypeDef->mIsOpaque))
	{
		mModule->Fail(StrFormat("Unable to determine attributes for opaque type '%s'", mModule->TypeToString(type).c_str()), typeRef);
	}

	BfType* sizeType = mModule->GetPrimitiveType(BfTypeCode_Int32);

	int attrVal = 0;
	switch (token)
	{
	case BfToken_SizeOf: attrVal = type->mSize; break;
	case BfToken_AlignOf: attrVal = type->mAlign; break;
	case BfToken_StrideOf: attrVal = type->GetStride(); break;
	default: break;
	}

	if (token == BfToken_OffsetOf)
	{
		bool found = false;
		String findName;
		if (memberName != NULL)
			findName = memberName->ToString();

		BfAstNode* refNode = typeRef;
		if (memberName != NULL)
			refNode = memberName;

		auto checkTypeInst = typeInst;
		while (checkTypeInst != NULL)
		{
			checkTypeInst->mTypeDef->PopulateMemberSets();

			String filter;
			if ((autoComplete != NULL) && (autoComplete->InitAutocomplete(commaToken, memberName, filter)))
			{
				auto activeTypeDef = mModule->GetActiveTypeDef();
				mModule->PopulateType(checkTypeInst);

				BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
				for (auto fieldDef : checkTypeInst->mTypeDef->mFields)
				{
					if (fieldDef->mIsStatic)
						continue;

					if (!mModule->CheckProtection(protectionCheckFlags, typeInst, fieldDef->mDeclaringType->mProject, fieldDef->mProtection, typeInst))
						continue;

					if ((!typeInst->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, mModule)) ||
						(!typeInst->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
						continue;

					auto& fieldInst = checkTypeInst->mFieldInstances[fieldDef->mIdx];
					autoComplete->AddField(checkTypeInst, fieldDef, &fieldInst, filter);
				}
			}

			BfMemberSetEntry* memberSetEntry = NULL;
			if (checkTypeInst->mTypeDef->mFieldSet.TryGetWith(findName, &memberSetEntry))
			{
				auto fieldDef = (BfFieldDef*)memberSetEntry->mMemberDef;
				if (fieldDef != NULL)
				{
					if (fieldDef->mIsStatic)
						mModule->Fail(StrFormat("Cannot generate an offset from static field '%s.%s'", mModule->TypeToString(type).c_str(), fieldDef->mName.c_str()), refNode);

					mModule->PopulateType(checkTypeInst);
					auto& fieldInst = checkTypeInst->mFieldInstances[fieldDef->mIdx];
					attrVal = fieldInst.mDataOffset;
					found = true;
					break;
				}
			}

			checkTypeInst = checkTypeInst->mBaseType;
		}

		if (!found)
		{
			mModule->Fail(StrFormat("Unable to locate field '%s.%s'", mModule->TypeToString(type).c_str(), findName.c_str()), refNode);
		}
	}

	bool isUndefVal = false;

	if (type->IsGenericParam())
		isUndefVal = true;

	if (type->IsSizedArray())
	{
		auto sizedArray = (BfSizedArrayType*)type;
		if (sizedArray->mElementCount == -1)
			isUndefVal = true;
	}

	if (isUndefVal)
	{
		// We do this so we know it's a constant but we can't assume anything about its value
		//  We make the value an Int32 which doesn't match the IntPtr type, but it allows us to
		//  assume it can be implicitly cased to int32
		mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->MapType(sizeType)), sizeType);
	}
	else
		mResult = BfTypedValue(mModule->GetConstValue(attrVal, sizeType), sizeType);
}

void BfExprEvaluator::Visit(BfSizeOfExpression* sizeOfExpr)
{
	DoTypeIntAttr(sizeOfExpr->mTypeRef, NULL, NULL, BfToken_SizeOf);
}

void BfExprEvaluator::Visit(BfAlignOfExpression* alignOfExpr)
{
	DoTypeIntAttr(alignOfExpr->mTypeRef, NULL, NULL, BfToken_AlignOf);
}

void BfExprEvaluator::Visit(BfStrideOfExpression* strideOfExpr)
{
	DoTypeIntAttr(strideOfExpr->mTypeRef, NULL, NULL, BfToken_StrideOf);
}

void BfExprEvaluator::Visit(BfOffsetOfExpression* offsetOfExpr)
{
	DoTypeIntAttr(offsetOfExpr->mTypeRef, offsetOfExpr->mCommaToken, offsetOfExpr->mMemberName, BfToken_OffsetOf);
}

void BfExprEvaluator::Visit(BfNameOfExpression* nameOfExpr)
{
	String name;

	if (mModule->IsInSpecializedGeneric())
	{
		if (auto identifierNode = BfNodeDynCastExact<BfIdentifierNode>(nameOfExpr->mTarget))
		{
			// This is necessary so we don't resolve 'T' to the actual generic argument type
			name = identifierNode->ToString();
		}
	}

	if (name.IsEmpty())
	{
		auto type = mModule->ResolveTypeRef(nameOfExpr->mTarget, {}, BfPopulateType_IdentityNoRemapAlias,
			(BfResolveTypeRefFlags)(BfResolveTypeRefFlag_AllowUnboundGeneric | BfResolveTypeRefFlag_IgnoreLookupError | BfResolveTypeRefFlag_IgnoreProtection));
		if (type != NULL)
		{
			auto typeInst = type->ToTypeInstance();
			if (typeInst != NULL)
				name = typeInst->mTypeDef->mName->ToString();
			else
				name = mModule->TypeToString(type);

			mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_NameReference);

			// Just do this for autocomplete
			SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
			VisitChild(nameOfExpr->mTarget);
		}
	}

	if (name.IsEmpty())
	{
		if (auto identifer = BfNodeDynCast<BfIdentifierNode>(nameOfExpr->mTarget))
		{
			String targetStr = nameOfExpr->mTarget->ToString();
			BfAtomComposite targetComposite;
			bool isValid = mModule->mSystem->ParseAtomComposite(targetStr, targetComposite);
			bool namespaceExists = false;

			BfProject* bfProject = NULL;
			auto activeTypeDef = mModule->GetActiveTypeDef();
			if (activeTypeDef != NULL)
				bfProject = activeTypeDef->mProject;
			auto _CheckProject = [&](BfProject* project)
			{
				if ((isValid) && (project->mNamespaces.ContainsKey(targetComposite)))
					namespaceExists = true;
			};

			if (bfProject != NULL)
			{
				for (int depIdx = -1; depIdx < (int)bfProject->mDependencies.size(); depIdx++)
				{
					BfProject* depProject = (depIdx == -1) ? bfProject : bfProject->mDependencies[depIdx];
					_CheckProject(depProject);
				}
			}
			else
			{
				for (auto project : mModule->mSystem->mProjects)
					_CheckProject(project);
			}

			if (namespaceExists)
			{
				if (mModule->mCompiler->mResolvePassData != NULL)
				{
					if (auto sourceClassifier = mModule->mCompiler->mResolvePassData->GetSourceClassifier(nameOfExpr->mTarget))
					{
						BfAstNode* checkIdentifier = identifer;
						while (true)
						{
							auto qualifiedIdentifier = BfNodeDynCast<BfQualifiedNameNode>(checkIdentifier);
							if (qualifiedIdentifier == NULL)
								break;
							sourceClassifier->SetElementType(qualifiedIdentifier->mRight, BfSourceElementType_Namespace);
							checkIdentifier = qualifiedIdentifier->mLeft;
						}

						sourceClassifier->SetElementType(checkIdentifier, BfSourceElementType_Namespace);
					}
				}

				name = targetComposite.mParts[targetComposite.mSize - 1]->ToString();
			}
		}
	}

	if (name.IsEmpty())
	{
		SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NameOf));
		SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mBfIRBuilder->mIgnoreWrites, true);
		VisitChild(nameOfExpr->mTarget);

		if ((mBfEvalExprFlags & BfEvalExprFlags_NameOfSuccess) != 0)
		{
			BfAstNode* nameNode = nameOfExpr->mTarget;
			if (auto attributedIdentifierNode = BfNodeDynCast<BfAttributedIdentifierNode>(nameNode))
				nameNode = attributedIdentifierNode->mIdentifier;
			if (auto memberReferenceExpr = BfNodeDynCast<BfMemberReferenceExpression>(nameNode))
				nameNode = memberReferenceExpr->mMemberName;
			if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(nameNode))
				nameNode = qualifiedNameNode->mRight;
			name = nameNode->ToString();
		}
		else if (mResultFieldInstance != NULL)
		{
			auto fieldDef = mResultFieldInstance->GetFieldDef();
			if (fieldDef != NULL)
				name = fieldDef->mName;
		}
		else if (mResultLocalVar != NULL)
		{
			name = mResultLocalVar->mName;
		}
		else if (mPropDef != NULL)
		{
			name = mPropDef->mName;
		}
	}

	if ((name.IsEmpty()) && (nameOfExpr->mTarget != NULL))
		mModule->Fail("Expression does not have a name", nameOfExpr->mTarget);

	mResult = BfTypedValue(mModule->GetStringObjectValue(name), mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
}

void BfExprEvaluator::Visit(BfIsConstExpression* isConstExpr)
{
	if (isConstExpr->mExpression == NULL)
	{
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
		return;
	}

	BfMethodState methodState;
	SetAndRestoreValue<BfMethodState*> prevMethodState(mModule->mCurMethodState, &methodState, false);
	if (mModule->mCurMethodState == NULL)
		prevMethodState.Set();
	methodState.mTempKind = BfMethodState::TempKind_NonStatic;

	SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
	SetAndRestoreValue<bool> allowUninitReads(mModule->mCurMethodState->mAllowUinitReads, true);

	BfEvalExprFlags exprFlags = BfEvalExprFlags_None;

	auto result = mModule->CreateValueFromExpression(isConstExpr->mExpression, NULL, BfEvalExprFlags_DeclType);
	bool isConst = mModule->mBfIRBuilder->IsConstValue(result.mValue);
	mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, isConst ? 1 : 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
}

void BfExprEvaluator::Visit(BfDefaultExpression* defaultExpr)
{
	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
		autoComplete->CheckTypeRef(defaultExpr->mTypeRef, false, true);

	BfType* type = NULL;
	if (defaultExpr->mOpenParen == NULL)
	{
		if (mExpectingType)
		{
			type = mExpectingType;
		}
		else
		{
			mModule->Fail("Type cannot be inferred, consider adding explicit type name", defaultExpr);
			return;
		}
	}
	else
		type = mModule->ResolveTypeRef(defaultExpr->mTypeRef);
	if (type == NULL)
		return;

	BfDefaultValueKind defaultKind = BfDefaultValueKind_Const;
	if (type->IsRef())
	{
		mModule->Fail(StrFormat("There is no default value for type '%s'", mModule->TypeToString(type).c_str()), defaultExpr);
		defaultKind = BfDefaultValueKind_Addr;
	}

	mModule->ValidateAllocation(type, defaultExpr->mTypeRef);
	mModule->AddDependency(type, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
	mResult = mModule->GetDefaultTypedValue(type, true, defaultKind);
}

void BfExprEvaluator::Visit(BfUninitializedExpression* uninitialziedExpr)
{
	mModule->Fail("Invalid use of the '?' uninitialized specifier", uninitialziedExpr);
}

void BfExprEvaluator::Visit(BfCheckTypeExpression* checkTypeExpr)
{
	auto targetValue = mModule->CreateValueFromExpression(checkTypeExpr->mTarget, NULL, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
	if (!targetValue)
		return;

	if (checkTypeExpr->mTypeRef == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
		autoComplete->CheckTypeRef(checkTypeExpr->mTypeRef, false, true);

	auto targetType = mModule->ResolveTypeRef(checkTypeExpr->mTypeRef, BfPopulateType_Declaration);
	if (!targetType)
	{
		mModule->AssertErrorState();
		return;
	}

	mModule->AddDependency(targetType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);

	auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
	if (targetValue.mType->IsVar())
	{
		mResult = mModule->GetDefaultTypedValue(boolType, false, BfDefaultValueKind_Undef);
		return;
	}

	if (targetValue.mType->IsValueTypeOrValueTypePtr())
	{
		auto typeInstance = targetValue.mType->ToTypeInstance();
		if (targetValue.mType->IsWrappableType())
			typeInstance = mModule->GetWrappedStructType(targetValue.mType);

		bool matches = (targetValue.mType == targetType) || (mModule->mContext->mBfObjectType == targetType);

		if (!matches)
			matches = mModule->TypeIsSubTypeOf(typeInstance, targetType->ToTypeInstance());

		if (!typeInstance->IsGenericParam())
		{
			if (matches)
				mModule->Warn(0, "The result of this operation is always 'true'", checkTypeExpr->mIsToken);
			else
				mModule->Warn(0, "The result of this operation is always 'false'", checkTypeExpr->mIsToken);
		}

		mResult = BfTypedValue(mModule->GetConstValue(matches ? 1 : 0, boolType), boolType);
		return;
	}

	if (targetType->IsValueType())
	{
		if ((targetValue.mType != mModule->mContext->mBfObjectType) && (!targetValue.mType->IsInterface()))
		{
			mResult = BfTypedValue(mModule->GetConstValue(0, boolType), boolType);
			return;
		}
	}

	int wantTypeId = 0;
	if (!targetType->IsGenericParam())
		wantTypeId = targetType->mTypeId;

	auto objectType = mModule->mContext->mBfObjectType;
	mModule->PopulateType(objectType, BfPopulateType_Full);

	targetValue = mModule->LoadValue(targetValue);

	BfTypeInstance* srcTypeInstance = targetValue.mType->ToTypeInstance();
	BfTypeInstance* targetTypeInstance = targetType->ToTypeInstance();
	bool wasGenericParamType = false;

	if ((srcTypeInstance != NULL) && (targetTypeInstance != NULL))
	{
		if (mModule->TypeIsSubTypeOf(srcTypeInstance, targetTypeInstance))
		{
			// We don't give this warning when we have wasGenericParmType set because that indicates we had a generic type constraint,
			//  and a type constraint infers that the ACTUAL type used will be equal to or derived from that type and therefore
			//  it may be a "necessary cast" indeed
			if ((!wasGenericParamType) && (mModule->mCurMethodState->mMixinState == NULL))
			{
				if (srcTypeInstance == targetType)
					mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary check, the value is already type '%s'",
						mModule->TypeToString(srcTypeInstance).c_str()), checkTypeExpr->mIsToken);
				else
					mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary check, '%s' is a subtype of '%s'",
						mModule->TypeToString(srcTypeInstance).c_str(), mModule->TypeToString(targetType).c_str()), checkTypeExpr->mIsToken);
			}

			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), boolType);
			return;
		}
		else if ((!targetType->IsInterface()) && (srcTypeInstance != mModule->mContext->mBfObjectType) && (!mModule->TypeIsSubTypeOf(targetTypeInstance, srcTypeInstance)))
		{
			mModule->Fail(StrFormat("Cannot convert type '%s' to '%s' via any conversion",
				mModule->TypeToString(targetValue.mType).c_str(), mModule->TypeToString(targetTypeInstance).c_str()), checkTypeExpr->mIsToken);
		}
	}

	if (mModule->mCompiler->IsAutocomplete())
	{
		mResult = mModule->GetDefaultTypedValue(boolType, false, BfDefaultValueKind_Addr);
		return;
	}

	auto irb = mModule->mBfIRBuilder;
	auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();
	auto matchBB = mModule->mBfIRBuilder->CreateBlock("is.match");
	auto endBB = mModule->mBfIRBuilder->CreateBlock("is.done");

	BfIRValue boolResult = mModule->CreateAlloca(boolType);
	irb->CreateAlignedStore(irb->CreateConst(BfTypeCode_Boolean, 0), boolResult, 1);

	mModule->EmitDynamicCastCheck(targetValue, targetType, matchBB, endBB);

	mModule->AddBasicBlock(matchBB);
	irb->CreateAlignedStore(irb->CreateConst(BfTypeCode_Boolean, 1), boolResult, 1);
	irb->CreateBr(endBB);

	mModule->AddBasicBlock(endBB);
	mResult = BfTypedValue(irb->CreateAlignedLoad(boolResult, 1), boolType);
}

void BfExprEvaluator::Visit(BfDynamicCastExpression* dynCastExpr)
{
	auto targetValue = mModule->CreateValueFromExpression(dynCastExpr->mTarget);
	auto targetType = mModule->ResolveTypeRefAllowUnboundGenerics(dynCastExpr->mTypeRef, BfPopulateType_Data, false);

	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		autoComplete->CheckTypeRef(dynCastExpr->mTypeRef, false, true);
	}

	auto origTargetType = targetType;

	if (!targetValue)
		return;
	if (!targetType)
	{
		mModule->AssertErrorState();
		return;
	}

	bool wasGenericParamType = false;
	if (targetType->IsGenericParam())
	{
		//targetType = mModule->ResolveGenericType(targetType);
		//wasGenericParamType = true;
	}

	if ((targetValue.mType->IsMethodRef()) || (targetType->IsMethodRef()))
	{
		// We can never cast a MethodRef to any class type
		mResult = mModule->GetDefaultTypedValue(targetType);
		return;
	}

	if (mModule->mContext->mResolvingVarField)
	{
		mResult = mModule->GetDefaultTypedValue(targetType);
		return;
	}

	mModule->AddDependency(targetType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);

	if (targetType->IsGenericParam())
	{
		wasGenericParamType = true;
		BfGenericParamInstance* origGenericParam = NULL;
		int pass = 0;
		while ((targetType != NULL) && (targetType->IsGenericParam()))
		{
			auto genericParamType = (BfGenericParamType*)targetType;
			auto genericParam = mModule->GetGenericParamInstance(genericParamType);
			if (pass == 0)
				origGenericParam = genericParam;
			auto typeConstraint = genericParam->mTypeConstraint;
			if ((typeConstraint == NULL) && (genericParam->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_Interface)))
				typeConstraint = mModule->mContext->mBfObjectType;
			targetType = typeConstraint;
			if (++pass >= 100) // Sanity - but we should have caught circular error before
				break;
		}

		if ((targetType == NULL) || (!targetType->IsObjectOrInterface()))
		{
			mModule->Fail(StrFormat("The type parameter '%s' cannot be used with the 'as' operator because it does not have a class type constraint nor a 'class' or 'interface' constraint",
				origGenericParam->GetGenericParamDef()->mName.c_str()), dynCastExpr->mTypeRef);
			return;
		}
	}

	if (targetType->IsVar())
	{
		mResult = mModule->GetDefaultTypedValue(targetType);
		return;
	}

	if ((!targetType->IsObjectOrInterface()) && (!targetType->IsNullable()))
	{
		mModule->Fail(StrFormat("The as operator must be used with a reference type or nullable type ('%s' is a non-nullable value type)",
			mModule->TypeToString(origTargetType).c_str()), dynCastExpr->mTypeRef);
		return;
	}

	if (targetValue.mType->IsGenericParam())
	{
		auto genericParamType = (BfGenericParamType*)targetValue.mType;
		auto genericParam = mModule->GetGenericParamInstance(genericParamType);
		auto typeConstraint = genericParam->mTypeConstraint;
		if (typeConstraint == NULL)
			typeConstraint = mModule->mContext->mBfObjectType;

		if ((typeConstraint->IsDelegate()) && (typeConstraint == targetType))
		{
			// Delegate constraints may be matched by valueless method references, so this won't always match (don't warn)
			mResult = mModule->GetDefaultTypedValue(targetType);
			return;
		}

		targetValue = mModule->GetDefaultTypedValue(typeConstraint);
	}

	auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

	auto _CheckResult = [&]()
	{
		if ((mResult) && (origTargetType->IsGenericParam()))
			mResult = mModule->GetDefaultTypedValue(origTargetType, false, BfDefaultValueKind_Undef);
	};

	if ((targetValue.mType->IsNullable()) && (targetType->IsInterface()))
	{
		mResult = mModule->Cast(dynCastExpr, targetValue, targetType, BfCastFlags_SilentFail);
		if (!mResult)
		{
			mModule->Warn(0, StrFormat("Conversion from '%s' to '%s' will always be null",
				mModule->TypeToString(targetValue.mType).c_str(), mModule->TypeToString(origTargetType).c_str()), dynCastExpr->mAsToken);
			mResult = BfTypedValue(mModule->GetDefaultValue(origTargetType), origTargetType);
		}
		_CheckResult();
		return;
	}

	if (targetValue.mType->IsVar())
	{
		mResult = mModule->GetDefaultTypedValue(targetType, false, BfDefaultValueKind_Undef);
		return;
	}

	if (targetValue.mType->IsValueTypeOrValueTypePtr())
	{
		mModule->Warn(0, StrFormat("Type '%s' is not applicable for dynamic casting",
			mModule->TypeToString(targetValue.mType).c_str()), dynCastExpr->mAsToken);

		auto typeInstance = targetValue.mType->ToTypeInstance();
		if (targetValue.mType->IsWrappableType())
			typeInstance = mModule->GetWrappedStructType(targetValue.mType);

		bool matches = (targetValue.mType == targetType) || (mModule->mContext->mBfObjectType == targetType);

		if (targetType->IsNullable())
		{
			auto elementType = targetType->GetUnderlyingType();
			if (elementType == targetValue.mType)
			{
				// We match nullable element
				auto allocaInst = mModule->CreateAlloca(targetType);
				auto hasValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1); // has_value
				mModule->mBfIRBuilder->CreateStore(mModule->GetConstValue(1, boolType), hasValueAddr);
				hasValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 0); // value
				mModule->mBfIRBuilder->CreateStore(targetValue.mValue, hasValueAddr);
				mResult = BfTypedValue(allocaInst, targetType, true);
				_CheckResult();
				return;
			}
		}

		if (!matches)
			matches = mModule->TypeIsSubTypeOf(typeInstance, targetType->ToTypeInstance());

		if (matches)
			mResult = mModule->Cast(dynCastExpr, targetValue, targetType, BfCastFlags_Explicit);
		else if (targetType->IsNullable())
		{
			auto allocaInst = mModule->CreateAlloca(targetType);
			auto allocaBits = mModule->mBfIRBuilder->CreateBitCast(allocaInst, mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
			mModule->mBfIRBuilder->CreateMemSet(allocaBits, mModule->GetConstValue(0, mModule->GetPrimitiveType(BfTypeCode_Int8)),
				mModule->GetConstValue(targetType->mSize), targetType->mAlign);
			mResult = BfTypedValue(allocaInst, targetType, true);
		}
		else
			mResult = BfTypedValue(mModule->GetDefaultValue(targetType), targetType);
		_CheckResult();
		return;
	}

	if (targetType->IsNullable())
	{
		if (autoComplete != NULL)
		{
			mResult = mModule->GetDefaultTypedValue(targetType);
			return;
		}

		auto elementType = targetType->GetUnderlyingType();
		auto allocaInst = mModule->CreateAlloca(targetType);
		auto hasValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, elementType->IsValuelessType() ? 1 : 2); // has_value
		mModule->mBfIRBuilder->CreateStore(mModule->GetConstValue(0, boolType), hasValueAddr);

		auto objectType = mModule->mContext->mBfObjectType;

		auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();
		auto matchBB = mModule->mBfIRBuilder->CreateBlock("as.match");
		auto endBB = mModule->mBfIRBuilder->CreateBlock("as.end");

		mModule->EmitDynamicCastCheck(targetValue, elementType, matchBB, endBB);

		BfBoxedType* boxedType = mModule->CreateBoxedType(elementType);

		mModule->AddBasicBlock(matchBB);
		if (elementType->IsValuelessType())
		{
			auto hasValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1); // has_value
			mModule->mBfIRBuilder->CreateStore(mModule->GetConstValue(1, boolType), hasValueAddr);
		}
		else
		{
			auto hasValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 2); // has_value
			mModule->mBfIRBuilder->CreateStore(mModule->GetConstValue(1, boolType), hasValueAddr);
			auto nullableValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1); // value
			auto srcBoxedType = mModule->mBfIRBuilder->CreateBitCast(targetValue.mValue, mModule->mBfIRBuilder->MapType(boxedType, BfIRPopulateType_Full));
			auto boxedValueAddr = mModule->mBfIRBuilder->CreateInBoundsGEP(srcBoxedType, 0, 1); // mValue
			auto boxedValue = mModule->mBfIRBuilder->CreateLoad(boxedValueAddr);
			mModule->mBfIRBuilder->CreateStore(boxedValue, nullableValueAddr);
		}
		mModule->mBfIRBuilder->CreateBr(endBB);

		mModule->AddBasicBlock(endBB);
		mResult = BfTypedValue(allocaInst, targetType, true);
		_CheckResult();
		return;
	}

	targetValue = mModule->LoadValue(targetValue);

	if (targetType->IsValueType())
	{
		mModule->Fail("Invalid dynamic cast type", dynCastExpr->mTypeRef);
		return;
	}

	BfTypeInstance* srcTypeInstance = targetValue.mType->ToTypeInstance();
	BfTypeInstance* targetTypeInstance = targetType->ToTypeInstance();

	if (mModule->TypeIsSubTypeOf(srcTypeInstance, targetTypeInstance))
	{
		// We don't give this warning when we have wasGenericParmType set because that indicates we had a generic type constraint,
		//  and a type constraint infers that the ACTUAL type used will be equal to or derived from that type and therefore
		//  it may be a "necessary cast" indeed
		if ((!wasGenericParamType) && (mModule->mCurMethodState->mMixinState == NULL))
		{
			if (srcTypeInstance == targetType)
				mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary cast, the value is already type '%s'",
					mModule->TypeToString(srcTypeInstance).c_str()), dynCastExpr->mAsToken);
			else
				mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary cast, '%s' is a subtype of '%s'",
					mModule->TypeToString(srcTypeInstance).c_str(), mModule->TypeToString(targetType).c_str()), dynCastExpr->mAsToken);
		}

		auto castedValue = mModule->mBfIRBuilder->CreateBitCast(targetValue.mValue, mModule->mBfIRBuilder->MapType(targetTypeInstance));
		mResult = BfTypedValue(castedValue, targetTypeInstance);
		_CheckResult();
		return;
	}
	else if ((!targetType->IsInterface()) && (!mModule->TypeIsSubTypeOf(targetTypeInstance, srcTypeInstance)))
	{
		if (!mModule->IsInSpecializedSection())
		{
			mModule->Fail(StrFormat("Cannot convert type '%s' to '%s' via any conversion",
				mModule->TypeToString(targetValue.mType).c_str(), mModule->TypeToString(targetTypeInstance).c_str()), dynCastExpr->mAsToken);
		}
		mResult = mModule->GetDefaultTypedValue(targetType);
		return;
	}

	if (autoComplete != NULL)
	{
		mResult = mModule->GetDefaultTypedValue(targetType, false, BfDefaultValueKind_Addr);
		_CheckResult();
		return;
	}

	auto irb = mModule->mBfIRBuilder;
	auto objectType = mModule->mContext->mBfObjectType;
	mModule->PopulateType(objectType, BfPopulateType_Full);

	auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();
	auto endBB = mModule->mBfIRBuilder->CreateBlock("as.end");
	auto matchBlock = irb->CreateBlock("as.match");

	BfIRValue targetVal = mModule->CreateAlloca(targetType);
	irb->CreateAlignedStore(irb->CreateConstNull(irb->MapType(targetType)), targetVal, targetType->mAlign);

	mModule->EmitDynamicCastCheck(targetValue, targetType, matchBlock, endBB);

	mModule->AddBasicBlock(matchBlock);
	BfIRValue castedCallResult = mModule->mBfIRBuilder->CreateBitCast(targetValue.mValue, mModule->mBfIRBuilder->MapType(targetType));
	irb->CreateAlignedStore(castedCallResult, targetVal, targetValue.mType->mAlign);
	irb->CreateBr(endBB);

	mModule->AddBasicBlock(endBB);
	mResult = BfTypedValue(irb->CreateAlignedLoad(targetVal, targetType->mAlign), targetType);
	_CheckResult();
}

void BfExprEvaluator::Visit(BfCastExpression* castExpr)
{
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (castExpr->mTypeRef != NULL))
	{
		// 'mayBeIdentifier' because this may be a misidentified cast - it could be a parenthesized expression
		autoComplete->CheckTypeRef(castExpr->mTypeRef, true, true);
	}

	BfType* resolvedType = NULL;

	if ((BfNodeDynCastExact<BfDotTypeReference>(castExpr->mTypeRef) != NULL) && (mExpectingType != NULL))
	{
		//mModule->SetElementType(castExpr->mTypeRef, BfSourceElementType_TypeRef);
		resolvedType = mExpectingType;
	}
	else
		resolvedType = ResolveTypeRef(castExpr->mTypeRef);
	if (resolvedType != NULL)
		mModule->AddDependency(resolvedType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
	// If resolvedType is NULL then that's okay- we just leave the following expression uncasted

	if (castExpr->mExpression == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	auto exprFlags = (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_NoAutoComplete) | BfEvalExprFlags_ExplicitCast);
	mResult = mModule->CreateValueFromExpression(castExpr->mExpression, resolvedType, exprFlags);
}

bool BfExprEvaluator::IsExactMethodMatch(BfMethodInstance* methodA, BfMethodInstance* methodB, bool ignoreImplicitParams)
{
	if (methodA->mReturnType != methodB->mReturnType)
		return false;
	int implicitParamCountA = methodA->GetImplicitParamCount();
	if (methodA->HasExplicitThis())
		implicitParamCountA++;
	int implicitParamCountB = methodB->GetImplicitParamCount();
	if (methodB->HasExplicitThis())
		implicitParamCountB++;

	if (methodA->GetParamCount() - implicitParamCountA != methodB->GetParamCount() - implicitParamCountB)
		return false;
	for (int i = 0; i < (int)methodA->GetParamCount() - implicitParamCountA; i++)
	{
		auto paramA = methodA->GetParamType(i + implicitParamCountA);
		auto paramB = methodB->GetParamType(i + implicitParamCountB);
		if (paramA != paramB)
			return false;
	}
	return true;
}

void BfExprEvaluator::ConstResolve(BfExpression* expr)
{
	BfConstResolver constResolver(mModule);
	constResolver.Resolve(expr);
	mResult = constResolver.mResult;
}

BfTypeInstance* BfExprEvaluator::VerifyBaseDelegateType(BfTypeInstance* baseDelegateType)
{
	mModule->PopulateType(baseDelegateType, BfPopulateType_DataAndMethods);
	if (baseDelegateType->mFieldInstances.size() != 2)
	{
		mModule->AssertErrorState();
		return NULL;
	}
	return baseDelegateType;
}

bool BfExprEvaluator::CanBindDelegate(BfDelegateBindExpression* delegateBindExpr, BfMethodInstance** boundMethod, BfType* origMethodExpectingType, BfTypeVector* methodGenericArgumentsSubstitute)
{
	if ((mExpectingType == NULL) && (origMethodExpectingType == NULL))
	{
		return false;
	}

	bool isGenericMatch = mExpectingType == NULL;
	auto expectingType = mExpectingType;
	if (expectingType == NULL)
		expectingType = origMethodExpectingType;

	auto typeInstance = expectingType->ToTypeInstance();
	if ((typeInstance == NULL) ||
		((!typeInstance->mTypeDef->mIsDelegate) && (!typeInstance->mTypeDef->mIsFunction)))
		return false;

	mModule->PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	auto methodInstance = mModule->GetRawMethodInstanceAtIdx(typeInstance, 0, "Invoke");
	if (methodInstance == NULL)
	{
		BF_DBG_FATAL("Invoke not found");
		return false;
	}

	if (delegateBindExpr->mTarget == NULL)
		return false;

	BfAutoParentNodeEntry autoParentNodeEntry(mModule, delegateBindExpr);
	SizedArray<BfTypedValueExpression, 4> typedValueExprs;
	typedValueExprs.resize(methodInstance->GetParamCount());

	SizedArray<BfExpression*, 4> args;
	args.resize(methodInstance->GetParamCount());

	auto _FixType = [&](BfType* type)
	{
		if (!isGenericMatch)
			return type;
		auto fixedType = mModule->ResolveGenericType(type, NULL, methodGenericArgumentsSubstitute, mModule->mCurTypeInstance);
		if (fixedType != NULL)
			return fixedType;
		return (BfType*)mModule->GetPrimitiveType(BfTypeCode_Var);
	};

	auto _TypeMatches = [&](BfType* lhs, BfType* rhs)
	{
		if (lhs == rhs)
			return true;
		return lhs->IsVar();
	};

	for (int i = 0; i < (int) methodInstance->GetParamCount(); i++)
	{
		auto typedValueExpr = &typedValueExprs[i];
		typedValueExpr->mTypedValue.mValue = BfIRValue(BfIRValueFlags_Value, -1);
		typedValueExpr->mTypedValue.mType = _FixType(methodInstance->GetParamType(i));
		typedValueExpr->mRefNode = NULL;
		args[i] = typedValueExpr;
	}

	BfMethodGenericArguments methodGenericArgs;
	if (delegateBindExpr->mGenericArgs != NULL)
		methodGenericArgs.mArguments = &delegateBindExpr->mGenericArgs->mGenericArgs;

	BfFunctionBindResult bindResult;
	bindResult.mSkipMutCheck = true; // Allow operating on copies
	bindResult.mBindType = expectingType;
	mFunctionBindResult = &bindResult;
	SetAndRestoreValue<bool> ignoreError(mModule->mIgnoreErrors, true);
	DoInvocation(delegateBindExpr->mTarget, delegateBindExpr, args, methodGenericArgs);
	mFunctionBindResult = NULL;
	if (bindResult.mMethodInstance == NULL)
		return false;
	if (boundMethod != NULL)
		*boundMethod = bindResult.mMethodInstance;

	auto matchedMethod = bindResult.mMethodInstance;

	if (!_TypeMatches(_FixType(methodInstance->mReturnType), matchedMethod->mReturnType))
		return false;

	int implicitParamCountA = methodInstance->GetImplicitParamCount();
	int implicitParamCountB = matchedMethod->GetImplicitParamCount();

	if (methodInstance->GetParamCount() - implicitParamCountA != matchedMethod->GetParamCount() - implicitParamCountB)
		return false;
	for (int i = 0; i < (int)methodInstance->GetParamCount() - implicitParamCountA; i++)
	{
		auto paramA = _FixType(methodInstance->GetParamType(i + implicitParamCountA));
		auto paramB = _FixType(matchedMethod->GetParamType(i + implicitParamCountB));
		if (!_TypeMatches(paramA, paramB))
			return false;
	}
	return true;
}

BfTypedValue BfExprEvaluator::DoImplicitArgCapture(BfAstNode* refNode, BfIdentifierNode* identifierNode, int shadowIdx)
{
	String findName = identifierNode->ToString();

	if (mModule->mCurMethodState != NULL)
	{
		auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();

		auto checkMethodState = mModule->mCurMethodState;
		bool isMixinOuterVariablePass = false;

		int shadowSkip = shadowIdx;

		while (checkMethodState != NULL)
		{
			BP_ZONE("LookupIdentifier:DoImplicitArgCapture");

			BfTypeInstance* closureTypeInst = NULL;
			BfClosureInstanceInfo* closureInstanceInfo = NULL;
			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mClosureType != NULL) && (!checkMethodState->mClosureState->mCapturing))
			{
				closureTypeInst = mModule->mCurMethodState->mClosureState->mClosureType;
			}

			BfLocalVarEntry* entry;
			if (checkMethodState->mLocalVarSet.TryGetWith<StringImpl&>(findName, &entry))
			{
				auto varDecl = entry->mLocalVar;

				while (varDecl != NULL)
				{
					if (varDecl->mNameNode == identifierNode)
					{
						if (shadowSkip > 0)
						{
							shadowSkip--;
						}
						else
						{
							BfTypedValue localResult = LoadLocal(varDecl);
							if (!isMixinOuterVariablePass)
							{
								mResultLocalVar = varDecl;
								mResultFieldInstance = NULL;
								mResultLocalVarRefNode = identifierNode;
							}
							return localResult;
						}
					}

					varDecl = varDecl->mShadowedLocal;
				}
			}

			// Check for the captured locals.  It's important we do it here so we get local-first precedence still
			if (closureTypeInst != NULL)
			{
				closureTypeInst->mTypeDef->PopulateMemberSets();
				BfMemberSetEntry* memberSetEntry = NULL;
				if (closureTypeInst->mTypeDef->mFieldSet.TryGetWith(findName, &memberSetEntry))
				{
					auto fieldDef = (BfFieldDef*)memberSetEntry->mMemberDef;
					while (fieldDef != NULL)
					{
						BfIdentifierNode* fieldNameNode = NULL;
						if (fieldDef->mIdx < (int)checkMethodState->mClosureState->mClosureInstanceInfo->mCaptureEntries.size())
							fieldNameNode = checkMethodState->mClosureState->mClosureInstanceInfo->mCaptureEntries[fieldDef->mIdx].mNameNode;

						if (fieldNameNode == identifierNode)
						{
							auto& field = closureTypeInst->mFieldInstances[fieldDef->mIdx];
							if (!field.mResolvedType->IsValuelessType())
							{
								if (mModule->mCurMethodState->mClosureState->mCapturing)
								{
									mModule->mCurMethodState->mClosureState->mReferencedOuterClosureMembers.Add(&field);
									return mModule->GetDefaultTypedValue(field.mResolvedType);
								}

								auto localVar = mModule->mCurMethodState->mLocals[0];
								auto thisValue = localVar->mValue;
								mModule->mBfIRBuilder->PopulateType(localVar->mResolvedType);
								BfTypedValue result = BfTypedValue(mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue, 0, field.mDataIdx), field.mResolvedType, true);
								if (field.mResolvedType->IsRef())
								{
									auto refType = (BfRefType*)field.mResolvedType;
									auto underlyingType = refType->GetUnderlyingType();
									result = BfTypedValue(mModule->mBfIRBuilder->CreateAlignedLoad(result.mValue, underlyingType->mAlign), underlyingType, true);
								}
								else if (fieldDef->mIsReadOnly)
									result = mModule->LoadValue(result);

								mResultLocalVar = localVar;
								mResultFieldInstance = &field;
								mResultLocalVarField = -(field.mMergedDataIdx + 1);

								return result;
							}
						}

						fieldDef = fieldDef->mNextWithSameName;
					}
				}
			}

			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mCapturing) /*&& (checkMethodState->mClosureState->mIsLocalMethod)*/)
			{
				checkMethodState = checkMethodState->mPrevMethodState;
				continue;
			}

			// Allow local mixin to see outside variables during its processing -- since we don't actually "capture" those into params
			bool isLocalMixinProcessing = false;
			if ((checkMethodState->mClosureState != NULL) && (!checkMethodState->mClosureState->mCapturing) && (closureTypeInst == NULL) &&
				(mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Mixin))
				isLocalMixinProcessing = true;
			if (!isLocalMixinProcessing)
				break;

			isMixinOuterVariablePass = true;
			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	return BfTypedValue();
}

BfTypedValue BfExprEvaluator::DoImplicitArgCapture(BfAstNode* refNode, BfMethodInstance* methodInstance, int paramIdx, bool& failed, BfImplicitParamKind paramKind, const BfTypedValue& methodRefTarget)
{
	if ((methodRefTarget) && (methodRefTarget.mValue.mId != BfIRValue::ID_IMPLICIT))
	{
		if (methodRefTarget.mType->IsMethodRef())
		{
			BfMethodRefType* methodRefType = (BfMethodRefType*)methodRefTarget.mType;
			BfMethodInstance* methodRefMethodInst = methodRefType->mMethodRef;

			BF_ASSERT(methodRefMethodInst == methodInstance);

			auto paramType = methodInstance->GetParamType(paramIdx);

			int dataIdx = methodRefType->GetDataIdxFromParamIdx(paramIdx);
			if (dataIdx == -1)
			{
				BF_ASSERT(paramType->IsValuelessType());
				return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), paramType);
			}

			if (methodRefTarget.IsSplat())
			{
				if (methodRefType->WantsDataPassedAsSplat(dataIdx))
				{
					BF_ASSERT(paramIdx == -1);
					return BfTypedValue(methodRefTarget.mValue, paramType, BfTypedValueKind_SplatHead);
				}
				else
				{
					int splatIdx = dataIdx;
					if (dataIdx > 0)
					{
						if (methodRefType->WantsDataPassedAsSplat(0))
							splatIdx += methodRefType->GetCaptureType(0)->GetSplatCount() - 1;
					}

					bool isAddr = false;
					BfIRValue value = mModule->ExtractSplatValue(methodRefTarget, splatIdx, paramType, &isAddr);
					// We moved the composite load from ExtractSplatValue to here. Hopefully this is correct.

					// in LLVM backend we get a direct 'arg' back, in Beef we get backing for a pointer to the struct (and thus need to load)
//  					if ((paramType->IsComposite()) && (mModule->IsTargetingBeefBackend()))
//  						value = mModule->mBfIRBuilder->CreateLoad(value);

					auto lookupVal = BfTypedValue(value, paramType, isAddr);
					if ((isAddr) && (!lookupVal.mType->IsComposite()))
						lookupVal = mModule->LoadValue(lookupVal);
					return lookupVal;
				}
			}
			if ((paramType->IsComposite()) && (methodRefTarget.IsAddr()))
				return BfTypedValue(mModule->mBfIRBuilder->CreateInBoundsGEP(methodRefTarget.mValue, 0, dataIdx), paramType, true);
			return BfTypedValue(mModule->ExtractValue(methodRefTarget, dataIdx), paramType);
		}
	}

	// 'Default' implicit arg lookup, by identifier.  May match field (for lambda), or local variable
	if (paramKind == BfImplicitParamKind_General)
	{
		if (paramIdx == -1)
		{
			if (auto delegateBindExpr = BfNodeDynCast<BfDelegateBindExpression>(refNode))
			{
				BfAstNode* thisNode = NULL;
				BfTypedValue thisValue;

				if (auto memberReferenceExpr = BfNodeDynCast<BfMemberReferenceExpression>(delegateBindExpr->mTarget))
					thisNode = memberReferenceExpr->mTarget;
				else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(delegateBindExpr->mTarget))
					thisNode = qualifiedNameNode->mLeft;
				else if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(delegateBindExpr->mTarget))
				{
					// Implicit
					thisValue = mModule->GetThis();
				}

				if (auto thisExpr = BfNodeDynCast<BfExpression>(thisNode))
				{
					thisValue = mModule->CreateValueFromExpression(thisExpr);
					if (!thisValue)
						return BfTypedValue();
				}

				if (thisValue)
				{
					//TODO: handle 'mut', throw error if trying to capture from a non-mut, etc...
					return thisValue;
				}
			}
		}

		String captureName = methodInstance->GetParamName(paramIdx);
		BfIdentifierNode* identifierNode = methodInstance->GetParamNameNode(paramIdx);
		BfTypedValue lookupVal;

		if (identifierNode != NULL)
		{
			lookupVal = DoImplicitArgCapture(refNode, identifierNode, 0);
		}
		else
		{
			lookupVal = LookupIdentifier(NULL, captureName);
		}
		if (lookupVal)
		{
			auto paramType = methodInstance->GetParamType(paramIdx);
			if (paramType->IsRef())
			{
				auto refType = (BfRefType*)paramType;

				if (mResultLocalVar != NULL)
				{
					// When we are capturing, we need to note that we require capturing by reference here
					auto localVar = mResultLocalVar;
					localVar->mWrittenToId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
				}

				bool isValid = false;
				if ((refType->mRefKind == BfRefType::RefKind_Mut) && (lookupVal.mKind == BfTypedValueKind_MutableValue))
					isValid = true;
				if ((lookupVal.mType->IsRef()) || (lookupVal.IsAddr()))
					isValid = true;
				if (!isValid)
				{
					// Is there another way this can fail than to be in a lambda?
					auto error = mModule->Fail(StrFormat("Method '%s' requires that '%s' be captured by reference. Consider adding by-reference capture specifier [&] to lambda.",
						mModule->MethodToString(methodInstance).c_str(), captureName.c_str()), refNode, true);
					if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
					failed = true;
				}
			}
			else
			{
				if (lookupVal.mType->IsRef())
					lookupVal = mModule->RemoveRef(lookupVal);
				if (!lookupVal.mType->IsComposite())
					lookupVal = mModule->LoadValue(lookupVal);
			}

			return lookupVal;
		}
		else
		{
			mModule->Fail(StrFormat("Failed to lookup implicit capture '%s'", captureName.c_str()), refNode, true);
			failed = true;
			return BfTypedValue();
		}
	}

	return BfTypedValue();
}

void BfExprEvaluator::Visit(BfDelegateBindExpression* delegateBindExpr)
{
	BfAutoParentNodeEntry autoParentNodeEntry(mModule, delegateBindExpr);

 	if (mExpectingType == NULL)
	{
		mModule->Fail("Cannot infer delegate type", delegateBindExpr);
		return;
	}

	BfTokenNode* newToken = NULL;
	BfAllocTarget allocTarget;
	ResolveAllocTarget(allocTarget, delegateBindExpr->mNewToken, newToken);

	SizedArray<BfTypedValueExpression, 4> typedValueExprs;
	SizedArray<BfExpression*, 4> args;

	BfTypeInstance* delegateTypeInstance = NULL;
	BfMethodInstance* methodInstance = NULL;

	const char* bindTypeName = NULL;
	bool isMethodRefMatch = false;
	if (mExpectingType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)mExpectingType;

		BF_ASSERT(delegateBindExpr->mNewToken == NULL);
		methodInstance = methodRefType->mMethodRef;
		isMethodRefMatch = true;
	}
	else
	{
		if (mExpectingType->IsGenericParam())
		{
			auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)mExpectingType);
			if ((genericParamInstance->mTypeConstraint != NULL) && (genericParamInstance->mTypeConstraint->IsDelegate()))
			{
				delegateTypeInstance = genericParamInstance->mTypeConstraint->ToTypeInstance();
			}
		}
		else
			delegateTypeInstance = mExpectingType->ToTypeInstance();

		if ((delegateTypeInstance == NULL) ||
			((!delegateTypeInstance->IsDelegate()) && (!delegateTypeInstance->IsFunction())))
		{
			mModule->Fail(StrFormat("Type '%s' cannot be used for method binding. Only delegate or function types are allowed.", mModule->TypeToString(mExpectingType).c_str()), delegateBindExpr);
			return;
		}

		bindTypeName = (delegateTypeInstance->IsDelegate()) ? "delegate" : "function";

		mModule->PopulateType(delegateTypeInstance, BfPopulateType_DataAndMethods);

		methodInstance = mModule->GetRawMethodInstanceAtIdx(delegateTypeInstance, 0, "Invoke");
		if (methodInstance == NULL)
		{
			BF_DBG_FATAL("Invoke not found");
			return;
		}

		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(delegateBindExpr->mNewToken))
		{
			if (delegateTypeInstance->IsFunction())
				mModule->Fail("Function bindings are direct assignments, allocation specifier is not applicable", delegateBindExpr->mNewToken);
			else if (tokenNode->GetToken() == BfToken_Append)
			{
				mModule->Fail("Append allocation on delegate bind not supported", delegateBindExpr->mNewToken);
			}
		}
	}

	int paramOffset = methodInstance->HasExplicitThis() ? 1 : 0;
	typedValueExprs.resize(methodInstance->GetParamCount() - paramOffset);
	args.resize(methodInstance->GetParamCount() - paramOffset);

	for (int i = 0; i < (int)methodInstance->GetParamCount() - paramOffset; i++)
	{
		auto typedValueExpr = &typedValueExprs[i];
		typedValueExpr->mTypedValue.mValue = BfIRValue(BfIRValueFlags_Value, -1);
		typedValueExpr->mTypedValue.mType = methodInstance->GetParamType(i + paramOffset);
		typedValueExpr->mRefNode = NULL;
		args[i] = typedValueExpr;
	}

	BfMethodGenericArguments methodGenericArgs;
	if (delegateBindExpr->mGenericArgs != NULL)
		methodGenericArgs.mArguments = &delegateBindExpr->mGenericArgs->mGenericArgs;

	if (delegateBindExpr->mTarget == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		SetAndRestoreValue<bool> prevForceAllowNonStatic(autoComplete->mForceAllowNonStatic, methodInstance->mMethodDef->mHasExplicitThis);
		GetAutoComplete()->CheckNode(delegateBindExpr->mTarget, true);
	}

	if ((!delegateBindExpr->mTarget->IsA<BfIdentifierNode>()) &&
		(!delegateBindExpr->mTarget->IsA<BfMemberReferenceExpression>()))
	{
		mModule->Fail(StrFormat("Invalid %s binding target, %s can only bind to method references. Consider wrapping expression in a lambda.", bindTypeName, bindTypeName), delegateBindExpr->mTarget);
		mModule->CreateValueFromExpression(delegateBindExpr->mTarget);
		return;
	}

	BfFunctionBindResult bindResult;
	bindResult.mSkipMutCheck = true; // Allow operating on copies
	bindResult.mBindType = delegateTypeInstance;
	//
	{
		SetAndRestoreValue<BfType*> prevExpectingType(mExpectingType, methodInstance->mReturnType);
		mFunctionBindResult = &bindResult;
		DoInvocation(delegateBindExpr->mTarget, delegateBindExpr, args, methodGenericArgs);
		mFunctionBindResult = NULL;
	}

	SetMethodElementType(delegateBindExpr->mTarget);

	if (bindResult.mMethodInstance == NULL)
	{
		if ((mResult) && (IsVar(mResult.mType)))
			return;
		mResult = BfTypedValue();
		return;
	}

	auto bindMethodInstance = bindResult.mMethodInstance;
	if (isMethodRefMatch)
	{
		// WTF- this was always false, what was it supposed to catch?
// 		if (bindMethodInstance != bindResult.mMethodInstance)
// 		{
// 			mResult = BfTypedValue();
// 			return;
// 		}
	}
	else
	{
		bool isExactMethodMatch = IsExactMethodMatch(methodInstance, bindMethodInstance, true);
		if ((mExpectingType != NULL) && (mExpectingType->IsFunction()) && (methodInstance->mMethodDef->mIsMutating != bindMethodInstance->mMethodDef->mIsMutating))
			isExactMethodMatch = false;

		if (!isExactMethodMatch)
		{
			if (bindResult.mCheckedMultipleMethods)
			{
				mModule->Fail(StrFormat("No overload for '%s' matches %s '%s'", bindMethodInstance->mMethodDef->mName.c_str(), bindTypeName,
					mModule->TypeToString(delegateTypeInstance).c_str()), delegateBindExpr->mTarget);
			}
			else
			{
				mModule->Fail(StrFormat("Method '%s' does not match %s '%s'", mModule->MethodToString(bindMethodInstance, (BfMethodNameFlags)(BfMethodNameFlag_ResolveGenericParamNames | BfMethodNameFlag_IncludeReturnType | BfMethodNameFlag_IncludeMut)).c_str(), bindTypeName,
					mModule->TypeToString(delegateTypeInstance).c_str()), delegateBindExpr->mTarget);
			}
			mResult = BfTypedValue();
			return;
		}
	}

	bool isDirectFunction = false;
	if ((bindResult.mMethodInstance->GetOwner()->IsFunction()) && (bindResult.mFunc))
		isDirectFunction = true;

	if (bindMethodInstance->mIsIntrinsic)
	{
		mModule->Fail(StrFormat("Method '%s' is an intrinsic and therefore cannot be used as a method binding target. Intrinsics have no addresses.", mModule->MethodToString(bindMethodInstance).c_str()), delegateBindExpr->mTarget);
		mResult = BfTypedValue();
		return;
	}

	bool hasIncompatibleCallingConventions = !mModule->mSystem->IsCompatibleCallingConvention(methodInstance->mCallingConvention, bindMethodInstance->mCallingConvention);

	auto _GetInvokeMethodName = [&]()
	{
		StringT<512> methodName = "Invoke$";
		methodName += mModule->mCurMethodInstance->mMethodDef->mName;

		int prevSepPos = (int)methodName.LastIndexOf('$');
		if (prevSepPos > 6)
		{
			methodName.RemoveToEnd(prevSepPos);
		}

		auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
		HashContext hashCtx;

		if (mModule->mCurMethodInstance->mMethodDef->mDeclaringType->mPartialIdx != -1)
			hashCtx.Mixin(mModule->mCurMethodInstance->mMethodDef->mDeclaringType->mPartialIdx);

		if (delegateBindExpr->mFatArrowToken != NULL)
		{
			hashCtx.Mixin(delegateBindExpr->mFatArrowToken->GetStartCharId());
		}
		if (rootMethodState->mMethodInstance->mMethodInfoEx != NULL)
		{
			for (auto methodGenericArg : rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments)
			{
				StringT<128> genericTypeName;
				BfMangler::Mangle(genericTypeName, mModule->mCompiler->GetMangleKind(), methodGenericArg);
				hashCtx.MixinStr(genericTypeName);
			}
		}

		Val128 hashVal = hashCtx.Finish128();

		methodName += '$';
		methodName += BfTypeUtils::HashEncode64(hashVal.mLow);

		StringT<512> mangledName;
		BfMangler::MangleMethodName(mangledName, mModule->mCompiler->GetMangleKind(), mModule->mCurTypeInstance, methodName);
		return mangledName;
	};

	if ((delegateBindExpr->mNewToken == NULL) || (delegateTypeInstance->IsFunction()))
	{
		if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mHotDataReferenceBuilder != NULL))
		{
			BF_ASSERT(bindResult.mMethodInstance->mHotMethod != NULL);
			mModule->mCurMethodState->mHotDataReferenceBuilder->mFunctionPtrs.Add(bindResult.mMethodInstance->mHotMethod);
		}

		if (isDirectFunction)
		{
			//if ((delegateTypeInstance != NULL) && (delegateTypeInstance->IsFunction()))
			if (mExpectingType->IsFunction())
			{
				auto intPtrVal = mModule->mBfIRBuilder->CreatePtrToInt(bindResult.mFunc, BfTypeCode_IntPtr);
				mResult = BfTypedValue(intPtrVal, mExpectingType);
				return;
			}
		}

		if (mExpectingType->IsFunction())
		{
			BfIRValue result;
			if ((hasIncompatibleCallingConventions) && (mModule->HasExecutedOutput()))
			{
				//
				{
					SetAndRestoreValue<bool> prevIgnore(mModule->mBfIRBuilder->mIgnoreWrites, true);
					result = mModule->CastToFunction(delegateBindExpr->mTarget, bindResult.mOrigTarget, bindResult.mMethodInstance, mExpectingType);
				}

				if (result)
				{
					String methodName = _GetInvokeMethodName();

					SizedArray<BfIRType, 8> irParamTypes;
					BfIRType irReturnType;
					methodInstance->GetIRFunctionInfo(mModule, irReturnType, irParamTypes);

					int thisFuncParamIdx = methodInstance->GetThisIdx();
					int thisBindParamIdx = methodInstance->GetThisIdx();

					auto prevActiveFunction = mModule->mBfIRBuilder->GetActiveFunction();
					auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
					mModule->mBfIRBuilder->SaveDebugLocation();

					auto funcType = mModule->mBfIRBuilder->CreateFunctionType(irReturnType, irParamTypes);
					auto funcValue = mModule->mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, methodName);

					auto srcCallingConv = mModule->GetIRCallingConvention(methodInstance);
					mModule->mBfIRBuilder->SetFuncCallingConv(funcValue, srcCallingConv);

					mModule->mBfIRBuilder->SetActiveFunction(funcValue);
					auto entryBlock = mModule->mBfIRBuilder->CreateBlock("entry", true);
					mModule->mBfIRBuilder->SetInsertPoint(entryBlock);

					SizedArray<BfIRValue, 8> irArgs;
					for (int paramIdx = 0; paramIdx < irParamTypes.size(); paramIdx++)
					{
						irArgs.push_back(mModule->mBfIRBuilder->GetArgument(paramIdx));
					}

					auto bindFuncVal = bindResult.mFunc;
					if ((mModule->mCompiler->mOptions.mAllowHotSwapping) && (!mModule->mIsComptimeModule))
						bindFuncVal = mModule->mBfIRBuilder->RemapBindFunction(bindFuncVal);
					auto callResult = mModule->mBfIRBuilder->CreateCall(bindFuncVal, irArgs);
					auto destCallingConv = mModule->GetIRCallingConvention(bindMethodInstance);
					if (destCallingConv != BfIRCallingConv_CDecl)
						mModule->mBfIRBuilder->SetCallCallingConv(callResult, destCallingConv);
					if (methodInstance->mReturnType->IsValuelessType())
						mModule->mBfIRBuilder->CreateRetVoid();
					else
						mModule->mBfIRBuilder->CreateRet(callResult);

					mModule->mBfIRBuilder->SetActiveFunction(prevActiveFunction);
					mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
					mModule->mBfIRBuilder->RestoreDebugLocation();

					result = mModule->mBfIRBuilder->CreatePtrToInt(funcValue, BfTypeCode_IntPtr);
				}
			}
			else
			{
				if ((bindResult.mOrigTarget) && (bindResult.mOrigTarget.mType->IsGenericParam()) && (bindResult.mMethodInstance->GetOwner()->IsInterface()))
				{
					bool matching = true;
					if (methodInstance->HasExplicitThis())
					{
						auto thisType = methodInstance->GetParamType(0);

						if (thisType->IsPointer())
							thisType = thisType->GetUnderlyingType();
						if (thisType->IsRef())
							thisType = thisType->GetUnderlyingType();

						matching = thisType == bindResult.mOrigTarget.mType;
					}

					if (matching)
					{
						mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), mExpectingType);
						return;
					}
				}
				result = mModule->CastToFunction(delegateBindExpr->mTarget, bindResult.mOrigTarget, bindResult.mMethodInstance, mExpectingType, BfCastFlags_None, bindResult.mFunc);
			}
			if (result)
				mResult = BfTypedValue(result, mExpectingType);
			return;
		}

		if (bindResult.mMethodInstance->mDisallowCalling)
		{
			BF_ASSERT(mModule->mBfIRBuilder->mIgnoreWrites);
			mResult = mModule->GetDefaultTypedValue(mExpectingType, false, BfDefaultValueKind_Addr);
			return;
		}

		auto methodRefType = mModule->CreateMethodRefType(bindResult.mMethodInstance);
		mModule->AddDependency(methodRefType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
		mModule->AddCallDependency(bindResult.mMethodInstance);
		if ((bindResult.mOrigTarget) && (bindResult.mOrigTarget.mType->IsMethodRef()))
		{
			mResult = bindResult.mOrigTarget;
		}
		else
		{
			if ((bindResult.mMethodInstance->mMethodDef->mIsLocalMethod) || (!bindResult.mTarget))
				mResult = BfTypedValue(BfIRValue(BfIRValueFlags_Value, BfIRValue::ID_IMPLICIT), methodRefType);
			else
			{
				auto methodRefPtr = mModule->CreateAlloca(methodRefType, "bindResult");
				auto elemPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(methodRefPtr, 0, 0);

				BfTypedValue target;
				if (bindResult.mTarget.IsSplat())
					target = mModule->AggregateSplat(bindResult.mTarget, &bindResult.mIRArgs[0]);
				else
					target = bindResult.mTarget;

				mModule->mBfIRBuilder->CreateStore(target.mValue, elemPtr);

				mResult = BfTypedValue(methodRefPtr, methodRefType, true);
			}
		}
		return;
	}

	int implicitParamCount = bindMethodInstance->GetImplicitParamCount();

	BfTypeInstance* useTypeInstance = delegateTypeInstance;
	BfClosureType* closureTypeInst = NULL;

	auto origTarget = bindResult.mOrigTarget;
	auto target = bindResult.mTarget;

	BfTypedValue methodRefTarget;
	if ((bindResult.mOrigTarget) && (bindResult.mOrigTarget.mType->IsMethodRef()))
		methodRefTarget = bindResult.mOrigTarget;

	bool isStructTarget = (target) && (target.mType->IsStruct());
	bool bindCapturesThis = bindMethodInstance->HasThis() && !isStructTarget;
	bool needsSplat = (isStructTarget) && (!bindMethodInstance->mMethodDef->mIsMutating) && (bindMethodInstance->AllowsSplatting(-1));
	bool captureThisByValue = isStructTarget;
	if (bindMethodInstance->mMethodDef->mIsLocalMethod)
	{
		// Local method captures always capture 'this' by reference
		captureThisByValue = false;
	}

	if ((origTarget.mType != NULL) &&
		((origTarget.mType->IsRef()) || (origTarget.mType->IsPointer())))
	{
		captureThisByValue = false;
	}

	if (methodRefTarget)
		BF_ASSERT(methodRefTarget.mType->IsMethodRef());

	if (target.IsSplat())
		target = mModule->AggregateSplat(target, &bindResult.mIRArgs[0]);

	bool hasCaptures = false;

	// Do we need a special delegate type for this?
	if (((captureThisByValue) || (needsSplat) || (implicitParamCount > 0) /*|| (hasIncompatibleCallingConventions)*/) &&
		(mModule->HasExecutedOutput()))
	{
		hasCaptures = true;
		auto curProject = mModule->mCurTypeInstance->mTypeDef->mProject;

		if (captureThisByValue)
			target = mModule->LoadValue(target);

		String delegateTypeName = mModule->TypeToString(delegateTypeInstance);
		HashContext hashCtx;
		hashCtx.MixinStr(delegateTypeName);

		// We mix in the project for reasons described in the lambda binding handler
		hashCtx.MixinStr(curProject->mName);

		String structTargetTypeName;
		String structTargetName;

		// Implicit param separator
		for (int implicitParamIdx = bindMethodInstance->HasThis() ? - 1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
		{
			auto paramType = bindResult.mMethodInstance->GetParamType(implicitParamIdx);
			if ((implicitParamIdx == -1) && (captureThisByValue))
			{
				if (paramType->IsPointer())
					paramType = paramType->GetUnderlyingType();
			}
			structTargetTypeName = mModule->TypeToString(paramType);
			structTargetName = bindResult.mMethodInstance->GetParamName(implicitParamIdx);
			hashCtx.MixinStr(structTargetTypeName);
			hashCtx.MixinStr(structTargetName);
		}

		Val128 hash128 = hashCtx.Finish128();
		BfClosureType* checkClosureType = new BfClosureType(delegateTypeInstance, hash128);
		checkClosureType->mContext = mModule->mContext;
		checkClosureType->mBaseType = delegateTypeInstance;
		BfType* resolvedClosureType = mModule->ResolveType(checkClosureType, BfPopulateType_TypeDef);
		closureTypeInst = (BfClosureType*)resolvedClosureType;
		if (checkClosureType == resolvedClosureType)
		{
			// This is a new closure type
			closureTypeInst->Init(curProject);
			for (int implicitParamIdx = bindMethodInstance->HasThis() ? -1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
			{
				String fieldName = bindResult.mMethodInstance->GetParamName(implicitParamIdx);
				BfType* paramType = bindResult.mMethodInstance->GetParamType(implicitParamIdx);
				if ((implicitParamIdx == -1) && (captureThisByValue))
				{
					if (paramType->IsPointer())
						paramType = paramType->GetUnderlyingType();
				}

				if (fieldName == "this")
					fieldName = "__this";
				closureTypeInst->AddField(paramType, fieldName);
			}

			closureTypeInst->Finish();
			mModule->PopulateType(resolvedClosureType, BfPopulateType_Declaration);
		}
		else
		{
			// Already had this entry
			delete checkClosureType;
		}
		useTypeInstance = closureTypeInst;
	}
	mModule->PopulateType(useTypeInstance);

	if (delegateBindExpr->mTarget == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	mResult = BfTypedValue(mModule->AllocFromType(useTypeInstance, allocTarget, BfIRValue(), BfIRValue(), 0, BfAllocFlags_None), useTypeInstance);

	// Do we need specialized calling code for this?
	BfIRValue funcValue;
	if (((needsSplat) || (implicitParamCount > 0) || (hasIncompatibleCallingConventions)) &&
		(mModule->HasExecutedOutput()))
	{
		int fieldIdx = 0;
		for (int implicitParamIdx = bindMethodInstance->HasThis() ? -1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
		{
			auto fieldInst = &useTypeInstance->mFieldInstances[fieldIdx];
			int gepIdx = fieldInst->mDataIdx;

			auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, gepIdx);
			auto fieldType = bindMethodInstance->GetParamType(implicitParamIdx);
			if ((implicitParamIdx == -1) && (captureThisByValue))
			{
				if (fieldType->IsPointer())
					fieldType = fieldType->GetUnderlyingType();
			}

			bool failed = false;
			BfTypedValue lookupVal;
			if (implicitParamIdx == -1)
				lookupVal = target;
			else
				lookupVal = DoImplicitArgCapture(delegateBindExpr->mTarget, bindResult.mMethodInstance, implicitParamIdx, failed, BfImplicitParamKind_General, methodRefTarget);
			if (!lookupVal)
				continue;
			if ((fieldType->IsPointer()) && (lookupVal.mType != fieldType))
			{
				BF_ASSERT(fieldType->GetUnderlyingType() == lookupVal.mType);
				BF_ASSERT(lookupVal.IsAddr());
			}
			else if (!fieldType->IsRef())
				lookupVal = mModule->LoadOrAggregateValue(lookupVal);
			if (lookupVal)
				mModule->mBfIRBuilder->CreateStore(lookupVal.mValue, fieldPtr);

			fieldIdx++;
		}

		String methodName = _GetInvokeMethodName();

		SizedArray<BfIRType, 8> irParamTypes;
		BfIRType irReturnType;

		bool hasThis = false;
		if (hasCaptures)
		{
			hasThis = true;
			methodInstance->GetIRFunctionInfo(mModule, irReturnType, irParamTypes);
			int thisIdx = 0;
			if (GetStructRetIdx(methodInstance) == 0)
				thisIdx = 1;
			irParamTypes[thisIdx] = mModule->mBfIRBuilder->MapType(useTypeInstance);
		}
		else
		{
			BF_ASSERT(hasIncompatibleCallingConventions);
			bindMethodInstance->GetIRFunctionInfo(mModule, irReturnType, irParamTypes);
			hasThis = bindMethodInstance->HasThis();
		}

		auto prevActiveFunction = mModule->mBfIRBuilder->GetActiveFunction();
		auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
		mModule->mBfIRBuilder->SaveDebugLocation();

		auto funcType = mModule->mBfIRBuilder->CreateFunctionType(irReturnType, irParamTypes);
		funcValue = mModule->mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, methodName);

		if (GetStructRetIdx(methodInstance) != -1)
		{
			mModule->mBfIRBuilder->Func_AddAttribute(funcValue, GetStructRetIdx(methodInstance) + 1, BfIRAttribute_NoAlias);
			mModule->mBfIRBuilder->Func_AddAttribute(funcValue, GetStructRetIdx(methodInstance) + 1, BfIRAttribute_StructRet);
		}

		auto srcCallingConv = mModule->GetIRCallingConvention(methodInstance);
		if ((!hasThis) && (methodInstance->mCallingConvention == BfCallingConvention_Stdcall))
			srcCallingConv = BfIRCallingConv_StdCall;
		else if (methodInstance->mCallingConvention == BfCallingConvention_Fastcall)
			srcCallingConv = BfIRCallingConv_FastCall;
		mModule->mBfIRBuilder->SetFuncCallingConv(funcValue, srcCallingConv);

		mModule->mBfIRBuilder->SetActiveFunction(funcValue);
		auto entryBlock = mModule->mBfIRBuilder->CreateBlock("entry", true);
		mModule->mBfIRBuilder->SetInsertPoint(entryBlock);

		fieldIdx = 0;
		SizedArray<BfIRValue, 8> irArgs;

		int argIdx = 0;
		if (GetStructRetIdx(bindMethodInstance) == 0)
		{
			irArgs.push_back(mModule->mBfIRBuilder->GetArgument(GetStructRetIdx(methodInstance)));
			argIdx++;
		}

		for (int implicitParamIdx = bindMethodInstance->HasThis() ? -1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
		{
			auto fieldInst = &useTypeInstance->mFieldInstances[fieldIdx];
			int gepIdx = fieldInst->mDataIdx;
			auto fieldType = bindMethodInstance->GetParamType(implicitParamIdx);
			bool disableSplat = false;
			if ((implicitParamIdx == -1) && (captureThisByValue))
			{
				if (fieldType->IsPointer())
				{
					fieldType = fieldType->GetUnderlyingType();
					disableSplat = true;
				}
			}

			int thisIdx = 0;
			if (GetStructRetIdx(methodInstance) == 0)
				thisIdx = 1;
			auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mModule->mBfIRBuilder->GetArgument(thisIdx), 0, gepIdx);
			BfTypedValue typedVal(fieldPtr, fieldType, true);
			PushArg(typedVal, irArgs, disableSplat);
			fieldIdx++;
		}

		if (hasThis)
			argIdx++;

		if (GetStructRetIdx(bindMethodInstance) == 1)
		{
			irArgs.push_back(mModule->mBfIRBuilder->GetArgument(GetStructRetIdx(methodInstance)));
			argIdx++;
		}

		for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
		{
			auto paramType = methodInstance->GetParamType(paramIdx);
			if ((paramType->IsSplattable()) && (!IsComptime()))
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { irArgs.push_back(mModule->mBfIRBuilder->GetArgument(argIdx++)); }, paramType);
			}
			else if (!paramType->IsValuelessType())
			{
				irArgs.push_back(mModule->mBfIRBuilder->GetArgument(argIdx++));
			}
		}

		auto bindFuncVal = bindResult.mFunc;
		if ((mModule->mCompiler->mOptions.mAllowHotSwapping) && (!mModule->mIsComptimeModule))
			bindFuncVal = mModule->mBfIRBuilder->RemapBindFunction(bindFuncVal);
		auto callInst = mModule->mBfIRBuilder->CreateCall(bindFuncVal, irArgs);
		if (GetStructRetIdx(bindMethodInstance) != -1)
			mModule->mBfIRBuilder->Call_AddAttribute(callInst, GetStructRetIdx(bindMethodInstance) + 1, BfIRAttribute_StructRet);
		auto destCallingConv = mModule->GetIRCallingConvention(bindMethodInstance);
		if (destCallingConv != BfIRCallingConv_CDecl)
			mModule->mBfIRBuilder->SetCallCallingConv(callInst, destCallingConv);
		if ((methodInstance->mReturnType->IsValuelessType()) || (GetStructRetIdx(methodInstance) != -1))
		{
			mModule->mBfIRBuilder->CreateRetVoid();
		}
		else
		{
			mModule->mBfIRBuilder->CreateRet(callInst);
		}

		mModule->mBfIRBuilder->SetActiveFunction(prevActiveFunction);
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
		mModule->mBfIRBuilder->RestoreDebugLocation();
	}
 	else if ((closureTypeInst != NULL) && (captureThisByValue))
 	{
 		// When we need to aggregrate a splat for a target, we just point out delegate's mTarget to inside ourselves where we aggregated the value
 		auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, 1);
 		target = mModule->LoadValue(target);
 		mModule->mBfIRBuilder->CreateStore(target.mValue, fieldPtr);
 		target = BfTypedValue(fieldPtr, target.mType, true);
 	}

	BfResolvedArgs resolvedArgs;
	MatchConstructor(delegateBindExpr, delegateBindExpr, mResult, useTypeInstance, resolvedArgs, false, false);

	auto baseDelegateType = VerifyBaseDelegateType(delegateTypeInstance->mBaseType);
	auto baseDelegate = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(baseDelegateType, BfIRPopulateType_Full));

	// >> delegate.mTarget = bindResult.mTarget
	BfIRValue valPtr;
	if (mModule->HasExecutedOutput())
	{
		if ((implicitParamCount > 0) || (needsSplat)) // Point back to self, it contains capture data
			valPtr = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
		else if (bindResult.mTarget)
			valPtr = mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
		else
			valPtr = mModule->GetDefaultValue(mModule->GetPrimitiveType(BfTypeCode_NullPtr));
		auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, 2);
		mModule->mBfIRBuilder->CreateStore(valPtr, fieldPtr);
	}

	if (!funcValue)
	{
		funcValue = bindResult.mFunc;

		if (!funcValue)
		{
			if ((mModule->HasExecutedOutput()) && (!mModule->mBfIRBuilder->mIgnoreWrites))
				mModule->AssertErrorState();
			return;
		}

		if ((mModule->mCompiler->mOptions.mAllowHotSwapping) && (!bindResult.mMethodInstance->mMethodDef->mIsVirtual) && (!mModule->mIsComptimeModule))
		{
			funcValue = mModule->mBfIRBuilder->RemapBindFunction(funcValue);
		}
	}

	// >> delegate.mFuncPtr = bindResult.mFunc
	auto nullPtrType = mModule->GetPrimitiveType(BfTypeCode_NullPtr);
	valPtr = mModule->mBfIRBuilder->CreateBitCast(funcValue, mModule->mBfIRBuilder->MapType(nullPtrType));
	auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, 1);
	mModule->mBfIRBuilder->CreateStore(valPtr, fieldPtr);
}

void BfExprEvaluator::VisitLambdaBodies(BfAstNode* body, BfFieldDtorDeclaration* fieldDtor)
{
	if (auto blockBody = BfNodeDynCast<BfBlock>(body))
		mModule->VisitChild(blockBody);
	else if (auto bodyExpr = BfNodeDynCast<BfExpression>(body))
	{
		auto result = mModule->CreateValueFromExpression(bodyExpr);
		if ((result) && (mModule->mCurMethodState->mClosureState != NULL) &&
			(mModule->mCurMethodState->mClosureState->mReturnTypeInferState == BfReturnTypeInferState_Inferring))
			mModule->mCurMethodState->mClosureState->mReturnType = result.mType;
	}

	while (fieldDtor != NULL)
	{
		mModule->mCurMethodState->mLeftBlockUncond = false;
		mModule->VisitChild(fieldDtor->mBody);
		fieldDtor = fieldDtor->mNextFieldDtor;
	}
}

BfLambdaInstance* BfExprEvaluator::GetLambdaInstance(BfLambdaBindExpression* lambdaBindExpr, BfAllocTarget& allocTarget)
{
	if (mModule->mCurMethodState == NULL)
		return NULL;

	auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
	BfAstNodeList cacheNodeList;
	cacheNodeList.mList.Add(lambdaBindExpr);

	///
	{
		auto checkMethodState = mModule->mCurMethodState;
		while (checkMethodState != NULL)
		{
			if (checkMethodState->mMixinState != NULL)
				cacheNodeList.mList.Add(checkMethodState->mMixinState->mSource);
			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	bool isInferReturnType = (mBfEvalExprFlags & BfEvalExprFlags_InferReturnType) != 0;

	BfLambdaInstance* lambdaInstance = NULL;
	if ((!isInferReturnType) && (rootMethodState->mLambdaCache.TryGetValue(cacheNodeList, &lambdaInstance)))
		return lambdaInstance;

	static int sBindCount = 0;
	sBindCount++;

	bool isFunctionBind = false;

	BfTypeInstance* delegateTypeInstance = NULL;
	BfMethodInstance* invokeMethodInstance = NULL;
	if (mExpectingType == NULL)
	{
		mModule->Fail("Cannot infer delegate type", lambdaBindExpr);
		delegateTypeInstance = mModule->ResolveTypeDef(mModule->mCompiler->mActionTypeDef)->ToTypeInstance();
	}
	else
	{
		delegateTypeInstance = mExpectingType->ToTypeInstance();
		if ((delegateTypeInstance == NULL) ||
			((!delegateTypeInstance->mTypeDef->mIsDelegate) && (!delegateTypeInstance->mTypeDef->mIsFunction)))
		{
			if (lambdaBindExpr->mFatArrowToken != NULL)
				mModule->Fail("Can only bind lambdas to delegate types", lambdaBindExpr->mFatArrowToken);
			delegateTypeInstance = mModule->ResolveTypeDef(mModule->mCompiler->mActionTypeDef)->ToTypeInstance();
		}
		else
		{
			invokeMethodInstance = mModule->GetRawMethodInstanceAtIdx(delegateTypeInstance, 0, "Invoke");
		}

		isFunctionBind = delegateTypeInstance->mTypeDef->mIsFunction;
	}

	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(lambdaBindExpr->mNewToken))
	{
		if (isFunctionBind)
		{
			mModule->Fail("Function lambda binding does not require allocation", lambdaBindExpr->mNewToken);
		}
		else if (tokenNode->GetToken() == BfToken_Append)
		{
			mModule->Fail("Append allocation on delegate bind not supported", lambdaBindExpr->mNewToken);
		}
	}

	if (invokeMethodInstance != NULL)
	{
		if ((int)lambdaBindExpr->mParams.size() < invokeMethodInstance->GetParamCount())
		{
			BfAstNode* refNode = lambdaBindExpr->mCloseParen;
			if (refNode == NULL)
				refNode = lambdaBindExpr;
			mModule->Fail(StrFormat("Not enough parameters for delegate type '%s'. Expected %d more.",
				mModule->TypeToString(delegateTypeInstance).c_str(), invokeMethodInstance->GetParamCount() - lambdaBindExpr->mParams.size()), refNode);
		}
		else if ((int)lambdaBindExpr->mParams.size() > invokeMethodInstance->GetParamCount())
		{
			BfAstNode* refNode = lambdaBindExpr->mParams[invokeMethodInstance->GetParamCount()];
			mModule->Fail(StrFormat("Too many parameters for delegate type '%s'. Expected %d fewer.",
				mModule->TypeToString(delegateTypeInstance).c_str(), lambdaBindExpr->mParams.size() - invokeMethodInstance->GetParamCount()), refNode);
		}
	}

	auto autoComplete = GetAutoComplete();

	bool wasCapturingMethodInfo = false;
	if ((autoComplete != NULL) && (invokeMethodInstance != NULL))
	{
		wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;

		if (autoComplete->IsAutocompleteNode(lambdaBindExpr, lambdaBindExpr->mFatArrowToken))
			autoComplete->CheckInvocation(lambdaBindExpr, lambdaBindExpr->mOpenParen, lambdaBindExpr->mCloseParen, lambdaBindExpr->mCommas);

		if (autoComplete->mIsCapturingMethodMatchInfo)
		{
			autoComplete->mMethodMatchInfo->mInstanceList.Clear();

			auto methodMatchInfo = autoComplete->mMethodMatchInfo;

			BfAutoComplete::MethodMatchEntry methodMatchEntry;
			methodMatchEntry.mTypeInstance = invokeMethodInstance->GetOwner();
			methodMatchEntry.mCurMethodInstance = mModule->mCurMethodInstance;
			methodMatchEntry.mMethodDef = invokeMethodInstance->mMethodDef;
			autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);

			methodMatchInfo->mBestIdx = 0;
			methodMatchInfo->mMostParamsMatched = 0;

 			int cursorIdx = lambdaBindExpr->GetParser()->mCursorIdx;
 			if ((lambdaBindExpr->mCloseParen == NULL) || (cursorIdx <= lambdaBindExpr->mCloseParen->GetSrcStart()))
 			{
 				int paramIdx = 0;
 				for (int commaIdx = 0; commaIdx < (int)lambdaBindExpr->mCommas.size(); commaIdx++)
 				{
 					auto commaNode = lambdaBindExpr->mCommas[commaIdx];
 					if ((commaNode != NULL) && (cursorIdx >= commaNode->GetSrcStart()))
 						paramIdx = commaIdx + 1;
 				}

 				bool isEmpty = true;

 				if (paramIdx < (int)lambdaBindExpr->mParams.size())
 				{
 					auto paramNode = lambdaBindExpr->mParams[paramIdx];
 					if (paramNode != NULL)
 						isEmpty = false;
 				}

 				if (isEmpty)
 				{
 					if (paramIdx < (int)invokeMethodInstance->GetParamCount())
 					{
 						String paramName = invokeMethodInstance->GetParamName(paramIdx);
 						autoComplete->mEntriesSet.Clear();
						if (paramName.IsEmpty())
							paramName += StrFormat("val%d", paramIdx + 1);
 						autoComplete->AddEntry(AutoCompleteEntry("paramName", paramName));
 						autoComplete->mInsertStartIdx = cursorIdx;
 						autoComplete->mInsertEndIdx = cursorIdx;

						if ((paramIdx == 0) && (lambdaBindExpr->mParams.IsEmpty()))
						{
							String totalNames;
							for (int checkIdx = 0; checkIdx < (int)invokeMethodInstance->GetParamCount(); checkIdx++)
							{
								if (!totalNames.IsEmpty())
									totalNames += ", ";
								String paramName = invokeMethodInstance->GetParamName(checkIdx);
								if (paramName.IsEmpty())
									paramName += StrFormat("val%d", checkIdx + 1);
								totalNames += paramName;
							}
							autoComplete->AddEntry(AutoCompleteEntry("paramNames", totalNames));
						}
 					}
 				}
 			}
		}
	}

	defer
	(
		{
			if (autoComplete != NULL)
				autoComplete->mIsCapturingMethodMatchInfo = (wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo) && (autoComplete->mMethodMatchInfo != NULL);
		}
	);

	if (lambdaBindExpr->mBody == NULL)
	{
		mModule->AssertErrorState();
		return NULL;
	}

	if ((lambdaBindExpr->mNewToken == NULL) && (isInferReturnType))
	{
		// Method ref, but let this follow infer route
	}
	else if ((lambdaBindExpr->mNewToken == NULL) || (isFunctionBind))
	{
		if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
			VisitLambdaBodies(lambdaBindExpr->mBody, lambdaBindExpr->mDtor);
		}

		if ((lambdaBindExpr->mNewToken != NULL) && (isFunctionBind))
			mModule->Fail("Binds to functions should do not require allocations.", lambdaBindExpr->mNewToken);

		if (lambdaBindExpr->mDtor != NULL)
		{
			mModule->Fail("Valueless method reference cannot contain destructor. Consider either removing destructor or using an allocated lambda.", lambdaBindExpr->mDtor->mTildeToken);

			// Eat it
			auto fieldDtor = lambdaBindExpr->mDtor;
			while (fieldDtor != NULL)
			{
				mModule->VisitEmbeddedStatement(fieldDtor->mBody);
				fieldDtor = fieldDtor->mNextFieldDtor;
			}
		}

		if (invokeMethodInstance != NULL)
		{
			BfLocalMethod* localMethod = new BfLocalMethod();
			localMethod->mMethodName = "anon";
			localMethod->mSystem = mModule->mSystem;
			localMethod->mModule = mModule;

			localMethod->mExpectedFullName = mModule->GetLocalMethodName(localMethod->mMethodName, lambdaBindExpr->mFatArrowToken, mModule->mCurMethodState, mModule->mCurMethodState->mMixinState);

			localMethod->mLambdaInvokeMethodInstance = invokeMethodInstance;
			localMethod->mLambdaBindExpr = lambdaBindExpr;
			localMethod->mDeclMethodState = mModule->mCurMethodState;
			mModule->mContext->mLocalMethodGraveyard.push_back(localMethod);

			auto moduleMethodInstance = mModule->GetLocalMethodInstance(localMethod, BfTypeVector());

			if (moduleMethodInstance.mMethodInstance->mDisallowCalling)
			{
				mResult = mModule->GetDefaultTypedValue(mExpectingType, false, BfDefaultValueKind_Addr);
				return NULL;
			}

			auto methodRefType = mModule->CreateMethodRefType(moduleMethodInstance.mMethodInstance);
			mModule->AddDependency(methodRefType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
			mModule->AddCallDependency(moduleMethodInstance.mMethodInstance);
			mResult = BfTypedValue(BfIRValue(BfIRValueFlags_Value, BfIRValue::ID_IMPLICIT), methodRefType);

			return NULL;
		}
	}

	//SetAndRestoreValue<bool> prevIgnoreIRWrites(mModule->mBfIRBuilder->mIgnoreWrites, mModule->mWantsIRIgnoreWrites);
	SetAndRestoreValue<bool> prevIgnoreIRWrites(mModule->mBfIRBuilder->mIgnoreWrites, mModule->mWantsIRIgnoreWrites || mModule->mCurMethodInstance->mIsUnspecialized);

	BfTypeInstance* outerClosure = NULL;
	if ((mModule->mCurMethodState->mClosureState != NULL) && (!mModule->mCurMethodState->mClosureState->mCapturing))
		outerClosure = mModule->mCurMethodState->mClosureState->mClosureType;

	Val128 val128(delegateTypeInstance->mTypeId);

	bool isConstEval = ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0);

	BfMethodState methodState;
	methodState.mPrevMethodState = mModule->mCurMethodState;
	BfIRFunctionType funcType;
	auto voidType = mModule->GetPrimitiveType(BfTypeCode_None);
	SizedArray<BfIRType, 0> paramTypes;
	funcType = mModule->mBfIRBuilder->CreateFunctionType(mModule->mBfIRBuilder->MapType(voidType), paramTypes, false);

	auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
	BF_ASSERT(prevInsertBlock || isConstEval);
	auto prevActiveFunction = mModule->mBfIRBuilder->GetActiveFunction();
	mModule->mBfIRBuilder->SaveDebugLocation();

	SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);

	BfDeferredLocalAssignData deferredLocalAssignData;
	if (mModule->mCurMethodState->mDeferredLocalAssignData != NULL)
		deferredLocalAssignData.ExtendFrom(mModule->mCurMethodState->mDeferredLocalAssignData);
	SetAndRestoreValue<BfMethodState*> prevMethodState(mModule->mCurMethodState, &methodState);

	methodState.mIRHeadBlock = mModule->mBfIRBuilder->CreateBlock("head", true);
	methodState.mIRInitBlock = mModule->mBfIRBuilder->CreateBlock("init", true);
	methodState.mIREntryBlock = mModule->mBfIRBuilder->CreateBlock("entry", true);
	methodState.mCurScope->mDIScope = prevMethodState.mPrevVal->mCurScope->mDIScope;//invokeMethodInstance->mDIFunction;

	methodState.mCurLocalVarId = -1;

	methodState.mIRFunction = prevMethodState.mPrevVal->mIRFunction;
	methodState.mDeferredLocalAssignData = &deferredLocalAssignData;

	mModule->mBfIRBuilder->SetInsertPoint(methodState.mIREntryBlock);

	BfClosureState closureState;
	if (delegateTypeInstance->IsDelegate())
		closureState.mReturnType = mModule->GetDelegateReturnType(delegateTypeInstance);
	else
		closureState.mReturnType = mModule->mContext->mBfObjectType;
	closureState.mCapturing = true;
	if (delegateTypeInstance->IsDelegate())
		closureState.mDelegateType = delegateTypeInstance;
	closureState.mDeclaringMethodIsMutating = mModule->mCurMethodInstance->mMethodDef->mIsMutating;
	methodState.mClosureState = &closureState;
	closureState.mClosureType = outerClosure;

	BF_ASSERT(methodState.mCurLocalVarId == -1);

	int outerLocalsCount = (int)methodState.mLocals.size();

// 	static int sItrCount = 0;
// 	++sItrCount;
// 	int itrCount = sItrCount;
// 	if ((itrCount == 8) || (itrCount == 10))
// 	{
// 		NOP;
// 	}

	String delegateTypeName = mModule->TypeToString(delegateTypeInstance);

	HashContext hashCtx;
	hashCtx.MixinStr(delegateTypeName);

	BfSource* bfSource = delegateTypeInstance->mTypeDef->mSource;
	BfMethodDef* methodDef = new BfMethodDef();
	methodDef->mDeclaringType = mModule->mCurMethodInstance->mMethodDef->mDeclaringType;
	Val128 closureMethodHash;
	OwnedVector<BfParameterDeclaration> tempParamDecls;

	if ((autoComplete != NULL) && (autoComplete->mMethodMatchInfo != NULL) && (autoComplete->IsAutocompleteNode(lambdaBindExpr->mBody)))
	{
		// Don't show outer method match info when our cursor is inside a lambda expression (being passed as a parameter)
		autoComplete->RemoveMethodMatchInfo();
	}

	if (invokeMethodInstance != NULL)
	{
		for (int paramIdx = 0; paramIdx < (int)invokeMethodInstance->mMethodDef->mParams.size(); paramIdx++)
		{
			auto invokeParamDef = invokeMethodInstance->mMethodDef->mParams[paramIdx];

			BfParameterDef* paramDef = new BfParameterDef();
			paramDef->mParamDeclaration = tempParamDecls.Alloc();
			BfAstNode::Zero(paramDef->mParamDeclaration);

			BfLocalVariable* localVar = new BfLocalVariable();
			if (paramIdx < (int)lambdaBindExpr->mParams.size())
			{
				localVar->mName = lambdaBindExpr->mParams[paramIdx]->ToString();
				localVar->mNameNode = lambdaBindExpr->mParams[paramIdx];
				paramDef->mParamDeclaration->mNameNode = lambdaBindExpr->mParams[paramIdx];
			}
			else
			{
				mModule->AssertErrorState();
				localVar->mName = invokeParamDef->mName;
				paramDef->mParamDeclaration->mNameNode = NULL;
			}
			paramDef->mName = localVar->mName;
			methodDef->mParams.push_back(paramDef);

			localVar->mResolvedType = invokeMethodInstance->GetParamType(paramIdx);
			mModule->PopulateType(localVar->mResolvedType);
			localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
			localVar->mReadFromId = 0;

			auto rootMethodState = methodState.GetRootMethodState();
			localVar->mLocalVarId = rootMethodState->mCurLocalVarId++;

			mModule->DoAddLocalVariable(localVar);

			if (autoComplete != NULL)
				autoComplete->CheckLocalDef(BfNodeDynCast<BfIdentifierNode>(paramDef->mParamDeclaration->mNameNode), methodState.mLocals.back());
			auto resolvePassData = mModule->mCompiler->mResolvePassData;
			if (resolvePassData != NULL)
				resolvePassData->HandleLocalReference(BfNodeDynCast<BfIdentifierNode>(paramDef->mParamDeclaration->mNameNode), mModule->mCurTypeInstance->mTypeDef,
					mModule->mCurMethodInstance->mMethodDef, localVar->mLocalVarId);
		}
	}

	bool isAutocomplete = mModule->mCompiler->IsAutocomplete();

	methodDef->mIdx = mModule->mCurMethodInstance->mMethodDef->mIdx;
	methodDef->mBody = lambdaBindExpr->mBody;
	///

	auto varMethodState = methodState.mPrevMethodState;
	bool hasExplicitCaptureNames = false;

	for (auto& captureEntry : allocTarget.mCaptureInfo->mCaptures)
	{
		if (captureEntry.mNameNode == NULL)
		{
			hasExplicitCaptureNames = false;
			break;
		}

		hasExplicitCaptureNames = true;
	}

	auto _SetNotCapturedFlag = [&](bool notCaptured)
	{
		auto varMethodState = methodState.mPrevMethodState;
		while (varMethodState != NULL)
		{
			for (int localIdx = 0; localIdx < varMethodState->mLocals.size(); localIdx++)
			{
				auto localVar = varMethodState->mLocals[localIdx];
				localVar->mNotCaptured = notCaptured;
			}

			varMethodState = varMethodState->mPrevMethodState;
			if (varMethodState == NULL)
				break;
			if (varMethodState->mMixinState != NULL)
				break;
			if (varMethodState->mClosureState != NULL)
			{
				if (!varMethodState->mClosureState->mCapturing)
					break;
			}
		}
	};

	if (hasExplicitCaptureNames)
	{
		_SetNotCapturedFlag(true);

		auto varMethodState = methodState.mPrevMethodState;
		while (varMethodState != NULL)
		{
			for (auto& captureEntry : allocTarget.mCaptureInfo->mCaptures)
			{
				if (captureEntry.mNameNode != NULL)
				{
					StringT<64> captureName;
					captureEntry.mNameNode->ToString(captureName);
					BfLocalVarEntry* entry;
					if (varMethodState->mLocalVarSet.TryGetWith<StringImpl&>(captureName, &entry))
					{
						auto localVar = entry->mLocalVar;
						while (localVar != NULL)
						{
							if (autoComplete != NULL)
								autoComplete->CheckLocalRef(captureEntry.mNameNode, localVar);
							if (((mModule->mCurMethodState->mClosureState == NULL) || (mModule->mCurMethodState->mClosureState->mCapturing)) &&
								(mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCurMethodInstance != NULL))
								mModule->mCompiler->mResolvePassData->HandleLocalReference(captureEntry.mNameNode, localVar->mNameNode, mModule->mCurTypeInstance->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, localVar->mLocalVarId);

							localVar->mNotCaptured = false;
							localVar = localVar->mShadowedLocal;
						}
					}
				}
			}

			varMethodState = varMethodState->mPrevMethodState;
			if (varMethodState == NULL)
				break;
			if (varMethodState->mMixinState != NULL)
				break;
			if (varMethodState->mClosureState != NULL)
			{
				if (!varMethodState->mClosureState->mCapturing)
					break;
			}
		}
	}

	BfClosureInstanceInfo* closureInstanceInfo = new BfClosureInstanceInfo();

	auto checkInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
	closureState.mCaptureStartAccessId = methodState.mPrevMethodState->GetRootMethodState()->mCurAccessId;
	closureState.mCaptureVisitingBody = true;
	closureState.mClosureInstanceInfo = closureInstanceInfo;

	if ((mBfEvalExprFlags & BfEvalExprFlags_InferReturnType) != 0)
	{
		closureState.mReturnType = NULL;
		closureState.mReturnTypeInferState = BfReturnTypeInferState_Inferring;
	}

	VisitLambdaBodies(lambdaBindExpr->mBody, lambdaBindExpr->mDtor);

	if (hasExplicitCaptureNames)
		_SetNotCapturedFlag(false);

	// If we ended up being called by a method with a lower captureStartAccessId, propagate that to whoever is calling us, too...
	if ((methodState.mPrevMethodState->mClosureState != NULL) && (methodState.mPrevMethodState->mClosureState->mCapturing))
	{
		auto prevClosureState = methodState.mPrevMethodState->mClosureState;
		if (closureState.mCaptureStartAccessId < prevClosureState->mCaptureStartAccessId)
			prevClosureState->mCaptureStartAccessId = closureState.mCaptureStartAccessId;
	}

	bool earlyExit = false;
	if (isInferReturnType)
	{
		if ((closureState.mReturnTypeInferState == BfReturnTypeInferState_Fail) ||
			(closureState.mReturnType == NULL))
		{
			mResult = BfTypedValue();
		}
		else
		{
			mResult = BfTypedValue(closureState.mReturnType);
		}

		earlyExit = true;
	}
	else if (mModule->mCurMethodInstance->mIsUnspecialized)
	{
		earlyExit = true;
		mResult = mModule->GetDefaultTypedValue(delegateTypeInstance);
	}

	if (earlyExit)
	{
		prevIgnoreWrites.Restore();
		mModule->mBfIRBuilder->RestoreDebugLocation();

		mModule->mBfIRBuilder->SetActiveFunction(prevActiveFunction);
		if (!prevInsertBlock.IsFake())
			mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
		delete methodDef;
		delete closureInstanceInfo;
		return NULL;
	}

	closureState.mCaptureVisitingBody = false;

	prevIgnoreWrites.Restore();
	mModule->mBfIRBuilder->RestoreDebugLocation();

	auto _GetCaptureType = [&](const StringImpl& str)
	{
		if (allocTarget.mCaptureInfo->mCaptures.IsEmpty())
			return BfCaptureType_Copy;

		for (auto& captureEntry : allocTarget.mCaptureInfo->mCaptures)
		{
			if ((captureEntry.mNameNode == NULL) || (captureEntry.mNameNode->Equals(str)))
			{
				captureEntry.mUsed = true;
				return captureEntry.mCaptureType;
			}
		}

		return BfCaptureType_None;
	};

	Array<BfClosureCapturedEntry> capturedEntries;

	bool copyOuterCaptures = false;
	//
	{
		auto varMethodState = methodState.mPrevMethodState;

		while (varMethodState != NULL)
		{
			for (int localIdx = 0; localIdx < varMethodState->mLocals.size(); localIdx++)
			{
				auto localVar = varMethodState->mLocals[localIdx];
				if ((localVar->mReadFromId >= closureState.mCaptureStartAccessId) || (localVar->mWrittenToId >= closureState.mCaptureStartAccessId))
				{
					if ((localVar->mIsThis) && (outerClosure != NULL))
					{
						continue;
					}

					auto outerLocal = localVar;

					if ((localVar->mConstValue) || (localVar->mResolvedType->IsValuelessType()))
					{
						closureState.mConstLocals.push_back(*outerLocal);
						continue;
					}

					BfClosureCapturedEntry capturedEntry;

					auto capturedType = outerLocal->mResolvedType;
					bool captureByRef = false;

					auto captureType = _GetCaptureType(localVar->mName);
					if (captureType == BfCaptureType_None)
					{
						continue;
					}

					if (!capturedType->IsRef())
					{
						if (captureType == BfCaptureType_Reference)
						{
							if (outerLocal->mIsThis)
							{
								if ((outerLocal->mResolvedType->IsValueType()) && (mModule->mCurMethodInstance->mMethodDef->HasNoThisSplat()))
									captureByRef = true;
							}
							else if ((!localVar->mIsReadOnly) && (localVar->mWrittenToId >= closureState.mCaptureStartAccessId))
								captureByRef = true;
						}
					}
					else
					{
						if ((captureType != BfCaptureType_Reference) || (localVar->mWrittenToId < closureState.mCaptureStartAccessId))
						{
							capturedType = ((BfRefType*)capturedType)->mElementType;
						}
					}

					if (captureByRef)
					{
						capturedType = mModule->CreateRefType(capturedType);
					}

					if (captureType == BfCaptureType_Reference)
						capturedEntry.mExplicitlyByReference = true;
					capturedEntry.mType = capturedType;
					capturedEntry.mNameNode = outerLocal->mNameNode;
					if (outerLocal->mName == "this")
						capturedEntry.mName = "__this";
					else
					{
						capturedEntry.mName = outerLocal->mName;

						BfLocalVarEntry* entry = NULL;
						if (varMethodState->mLocalVarSet.TryGetWith<StringImpl&>(capturedEntry.mName, &entry))
						{
							auto startCheckVar = entry->mLocalVar;

							int shadowIdx = 0;
							auto checkVar = startCheckVar;
							while (checkVar != NULL)
							{
								if (checkVar == outerLocal)
								{
									// We only use mShadowIdx when we have duplicate name nodes (ie: in the case of for looks with iterator vs value)
									auto shadowCheckVar = startCheckVar;
									while (shadowCheckVar != checkVar)
									{
										if (shadowCheckVar->mNameNode == checkVar->mNameNode)
											capturedEntry.mShadowIdx++;

										shadowCheckVar = shadowCheckVar->mShadowedLocal;
									}

									for (int i = 0; i < shadowIdx; i++)
										capturedEntry.mName.Insert(0, '@');

									break;
								}

								shadowIdx++;
								checkVar = checkVar->mShadowedLocal;
							}
						}
					}
					capturedEntries.Add(capturedEntry);
				}
			}

			varMethodState = varMethodState->mPrevMethodState;
			if (varMethodState == NULL)
				break;

			if (varMethodState->mMixinState != NULL)
				break;

			if (varMethodState->mClosureState != NULL)
			{
				if (!varMethodState->mClosureState->mCapturing)
					break;
			}
		}
	}

	for (auto& captureEntry : allocTarget.mCaptureInfo->mCaptures)
	{
		if ((!captureEntry.mUsed) && (captureEntry.mNameNode != NULL))
			mModule->Warn(0, "Capture specifier not used", captureEntry.mNameNode);
	}

	for (auto copyField : closureState.mReferencedOuterClosureMembers)
	{
		auto fieldDef = copyField->GetFieldDef();
		auto captureType = _GetCaptureType(fieldDef->mName);

		BfClosureCapturedEntry capturedEntry;
		capturedEntry.mName = fieldDef->mName;
		capturedEntry.mType = copyField->mResolvedType;
		if ((captureType == BfCaptureType_Reference) && (capturedEntry.mType->IsRef()))
		{
			capturedEntry.mExplicitlyByReference = true;
		}
		else if ((captureType != BfCaptureType_Reference) && (capturedEntry.mType->IsRef()))
		{
			auto refType = (BfRefType*)capturedEntry.mType;
			capturedEntry.mType = refType->mElementType;
		}
		else if ((captureType == BfCaptureType_Reference) && (!capturedEntry.mType->IsRef()) && (!fieldDef->mIsReadOnly))
		{
			capturedEntry.mType = mModule->CreateRefType(capturedEntry.mType);
		}
	}

	std::sort(capturedEntries.begin(), capturedEntries.end());

	bool hasCapture = false;

 	BfMethodInstanceGroup methodInstanceGroup;
 	methodInstanceGroup.mOwner = mModule->mCurTypeInstance;
 	methodInstanceGroup.mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;

	BfMethodInstance* methodInstance = new BfMethodInstance();
	methodInstance->mMethodInstanceGroup = &methodInstanceGroup;
	methodInstance->GetMethodInfoEx()->mClosureInstanceInfo = closureInstanceInfo;
	if (invokeMethodInstance != NULL)
		methodInstance->mParams = invokeMethodInstance->mParams;
	methodInstance->mIsClosure = true;

	// We want the closure ID to match between hot reloads -- otherwise we wouldn't be able to modify them,
	//  so we use the charId from the 'fat arrow' token
	int closureId = 0;
	if (lambdaBindExpr->mFatArrowToken != NULL)
		closureId = lambdaBindExpr->mFatArrowToken->GetStartCharId();

	auto curProject = mModule->mCurTypeInstance->mTypeDef->mProject;
	BF_ASSERT(curProject != NULL);

	// We need to make these per-project even though you'd think we might not because we
	//  insert generic type specializations in the generic definition's project,
	//  BECAUSE: we would need to scan the captured fields the same way we scan the
	//  generic arguments to determine if each project can see it or not in the vdata

	BfTypeInstance* useTypeInstance = delegateTypeInstance;
	BfClosureType* closureTypeInst = NULL;

	// If we are allowing hot swapping we may add a capture later. We also need an equal method that ignores 'target' even when we're capturing ourself
	if ((capturedEntries.size() != 0) || (lambdaBindExpr->mDtor != NULL) || (copyOuterCaptures) || (mModule->mCompiler->mOptions.mAllowHotSwapping) || (closureState.mCapturedDelegateSelf))
	{
		hashCtx.MixinStr(curProject->mName);

		if (copyOuterCaptures)
		{
			hashCtx.Mixin(outerClosure->mTypeId);
		}

		for (auto& capturedEntry : capturedEntries)
		{
			hashCtx.Mixin(capturedEntry.mType->mTypeId);
			hashCtx.MixinStr(capturedEntry.mName);
			hashCtx.Mixin(capturedEntry.mExplicitlyByReference);
		}

		if (lambdaBindExpr->mDtor != NULL)
		{
			// Has DTOR thunk
			bool hasDtorThunk = true;
			hashCtx.Mixin(hasDtorThunk);
		}

		Val128 hash128 = hashCtx.Finish128();
		BfClosureType* checkClosureType = new BfClosureType(delegateTypeInstance, hash128);
		checkClosureType->mContext = mModule->mContext;
		checkClosureType->mBaseType = delegateTypeInstance;
		BfType* resolvedClosureType = mModule->ResolveType(checkClosureType, BfPopulateType_TypeDef);
		closureTypeInst = (BfClosureType*)resolvedClosureType;
		if (checkClosureType == resolvedClosureType)
		{
			// This is a new closure type
			closureTypeInst->Init(curProject);
			closureTypeInst->mTypeDef->mProject = curProject;

			auto delegateDirectTypeRef = BfAstNode::ZeroedAlloc<BfDirectTypeDefReference>();
			delegateDirectTypeRef->Init(mModule->mCompiler->mDelegateTypeDef);
			closureTypeInst->mDirectAllocNodes.push_back(delegateDirectTypeRef);

			BfMethodDef* methodDef = BfDefBuilder::AddMethod(closureTypeInst->mTypeDef, BfMethodType_Normal, BfProtection_Public, false, "Equals");
			methodDef->mReturnTypeRef = mModule->mSystem->mDirectBoolTypeRef;
			BfDefBuilder::AddParam(methodDef, delegateDirectTypeRef, "val");
			methodDef->mIsVirtual = true;
			methodDef->mIsOverride = true;

			if (copyOuterCaptures)
			{
				for (auto& fieldInstance : outerClosure->mFieldInstances)
				{
					BfFieldDef* origFieldDef = fieldInstance.GetFieldDef();
					BfFieldDef* fieldDef = closureTypeInst->AddField(fieldInstance.mResolvedType, origFieldDef->mName);
					fieldDef->mIsReadOnly = origFieldDef->mIsReadOnly;
				}
			}

			for (auto& capturedEntry : capturedEntries)
			{
				BfFieldDef* fieldDef = closureTypeInst->AddField(capturedEntry.mType, capturedEntry.mName);
				if (!capturedEntry.mExplicitlyByReference)
					fieldDef->mIsReadOnly = true;
			}

			if (lambdaBindExpr->mDtor != NULL)
			{
				auto dtorDef = closureTypeInst->AddDtor();
				auto voidType = mModule->GetPrimitiveType(BfTypeCode_None);
				auto voidPtrType = mModule->CreatePointerType(voidType);
				closureTypeInst->AddField(voidPtrType, "__dtorThunk");
			}

			closureTypeInst->Finish();
		}
		else
		{
			// Already had this entry
			delete checkClosureType;
		}
		useTypeInstance = closureTypeInst;
	}
	mModule->mBfIRBuilder->PopulateType(useTypeInstance);
	mModule->PopulateType(useTypeInstance);

	methodDef->mIsStatic = closureTypeInst == NULL;

	SizedArray<BfIRType, 8> origParamTypes;
	BfIRType origReturnType;

	bool forceStatic = false;
	if (invokeMethodInstance != NULL)
	{
		forceStatic = methodDef->mIsStatic;
		auto invokeFunctionType = mModule->mBfIRBuilder->MapMethod(invokeMethodInstance);
		invokeMethodInstance->GetIRFunctionInfo(mModule, origReturnType, origParamTypes, forceStatic);
	}
	else
	{
		origReturnType = mModule->mBfIRBuilder->MapType(mModule->GetPrimitiveType(BfTypeCode_None));
	}

	SizedArray<BfIRType, 3> newTypes;

	if ((invokeMethodInstance != NULL) && (GetStructRetIdx(invokeMethodInstance, forceStatic) == 0))
		newTypes.push_back(origParamTypes[0]);
	if (!methodDef->mIsStatic)
		newTypes.push_back(mModule->mBfIRBuilder->MapType(useTypeInstance));
	if ((invokeMethodInstance != NULL) && (GetStructRetIdx(invokeMethodInstance, forceStatic) == 1))
		newTypes.push_back(origParamTypes[1]);

	int paramStartIdx = 0;
	if ((invokeMethodInstance != NULL) && (GetStructRetIdx(invokeMethodInstance, forceStatic) != -1))
		paramStartIdx++;
	if (!methodDef->mIsStatic)
		paramStartIdx++;

	for (int i = paramStartIdx; i < (int)origParamTypes.size(); i++)
		newTypes.push_back(origParamTypes[i]);
	auto closureFuncType = mModule->mBfIRBuilder->CreateFunctionType(origReturnType, newTypes, false);

	prevMethodState.Restore();
	mModule->mBfIRBuilder->SetActiveFunction(prevActiveFunction);
	if ((prevInsertBlock) && (!prevInsertBlock.IsFake()))
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);

	// Just a check
	mModule->mBfIRBuilder->GetInsertBlock();

	//auto rootMethodState = mModule->mCurMethodState;

	HashContext closureHashCtx;
	closureHashCtx.Mixin(closureId);

	// When we're a nested lambda, strip off the outer hash and closureTypeInst markers
	methodDef->mName = mModule->mCurMethodInstance->mMethodDef->mName;
	int prevSepPos = (int)methodDef->mName.LastIndexOf('$');
	if (prevSepPos != -1)
	{
		closureHashCtx.Mixin(methodDef->mName.c_str() + prevSepPos, (int)methodDef->mName.length() - prevSepPos);
		methodDef->mName.RemoveToEnd(prevSepPos);
	}

	// Mix in this because this can be emitted multiple times when there's multiple ctors and field initializers with lambdas
	if (mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)
	{
		if (auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(mModule->mCurMethodInstance->mMethodDef->mMethodDeclaration))
		{
			if (ctorDecl->mThisToken != NULL)
				closureHashCtx.Mixin(ctorDecl->mThisToken->GetStartCharId());
		}
	}

	auto checkMethodState = mModule->mCurMethodState;
	while (checkMethodState != NULL)
	{
		if (checkMethodState->mMethodInstance != NULL)
		{
			if (checkMethodState->mMethodInstance->mMethodInfoEx != NULL)
			{
				for (auto methodGenericArg : checkMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments)
				{
					StringT<128> genericTypeName;
					BfMangler::Mangle(genericTypeName, mModule->mCompiler->GetMangleKind(), methodGenericArg);
					closureHashCtx.MixinStr(genericTypeName);
				}
			}
		}
		checkMethodState = checkMethodState->mPrevMethodState;
	}

	uint64 closureHash = closureHashCtx.Finish64();
	methodDef->mName += "$";
	methodDef->mName += BfTypeUtils::HashEncode64(closureHash);

	methodInstance->mMethodDef = methodDef;
	if (invokeMethodInstance != NULL)
	{
		methodInstance->mParams = invokeMethodInstance->mParams;
		methodInstance->mReturnType = invokeMethodInstance->mReturnType;
	}
	else
		methodInstance->mReturnType = mModule->GetPrimitiveType(BfTypeCode_None);

	StringT<128> closureFuncName;
	BfMangler::Mangle(closureFuncName, mModule->mCompiler->GetMangleKind(), methodInstance);

	auto closureFunc = mModule->mBfIRBuilder->CreateFunction(closureFuncType, BfIRLinkageType_External, closureFuncName);

	methodInstance->mIRFunction = closureFunc;
	if (methodInstance->mIsReified)
		mModule->CheckHotMethod(methodInstance, closureFuncName);
	if ((methodInstance->mHotMethod != NULL) && (mModule->mCurMethodState->mHotDataReferenceBuilder))
		mModule->mCurMethodState->mHotDataReferenceBuilder->mInnerMethods.Add(methodInstance->mHotMethod);

	methodState.Reset();

	lambdaInstance = new BfLambdaInstance();
	rootMethodState->mLambdaCache[cacheNodeList] = lambdaInstance;
	lambdaInstance->mDelegateTypeInstance = delegateTypeInstance;
	lambdaInstance->mUseTypeInstance = useTypeInstance;
	lambdaInstance->mClosureTypeInstance = closureTypeInst;
	lambdaInstance->mOuterClosure = outerClosure;
	lambdaInstance->mCopyOuterCaptures = copyOuterCaptures;
	lambdaInstance->mDeclaringMethodIsMutating = mModule->mCurMethodInstance->mMethodDef->mIsMutating;
	lambdaInstance->mIsStatic = methodDef->mIsStatic;
	lambdaInstance->mClosureFunc = closureFunc;
	lambdaInstance->mMethodInstance = methodInstance;
	lambdaInstance->mConstLocals = closureState.mConstLocals;
	lambdaInstance->mParamDecls = tempParamDecls;
	lambdaInstance->mDeclMixinState = mModule->mCurMethodState->mMixinState;
	if (lambdaInstance->mDeclMixinState != NULL)
		lambdaInstance->mDeclMixinState->mHasDeferredUsage = true;
	tempParamDecls.ClearWithoutDeleting();

	closureState.mCapturing = false;
	closureState.mClosureType = useTypeInstance;
	closureInstanceInfo->mThisOverride = useTypeInstance;
	mModule->mIncompleteMethodCount++;
	SetAndRestoreValue<BfClosureState*> prevClosureState(mModule->mCurMethodState->mClosureState, &closureState);

	if (mModule->HasExecutedOutput())
		mModule->SetupIRMethod(methodInstance, methodInstance->mIRFunction, methodInstance->mAlwaysInline);

	// This keeps us from giving errors twice.  ProcessMethod can give errors when we capture by value but needed to
	//  capture by reference, so we still need to do it for resolve-only
	bool processMethods = (mModule->mCompiler->GetAutoComplete() == NULL) && !mModule->mHadBuildError;

	mModule->mBfIRBuilder->SaveDebugLocation();
	//
	{
		BfGetSymbolReferenceKind prevSymbolRefKind = BfGetSymbolReferenceKind_None;
		if (mModule->mCompiler->mResolvePassData != NULL)
		{
			prevSymbolRefKind = mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind;
			mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_None;
		}

		if (processMethods)
		{
			// If we are in an always-ignored block, we will have mIgnoreWrites set
// 			SetAndRestoreValue<bool> prevWantsIgnoreWrite(mModule->mWantsIRIgnoreWrites, mModule->mBfIRBuilder->mIgnoreWrites);
// 			mModule->ProcessMethod(methodInstance);
		}

		if (mModule->mCompiler->mResolvePassData != NULL)
			mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind = prevSymbolRefKind;
	}
	mModule->mBfIRBuilder->RestoreDebugLocation();
	if (mModule->IsSkippingExtraResolveChecks())
		closureFunc = BfIRFunction();

	BfIRFunction dtorFunc;
	if (lambdaBindExpr->mDtor != NULL)
	{
		SizedArray<BfIRType, 1> newTypes;
		newTypes.push_back(mModule->mBfIRBuilder->MapType(useTypeInstance));
		auto voidType = mModule->GetPrimitiveType(BfTypeCode_None);
		auto dtorFuncType = mModule->mBfIRBuilder->CreateFunctionType(mModule->mBfIRBuilder->MapType(voidType), newTypes, false);

		BfMethodDef* dtorMethodDef = new BfMethodDef();
		dtorMethodDef->mDeclaringType = mModule->mCurMethodInstance->mMethodDef->mDeclaringType;
		dtorMethodDef->mName = "~this$";
		dtorMethodDef->mName += methodDef->mName;

		dtorMethodDef->mMethodType = BfMethodType_Normal;
		dtorMethodDef->mBody = lambdaBindExpr->mDtor;
		dtorMethodDef->mIdx = mModule->mCurMethodInstance->mMethodDef->mIdx;

		BfMethodInstance* dtorMethodInstance = new BfMethodInstance();
		dtorMethodInstance->mMethodDef = dtorMethodDef;
		dtorMethodInstance->mReturnType = mModule->GetPrimitiveType(BfTypeCode_None);
		dtorMethodInstance->mMethodInstanceGroup = &methodInstanceGroup;

		StringT<128> dtorMangledName;
		BfMangler::Mangle(dtorMangledName, mModule->mCompiler->GetMangleKind(), dtorMethodInstance);
		dtorFunc = mModule->mBfIRBuilder->CreateFunction(dtorFuncType, BfIRLinkageType_External, dtorMangledName);
		mModule->SetupIRMethod(NULL, dtorFunc, false);
		dtorMethodInstance->mIRFunction = dtorFunc;

		mModule->mIncompleteMethodCount++;
		mModule->mBfIRBuilder->SaveDebugLocation();
		//
		if (processMethods)
		{
			// If we are in an always-ignored block, we will have mIgnoreWrites set
// 			SetAndRestoreValue<bool> prevWantsIgnoreWrite(mModule->mWantsIRIgnoreWrites, mModule->mBfIRBuilder->mIgnoreWrites);
// 			mModule->ProcessMethod(dtorMethodInstance);
		}
		mModule->mBfIRBuilder->RestoreDebugLocation();
		if (mModule->IsSkippingExtraResolveChecks())
			dtorFunc = BfIRFunction();

		if (dtorMethodInstance->mIsReified)
			mModule->CheckHotMethod(dtorMethodInstance, dtorMangledName);
		if ((dtorMethodInstance->mHotMethod != NULL) && (mModule->mCurMethodState->mHotDataReferenceBuilder))
			mModule->mCurMethodState->mHotDataReferenceBuilder->mInnerMethods.Add(dtorMethodInstance->mHotMethod);
		lambdaInstance->mDtorMethodInstance = dtorMethodInstance;
		lambdaInstance->mDtorFunc = dtorFunc;

		dtorMethodInstance->mMethodInstanceGroup = NULL;
	}

	prevClosureState.Restore();
	if ((prevInsertBlock) && (!prevInsertBlock.IsFake()))
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);

	for (auto& capturedEntry : capturedEntries)
	{
		BfLambdaCaptureInfo lambdaCapture;
		if (capturedEntry.mName == "__this")
			lambdaCapture.mName = "this";
		else
			lambdaCapture.mName = capturedEntry.mName;

		lambdaInstance->mCaptures.Add(lambdaCapture);
		closureInstanceInfo->mCaptureEntries.Add(capturedEntry);
	}

	if (processMethods)
		rootMethodState->mDeferredLambdaInstances.Add(lambdaInstance);

	methodInstance->mMethodInstanceGroup = NULL;

	return lambdaInstance;
}

void BfExprEvaluator::Visit(BfLambdaBindExpression* lambdaBindExpr)
{
	BfTokenNode* newToken = NULL;
	BfCaptureInfo captureInfo;
	BfAllocTarget allocTarget;
	allocTarget.mCaptureInfo = &captureInfo;
	ResolveAllocTarget(allocTarget, lambdaBindExpr->mNewToken, newToken);

	if (mModule->mCurMethodInstance == NULL)
		mModule->Fail("Invalid use of lambda bind expression", lambdaBindExpr);

	if (((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mBlindCapturing)) ||
		(mModule->mCurMethodInstance == NULL))
	{
		// We're just capturing.  We just need to visit the bodies here.  This helps infinite recursion with local methods containing lambdas calling each other
		if (lambdaBindExpr->mBody != NULL)
			mModule->VisitChild(lambdaBindExpr->mBody);
		if ((lambdaBindExpr->mDtor != NULL) && (lambdaBindExpr->mDtor->mBody != NULL))
			mModule->VisitChild(lambdaBindExpr->mDtor->mBody);
		return;
	}

	BfLambdaInstance* lambdaInstance = GetLambdaInstance(lambdaBindExpr, allocTarget);
	if (lambdaInstance == NULL)
		return;
	BfTypeInstance* delegateTypeInstance = lambdaInstance->mDelegateTypeInstance;
	BfTypeInstance* useTypeInstance = lambdaInstance->mUseTypeInstance;
	BfTypeInstance* closureTypeInst = lambdaInstance->mClosureTypeInstance;

	mResult = BfTypedValue(mModule->AllocFromType(useTypeInstance, allocTarget, BfIRValue(), BfIRValue(), 0, BfAllocFlags_None), useTypeInstance);

	if (!delegateTypeInstance->IsDelegate())
	{
		mModule->AssertErrorState();
		return;
	}

	auto baseDelegateType = VerifyBaseDelegateType(delegateTypeInstance->mBaseType);
	if (baseDelegateType == NULL)
	{
		mModule->Fail("Invalid delegate type", lambdaBindExpr);
		return;
	}
	mModule->PopulateType(baseDelegateType);

	auto& funcPtrField = baseDelegateType->mFieldInstances[0];
	auto& targetField = baseDelegateType->mFieldInstances[1];
	auto baseDelegate = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(baseDelegateType));

	// >> delegate.mTarget = bindResult.mTarget
	auto nullPtrType = mModule->GetPrimitiveType(BfTypeCode_NullPtr);
	BfIRValue valPtr;
	if (!lambdaInstance->mIsStatic)
		valPtr = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(nullPtrType));
	else
		valPtr = mModule->GetDefaultValue(nullPtrType);
	auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, targetField.mDataIdx);
	mModule->mBfIRBuilder->CreateAlignedStore(valPtr, fieldPtr, targetField.mResolvedType->mAlign);

	// >> delegate.mFuncPtr = bindResult.mFunc
	if (lambdaInstance->mClosureFunc)
	{
		auto nullPtrType = mModule->GetPrimitiveType(BfTypeCode_NullPtr);
		auto valPtr = mModule->mBfIRBuilder->CreateBitCast(lambdaInstance->mClosureFunc, mModule->mBfIRBuilder->MapType(nullPtrType));
		auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, funcPtrField.mDataIdx);
		mModule->mBfIRBuilder->CreateAlignedStore(valPtr, fieldPtr, funcPtrField.mResolvedType->mAlign);
	}

	mModule->AddDependency(useTypeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);

	// Copy captures into the delegate
	if (lambdaInstance->mClosureTypeInstance != NULL)
	{
		int fieldIdx = 0;

		if (lambdaInstance->mCopyOuterCaptures)
		{
			for (auto& fieldInstance : lambdaInstance->mOuterClosure->mFieldInstances)
			{
				if (!fieldInstance.mResolvedType->IsValuelessType())
				{
					BF_ASSERT(fieldInstance.mDataIdx == fieldIdx + 1);

					auto localVar = mModule->mCurMethodState->mLocals[0];
					auto capturedValue = mModule->mBfIRBuilder->CreateInBoundsGEP(localVar->mValue, 0, fieldInstance.mDataIdx);
					capturedValue = mModule->mBfIRBuilder->CreateAlignedLoad(capturedValue, fieldInstance.mResolvedType->mAlign);
					auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, fieldInstance.mDataIdx);
					mModule->mBfIRBuilder->CreateStore(capturedValue, fieldPtr);

					fieldIdx++;
				}
			}
		}

		int captureIdx = 0;
		for (int captureIdx = 0; captureIdx < (int)lambdaInstance->mCaptures.size(); captureIdx++)
		{
			auto& capturedEntry = lambdaInstance->mCaptures[captureIdx];
			auto& closureCaptureEntry = lambdaInstance->mMethodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries[captureIdx];
			BfIdentifierNode* identifierNode = closureCaptureEntry.mNameNode;

			BfIRValue capturedValue;
			auto fieldInstance = &closureTypeInst->mFieldInstances[fieldIdx];
			BfTypedValue capturedTypedVal;
			if (identifierNode != NULL)
				capturedTypedVal = DoImplicitArgCapture(NULL, identifierNode, closureCaptureEntry.mShadowIdx);
			else
				capturedTypedVal = LookupIdentifier(NULL, capturedEntry.mName);
			if (!fieldInstance->mResolvedType->IsRef())
				capturedTypedVal = mModule->LoadOrAggregateValue(capturedTypedVal);
			else if (!capturedTypedVal.IsAddr())
			{
				mModule->Fail(StrFormat("Unable to capture '%s' by reference", capturedEntry.mName.c_str()), lambdaBindExpr);
				break;
			}
			capturedValue = capturedTypedVal.mValue;

			if (capturedValue)
			{
				if (!IsVar(capturedTypedVal.mType))
				{
					auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, fieldInstance->mDataIdx);
					mModule->mBfIRBuilder->CreateAlignedStore(capturedValue, fieldPtr, fieldInstance->mResolvedType->mAlign);
				}
			}
			else
			{
				mModule->Fail(StrFormat("Unable to capture '%s'", capturedEntry.mName.c_str()), lambdaBindExpr);
				mModule->AssertErrorState();
			}
			fieldIdx++;
		}

		if (lambdaInstance->mDtorFunc)
		{
			auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, closureTypeInst->mFieldInstances[fieldIdx].mDataIdx);

			auto voidType = mModule->GetPrimitiveType(BfTypeCode_None);
			auto voidPtrType = mModule->CreatePointerType(voidType);
			auto dtorThunk = mModule->mBfIRBuilder->CreateBitCast(lambdaInstance->mDtorFunc, mModule->mBfIRBuilder->MapType(voidPtrType));

			mModule->mBfIRBuilder->CreateAlignedStore(dtorThunk, fieldPtr, mModule->mSystem->mPtrSize);
			fieldIdx++;
		}
	}
}

void BfExprEvaluator::ProcessArrayInitializer(BfTokenNode* openToken, const BfSizedArray<BfExpression*>& valueExprs, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken, int dimensions, SizedArrayImpl<int64>& dimLengths, int dim, bool& hasFailed)
{
	bool setSize = false;

	if (dim == dimLengths.size())
	{
		dimLengths.push_back((int)valueExprs.size());
		setSize = true;
	}
	else if (dimLengths[dim] == -1)
	{
		dimLengths[dim] = (int)valueExprs.size();
		setSize = true;
	}

	int64 initCountDiff = (int)valueExprs.size() - dimLengths[dim];
	if ((dimLengths[dim] != -1) && (initCountDiff != 0) && (!hasFailed))
	{
		if (initCountDiff > 0)
		{
			mModule->Fail(StrFormat("Too many initializers, expected %d fewer", initCountDiff), valueExprs[(int)dimLengths[dim]]);
			hasFailed = true;
		}
		else
		{
			// If it ends with ", ?) or ",)" then allow unsized
			if (((valueExprs.size() == 0) || (BfNodeDynCast<BfUninitializedExpression>(valueExprs.back()) == NULL)) &&
				(commas.size() < valueExprs.size()))
			{
				BfAstNode* refNode = closeToken;
				if ((refNode == NULL) && (mModule->mParentNodeEntry != NULL))
					refNode = mModule->mParentNodeEntry->mNode;

				mModule->Fail(StrFormat("Too few initializer, expected %d more", -initCountDiff), refNode);
				hasFailed = true;
			}
		}
	}

	for (int i = 0; i < (int)valueExprs.size(); i++)
	{
		BfExpression* expr = valueExprs[i];

		if (auto uninitExpr = BfNodeDynCast<BfUninitializedExpression>(expr))
		{
			continue;
		}

		auto innerInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(expr);
		if (dim < dimensions - 1)
		{
			if (innerInitExpr == NULL)
			{
				if (auto innerTupleExpr = BfNodeDynCast<BfTupleExpression>(expr))
				{
					ProcessArrayInitializer(innerTupleExpr->mOpenParen, innerTupleExpr->mValues, innerTupleExpr->mCommas, innerTupleExpr->mCloseParen, dimensions, dimLengths, dim + 1, hasFailed);
				}
				else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
				{
					SizedArray<BfExpression*, 1> values;
					values.Add(parenExpr->mExpression);
					SizedArray<BfTokenNode*, 1> commas;
					ProcessArrayInitializer(parenExpr->mOpenParen, values, commas, parenExpr->mCloseParen, dimensions, dimLengths, dim + 1, hasFailed);
				}
				else
				{
					hasFailed = true;
					mModule->Fail("A nested array initializer is expected", expr);
					continue;
				}
			}
			else
				ProcessArrayInitializer(innerInitExpr->mOpenBrace, innerInitExpr->mValues, innerInitExpr->mCommas, innerInitExpr->mCloseBrace, dimensions, dimLengths, dim + 1, hasFailed);
		}
		else if (innerInitExpr != NULL)
		{
			hasFailed = true;
			mModule->Fail("Unexpected nested initializer", expr);
			ProcessArrayInitializer(innerInitExpr->mOpenBrace, innerInitExpr->mValues, innerInitExpr->mCommas, innerInitExpr->mCloseBrace, dimensions, dimLengths, dim + 1, hasFailed);
		}
		else
		{
			//mModule->Fail("Expected initializer", )
		}
	}
}

void BfExprEvaluator::CheckObjectCreateTypeRef(BfType* expectingType, BfAstNode* afterNode)
{
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (afterNode != NULL) && (autoComplete->mIsAutoComplete) &&
		(afterNode->IsFromParser(mModule->mCompiler->mResolvePassData->mParsers[0])) &&
		(afterNode->GetParser()->mCursorIdx == afterNode->GetSrcEnd() + 1))
	{
		BfType* expectingType = mExpectingType;
		BfTypeInstance* expectingTypeInst = NULL;

		if (mExpectingType != NULL)
		{
			expectingTypeInst = mExpectingType->ToTypeInstance();
		}

		if ((mExpectingType != NULL) && (((expectingTypeInst == NULL) || (!expectingTypeInst->mTypeDef->mIsDelegate))))
		{
			// Why were we doing this? It floods the autocomplete with every possible type
			//autoComplete->AddTopLevelTypes(NULL);
			autoComplete->mInsertStartIdx = afterNode->GetSourceData()->ToParser()->mCursorIdx;
			BF_ASSERT(autoComplete->mInsertStartIdx != -1);
			auto expectingType = mExpectingType;
			while (expectingType->IsArray())
			{
				auto arrayType = (BfArrayType*)expectingType;
				expectingType = arrayType->mGenericTypeInfo->mTypeGenericArguments[0];
			}

			auto expectingTypeInst = expectingType->ToTypeInstance();
			if (expectingTypeInst != NULL)
			{
				autoComplete->AddTypeInstanceEntry(expectingTypeInst);
			}
			else
				autoComplete->mDefaultSelection = mModule->TypeToString(expectingType);
		}
	}
}

void BfExprEvaluator::Visit(BfObjectCreateExpression* objCreateExpr)
{
	CreateObject(objCreateExpr, objCreateExpr->mNewNode, NULL);
}

void BfExprEvaluator::CreateObject(BfObjectCreateExpression* objCreateExpr, BfAstNode* allocNode, BfType* wantAllocType)
{
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (objCreateExpr != NULL) && (objCreateExpr->mTypeRef != NULL))
	{
		autoComplete->CheckTypeRef(objCreateExpr->mTypeRef, false, true);
	}

	if ((autoComplete != NULL) && (objCreateExpr != NULL) && (objCreateExpr->mOpenToken != NULL) && (objCreateExpr->mCloseToken != NULL) &&
		(objCreateExpr->mOpenToken->mToken == BfToken_LBrace) && (autoComplete->CheckFixit(objCreateExpr->mOpenToken)))
	{
		auto refNode = objCreateExpr->mOpenToken;
		BfParserData* parser = refNode->GetSourceData()->ToParserData();
		if (parser != NULL)
		{
			autoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("Change initializer braces to parentheses\treformat|%s|%d-1|(\x01|%s|%d-1|)",
				parser->mFileName.c_str(), refNode->mSrcStart,
				parser->mFileName.c_str(), objCreateExpr->mCloseToken->mSrcStart).c_str()));
		}
	}

	CheckObjectCreateTypeRef(mExpectingType, allocNode);

	BfAttributeState attributeState;
	attributeState.mTarget = BfAttributeTargets_Alloc;
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);
	BfTokenNode* newToken = NULL;
	BfAllocTarget allocTarget;
	ResolveAllocTarget(allocTarget, allocNode, newToken, &attributeState.mCustomAttributes);

	bool isScopeAlloc = newToken->GetToken() == BfToken_Scope;
	bool isAppendAlloc = newToken->GetToken() == BfToken_Append;
	bool isStackAlloc = (newToken->GetToken() == BfToken_Stack) || (isScopeAlloc);
	bool isArrayAlloc = false;// (objCreateExpr->mArraySizeSpecifier != NULL);
	bool isRawArrayAlloc = (objCreateExpr != NULL) && (objCreateExpr->mStarToken != NULL);

	if (isScopeAlloc)
	{
		if ((mBfEvalExprFlags & BfEvalExprFlags_FieldInitializer) != 0)
		{
			mModule->Warn(0, "This allocation will only be in scope during the constructor. Consider using a longer-term allocation such as 'new'", allocNode);
		}

		if (allocNode == newToken) // Scope, no target specified
		{
			if (mModule->mParentNodeEntry != NULL)
			{
				if (auto assignExpr = BfNodeDynCastExact<BfAssignmentExpression>(mModule->mParentNodeEntry->mNode))
				{
					if (mModule->mCurMethodState->mCurScope->mCloseNode == NULL)
					{
						// If we are assigning this to a property then it's possible the property setter can actually deal with a temporary allocation so no warning in that case
						if ((mBfEvalExprFlags & BfEvalExprFlags_PendingPropSet) == 0)
							mModule->Warn(0, "This allocation will immediately go out of scope. Consider specifying a wider scope target such as 'scope::'", allocNode);
					}
				}
			}
		}
	}

	BfAutoParentNodeEntry autoParentNodeEntry(mModule, objCreateExpr);

	SizedArray<BfAstNode*, 2> dimLengthRefs;
	SizedArray<BfIRValue, 2> dimLengthVals;

	BfArrayType* arrayType = NULL;

	BfType* unresolvedTypeRef = NULL;
 	BfType* resolvedTypeRef = NULL;
	if (wantAllocType != NULL)
	{
		unresolvedTypeRef = wantAllocType;
		resolvedTypeRef = wantAllocType;
	}
	else if (objCreateExpr->mTypeRef == NULL)
	{
		if ((!mExpectingType) || (!mExpectingType->IsArray()))
		{
			mModule->Fail("Cannot imply array type. Explicitly state array type or use array in an assignment to an array type.", objCreateExpr);
			resolvedTypeRef = mModule->mContext->mBfObjectType;
			unresolvedTypeRef = resolvedTypeRef;
		}
		else
		{
			auto arrayType = (BfArrayType*)mExpectingType;
			unresolvedTypeRef = arrayType->GetUnderlyingType();
			resolvedTypeRef = unresolvedTypeRef;
		}
	}
	else
	{
		if ((objCreateExpr->mTypeRef->IsExact<BfDotTypeReference>()) && (mExpectingType != NULL))
		{
			//mModule->SetElementType(objCreateExpr->mTypeRef, BfSourceElementType_TypeRef);

			if ((mExpectingType->IsObject()) || (mExpectingType->IsGenericParam()))
			{
				unresolvedTypeRef = mExpectingType;

				if (unresolvedTypeRef->IsArray())
				{
					arrayType = (BfArrayType*)unresolvedTypeRef;
					unresolvedTypeRef = unresolvedTypeRef->GetUnderlyingType();
					isArrayAlloc = true;
				}
			}
			else if (mExpectingType->IsPointer())
			{
				unresolvedTypeRef = mExpectingType->GetUnderlyingType();
			}
			else if (mExpectingType->IsVar())
				unresolvedTypeRef = mExpectingType;
		}

		if (unresolvedTypeRef == NULL)
		{
			if (auto arrayTypeRef = BfNodeDynCast<BfArrayTypeRef>(objCreateExpr->mTypeRef))
			{
				isArrayAlloc = true;
				if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(arrayTypeRef->mElementType))
				{
					if ((mExpectingType != NULL) &&
						((mExpectingType->IsArray()) || (mExpectingType->IsPointer()) || (mExpectingType->IsSizedArray())))
						unresolvedTypeRef = mExpectingType->GetUnderlyingType();
				}
				if (unresolvedTypeRef == NULL)
					unresolvedTypeRef = mModule->ResolveTypeRef(arrayTypeRef->mElementType);
				if (unresolvedTypeRef == NULL)
					unresolvedTypeRef = mModule->GetPrimitiveType(BfTypeCode_Var);

				int dimensions = 1;

				bool commaExpected = false;
				if (arrayTypeRef->mParams.size() != 0)
				{
					auto intType = mModule->ResolveTypeDef(mModule->mSystem->mTypeIntPtr);

					for (auto arg : arrayTypeRef->mParams)
					{
						if (auto tokenNode = BfNodeDynCastExact<BfTokenNode>(arg))
						{
							if (tokenNode->GetToken() == BfToken_Comma)
							{
								if (isRawArrayAlloc)
								{
									mModule->Fail("Sized arrays cannot be multidimensional.", tokenNode);
									continue;
								}
								dimensions++;

								if (dimensions == 5)
								{
									mModule->Fail("Too many array dimensions, consider using a jagged array.", tokenNode);
								}

								commaExpected = false;
								continue;
							}
						}
						auto expr = BfNodeDynCast<BfExpression>(arg);
						if ((isRawArrayAlloc) && (!dimLengthVals.IsEmpty()))
						{
							mModule->CreateValueFromExpression(expr, intType);
							continue;
						}

						dimLengthRefs.Add(expr);

						BfTypedValue dimLength;
						if (expr == NULL)
						{
							// Not specified
							dimLengthVals.push_back(BfIRValue());
							continue;
						}

						if (arg != NULL)
						{
							dimLength = mModule->CreateValueFromExpression(expr, intType, BfEvalExprFlags_NoCast);

							BfCastFlags castFlags = BfCastFlags_None;
							if ((dimLength) && (dimLength.mType->IsInteger()))
							{
								// Allow uint for size - just force to int
								if (!((BfPrimitiveType*)dimLength.mType)->IsSigned())
									castFlags = BfCastFlags_Explicit;
							}
							if (dimLength)
								dimLength = mModule->Cast(expr, dimLength, intType, castFlags);
						}

						if (commaExpected)
						{
							mModule->AssertErrorState();
							continue;
						}

						if (!dimLength)
						{
							dimLength = mModule->GetDefaultTypedValue(intType);
						}
						dimLengthVals.push_back(dimLength.mValue);
						commaExpected = true;
					}
				}

				if ((arrayTypeRef->mParams.size() == 0) && (objCreateExpr->mOpenToken == NULL))
					mModule->Fail("Array size or array initializer expected", arrayTypeRef->mOpenBracket);

				if (dimensions > 4)
					dimensions = 4;

				if ((!isRawArrayAlloc) && (!unresolvedTypeRef->IsVar()))
					arrayType = mModule->CreateArrayType(unresolvedTypeRef, dimensions);
			}

			if (unresolvedTypeRef == NULL)
			{
				unresolvedTypeRef = ResolveTypeRef(objCreateExpr->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);
			}
		}

		resolvedTypeRef = unresolvedTypeRef;
		if ((resolvedTypeRef != NULL) && (IsVar(resolvedTypeRef)))
			resolvedTypeRef = unresolvedTypeRef;
	}

	if (resolvedTypeRef == NULL)
	{
		unresolvedTypeRef = mModule->GetPrimitiveType(BfTypeCode_Var);
		resolvedTypeRef = unresolvedTypeRef;
	}
	auto resultType = resolvedTypeRef;

	if ((resolvedTypeRef->IsInterface()) && (!isArrayAlloc))
	{
		mModule->Fail("Cannot create an instance of an interface", objCreateExpr->mTypeRef);
		resolvedTypeRef = mModule->mContext->mBfObjectType;
	}

	BfTypeInstance* typeInstance = resolvedTypeRef->ToTypeInstance();
	int elementSize = resolvedTypeRef->mSize;
	int elementAlign = resolvedTypeRef->mAlign;
	BfIRType allocType = mModule->mBfIRBuilder->MapType(resolvedTypeRef);
	if (typeInstance != NULL)
	{
		if (!mModule->mCurTypeInstance->mResolvingVarField)
			mModule->PopulateType(typeInstance);

		if ((typeInstance->mTypeDef->mIsDelegate) && (!isArrayAlloc))
			mModule->Fail("Delegates must be constructed through delegate binding", objCreateExpr->mTypeRef);

		elementSize = BF_MAX(0, typeInstance->mInstSize);
		elementAlign = typeInstance->mInstAlign;
		allocType = mModule->mBfIRBuilder->MapTypeInst(typeInstance);
	}

	if (isAppendAlloc)
	{
		if (!mModule->mCurTypeInstance->IsObject())
		{
			mModule->Fail("Append allocations are only allowed in classes", allocNode);
			isAppendAlloc = false;
		}
		else if ((mBfEvalExprFlags & BfEvalExprFlags_VariableDeclaration) == 0)
		{
			mModule->Fail("Append allocations are only allowed as local variable initializers in constructor body", allocNode);
			isAppendAlloc = false;
		}
		else
		{
			auto methodDef = mModule->mCurMethodInstance->mMethodDef;
			if (methodDef->mMethodType == BfMethodType_CtorCalcAppend)
			{
				mModule->Fail("Append allocations are only allowed as local variable declarations in the main method body", allocNode);
				isAppendAlloc = false;
			}
			else if (!methodDef->mHasAppend)
			{
				mModule->Fail("Append allocations can only be used on constructors with [AllowAppend] specified", allocNode);
				isAppendAlloc = false;
			}
			else if (methodDef->mMethodType != BfMethodType_Ctor)
			{
				mModule->Fail("Append allocations are only allowed in constructors", allocNode);
				isAppendAlloc = false;
			}
			else if (methodDef->mIsStatic)
			{
				mModule->Fail("Append allocations are only allowed in non-static constructors", allocNode);
				isAppendAlloc = false;
			}
		}
	}

	if (isArrayAlloc)
	{
		const int MAX_DIMENSIONS = 2;

		int dimensions = 1;
		if (arrayType != NULL)
		{
			dimensions = arrayType->mDimensions;
			mModule->AddDependency(arrayType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
		}

		bool zeroMemory = true;
		if (objCreateExpr->mOpenToken != NULL)
		{
			if ((objCreateExpr->mArguments.size() == 1) &&
				(BfNodeDynCastExact<BfUninitializedExpression>(objCreateExpr->mArguments[0]) != NULL))
			{
				// Special case for a single "{ ? }"
				zeroMemory = false;
			}
			else
			{
				SizedArray<int64, 2> dimLengths;

				if (dimLengthVals.size() != 0)
				{
					for (int dim = 0; dim < dimensions; dim++)
					{
						BfIRValue dimLengthVal;
						if (dim < (int)dimLengthVals.size())
							dimLengthVal = dimLengthVals[dim];
						if (!dimLengthVal)
						{
							dimLengths.push_back(-1);
							continue;
						}

						auto constant = mModule->mBfIRBuilder->GetConstant(dimLengthVal);
						if ((constant != NULL) && (mModule->mBfIRBuilder->IsInt(constant->mTypeCode)))
						{
							int64 dimLength = constant->mInt64;
							if (dimLength < 0)
							{
								mModule->Fail(StrFormat("Invalid array dimension '%lld'", dimLength), dimLengthRefs[dim]);
								dimLength = -1;
							}
							dimLengths.push_back(dimLength);
						}
						else if ((constant != NULL) && (constant->mConstType == BfConstType_Undef))
						{
							dimLengths.push_back(-1);
						}
						else
						{
							mModule->Fail("A constant length is required when using an initializer", dimLengthRefs[dim]);
							dimLengths.push_back(-1);
						}
					}
				}

				// Ending in an ", )" means we need to zero-fill ending
				zeroMemory = objCreateExpr->mCommas.size() >= objCreateExpr->mArguments.size();

				bool hasFailed = false;
				ProcessArrayInitializer(objCreateExpr->mOpenToken, objCreateExpr->mArguments, objCreateExpr->mCommas, objCreateExpr->mCloseToken, dimensions, dimLengths, 0, hasFailed);

				dimLengthVals.resize(dimLengths.size());

				for (int i = 0; i < (int)dimLengthVals.size(); i++)
				{
					if (!dimLengthVals[i])
					{
						auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
						dimLengthVals[i] = mModule->GetConstValue(dimLengths[i], intType);
					}
				}
			}
		}

		while ((int)dimLengthVals.size() < dimensions)
			dimLengthVals.push_back(mModule->GetConstValue(0));

		BfTypedValue arrayValue;

		BfIRValue arraySize;
		for (BfIRValue dimSize : dimLengthVals)
		{
			if (!arraySize)
				arraySize = dimSize;
			else
				arraySize = mModule->mBfIRBuilder->CreateMul(arraySize, dimSize);
		}

		BfAllocFlags allocFlags = BfAllocFlags_None;
		if (isRawArrayAlloc)
			allocFlags = (BfAllocFlags)(allocFlags | BfAllocFlags_RawArray);

		int writeIdx = 0;

		struct BfInitContext
		{
		public:
			BfModule* mModule;
			BfType* resultType;
			int dimensions;
			SizedArray<BfIRValue, 2>& dimLengthVals;
			BfIRValue arraySize;
			int& writeIdx;

			BfInitContext(BfModule* module, BfType* resultType, int dimensions, SizedArray<BfIRValue, 2>& dimLengthVals, BfIRValue arraySize, int& writeIdx) :
				mModule(module), resultType(resultType), dimensions(dimensions), dimLengthVals(dimLengthVals), arraySize(arraySize), writeIdx(writeIdx)
			{
			}

			void Handle(BfIRValue addr, int curDim, const BfSizedArray<BfExpression*>& valueExprs)
			{
				int exprIdx = 0;
				int dimWriteIdx = 0;
				bool isUninit = false;

				int dimLength = -1;
				if (dimLengthVals[curDim].IsConst())
				{
					auto constant = mModule->mBfIRBuilder->GetConstant(dimLengthVals[curDim]);
					dimLength = constant->mInt32;
				}

				while (exprIdx < (int)valueExprs.size())
				{
					auto initExpr = valueExprs[exprIdx];
					exprIdx++;
					if (!initExpr)
						break;
					if (auto unintExpr = BfNodeDynCastExact<BfUninitializedExpression>(initExpr))
					{
						isUninit = true;
						break;
					}

					if (exprIdx > dimLength)
						break;

					if (curDim < dimensions - 1)
					{
						if (auto innerTupleExpr = BfNodeDynCast<BfTupleExpression>(initExpr))
						{
							Handle(addr, curDim + 1, innerTupleExpr->mValues);
						}
						else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(initExpr))
						{
							SizedArray<BfExpression*, 1> values;
							values.Add(parenExpr->mExpression);
							Handle(addr, curDim + 1, values);
						}
						else if (auto innerInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(initExpr))
						{
							Handle(addr, curDim + 1, innerInitExpr->mValues);
						}

						dimWriteIdx++;
						continue;
					}

					BfIRValue elemAddr;
					if (!resultType->IsValuelessType())
						elemAddr = mModule->CreateIndexedValue(resultType, addr, writeIdx);
					else
						elemAddr = mModule->mBfIRBuilder->GetFakeVal();
					writeIdx++;
					dimWriteIdx++;

					BfTypedValue elemPtrTypedVal = BfTypedValue(elemAddr, resultType, BfTypedValueKind_Addr);

					BfExprEvaluator exprEvaluator(mModule);
					exprEvaluator.mExpectingType = resultType;
					exprEvaluator.mReceivingValue = &elemPtrTypedVal;
					exprEvaluator.Evaluate(initExpr);
					exprEvaluator.GetResult();

					if (exprEvaluator.mReceivingValue == NULL)
					{
						// We wrote directly to the array in-place, we're done with this element
						continue;
					}
					auto storeValue = exprEvaluator.mResult;
					if (!storeValue)
						continue;
					storeValue = mModule->Cast(initExpr, storeValue, resultType);
					if (!storeValue)
						continue;
					if (!resultType->IsValuelessType())
					{
						storeValue = mModule->LoadOrAggregateValue(storeValue);
						mModule->mBfIRBuilder->CreateStore(storeValue.mValue, elemAddr);
					}
				}

				int clearFromIdx = writeIdx;
				int sectionElemCount = 1;

				BfIRValue numElemsLeft = arraySize;
				if (dimLength != -1)
				{
					int clearCount = dimLength - dimWriteIdx;
					if (clearCount > 0)
					{
						for (int checkDim = curDim + 1; checkDim < (int)dimLengthVals.size(); checkDim++)
						{
							if (dimLengthVals[checkDim].IsConst())
							{
								auto constant = mModule->mBfIRBuilder->GetConstant(dimLengthVals[checkDim]);
								clearCount *= constant->mInt32;
								sectionElemCount *= constant->mInt32;
							}
						}
					}

					writeIdx += clearCount;
					numElemsLeft = mModule->GetConstValue(clearCount);
				}

				// Actually leave it alone?
				if ((isUninit) &&
					((mModule->IsOptimized()) || (mModule->mIsComptimeModule) || (mModule->mBfIRBuilder->mIgnoreWrites)))
					return;

				bool doClear = true;
				if (numElemsLeft.IsConst())
				{
					auto constant = mModule->mBfIRBuilder->GetConstant(numElemsLeft);
					doClear = constant->mInt64 > 0;
				}
				if (doClear)
				{
					// We multiply by GetStride.  This relies on the fact that we over-allocate on the array allocation -- the last
					//  element doesn't need to be padded out to the element alignment, but we do anyway.  Otherwise this would be
					//  a more complicated computation
					auto clearBytes = mModule->mBfIRBuilder->CreateMul(numElemsLeft, mModule->GetConstValue(resultType->GetStride()));

					if (isUninit)
					{
						// Limit to a reasonable number of bytes to stomp with 0xCC
						int maxStompBytes = BF_MIN(128, resultType->GetStride() * sectionElemCount);
						if (clearBytes.IsConst())
						{
							auto constant = mModule->mBfIRBuilder->GetConstant(clearBytes);
							if (constant->mInt64 > maxStompBytes)
								clearBytes = mModule->GetConstValue(maxStompBytes);
						}
						else
						{
							auto insertBlock = mModule->mBfIRBuilder->GetInsertBlock();

							auto gtBlock = mModule->mBfIRBuilder->CreateBlock("unint.gt");
							auto contBlock = mModule->mBfIRBuilder->CreateBlock("unint.cont");

							auto cmp = mModule->mBfIRBuilder->CreateCmpLTE(clearBytes, mModule->GetConstValue(maxStompBytes), true);
							mModule->mBfIRBuilder->CreateCondBr(cmp, contBlock, gtBlock);

							mModule->mBfIRBuilder->AddBlock(gtBlock);
							mModule->mBfIRBuilder->SetInsertPoint(gtBlock);
							mModule->mBfIRBuilder->CreateBr(contBlock);

							mModule->mBfIRBuilder->AddBlock(contBlock);
							mModule->mBfIRBuilder->SetInsertPoint(contBlock);
							auto phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(mModule->GetPrimitiveType(BfTypeCode_IntPtr)), 2);
							mModule->mBfIRBuilder->AddPhiIncoming(phi, clearBytes, insertBlock);
							mModule->mBfIRBuilder->AddPhiIncoming(phi, mModule->GetConstValue(maxStompBytes), gtBlock);

							clearBytes = phi;
						}
					}

					mModule->mBfIRBuilder->PopulateType(resultType);
					if (!resultType->IsValuelessType())
					{
						mModule->mBfIRBuilder->CreateMemSet(mModule->CreateIndexedValue(resultType, addr, clearFromIdx),
							mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int8, isUninit ? 0xCC : 0), clearBytes, resultType->mAlign);
					}
				}
			}
		};

		BfInitContext initContext(mModule, resultType, dimensions, dimLengthVals, arraySize, writeIdx);

		if (IsVar(resultType))
		{
			SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
			mResult = BfTypedValue(BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), mModule->GetPrimitiveType(BfTypeCode_Var)));
			initContext.Handle(mResult.mValue, 0, objCreateExpr->mArguments);
			return;
		}

		if (isRawArrayAlloc)
		{
			// If we have a constant-sized alloc then make the type a pointer to the sized array, otherwise just a pointer to the raw type
			BfType* ptrType = mModule->CreatePointerType(resultType);
			if (isAppendAlloc)
				arrayValue = BfTypedValue(mModule->AppendAllocFromType(resultType, BfIRValue(), 0, arraySize, (int)dimLengthVals.size(), isRawArrayAlloc, false), ptrType);
			else
			{
				arrayValue = BfTypedValue(mModule->AllocFromType(resultType, allocTarget, BfIRValue(), arraySize, (int)dimLengthVals.size(), allocFlags, allocTarget.mAlignOverride), ptrType);
			}

			initContext.Handle(arrayValue.mValue, 0, objCreateExpr->mArguments);

			mResult = arrayValue;
			return;
		}

		if (dimLengthVals.size() > 4)
		{
			dimLengthVals.RemoveRange(4, dimLengthVals.size() - 4);
			mModule->Fail("Too many array dimensions, consider using a jagged array.", objCreateExpr);
		}

		if (arrayType == NULL)
			return;

		if (isAppendAlloc)
			arrayValue = BfTypedValue(mModule->AppendAllocFromType(resultType, BfIRValue(), 0, arraySize, (int)dimLengthVals.size(), isRawArrayAlloc, zeroMemory), arrayType);
		else
		{
			arrayValue = BfTypedValue(mModule->AllocFromType(resultType, allocTarget, BfIRValue(), arraySize, (int)dimLengthVals.size(), allocFlags, allocTarget.mAlignOverride), arrayType);

			if (isScopeAlloc)
			{
				// See notes below on "general" SkipObjectAccessCheck usage on why we can do this
				mModule->SkipObjectAccessCheck(arrayValue);
			}
		}
		//mModule->InitTypeInst(arrayValue, scopeData);

		BfAstNode* refNode = objCreateExpr->mTypeRef;
		while (true)
		{
			if (auto arrayRef = BfNodeDynCast<BfElementedTypeRef>(refNode))
			{
				refNode = arrayRef->mElementType;
				continue;
			}
			break;
		}

		BfResolvedArgs resolvedArgs;

		auto rawAutoComplete = mModule->mCompiler->GetAutoComplete();
		if (rawAutoComplete != NULL)
		{
			SetAndRestoreValue<bool> prevCapturing(rawAutoComplete->mIsCapturingMethodMatchInfo, false);
			MatchConstructor(refNode, objCreateExpr, arrayValue, arrayType, resolvedArgs, false, false);
		}
		else
		{
			MatchConstructor(refNode, objCreateExpr, arrayValue, arrayType, resolvedArgs, false, false);
		}

		//TODO: Assert 'length' var is at slot 1
		mModule->PopulateType(arrayType->mBaseType, BfPopulateType_DataAndMethods);
		mModule->mBfIRBuilder->PopulateType(arrayType);
		auto arrayBits = mModule->mBfIRBuilder->CreateBitCast(arrayValue.mValue, mModule->mBfIRBuilder->MapTypeInstPtr(arrayType->mBaseType));
		int arrayLengthBitCount = arrayType->GetLengthBitCount();
		if (arrayLengthBitCount == 0)
		{
			mModule->Fail("INTERNAL ERROR: Unable to find array 'length' field", objCreateExpr);
			return;
		}

		mResult = arrayValue;

		auto lengthFieldInstance = mModule->GetFieldByName(arrayType->mBaseType->ToTypeInstance(), "mLength");
		if (lengthFieldInstance == NULL)
			return;
		auto firstElementFieldInstance = mModule->GetFieldByName(arrayType->ToTypeInstance(), "mFirstElement");
		if (firstElementFieldInstance == NULL)
			return;

		auto addr = mModule->mBfIRBuilder->CreateInBoundsGEP(arrayBits, 0, lengthFieldInstance->mDataIdx);

		if (arrayLengthBitCount == 64)
			mModule->mBfIRBuilder->CreateAlignedStore(arraySize, addr, 8);
		else
		{
			auto arraySize32 = mModule->mBfIRBuilder->CreateNumericCast(arraySize, true, BfTypeCode_Int32);
			mModule->mBfIRBuilder->CreateAlignedStore(arraySize32, addr, 4);
		}

		for (int lowerDim = 1; lowerDim < (int)dimLengthVals.size(); lowerDim++)
		{
			auto length1FieldInstance = mModule->GetFieldByName(arrayType->ToTypeInstance(), "mLength1");
			if (length1FieldInstance == NULL)
				return;

			addr = mModule->mBfIRBuilder->CreateInBoundsGEP(arrayValue.mValue, 0, length1FieldInstance->mDataIdx + lowerDim - 1);

			auto lowerDimVal = mModule->mBfIRBuilder->CreateNumericCast(dimLengthVals[lowerDim], true, (arrayLengthBitCount == 64) ? BfTypeCode_Int64 : BfTypeCode_Int32);
			mModule->mBfIRBuilder->CreateStore(lowerDimVal, addr);
		}

		if (resultType->IsValuelessType())
			addr = mModule->mBfIRBuilder->GetFakeVal();
		else
			addr = mModule->mBfIRBuilder->CreateInBoundsGEP(arrayValue.mValue, 0, firstElementFieldInstance->mDataIdx);

		initContext.Handle(addr, 0, objCreateExpr->mArguments);

		return;
	}
	else
	{
		if (resolvedTypeRef->IsVar())
		{
			// Leave as a var
		}
		else if ((!resolvedTypeRef->IsObjectOrInterface()) && (!resolvedTypeRef->IsGenericParam()))
		{
			resultType = mModule->CreatePointerType(resolvedTypeRef);
		}
	}

	if ((isStackAlloc) && (mModule->mCurMethodState == NULL))
	{
		mModule->Fail("Cannot use 'stack' here", allocNode);
		isStackAlloc = false;
		isScopeAlloc = false;
	}

	bool isGenericParam = unresolvedTypeRef->IsGenericParam();
	if (resolvedTypeRef->IsGenericParam())
	{
		BfGenericParamFlags genericParamFlags = BfGenericParamFlag_None;
		BfType* typeConstraint = NULL;
		auto genericParam = mModule->GetMergedGenericParamData((BfGenericParamType*)resolvedTypeRef, genericParamFlags, typeConstraint);

		if (typeConstraint == NULL)
		{
			if ((genericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				// Allow it
			}
			else
			{
				if ((genericParamFlags & BfGenericParamFlag_New) == 0)
				{
					mModule->Fail(StrFormat("Must add 'where %s : new' constraint to generic parameter to instantiate type", genericParam->GetName().c_str()), objCreateExpr->mTypeRef);
				}
				if (objCreateExpr->mArguments.size() != 0)
				{
					mModule->Fail(StrFormat("Only default parameterless constructors can be called on generic argument '%s'", genericParam->GetName().c_str()), objCreateExpr->mTypeRef);
				}
			}
		}

		if (((typeConstraint != NULL) && (typeConstraint->IsValueType())) ||
			((genericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr)) != 0))
		{
			resultType = mModule->CreatePointerType(resolvedTypeRef);
		}
		else if (((typeConstraint != NULL) && (!typeConstraint->IsValueType())) ||
			((genericParamFlags & (BfGenericParamFlag_Class)) != 0))
		{
			// Leave as 'T'
			resultType = resolvedTypeRef;
		}
		else
			resultType = mModule->CreateModifiedTypeType(resolvedTypeRef, BfToken_AllocType);

		mResult.mType = resultType;

		if (typeInstance == NULL)
		{
			mResult = mModule->GetDefaultTypedValue(resultType);
			return;
		}
	}
	else if (resolvedTypeRef->IsSizedArray())
	{
		// Handle the case of "int[3]* val = new .(1, 2, 3)"
		if (auto dotTypeRef = BfNodeDynCastExact<BfDotTypeReference>(objCreateExpr->mTypeRef))
		{
			BfIRValue allocValue;
			if (isAppendAlloc)
				allocValue = mModule->AppendAllocFromType(resolvedTypeRef, BfIRValue(), 0, BfIRValue(), 0, false, false);
			else
				allocValue = mModule->AllocFromType(resolvedTypeRef, allocTarget, BfIRValue(), BfIRValue(), 0, BfAllocFlags_None);

			auto result = BfTypedValue(allocValue, resolvedTypeRef, BfTypedValueKind_Addr);
			InitializedSizedArray((BfSizedArrayType*)resolvedTypeRef, objCreateExpr->mOpenToken, objCreateExpr->mArguments, objCreateExpr->mCommas, objCreateExpr->mCloseToken, &result);

			// Turn from an addr of a sized array to pointer of sized array
			mResult = BfTypedValue(mResult.mValue, resultType);
			return;
		}
	}

	SetAndRestoreValue<bool> prevNoBind(mNoBind, mNoBind || isGenericParam);

	if ((typeInstance != NULL) && (typeInstance->mTypeDef->mIsAbstract))
	{
		mModule->Fail("Cannot create an instance of an abstract class", objCreateExpr->mTypeRef);
		return;
	}

	BfFunctionBindResult bindResult;
	bindResult.mSkipThis = true;
	bindResult.mWantsArgs = true;
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, &bindResult);

	BfIRValue appendSizeValue;
	BfTypedValue emtpyThis(mModule->mBfIRBuilder->GetFakeVal(), resolvedTypeRef, resolvedTypeRef->IsStruct());

	BfResolvedArgs argValues;
	if (objCreateExpr != NULL)
	{
		argValues.Init(objCreateExpr->mOpenToken, &objCreateExpr->mArguments, &objCreateExpr->mCommas, objCreateExpr->mCloseToken);
		ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval); ////
	}

	if (typeInstance == NULL)
	{
		// No CTOR needed
		if (objCreateExpr->mArguments.size() != 0)
		{
			mModule->Fail(StrFormat("Only default parameterless constructors can be called on primitive type '%s'", mModule->TypeToString(resolvedTypeRef).c_str()), objCreateExpr->mTypeRef);
		}
	}
	else if ((autoComplete != NULL) && (objCreateExpr != NULL) && (objCreateExpr->mOpenToken != NULL))
	{
		auto wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;
		autoComplete->CheckInvocation(objCreateExpr, objCreateExpr->mOpenToken, objCreateExpr->mCloseToken, objCreateExpr->mCommas);
		MatchConstructor(objCreateExpr->mTypeRef, objCreateExpr, emtpyThis, typeInstance, argValues, false, true);
		if ((wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo))
		{
			if (autoComplete->mMethodMatchInfo != NULL)
				autoComplete->mIsCapturingMethodMatchInfo = true;
		}
		else
			autoComplete->mIsCapturingMethodMatchInfo = false;
	}
	else if (!resolvedTypeRef->IsFunction())
	{
		auto refNode = allocNode;
		if (objCreateExpr != NULL)
			refNode = objCreateExpr->mTypeRef;
		MatchConstructor(refNode, objCreateExpr, emtpyThis, typeInstance, argValues, false, true);
	}
	if (objCreateExpr != NULL)
		mModule->ValidateAllocation(typeInstance, objCreateExpr->mTypeRef);

	prevBindResult.Restore();

	int allocAlign = resolvedTypeRef->mAlign;
	if (typeInstance != NULL)
		allocAlign = typeInstance->mInstAlign;
	int appendAllocAlign = 0;

	if ((bindResult.mMethodInstance != NULL) && (bindResult.mMethodInstance->mMethodDef->mHasAppend))
	{
		if (!bindResult.mFunc)
		{
			BF_ASSERT((!mModule->HasExecutedOutput()) || (mModule->mBfIRBuilder->mIgnoreWrites));
			appendSizeValue = mModule->GetConstValue(0);
		}
		else
		{
			auto calcAppendMethodModule = mModule->GetMethodInstanceAtIdx(bindResult.mMethodInstance->GetOwner(), bindResult.mMethodInstance->mMethodDef->mIdx + 1, BF_METHODNAME_CALCAPPEND);

			SizedArray<BfIRValue, 2> irArgs;
			if (bindResult.mIRArgs.size() > 1)
				irArgs.Insert(0, &bindResult.mIRArgs[1], bindResult.mIRArgs.size() - 1);
			BfTypedValue appendSizeTypedValue = mModule->TryConstCalcAppend(calcAppendMethodModule.mMethodInstance, irArgs);
			if (!appendSizeTypedValue)
			{
				BF_ASSERT(calcAppendMethodModule.mFunc);

				appendSizeTypedValue = CreateCall(objCreateExpr, calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, irArgs);
				BF_ASSERT(appendSizeTypedValue.mType == mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			}
			appendSizeValue = appendSizeTypedValue.mValue;
			allocAlign = BF_MAX(allocAlign, calcAppendMethodModule.mMethodInstance->mAppendAllocAlign);
			appendAllocAlign = calcAppendMethodModule.mMethodInstance->mAppendAllocAlign;
		}

		if (appendAllocAlign != 0)
		{
			int endingAlign = typeInstance->GetEndingInstanceAlignment();
			if (endingAlign % appendAllocAlign != 0)
			{
				int extraSize = appendAllocAlign - (endingAlign % appendAllocAlign);
				appendSizeValue = mModule->mBfIRBuilder->CreateAdd(appendSizeValue, mModule->GetConstValue(extraSize));
			}
		}
	}

	// WTF? I'm not even sure this is correct - add more tests
	appendAllocAlign = BF_MAX(appendAllocAlign, allocAlign);

	BfIRValue allocValue;
	if (IsVar(resolvedTypeRef))
	{
		mResult = mModule->GetDefaultTypedValue(resultType);
		return;
	}
	else
	{
		if (isAppendAlloc)
		{
			allocValue = mModule->AppendAllocFromType(resolvedTypeRef, appendSizeValue, appendAllocAlign);
		}
		else
		{
			allocValue = mModule->AllocFromType(resolvedTypeRef, allocTarget, appendSizeValue, BfIRValue(), 0, BfAllocFlags_None, allocAlign);
		}
		if (((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) && (mModule->mCompiler->mCeMachine != NULL))
		{
			mModule->mCompiler->mCeMachine->SetAppendAllocInfo(mModule, allocValue, appendSizeValue);
		}
		mResult = BfTypedValue(allocValue, resultType);
	}

	if (isScopeAlloc)
	{
		// This allows readonly (ie: 'let') local usage to not require an access check.  No matter what scope the alloc is tied to, the
		//  lifetime of the local variable will be no longer than that of the allocated value
		mModule->SkipObjectAccessCheck(mResult);
	}

	/*if (typeInstance != NULL)
	{
		mModule->InitTypeInst(mResult, scopeData, true);
	}
	if (isStackAlloc)
	{
		mModule->AddStackAlloc(mResult, objCreateExpr, scopeData);
	}*/

	if (mResult)
	{
		if (bindResult.mMethodInstance == NULL)
		{
			// Why did we have this?  It was already zeroed right?
			// Zero
			//mModule->mBfIRBuilder->CreateMemSet(mResult.mValue, mModule->GetConstValue8(0), mModule->GetConstValue(resolvedTypeRef->mSize), resolvedTypeRef->mAlign);
		}
		else if (bindResult.mFunc)
		{
			if (typeInstance->IsObject())
			{
				bool hasRealtimeLeakCheck = (mModule->mCompiler->mOptions.mEnableRealtimeLeakCheck) && (!IsComptime());

				bool wantsCtorClear = true;
				if (hasRealtimeLeakCheck)
				{
					// Dbg_ObjectAlloc clears internally so we don't need to call CtorClear for those
					if ((!isStackAlloc) && (!isAppendAlloc) && (!allocTarget.mCustomAllocator) && (allocTarget.mScopedInvocationTarget == NULL))
						wantsCtorClear = false;
				}

				if (wantsCtorClear)
				{
					auto ctorClear = mModule->GetMethodByName(typeInstance, "__BfCtorClear");
					if (!ctorClear)
					{
						mModule->AssertErrorState();
					}
					else if ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) == 0)
					{
						SizedArray<BfIRValue, 1> irArgs;
						irArgs.push_back(mResult.mValue);
						CreateCall(objCreateExpr, ctorClear.mMethodInstance, ctorClear.mFunc, false, irArgs);
					}
				}

				if ((!mModule->mIsComptimeModule) && (isStackAlloc) && (hasRealtimeLeakCheck))
				{
					BfMethodInstance* markMethod = mModule->GetRawMethodByName(mModule->mContext->mBfObjectType, "GCMarkMembers");
					BF_ASSERT(markMethod != NULL);
					if (markMethod != NULL)
					{
						auto& vtableEntry = typeInstance->mVirtualMethodTable[markMethod->mVirtualTableIdx];
						if (vtableEntry.mImplementingMethod.mTypeInstance != mModule->mContext->mBfObjectType)
						{
							auto impMethodInstance = (BfMethodInstance*)vtableEntry.mImplementingMethod;
							bool needsCall = false;
							if (impMethodInstance != NULL)
							{
								needsCall = impMethodInstance->mMethodDef->mBody != NULL;
							}
							else
							{
								needsCall = true;
								BF_ASSERT(vtableEntry.mImplementingMethod.mKind == BfMethodRefKind_AmbiguousRef);
							}

							if (!needsCall)
							{
								for (auto& fieldInst : typeInstance->mFieldInstances)
								{
									if (fieldInst.IsAppendedObject())
									{
										needsCall = true;
										break;
									}
								}
							}

							if (needsCall)
							{
								SizedArray<BfIRValue, 1> irArgs;
								irArgs.push_back(mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(mModule->mContext->mBfObjectType)));

								auto gcType = mModule->ResolveTypeDef(mModule->mCompiler->mGCTypeDef);
								BF_ASSERT(gcType != NULL);
								if (gcType != NULL)
								{
									auto addStackObjMethod = mModule->GetMethodByName(gcType->ToTypeInstance(), "AddStackMarkableObject", 1);
									BF_ASSERT(addStackObjMethod);
									if (addStackObjMethod)
									{
										mModule->mBfIRBuilder->CreateCall(addStackObjMethod.mFunc, irArgs);
									}

									auto removeStackObjMethod = mModule->GetMethodByName(gcType->ToTypeInstance(), "RemoveStackMarkableObject", 1);
									BF_ASSERT(removeStackObjMethod);
									if (removeStackObjMethod)
									{
										mModule->AddDeferredCall(removeStackObjMethod, irArgs, allocTarget.mScopeData, allocNode);
									}
								}
							}
						}
					}
				}
			}

			if ((bindResult.mMethodInstance->mMethodDef->mHasAppend) && (mResult.mType->IsObject()))
			{
				BF_ASSERT(bindResult.mIRArgs[0].IsFake());
				auto typeInst = mResult.mType->ToTypeInstance();
				auto intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				auto thisVal = mResult;
				BfIRValue intPtrVal = mModule->CreateAlloca(intPtrType);
				auto intPtrThisVal = mModule->mBfIRBuilder->CreatePtrToInt(thisVal.mValue, (intPtrType->mSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64);
				auto curValPtr = mModule->mBfIRBuilder->CreateAdd(intPtrThisVal, mModule->GetConstValue(typeInst->mInstSize, intPtrType));
				mModule->mBfIRBuilder->CreateStore(curValPtr, intPtrVal);
				bindResult.mIRArgs[0] = intPtrVal;
			}
			if (!typeInstance->IsValuelessType())
				bindResult.mIRArgs.Insert(0, mResult.mValue);
			auto result = CreateCall(objCreateExpr, bindResult.mMethodInstance, bindResult.mFunc, false, bindResult.mIRArgs);
			if ((result) && (!result.mType->IsVoid()))
				mResult = result;
		}
	}

	if (((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0) && (mModule->mCompiler->mCeMachine != NULL))
	{
		mModule->mCompiler->mCeMachine->ClearAppendAllocInfo();
	}
}

void BfExprEvaluator::Visit(BfBoxExpression* boxExpr)
{
	/*if ((boxExpr->mAllocNode == NULL) || (boxExpr->mExpression == NULL))
	{
		mModule->AssertErrorState();
		return;
	}*/

	BfTokenNode* newToken = NULL;
	BfAllocTarget allocTarget;
	ResolveAllocTarget(allocTarget, boxExpr->mAllocNode, newToken);

	if ((boxExpr->mAllocNode != NULL) && (boxExpr->mAllocNode->mToken == BfToken_Scope))
	{
		if ((mBfEvalExprFlags & BfEvalExprFlags_FieldInitializer) != 0)
		{
			mModule->Warn(0, "This allocation will only be in scope during the constructor. Consider using a longer-term allocation such as 'new'", boxExpr->mAllocNode);
		}
	}

	if (boxExpr->mExpression == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	auto exprValue = mModule->CreateValueFromExpression(boxExpr->mExpression);
	if (exprValue)
	{
		bool doFail = false;
		bool doWarn = false;
		BfType* boxedType = NULL;

		if (exprValue.mType->IsGenericParam())
		{
			BF_ASSERT(mModule->mCurMethodInstance->mIsUnspecialized);

			auto genericParamTarget = (BfGenericParamType*)exprValue.mType;
			auto genericParamInstance = mModule->GetGenericParamInstance(genericParamTarget);

			if ((genericParamInstance->mGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Class)) == BfGenericParamFlag_Class)
				doWarn = true;

			if ((genericParamInstance->mTypeConstraint != NULL) && (genericParamInstance->mTypeConstraint->IsObjectOrInterface()))
				doWarn = true;

			boxedType = mModule->mContext->mBfObjectType;
		}
		else
		{
			doFail = !exprValue.mType->IsValueTypeOrValueTypePtr();
			doWarn = exprValue.mType->IsObjectOrInterface();
		}

		if (doWarn)
		{
			mModule->Warn(0, StrFormat("Boxing is unnecessary since type '%s' is already a reference type.", mModule->TypeToString(exprValue.mType).c_str()), boxExpr->mExpression);
			mResult = exprValue;
			return;
		}

		if (doFail)
		{
			mModule->Fail(StrFormat("Box target '%s' must be a value type or pointer to a value type", mModule->TypeToString(exprValue.mType).c_str()), boxExpr->mExpression);
			return;
		}

		if (boxedType == NULL)
			boxedType = mModule->CreateBoxedType(exprValue.mType);
		if (boxedType == NULL)
			boxedType = mModule->mContext->mBfObjectType;
		mResult = mModule->BoxValue(boxExpr->mExpression, exprValue, boxedType, allocTarget);
		if (!mResult)
		{
			mModule->Fail(StrFormat("Type '%s' is not boxable", mModule->TypeToString(exprValue.mType).c_str()), boxExpr->mExpression);
			return;
		}
	}
}

void BfExprEvaluator::ResolveAllocTarget(BfAllocTarget& allocTarget, BfAstNode* allocNode, BfTokenNode*& newToken, BfCustomAttributes** outCustomAttributes)
{
	auto autoComplete = GetAutoComplete();
	BfAttributeDirective* attributeDirective = NULL;

	allocTarget.mRefNode = allocNode;
	newToken = BfNodeDynCast<BfTokenNode>(allocNode);
	if (newToken == NULL)
	{
		if (auto scopeNode = BfNodeDynCast<BfScopeNode>(allocNode))
		{
			newToken = scopeNode->mScopeToken;
			allocTarget.mScopeData = mModule->FindScope(scopeNode->mTargetNode, true);

			if (autoComplete != NULL)
			{
				auto targetIdentifier = BfNodeDynCast<BfIdentifierNode>(scopeNode->mTargetNode);
				if ((scopeNode->mTargetNode == NULL) || (targetIdentifier != NULL))
					autoComplete->CheckLabel(targetIdentifier, scopeNode->mColonToken, allocTarget.mScopeData);
			}
			attributeDirective = scopeNode->mAttributes;
		}
		if (auto newNode = BfNodeDynCast<BfNewNode>(allocNode))
		{
			newToken = newNode->mNewToken;
			if (auto allocExpr = BfNodeDynCast<BfExpression>(newNode->mAllocNode))
			{
				allocTarget.mCustomAllocator = mModule->CreateValueFromExpression(allocExpr);
				allocTarget.mRefNode = allocExpr;
			}
			else if (auto scopedInvocationTarget = BfNodeDynCast<BfScopedInvocationTarget>(newNode->mAllocNode))
			{
				allocTarget.mScopedInvocationTarget = scopedInvocationTarget;
			}
			attributeDirective = newNode->mAttributes;
		}
	}
	else if (newToken->GetToken() == BfToken_Scope)
	{
		if (mModule->mCurMethodState != NULL)
			allocTarget.mScopeData = mModule->mCurMethodState->mCurScope->GetTargetable();
	}
	else if (newToken->GetToken() == BfToken_Stack)
	{
		if (mModule->mCurMethodState != NULL)
			allocTarget.mScopeData = &mModule->mCurMethodState->mHeadScope;
	}

	if (attributeDirective != NULL)
	{
		auto customAttrs = mModule->GetCustomAttributes(attributeDirective, BfAttributeTargets_Alloc, BfGetCustomAttributesFlags_AllowNonConstArgs, allocTarget.mCaptureInfo);
		if (customAttrs != NULL)
		{
			for (auto& attrib : customAttrs->mAttributes)
			{
				if (attrib.mType->IsInstanceOf(mModule->mCompiler->mAlignAttributeTypeDef))
				{
					allocTarget.mAlignOverride = 16; // System conservative default

					if (!attrib.mCtorArgs.IsEmpty())
					{
						BfIRConstHolder* constHolder = mModule->mCurTypeInstance->mConstHolder;
						auto constant = constHolder->GetConstant(attrib.mCtorArgs[0]);
						if (constant != NULL)
						{
							int alignOverride = (int)BF_MAX(1, constant->mInt64);
							if ((alignOverride & (alignOverride - 1)) == 0)
								allocTarget.mAlignOverride = alignOverride;
							else
								mModule->Fail("Alignment must be a power of 2", attrib.GetRefNode());
						}
					}
				}
				else if (attrib.mType->IsInstanceOf(mModule->mCompiler->mFriendAttributeTypeDef))
					allocTarget.mIsFriend = true;
			}

			if (outCustomAttributes != NULL)
				*outCustomAttributes = customAttrs;
			else
				delete customAttrs;
		}
	}
}

BfTypedValue BfExprEvaluator::MakeCallableTarget(BfAstNode* targetSrc, BfTypedValue target)
{
	if ((target.mType->IsRef()) || (target.mType->IsPointer()))
	{
		auto underlying = target.mType->GetUnderlyingType();
		bool underlyingIsStruct = underlying->IsStruct();

		if (underlyingIsStruct)
		{
			target = mModule->LoadValue(target);
			target.mType = underlying;
			target.mKind = BfTypedValueKind_Addr;
		}
	}

	if ((target.mType->IsStruct()) && (!target.IsAddr()))
	{
		if (IsConstEval())
			return target;
		target = mModule->MakeAddressable(target);
	}

	if (IsVar(target.mType))
	{
		target.mType = mModule->mContext->mBfObjectType;
		return target;
	}

	if (target.mType->IsWrappableType())
	{
		auto primStructType = mModule->GetWrappedStructType(target.mType);
		if (primStructType != NULL)
		{
			mModule->PopulateType(primStructType);

			if (primStructType->IsTypedPrimitive())
			{
				// Type is already the same
				target.mType = primStructType;
			}
			else if (target.IsAddr())
			{
				auto ptrType = mModule->CreatePointerType(primStructType);
				target = BfTypedValue(mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->MapType(ptrType)), primStructType, true);
			}
			else if ((primStructType->IsSplattable()) && (target.IsSplat()) && (!IsComptime()))
			{
				target.mType = primStructType;
				target.mKind = BfTypedValueKind_SplatHead;
			}
			else
			{
				auto allocPtr = mModule->CreateAlloca(primStructType);
				auto srcPtrType = mModule->mBfIRBuilder->CreateBitCast(allocPtr, mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->MapType(target.mType)));
				mModule->mBfIRBuilder->CreateStore(target.mValue, srcPtrType);
				target = BfTypedValue(allocPtr, primStructType, true);
			}
		}

		return target;
	}

	if (target.mType->IsGenericParam())
	{
		target.mType = mModule->mContext->mBfObjectType;
		return target;
	}

	if ((!target.mType->IsTypeInstance()) && (!target.mType->IsConcreteInterfaceType()))
	{
		mModule->Fail(StrFormat("Methods cannot be called on type '%s'", mModule->TypeToString(target.mType).c_str()), targetSrc);
		return BfTypedValue();
	}

	return target;
}

int BfExprEvaluator::GetMixinVariable()
{
	auto curMethodState = mModule->mCurMethodState;
	for (int localIdx = (int)curMethodState->mLocals.size() - 1; localIdx >= 0; localIdx--)
	{
		auto varDecl = curMethodState->mLocals[localIdx];
		if (varDecl->mName == "mixin")
			return localIdx;
	}
	return -1;
}

BfModuleMethodInstance BfExprEvaluator::GetSelectedMethod(BfAstNode* targetSrc, BfTypeInstance* curTypeInst, BfMethodDef* methodDef, BfMethodMatcher& methodMatcher, BfType** overrideReturnType)
{
	bool failed = false;

	BfTypeVector resolvedGenericArguments;
	BfMethodState* rootMethodState = NULL;
	if (mModule->mCurMethodState != NULL)
		rootMethodState = mModule->mCurMethodState->GetRootMethodState();

	int localInferrableGenericArgCount = -1;

	if ((methodMatcher.mBestMethodGenericArguments.size() == 0) && (!methodMatcher.mExplicitMethodGenericArguments.IsEmpty()))
	{
		int uniqueGenericStartIdx = mModule->GetLocalInferrableGenericArgCount(methodDef);
		int64 genericArgCountDiff = (int)methodMatcher.mExplicitMethodGenericArguments.size() + uniqueGenericStartIdx - (int)methodDef->mGenericParams.size();

		BfInvocationExpression* invocationExpr = NULL;
		if (mModule->mParentNodeEntry != NULL)
		{
			invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode);
		}

		if (genericArgCountDiff > 0)
		{
			BfAstNode* errorNode = targetSrc;
			if ((invocationExpr != NULL) && (invocationExpr->mGenericArgs != NULL))
			{
				errorNode = invocationExpr->mGenericArgs->mGenericArgs[(int)methodDef->mGenericParams.size()];
				if (errorNode == NULL)
					invocationExpr->mGenericArgs->mCommas.GetSafe((int)methodDef->mGenericParams.size() - 1, errorNode);
				if (errorNode == NULL)
					errorNode = targetSrc;
			}
			mModule->Fail(StrFormat("Too many generic arguments, expected %d fewer", genericArgCountDiff), errorNode);
		}
		else if ((genericArgCountDiff < 0) && (!methodMatcher.mHadOpenGenericArguments))
		{
			BfAstNode* errorNode = targetSrc;
			if ((invocationExpr != NULL) && (invocationExpr->mGenericArgs != NULL) && (invocationExpr->mGenericArgs->mCloseChevron != NULL))
				errorNode = invocationExpr->mGenericArgs->mCloseChevron;
			mModule->Fail(StrFormat("Too few generic arguments, expected %d more", -genericArgCountDiff), errorNode);
		}

		methodMatcher.mBestMethodGenericArguments.resize(methodDef->mGenericParams.size());
		for (int i = 0; i < std::min(methodDef->mGenericParams.size() - uniqueGenericStartIdx, methodMatcher.mExplicitMethodGenericArguments.size()); i++)
		{
			methodMatcher.mBestMethodGenericArguments[i + uniqueGenericStartIdx] = methodMatcher.mExplicitMethodGenericArguments[i];
		}
	}

	BfMethodInstance* unspecializedMethod = NULL;

	bool hasVarGenerics = false;

	for (int checkGenericIdx = 0; checkGenericIdx < (int)methodMatcher.mBestMethodGenericArguments.size(); checkGenericIdx++)
	{
		BfMethodInstance* outerMethodInstance = NULL;

		auto& genericArg = methodMatcher.mBestMethodGenericArguments[checkGenericIdx];

		if (genericArg == NULL)
		{
			if ((methodDef->mIsLocalMethod) && (checkGenericIdx < mModule->mCurMethodInstance->GetNumGenericArguments()))
			{
				// If the root method is generic and we need that param then use that...
				auto rootMethodInstance = rootMethodState->mMethodInstance;
				if ((rootMethodInstance->mMethodInfoEx != NULL) && (checkGenericIdx < rootMethodInstance->mMethodInfoEx->mMethodGenericArguments.size()))
				{
					genericArg = rootMethodInstance->mMethodInfoEx->mMethodGenericArguments[checkGenericIdx];
				}
				else
				{
					if (localInferrableGenericArgCount == -1)
						localInferrableGenericArgCount = mModule->GetLocalInferrableGenericArgCount(methodDef);

					// Otherwise we can only infer generics at the level that the called method was contained
					if (checkGenericIdx < localInferrableGenericArgCount)
						genericArg = mModule->mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments[checkGenericIdx];
				}
			}
		}

		if (genericArg == NULL)
		{
			if (unspecializedMethod == NULL)
				unspecializedMethod = mModule->GetRawMethodInstance(curTypeInst, methodDef);

			auto genericParam = unspecializedMethod->mMethodInfoEx->mGenericParams[checkGenericIdx];
			if ((genericParam->mTypeConstraint != NULL) && (genericParam->mTypeConstraint->IsDelegate()))
			{
				// The only other option was to bind to a MethodRef
				genericArg = mModule->ResolveGenericType(genericParam->mTypeConstraint, NULL, &methodMatcher.mBestMethodGenericArguments, mModule->mCurTypeInstance);
			}
			else
			{
				if (((genericParam->mGenericParamFlags & BfGenericParamFlag_Const) != 0) && (genericParam->mTypeConstraint != NULL))
				{
					for (int paramIdx = 0; paramIdx < (int)unspecializedMethod->mDefaultValues.size(); paramIdx++)
					{
						auto defaultVal = unspecializedMethod->mDefaultValues[paramIdx];
						if (!defaultVal)
							continue;

						auto& param = unspecializedMethod->mParams[paramIdx];
						if (param.mResolvedType->IsGenericParam())
						{
							auto genericParamType = (BfGenericParamType*)param.mResolvedType;
							if ((genericParamType->mGenericParamKind == BfGenericParamKind_Method) && (genericParamType->mGenericParamIdx == checkGenericIdx))
							{
								BfTypedValue constExprVal;
								constExprVal.mType = genericParam->mTypeConstraint;
 								auto constant = curTypeInst->mConstHolder->GetConstant(defaultVal.mValue);
								constExprVal.mValue = mModule->ConstantToCurrent(constant, curTypeInst->mConstHolder, genericParam->mTypeConstraint);
								genericArg = mModule->CreateConstExprValueType(constExprVal);
							}
						}
					}
				}
			}

			if (genericArg == NULL)
			{
				BfGenericInferContext genericInferContext;
				genericInferContext.mModule = mModule;
				genericInferContext.mCheckMethodGenericArguments = &methodMatcher.mBestMethodGenericArguments;
				genericInferContext.InferGenericArguments(unspecializedMethod);
			}

			if (genericArg == NULL)
			{
				failed = true;
				BfError* error = mModule->Fail(StrFormat("Unable to determine generic argument '%s'", methodDef->mGenericParams[checkGenericIdx]->mName.c_str()).c_str(), targetSrc);
				if ((genericParam->mTypeConstraint != NULL) && (!genericParam->mTypeConstraint->IsUnspecializedType()))
					genericArg = genericParam->mTypeConstraint;
				else
					genericArg = mModule->mContext->mBfObjectType;
				if (error != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), unspecializedMethod->mMethodDef->GetRefNode());
			}
		}

		if (genericArg->IsVar())
		{
			//BF_ASSERT(methodMatcher.mHasVarArguments);
			hasVarGenerics = true;
		}

		if (genericArg->IsIntUnknown())
			genericArg = mModule->FixIntUnknown(genericArg);

		auto resolvedGenericArg = genericArg;
		resolvedGenericArguments.push_back(genericArg);
	}

	BfTypeInstance* foreignType = NULL;
	BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None;

	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mInlineAttributeTypeDef)))
	{
		flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForceInline);
		mModule->mAttributeState->mUsed = true;
	}

	if ((!mModule->mCurTypeInstance->IsInterface()) && (methodDef->mBody != NULL))
	{
		if ((methodMatcher.mBypassVirtual) && (methodMatcher.mBestMethodTypeInstance->IsInterface()))
		{
			// This is an explicit 'base' call to a default interface method.  We pull the methodDef into our own concrete type.
			foreignType = curTypeInst;
			curTypeInst = mModule->mCurTypeInstance;
			flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForeignMethodDef);
		}
		else if ((methodDef->mIsStatic) && (curTypeInst->IsInterface()))
		{
			if (mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, curTypeInst))
			{
				// This is an explicit call to a default static interface method.  We pull the methodDef into our own concrete type.
				foreignType = curTypeInst;
				curTypeInst = mModule->mCurTypeInstance;
				flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForeignMethodDef);
			}
		}
	}

	if (hasVarGenerics)
		return BfModuleMethodInstance();

	BfModuleMethodInstance moduleMethodInstance;
	if (methodMatcher.mBestMethodInstance)
	{
		moduleMethodInstance = methodMatcher.mBestMethodInstance;
	}
	else
	{
		moduleMethodInstance = mModule->GetMethodInstance(curTypeInst, methodDef, resolvedGenericArguments, flags, foreignType);
	}
	if (mModule->IsSkippingExtraResolveChecks())
	{
		//BF_ASSERT(methodInstance.mFunc == NULL);
	}
	if (moduleMethodInstance.mMethodInstance == NULL)
		return NULL;

	if (methodDef->IsEmptyPartial())
		return moduleMethodInstance;

	if (moduleMethodInstance.mMethodInstance->mMethodInfoEx != NULL)
	{
		for (int checkGenericIdx = 0; checkGenericIdx < (int)moduleMethodInstance.mMethodInstance->mMethodInfoEx->mGenericParams.size(); checkGenericIdx++)
		{
			auto genericParams = moduleMethodInstance.mMethodInstance->mMethodInfoEx->mGenericParams[checkGenericIdx];
			BfTypeVector* checkMethodGenericArgs = NULL;
			BfType* genericArg = NULL;

			if (checkGenericIdx < (int)methodMatcher.mBestMethodGenericArguments.size())
			{
				genericArg = methodMatcher.mBestMethodGenericArguments[checkGenericIdx];
			}
			else
			{
				checkMethodGenericArgs = &methodMatcher.mBestMethodGenericArguments;
				genericArg = genericParams->mExternType;

				auto owner = moduleMethodInstance.mMethodInstance->GetOwner();
				BfTypeVector* typeGenericArguments = NULL;
				if (owner->mGenericTypeInfo != NULL)
					typeGenericArguments = &owner->mGenericTypeInfo->mTypeGenericArguments;
				//genericArg = mModule->ResolveGenericType(genericArg, typeGenericArguments, checkMethodGenericArgs);
			}

			if (genericArg->IsVar())
				continue;

			BfAstNode* paramSrc;
			if (checkGenericIdx >= methodMatcher.mBestMethodGenericArgumentSrcs.size())
			{
				paramSrc = targetSrc;
			}
			else
				paramSrc = methodMatcher.mArguments[methodMatcher.mBestMethodGenericArgumentSrcs[checkGenericIdx]].mExpression;

			// Note: don't pass methodMatcher.mBestMethodGenericArguments into here, this method is already specialized
			BfError* error = NULL;
			if (!mModule->CheckGenericConstraints(BfGenericParamSource(moduleMethodInstance.mMethodInstance), genericArg, paramSrc, genericParams, NULL,
 				failed ? NULL : &error))
			{
				if (moduleMethodInstance.mMethodInstance->IsSpecializedGenericMethod())
				{
					// We mark this as failed to make sure we don't try to process a method that doesn't even follow the constraints
					moduleMethodInstance.mMethodInstance->mFailedConstraints = true;
				}
				if (moduleMethodInstance.mMethodInstance->mMethodDef->mMethodDeclaration != NULL)
				{
					if (error != NULL)
						mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), moduleMethodInstance.mMethodInstance->mMethodDef->GetRefNode());
				}
			}
		}
	}
	else
		BF_ASSERT(methodMatcher.mBestMethodGenericArguments.IsEmpty());

	if ((overrideReturnType != NULL) && (moduleMethodInstance.mMethodInstance->mIsUnspecializedVariation) &&
		((moduleMethodInstance.mMethodInstance->mReturnType->IsUnspecializedTypeVariation()) || (moduleMethodInstance.mMethodInstance->mReturnType->IsVar())))
	{
		if (unspecializedMethod == NULL)
			unspecializedMethod = mModule->GetRawMethodInstance(curTypeInst, methodDef);

		BfTypeVector* typeGenericArgs = NULL;
		auto typeUnspecMethodInstance = unspecializedMethod;
		if (curTypeInst->IsUnspecializedTypeVariation())
		{
			typeUnspecMethodInstance = mModule->GetUnspecializedMethodInstance(typeUnspecMethodInstance, true);
			typeGenericArgs = &curTypeInst->mGenericTypeInfo->mTypeGenericArguments;
		}

		BfType* specializedReturnType = mModule->ResolveGenericType(typeUnspecMethodInstance->mReturnType, typeGenericArgs, &methodMatcher.mBestMethodGenericArguments,
			mModule->mCurTypeInstance);
		if (specializedReturnType != NULL)
			*overrideReturnType = specializedReturnType;
	}

	return moduleMethodInstance;
}

BfModuleMethodInstance BfExprEvaluator::GetSelectedMethod(BfMethodMatcher& methodMatcher)
{
	if (!methodMatcher.mBestMethodInstance)
		methodMatcher.mBestMethodInstance = GetSelectedMethod(methodMatcher.mTargetSrc, methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher);
	return methodMatcher.mBestMethodInstance;
}

void BfExprEvaluator::CheckLocalMethods(BfAstNode* targetSrc, BfTypeInstance* typeInstance, const StringImpl& methodName, BfMethodMatcher& methodMatcher, BfMethodType methodType)
{
	auto _GetNodeId = [&]()
	{
		auto parser = targetSrc->GetSourceData()->ToParserData();
		return ((int64)parser->mDataId << 32) + targetSrc->GetSrcStart();
	};

	BfMethodState* ctxMethodState = NULL;
	BfClosureInstanceInfo* ctxClosureInstanceInfo = NULL;

 	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL))
 	{
		 ctxClosureInstanceInfo = mModule->mCurMethodState->mClosureState->mClosureInstanceInfo;
		 ctxMethodState = mModule->mCurMethodState;
	}

	bool atCtxMethodState = false;
	auto checkMethodState = mModule->mCurMethodState;
	auto rootMethodState = checkMethodState;
	while (checkMethodState != NULL)
	{
		rootMethodState = checkMethodState;

		if (checkMethodState == ctxMethodState)
			atCtxMethodState = true;

		if ((ctxClosureInstanceInfo != NULL) && (!ctxMethodState->mClosureState->mCapturing))
		{
			BfMethodDef* localMethodDef = NULL;
			if (ctxClosureInstanceInfo->mLocalMethodBindings.TryGetValue(_GetNodeId(), &localMethodDef))
			{
				methodMatcher.CheckMethod(mModule->mCurTypeInstance, mModule->mCurTypeInstance, localMethodDef, true);
				BF_ASSERT(methodMatcher.mBestMethodDef != NULL);
				return;
			}
		}
		else
		{
			BfLocalMethod* matchedLocalMethod = NULL;
			BfLocalMethod* localMethod = NULL;
			if (checkMethodState->mLocalMethodMap.TryGetValue(methodName, &localMethod))
			{
				auto typeInst = mModule->mCurTypeInstance;
				if (checkMethodState->mMixinState != NULL)
					typeInst = checkMethodState->mMixinState->mMixinMethodInstance->GetOwner();

				while (localMethod != NULL)
				{
					auto methodDef = mModule->GetLocalMethodDef(localMethod);
					if (methodDef->mMethodType == methodType)
					{
						methodMatcher.CheckMethod(mModule->mCurTypeInstance, typeInst, methodDef, true);
						if (methodMatcher.mBestMethodDef == methodDef)
							matchedLocalMethod = localMethod;
					}
					localMethod = localMethod->mNextWithSameName;
				}
			}

			if (matchedLocalMethod != NULL)
			{
				if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
				{
					BfModuleMethodInstance moduleMethodInstance = GetSelectedMethod(targetSrc, typeInstance, matchedLocalMethod->mMethodDef, methodMatcher);
					if (moduleMethodInstance)
					{
						auto methodInstance = moduleMethodInstance.mMethodInstance;

						if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState != NULL))
						{
							// The called method is calling us from its mLocalMethodRefs set.  Stretch our mCaptureStartAccessId back to incorporate its
							//  captures as well
							if (methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState->mCaptureStartAccessId < mModule->mCurMethodState->mClosureState->mCaptureStartAccessId)
								mModule->mCurMethodState->mClosureState->mCaptureStartAccessId = methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState->mCaptureStartAccessId;
						}
						else
						{
							if (methodInstance->mDisallowCalling) // We need to process the captures from this guy
							{
								if (mModule->mCurMethodState->mClosureState->mLocalMethodRefSet.Add(methodInstance))
									mModule->mCurMethodState->mClosureState->mLocalMethodRefs.Add(methodInstance);
							}
						}
					}
				}

				if (ctxClosureInstanceInfo != NULL)
				{
					BF_ASSERT(mModule->mCurMethodState->mClosureState->mCapturing);
					ctxClosureInstanceInfo->mLocalMethodBindings[_GetNodeId()] = methodMatcher.mBestMethodDef;
				}

				break;
			}
		}

		checkMethodState = checkMethodState->mPrevMethodState;
	}
}

void BfExprEvaluator::InjectMixin(BfAstNode* targetSrc, BfTypedValue target, bool allowImplicitThis, const StringImpl& name, const BfSizedArray<BfExpression*>& arguments, const BfMethodGenericArguments& methodGenericArgs)
{
	if (mModule->mCurMethodState == NULL)
		return;

	if (mDeferCallRef != NULL)
	{
		mModule->Fail("Mixins cannot be directly deferred. Consider wrapping in a block.", targetSrc);
	}

	BfAstNode* origTargetSrc = targetSrc;
	BfScopedInvocationTarget* scopedInvocationTarget = NULL;

	if (mModule->mParentNodeEntry != NULL)
	{
		if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
		{
			scopedInvocationTarget = BfNodeDynCast<BfScopedInvocationTarget>(invocationExpr->mTarget);
		}
	}
	auto targetNameNode = targetSrc;
	if (scopedInvocationTarget != NULL)
		targetNameNode = scopedInvocationTarget->mTarget;

	while (true)
	{
		if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(targetNameNode))
		{
			targetNameNode = qualifiedNameNode->mRight;
			continue;
		}
		if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(targetNameNode))
		{
			targetNameNode = memberRefExpr->mMemberName;
			continue;
		}
		break;
	}

	BfTypeInstance* mixinClass = NULL;
	if (target.mType != NULL)
		mixinClass = target.mType->ToTypeInstance();
	int inLine = mModule->mCurFilePosition.mCurLine;

	SizedArray<BfResolvedArg, 4> args;
	SizedArray<BfExprEvaluator*, 8> argExprEvaluators;

	defer
	(
		{
			for (auto exprEvaluator : argExprEvaluators)
				delete exprEvaluator;
		}
	);

	auto _AddArg = [&](BfExpression* argExpr)
	{
		BfResolvedArg resolvedArg;

		argExprEvaluators.push_back(new BfExprEvaluator(mModule));
		BfExprEvaluator* exprEvaluator = argExprEvaluators.back();
		exprEvaluator->mResolveGenericParam = false;
		exprEvaluator->mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator->mBfEvalExprFlags | BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_AllowOutExpr);

		bool deferExpr = false;
		if (auto variableDecl = BfNodeDynCast<BfVariableDeclaration>(argExpr))
		{
			deferExpr = true;
			resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_VariableDeclaration);
		}

		if (deferExpr)
		{
			//
		}
		else if (argExpr != NULL)
			exprEvaluator->Evaluate(argExpr, false, false, true);
		else
			mModule->Fail("Missing argument", targetSrc);
		auto argValue = exprEvaluator->mResult;
		mModule->FixIntUnknown(argValue);

		if (argValue)
		{
			if (argValue.mType->IsRef())
			{
				exprEvaluator->FinishExpressionResult();
			}
		}

		resolvedArg.mTypedValue = argValue;
		resolvedArg.mExpression = argExpr;
		args.push_back(resolvedArg);
	};

	for (BfExpression* argExpr : arguments)
	{
		_AddArg(argExpr);
	}

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo) && (autoComplete->mMethodMatchInfo != NULL) && (autoComplete->mMethodMatchInfo->mInstanceList.size() != 0))
		autoComplete->mIsCapturingMethodMatchInfo = false;

	BfMethodMatcher methodMatcher(targetSrc, mModule, name, args, methodGenericArgs);
	methodMatcher.mMethodType = BfMethodType_Mixin;
	methodMatcher.mSkipImplicitParams = true;

	auto curTypeInst = mModule->mCurTypeInstance;
	if (mixinClass != NULL)
		curTypeInst = mixinClass;

	if (target.mType == NULL)
	{
		CheckLocalMethods(targetSrc, curTypeInst, name, methodMatcher, BfMethodType_Mixin);
	}

	if (methodMatcher.mBestMethodDef == NULL)
		methodMatcher.mBestMethodDef = methodMatcher.mBackupMethodDef;

	if (methodMatcher.mBestMethodDef == NULL)
	{
		if (mixinClass != NULL)
			methodMatcher.CheckType(mixinClass, BfTypedValue(), false);
		else
			methodMatcher.CheckType(mModule->mCurTypeInstance, BfTypedValue(), false);
	}

	if ((methodMatcher.mBestMethodDef == NULL) && (target.mType == NULL) && (mModule->mContext->mCurTypeState != NULL))
	{
		BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mType);
		BfGlobalLookup globalLookup;
		globalLookup.mKind = BfGlobalLookup::Kind_Method;
		globalLookup.mName = name;
		mModule->PopulateGlobalContainersList(globalLookup);
		for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
		{
			if (globalContainer.mTypeInst == NULL)
				continue;
			methodMatcher.CheckType(globalContainer.mTypeInst, BfTypedValue(), false);
			if (methodMatcher.mBestMethodDef != NULL)
				break;
		}
	}

	if (methodMatcher.mBestMethodDef == NULL)
	{
		BfStaticSearch* staticSearch = mModule->GetStaticSearch();
		if (staticSearch != NULL)
		{
			for (auto typeInst : staticSearch->mStaticTypes)
			{
				if (methodMatcher.CheckType(typeInst, BfTypedValue(), false))
				{
					if (methodMatcher.mBestMethodDef != NULL)
						break;
				}
			}
		}
	}

	if (methodMatcher.mBestMethodDef == NULL)
	{
		mModule->Fail("Cannot find mixin", targetSrc);
		return;
	}

	auto resolvePassData = mModule->mCompiler->mResolvePassData;
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(targetNameNode)) && (autoComplete->mDefType == NULL))
	{
		autoComplete->mInsertStartIdx = targetNameNode->GetSrcStart();
		autoComplete->mInsertEndIdx = targetNameNode->GetSrcEnd();
		autoComplete->mDefType = methodMatcher.mBestMethodTypeInstance->mTypeDef;
		autoComplete->mDefMethod = methodMatcher.mBestMethodDef;
		autoComplete->SetDefinitionLocation(methodMatcher.mBestMethodDef->GetMethodDeclaration()->mNameNode);
	}

	if ((mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Method))
	{
		targetNameNode->SetSrcEnd(targetNameNode->GetSrcEnd() - 1);
		mModule->mCompiler->mResolvePassData->HandleMethodReference(targetNameNode, methodMatcher.mBestMethodTypeInstance->mTypeDef, methodMatcher.mBestMethodDef);
		targetNameNode->SetSrcEnd(targetNameNode->GetSrcEnd() + 1);
	}

	auto curMethodState = mModule->mCurMethodState;

	auto moduleMethodInstance = GetSelectedMethod(targetSrc, methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher);
	if (!moduleMethodInstance)
	{
		if (methodMatcher.mHasVarArguments)
		{
			mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
			return;
		}

		mModule->Fail("Failed to get selected mixin", targetSrc);
		return;
	}

	if (!mModule->CheckUseMethodInstance(moduleMethodInstance.mMethodInstance, targetSrc))
		return;

	auto methodInstance = moduleMethodInstance.mMethodInstance;
	PerformCallChecks(methodInstance, targetSrc);

	for (int checkGenericIdx = 0; checkGenericIdx < (int)methodMatcher.mBestMethodGenericArguments.size(); checkGenericIdx++)
	{
		auto& genericParams = methodInstance->mMethodInfoEx->mGenericParams;
		auto genericArg = methodMatcher.mBestMethodGenericArguments[checkGenericIdx];
		if (genericArg->IsVar())
			continue;

		BfAstNode* paramSrc;
		if (methodMatcher.mBestMethodGenericArgumentSrcs.size() == 0)
			paramSrc = targetSrc;
		else
			paramSrc = methodMatcher.mArguments[methodMatcher.mBestMethodGenericArgumentSrcs[checkGenericIdx]].mExpression;

		// Note: don't pass methodMatcher.mBestMethodGenericArguments into here, this method is already specialized
		BfError* error = NULL;
		if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), genericArg, paramSrc, genericParams[checkGenericIdx], NULL, &error))
		{
			if (methodInstance->mMethodDef->mMethodDeclaration != NULL)
			{
				if (error != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
			}
		}
	}

	if (curMethodState->mCurScope->mMixinDepth >= 128)
	{
		mModule->Fail("Maximum nested mixin depth exceeded", targetSrc);
		return;
	}

	// Check circular ref based on methodInstance. We must check the arg types since we could be making progress in mixin evaluation
	//  based on method selection within the mixin dependent on args
 	{
 		bool hasCircularRef = false;

		BfMixinState* checkMixinState = NULL;

 		auto checkMethodState = curMethodState;
 		while (checkMethodState != NULL)
 		{
			auto curMixinState = checkMethodState->mMixinState;
			while (curMixinState != NULL)
			{
				if ((curMixinState->mDoCircularVarResult) && (curMixinState->mMixinMethodInstance == methodInstance))
				{
					mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
					return;
				}

				if ((!curMixinState->mCheckedCircularRef) && (curMixinState->mMixinMethodInstance == methodInstance))
				{
					checkMixinState = curMixinState;
					checkMixinState->mCheckedCircularRef = true;
				}
				else if (checkMixinState != NULL)
				{
					if ((curMixinState->mMixinMethodInstance == checkMixinState->mMixinMethodInstance) &&
						(curMixinState->mArgTypes == checkMixinState->mArgTypes) &&
						(curMixinState->mArgConsts.mSize == checkMixinState->mArgConsts.mSize))
					{
						bool constsMatch = true;

						for (int i = 0; i < curMixinState->mArgConsts.mSize; i++)
						{
							if (!mModule->mBfIRBuilder->CheckConstEquality(curMixinState->mArgConsts[i], checkMixinState->mArgConsts[i]))
							{
								constsMatch = false;
								break;
							}
						}

						if (constsMatch)
							hasCircularRef = true;
					}
				}

				curMixinState = curMixinState->mPrevMixinState;
			}

 			checkMethodState = checkMethodState->mPrevMethodState;
 		}

 		if (hasCircularRef)
 		{
			for (auto argType : checkMixinState->mArgTypes)
			{
				if (argType->IsVar())
					checkMixinState->mDoCircularVarResult = true;
			}

			if (checkMixinState->mDoCircularVarResult)
			{
				mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
				return;
			}

 			mModule->Fail("Circular reference detected between mixins", targetSrc);
 			return;
 		}
 	}

	AddCallDependencies(methodInstance);

	if (!methodMatcher.mBestMethodDef->mIsStatic)
	{
		if ((!target) && (allowImplicitThis))
			target = mModule->GetThis();

		if (!target)
		{
			BfError* error = mModule->Fail(StrFormat("An instance reference is required to invoke the non-static mixin '%s'",
				mModule->MethodToString(methodInstance).c_str()), targetSrc);
			if ((error != NULL) && (methodInstance->mMethodDef->GetRefNode() != NULL))
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
		}
	}
	else
	{
		if (target)
		{
			BfError* error = mModule->Fail(StrFormat("Mixin '%s' cannot be accessed with an instance reference; qualify it with a type name instead",
				mModule->MethodToString(methodInstance).c_str()), targetSrc);
			if ((error != NULL) && (methodInstance->mMethodDef->GetRefNode() != NULL))
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
		}
	}

	int methodParamCount = (int)methodInstance->GetParamCount();
	// Implicit params are ignored for calling- they should be resolved at the injection site
	int implicitParamCount = methodInstance->GetImplicitParamCount();
	int explicitParamCount = methodParamCount - implicitParamCount;

 	while ((int)args.size() < explicitParamCount)
 	{
		int argIdx = (int)args.size();
		BfExpression* expr = methodInstance->GetParamInitializer(argIdx);
		if (expr == NULL)
			break;
		_AddArg(expr);
 	}

	if ((int)args.size() < explicitParamCount)
	{
		BfError* error = mModule->Fail(StrFormat("Not enough arguments specified, expected %d more.", explicitParamCount - (int)arguments.size()), targetSrc);
		if ((error != NULL) && (methodInstance->mMethodDef->GetRefNode() != NULL))
			mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
		return;
	}
	else if ((int)args.size() > explicitParamCount)
	{
		BfError* error = mModule->Fail(StrFormat("Too many arguments specified, expected %d fewer.", (int)arguments.size() - explicitParamCount), targetSrc);
		if ((error != NULL) && (methodInstance->mMethodDef->GetRefNode() != NULL))
			mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
		return;
	}

	int paramIdx = implicitParamCount;
	auto argExprEvaluatorItr = argExprEvaluators.begin();
	for (int argIdx = 0; argIdx < (int)args.size(); argIdx++)
	{
		auto exprEvaluator = *argExprEvaluatorItr;
		//auto paramType = methodInstance->GetParamKind(paramIdx);

		BfType* wantType = methodInstance->mParams[paramIdx].mResolvedType;

		auto& arg = args[argIdx];

		if ((arg.mArgFlags & BfArgFlag_VariableDeclaration) != 0)
		{
			arg.mTypedValue = ResolveArgValue(arg, wantType);
		}

		if (wantType->IsGenericParam())
		{
			//
		}
		else if (!wantType->IsVar())
		{
			if (arg.mTypedValue.mType == NULL)
			{
				mModule->AssertErrorState();
				return;
			}

			if (arg.mTypedValue.mType != wantType)
			{
				exprEvaluator->FinishExpressionResult();
				arg.mTypedValue = mModule->LoadValue(arg.mTypedValue);
				arg.mTypedValue = mModule->Cast(arg.mExpression, arg.mTypedValue, wantType);

				/*// Do this to generate default implicit cast error
				mModule->Fail(StrFormat("Mixin argument type '%s' must match parameter type '%s'.",
					mModule->TypeToString(arg.mTypedValue.mType).c_str(),
					mModule->TypeToString(wantType).c_str()), arg.mExpression);
				return;*/
			}
		}

		paramIdx++;
		argExprEvaluatorItr++;
	}

	mModule->AddDependency(methodInstance->GetOwner(), mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_InlinedCall);

	auto startBlock = mModule->mBfIRBuilder->CreateBlock("mixinStart");
	mModule->mBfIRBuilder->CreateBr(startBlock);
	mModule->mBfIRBuilder->AddBlock(startBlock);
	mModule->mBfIRBuilder->SetInsertPoint(startBlock);

	//auto prevDebugLoc = mModule->mBfIRBuilder->getCurrentDebugLocation();
	// This is so when we debug we can hit a steppoint on the inlined "call line"
	mModule->EmitEnsureInstructionAt();

	auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();

	BfMixinState* mixinState = rootMethodState->mMixinStates.Alloc();
	mixinState->mInjectFilePosition = mModule->mCurFilePosition;
	mixinState->mPrevMixinState = curMethodState->mMixinState;
	mixinState->mLocalsStartIdx = (int)mModule->mCurMethodState->mLocals.size();
	mixinState->mMixinMethodInstance = methodInstance;
	mixinState->mSource = origTargetSrc;
	mixinState->mCallerScope = mModule->mCurMethodState->mCurScope;
	mixinState->mTargetScope = mixinState->mCallerScope;
	mixinState->mResultExpr = NULL;
	mixinState->mHasDeferredUsage = false;
	mixinState->mUsedInvocationScope = false;
	mixinState->mTarget = target;

	auto checkNode = origTargetSrc;
	if (scopedInvocationTarget != NULL)
	{
		auto targetScope = mModule->FindScope(scopedInvocationTarget->mScopeName, curMethodState->mMixinState);
		if (targetScope != NULL)
		{
			mixinState->mTargetScope = targetScope;
			if (autoComplete != NULL)
			{
				if (auto identifer = BfNodeDynCast<BfIdentifierNode>(scopedInvocationTarget->mScopeName))
					autoComplete->CheckLabel(identifer, NULL, targetScope);
			}
		}
	}

	mModule->mBfIRBuilder->SaveDebugLocation();
	SetAndRestoreValue<BfMixinState*> prevMixinState(curMethodState->mMixinState, mixinState);

	BfGetSymbolReferenceKind prevSymbolRefKind = BfGetSymbolReferenceKind_None;
	if (mModule->mCompiler->mResolvePassData != NULL)
	{
		prevSymbolRefKind = mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind;
		mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_None;
	}

	defer
	(
		{
			if (mModule->mCompiler->mResolvePassData != NULL)
				mModule->mCompiler->mResolvePassData->mGetSymbolReferenceKind = prevSymbolRefKind;
		}
	);

	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();
	BfScopeData scopeData;
	scopeData.mCloseNode = methodDeclaration->mBody;
	if (auto block = BfNodeDynCast<BfBlock>(methodDeclaration->mBody))
	{
		if (block->mCloseBrace != NULL)
			scopeData.mCloseNode = block->mCloseBrace;
	}
	curMethodState->AddScope(&scopeData);
	curMethodState->mCurScope->mMixinDepth++;
	// We can't flush scope state because we extend params in as arbitrary values
	mModule->NewScopeState(true, false);

	bool wantsDIData = (mModule->mBfIRBuilder->DbgHasInfo()) && (mModule->mHasFullDebugInfo);
	DISubprogram* diFunction = NULL;

	int startLocalIdx = (int)mModule->mCurMethodState->mLocals.size();
	int endLocalIdx = startLocalIdx;

	if ((wantsDIData) || (mModule->mIsComptimeModule))
	{
		BfIRMDNode diFuncType = mModule->mBfIRBuilder->DbgCreateSubroutineType(methodInstance);

		//int defLine = mModule->mCurFilePosition.mCurLine;

		int flags = 0;
		curMethodState->mCurScope->mDIInlinedAt = mModule->mBfIRBuilder->DbgGetCurrentLocation();

		// We used to have the "def" line be the inlining position, but the linker we de-duplicate instances of these functions without regard to their unique line
		//  definitions, so we need to be consistent and use the actual line
		mModule->UpdateSrcPos(methodDeclaration->mNameNode, BfSrcPosFlag_NoSetDebugLoc);
		int defLine = mModule->mCurFilePosition.mCurLine;
		auto diParentType = mModule->mBfIRBuilder->DbgGetTypeInst(methodInstance->GetOwner());
		if (!mModule->mBfIRBuilder->mIgnoreWrites)
		{
			String methodName = methodDef->mName;
			methodName += "!";
			BfMangler::Mangle(methodName, mModule->mCompiler->GetMangleKind(), methodInstance);
			String linkageName;
			if ((mModule->mIsComptimeModule) && (mModule->mCompiler->mCeMachine->mCurBuilder != NULL))
				linkageName = StrFormat("%d", mModule->mCompiler->mCeMachine->mCurBuilder->DbgCreateMethodRef(methodInstance, ""));
			curMethodState->mCurScope->mDIScope = mModule->mBfIRBuilder->DbgCreateFunction(diParentType, methodName, linkageName, mModule->mCurFilePosition.mFileInstance->mDIFile,
				defLine + 1, diFuncType, false, true, mModule->mCurFilePosition.mCurLine + 1, flags, false, BfIRValue());
			scopeData.mAltDIFile = mModule->mCurFilePosition.mFileInstance->mDIFile;
		}
	}

	if (methodDef->mBody != NULL)
		mModule->UpdateSrcPos(methodDef->mBody);

	mModule->SetIllegalSrcPos();

	auto _AddLocalVariable = [&](BfLocalVariable* newLocalVar, BfExprEvaluator* exprEvaluator)
	{
		mModule->SetIllegalSrcPos();

		bool hasConstValue = newLocalVar->mConstValue;

		if (hasConstValue)
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(newLocalVar->mConstValue);
			hasConstValue = constant->mConstType < BfConstType_GlobalVar;
		}

		if ((exprEvaluator != NULL) && (exprEvaluator->mResultLocalVar != NULL) && (exprEvaluator->mResultLocalVarField == 0))
		{
			mModule->UpdateSrcPos(methodDeclaration->mNameNode);
			mModule->AddLocalVariableDef(newLocalVar, hasConstValue);
			auto inLocalVar = exprEvaluator->mResultLocalVar;

			newLocalVar->mAssignedKind = inLocalVar->mAssignedKind;
			newLocalVar->mUnassignedFieldFlags = inLocalVar->mUnassignedFieldFlags;
			newLocalVar->mReadFromId = inLocalVar->mReadFromId;
			newLocalVar->mIsReadOnly = inLocalVar->mIsReadOnly;

			if ((newLocalVar->mAssignedKind == BfLocalVarAssignKind_None) && (mModule->mCurMethodState->mDeferredLocalAssignData != NULL))
			{
				for (auto deferredAssign : mModule->mCurMethodState->mDeferredLocalAssignData->mAssignedLocals)
				{
					if (deferredAssign.mLocalVar == inLocalVar)
						newLocalVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
				}
			}
		}
		else
		{
			mModule->AddLocalVariableDef(newLocalVar, hasConstValue);
		}

		if ((wantsDIData) && (!mModule->mBfIRBuilder->mIgnoreWrites))
		{
			bool handled = false;

			mModule->UpdateSrcPos(methodDeclaration->mNameNode);
			if (hasConstValue)
			{
				// Already handled
				handled = true;
			}
			else if (newLocalVar->mIsSplat)
			{
				bool found = false;

				auto checkMethodState = mModule->mCurMethodState;
				while ((checkMethodState != NULL) && (!found))
				{
					for (auto localVar : checkMethodState->mLocals)
					{
						if (localVar == newLocalVar)
							continue;
						if (!localVar->mIsSplat)
							continue;
						if (newLocalVar->mValue != localVar->mAddr)
							continue;

						bool isDupName = false;
						for (auto param : methodDef->mParams)
						{
							if (param->mName == localVar->mName)
							{
								isDupName = true;
								break;
							}
						}

						if (isDupName)
						{
							auto splatAgg = mModule->AggregateSplat(BfTypedValue(newLocalVar->mValue, newLocalVar->mResolvedType, BfTypedValueKind_SplatHead));

							// Don't allow alias if one of our args has the same name
							newLocalVar->mIsSplat = false;
							newLocalVar->mValue = BfIRValue();

							if (splatAgg.IsAddr())
								newLocalVar->mAddr = splatAgg.mValue;
							else
								newLocalVar->mValue = splatAgg.mValue;

							found = true;
							break;
						}

						String name = "$";
						name += newLocalVar->mName;
						name += "$alias$";
						name += localVar->mName;

// 						auto fakeValue = mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int32, 0);
// 						auto diType = mModule->mBfIRBuilder->DbgGetType(mModule->GetPrimitiveType(BfTypeCode_Int32));
// 						auto diVariable = mModule->mBfIRBuilder->DbgCreateAutoVariable(mModule->mCurMethodState->mCurScope->mDIScope,
// 							name, mModule->mCurFilePosition.mFileInstance->mDIFile, mModule->mCurFilePosition.mCurLine, diType);
// 						mModule->mBfIRBuilder->DbgInsertValueIntrinsic(fakeValue, diVariable);

 						auto diType = mModule->mBfIRBuilder->DbgGetType(mModule->GetPrimitiveType(BfTypeCode_NullPtr));
 						auto diVariable = mModule->mBfIRBuilder->DbgCreateAutoVariable(mModule->mCurMethodState->mCurScope->mDIScope,
 							name, mModule->mCurFilePosition.mFileInstance->mDIFile, mModule->mCurFilePosition.mCurLine, diType);
 						mModule->mBfIRBuilder->DbgInsertValueIntrinsic(mModule->mBfIRBuilder->CreateConstNull(), diVariable);

						handled = true;
						found = true;
						break;
					}

					checkMethodState = checkMethodState->mPrevMethodState;
				}
			}

			if (handled)
			{
				//
			}
			else if ((mModule->IsTargetingBeefBackend()) && (!mModule->mIsComptimeModule))
			{
				mModule->UpdateSrcPos(methodDeclaration->mNameNode);
				mModule->SetIllegalSrcPos();

				// With the Beef backend we can assign two variables to the same value, but LLVM does not allow this
				//  so we have to create a ref to that variable
				auto diType = mModule->mBfIRBuilder->DbgGetType(newLocalVar->mResolvedType);

				auto diVariable = mModule->mBfIRBuilder->DbgCreateAutoVariable(mModule->mCurMethodState->mCurScope->mDIScope,
					newLocalVar->mName, mModule->mCurFilePosition.mFileInstance->mDIFile, mModule->mCurFilePosition.mCurLine, diType);

				if (newLocalVar->mIsSplat)
				{
					//TODO: Implement
				}
				else if (newLocalVar->mResolvedType->IsValuelessType())
				{
					// Do nothing
				}
				else
				{
					BfIRValue value = newLocalVar->mValue;
					if (newLocalVar->mAddr)
						value = newLocalVar->mAddr;
					else if (newLocalVar->mConstValue)
						value = newLocalVar->mConstValue;

					auto aliasValue = mModule->mBfIRBuilder->CreateAliasValue(value);
					if (mModule->WantsLifetimes())
						scopeData.mDeferredLifetimeEnds.Add(aliasValue);

					if (newLocalVar->mAddr)
						mModule->mBfIRBuilder->DbgInsertDeclare(aliasValue, diVariable);
					else
					{
						if (newLocalVar->mResolvedType->IsBoolean())
						{
							// Fix case of remote condbr referencing
							newLocalVar->mAddr = mModule->CreateAlloca(newLocalVar->mResolvedType);
							mModule->mBfIRBuilder->CreateStore(newLocalVar->mValue, newLocalVar->mAddr);
						}

						mModule->mBfIRBuilder->DbgInsertValueIntrinsic(aliasValue, diVariable);
					}
				}
			}
			else if (newLocalVar->mAddr)
			{
				mModule->UpdateSrcPos(methodDeclaration->mNameNode);
				mModule->SetIllegalSrcPos();

				auto refType = mModule->CreateRefType(newLocalVar->mResolvedType);
				auto allocaVal = mModule->CreateAlloca(refType);

				mModule->mBfIRBuilder->CreateStore(newLocalVar->mAddr, allocaVal);

				if (!mModule->mBfIRBuilder->mIgnoreWrites)
				{
					auto diType = mModule->mBfIRBuilder->DbgGetType(refType);
					auto diVariable = mModule->mBfIRBuilder->DbgCreateAutoVariable(mModule->mCurMethodState->mCurScope->mDIScope,
						newLocalVar->mName, mModule->mCurFilePosition.mFileInstance->mDIFile, mModule->mCurFilePosition.mCurLine, diType);
					mModule->mBfIRBuilder->DbgInsertDeclare(allocaVal, diVariable);
				}
			}
			else if (newLocalVar->mValue)
			{
				mModule->UpdateSrcPos(methodDeclaration->mNameNode);
				mModule->SetIllegalSrcPos();

				auto localVal = LoadLocal(newLocalVar);
				localVal = mModule->LoadValue(localVal);
				localVal = mModule->AggregateSplat(localVal);

				BfType* allocType = localVal.mType;

				auto allocaVal = mModule->CreateAlloca(allocType);

				mModule->mBfIRBuilder->CreateStore(localVal.mValue, allocaVal);

				if (!mModule->mBfIRBuilder->mIgnoreWrites)
				{
					if (newLocalVar->mIsSplat)
					{
						//TODO: Implement
					}
					else
					{
						auto diType = mModule->mBfIRBuilder->DbgGetType(allocType);
						auto diVariable = mModule->mBfIRBuilder->DbgCreateAutoVariable(mModule->mCurMethodState->mCurScope->mDIScope,
							newLocalVar->mName, mModule->mCurFilePosition.mFileInstance->mDIFile, mModule->mCurFilePosition.mCurLine, diType);
						mModule->mBfIRBuilder->DbgInsertDeclare(allocaVal, diVariable);
					}
				}
			}
		}

		newLocalVar->mParamIdx = -3;
		mixinState->mArgTypes.Add(newLocalVar->mResolvedType);
		if (mModule->mBfIRBuilder->IsConstValue(newLocalVar->mConstValue))
			mixinState->mArgConsts.Add(newLocalVar->mConstValue);
	};

	argExprEvaluatorItr = argExprEvaluators.begin();
	for (int argIdx = methodDef->mIsStatic ? 0 : -1; argIdx < (int)explicitParamCount; argIdx++)
	{
		int paramIdx = argIdx;

		auto exprEvaluator = *argExprEvaluatorItr;

		BfTypedValue argValue;

		BfLocalVariable* localVar = new BfLocalVariable();

		if (argIdx == -1)
		{
			argValue = target;
			localVar->mName = "this";
			localVar->mIsThis = true;
			localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		}
		else
		{
			auto arg = &args[argIdx];
			auto paramDef = methodDef->mParams[paramIdx];

			localVar->mName = paramDef->mName;

			if (!arg->mTypedValue)
			{
				auto wantType = methodInstance->GetParamType(paramIdx);
				if (wantType->IsVar())
					wantType = mModule->mContext->mBfObjectType;
				arg->mTypedValue = mModule->GetDefaultTypedValue(wantType);
			}
			argValue = arg->mTypedValue;
		}

		if (!argValue)
			continue;

		localVar->mResolvedType = argValue.mType;
		if (argValue.mType->IsRef())
		{
			auto refType = (BfRefType*)localVar->mResolvedType;
			localVar->mAddr = mModule->LoadValue(argValue).mValue;
			localVar->mResolvedType = argValue.mType->GetUnderlyingType();
			localVar->mAssignedKind = (refType->mRefKind != BfRefType::RefKind_Out) ? BfLocalVarAssignKind_Unconditional : BfLocalVarAssignKind_None;
		}
		else if (argValue.IsAddr())
			localVar->mAddr = argValue.mValue;
		else
		{
			if (!argValue.mValue)
			{
				// Untyped value
			}
			else if (argValue.mValue.IsConst())
			{
				localVar->mConstValue = argValue.mValue;
			}
			else if (argValue.IsSplat())
			{
				localVar->mValue = argValue.mValue;
				localVar->mIsSplat = true;
			}
			else
			{
				if (argValue.IsAddr())
					localVar->mAddr = argValue.mValue;
				else
					localVar->mValue = argValue.mValue;
			}

			localVar->mIsReadOnly = argValue.IsReadOnly();
		}
		if (argValue.IsReadOnly())
			localVar->mIsReadOnly = true;

		if (argIdx == -1)
		{
			_AddLocalVariable(localVar, NULL);
		}
		else
		{
			_AddLocalVariable(localVar, exprEvaluator);

			endLocalIdx++;
			++argExprEvaluatorItr;
		}
	}

	if (auto blockBody = BfNodeDynCast<BfBlock>(methodDef->mBody))
	{
		mModule->VisitCodeBlock(blockBody);

		if (mixinState->mResultExpr != NULL)
		{
			if (auto exprNode = BfNodeDynCast<BfExpression>(mixinState->mResultExpr))
			{
				if (!exprNode->IsA<BfBlock>())
				{
					// Mixin expression result
					SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_AllowRefExpr));
					mModule->UpdateSrcPos(exprNode);
					VisitChild(exprNode);
					FinishExpressionResult();
					ResolveGenericType();
				}
			}
		}

		GetResult();
	}
	else if (auto expr = BfNodeDynCast<BfExpression>(methodDef->mBody))
	{
		mModule->UpdateSrcPos(expr);
		mResult = mModule->CreateValueFromExpression(expr);
	}

	if (!mResult)
	{
		// If we didn't have an expression body then just make the result "void"
		mResult = BfTypedValue(BfIRValue(), mModule->GetPrimitiveType(BfTypeCode_None));
	}

	mResult = mModule->RemoveRef(mResult);

	if (mResult.IsAddr())
	{
		if (mModule->mCurMethodState->mCurScope->ExtendLifetime(mResult.mValue))
			mModule->mBfIRBuilder->CreateLifetimeSoftEnd(mResult.mValue);
	}

	int localIdx = startLocalIdx;

	argExprEvaluatorItr = argExprEvaluators.begin();
	for (; localIdx < endLocalIdx; localIdx++)
	{
		auto exprEvaluator = *argExprEvaluatorItr;
		BfLocalVariable* localVar = curMethodState->mLocals[localIdx];
		//TODO: Merge unassigned flags together
		if ((exprEvaluator != NULL) && (exprEvaluator->mResultLocalVar != NULL))
		{
			auto inLocalVar = exprEvaluator->mResultLocalVar;
			if (localVar->mAssignedKind != BfLocalVarAssignKind_None)
				inLocalVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
			if (localVar->mReadFromId != -1)
				inLocalVar->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}

		++argExprEvaluatorItr;
	}

	if (auto blockBody = BfNodeDynCast<BfBlock>(methodDef->mBody))
	{
		if (blockBody->mCloseBrace != NULL)
			mModule->UpdateSrcPos(blockBody->mCloseBrace);
	}
	else if (auto methodDeclaration = BfNodeDynCast<BfMethodDeclaration>(methodDef->mMethodDeclaration))
	{
		if (methodDeclaration->mFatArrowToken != NULL)
			mModule->UpdateSrcPos(methodDeclaration->mFatArrowToken);
	}

	mModule->RestoreScopeState();

	prevMixinState.Restore();

	if ((scopedInvocationTarget != NULL) && (scopedInvocationTarget->mScopeName != NULL) && (!mixinState->mUsedInvocationScope))
	{
		mModule->Warn(0, "Scope specifier was not referenced in mixin", scopedInvocationTarget->mScopeName);
	}

	if (mixinState->mHasDeferredUsage)
	{
		mixinState->mTarget = BfTypedValue();
		// Put deferred mixin states at the front
		BF_ASSERT(rootMethodState->mMixinStates.back() == mixinState);
		rootMethodState->mMixinStates.pop_back();
		rootMethodState->mMixinStates.Insert(0, mixinState);
	}
	else
	{
		BF_ASSERT(rootMethodState->mMixinStates.back() == mixinState);
		rootMethodState->mMixinStates.pop_back();
		delete mixinState;
	}

	mModule->mBfIRBuilder->RestoreDebugLocation();
	mModule->mBfIRBuilder->DupDebugLocation();
}

void BfExprEvaluator::SetMethodElementType(BfAstNode* target)
{
	if (auto delegateBindExpr = BfNodeDynCast<BfDelegateBindExpression>(target))
	{
		SetMethodElementType(delegateBindExpr->mTarget);
		return;
	}

	if (auto lambdaBindExpr = BfNodeDynCast<BfLambdaBindExpression>(target))
	{
		return;
	}

	if (auto attributedIdentifierNode = BfNodeDynCast<BfAttributedIdentifierNode>(target))
	{
		if (attributedIdentifierNode->mIdentifier != NULL)
			mModule->SetElementType(attributedIdentifierNode->mIdentifier, BfSourceElementType_Method);
	}
	else if (auto memberReferenceExpr = BfNodeDynCast<BfMemberReferenceExpression>(target))
	{
		if (memberReferenceExpr->mMemberName != NULL)
			SetMethodElementType(memberReferenceExpr->mMemberName);
	}
	else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(target))
		SetMethodElementType(qualifiedNameNode->mRight);
	else
		mModule->SetElementType(target, BfSourceElementType_Method);
}

void BfExprEvaluator::DoInvocation(BfAstNode* target, BfMethodBoundExpression* methodBoundExpr, const BfSizedArray<BfExpression*>& args, const BfMethodGenericArguments& methodGenericArgs, BfTypedValue* outCascadeValue)
{
	auto methodGenericArguments = methodGenericArgs.mArguments;

	// Just a check
	mModule->mBfIRBuilder->GetInsertBlock();

	bool wasCapturingMethodInfo = false;
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (methodGenericArguments != NULL))
	{
		for (BfAstNode* methodGenericArg : *methodGenericArguments)
		{
			autoComplete->CheckNode(methodGenericArg, false, true);
		}
	}

	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		// We don't want to capture a call within the target node
		wasCapturingMethodInfo = true;
		autoComplete->mIsCapturingMethodMatchInfo = false;
	}

	bool allowImplicitThis = false;
	BfAstNode* methodNodeSrc = target;

	BfAttributeState attributeState;
	attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_Invocation | BfAttributeTargets_MemberAccess);

	if (auto scopedTarget = BfNodeDynCast<BfScopedInvocationTarget>(target))
	{
		target = scopedTarget->mTarget;
		if (autoComplete != NULL)
		{
			if (auto identifier = BfNodeDynCast<BfIdentifierNode>(scopedTarget->mScopeName))
				autoComplete->CheckLabel(identifier, scopedTarget->mColonToken, NULL);
		}
		//mModule->FindScope(scopedTarget->mScopeName);
	}

	bool isCascade = false;
	BfAstNode* cascadeOperatorToken = NULL;
	bool bypassVirtual = false;
	bool gaveUnqualifiedDotError = false;
	String targetFunctionName;
	BfTypedValue thisValue;
	//TODO: This may just be a fully qualified static method name, so let's check that also
	if (auto memberRefExpression = BfNodeDynCast<BfMemberReferenceExpression>(target))
	{
		if (autoComplete != NULL)
		{
			if (memberRefExpression->mTarget != NULL)
				autoComplete->CheckMemberReference(memberRefExpression->mTarget, memberRefExpression->mDotToken, memberRefExpression->mMemberName, false, mExpectingType);
			else if (mExpectingType != NULL)
			{
 				String filter;
				if ((autoComplete != NULL) && (autoComplete->InitAutocomplete(memberRefExpression->mDotToken, memberRefExpression->mMemberName, filter)))
				{
					auto typeInst = mExpectingType->ToTypeInstance();
					if (typeInst != NULL)
					{
						String filter;
						if ((memberRefExpression->mMemberName != NULL) && (autoComplete->IsAutocompleteNode(memberRefExpression->mMemberName)))
							filter = autoComplete->GetFilter(memberRefExpression->mMemberName);

						bool allowPrivate = typeInst == mModule->mCurTypeInstance;
						autoComplete->AddEnumTypeMembers(typeInst, filter, false, allowPrivate);
						autoComplete->AddSelfResultTypeMembers(typeInst, typeInst, filter, allowPrivate);
					}
				}
			}
		}

		if ((memberRefExpression->mTarget == NULL) && (memberRefExpression->mMemberName == NULL))
		{
			auto expectingType = mExpectingType;
			if ((expectingType != NULL) && (expectingType->IsNullable()))
			{
				auto underlyingType = expectingType->GetUnderlyingType();
				expectingType = underlyingType;
			}

			if (expectingType != NULL)
			{
				if (expectingType->IsSizedArray())
				{
					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							InitializedSizedArray((BfSizedArrayType*)expectingType, invocationExpr->mOpenParen, invocationExpr->mArguments, invocationExpr->mCommas, invocationExpr->mCloseParen);
							return;
						}
					}
				}
				else if ((expectingType->IsStruct()) || (expectingType->IsTypedPrimitive()))
				{
					if ((wasCapturingMethodInfo) && (autoComplete->mMethodMatchInfo != NULL))
					{
						autoComplete->mIsCapturingMethodMatchInfo = true;
						BF_ASSERT(autoComplete->mMethodMatchInfo != NULL);
					}

					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							BfResolvedArgs argValues(invocationExpr->mOpenParen, &invocationExpr->mArguments, &invocationExpr->mCommas, invocationExpr->mCloseParen);

							BfResolveArgsFlags resolveArgsFlags = BfResolveArgsFlag_DeferParamEval;
							ResolveArgValues(argValues, resolveArgsFlags);

							if ((mReceivingValue != NULL) && (mReceivingValue->mType == expectingType) && (mReceivingValue->IsAddr()))
							{
								mResult = *mReceivingValue;
								mReceivingValue = NULL;
							}
							else
								mResult = BfTypedValue(mModule->CreateAlloca(expectingType), expectingType, BfTypedValueKind_TempAddr);

							auto ctorResult = MatchConstructor(target, methodBoundExpr, mResult, expectingType->ToTypeInstance(), argValues, false, false);
							if ((ctorResult) && (!ctorResult.mType->IsVoid()))
								mResult = ctorResult;
							mModule->ValidateAllocation(expectingType, invocationExpr->mTarget);

							return;
						}
					}
				}
				else if (expectingType->IsGenericParam())
				{
					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							BfResolvedArgs argValues(invocationExpr->mOpenParen, &invocationExpr->mArguments, &invocationExpr->mCommas, invocationExpr->mCloseParen);

							BfResolveArgsFlags resolveArgsFlags = BfResolveArgsFlag_None;
							ResolveArgValues(argValues, resolveArgsFlags);

							CheckGenericCtor((BfGenericParamType*)expectingType, argValues, invocationExpr->mTarget);
							mResult = mModule->GetDefaultTypedValue(expectingType);
							return;
						}
					}
				}
				else if (expectingType->IsVar())
				{
					// Silently allow
					gaveUnqualifiedDotError = true;
				}
				else if (expectingType->IsPrimitiveType())
				{
					// Allow
				}
				else if ((mBfEvalExprFlags & BfEvalExprFlags_AppendFieldInitializer) == 0)
				{
					gaveUnqualifiedDotError = true;
					if (mModule->PreFail())
						mModule->Fail(StrFormat("Cannot use inferred constructor on type '%s'", mModule->TypeToString(expectingType).c_str()), memberRefExpression->mDotToken);
				}
			}
		}

		if (memberRefExpression->IsA<BfBaseExpression>())
			bypassVirtual = true;

		if (memberRefExpression->mMemberName != NULL)
			methodNodeSrc = memberRefExpression->mMemberName;
		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(memberRefExpression->mMemberName))
		{
			if (attrIdentifier->mIdentifier != NULL)
				methodNodeSrc = attrIdentifier->mIdentifier;
			attributeState.mSrc = attrIdentifier->mAttributes;
			attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifier->mAttributes, attributeState.mTarget);
			if (attrIdentifier->mIdentifier != NULL)
				targetFunctionName = attrIdentifier->mIdentifier->ToString();
		}
		else if (memberRefExpression->mMemberName != NULL)
			targetFunctionName = memberRefExpression->mMemberName->ToString();

		if (memberRefExpression->mTarget == NULL)
		{
			if (mExpectingType)
			{
				if (mExpectingType->IsVar())
				{
				}

				mResult = BfTypedValue(mExpectingType);
			}
			else if (!gaveUnqualifiedDotError)
				mModule->Fail("Unqualified dot syntax can only be used when the result type can be inferred", memberRefExpression->mDotToken);
		}
		else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberRefExpression->mTarget))
		{
			// Static method
			mResult = BfTypedValue(ResolveTypeRef(typeRef));
		}
		if (auto leftIdentifier = BfNodeDynCast<BfIdentifierNode>(memberRefExpression->mTarget))
		{
			bool hadError = false;
			thisValue = LookupIdentifier(leftIdentifier, true, &hadError);

			CheckResultForReading(thisValue);
			if (mPropDef != NULL)
				thisValue = GetResult(true);

			if (hadError)
			{
				mModule->AssertErrorState();
				thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
			}

			if ((!thisValue) && (mPropDef == NULL))
			{
				// Identifier not found. Static method? Just check speculatively don't throw error
				BfType* type;
				{
					SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
					type = mModule->ResolveTypeRef(leftIdentifier, NULL, BfPopulateType_DataAndMethods, BfResolveTypeRefFlag_NoResolveGenericParam);
				}

				if (type != NULL)
					thisValue = BfTypedValue(type);
				else if (auto qualifiedLeft = BfNodeDynCast<BfQualifiedNameNode>(leftIdentifier))
				{
					LookupQualifiedStaticField(qualifiedLeft, true);
					thisValue = mResult;
					mResult = BfTypedValue();
				}
			}
			if (mPropDef != NULL)
				thisValue = GetResult(true);
			if (!thisValue.mType)
			{
				mModule->Fail("Identifier not found", leftIdentifier);
				mModule->CheckTypeRefFixit(leftIdentifier);
				thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
			}
			if (mResult)
				CheckResultForReading(mResult);
			mResult = thisValue;
		}
		else if (auto expr = BfNodeDynCast<BfExpression>(memberRefExpression->mTarget))
		{
			BfType* expectingTargetType = NULL;
			if (memberRefExpression->mDotToken->mToken == BfToken_DotDot)
				expectingTargetType = mExpectingType;

			bool handled = false;
			if (auto subMemberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(expr))
			{
				String findName;
				if (subMemberRefExpr->mMemberName != NULL)
                	findName = subMemberRefExpr->mMemberName->ToString();
				if (findName == "base") // Generic IFace<T>.base
				{
					thisValue = mModule->GetThis();
					if (thisValue)
					{
						VisitChild(subMemberRefExpr->mTarget);
						if (mResult.HasType())
						{
							if (mResult.mValue)
							{
								mModule->Fail("Type name expected", subMemberRefExpr->mTarget);
							}
							else
							{
								auto type = mResult.mType;

								if (type != NULL)
								{
									if ((thisValue.mType == type) || (!mModule->TypeIsSubTypeOf(thisValue.mType->ToTypeInstance(), type->ToTypeInstance())))
									{
										mModule->Fail(StrFormat("Type '%s' is not a base type of '%s'",
											mModule->TypeToString(type).c_str(),
											mModule->TypeToString(thisValue.mType).c_str()), subMemberRefExpr->mTarget);
									}
									else
									{
										if (type->IsInterface())
										{
											thisValue.mType = type;
										}
										else
										{
											auto castedThis = mModule->Cast(subMemberRefExpr->mMemberName, thisValue, type, BfCastFlags_Explicit);
											if (castedThis)
												thisValue = castedThis;
											//mModule->Fail("Explicit base types can only be used for specifying default interface members", qualifiedLeft->mLeft);
										}
									}
									handled = true;
								}
								bypassVirtual = true;
							}
						}
					}
				}
			}

			if (!handled)
			{
				SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);
				auto flags = (BfEvalExprFlags)(BfEvalExprFlags_PropogateNullConditional | BfEvalExprFlags_NoCast);
				if (mFunctionBindResult != NULL)
				{
					if (auto paranExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
					{
						// Allow 'ref' on binding, to indicate we want to capture 'this' by reference
						flags = (BfEvalExprFlags)(flags | BfEvalExprFlags_AllowRefExpr);
						expr = paranExpr->mExpression;
					}
				}
				if (expr != NULL)
					mResult = mModule->CreateValueFromExpression(expr, expectingTargetType, flags);
			}
		}

		isCascade = (memberRefExpression->mDotToken != NULL) && (memberRefExpression->mDotToken->GetToken() == BfToken_DotDot);
		if (isCascade)
			cascadeOperatorToken = memberRefExpression->mDotToken;
		bool isNullCondLookup = (memberRefExpression->mDotToken != NULL) && (memberRefExpression->mDotToken->GetToken() == BfToken_QuestionDot);
		if (isNullCondLookup)
			mResult = SetupNullConditional(mResult, memberRefExpression->mDotToken);

		if ((mResult.mType == NULL) && (memberRefExpression->mTarget != NULL))
		{
			mModule->AssertErrorState();
			mResult = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
		}
		thisValue = mResult;
		mResult = BfTypedValue();

		if ((thisValue) && (memberRefExpression->mDotToken != NULL) && (memberRefExpression->mDotToken->mToken == BfToken_Arrow))
			thisValue = TryArrowLookup(thisValue, memberRefExpression->mDotToken);
	}
	else if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(target))
	{
		if (GetAutoComplete() != NULL)
			GetAutoComplete()->CheckMemberReference(qualifiedName->mLeft, qualifiedName->mDot, qualifiedName->mRight);

		if (qualifiedName->mLeft->GetSrcLength() == 4)
		{
			if (CheckIsBase(qualifiedName->mLeft))
				bypassVirtual = true;
		}

		if (qualifiedName->mRight != NULL)
			methodNodeSrc = qualifiedName->mRight;

		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(qualifiedName->mRight))
		{
			if (attrIdentifier->mIdentifier != NULL)
				methodNodeSrc = attrIdentifier->mIdentifier;
			attributeState.mSrc = attrIdentifier->mAttributes;
			attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifier->mAttributes, attributeState.mTarget);
			targetFunctionName = attrIdentifier->mIdentifier->ToString();
		}
		else
			targetFunctionName = qualifiedName->mRight->ToString();

		bool hadError = false;
		thisValue = LookupIdentifier(qualifiedName->mLeft, true, &hadError);

		CheckResultForReading(thisValue);
		if (mPropDef != NULL)
			thisValue = GetResult(true);

		if (hadError)
		{
			mModule->AssertErrorState();
			thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
		}

		if ((!thisValue) && (mPropDef == NULL))
		{
			// Identifier not found. Static method? Just check speculatively don't throw error
			BfType* type;
			{
				//SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
				type = mModule->ResolveTypeRef(qualifiedName->mLeft, NULL, BfPopulateType_DataAndMethods, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowGlobalContainer | BfResolveTypeRefFlag_IgnoreLookupError));
			}

			if (type == NULL)
			{
				//SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);

				type = mModule->ResolveTypeRef(qualifiedName, methodGenericArguments, BfPopulateType_DataAndMethods, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowGlobalContainer | BfResolveTypeRefFlag_IgnoreLookupError));
				if (type != NULL)
				{
					// This is a CTOR call, treat it as such
					targetFunctionName.clear();
				}
			}

			if (type != NULL)
				thisValue = BfTypedValue(type);
			else if (auto qualifiedLeft = BfNodeDynCast<BfQualifiedNameNode>(qualifiedName->mLeft))
			{
				String findName = qualifiedLeft->mRight->ToString();
				bool handled = false;
				if (findName == "base")
				{
 					auto type = mModule->ResolveTypeRef(qualifiedLeft->mLeft, NULL, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);
					mModule->CheckTypeRefFixit(qualifiedLeft->mLeft);
					thisValue = mModule->GetThis();
					if (type != NULL)
					{
						if ((thisValue.mType == type) || (!mModule->TypeIsSubTypeOf(thisValue.mType->ToTypeInstance(), type->ToTypeInstance())))
						{
							mModule->Fail(StrFormat("Type '%s' is not a base type of '%s'",
								mModule->TypeToString(type).c_str(),
								mModule->TypeToString(thisValue.mType).c_str()), qualifiedLeft->mLeft);
						}
						else
						{
							if (type->IsInterface())
							{
								thisValue.mType = type;
							}
							else
							{
								auto castedThis = mModule->Cast(qualifiedLeft->mRight, thisValue, type, BfCastFlags_Explicit);
								if (castedThis)
									thisValue = castedThis;
								//mModule->Fail("Explicit base types can only be used for specifying default interface members", qualifiedLeft->mLeft);
							}
						}
						handled = true;
					}
					bypassVirtual = true;
				}

				if (!handled)
				{
					LookupQualifiedStaticField(qualifiedLeft, true);
					thisValue = mResult;
					mResult = BfTypedValue();
				}
			}
		}
		if (mPropDef != NULL)
			thisValue = GetResult(true);
		if (!thisValue.mType)
		{
			mModule->Fail("Identifier not found", qualifiedName->mLeft);
			mModule->CheckTypeRefFixit(qualifiedName->mLeft);
			mModule->CheckIdentifierFixit(qualifiedName->mLeft);
			thisValue = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		}
		if (mResult)
			CheckResultForReading(mResult);
		mResult = BfTypedValue();
	}
	else if (auto identiferExpr = BfNodeDynCast<BfIdentifierNode>(target))
	{
		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(target))
		{
			if (attrIdentifier->mIdentifier != NULL)
				methodNodeSrc = attrIdentifier->mIdentifier;
			attributeState.mSrc = attrIdentifier->mAttributes;
			attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifier->mAttributes, attributeState.mTarget);
		}

		allowImplicitThis = true;

		if (autoComplete != NULL)
			autoComplete->CheckIdentifier(identiferExpr);

		targetFunctionName = target->ToString();
		if (targetFunctionName == "PrintF") // Just directly call that one
		{
			BfType* charType = mModule->GetPrimitiveType(BfTypeCode_Char8);
			BfType* charPtrType = mModule->CreatePointerType(charType);

			auto func = mModule->GetBuiltInFunc(BfBuiltInFuncType_PrintF);
			SizedArray<BfIRValue, 4> irArgs;
			for (BfExpression* arg : args)
			{
				BfTypedValue value;
				if (arg != NULL)
					value = mModule->CreateValueFromExpression(arg);
				if (!value)
					return;
				auto typeInst = value.mType->ToTypeInstance();
				if ((typeInst != NULL) && (typeInst->IsInstanceOf(mModule->mCompiler->mStringTypeDef)))
					value = mModule->Cast(arg, value, charPtrType);
				if ((value.mType->IsFloat()) && (value.mType->mSize != 8)) // Always cast float to double
					value = mModule->Cast(arg, value, mModule->GetPrimitiveType(BfTypeCode_Double));
				irArgs.push_back(value.mValue);
			}

			if ((targetFunctionName != "PrintF") || (irArgs.size() > 0))
			{
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCall(func, irArgs/*, targetFunctionName*/),
					mModule->ResolveTypeDef(mModule->mSystem->mTypeInt32));
			}
			return;
		}

		if (auto ceDbgState = mModule->GetCeDbgState())
		{
			if (targetFunctionName.StartsWith("__"))
			{
				auto ceDebugger = mModule->mCompiler->mCeMachine->mDebugger;

				auto _ResolveArg = [&](int argIdx, BfType* type = NULL)
				{
					if ((argIdx < 0) || (argIdx >= args.mSize))
						return BfTypedValue();
					return mModule->CreateValueFromExpression(args[argIdx], type);
				};

				if (targetFunctionName == "__getHighBits")
				{
					auto typedVal = _ResolveArg(0);
					if ((typedVal) && (typedVal.mType->IsPrimitiveType()))
					{
						auto primType = (BfPrimitiveType*)typedVal.mType;

						int64 val = ceDebugger->ValueToInt(typedVal);
						int64 bitCount = ceDebugger->ValueToInt(_ResolveArg(1));
						int64 resultVal = val >> (typedVal.mType->mSize * 8 - bitCount);
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, (uint64)resultVal), typedVal.mType);
						return;
					}
				}
				else if (targetFunctionName == "__clearHighBits")
				{
					auto typedVal = _ResolveArg(0);
					if ((typedVal) && (typedVal.mType->IsPrimitiveType()))
					{
						auto primType = (BfPrimitiveType*)typedVal.mType;

						int64 val = ceDebugger->ValueToInt(typedVal);
						int64 bitCount = ceDebugger->ValueToInt(_ResolveArg(1));
						int64 andBits = (0x8000000000000000LL) >> ((typedVal.mType->mSize - 8) * 8 + bitCount - 1);
						int64 resultVal = val & ~andBits;
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, (uint64)resultVal), typedVal.mType);
						return;
					}
				}
				else if ((targetFunctionName == "__cast") || (targetFunctionName == "__bitcast"))
				{
					BfType* type = NULL;
					String typeName;
					for (int argIdx = 0; argIdx < args.mSize - 1; argIdx++)
					{
						auto arg = _ResolveArg(argIdx);
						if (!arg)
							continue;
						if (arg.mType->IsInstanceOf(mModule->mCompiler->mStringTypeDef))
						{
							auto strPtr = mModule->GetStringPoolString(arg.mValue, mModule->mBfIRBuilder);
							if (strPtr != NULL)
							{
								if ((type != NULL) && (*strPtr == "*"))
								{
									type = mModule->CreatePointerType(type);
								}
								else
									typeName += *strPtr;
							}
						}

						if (arg.mType->IsInteger())
						{
							int64 intVal = ceDebugger->ValueToInt(arg);
							if (typeName.IsEmpty())
							{
								auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef)->ToTypeInstance();
								auto fieldDef = typeType->mTypeDef->GetFieldByName("mTypeId");
								if (fieldDef != NULL)
								{
									int typeId = ceDebugger->ReadMemory<int>((intptr)intVal + typeType->mFieldInstances[fieldDef->mIdx].mDataOffset);
									type = mModule->mContext->FindTypeById(typeId);
								}
							}
							else
								typeName += StrFormat("%lld", intVal);
						}
					}

					auto fromTypedVal = _ResolveArg(args.mSize - 1);

					if (!typeName.IsEmpty())
						type = ceDebugger->FindType(typeName);

					if (type != NULL)
					{
						if (targetFunctionName == "__bitcast")
						{
							if ((type->IsObjectOrInterface()) || (type->IsPointer()))
							{
								auto ceAddrVal = ceDebugger->GetAddr(fromTypedVal);
								mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIntToPtr(ceAddrVal.mAddr, mModule->mBfIRBuilder->MapType(type)), type);
							}
							else
							{
								Array<uint8> memArr;
								memArr.Resize(BF_MAX(type->mSize, fromTypedVal.mType->mSize));
								mModule->mBfIRBuilder->WriteConstant(fromTypedVal.mValue, memArr.mVals, fromTypedVal.mType);
								auto newVal = mModule->mBfIRBuilder->ReadConstant(memArr.mVals, type);
								mResult = BfTypedValue(newVal, type);
							}
						}
						else
							mResult = mModule->Cast(target, fromTypedVal, type, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail));
					}

					return;
				}
				else if (targetFunctionName == "__stringView")
				{
					auto ptrVal = _ResolveArg(0);
					auto sizeVal = _ResolveArg(1);

					if (ceDbgState->mFormatInfo != NULL)
						ceDbgState->mFormatInfo->mOverrideCount = (int)ceDebugger->ValueToInt(sizeVal);

					mResult = ptrVal;
					return;
				}
				else if ((targetFunctionName == "__funcName") || (targetFunctionName == "__funcTarget"))
				{
					auto addrVal = _ResolveArg(0);
					auto ceAddrVal = ceDebugger->GetAddr(addrVal);

					CeFunction* ceFunction = NULL;
					int functionId = 0;
					if (mModule->mSystem->mPtrSize == 4)
					{
						functionId = (int)addrVal;
					}
					else
					{
						CeFunction* checkCeFunction = (CeFunction*)ceAddrVal.mAddr;
						functionId = checkCeFunction->SafeGetId();
					}

					if (mModule->mCompiler->mCeMachine->mFunctionIdMap.TryGetValue(functionId, &ceFunction))
					{
						BfMethodInstance* methodInstance = ceFunction->mMethodInstance;
						if (methodInstance != NULL)
						{
							if (targetFunctionName == "__funcTarget")
							{
								BfType* targetType = methodInstance->GetOwner();
								if (targetType->IsValueType())
									targetType = mModule->CreatePointerType(targetType);

								auto targetVal = _ResolveArg(1);
								auto ceTargetVal = ceDebugger->GetAddr(targetVal);
								mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIntToPtr(ceTargetVal.mAddr, mModule->mBfIRBuilder->MapType(targetType)), targetType);
								return;
							}
							else
							{
								mResult = BfTypedValue(mModule->GetStringObjectValue(mModule->MethodToString(methodInstance)),
									mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
								return;
							}
						}
						else if ((ceFunction->mCeInnerFunctionInfo != NULL) && (targetFunctionName == "__funcName"))
						{
							mResult = BfTypedValue(mModule->GetStringObjectValue(BfDemangler::Demangle(ceFunction->mCeInnerFunctionInfo->mName, DbgLanguage_Beef)),
								mModule->ResolveTypeDef(mModule->mCompiler->mStringTypeDef));
							return;
						}
					}

					if (targetFunctionName == "__funcTarget")
						mResult = _ResolveArg(1);
					else
						mResult = addrVal;
					return;
				}
			}
		}
	}
	else if (auto expr = BfNodeDynCast<BfExpression>(target))
	{
		auto innerInvocationResult = mModule->CreateValueFromExpression(expr);
		if (!innerInvocationResult)
		{
			mModule->AssertErrorState();
			innerInvocationResult = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
		}

		if (innerInvocationResult.mType->IsVar())
		{
			mResult = innerInvocationResult;
			return;
		}

		targetFunctionName = "Invoke";
		if (innerInvocationResult.mType->IsTypeInstance())
		{
			auto invocationTypeInst = innerInvocationResult.mType->ToTypeInstance();
			if ((invocationTypeInst->mTypeDef->mIsDelegate) || (invocationTypeInst->mTypeDef->mIsFunction))
			{
				thisValue = innerInvocationResult;
			}
		}

		if (!thisValue)
		{
			mModule->Fail(StrFormat("Cannot perform invocation on type '%s'", mModule->TypeToString(innerInvocationResult.mType).c_str()), expr);
			thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
		}
	}
	else
	{
		mModule->Fail("Invalid invocation target", target);
		thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
	}

	if ((wasCapturingMethodInfo) && (autoComplete->mMethodMatchInfo != NULL))
	{
		autoComplete->mIsCapturingMethodMatchInfo = true;
		BF_ASSERT(autoComplete->mMethodMatchInfo != NULL);
	}

	SetAndRestoreValue<BfAttributeState*> prevAttributeState;
	if (attributeState.mCustomAttributes != NULL)
		prevAttributeState.Init(mModule->mAttributeState, &attributeState);

	if ((targetFunctionName != "") && (targetFunctionName[targetFunctionName.length() - 1] == '!'))
	{
		targetFunctionName = targetFunctionName.Substring(0, targetFunctionName.length() - 1);
		InjectMixin(methodNodeSrc, thisValue, allowImplicitThis, targetFunctionName, args, methodGenericArgs);

		return;
	}

	//TODO: We removed this...  Messed up with PrimStruct 'this' non-mut errors

	// We moved this until later in MatchMethod, we want the raw target for the GetType optimization, plus we shouldn't do this until we know we won't do a SkipCall
	/*if (thisValue)
	{
		if ((!thisValue.mType->IsGenericParam()) && (!thisValue.IsSplat()) && (!thisValue.mType->IsVar()))
			thisValue = MakeCallableTarget(target, thisValue);
	}*/

	int methodCount = 0;
	bool mayBeSkipCall = false;
	bool mayBeComptimeCall = false;

	BfTypeInstance* checkTypeInst = NULL;

	if (thisValue.mType != NULL)
	{
		if (thisValue.mType->IsAllocType())
			thisValue.mType = thisValue.mType->GetUnderlyingType();

		checkTypeInst = thisValue.mType->ToTypeInstance();		
	}
	else if (allowImplicitThis)
	{
		checkTypeInst = mModule->mCurTypeInstance;
	}

	while (checkTypeInst != NULL)
	{
		checkTypeInst->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* memberSetEntry;
		if (checkTypeInst->mTypeDef->mMethodSet.TryGetWith(targetFunctionName, &memberSetEntry))
		{
			BfMethodDef* methodDef = (BfMethodDef*)memberSetEntry->mMemberDef;
			while (methodDef != NULL)
			{
				if (methodDef->mIsSkipCall)
					mayBeSkipCall = true;
				if (methodDef->mHasComptime)
					mayBeComptimeCall = true;
				methodDef = methodDef->mNextWithSameName;
			}
		}
		checkTypeInst = checkTypeInst->mBaseType;
	}

	SizedArray<BfExpression*, 8> copiedArgs;
	for (BfExpression* arg : args)
		copiedArgs.push_back(arg);
	BfSizedArray<BfExpression*> sizedCopiedArgs(copiedArgs);
	BfResolvedArgs argValues(&sizedCopiedArgs);

	if (mModule->mParentNodeEntry != NULL)
	{
		if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
		{
			argValues.mOpenToken = invocationExpr->mOpenParen;
			argValues.mCommas = &invocationExpr->mCommas;
			argValues.mCloseToken = invocationExpr->mCloseParen;
		}
	}

	BfResolveArgsFlags resolveArgsFlags = (BfResolveArgsFlags)(BfResolveArgsFlag_DeferFixits | BfResolveArgsFlag_AllowUnresolvedTypes);
	resolveArgsFlags = (BfResolveArgsFlags)(resolveArgsFlags | BfResolveArgsFlag_DeferParamEval);
	if ((mayBeSkipCall) || (mayBeComptimeCall))
		resolveArgsFlags = (BfResolveArgsFlags)(resolveArgsFlags | BfResolveArgsFlag_DeferParamValues);

	static int sCallIdx = 0;
	sCallIdx++;
	int callIdx = sCallIdx;
	if (callIdx == 1557)
	{
		NOP;
	}

	BfCheckedKind checkedKind = BfCheckedKind_NotSet;
	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
	{
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mCheckedAttributeTypeDef))
		{
			checkedKind = BfCheckedKind_Checked;
			mModule->mAttributeState->mUsed = true;
		}
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mUncheckedAttributeTypeDef))
		{
			checkedKind = BfCheckedKind_Unchecked;
			mModule->mAttributeState->mUsed = true;
		}
	}

	if ((isCascade) && (cascadeOperatorToken != NULL) && ((mBfEvalExprFlags & BfEvalExprFlags_Comptime) != 0))
		mModule->Fail("Cascade operator cannot be used in const evaluation", cascadeOperatorToken);

	SetAndRestoreValue<bool> prevUsedAsStatement(mUsedAsStatement, mUsedAsStatement || isCascade);
	SetAndRestoreValue<BfEvalExprFlags> prevEvalExprFlags(mBfEvalExprFlags);
	if (isCascade)
		mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_InCascade);
	ResolveArgValues(argValues, resolveArgsFlags);

	//
	{
		// We also apply this right before the actual call, but we need to set the comptime flag earlier
		SetAndRestoreValue<BfEvalExprFlags> prevEvalExprFlag(mBfEvalExprFlags);

		if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mConstEvalAttributeTypeDef)))
		{
			mModule->mAttributeState->mUsed = true;
			mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);
		}
		mResult = MatchMethod(methodNodeSrc, methodBoundExpr, thisValue, allowImplicitThis, bypassVirtual, targetFunctionName, argValues, methodGenericArgs, checkedKind);
	}

	argValues.HandleFixits(mModule);

	if (mModule->mAttributeState == &attributeState)
		mModule->FinishAttributeState(&attributeState);

	if (isCascade)
	{
		if ((outCascadeValue != NULL) && (thisValue.mValue))
		{
			*outCascadeValue = thisValue;
		}
		else
		{
			mModule->Fail("Invalid use of cascade operator", cascadeOperatorToken);
		}
	}
}

void BfExprEvaluator::Visit(BfInvocationExpression* invocationExpr)
{
	BfAutoParentNodeEntry autoParentNodeEntry(mModule, invocationExpr);

	// We need to check for sized array constructor like "uint8[2](1, 2)"
	if (BfNodeDynCastExact<BfIndexerExpression>(invocationExpr->mTarget) != NULL)
	{
		auto checkTarget = invocationExpr->mTarget;
		while (auto indexerExpr = BfNodeDynCastExact<BfIndexerExpression>(checkTarget))
		{
			checkTarget = indexerExpr->mTarget;
		}

		SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);
		auto resolvedType = mModule->ResolveTypeRef(checkTarget, NULL, BfPopulateType_Identity);
		prevIgnoreError.Restore();

		if (resolvedType != NULL)
		{
			BfType* curType = resolvedType;

			auto checkTarget = invocationExpr->mTarget;
			while (auto indexerExpr = BfNodeDynCastExact<BfIndexerExpression>(checkTarget))
			{
				checkTarget = indexerExpr->mTarget;

				if (indexerExpr->mCommas.size() != 0)
					mModule->Fail("Only one value expected. Consider adding an allocation specifier such as 'new' if construction of a dynamic multidimensional was intended.", indexerExpr->mCommas[0]);

				int arrSize = 0;

				BfTypeState typeState;
				typeState.mArrayInitializerSize = (int)invocationExpr->mArguments.size();
				SetAndRestoreValue<BfTypeState*> prevTypeState(mModule->mContext->mCurTypeState, &typeState);

				if (indexerExpr->mArguments.size() != 0)
				{
					BfConstResolver constResolver(mModule);
					auto arg = indexerExpr->mArguments[0];
					constResolver.mExpectingType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

					if (arg != NULL)
						constResolver.Resolve(arg, NULL, BfConstResolveFlag_ArrayInitSize);

					if (constResolver.mResult.mValue.IsConst())
					{
						auto constant = mModule->mBfIRBuilder->GetConstant(constResolver.mResult.mValue);

						if ((mModule->mBfIRBuilder->IsInt(constant->mTypeCode)) && (constant->mInt64 >= 0))
						{
							arrSize = constant->mInt32;
						}
						else
							mModule->Fail("Non-negative integer expected", indexerExpr->mArguments[0]);
					}
				}
				else
					arrSize = invocationExpr->mArguments.size();

				curType = mModule->CreateSizedArrayType(curType, arrSize);
			}

			InitializedSizedArray((BfSizedArrayType*)curType, invocationExpr->mOpenParen, invocationExpr->mArguments, invocationExpr->mCommas, invocationExpr->mCloseParen, NULL);
			return;
		}
	}

	auto autoComplete = GetAutoComplete();
	auto wasCapturingMethodInfo = (autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo);
	if (autoComplete != NULL)
		autoComplete->CheckInvocation(invocationExpr, invocationExpr->mOpenParen, invocationExpr->mCloseParen, invocationExpr->mCommas);

	mModule->UpdateExprSrcPos(invocationExpr);
	BfMethodGenericArguments methodGenericArgs;
	if (invocationExpr->mGenericArgs != NULL)
	{
		methodGenericArgs.mArguments = &invocationExpr->mGenericArgs->mGenericArgs;
		if ((!invocationExpr->mGenericArgs->mCommas.IsEmpty()) && (invocationExpr->mGenericArgs->mCommas.back()->mToken == BfToken_DotDotDot))
		{
			methodGenericArgs.mIsOpen = true;
			methodGenericArgs.mIsPartial = true;
		}
		for (int i = 0; i < (int)methodGenericArgs.mArguments->mSize; i++)
			if (BfNodeIsA<BfUninitializedExpression>((*methodGenericArgs.mArguments)[i]))
				methodGenericArgs.mIsPartial = true;
	}
	SizedArray<BfExpression*, 8> copiedArgs;
	for (BfExpression* arg : invocationExpr->mArguments)
		copiedArgs.push_back(arg);

	BfTypedValue cascadeValue;
	DoInvocation(invocationExpr->mTarget, invocationExpr, copiedArgs, methodGenericArgs, &cascadeValue);

	if (autoComplete != NULL)
	{
		if ((wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo))
		{
			if (autoComplete->mMethodMatchInfo != NULL)
				autoComplete->mIsCapturingMethodMatchInfo = true;
			else
				autoComplete->mIsCapturingMethodMatchInfo = false;
			//BF_ASSERT(autoComplete->mMethodMatchInfo != NULL);
		}
		else
			autoComplete->mIsCapturingMethodMatchInfo = false;
	}

	/// Previous check for discard

	if (cascadeValue)
		mResult = cascadeValue;
}

BfMethodDef* BfExprEvaluator::GetPropertyMethodDef(BfPropertyDef* propDef, BfMethodType methodType, BfCheckedKind checkedKind, BfTypedValue propTarget)
{
	bool allowMut = true;
	if ((propTarget) && (propTarget.mType->IsValueType()))
	{
		if (propTarget.IsReadOnly())
		{
			allowMut = false;
		}
		else if (!propTarget.IsAddr())
		{
			mModule->PopulateType(propTarget.mType);
			if (!propTarget.IsValuelessType())
				allowMut = false;
		}
	}

	int bestPri = -1000;
	BfMethodDef* matchedMethod = NULL;

	for (auto methodDef : propDef->mMethods)
	{
		if (methodDef->mMethodType != methodType)
			continue;

		int curPri = 0;

		if (methodDef->mCheckedKind == checkedKind)
		{
			curPri = 5;
		}
		else if ((checkedKind == BfCheckedKind_NotSet) && (methodDef->mCheckedKind == mModule->GetDefaultCheckedKind()))
			curPri = 3;
		else
			curPri = 1;

		if (methodDef->mIsMutating)
		{
			if (allowMut)
				curPri++;
			else
				curPri -= 10;
		}

		if (curPri > bestPri)
		{
			bestPri = curPri;
			matchedMethod = methodDef;
		}
	}

	return matchedMethod;

	/*BfMethodDef* matchedMethod = NULL;
	BfMethodDef* backupMethod = NULL;
	for (auto methodDef : propDef->mMethods)
	{
		if (methodDef->mMethodType != methodType)
			continue;

		if (methodDef->mCheckedKind == checkedKind)
		{
			matchedMethod = methodDef;
			break;
		}
		if ((checkedKind == BfCheckedKind_NotSet) && (methodDef->mCheckedKind == mModule->GetDefaultCheckedKind()))
			matchedMethod = methodDef;
		else
			backupMethod = methodDef;
	}
	if (matchedMethod == NULL)
		matchedMethod = backupMethod;
	return matchedMethod;*/
}

BfModuleMethodInstance BfExprEvaluator::GetPropertyMethodInstance(BfMethodDef* methodDef)
{
	if (mPropDefBypassVirtual)
	{
		if (mPropTarget.mType->IsInterface())
		{
			auto curTypeInst = mPropTarget.mType->ToTypeInstance();
			if (mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, curTypeInst))
			{
				if (methodDef->mBody != NULL)
				{
					// This is an explicit call to a default static interface method.  We pull the methodDef into our own concrete type.
					mPropTarget = mModule->GetThis();
					return mModule->GetMethodInstance(mModule->mCurTypeInstance, methodDef, BfTypeVector(), BfGetMethodInstanceFlag_ForeignMethodDef, curTypeInst);
				}
			}
			else
			{
				mModule->Fail("Property is not implemented by this type", mPropSrc);
				return BfModuleMethodInstance();
			}
		}
		else
		{
			auto propTypeInst = mPropTarget.mType->ToTypeInstance();
			mModule->PopulateType(propTypeInst, BfPopulateType_DataAndMethods);
			auto rawMethodInstance = mModule->GetRawMethodInstance(propTypeInst, methodDef);

			if (rawMethodInstance->mVirtualTableIdx == -1)
			{
				if (!mModule->mCompiler->mIsResolveOnly)
				{
					// ResolveOnly does not force methods to slot
					BF_ASSERT(rawMethodInstance->mVirtualTableIdx != -1);
					mModule->Fail(StrFormat("Failed to devirtualize %s", mModule->MethodToString(rawMethodInstance).c_str()));
				}
			}
			else
			{
				auto useTypeInst = mOrigPropTarget.mType->ToTypeInstance();
				auto virtualMethod = (BfMethodInstance*)useTypeInst->mVirtualMethodTable[rawMethodInstance->mVirtualTableIdx].mImplementingMethod;
				return mModule->ReferenceExternalMethodInstance(virtualMethod);
			}
		}
	}

	if ((mOrigPropTarget) && (mOrigPropTarget.mType != mPropTarget.mType) &&
		((!mOrigPropTarget.mType->IsGenericParam()) && (mPropTarget.mType->IsInterface())))
	{
		auto checkType = mOrigPropTarget.mType;

		if ((checkType->IsNullable()) && (!mPropTarget.mType->IsNullable()))
			checkType = checkType->GetUnderlyingType();
		if (checkType->IsPointer())
			checkType = ((BfPointerType*)checkType)->mElementType;
		if (checkType->IsWrappableType())
			checkType = mModule->GetWrappedStructType(checkType);
		if ((checkType != NULL) && (checkType->IsTypeInstance()))
		{
			auto activeTypeDef = mModule->GetActiveTypeDef();
			BfTypeInterfaceEntry* bestIFaceEntry = NULL;

			bool checkedUnderlying = false;
			auto checkTypeInst = checkType->ToTypeInstance();
			while (checkTypeInst != NULL)
			{
				mModule->PopulateType(checkTypeInst, BfPopulateType_DataAndMethods);
				BF_ASSERT((checkTypeInst->mDefineState >= BfTypeDefineState_DefinedAndMethodsSlotted) || (mModule->mCompiler->IsAutocomplete()));

				if (checkTypeInst->mDefineState != BfTypeDefineState_DefinedAndMethodsSlotted)
					break;

				for (auto& iface : checkTypeInst->mInterfaces)
				{
					if (!mModule->IsInSpecializedSection())
					{
						if (!checkTypeInst->IsTypeMemberAccessible(iface.mDeclaringType, activeTypeDef))
							continue;
					}

					if (iface.mInterfaceType == mPropTarget.mType)
					{
						if (bestIFaceEntry == NULL)
						{
							bestIFaceEntry = &iface;
							continue;
						}

						bool isBetter;
						bool isWorse;
						mModule->CompareDeclTypes(NULL, iface.mDeclaringType, bestIFaceEntry->mDeclaringType, isBetter, isWorse);
						if (isBetter == isWorse)
						{
							// Failed
						}
						else
						{
							if (isBetter)
								bestIFaceEntry = &iface;
						}
					}
				}

				if (bestIFaceEntry != NULL)
					break;
				checkTypeInst = checkTypeInst->mBaseType;

				if (checkTypeInst == NULL)
				{
					if (!checkedUnderlying)
					{
						checkedUnderlying = true;
						if (checkType->HasWrappedRepresentation())
						{
							auto underlyingType = checkType->GetUnderlyingType();
							if (underlyingType != NULL)
								checkTypeInst = mModule->GetWrappedStructType(underlyingType);
						}
					}
				}
			}

			if (bestIFaceEntry != NULL)
			{
				auto ifaceMethodEntry = checkTypeInst->mInterfaceMethodTable[bestIFaceEntry->mStartInterfaceTableIdx + methodDef->mIdx];
				BfMethodInstance* bestMethodInstance = ifaceMethodEntry.mMethodRef;
				if (bestMethodInstance != NULL)
				{
					mPropTarget = mOrigPropTarget;
					//mPropTarget.mType = checkTypeInst;
					//mPropTarget = mModule->Cast( mOrigPropTarget, checkTypeInst);
					return mModule->GetMethodInstanceAtIdx(ifaceMethodEntry.mMethodRef.mTypeInstance, ifaceMethodEntry.mMethodRef.mMethodNum);
				}
			}
		}

		mModule->AssertErrorState();
		return BfModuleMethodInstance();
	}

	auto propTypeInst = mPropTarget.mType->ToTypeInstance();

	if (propTypeInst == NULL)
	{
		propTypeInst = mModule->GetWrappedStructType(mPropTarget.mType);
	}

	if (propTypeInst == NULL)
	{
		mModule->Fail("INTERNAL ERROR: Invalid property target", mPropSrc);
		return BfModuleMethodInstance();
	}

	return mModule->GetMethodInstance(propTypeInst, methodDef, BfTypeVector(), mPropGetMethodFlags);
}

void BfExprEvaluator::CheckPropFail(BfMethodDef* propMethodDef, BfMethodInstance* methodInstance, bool checkProt)
{
	auto propTypeInst = mPropTarget.mType->ToTypeInstance();
	// If mExplicitInterface is null then we are implicitly calling through an interface
	if ((checkProt) && (propTypeInst != NULL) && (methodInstance->GetExplicitInterface() == NULL) &&
		(!mModule->CheckAccessMemberProtection(propMethodDef->mProtection, propTypeInst)))
		mModule->Fail(StrFormat("'%s' is inaccessible due to its protection level", mModule->MethodToString(methodInstance).c_str()), mPropSrc);
	else if (mPropCheckedKind != methodInstance->mMethodDef->mCheckedKind)
	{
		bool passes = true;
		if (mPropCheckedKind != BfCheckedKind_NotSet)
		{
			passes = false;
		}
		else
		{
			auto defaultCheckedKind = mModule->GetDefaultCheckedKind();
			if (defaultCheckedKind != methodInstance->mMethodDef->mCheckedKind)
				passes = false;
		}
		if (!passes)
		{
			auto error = mModule->Fail(StrFormat("'%s' cannot be used because its 'checked' specifier does not match the requested specifier", mModule->MethodToString(methodInstance).c_str()), mPropSrc);
			if (error != NULL)
			{
				if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
			}
		}
	}
}

bool BfExprEvaluator::HasResult()
{
	return (mResult) || (mPropDef != NULL);
}

BfTypedValue BfExprEvaluator::GetResult(bool clearResult, bool resolveGenericType)
{
	if ((!mResult) && (mPropDef != NULL))
	{
		bool handled = false;
		if (mPropTarget.mType->IsGenericTypeInstance())
		{
			auto genericTypeInst = (BfTypeInstance*)mPropTarget.mType;
			if (genericTypeInst->IsInstanceOf(mModule->mCompiler->mSizedArrayTypeDef))
			{
				if (mPropDef->mName == "Count")
				{
					auto sizedType = genericTypeInst->mGenericTypeInfo->mTypeGenericArguments[1];
					if (sizedType->IsConstExprValue())
					{
						auto constExprType = (BfConstExprValueType*)sizedType;
						mResult = BfTypedValue(mModule->GetConstValue(constExprType->mValue.mInt64), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
						handled = true;
					}
					else
					{
						BF_ASSERT(mModule->mCurMethodInstance->mIsUnspecialized);
						mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_IntPtr)), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
						handled = true;
					}
				}
			}
		}

		if (!handled)
		{
			SetAndRestoreValue<BfFunctionBindResult*> prevFunctionBindResult(mFunctionBindResult, NULL);
			SetAndRestoreValue<BfAstNode*> prevDeferCallRef(mDeferCallRef, NULL);

			BfMethodDef* matchedMethod = GetPropertyMethodDef(mPropDef, BfMethodType_PropertyGetter, mPropCheckedKind, mPropTarget);
			if (matchedMethod == NULL)
			{
				mModule->Fail("Property has no getter", mPropSrc);
				return mResult;
			}

			auto methodInstance = GetPropertyMethodInstance(matchedMethod);
			if (methodInstance.mMethodInstance == NULL)
				return mResult;
			BF_ASSERT(methodInstance.mMethodInstance->mMethodDef->mName == matchedMethod->mName);
			if (!mModule->mBfIRBuilder->mIgnoreWrites)
			{
				BF_ASSERT(!methodInstance.mFunc.IsFake());
			}

			if (mPropSrc != NULL)
				mModule->UpdateExprSrcPos(mPropSrc);

			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(mPropSrc)) && (autoComplete->mResolveType == BfResolveType_GetResultString))
			{
				autoComplete->mResultString = ":";
				autoComplete->mResultString += mModule->TypeToString(methodInstance.mMethodInstance->mReturnType);
				autoComplete->mResultString += " ";
				autoComplete->mResultString += mModule->TypeToString(methodInstance.mMethodInstance->GetOwner());
				autoComplete->mResultString += ".";
				autoComplete->mResultString += mPropDef->mName;
			}

			CheckPropFail(matchedMethod, methodInstance.mMethodInstance, (mPropGetMethodFlags & BfGetMethodInstanceFlag_Friend) == 0);
			PerformCallChecks(methodInstance.mMethodInstance, mPropSrc);

			if (methodInstance.mMethodInstance->IsSkipCall())
			{
				mResult = mModule->GetDefaultTypedValue(methodInstance.mMethodInstance->mReturnType);
			}
			else
			{
				SizedArray<BfIRValue, 4> args;
				if (!matchedMethod->mIsStatic)
				{
					auto owner = methodInstance.mMethodInstance->GetOwner();

					bool isTypeMatch = mPropTarget.mType == owner;
					if (owner->IsTypedPrimitive())
						isTypeMatch |= mPropTarget.mType == owner->GetUnderlyingType();

					if ((!isTypeMatch) ||
						((mPropTarget.mValue.IsFake()) && (!mOrigPropTarget.mValue.IsFake())))
					{
						auto prevPropTarget = mPropTarget;

						mPropTarget = mModule->Cast(mPropSrc, mOrigPropTarget, owner);

						if (!mPropTarget)
						{
							mModule->Fail("Internal property error", mPropSrc);
							return BfTypedValue();
						}
					}

					if ((mPropGetMethodFlags & BfGetMethodInstanceFlag_DisableObjectAccessChecks) == 0)
						mModule->EmitObjectAccessCheck(mPropTarget);
				}

				auto callFlags = mPropDefBypassVirtual ? BfCreateCallFlags_BypassVirtual : BfCreateCallFlags_None;
				mResult = CreateCall(mPropSrc, mPropTarget, mOrigPropTarget, matchedMethod, methodInstance, callFlags, mIndexerValues, NULL);
			}
		}

		mPropDef = NULL;
		mPropDefBypassVirtual = false;
		mIndexerValues.clear();
		mResultLocalVar = NULL;
		mResultFieldInstance = NULL;
	}
	if (resolveGenericType)
		ResolveGenericType();
	BfTypedValue result = mResult;
	if (clearResult)
		mResult = BfTypedValue();
	return result;
}

void BfExprEvaluator::CheckResultForReading(BfTypedValue& typedValue)
{
	if (mModule->mCurMethodState == NULL)
		return;
	if ((mModule->mCurMethodState->mTempKind != BfMethodState::TempKind_None) || (mModule->mCurMethodState->mAllowUinitReads))
		return;

	if ((mModule->mCurTypeInstance->mResolvingVarField) || (mModule->mCurTypeInstance->mResolvingConstField))
		return;

	if ((typedValue) && (typedValue.IsAddr()))
	{
		if ((mResultLocalVar != NULL) && (mResultLocalVarRefNode != NULL))
		{
			SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors);

			if ((mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
			{
				// These errors can only be detected during capture time, so we don't ignore them on this pass
				mModule->mIgnoreErrors = false;
			}

			int fieldIdx = mResultLocalVarField - 1;

			auto localVar = mResultLocalVar;
			if (localVar->mCompositeCount > 0)
			{
				mModule->Fail(StrFormat("Cannot read from composite '%s', it can only be used in an argument list", localVar->mName.c_str()), mResultLocalVarRefNode);
				typedValue = BfTypedValue();
				return;
			}

			if (localVar->mAssignedKind == BfLocalVarAssignKind_None)
			{
				mModule->TryLocalVariableInit(localVar);
			}

			if (localVar->mAssignedKind == BfLocalVarAssignKind_None)
			{
				auto methodStateForLocal = mModule->mCurMethodState->GetMethodStateForLocal(localVar);

				bool isAssigned = false;
				int64 undefinedFieldFlags = localVar->mUnassignedFieldFlags;
				auto deferredLocalAssignData = methodStateForLocal->mDeferredLocalAssignData;
				while ((deferredLocalAssignData != NULL) && (deferredLocalAssignData->mIsChained))
					deferredLocalAssignData = deferredLocalAssignData->mChainedAssignData;

				if (deferredLocalAssignData != NULL)
				{
					for (auto& assignedVar : deferredLocalAssignData->mAssignedLocals)
					{
						auto assignedLocal = assignedVar.mLocalVar;
						if (assignedLocal == localVar)
						{
							int assignedFieldIdx = assignedVar.mLocalVarField;
							if (assignedFieldIdx >= 0)
								undefinedFieldFlags &= ~((int64)1 << assignedFieldIdx);
							else
								undefinedFieldFlags = 0;
						}
					}
					if (deferredLocalAssignData->mLeftBlockUncond)
						isAssigned = true;
				}

				if (fieldIdx == -1)
				{
					if (undefinedFieldFlags == 0)
						isAssigned = true;
				}
				else
				{
					isAssigned = true;
					for (int i = 0; i < mResultLocalVarFieldCount; i++)
						if ((undefinedFieldFlags & (1LL << (fieldIdx + i))) != 0)
							isAssigned = false;
				}

				if (!isAssigned)
				{
					if ((localVar->mIsThis) && (fieldIdx != -1))
					{
						// When we have initializers for fields, they won't all be processed in the autocomplte case
						if (!mModule->mCompiler->IsAutocomplete())
						{
							auto bfError = mModule->Fail(StrFormat("Use of possibly unassigned field '%s'", mResultLocalVarRefNode->ToString().c_str()), mResultLocalVarRefNode, true);
						}
					}
					else
					{
						mModule->Fail(StrFormat("Use of unassigned local variable '%s'", localVar->mName.c_str()), mResultLocalVarRefNode);
					}
				}
			}
			localVar->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}
	}
}

void BfExprEvaluator::FinishExpressionResult()
{
	CheckResultForReading(mResult);
	mResultLocalVar = NULL;
	mResultFieldInstance = NULL;
}

bool BfExprEvaluator::CheckAllowValue(const BfTypedValue& typedValue, BfAstNode* refNode)
{
	return true;
}

void BfExprEvaluator::MarkResultUsed()
{
	if (mResultLocalVar != NULL)
	{
		mResultLocalVar->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
	}
}

void BfExprEvaluator::MarkResultAssigned()
{
	if (mResultLocalVar != NULL)
	{
		//int localIdx = mResultLocalVarIdx;
		//if (localIdx == 0x7FFF)
			//return;

		auto localVar = mResultLocalVar;
		int fieldIdx = mResultLocalVarField - 1;
		int count = mResultLocalVarFieldCount;
		if (fieldIdx == -1)
			count = 1;
		for (int i = 0; i < count; i++)
			mModule->mCurMethodState->GetMethodStateForLocal(localVar)->LocalDefined(localVar, fieldIdx + i);

		//if (localIdx != 0x7FFF)
		{
			if (localVar->mCompositeCount > 0)
			{
				mModule->Fail(StrFormat("Cannot write to composite '%s', it can only be used in an argument list", localVar->mName.c_str()), mResultLocalVarRefNode);
			}
		}
	}
}

void BfExprEvaluator::MakeResultAsValue()
{
	// Expressions like parens will turn a variable reference into a simple value
	mResultLocalVar = NULL;
	mResultFieldInstance = NULL;
}

bool BfExprEvaluator::CheckIsBase(BfAstNode* checkNode)
{
	if (checkNode == NULL)
		return false;

	if (!checkNode->Equals("base"))
		return false;

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(checkNode)))
	{
		if ((mModule->mCurTypeInstance != NULL) && (mModule->mCurTypeInstance->mBaseType != NULL))
			autoComplete->SetDefinitionLocation(mModule->mCurTypeInstance->mBaseType->mTypeDef->GetRefNode());
	}

	return true;
}

bool BfExprEvaluator::CheckModifyResult(BfTypedValue& typedVal, BfAstNode* refNode, const char* modifyType, bool onlyNeedsMut, bool emitWarning, bool skipCopyOnMutate)
{
	BfLocalVariable* localVar = NULL;
	bool isCapturedLocal = false;
	if (mResultLocalVar != NULL)
	{
		localVar = mResultLocalVar;
		localVar->mWrittenToId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
	}
	else if (typedVal.IsThis())
	{
		localVar = mModule->GetThisVariable();
	}
	else if (typedVal.IsSplat())
	{
		for (auto checkLocal : mModule->mCurMethodState->mLocals)
		{
			if (checkLocal->mAddr == typedVal.mValue)
			{
				localVar = checkLocal;
				break;
			}
		}
	}
	else if (typedVal.mValue.IsArg())
	{
		auto methodState = mModule->mCurMethodState->GetNonCaptureState();
		localVar = methodState->mLocals[typedVal.mValue.mId];
	}

	if ((typedVal.mKind == BfTypedValueKind_MutableValue) && (onlyNeedsMut))
	{
		return true;
	}

	bool canModify = typedVal.CanModify();
	if (((typedVal.mKind == BfTypedValueKind_TempAddr) || (typedVal.mKind == BfTypedValueKind_CopyOnMutateAddr_Derived)) &&
		(strcmp(modifyType, "assign to") == 0))
		mModule->Warn(0, "Assigning to temporary copy of a value. Consider using 'ref' in value source declaration.", refNode);

	auto _Fail = [&](const StringImpl& error, BfAstNode* refNode)
	{
		if (emitWarning)
			return mModule->Warn(BfWarning_BF4204_AddressOfReadOnly, error, refNode);
		else
			return mModule->Fail(error, refNode);
	};

	if (localVar != NULL)
	{
		if (!canModify)
		{
			BfError* error = NULL;
			if (localVar->mIsThis)
			{
				bool isClosure = false;
				BfTypeInstance* checkTypeInst = localVar->mResolvedType->ToTypeInstance();

				int fieldIdx = mResultLocalVarField - 1;
				if ((!isCapturedLocal) && (mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mClosureType != NULL))
				{
					isClosure = true;
					auto closureThis = mModule->mCurMethodState->mClosureState->mClosureType;
					closureThis = checkTypeInst->mFieldInstances[0].mResolvedType->ToTypeInstance();

					if (fieldIdx >= -1)
					{
						checkTypeInst = closureThis;
					}
					else
					{
						fieldIdx = -fieldIdx - 2;
						isCapturedLocal = true;
					}
				}

				if (fieldIdx < 0)
				{
					if (isClosure)
					{
						if (!mModule->mCurMethodState->mClosureState->mDeclaringMethodIsMutating)
							error = _Fail(StrFormat("Cannot %s 'this' within struct lambda. Consider adding 'mut' specifier to this method.", modifyType), refNode);
						else
							error = _Fail(StrFormat("Cannot %s 'this' within struct lambda. Consider adding by-reference capture specifier [&] to lambda.", modifyType), refNode);
					}
					else if (localVar->mResolvedType->IsValueType())
					{
						error = _Fail(StrFormat("Cannot %s 'this' within struct method '%s'. Consider adding 'mut' specifier to this method.", modifyType,
			                mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					}
					else
					{
						error = _Fail(StrFormat("Cannot %s 'this' because '%s' is a reference type.", modifyType,
							mModule->TypeToString(localVar->mResolvedType).c_str()), refNode);
					}
					return false;
				}
				else if (mResultFieldInstance != NULL)
				{
					if (isCapturedLocal)
					{
						error = _Fail(StrFormat("Cannot %s read-only captured local variable '%s'. Consider adding by-reference capture specifier [&] to lambda and ensuring that captured value is not read-only.", modifyType,
							mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
					}
					else if (isClosure)
					{
						if (!mModule->mCurMethodState->mClosureState->mDeclaringMethodIsMutating)
							error = _Fail(StrFormat("Cannot %s field '%s.%s' within struct lambda. Consider adding 'mut' specifier to this method.", modifyType,
								mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
						else
							error = _Fail(StrFormat("Cannot %s field '%s.%s' within struct lambda. Consider adding by-reference capture specifier [&] to lambda.", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
					}
					else if (mResultFieldInstance->GetFieldDef()->mIsReadOnly)
					{
						error = _Fail(StrFormat("Cannot %s readonly field '%s.%s' within method '%s'", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str(),
							mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					}
					else if (auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(mResultFieldInstance->GetFieldDef()->mFieldDeclaration))
					{
						String propNam;
						if (propertyDeclaration->mNameNode != NULL)
							propertyDeclaration->mNameNode->ToString(propNam);

						error = _Fail(StrFormat("Cannot %s auto-implemented property '%s.%s' without set accessor", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), propNam.c_str()), refNode);
					}
					else
					{
						error = _Fail(StrFormat("Cannot %s field '%s.%s' within struct method '%s'. Consider adding 'mut' specifier to this method.", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str(),
							mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					}
					return false;
				}
			}
			else if (localVar->IsParam())
			{
				if (!mModule->mCurMethodInstance->IsMixin())
				{
					if (mModule->mCurMethodState->mMixinState != NULL)
					{
						error = _Fail(StrFormat("Cannot %s mixin parameter '%s'", modifyType,
							localVar->mName.c_str()), refNode);
					}
					else if ((onlyNeedsMut) && (localVar->mResolvedType != NULL) && (localVar->mResolvedType->IsGenericParam()))
					{
						if (emitWarning)
							error = _Fail(StrFormat("The address of '%s' may be temporary or immutable. Consider adding 'mut' or 'ref' specifier to parameter or declaring 'var %s;' to create a mutable copy.",
								localVar->mName.c_str(), localVar->mName.c_str()), refNode);
						else
							error = _Fail(StrFormat("Cannot %s parameter '%s'. Consider adding 'mut' or 'ref' specifier to parameter or declaring 'var %s;' to create a mutable copy.", modifyType,
								localVar->mName.c_str(), localVar->mName.c_str()), refNode);
					}
					else
					{
						if (emitWarning)
							error = _Fail(StrFormat("The address of '%s' may be temporary or immutable. Consider adding 'ref' specifier to parameter or declaring 'var %s;' to create a mutable copy.",
								localVar->mName.c_str(), localVar->mName.c_str()), refNode);
						else
							error = _Fail(StrFormat("Cannot %s parameter '%s'. Consider adding 'ref' specifier to parameter or declaring 'var %s;' to create a mutable copy.", modifyType,
								localVar->mName.c_str(), localVar->mName.c_str()), refNode);
					}
					return false;
				}
			}
			else
			{
				if ((mResultLocalVarField != 0) && (!localVar->mIsReadOnly))
				{
					auto typeInst = localVar->mResolvedType->ToTypeInstance();
					int dataIdx = mResultLocalVarField - 1;
					if (typeInst != NULL)
					{
						for (auto& field : typeInst->mFieldInstances)
						{
							if (field.mDataIdx == dataIdx)
							{
								error = _Fail(StrFormat("Cannot %s readonly field '%s.%s'.", modifyType,
									mModule->TypeToString(typeInst).c_str(),
									field.GetFieldDef()->mName.c_str()), refNode);
								break;
							}
						}
					}
				}

				if (error == NULL)
				{
					error = _Fail(StrFormat("Cannot %s read-only local variable '%s'.", modifyType,
						localVar->mName.c_str()), refNode);
				}
				return false;
			}
		}
		else
		{
			// When we are capturing, we need to note that we require capturing by reference here
			localVar->mWrittenToId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}
	}

	if ((mResultFieldInstance != NULL) && (mResultFieldInstance->GetFieldDef()->mIsReadOnly) && (!canModify))
	{
		auto error = _Fail(StrFormat("Cannot %s static readonly field '%s.%s' within method '%s'", modifyType,
			mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str(),
			mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);

		return false;
	}

	if ((!skipCopyOnMutate) && (typedVal.IsCopyOnMutate()))
		typedVal = mModule->CopyValue(typedVal);

	return mModule->CheckModifyValue(typedVal, refNode, modifyType);
}

void BfExprEvaluator::Visit(BfConditionalExpression* condExpr)
{
	static int sCallCount = 0;
	sCallCount++;

	auto condResult = mModule->CreateValueFromExpression(condExpr->mConditionExpression, mModule->GetPrimitiveType(BfTypeCode_Boolean), (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
	if (!condResult)
		return;

	if (condExpr->mTrueExpression == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	if (condExpr->mFalseExpression == NULL)
	{
		mModule->CreateValueFromExpression(condExpr->mTrueExpression, mExpectingType, BfEvalExprFlags_NoCast);
		mModule->AssertErrorState();
		return;
	}

	bool isConstBranch = false;
	bool constResult = false;
	bool constResultUndef = false;
	if (condResult.mValue.IsConst())
	{
		auto constValue = mModule->mBfIRBuilder->GetConstant(condResult.mValue);
		if (constValue->mTypeCode == BfTypeCode_Boolean)
		{
			isConstBranch = true;
			constResult = constValue->mBool;
		}
		else if (constValue->mConstType == BfConstType_Undef)
		{
			isConstBranch = true;
			constResultUndef = true;
		}
	}

	if (isConstBranch)
	{
		BfExpression* actualExpr = (constResult) ? condExpr->mTrueExpression : condExpr->mFalseExpression;
		BfExpression* ignoredExpr = (constResult) ? condExpr->mFalseExpression : condExpr->mTrueExpression;

		BfTypedValue actualValue = mModule->CreateValueFromExpression(actualExpr, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast));
		BfTypedValue ignoredValue;
		//
		{
			auto curBlock = mModule->mBfIRBuilder->GetInsertBlock();
			SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
			SetAndRestoreValue<bool> prevInConstIgnore(mModule->mCurMethodState->mCurScope->mInConstIgnore, true);
			ignoredValue = mModule->CreateValueFromExpression(ignoredExpr, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast));
			mModule->mBfIRBuilder->SetInsertPoint(curBlock);
		}

		if (!actualValue)
			return;
		if ((ignoredValue) && (ignoredValue.mType != actualValue.mType) && (!ignoredValue.mType->IsVar()))
		{
			// Cast to more specific 'ignored' type if applicable
			if (mModule->CanCast(actualValue, ignoredValue.mType))
			{
				actualValue = mModule->Cast(actualExpr, actualValue, ignoredValue.mType);
			}
			else if (!mModule->CanCast(ignoredValue, actualValue.mType))
			{
				mModule->Fail(StrFormat("Type of conditional expression cannot be determined because there is no implicit conversion between '%s' and '%s'",
					mModule->TypeToString(actualValue.mType).c_str(), mModule->TypeToString(ignoredValue.mType).c_str()), condExpr);
			}
		}
		mResult = actualValue;

		if (constResultUndef)
			mResult = mModule->GetDefaultTypedValue(mResult.mType, false, BfDefaultValueKind_Undef);

		return;
	}

	auto trueBB = mModule->mBfIRBuilder->CreateBlock("cond.then");
	auto falseBB = mModule->mBfIRBuilder->CreateBlock("cond.else");
	auto endBB = mModule->mBfIRBuilder->CreateBlock("cond.end");
	auto contBB = mModule->mBfIRBuilder->CreateBlock("cond.cont");

	mModule->mBfIRBuilder->CreateCondBr(condResult.mValue, trueBB, falseBB);

	SetAndRestoreValue<bool> prevInCondBlock(mModule->mCurMethodState->mCurScope->mInnerIsConditional, true);

	bool wantExpectingCast = (mExpectingType != NULL) && ((mBfEvalExprFlags & BfEvalExprFlags_NoCast) == 0);

	mModule->AddBasicBlock(trueBB);
	auto trueValue = mModule->CreateValueFromExpression(condExpr->mTrueExpression, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_CreateConditionalScope));
	if ((wantExpectingCast) && (trueValue) && (trueValue.mType != mExpectingType))
	{
		// In some cases like typed primitives - we CAN individually cast each value which it's a constant still, but not after the merging
		// IE: Color c = isOver ? 0xFF000000 : 0xFFFFFFFF;
		//  Otherwise the resulting value would just be 'int' which cannot implicitly convert to Color, but each of those ints can be
		//  a uint32 if converted separately
		auto checkTrueValue = mModule->Cast(condExpr->mTrueExpression, trueValue, mExpectingType, BfCastFlags_SilentFail);
		if (checkTrueValue)
			trueValue = checkTrueValue;
		mModule->FixIntUnknown(trueValue);
	}
	auto trueBlockPos = mModule->mBfIRBuilder->GetInsertBlock();

	mModule->AddBasicBlock(falseBB);
	auto falseValue = mModule->CreateValueFromExpression(condExpr->mFalseExpression, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_CreateConditionalScope));
	auto falseBlockPos = mModule->mBfIRBuilder->GetInsertBlock();
 	if ((wantExpectingCast) && (falseValue) && (falseValue.mType != mExpectingType))
 	{
 		auto checkFalseValue = mModule->Cast(condExpr->mFalseExpression, falseValue, mExpectingType, BfCastFlags_SilentFail);
 		if (checkFalseValue)
 			falseValue = checkFalseValue;
		mModule->FixIntUnknown(falseValue);
 	}

	bool isValid = trueValue && falseValue;

	if (isValid)
	{
		BfTypedValue falseToTrue;
		{
			SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);
			mModule->mBfIRBuilder->SetInsertPoint(falseBlockPos);
			falseToTrue = mModule->Cast(condExpr->mFalseExpression, falseValue, trueValue.mType);
		}

		if (falseToTrue)
		{
			falseValue = falseToTrue;
		}
		else
		{
			BfTypedValue trueToFalse;
			{
				SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);
				mModule->mBfIRBuilder->SetInsertPoint(trueBlockPos);
				trueToFalse = mModule->Cast(condExpr->mTrueExpression, trueValue, falseValue.mType);
			}

			if (!trueToFalse)
			{
				mModule->Fail(StrFormat("Type of conditional expression cannot be determined because there is no implicit conversion between '%s' and '%s'",
					mModule->TypeToString(trueValue.mType).c_str(), mModule->TypeToString(falseValue.mType).c_str()), condExpr);
				//return;
				isValid = false;
			}
			else
				trueValue = trueToFalse;
		}
	}

	prevInCondBlock.Restore();

	mModule->mBfIRBuilder->SetInsertPoint(trueBlockPos);
	if (isValid)
		trueValue = mModule->LoadValue(trueValue);
	mModule->mBfIRBuilder->CreateBr(endBB);

	mModule->mBfIRBuilder->SetInsertPoint(falseBlockPos);
	if (isValid)
		falseValue = mModule->LoadValue(falseValue);
	mModule->mBfIRBuilder->CreateBr(endBB);

	mModule->AddBasicBlock(endBB, false);
	if (!isValid)
		return;

	mModule->mBfIRBuilder->SetInsertPoint(endBB);
	BfIRValue phi;
	if (!trueValue.mType->IsValuelessType())
	{
		if (trueValue.mType->IsVar())
		{
			phi = mModule->mBfIRBuilder->GetFakeVal();
		}
		else
		{
			mModule->mBfIRBuilder->PopulateType(trueValue.mType);
			phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(trueValue.mType), 2);
			mModule->mBfIRBuilder->AddPhiIncoming(phi, trueValue.mValue, trueBlockPos);
			mModule->mBfIRBuilder->AddPhiIncoming(phi, falseValue.mValue, falseBlockPos);
		}
	}
	mModule->mBfIRBuilder->CreateBr(contBB);
	mModule->AddBasicBlock(contBB);

	mResult = BfTypedValue(phi, trueValue.mType);
}

void BfExprEvaluator::PopulateDeferrredTupleAssignData(BfTupleExpression* tupleExpr, DeferredTupleAssignData& deferredTupleAssignData)
{
	BfTypeVector fieldTypes;
	Array<String> fieldNames;

	// We need to evaluate each LHS tuple component in a separate BfExprEvaluator because each one
	//  could be a property and the 'mPropDef' target info is tied to a single evaluator
	for (int valueIdx = 0; valueIdx < (int)tupleExpr->mValues.size(); valueIdx++)
	{
		DeferredTupleAssignData::Entry entry;
		entry.mExprEvaluator = NULL;
		entry.mInnerTuple = NULL;
		entry.mVarType = NULL;
		entry.mVarNameNode = NULL;

		BfExpression* valueExpr = tupleExpr->mValues[valueIdx];
		entry.mExpr = valueExpr;
		BfType* fieldType = NULL;
		BfType* resultType = NULL;
		if (auto innerTupleExpr = BfNodeDynCast<BfTupleExpression>(valueExpr))
		{
			entry.mInnerTuple = new DeferredTupleAssignData();
			PopulateDeferrredTupleAssignData(innerTupleExpr, *entry.mInnerTuple);
			resultType = entry.mInnerTuple->mTupleType;
		}
		else
		{
			BfExprEvaluator* exprEvaluator = new BfExprEvaluator(mModule);
			entry.mExprEvaluator = exprEvaluator;

			if (valueExpr->IsA<BfUninitializedExpression>())
			{
				resultType = mModule->GetPrimitiveType(BfTypeCode_None);
			}

			if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(valueExpr))
			{
				if ((varDecl->mTypeRef->IsA<BfLetTypeReference>()) || (varDecl->mTypeRef->IsA<BfVarTypeReference>()))
				{
					resultType = mModule->GetPrimitiveType(BfTypeCode_Var);
				}
				else
				{
					resultType = ResolveTypeRef(varDecl->mTypeRef);
					if (resultType == NULL)
						resultType = mModule->GetPrimitiveType(BfTypeCode_Var);
				}
				entry.mVarType = resultType;
			}

			if (auto binOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>(valueExpr))
			{
				if (binOpExpr->mOp == BfBinaryOp_Multiply)
				{
					SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);
					auto resolvedType = mModule->ResolveTypeRef(binOpExpr->mLeft, NULL);
					prevIgnoreError.Restore();
					if (resolvedType != NULL)
					{
						resultType = mModule->CreatePointerType(resolvedType);
						entry.mVarType = resultType;
						entry.mVarNameNode = binOpExpr->mRight;
					}
				}
			}

			if (resultType == NULL)
			{
				exprEvaluator->VisitChild(valueExpr);
				if ((!exprEvaluator->mResult) && (exprEvaluator->mPropDef == NULL))
					exprEvaluator->mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_None));

				resultType = exprEvaluator->mResult.mType;
			}

			if ((resultType == NULL) && (exprEvaluator->mPropDef != NULL) && (exprEvaluator->mPropTarget.mType != NULL))
			{
				auto propTypeInst = exprEvaluator->mPropTarget.mType->ToTypeInstance();
				if ((propTypeInst == NULL) && (exprEvaluator->mPropTarget.mType->IsPointer()))
				{
					BfPointerType* pointerType = (BfPointerType*)exprEvaluator->mPropTarget.mType;
					propTypeInst = pointerType->mElementType->ToTypeInstance();
				}

				auto setMethod = GetPropertyMethodDef(exprEvaluator->mPropDef, BfMethodType_PropertySetter, mPropCheckedKind, mPropTarget);
				if (setMethod != NULL)
				{
					auto methodInstance = mModule->GetMethodInstance(propTypeInst, setMethod, BfTypeVector());
					resultType = methodInstance.mMethodInstance->GetParamType(0);
				}
				else
				{
					auto getMethod = GetPropertyMethodDef(exprEvaluator->mPropDef, BfMethodType_PropertyGetter, mPropCheckedKind, mPropTarget);
					if (getMethod != NULL)
					{
						auto methodInstance = mModule->GetMethodInstance(propTypeInst, getMethod, BfTypeVector());
						auto retType = methodInstance.mMethodInstance->mReturnType;
						if (retType->IsRef())
							resultType = retType->GetUnderlyingType();
					}
				}
			}
		}
		if (resultType == NULL)
			resultType = mModule->GetPrimitiveType(BfTypeCode_None);

		deferredTupleAssignData.mChildren.push_back(entry);
		fieldTypes.push_back(resultType);
	}
	for (BfTupleNameNode* requestedName : tupleExpr->mNames)
	{
		if (requestedName == NULL)
			fieldNames.push_back("");
		else
			fieldNames.push_back(requestedName->mNameNode->ToString());
	}

	BfTypeInstance* tupleType = mModule->CreateTupleType(fieldTypes, fieldNames, true);
	deferredTupleAssignData.mTupleType = tupleType;
}

void BfExprEvaluator::AssignDeferrredTupleAssignData(BfAssignmentExpression* assignExpr, DeferredTupleAssignData& deferredTupleAssignData, BfTypedValue rightValue)
{
	BF_ASSERT(rightValue.mType->IsTuple());
	auto tupleType = (BfTypeInstance*)rightValue.mType;
	for (int valueIdx = 0; valueIdx < (int)deferredTupleAssignData.mChildren.size(); valueIdx++)
	{
		auto& child = deferredTupleAssignData.mChildren[valueIdx];
		BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[valueIdx];
		BfTypedValue elementValue;
		if (fieldInstance->mDataIdx >= 0)
		{
			rightValue = mModule->LoadOrAggregateValue(rightValue);
			mModule->mBfIRBuilder->PopulateType(rightValue.mType);
			auto extractedValue = mModule->mBfIRBuilder->CreateExtractValue(rightValue.mValue, fieldInstance->mDataIdx);
			elementValue = BfTypedValue(extractedValue, fieldInstance->GetResolvedType());
			if (child.mInnerTuple != NULL)
			{
				AssignDeferrredTupleAssignData(assignExpr, *child.mInnerTuple, elementValue);
				delete child.mInnerTuple;
				child.mInnerTuple = NULL;
			}
			else
			{
				if (child.mExprEvaluator->HasResult())
 				{
 					child.mExprEvaluator->mBfEvalExprFlags = (BfEvalExprFlags)(child.mExprEvaluator->mBfEvalExprFlags | BfEvalExprFlags_NoAutoComplete);
 					child.mExprEvaluator->PerformAssignment(assignExpr, true, elementValue);
 				}
			}
		}

		if (child.mVarType != NULL)
		{
			if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(child.mExpr))
			{
				if (!elementValue)
					elementValue = mModule->GetDefaultTypedValue(fieldInstance->GetResolvedType());
				mModule->HandleVariableDeclaration(varDecl, elementValue);
			}
			else
			{
				// This handles the 'a*b' disambiguated variable decl case
				mModule->HandleVariableDeclaration(child.mVarType, child.mVarNameNode, elementValue);
			}
		}
	}
}

void BfExprEvaluator::DoTupleAssignment(BfAssignmentExpression* assignExpr)
{
	auto tupleExpr = BfNodeDynCast<BfTupleExpression>(assignExpr->mLeft);

	DeferredTupleAssignData deferredTupleAssignData;
	PopulateDeferrredTupleAssignData(tupleExpr, deferredTupleAssignData);
	BfTypeInstance* tupleType = deferredTupleAssignData.mTupleType;

	BfTypedValue rightValue;
	if (assignExpr->mRight != NULL)
	{
		rightValue = mModule->CreateValueFromExpression(assignExpr->mRight, tupleType);
	}
	if (!rightValue)
	{
		tupleType = mModule->SantizeTupleType(tupleType);
		rightValue = mModule->GetDefaultTypedValue(tupleType);
	}
	rightValue = mModule->LoadValue(rightValue);

	AssignDeferrredTupleAssignData(assignExpr, deferredTupleAssignData, rightValue);

	mResult = rightValue;
}

BfTypedValue BfExprEvaluator::PerformAssignment_CheckOp(BfAssignmentExpression* assignExpr, bool deferBinop, BfTypedValue& leftValue, BfTypedValue& rightValue, bool& evaluatedRight)
{
	BfResolvedArgs argValues;
	auto checkTypeInst = leftValue.mType->ToTypeInstance();
	while (checkTypeInst != NULL)
	{
		for (auto operatorDef : checkTypeInst->mTypeDef->mOperators)
		{
			if (operatorDef->mOperatorDeclaration->mAssignOp != assignExpr->mOp)
				continue;

			auto methodInst = mModule->GetRawMethodInstanceAtIdx(checkTypeInst, operatorDef->mIdx);
			if (methodInst->GetParamCount() != 1)
				continue;

			auto paramType = methodInst->GetParamType(0);
			if (deferBinop)
			{
				if (argValues.mArguments == NULL)
				{
					SizedArray<BfExpression*, 2> argExprs;
					argExprs.push_back(assignExpr->mRight);
					BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
					argValues.Init(&sizedArgExprs);
					ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);
				}

				evaluatedRight = true;
				rightValue = ResolveArgValue(argValues.mResolvedArgs[0], paramType);
				if (!rightValue)
					continue;
			}
			else
			{
				if (!mModule->CanCast(rightValue, paramType))
					continue;

				rightValue = mModule->Cast(assignExpr->mLeft, rightValue, paramType);
				BF_ASSERT(rightValue);
			}

			mModule->SetElementType(assignExpr->mOpToken, BfSourceElementType_Method);
			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(assignExpr->mOpToken)))
			{
				if (operatorDef->mOperatorDeclaration != NULL)
					autoComplete->SetDefinitionLocation(operatorDef->mOperatorDeclaration->mOpTypeToken);
			}

			auto moduleMethodInstance = mModule->GetMethodInstance(checkTypeInst, operatorDef, BfTypeVector());

			BfExprEvaluator exprEvaluator(mModule);
			SizedArray<BfIRValue, 1> args;
			exprEvaluator.PushThis(assignExpr->mLeft, leftValue, moduleMethodInstance.mMethodInstance, args);
			exprEvaluator.PushArg(rightValue, args);
			exprEvaluator.CreateCall(assignExpr, moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, args);
			return leftValue;
		}

		checkTypeInst = mModule->GetBaseType(checkTypeInst);
	}

	return BfTypedValue();
}

void BfExprEvaluator::PerformAssignment(BfAssignmentExpression* assignExpr, bool evaluatedLeft, BfTypedValue rightValue, BfTypedValue* outCascadeValue)
{
	auto binaryOp = BfAssignOpToBinaryOp(assignExpr->mOp);

	BfExpression* targetNode = assignExpr->mLeft;

	if ((BfNodeIsA<BfMixinExpression>(targetNode)) && (!mModule->mCurMethodInstance->mIsUnspecialized))
	{
		// If we have a "mixin = <X>" but there's no mixin target then ignore the assignment
		int mixinVar = GetMixinVariable();
		if (mixinVar == -1)
		{
			mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_None));
			return;
		}
	}

	BfAutoComplete* autoComplete = GetAutoComplete();
	bool deferredFixits = false;

	//TODO: Why was this needed? This breaks fixits on target nodes (ie: 'using' field fixit for 'fully quality')
	/*if ((autoComplete != NULL) && (autoComplete->mResolveType == BfResolveType_GetFixits))
	{
		SetAndRestoreValue<bool> ignoreFixits(autoComplete->mIgnoreFixits, true);
		VisitChild(targetNode);
		deferredFixits = true;
	}
	else*/
	if (!evaluatedLeft)
	{
		if (auto memberReferenceExpr = BfNodeDynCast<BfMemberReferenceExpression>(targetNode))
		{
			DoMemberReference(memberReferenceExpr, outCascadeValue);
		}
		else
			VisitChild(targetNode);
	}

	if ((!mResult) && (mPropDef == NULL))
	{
		if (assignExpr->mRight != NULL)
		{
			auto result = mModule->CreateValueFromExpression(assignExpr->mRight);
			if (deferredFixits)
			{
				SetAndRestoreValue<bool> ignoreErrors(mModule->mIgnoreErrors, true);
				mExpectingType = result.mType;
				VisitChild(targetNode);
				mResult = BfTypedValue();
			}
		}
		return;
	}

	ResolveGenericType();
	auto ptr = mModule->RemoveRef(mResult);
	mResult = BfTypedValue();

	if (mPropDef != NULL)
	{
		bool hasLeftVal = false;

		auto propDef = mPropDef;
		auto propTarget = mPropTarget;

		auto setMethod = GetPropertyMethodDef(mPropDef, BfMethodType_PropertySetter, mPropCheckedKind, mPropTarget);
		if (setMethod == NULL)
		{
			// Allow for a ref return on the getter to be used if a setter is not available
			GetResult();

			if ((mResult) && (mResult.mKind == BfTypedValueKind_Addr))
			{
				ptr = mResult;
				mResult = BfTypedValue();
				hasLeftVal = true;
			}
			else
			{
				mModule->Fail("Property has no setter", mPropSrc);
				if (assignExpr->mRight != NULL)
					mModule->CreateValueFromExpression(assignExpr->mRight, ptr.mType, BfEvalExprFlags_NoCast);
				return;
			}
		}

		if (!hasLeftVal)
		{
			auto methodInstance = GetPropertyMethodInstance(setMethod);
			if (methodInstance.mMethodInstance == NULL)
				return;
			//BF_ASSERT(methodInstance.mMethodInstance->mMethodDef == setMethod);
			CheckPropFail(setMethod, methodInstance.mMethodInstance, (mPropGetMethodFlags & BfGetMethodInstanceFlag_Friend) == 0);

			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(mPropSrc)) && (autoComplete->mResolveType == BfResolveType_GetResultString))
			{
				autoComplete->mResultString = ":";
				autoComplete->mResultString += mModule->TypeToString(methodInstance.mMethodInstance->GetParamType(0));
				autoComplete->mResultString += " ";
				autoComplete->mResultString += mModule->TypeToString(methodInstance.mMethodInstance->GetOwner());
				autoComplete->mResultString += ".";
				autoComplete->mResultString += mPropDef->mName;
			}

			bool handled = false;

			BfTypedValue convVal;
			if (binaryOp != BfBinaryOp_None)
			{
 				BfTypedValue leftValue = mModule->CreateValueFromExpression(assignExpr->mLeft, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowIntUnknown));
 				if (!leftValue)
 					return;

				bool evaluatedRight = false;
				auto opResult = PerformAssignment_CheckOp(assignExpr, true, leftValue, rightValue, evaluatedRight);
				if (opResult)
				{
					mResult = opResult;
					return;
				}
				else
				{
					if (evaluatedRight)
					{
						if (!rightValue)
							return;
						PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, BfBinOpFlag_ForceLeftType, leftValue, rightValue);
					}
					else
						PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, BfBinOpFlag_ForceLeftType, leftValue);
					if (!mResult)
						return;
					convVal = mResult;
					mResult = BfTypedValue();
					if (!convVal)
						return;
				}
			}
			else
			{
				auto wantType = methodInstance.mMethodInstance->GetParamType(0);
				if (rightValue)
				{
					convVal = mModule->Cast(assignExpr->mRight, rightValue, wantType);
				}
				else
				{
					if (assignExpr->mRight == NULL)
					{
						mModule->AssertErrorState();
						return;
					}
					BfEvalExprFlags exprFlags = (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_PendingPropSet);
					if (wantType->IsRef())
						exprFlags = (BfEvalExprFlags)(exprFlags | BfEvalExprFlags_AllowRefExpr);
					convVal = mModule->CreateValueFromExpression(assignExpr->mRight, wantType, exprFlags);
				}
				if (!convVal)
				{
					mPropDef = NULL;
					return;
				}
			}

			if (!handled)
			{
				if (mPropSrc != NULL)
					mModule->UpdateExprSrcPos(mPropSrc);

				BfResolvedArg valueArg;
				valueArg.mTypedValue = convVal;
				mIndexerValues.Insert(0, valueArg);

				if (!setMethod->mIsStatic)
				{
					auto owner = methodInstance.mMethodInstance->GetOwner();
					if ((mPropTarget.mType != owner) ||
						((mPropTarget.mValue.IsFake()) && (!mOrigPropTarget.mValue.IsFake())))
					{
						if ((mPropDefBypassVirtual) || (!mPropTarget.mType->IsInterface()))
						{
							mPropTarget = mModule->Cast(mPropSrc, mOrigPropTarget, owner);
							if (!mPropTarget)
							{
								mModule->Fail("Internal property error", mPropSrc);
								return;
							}
						}
					}
				}

				auto callFlags = mPropDefBypassVirtual ? BfCreateCallFlags_BypassVirtual : BfCreateCallFlags_None;
				mResult = CreateCall(mPropSrc, mPropTarget, mOrigPropTarget, setMethod, methodInstance, callFlags, mIndexerValues, NULL);
				mPropDef = NULL;
				mResult = convVal;
				mIndexerValues.Clear();
				return;
			}
		}
	}

	auto toType = ptr.mType;
	if (toType->IsRef())
	{
		auto refType = (BfRefType*)toType;
		toType = refType->mElementType;
	}
	if (toType->IsIntUnknown())
		toType = mModule->FixIntUnknown(toType);

	if ((autoComplete != NULL) && (assignExpr->mOpToken != NULL) && (toType != NULL))
		autoComplete->CheckEmptyStart(assignExpr->mOpToken, toType);

	BfExpression* rightExpr = assignExpr->mRight;
	if (rightExpr == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	bool alreadyWritten = false;
	BfTypedValue convVal;
	if (binaryOp != BfBinaryOp_None)
	{
		CheckResultForReading(ptr);
		BfTypedValue leftValue = ptr;

		bool deferBinop = false;
		BfDeferEvalChecker deferEvalChecker;
		deferEvalChecker.mDeferLiterals = false;
		assignExpr->mRight->Accept(&deferEvalChecker);
		if (deferEvalChecker.mNeedsDeferEval)
			deferBinop = true;

		if (binaryOp == BfBinaryOp_NullCoalesce)
		{
			deferBinop = true;
		}

		if (!deferBinop)
		{
			auto expectedType = ptr.mType;
			if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
				expectedType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

			if ((!rightValue) && (assignExpr->mRight != NULL))
			{
				rightValue = mModule->CreateValueFromExpression(assignExpr->mRight, expectedType, (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_NoCast));
			}
		}

		BfResolvedArgs argValues;

		if ((rightValue) || (deferBinop))
		{
			bool evaluatedRight = false;
			auto opResult = PerformAssignment_CheckOp(assignExpr, deferBinop, leftValue, rightValue, evaluatedRight);
			if (opResult)
			{
				mResult = opResult;
				return;
			}
			else
			{
				auto flags = BfBinOpFlag_ForceLeftType;
				if (deferBinop)
					flags = (BfBinOpFlags)(flags | BfBinOpFlag_DeferRight);

				leftValue = mModule->LoadValue(leftValue);

				if (binaryOp == BfBinaryOp_NullCoalesce)
				{
					if (!CheckModifyResult(ptr, assignExpr->mOpToken, "assign to", false, false, true))
					{
						mModule->CreateValueFromExpression(assignExpr->mRight, ptr.mType, (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_NoCast));
						mResult = leftValue;
						return;
					}
					if (PerformBinaryOperation_NullCoalesce(assignExpr->mOpToken, assignExpr->mLeft, assignExpr->mRight, leftValue, leftValue.mType, &ptr))
						return;
				}

				PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, flags, leftValue, rightValue);
			}
		}

		convVal = mResult;
		mResult = BfTypedValue();

		if (!convVal)
			return;
	}
	else
	{
		convVal = rightValue;

		if (!convVal)
		{
			if (auto uninitExpr = BfNodeDynCast<BfUninitializedExpression>(rightExpr))
			{
				if (mResultLocalVar != NULL)
				{
					MarkResultAssigned();
					return;
				}
			}

			// In the cases like "structVal = GetVal()", the allowDirectStructRetWrite optimization allows us to pass the
			//  address of structVal into the sret.  We only allow that if structVal is a local, because if it's globally
			//  visible then we could see the results of a partially-modified structVal
			//bool allowDirectStructRetWrite = mResultLocalVarIdx != -1;
			// ALTHOUGH- we can only allow this optimization if we can be sure the local value is not aliased- we could
			//  have the backend ensure that this local value never gets its address taken.
			bool allowDirectStructWrite = false;

			BfExprEvaluator exprEvaluator(mModule);
			exprEvaluator.mExpectingType = toType;
			if (allowDirectStructWrite)
				exprEvaluator.mReceivingValue = &ptr;
			exprEvaluator.Evaluate(rightExpr, false, false, true);
			exprEvaluator.CheckResultForReading(exprEvaluator.mResult);
			convVal = exprEvaluator.GetResult();
			mModule->FixIntUnknown(convVal);
			alreadyWritten = (allowDirectStructWrite) && (exprEvaluator.mReceivingValue == NULL);

			if (!convVal)
				convVal = mModule->GetDefaultTypedValue(toType);

			// Did we use mReceivingValue as a mixin result?
			if ((convVal.mValue) && (convVal.mValue == ptr.mValue))
			{
				mResult = convVal;
				return;
			}
		}
	}

	if (!CheckModifyResult(ptr, assignExpr->mOpToken, "assign to", false, false, true))
	{
		mResult = convVal;
		return;
	}

	if (ptr.mKind == BfTypedValueKind_CopyOnMutateAddr)
		ptr.mKind = BfTypedValueKind_Addr;
	else if (ptr.IsCopyOnMutate())
		ptr = mModule->CopyValue(ptr);

	BF_ASSERT(convVal);
	if ((convVal) && (convVal.mType->IsNull()) && (ptr.mType->IsNullable()))
	{
		// Allow this to pass through so we can catch it in the memset later in this function
	}
	else
	{
		if (!convVal.mType->IsComposite())
			convVal = mModule->LoadValue(convVal);
		convVal = mModule->Cast(rightExpr, convVal, toType);
		if (!convVal)
			return;
		convVal = mModule->LoadValue(convVal);
	}

	if (ptr.mType->IsVar())
	{
		mResult = ptr;
		MarkResultAssigned();
		return;
	}
	if (convVal.mValue)
	{
		if ((ptr.mType->IsStruct()) && (!ptr.mType->IsValuelessType()) && (convVal.mValue.IsConst()))
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(convVal.mValue);
			if ((constant->mTypeCode == BfTypeCode_NullPtr) || (constant->mConstType == BfConstType_AggZero))
			{
				auto type = ptr.mType;
				mModule->mBfIRBuilder->CreateMemSet(ptr.mValue, mModule->GetConstValue(0, mModule->GetPrimitiveType(BfTypeCode_Int8)),
					mModule->GetConstValue(type->mSize), type->mAlign);
				mResult = ptr;
				MarkResultAssigned();
				return;
			}
		}

// 		if (ptr.mType->IsMethodRef())
// 		{
// 			auto methodRefType = (BfMethodRefType*)ptr.mType;
// 			auto methodInstance = methodRefType->mMethodInstance;
// 			int implicitParamCount = methodInstance->GetImplicitParamCount();
// 			for (int implicitParamIdx = methodInstance->HasThis() ? - 1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
// 			{
// 				bool failed = false;
// 				auto destPtr = DoImplicitArgCapture(assignExpr, methodRefType->mMethodInstance, implicitParamIdx, failed, BfImplicitParamKind_GenericTypeMember_Addr);
// 				auto srcVal = DoImplicitArgCapture(assignExpr, methodRefType->mMethodInstance, implicitParamIdx, failed, BfImplicitParamKind_GenericMethodMember);
// 				if ((destPtr) && (srcVal))
// 				{
// 					srcVal = mModule->AggregateSplat(srcVal);
// 					mModule->mBfIRBuilder->CreateStore(srcVal.mValue, destPtr.mValue);
// 				}
// 			}
// 		}
// 		else
		{
			mModule->mBfIRBuilder->PopulateType(ptr.mType);

			if (convVal.IsSplat())
			{
				//convVal = mModule->AggregateSplat(convVal);
				mModule->AggregateSplatIntoAddr(convVal, ptr.mValue);
			}
			else
			{
				if (ptr.mType->IsValuelessType())
				{
					mModule->EmitEnsureInstructionAt();
				}
				else if (!alreadyWritten)
				{
					if ((mModule->mIsComptimeModule) && (mModule->mCompiler->mCeMachine->mDebugger != NULL) && (mModule->mCompiler->mCeMachine->mDebugger->mCurDbgState != NULL))
					{
						auto ceDbgState = mModule->mCompiler->mCeMachine->mDebugger->mCurDbgState;
						bool success = false;

						if ((convVal.mValue.IsConst()) && (ptr.mValue.IsConst()))
						{
							auto constant = mModule->mBfIRBuilder->GetConstant(ptr.mValue);
							auto valConstant = mModule->mBfIRBuilder->GetConstant(convVal.mValue);

							auto ceTypedVal = mModule->mCompiler->mCeMachine->mDebugger->GetAddr(constant);
							if (!ceTypedVal)
							{
								mModule->Fail("Invalid assignment address", assignExpr);
								return;
							}

							auto ceContext = mModule->mCompiler->mCeMachine->mCurContext;
							if (ceContext->CheckMemory((addr_ce)ceTypedVal.mAddr, convVal.mType->mSize))
							{
								if ((ceDbgState->mDbgExpressionFlags & DwEvalExpressionFlag_AllowSideEffects) != 0)
								{
									ceDbgState->mHadSideEffects = true;
									if (ceContext->WriteConstant(mModule, (addr_ce)ceTypedVal.mAddr, valConstant, convVal.mType))
										success = true;
								}
								else
								{
									ceDbgState->mBlockedSideEffects = true;
									success = true;
								}
							}
						}

						if (!success)
						{
							mModule->Fail("Assignment failed", assignExpr);
							return;
						}
					}

					if (!alreadyWritten)
					{
						//ptr = mModule->LoadValue(ptr);
						BF_ASSERT(ptr.IsAddr());
						convVal = mModule->LoadValue(convVal);
						auto storeInst = mModule->mBfIRBuilder->CreateAlignedStore(convVal.mValue, ptr.mValue, ptr.mType->mAlign, mIsVolatileReference);
					}
				}
			}
		}
	}
	else
	{
		BF_ASSERT(convVal.mType->IsValuelessType());
	}
	mResult = convVal;

	MarkResultAssigned();
}

void BfExprEvaluator::Visit(BfAssignmentExpression* assignExpr)
{
	if (assignExpr->mLeft->IsA<BfTupleExpression>())
	{
		DoTupleAssignment(assignExpr);
		return;
	}

	BfAutoParentNodeEntry autoParentNodeEntry(mModule, assignExpr);

	BfTypedValue cascadeValue;
	PerformAssignment(assignExpr, false, BfTypedValue(), &cascadeValue);
	if (cascadeValue)
		mResult = cascadeValue;
}

void BfExprEvaluator::Visit(BfParenthesizedExpression* parenExpr)
{
	VisitChild(parenExpr->mExpression);
	MakeResultAsValue();
}

void BfExprEvaluator::InitializedSizedArray(BfSizedArrayType* arrayType, BfTokenNode* openToken, const BfSizedArray<BfExpression*>& valueExprs, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken, BfTypedValue* receivingValue)
{
	struct InitValue
	{
		BfTypedValue mValue;
		bool mIsUninitialized;
		bool mIsDefaultInitializer;
		bool mIsDeferred;

		InitValue()
		{
			mIsUninitialized = false;
			mIsDefaultInitializer = false;
			mIsDeferred = false;
		}
	};

	SizedArray<InitValue, 8> values;

	{
		//bool hasFailed = false;
		HashSet<int> failedAt;
		bool isAllConst = true;

		//bool endUninitialzied = false;

		int depth = 0;

		std::function<void(BfSizedArrayType*, BfTokenNode* openToken, const BfSizedArray<BfExpression*>&, const BfSizedArray<BfTokenNode*>&, BfTokenNode*, bool)>
			_GetValues = [&](BfSizedArrayType* checkArrayType, BfTokenNode* openToken, const BfSizedArray<BfExpression*>& valueExprs, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken, bool ignore)
		{
			int64 initCountDiff = (int)valueExprs.size() - checkArrayType->mElementCount;
			if ((initCountDiff != 0) && (!valueExprs.IsEmpty()) && (!failedAt.Contains(depth)))
			{
				if (checkArrayType->mElementCount == -1)
				{
// 					mModule->Fail("Initializers not supported for unknown-sized arrays", valueExprs[0]);
// 					failedAt.Add(depth);
				}
				else if (initCountDiff > 0)
				{
					mModule->Fail(StrFormat("Too many initializers, expected %d fewer", initCountDiff), valueExprs[BF_MAX((int)checkArrayType->mElementCount, 0)]);
					failedAt.Add(depth);
				}
				else
				{
					// If it ends with ", ?) or ",)" then allow unsized
					if (((valueExprs.size() == 0) || (BfNodeDynCast<BfUninitializedExpression>(valueExprs.back()) == NULL)) &&
						((commas.size() < valueExprs.size()) || (valueExprs.size() == 0)))
					{
						BfAstNode* refNode = closeToken;
						if ((refNode == NULL) && (mModule->mParentNodeEntry != NULL))
							refNode = mModule->mParentNodeEntry->mNode;
						BF_ASSERT(refNode != NULL);
						mModule->Fail(StrFormat("Too few initializer, expected %d more", -initCountDiff), refNode);
						failedAt.Add(depth);
					}
				}
			}

			for (int idx = 0; idx < BF_MAX(checkArrayType->mElementCount, valueExprs.size()); idx++)
			{
				BfTypedValue elementValue;
				bool deferredValue = false;
				BfExpression* expr = NULL;
				if (idx < (int)valueExprs.size())
				{
					expr = valueExprs[idx];
					if (expr == NULL)
					{
						if (idx == 0)
							mModule->FailAfter("Expression expected", openToken);
						else
							mModule->FailAfter("Expression expected", commas[idx - 1]);
					}

					if ((BfNodeDynCastExact<BfUninitializedExpression>(expr) != NULL) && (idx == (int)commas.size()))
					{
						isAllConst = false;
						break;
					}

					if (checkArrayType->mElementType->IsSizedArray())
					{
						if (auto arrayInitExpr = BfNodeDynCast<BfTupleExpression>(expr))
						{
							depth++;
							_GetValues((BfSizedArrayType*)checkArrayType->mElementType, arrayInitExpr->mOpenParen, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseParen, ignore);
							depth--;
							continue;
						}
						else if (auto arrayInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(expr))
						{
							depth++;
							_GetValues((BfSizedArrayType*)checkArrayType->mElementType, arrayInitExpr->mOpenBrace, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseBrace, ignore);
							depth--;
							continue;
						}
						else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
						{
							depth++;
							SizedArray<BfExpression*, 1> values;
							values.Add(parenExpr->mExpression);
							SizedArray<BfTokenNode*, 1> commas;
							_GetValues((BfSizedArrayType*)checkArrayType->mElementType, parenExpr->mOpenParen, values, commas, parenExpr->mCloseParen, ignore);
							depth--;
							continue;
						}
					}

					if (expr != NULL)
					{
						auto evalFlags = (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags);

						bool tryDefer = false;
 						if ((checkArrayType->IsComposite()) &&
 							((expr->IsA<BfInvocationExpression>()) || (expr->IsExact<BfTupleExpression>())))
 						{
							// We evaluate with a new scope because this expression may create variables that we don't want to be visible to other
							//  non-deferred evaluations (since the value may actually be a FakeVal)
							SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
							elementValue = mModule->CreateValueFromExpression(expr, checkArrayType->mElementType, (BfEvalExprFlags)(evalFlags | BfEvalExprFlags_CreateConditionalScope));
							deferredValue = !prevIgnoreWrites.mPrevVal && elementValue.mValue.IsFake();
 						}
						else
						{
							elementValue = mModule->CreateValueFromExpression(expr, checkArrayType->mElementType, evalFlags);
						}

						if (!elementValue)
							elementValue = mModule->GetDefaultTypedValue(checkArrayType->mElementType);

						if ((!elementValue) || (!CheckAllowValue(elementValue, expr)))
							elementValue = mModule->GetDefaultTypedValue(checkArrayType->mElementType);

						// For now, we can't properly create const-valued non-size-aligned composites
						if (!elementValue.mValue.IsConst())
							isAllConst = false;
						if (elementValue.IsAddr())
							isAllConst = false;

						InitValue initValue;
						initValue.mValue = elementValue;
						initValue.mIsDeferred = deferredValue;
						values.push_back(initValue);
					}
				}
			}
		};

		int valueIdx = 0;

		std::function<void(BfTypedValue, BfTokenNode* openToken, const BfSizedArray<BfExpression*>&, const BfSizedArray<BfTokenNode*>&, BfTokenNode*)>
			_CreateMemArray = [&](BfTypedValue arrayValue, BfTokenNode* openToken, const BfSizedArray<BfExpression*>& valueExprs, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken)
		{
			BF_ASSERT(arrayValue.mType->IsSizedArray());
			auto checkArrayType = (BfSizedArrayType*)arrayValue.mType;

			int valIdx = 0;

			bool hasUninit = false;
			for (int idx = 0; idx < checkArrayType->mElementCount; idx++)
			{
				BfExpression* expr = NULL;
				BfTypedValue elementValue;
				if (idx >= (int)valueExprs.size())
					break;

				expr = valueExprs[idx];

				if ((BfNodeDynCastExact<BfUninitializedExpression>(expr) != NULL) && (idx == (int)commas.size()))
				{
					hasUninit = true;
					break;
				}

				BfIRValue elemPtrValue = mModule->CreateIndexedValue(checkArrayType->mElementType, arrayValue.mValue, valIdx, true);
				valIdx++;

				if (checkArrayType->mElementType->IsSizedArray())
				{
					if (auto arrayInitExpr = BfNodeDynCast<BfTupleExpression>(expr))
					{
						_CreateMemArray(BfTypedValue(elemPtrValue, checkArrayType->mElementType, true), arrayInitExpr->mOpenParen, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseParen);
						continue;
					}
					else if (auto arrayInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(expr))
					{
						_CreateMemArray(BfTypedValue(elemPtrValue, checkArrayType->mElementType, true), arrayInitExpr->mOpenBrace, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseBrace);
						continue;
					}
					else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
					{
						depth++;
						SizedArray<BfExpression*, 1> values;
						values.Add(parenExpr->mExpression);
						SizedArray<BfTokenNode*, 1> commas;
						_CreateMemArray(BfTypedValue(elemPtrValue, checkArrayType->mElementType, true), parenExpr->mOpenParen, values, commas, parenExpr->mCloseParen);
						depth--;
						continue;
					}
				}

				if (expr != NULL)
				{
					InitValue initValue = values[valueIdx++];
					elementValue = initValue.mValue;

					if (initValue.mIsDeferred)
					{
						BfTypedValue elemePtrTypedVal = BfTypedValue(elemPtrValue, checkArrayType->mElementType, BfTypedValueKind_Addr);

						BfExprEvaluator exprEvaluator(mModule);
						exprEvaluator.mExpectingType = checkArrayType->mElementType;
						exprEvaluator.mReceivingValue = &elemePtrTypedVal;
						exprEvaluator.Evaluate(expr);
						exprEvaluator.GetResult();

						if (exprEvaluator.mReceivingValue == NULL)
						{
							// We wrote directly to the array in-place, we're done with this element
							continue;
						}

						elementValue = exprEvaluator.mResult;
						elementValue = mModule->Cast(expr, elementValue, checkArrayType->mElementType);
						if (!elementValue)
						{
							mModule->AssertErrorState();
							continue;
						}
					}

					elementValue = mModule->LoadValue(elementValue);
					mModule->mBfIRBuilder->CreateAlignedStore(elementValue.mValue, elemPtrValue, checkArrayType->mElementType->mAlign);
				}
			}

			int fillCount = (int)(checkArrayType->mElementCount - valIdx);
			if (fillCount > 0)
			{
				BfIRValue elemPtrValue = mModule->CreateIndexedValue(checkArrayType->mElementType, arrayValue.mValue, valIdx, true);
				if (hasUninit)
				{
					if (!mModule->IsOptimized())
					{
						int setSize = std::min(checkArrayType->mElementType->mSize, 128); // Keep it to a reasonable number of bytes to trash
						mModule->mBfIRBuilder->CreateMemSet(elemPtrValue, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int8, 0xCC),
							mModule->GetConstValue(setSize), checkArrayType->mElementType->mAlign);
					}
				}
				else
				{
					int setSize = (int)((checkArrayType->mElementType->GetStride() * (fillCount - 1)) + checkArrayType->mElementType->mSize);
					if (setSize >= 0)
					{
						mModule->mBfIRBuilder->CreateMemSet(elemPtrValue, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int8, 0),
							mModule->GetConstValue(setSize), checkArrayType->mElementType->mAlign);
					}
				}
			}
		};

		std::function<BfIRValue(BfTypedValue, BfTokenNode*, const BfSizedArray<BfExpression*>&, const BfSizedArray<BfTokenNode*>&, BfTokenNode*)>
			_CreateConstArray = [&](BfTypedValue arrayValue, BfTokenNode* openToken, const BfSizedArray<BfExpression*>& valueExprs, const BfSizedArray<BfTokenNode*>& commas, BfTokenNode* closeToken)
		{
			SizedArray<BfIRValue, 8> members;

			BF_ASSERT(arrayValue.mType->IsSizedArray());
			auto checkArrayType = (BfSizedArrayType*)arrayValue.mType;

			int valIdx = 0;
			for (int idx = 0; idx < checkArrayType->mElementCount; idx++)
			{
				BfTypedValue elementValue;
				if (idx >= (int)valueExprs.size())
					break;

				auto expr = valueExprs[idx];
				if (expr == NULL)
					continue;

				valIdx++;

				if (checkArrayType->mElementType->IsSizedArray())
				{
					if (auto arrayInitExpr = BfNodeDynCast<BfTupleExpression>(expr))
					{
						members.push_back(_CreateConstArray(checkArrayType->mElementType, arrayInitExpr->mOpenParen, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseParen));
						continue;
					}
					else if (auto arrayInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(expr))
					{
						members.push_back(_CreateConstArray(checkArrayType->mElementType, arrayInitExpr->mOpenBrace, arrayInitExpr->mValues, arrayInitExpr->mCommas, arrayInitExpr->mCloseBrace));
						continue;
					}
					else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
					{
						depth++;
						SizedArray<BfExpression*, 1> values;
						values.Add(parenExpr->mExpression);
						SizedArray<BfTokenNode*, 1> commas;
						members.push_back(_CreateConstArray(checkArrayType->mElementType, parenExpr->mOpenParen, values, commas, parenExpr->mCloseParen));
						depth--;
						continue;
					}
				}

				InitValue initValue = values[valueIdx++];
				BF_ASSERT(!initValue.mIsUninitialized);
				elementValue = initValue.mValue;
				members.push_back(elementValue.mValue);
			}

			int fillCount = (int)(checkArrayType->mElementCount - valIdx);
			if (fillCount > 0)
			{
				// We just need to insert one default value, it will be duplicated as needed into the backend
				auto defaultVal = mModule->GetDefaultTypedValue(checkArrayType->GetUnderlyingType());
				BF_ASSERT(defaultVal.mValue.IsConst());
				members.push_back(defaultVal.mValue);
			}

			auto allocArrayType = checkArrayType;
			if (checkArrayType->IsUndefSizedArray())
				allocArrayType = mModule->CreateSizedArrayType(checkArrayType->GetUnderlyingType(), (int)members.size());

			return mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(checkArrayType), members);
		};

		_GetValues(arrayType, openToken, valueExprs, commas, closeToken, false);

		if (!failedAt.IsEmpty())
		{
			mResult = mModule->GetDefaultTypedValue(arrayType, false, BfDefaultValueKind_Addr);
			return;
		}

		if (receivingValue != NULL)
		{
			BF_ASSERT(receivingValue->mType == arrayType);
			BF_ASSERT(receivingValue->IsAddr());

			mResult = *receivingValue;
			_CreateMemArray(mResult, openToken, valueExprs, commas, closeToken);
		}
		else if (isAllConst)
		{
			mResult = BfTypedValue(_CreateConstArray(arrayType, openToken, valueExprs, commas, closeToken), arrayType, BfTypedValueKind_Value);
		}
		else
		{
			if ((mReceivingValue != NULL) && (mReceivingValue->mType == arrayType) && (mReceivingValue->IsAddr()))
			{
				mResult = *mReceivingValue;
				mReceivingValue = NULL;
			}
			else
			{
				auto arrayValue = mModule->CreateAlloca(arrayType);
				mResult = BfTypedValue(arrayValue, arrayType, BfTypedValueKind_TempAddr);
			}
			if (!arrayType->IsValuelessType())
				_CreateMemArray(mResult, openToken, valueExprs, commas, closeToken);
		}
	}
}

void BfExprEvaluator::Visit(BfTupleExpression* tupleExpr)
{
	BfTypeInstance* tupleType = NULL;
	bool hadFullMatch = false;
	if ((mExpectingType != NULL) && (mExpectingType->IsTuple()))
	{
		tupleType = (BfTypeInstance*)mExpectingType;
		hadFullMatch = tupleType->mFieldInstances.size() == tupleExpr->mValues.size();
	}

	struct InitValue
	{
		BfTypedValue mValue;
		bool mIsUninitialized;
		bool mIsDefaultInitializer;
		bool mIsDeferred;

		InitValue()
		{
			mIsUninitialized = false;
			mIsDefaultInitializer = false;
			mIsDeferred = false;
		}
	};

	SizedArray<BfTypedValue, 2> typedValues;

	if ((tupleExpr->mCommas.size() != 0) && (tupleExpr->mCommas.size() >= tupleExpr->mValues.size()))
	{
		// We would normally give this error during syntax parsing, but a TupleExpression can be an array initializer
		mModule->FailAfter("Expression expected", tupleExpr->mCommas.back());
	}

	for (int valueIdx = 0; valueIdx < (int)tupleExpr->mValues.size(); valueIdx++)
	{
		BfExpression* valueExpr = tupleExpr->mValues[valueIdx];
		BfType* fieldType = NULL;
		BfFieldInstance* fieldInstance = NULL;
		if (tupleType != NULL)
		{
			if (valueIdx < (int)tupleType->mFieldInstances.size())
			{
				fieldInstance = (BfFieldInstance*)&tupleType->mFieldInstances[valueIdx];
				fieldType = fieldInstance->GetResolvedType();
				if (fieldType->IsVoid())
				{
					typedValues.push_back(BfTypedValue());
					continue;
				}

				if (fieldType->IsVar())
				{
					hadFullMatch = false;
					fieldType = NULL;
				}
			}
		}

		bool tryDefer = false;
		if (((fieldType == NULL) || (fieldType->IsComposite())) &&
			((valueExpr->IsA<BfInvocationExpression>()) || (valueExpr->IsExact<BfTupleExpression>())))
		{
			tryDefer = true;
		}

		SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, mModule->mBfIRBuilder->mIgnoreWrites || tryDefer);
		BfTypedValue value = mModule->CreateValueFromExpression(valueExpr, fieldType);

		if (!value)
		{
			if (fieldType != NULL)
				value = mModule->GetDefaultTypedValue(fieldType);
			else
				value = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
		}

		if ((fieldInstance != NULL) && (!fieldInstance->GetFieldDef()->IsUnnamedTupleField()) && (valueIdx < (int)tupleExpr->mNames.size()))
		{
			auto checkName = tupleExpr->mNames[valueIdx];
			if (checkName != NULL)
			{
				if (checkName->ToString() != fieldInstance->GetFieldDef()->mName)
					hadFullMatch = false;
			}
		}

		value = mModule->LoadValue(value);
		typedValues.push_back(value);
	}

	if (!hadFullMatch)
	{
		BfTypeVector fieldTypes;
		Array<String> fieldNames;
		HashSet<String> fieldNameSet;
		for (auto typedVal : typedValues)
		{
			auto type = typedVal.mType;
			if (type != NULL)
				fieldTypes.push_back(type);
			else
				fieldTypes.push_back(mModule->mContext->mBfObjectType);
		}
		for (BfTupleNameNode* requestedName : tupleExpr->mNames)
		{
			if (requestedName == NULL)
				fieldNames.push_back("");
			else
			{
				auto fieldName = requestedName->mNameNode->ToString();
				if (!fieldNameSet.TryAdd(fieldName, NULL))
				{
					mModule->Fail(StrFormat("A field named '%s' has already been declared", fieldName.c_str()), requestedName->mNameNode);
				}
				fieldNames.push_back(fieldName);
			}
		}
		tupleType = mModule->CreateTupleType(fieldTypes, fieldNames);
	}

	mModule->mBfIRBuilder->PopulateType(tupleType);

	BfIRValue curTupleValue;

	if ((mReceivingValue != NULL) && (mReceivingValue->mType == tupleType) && (mReceivingValue->IsAddr()))
	{
		mResult = *mReceivingValue;
		mReceivingValue = NULL;
		curTupleValue = mResult.mValue;
	}
	else
	{
		int valueIdx = -1;
		bool isExactConst = true;

		for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			if (fieldInstance->mDataIdx < 0)
				continue;
			++valueIdx;
			auto typedValue = typedValues[valueIdx];
			if (typedValue.mType != fieldInstance->mResolvedType)
			{
				isExactConst = false;
				break;
			}
			if (!typedValue.mValue.IsConst())
			{
				isExactConst = false;
				break;
			}
		}

		if (isExactConst)
		{
			mModule->PopulateType(tupleType);

			Array<BfIRValue> irValues;
			irValues.Resize(typedValues.mSize + 1);
			irValues[0] = mModule->mBfIRBuilder->CreateConstAggZero(mModule->mBfIRBuilder->MapType(tupleType->mBaseType));

			for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
			{
				BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
				if (fieldInstance->mDataIdx < 0)
					continue;
				irValues[fieldInstance->mDataIdx] = typedValues[fieldIdx].mValue;
			}

			for (auto& val : irValues)
			{
				if (!val)
					val = mModule->mBfIRBuilder->CreateConstArrayZero(0);
			}

			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(tupleType), irValues), tupleType);
			return;
		}

		curTupleValue = mModule->CreateAlloca(tupleType);
		mResultIsTempComposite = true;
		mResult = BfTypedValue(curTupleValue, tupleType, BfTypedValueKind_TempAddr);
	}

	int valueIdx = -1;
	for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
	{
		BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
		++valueIdx;
		if (fieldInstance->mResolvedType->IsValuelessType())
			continue;
		auto typedVal = typedValues[valueIdx];
		if (!typedVal)
		{
			mModule->AssertErrorState();
			continue;
		}
		if (fieldInstance->mDataIdx >= 0)
		{
			auto memberVal = mModule->mBfIRBuilder->CreateInBoundsGEP(curTupleValue, 0, fieldInstance->mDataIdx);

			if ((!mModule->mBfIRBuilder->mIgnoreWrites) && (typedVal.mValue.IsFake()))
			{
				// Value was deferred.  Allow us to try to init in place
				BfExpression* valueExpr = tupleExpr->mValues[valueIdx];

				BfTypedValue memberPtrTypedVal = BfTypedValue(memberVal, fieldInstance->mResolvedType, BfTypedValueKind_Addr);

				BfExprEvaluator exprEvaluator(mModule);
				exprEvaluator.mExpectingType = fieldInstance->mResolvedType;
				exprEvaluator.mReceivingValue = &memberPtrTypedVal;
				exprEvaluator.Evaluate(valueExpr);
				exprEvaluator.GetResult();

				if (exprEvaluator.mReceivingValue == NULL)
				{
					// We wrote directly to the array in-place, we're done with this element
					continue;
				}

				typedVal = exprEvaluator.mResult;
				typedVal = mModule->Cast(valueExpr, typedVal, fieldInstance->mResolvedType);
				if (!typedVal)
				{
					mModule->AssertErrorState();
					continue;
				}
				typedVal = mModule->LoadValue(typedVal);
			}

			if (typedVal.mType->IsVar())
			{
				// Do nothing
			}
			else if (typedVal.IsSplat())
				mModule->AggregateSplatIntoAddr(typedVal, memberVal);
			else
				mModule->mBfIRBuilder->CreateAlignedStore(typedVal.mValue, memberVal, typedVal.mType->mAlign);
		}
	}
}

BfTypedValue BfExprEvaluator::SetupNullConditional(BfTypedValue thisValue, BfTokenNode* dotToken)
{
	bool isStaticLookup = (!thisValue) ||
		((!thisValue.mType->IsValuelessType()) && (!thisValue.mValue));

	if (isStaticLookup)
	{
		mModule->Fail("Null conditional reference not valid for static field references", dotToken);
		return thisValue;
	}

	auto opResult = PerformUnaryOperation_TryOperator(thisValue, NULL, BfUnaryOp_NullConditional, dotToken, BfUnaryOpFlag_None);
	if (opResult)
		thisValue = opResult;

	if (thisValue.mType->IsGenericParam())
	{
		bool isValid = false;

		auto genericParams = mModule->GetGenericParamInstance((BfGenericParamType*)thisValue.mType);
		if (genericParams->mTypeConstraint != NULL)
		{
			if ((genericParams->mTypeConstraint->IsNullable()) ||
				(genericParams->mTypeConstraint->IsPointer()) ||
				(genericParams->mTypeConstraint->IsObjectOrInterface()))
				isValid = true;
		}

		if ((genericParams->mGenericParamFlags & (BfGenericParamFlag_Var | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Class)) != 0)
			isValid = true;

		if (isValid)
			return thisValue;
	}

	if ((thisValue.mType->IsNullable()) || (thisValue.mType->IsVar()))
	{
		// Success
	}
	else if ((thisValue.mType->IsPointer()) || (thisValue.mType->IsObjectOrInterface()))
	{
		// Also good
	}
	else
	{
		bool canBeNull = false;
		if (thisValue.mType->IsGenericParam())
			canBeNull = true;

		if (!canBeNull)
			mModule->Warn(0, StrFormat("Null conditional reference is unnecessary since value type '%s' can never be null", mModule->TypeToString(thisValue.mType).c_str()), dotToken);
		return thisValue;
	}

	thisValue = mModule->LoadValue(thisValue);
	if (thisValue.mType->IsVar())
		return thisValue;

	BfPendingNullConditional* pendingNullCond = mModule->mCurMethodState->mPendingNullConditional;
	if (pendingNullCond == NULL)
	{
		pendingNullCond = new BfPendingNullConditional();
		mModule->mCurMethodState->mPendingNullConditional = pendingNullCond;
	}
	if (!pendingNullCond->mPrevBB)
		pendingNullCond->mPrevBB = mModule->mBfIRBuilder->GetInsertBlock();

	if (!pendingNullCond->mDoneBB)
		pendingNullCond->mDoneBB = mModule->mBfIRBuilder->CreateBlock("nullCond.done");

	// We will in the br to checkBB later
	if (!pendingNullCond->mCheckBB)
	{
		pendingNullCond->mCheckBB = mModule->mBfIRBuilder->CreateBlock("nullCond.check");
		mModule->AddBasicBlock(pendingNullCond->mCheckBB);
	}

 	BfIRValue isNotNull;

	if (thisValue.mType->IsNullable())
	{
		BfTypeInstance* nullableType = (BfTypeInstance*)thisValue.mType->ToTypeInstance();
		auto elementType = nullableType->GetUnderlyingType();
		if (elementType->IsValuelessType())
		{
			thisValue = mModule->MakeAddressable(thisValue);
			BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 1); // mHasValue
			isNotNull = mModule->mBfIRBuilder->CreateAlignedLoad(hasValuePtr, 1);
			thisValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), elementType, true);
		}
		else
		{
			thisValue = mModule->MakeAddressable(thisValue);
			BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 2); // mHasValue
			isNotNull = mModule->mBfIRBuilder->CreateAlignedLoad(hasValuePtr, 1);
			BfIRValue valuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 1); // mValue
			thisValue = BfTypedValue(valuePtr, elementType, true);
		}
	}
	else
		isNotNull = mModule->mBfIRBuilder->CreateIsNotNull(thisValue.mValue);
	BfIRBlock notNullBB = mModule->mBfIRBuilder->CreateBlock("nullCond.notNull");
	pendingNullCond->mNotNullBBs.Add(notNullBB);
	mModule->mBfIRBuilder->CreateCondBr(isNotNull, notNullBB, pendingNullCond->mDoneBB);

	mModule->AddBasicBlock(notNullBB);
	return thisValue;
}

void BfExprEvaluator::CheckDotToken(BfTokenNode* tokenNode)
{
	if ((tokenNode != NULL) && (tokenNode->mToken == BfToken_DotDot))
		mModule->Fail("Unexpected cascade operation. Chaining can only be used for method invocations", tokenNode);
}

void BfExprEvaluator::DoMemberReference(BfMemberReferenceExpression* memberRefExpr, BfTypedValue* outCascadeValue)
{
	CheckDotToken(memberRefExpr->mDotToken);

	BfAttributeState attributeState;
	attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_MemberAccess);

	String findName;
	BfAstNode* nameRefNode = memberRefExpr->mMemberName;
	if (auto attrIdentifierExpr = BfNodeDynCast<BfAttributedIdentifierNode>(memberRefExpr->mMemberName))
	{
		nameRefNode = attrIdentifierExpr->mIdentifier;
		// Don't validate
		attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifierExpr->mAttributes, BfAttributeTargets_SkipValidate);
		if (nameRefNode != NULL)
			findName = attrIdentifierExpr->mIdentifier->ToString();
	}
	else if (memberRefExpr->mMemberName != NULL)
		findName = memberRefExpr->mMemberName->ToString();
	else if (memberRefExpr->mDotToken != NULL)
		mModule->FailAfter("Member name expected", memberRefExpr->mDotToken);

	defer
	(
		if (attributeState.mCustomAttributes != NULL)
		{
			if (mPropDef != NULL)
				attributeState.mTarget = (BfAttributeTargets)(attributeState.mTarget | BfAttributeTargets_Invocation);
			mModule->ValidateCustomAttributes(attributeState.mCustomAttributes, attributeState.mTarget);
		}
	);

	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);

	BfTypeInstance* expectingTypeInst = NULL;
	if (mExpectingType != NULL)
	{
		expectingTypeInst = mExpectingType->ToTypeInstance();
		if (mExpectingType->IsPointer())
			expectingTypeInst = mExpectingType->GetUnderlyingType()->ToTypeInstance();
		else if (mExpectingType->IsNullable())
			expectingTypeInst = mExpectingType->GetUnderlyingType()->ToTypeInstance();
		else if (mExpectingType->IsConstExprValue())
			expectingTypeInst = mExpectingType->GetUnderlyingType()->ToTypeInstance();
		else if (mExpectingType->IsGenericParam())
		{
			auto genericParam = mModule->GetGenericParamInstance((BfGenericParamType*)mExpectingType);
			if (genericParam->mTypeConstraint != NULL)
				expectingTypeInst = genericParam->mTypeConstraint->ToTypeInstance();
		}
	}

	BfAutoComplete* autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		SetAndRestoreValue<bool> prevFriendSet(autoComplete->mHasFriendSet, (attributeState.mCustomAttributes != NULL) && (attributeState.mCustomAttributes->Contains(mModule->mCompiler->mFriendAttributeTypeDef)));

		if (memberRefExpr->mTarget == NULL)
		{
			String filter;
			if ((mExpectingType != NULL) &&
				(autoComplete->InitAutocomplete(memberRefExpr->mDotToken, memberRefExpr->mMemberName, filter)))
			{
				if (expectingTypeInst != NULL)
				{
					bool allowPrivate = expectingTypeInst == mModule->mCurTypeInstance;
					if (expectingTypeInst->IsEnum())
						autoComplete->AddEnumTypeMembers(expectingTypeInst, filter, false, allowPrivate);
					autoComplete->AddSelfResultTypeMembers(expectingTypeInst, expectingTypeInst, filter, allowPrivate);
				}
			}
		}
		else
		{
			autoComplete->CheckMemberReference(memberRefExpr->mTarget, memberRefExpr->mDotToken, memberRefExpr->mMemberName, false, mExpectingType);

			if (auto objCreateExpr = BfNodeDynCast<BfObjectCreateExpression>(memberRefExpr->mTarget))
			{
				// This handles a weird case where we have "obj a = new\nWhatever().Thing = 123;".
				// That gets parsed as "Whatever()" being the type we want to create, and then referencing
				// the "Thing" member of that new object.
				//if (objCreateExpr->mArraySizeSpecifier == NULL)
				CheckObjectCreateTypeRef(mExpectingType, objCreateExpr->mNewNode);
			}
		}
	}

	if (memberRefExpr->mTarget == NULL)
	{
		if (mExpectingType == NULL)
		{
			if (mModule->PreFail())
				mModule->Fail("Unqualified dot syntax can only be used when the result type can be inferred", nameRefNode);
			return;
		}

		if (mExpectingType->IsVar())
		{
			mResult = mModule->GetDefaultTypedValue(mExpectingType);
			return;
		}

		if (mExpectingType->IsSizedArray())
		{
			expectingTypeInst = mModule->GetWrappedStructType(mExpectingType);
		}

		if (expectingTypeInst == NULL)
		{
			if (mModule->PreFail())
				mModule->Fail(StrFormat("Unqualified dot syntax cannot be used with type '%s'", mModule->TypeToString(mExpectingType).c_str()), nameRefNode);
			return;
		}

		BfTypedValue expectingVal(expectingTypeInst);
		mResult = LookupField(memberRefExpr->mMemberName, expectingVal, findName);
		if ((mResult) || (mPropDef != NULL))
			return;
	}

	bool isNullCondLookup = (memberRefExpr->mDotToken != NULL) && (memberRefExpr->mDotToken->GetToken() == BfToken_QuestionDot);
	bool isCascade = ((memberRefExpr->mDotToken != NULL) && (memberRefExpr->mDotToken->GetToken() == BfToken_DotDot));
	bool isArrowLookup = ((memberRefExpr->mDotToken != NULL) && (memberRefExpr->mDotToken->GetToken() == BfToken_Arrow));

	BfIdentifierNode* nameLeft = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mTarget);
	BfIdentifierNode* nameRight = BfIdentifierCast(memberRefExpr->mMemberName);
	if ((nameLeft != NULL) && (nameRight != NULL) && (!isNullCondLookup) && (!isCascade) && (!isArrowLookup))
	{
		bool hadError = false;
		LookupQualifiedName(memberRefExpr, nameLeft, nameRight, true, &hadError);
		if ((mResult) || (mPropDef != NULL))
			return;
		if (hadError)
			return;

		LookupQualifiedStaticField(memberRefExpr, nameLeft, nameRight, false);

		return;
	}

	BfTypedValue thisValue;
	if (auto exprTarget = BfNodeDynCast<BfExpression>(memberRefExpr->mTarget))
	{
		if (auto typeOfExpr = BfNodeDynCast<BfTypeOfExpression>(memberRefExpr->mTarget))
		{
			if (auto nameIdentifer = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mMemberName))
			{
				if (LookupTypeProp(typeOfExpr, nameIdentifer))
					return;
			}
		}

		//Hm, not using VisitChild broke our ability to write to a field for a not-initialized local struct
		VisitChild(memberRefExpr->mTarget);
		GetResult();
		thisValue = mResult;

		if (!thisValue)
		{
			if (auto targetIdentifier = BfNodeDynCast<BfIdentifierNode>(exprTarget))
			{
				thisValue = BfTypedValue(mModule->ResolveTypeRef(targetIdentifier, NULL, BfPopulateType_Declaration));
			}
		}

		if (!thisValue.HasType())
			return;
		//thisValue = mResult;
	}
	else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberRefExpr->mTarget))
	{
		// Look up static field
		thisValue = BfTypedValue(ResolveTypeRef(typeRef));
	}

	if (nameRefNode == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	if (isNullCondLookup)
		thisValue = SetupNullConditional(thisValue, memberRefExpr->mDotToken);

	if ((isArrowLookup) && (thisValue))
		thisValue = TryArrowLookup(thisValue, memberRefExpr->mDotToken);

	mResult = LookupField(nameRefNode, thisValue, findName);

	if ((!mResult) && (mPropDef == NULL))
	{
		if (thisValue.mType != NULL)
		{
			BfTypeInstance* typeInst = thisValue.mType->ToTypeInstance();
			auto compiler = mModule->mCompiler;
			if ((typeInst != NULL) && (compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(memberRefExpr->mMemberName)))
			{
				FixitAddMember(typeInst, mExpectingType, findName, !thisValue.mValue);
			}
		}

		if ((!thisValue.mValue) && (thisValue.mType != NULL))
		{
			if (auto targetIdentifier = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mMemberName))
			{
				if ((mBfEvalExprFlags & BfEvalExprFlags_NameOf) != 0)
				{
					auto typeInst = thisValue.mType->ToTypeInstance();
					if ((typeInst != NULL) && (CheckForMethodName(nameRight, typeInst, findName)))
						return;
				}

				mResult.mType = mModule->ResolveInnerType(thisValue.mType, targetIdentifier, BfPopulateType_Declaration);
			}
		}

		if ((memberRefExpr->mTarget == NULL) && (expectingTypeInst != NULL) && (autoComplete != NULL))
		{
			if (autoComplete->CheckFixit(memberRefExpr->mMemberName))
			{
				autoComplete->FixitAddCase(expectingTypeInst, memberRefExpr->mMemberName->ToString(), BfTypeVector());
			}
		}

		if (mResult.mType == NULL)
		{
			if (mModule->PreFail())
			{
				if ((thisValue) && (thisValue.mType->IsPointer()) && (thisValue.mType->GetUnderlyingType()->IsObjectOrInterface()))
					mModule->Fail(StrFormat("Members cannot be referenced on type '%s' because the type is a pointer to a reference type (ie: a double-reference).",
						mModule->TypeToString(thisValue.mType).c_str()), nameRefNode);
				else if (thisValue)
					mModule->Fail(StrFormat("Unable to find member '%s' in '%s'", findName.c_str(), mModule->TypeToString(thisValue.mType).c_str()), nameRefNode);
				else
					mModule->Fail("Unable to find member", nameRefNode);
			}
		}
	}

	if ((isNullCondLookup) && (mPropDef == NULL))
		mResult = GetResult();

	if (isCascade)
	{
		if (outCascadeValue != NULL)
			*outCascadeValue = thisValue;
		else if (mModule->PreFail())
			mModule->Fail("Unexpected cascade operation. Chaining can only be used for method invocations", memberRefExpr->mDotToken);
	}
}

void BfExprEvaluator::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	DoMemberReference(memberRefExpr, NULL);
}

void BfExprEvaluator::Visit(BfIndexerExpression* indexerExpr)
{
	BfTypedValue target;
	bool wantStatic = false;
	// Try first as a non-static indexer, then as a static indexer
	for (int pass = 0; pass < 2; pass++)
	{
		///
		{
			SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NoLookupError | BfEvalExprFlags_AllowBase), pass == 0);
			VisitChild(indexerExpr->mTarget);
		}
		ResolveGenericType();
		target = GetResult(true);
		if (target)
			break;

		if (pass == 0)
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, (mModule->mIgnoreErrors) || (pass == 0));
			auto staticType = mModule->ResolveTypeRef(indexerExpr->mTarget, {});
			if (staticType != NULL)
			{
				wantStatic = true;
				target.mType = staticType;
				break;
			}
		}
	}

	if (!target.HasType())
		return;

	if (target.mType->IsGenericParam())
	{
		auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)target.mType);
		if (genericParamInstance->mTypeConstraint != NULL)
			target.mType = genericParamInstance->mTypeConstraint;
	}

	BfCheckedKind checkedKind = BfCheckedKind_NotSet;

	bool isInlined = false;
	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
	{
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mInlineAttributeTypeDef))
		{
			isInlined = true;
			mModule->mAttributeState->mUsed = true;
		}
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mCheckedAttributeTypeDef))
		{
			checkedKind = BfCheckedKind_Checked;
			mModule->mAttributeState->mUsed = true;
		}
		if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mUncheckedAttributeTypeDef))
		{
			checkedKind = BfCheckedKind_Unchecked;
			mModule->mAttributeState->mUsed = true;
		}
	}

	// Avoid attempting to apply the current attributes to the indexer arguments
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, NULL);

	bool isNullCondLookup = (indexerExpr->mOpenBracket != NULL) && (indexerExpr->mOpenBracket->GetToken() == BfToken_QuestionLBracket);
	if (isNullCondLookup)
		target = SetupNullConditional(target, indexerExpr->mOpenBracket);

	if (target.mType->IsVar())
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(target.mType), target.mType, true);
		return;
	}

	if (target.mType->IsTypeInstance())
	{
		mIndexerValues.clear();

		SizedArray<BfExpression*, 2> argExprs;
		BfSizedArray<BfExpression*> sizedArgExprs(indexerExpr->mArguments);
		BfResolvedArgs argValues(&sizedArgExprs);
		ResolveArgValues(argValues, (BfResolveArgsFlags)(BfResolveArgsFlag_DeferParamEval | BfResolveArgsFlag_FromIndexer));

		mIndexerValues = argValues.mResolvedArgs;

		BfMethodMatcher methodMatcher(indexerExpr->mTarget, mModule, "[]", mIndexerValues, BfMethodGenericArguments());
		methodMatcher.mCheckedKind = checkedKind;

		BfMethodDef* methodDef = NULL;

		auto startCheckTypeInst = target.mType->ToTypeInstance();

		for (int pass = 0; pass < 2; pass++)
		{
			bool isFailurePass = pass == 1;

			BfPropertyDef* foundProp = NULL;
			BfTypeInstance* foundPropTypeInst = NULL;

			auto curCheckType = startCheckTypeInst;
			while (curCheckType != NULL)
			{
				BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;

				curCheckType->mTypeDef->PopulateMemberSets();

				BfMemberSetEntry* entry;
				BfPropertyDef* matchedProp = NULL;
				BfPropertyDef* nextProp = NULL;
				if (curCheckType->mTypeDef->mPropertySet.TryGetWith(String("[]"), &entry))
					nextProp = (BfPropertyDef*)entry->mMemberDef;

				while (nextProp != NULL)
				{
					auto prop = nextProp;
					nextProp = nextProp->mNextWithSameName;

					//TODO: Match against setMethod (minus last param) if we have no 'get' method
					for (auto checkMethod : prop->mMethods)
					{
						if (checkMethod->mMethodType != BfMethodType_PropertyGetter)
							continue;

						// For generic params - check interface constraints for an indexer, call that method
						BF_ASSERT(!target.mType->IsGenericParam());

						if (checkMethod->mIsStatic != wantStatic)
							continue;

						if (checkMethod->mExplicitInterface != NULL)
							continue;

						auto autoComplete = GetAutoComplete();
						bool wasCapturingMethodMatchInfo = false;
						if (autoComplete != NULL)
						{
							// Set to false to make sure we don't capture method match info from 'params' array creation
							wasCapturingMethodMatchInfo = autoComplete->mIsCapturingMethodMatchInfo;
							autoComplete->mIsCapturingMethodMatchInfo = false;
						}

						defer
						(
							if (autoComplete != NULL)
								autoComplete->mIsCapturingMethodMatchInfo = wasCapturingMethodMatchInfo;
						);

						if ((!isFailurePass) && (!methodMatcher.WantsCheckMethod(protectionCheckFlags, startCheckTypeInst, curCheckType, checkMethod)))
							continue;

						if (!methodMatcher.IsMemberAccessible(curCheckType, checkMethod->mDeclaringType))
							continue;

						methodMatcher.mCheckedKind = checkedKind;
						methodMatcher.mTarget = target;
						bool hadMatch = methodMatcher.CheckMethod(startCheckTypeInst, curCheckType, checkMethod, false);

						if ((hadMatch) || (methodMatcher.mBackupMethodDef == checkMethod))
						{
							foundPropTypeInst = curCheckType;
							foundProp = prop;
						}
					}
				}

				curCheckType = curCheckType->mBaseType;
			}

			if (foundProp != NULL)
			{
				mPropSrc = indexerExpr->mOpenBracket;
				mPropDef = foundProp;
				if (foundProp->mIsStatic)
				{
					mPropTarget = BfTypedValue(foundPropTypeInst);
				}
				else
				{
					if (target.mType != foundPropTypeInst)
						mPropTarget = mModule->Cast(indexerExpr->mTarget, target, foundPropTypeInst);
					else
						mPropTarget = target;
				}
				mOrigPropTarget = mPropTarget;
				if (isInlined)
					mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_ForceInline);
				mPropCheckedKind = checkedKind;

				if ((target.IsBase()) && (mPropDef->IsVirtual()))
					mPropDefBypassVirtual = true;

				return;
			}
		}

		mModule->Fail("Unable to find indexer property", indexerExpr->mTarget);
		return;
	}

	bool wantsChecks = checkedKind == BfCheckedKind_Checked;
	if (checkedKind == BfCheckedKind_NotSet)
		wantsChecks = mModule->GetDefaultCheckedKind() == BfCheckedKind_Checked;

	//target.mType = mModule->ResolveGenericType(target.mType);
	if (target.mType->IsVar())
	{
		mResult = target;
		return;
	}

	if ((!target.mType->IsPointer()) && (!target.mType->IsSizedArray()))
	{
		mModule->Fail("Expected pointer or array type", indexerExpr->mTarget);
		return;
	}

	auto _GetDefaultResult = [&]()
	{
		return mModule->GetDefaultTypedValue(target.mType->GetUnderlyingType(), false, BfDefaultValueKind_Addr);
	};

	if (indexerExpr->mArguments.size() != 1)
	{
		mModule->Fail("Expected single index", indexerExpr->mOpenBracket);
		mResult = _GetDefaultResult();
		return;
	}

	if (indexerExpr->mArguments[0] == NULL)
	{
		mModule->AssertErrorState();
		mResult = _GetDefaultResult();
		return;
	}

	bool isUndefIndex = false;

	auto indexArgument = mModule->CreateValueFromExpression(indexerExpr->mArguments[0], mModule->GetPrimitiveType(BfTypeCode_IntPtr), BfEvalExprFlags_NoCast);
	if (!indexArgument)
		return;
	if (!indexArgument.mType->IsIntegral())
	{
		if (indexArgument.mType->IsVar())
		{
			isUndefIndex = true;
			indexArgument = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_IntPtr), false, BfDefaultValueKind_Undef);
		}
		else
		{
			indexArgument = mModule->Cast(indexerExpr->mArguments[0], indexArgument, mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			if (!indexArgument)
				indexArgument = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_IntPtr));
		}
	}

	if (indexArgument.mType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)indexArgument.mType;
		if ((!primType->IsSigned()) && (primType->mSize < 8))
		{
			// GEP will always do a signed upcast so we need to cast manually if we are unsigned
			indexArgument = BfTypedValue(mModule->mBfIRBuilder->CreateNumericCast(indexArgument.mValue, false, BfTypeCode_IntPtr), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
		}
	}

	mModule->PopulateType(target.mType);
	if (target.mType->IsSizedArray())
	{
		BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)target.mType;
		auto underlyingType = sizedArrayType->mElementType;
		if (indexArgument.mValue.IsConst())
		{
			auto indexConst = mModule->mBfIRBuilder->GetConstant(indexArgument.mValue);
			if (indexConst->mUInt64 >= (uint64)sizedArrayType->mElementCount)
			{
				if ((!mModule->IsInSpecializedSection()) && (checkedKind != BfCheckedKind_Unchecked))
				{
					mModule->Fail(StrFormat("Index '%d' is out of bounds for type '%s'", indexConst->mInt32, mModule->TypeToString(target.mType).c_str()), indexerExpr->mArguments[0]);
					mResult = _GetDefaultResult();
					return;
				}
				else
				{
					// Is this any good?
					mModule->mBfIRBuilder->CreateUnreachable();
				}
			}
		}
		else if (((mModule->HasExecutedOutput()) || (mModule->mIsComptimeModule)) &&
			(wantsChecks))
		{
			if (checkedKind == BfCheckedKind_NotSet)
				checkedKind = mModule->GetDefaultCheckedKind();
			if (checkedKind == BfCheckedKind_Checked)
			{
				auto oobBlock = mModule->mBfIRBuilder->CreateBlock("oob", true);
				auto contBlock = mModule->mBfIRBuilder->CreateBlock("cont", true);

				auto indexType = (BfPrimitiveType*)indexArgument.mType;

				if (!mModule->mSystem->DoesLiteralFit(indexType->mTypeDef->mTypeCode, (int64)sizedArrayType->mElementCount))
				{
					// We need to upsize the index so we can compare it against the larger elementCount
					indexType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
					indexArgument = mModule->Cast(indexerExpr->mArguments[0], indexArgument, indexType);
				}

				auto cmpRes = mModule->mBfIRBuilder->CreateCmpGTE(indexArgument.mValue, mModule->mBfIRBuilder->CreateConst(indexType->mTypeDef->mTypeCode, (uint64)sizedArrayType->mElementCount), false);
				mModule->mBfIRBuilder->CreateCondBr(cmpRes, oobBlock, contBlock);

				mModule->mBfIRBuilder->SetInsertPoint(oobBlock);
				auto internalType = mModule->ResolveTypeDef(mModule->mCompiler->mInternalTypeDef);
				auto oobFunc = mModule->GetMethodByName(internalType->ToTypeInstance(), "ThrowIndexOutOfRange");
				if (oobFunc.mFunc)
				{
					if (mModule->mIsComptimeModule)
						mModule->mCompiler->mCeMachine->QueueMethod(oobFunc.mMethodInstance, oobFunc.mFunc);

					SizedArray<BfIRValue, 1> args;
					args.push_back(mModule->GetConstValue(0));
					mModule->mBfIRBuilder->CreateCall(oobFunc.mFunc, args);
					mModule->mBfIRBuilder->CreateUnreachable();
				}
				else
				{
					mModule->Fail("System.Internal class must contain method 'ThrowIndexOutOfRange'");
				}

				mModule->mBfIRBuilder->SetInsertPoint(contBlock);
			}
		}

		// If this is a 'bag of bytes', we should try hard not to have to make this addressable
		if ((!target.IsAddr()) && (!target.mType->IsSizeAligned()))
			mModule->MakeAddressable(target);

		mModule->PopulateType(underlyingType);
		if ((sizedArrayType->IsUndefSizedArray()) || (isUndefIndex))
		{
			mResult = mModule->GetDefaultTypedValue(underlyingType, false, BfDefaultValueKind_Addr);
		}
		else if (sizedArrayType->IsValuelessType())
		{
			if (underlyingType->IsValuelessType())
            	mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), underlyingType, true);
			else
			{
				mResult = mModule->GetDefaultTypedValue(underlyingType, false, BfDefaultValueKind_Addr);
			}
		}
		else if (target.IsAddr())
		{
			if (target.mType->IsSizeAligned())
			{
				auto gepResult = mModule->mBfIRBuilder->CreateInBoundsGEP(target.mValue, mModule->GetConstValue(0), indexArgument.mValue);
				mResult = BfTypedValue(gepResult, underlyingType, target.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
			}
			else
			{
				auto indexResult = mModule->CreateIndexedValue(underlyingType, target.mValue, indexArgument.mValue);
				mResult = BfTypedValue(indexResult, underlyingType, target.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
			}
		}
		else
		{
			if ((!target.mValue.IsConst()) && (!indexArgument.mValue.IsConst()))
			{
				mModule->Fail("Unable to index value", indexerExpr->mTarget);
				return;
			}

			mModule->mBfIRBuilder->PopulateType(target.mType);
			auto gepResult = mModule->mBfIRBuilder->CreateExtractValue(target.mValue, indexArgument.mValue);

			if ((underlyingType->IsString()) || (underlyingType->IsPointer()))
			{
				auto resultConst = mModule->mBfIRBuilder->GetConstant(gepResult);
				if ((resultConst != NULL) && (resultConst->mTypeCode == BfTypeCode_Int32))
				{
					int strId = resultConst->mInt32;
					const StringImpl& str = mModule->mContext->mStringObjectIdMap[strId].mString;

					if (underlyingType->IsString())
						gepResult = mModule->GetStringObjectValue(str, false);
					else
						gepResult = mModule->GetStringCharPtr(strId);
				}
			}

			mResult = BfTypedValue(gepResult, underlyingType, BfTypedValueKind_Value);
		}
	}
	else
	{
		target = mModule->LoadValue(target);
		BfPointerType* pointerType = (BfPointerType*)target.mType;
		auto underlyingType = pointerType->mElementType;
		mModule->mBfIRBuilder->PopulateType(underlyingType);

		if (isUndefIndex)
		{
			mResult = mModule->GetDefaultTypedValue(underlyingType, false, BfDefaultValueKind_Addr);
		}
		else
		{
			BfIRValue result = mModule->CreateIndexedValue(underlyingType, target.mValue, indexArgument.mValue);
			mResult = BfTypedValue(result, underlyingType, true);
		}
	}
}

void BfExprEvaluator::Visit(BfUnaryOperatorExpression* unaryOpExpr)
{
	BfAutoParentNodeEntry autoParentNodeEntry(mModule, unaryOpExpr);
	PerformUnaryOperation(unaryOpExpr->mExpression, unaryOpExpr->mOp, unaryOpExpr->mOpToken, BfUnaryOpFlag_None);
}

void BfExprEvaluator::PerformUnaryOperation(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags)
{
	if ((unaryOpExpr == NULL) && (unaryOp == BfUnaryOp_PartialRangeThrough))
	{
		PerformBinaryOperation(NULL, NULL, BfBinaryOp_ClosedRange, opToken, BfBinOpFlag_None);
		return;
	}

	///
	{
		// If this is a cast, we don't want the value to be coerced before the unary operator is applied.
		// WAIT: Why not?
		//SetAndRestoreValue<BfType*> prevExpectingType(mExpectingType, NULL);

		BfType* prevExpedcting = mExpectingType;
		switch (unaryOp)
		{
		case BfUnaryOp_Negate:
		case BfUnaryOp_Positive:
		case BfUnaryOp_InvertBits:
			// If we're expecting an int64 or uint64 then just leave the type as unknown
			if ((mExpectingType != NULL) && (mExpectingType->IsInteger()) && (mExpectingType->mSize == 8))
				mExpectingType = NULL;

			// Otherwise keep expecting type
			break;
		default:
			mExpectingType = NULL;
		}
		VisitChild(unaryOpExpr);
		mExpectingType = prevExpedcting;
	}

	BfExprEvaluator::PerformUnaryOperation_OnResult(unaryOpExpr, unaryOp, opToken, opFlags);
}

BfTypedValue BfExprEvaluator::PerformUnaryOperation_TryOperator(const BfTypedValue& inValue, BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags)
{
	if ((!inValue.mType->IsTypeInstance()) && (!inValue.mType->IsGenericParam()))
		return BfTypedValue();

	SizedArray<BfResolvedArg, 1> args;
	BfResolvedArg resolvedArg;
	resolvedArg.mTypedValue = inValue;
	args.push_back(resolvedArg);
	BfMethodMatcher methodMatcher(opToken, mModule, "", args, BfMethodGenericArguments());
	methodMatcher.mBfEvalExprFlags = BfEvalExprFlags_NoAutoComplete;
	methodMatcher.mAllowImplicitRef = true;
	BfBaseClassWalker baseClassWalker(inValue.mType, NULL, mModule);

	BfUnaryOp findOp = unaryOp;
	bool isPostOp = false;

	if (findOp == BfUnaryOp_PostIncrement)
	{
		findOp = BfUnaryOp_Increment;
		isPostOp = true;
	}

	if (findOp == BfUnaryOp_PostDecrement)
	{
		findOp = BfUnaryOp_Decrement;
		isPostOp = true;
	}

	bool isConstraintCheck = ((opFlags & BfUnaryOpFlag_IsConstraintCheck) != 0);

	BfType* operatorConstraintReturnType = NULL;
	BfType* bestSelfType = NULL;
	while (true)
	{
		auto entry = baseClassWalker.Next();
		auto checkType = entry.mTypeInstance;
		if (checkType == NULL)
			break;
		for (auto operatorDef : checkType->mTypeDef->mOperators)
		{
			if (operatorDef->mOperatorDeclaration->mUnaryOp == findOp)
			{
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
					auto returnType = mModule->CheckOperator(checkType, operatorDef, inValue, BfTypedValue());
					if (returnType != NULL)
					{
						operatorConstraintReturnType = returnType;
						methodMatcher.mBestMethodDef = operatorDef;
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

	methodMatcher.FlushAmbiguityError();

	if (methodMatcher.mBestMethodDef == NULL)
	{
		// Check method generic constraints
		if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
		{
			for (int genericParamIdx = 0; genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
				for (auto& opConstraint : genericParam->mOperatorConstraints)
				{
					if (opConstraint.mUnaryOp == findOp)
					{
						if (mModule->CanCast(args[0].mTypedValue, opConstraint.mRightType, isConstraintCheck ? BfCastFlags_IsConstraintCheck : BfCastFlags_None))
						{
							return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), genericParam->mExternType);
						}
					}
				}
			}
		}

		// Check type generic constraints
		if ((mModule->mCurTypeInstance->IsGenericTypeInstance()) && (mModule->mCurTypeInstance->IsUnspecializedType()))
		{
			auto genericTypeInst = (BfTypeInstance*)mModule->mCurTypeInstance;
			for (int genericParamIdx = 0; genericParamIdx < genericTypeInst->mGenericTypeInfo->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParam = mModule->GetGenericTypeParamInstance(genericParamIdx);
				for (auto& opConstraint : genericParam->mOperatorConstraints)
				{
					if (opConstraint.mUnaryOp == findOp)
					{
						if (mModule->CanCast(args[0].mTypedValue, opConstraint.mRightType, isConstraintCheck ? BfCastFlags_IsConstraintCheck : BfCastFlags_None))
						{
							return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), genericParam->mExternType);
						}
					}
				}
			}
		}

		return BfTypedValue();
	}

	if ((!baseClassWalker.mMayBeFromInterface) && (opToken != NULL))
		mModule->SetElementType(opToken, BfSourceElementType_Method);

	auto methodDef = methodMatcher.mBestMethodDef;
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(opToken)))
	{
		auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDef->mMethodDeclaration);
		if ((operatorDecl != NULL) && (operatorDecl->mOpTypeToken != NULL))
			autoComplete->SetDefinitionLocation(operatorDecl->mOpTypeToken);
	}

	SizedArray<BfExpression*, 2> argSrcs;
	argSrcs.push_back(unaryOpExpr);

	BfTypedValue targetVal = args[0].mTypedValue;
	BfTypedValue postOpVal;
	if (isPostOp)
		postOpVal = mModule->LoadValue(targetVal);

	BfTypedValue callTarget;
	if (!methodMatcher.mBestMethodDef->mIsStatic)
	{
		callTarget = targetVal;
		args.Clear();
	}

	BfTypedValue result;
	if (isConstraintCheck)
	{
		result = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), operatorConstraintReturnType);
	}
	else
	{
		SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NoAutoComplete));
		result = CreateCall(&methodMatcher, callTarget);
	}

	if (!methodMatcher.mBestMethodDef->mIsStatic)
	{
		if (!isPostOp)
			result = mModule->LoadValue(targetVal);
	}
	else if ((result.mType != NULL) && (methodMatcher.mSelfType != NULL) && (result.mType->IsSelf()))
	{
		BF_ASSERT(mModule->IsInGeneric());
		result = mModule->GetDefaultTypedValue(methodMatcher.mSelfType);
	}

	if ((methodMatcher.mBestMethodInstance) &&
		((findOp == BfUnaryOp_Increment) || (findOp == BfUnaryOp_Decrement)))
	{
		if (methodMatcher.mBestMethodInstance.mMethodInstance->mIsIntrinsic)
		{
			if (args[0].mTypedValue.IsAddr())
				mModule->mBfIRBuilder->CreateStore(result.mValue, args[0].mTypedValue.mValue);
			else
			{
				mModule->AssertErrorState();
			}
		}
		else
		{
			if (!result.mType->IsValuelessType())
			{
				if (targetVal.IsAddr())
				{
					result = mModule->LoadValue(result);
					mModule->mBfIRBuilder->CreateStore(result.mValue, targetVal.mValue);
				}
			}
		}
	}

	if (postOpVal)
		result = postOpVal;
	return result;
}

void BfExprEvaluator::PerformUnaryOperation_OnResult(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken, BfUnaryOpFlags opFlags)
{
	BfAstNode* propSrc = mPropSrc;
	BfTypedValue origPropTarget = mOrigPropTarget;
	BfTypedValue propTarget = mPropTarget;
	BfPropertyDef* propDef = mPropDef;
	SizedArray<BfResolvedArg, 2> indexerVals = mIndexerValues;
	BfTypedValue writeToProp;

	GetResult();
	if (!mResult)
		return;

	mResult = mModule->RemoveRef(mResult);

	if (mResult.mType->IsVar())
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(mResult.mType), mResult.mType);
		return;
	}

	if (BfCanOverloadOperator(unaryOp))
	{
		auto opResult = PerformUnaryOperation_TryOperator(mResult, unaryOpExpr, unaryOp, opToken, opFlags);
		if (opResult)
		{
			mResult = opResult;
			return;
		}
	}

	switch (unaryOp)
	{
	case BfUnaryOp_PostIncrement:
	case BfUnaryOp_Increment:
	case BfUnaryOp_PostDecrement:
	case BfUnaryOp_Decrement:
		{
			if (mResult.mKind == BfTypedValueKind_CopyOnMutateAddr)
			{
				// Support this ops on direct auto-property access without a copy
				mResult.mKind = BfTypedValueKind_Addr;
			}
		}
		break;
	}

	bool numericFail = false;
	switch (unaryOp)
	{
	case BfUnaryOp_Not:
		{
			CheckResultForReading(mResult);
			auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
			auto value = mModule->LoadValue(mResult);
			value = mModule->Cast(unaryOpExpr, value, boolType);
			if (!value)
			{
				mResult = BfTypedValue();
				return;
			}
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateNot(value.mValue), boolType);
		}
		break;
	case BfUnaryOp_Positive:
		return;
	case BfUnaryOp_Negate:
		{
			CheckResultForReading(mResult);
			auto value = mModule->LoadValue(mResult);
			if (!value)
			{
				mResult = BfTypedValue();
				return;
			}

			BfType* origType = value.mType;
			if (value.mType->IsTypedPrimitive())
				value.mType = value.mType->GetUnderlyingType();

			if (value.mType->IsIntegral())
			{
				auto primType = (BfPrimitiveType*)value.mType;
				auto wantType = primType;

				auto constant = mModule->mBfIRBuilder->GetConstant(value.mValue);
				if ((constant != NULL) && (mModule->mBfIRBuilder->IsInt(constant->mTypeCode)))
				{
					if ((primType->mTypeDef->mTypeCode == BfTypeCode_UInt32) && (constant->mInt64 == 0x80000000LL))
					{
						mResult = BfTypedValue(mModule->GetConstValue32(-0x80000000LL), mModule->GetPrimitiveType(BfTypeCode_Int32));
						return;
					}
					else if ((primType->mTypeDef->mTypeCode == BfTypeCode_UInt64) && (constant->mInt64 == 0x8000000000000000LL))
					{
						mResult = BfTypedValue(mModule->GetConstValue64(-0x8000000000000000LL), mModule->GetPrimitiveType(BfTypeCode_Int64));
						return;
					}
				}

				/*if (auto constantInt = dyn_cast<ConstantInt>((Value*)value.mValue))
				{
					int64 i64Val = constantInt->getSExtValue();
					// This is a special case where the user entered -0x80000000 (maxint) but we thought "0x80000000" was a uint in the parser
					//  which would get upcasted to an int64 for this negate.  Properly bring back down to an int32
					if ((primType->mTypeDef->mTypeCode == BfTypeCode_UInt32) && (i64Val == -0x80000000LL))
					{
						mResult = BfTypedValue(mModule->GetConstValue((int)i64Val), mModule->GetPrimitiveType(BfTypeCode_Int32));
						return;
					}
				}*/

				if (!primType->IsSigned())
				{
					if (primType->mSize == 1)
						wantType = mModule->GetPrimitiveType(BfTypeCode_Int16);
					else if (primType->mSize == 2)
						wantType = mModule->GetPrimitiveType(BfTypeCode_Int32);
					else if (primType->mSize == 4)
						wantType = mModule->GetPrimitiveType(BfTypeCode_Int64);
					else
						mModule->Fail("Operator '-' cannot be applied to uint64", opToken);
				}

				if (primType != wantType)
				{
					value = mModule->Cast(unaryOpExpr, value, wantType, BfCastFlags_Explicit);
					if (!value)
					{
						mResult = BfTypedValue();
						return;
					}
				}

				if (origType->mSize == wantType->mSize) // Allow negative of primitive typed but not if we had to upsize
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateNeg(value.mValue), origType);
				else
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateNeg(value.mValue), wantType);
			}
			else if (value.mType->IsFloat())
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateNeg(value.mValue), origType);
			else
				numericFail = true;
		}
		break;
	case BfUnaryOp_InvertBits:
		{
			CheckResultForReading(mResult);
			auto value = mModule->LoadValue(mResult);
			if (!value)
				return;
			bool isInteger = value.mType->IsIntegral();
			if (value.mType->IsTypedPrimitive())
				isInteger = value.mType->GetUnderlyingType()->IsIntegral();
			if (!isInteger)
			{
				mResult = BfTypedValue();
				mModule->Fail("Operator can only be used on integer types", opToken);
				return;
			}
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateNot(value.mValue), value.mType);
		}
		break;
	case BfUnaryOp_AddressOf:
		{
			MarkResultUsed();

			mModule->FixIntUnknown(mResult);
			mModule->PopulateType(mResult.mType);
			auto ptrType = mModule->CreatePointerType(mResult.mType);
			if (mResult.mType->IsValuelessType())
			{
				if (!mModule->IsInSpecializedSection())
				{
					mModule->Warn(0, StrFormat("Operator '&' results in a sentinel address for zero-sized type '%s'",
						mModule->TypeToString(mResult.mType).c_str()), opToken);
				}

				// Sentinel value
				auto val = mModule->mBfIRBuilder->CreateIntToPtr(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1), mModule->mBfIRBuilder->MapType(ptrType));
				mResult = BfTypedValue(val, ptrType);
			}
			else if (!CheckModifyResult(mResult, unaryOpExpr, "take address of", false, true))
			{
				if (!mResult.IsAddr())
					mResult = mModule->MakeAddressable(mResult, false, true);
				mResult = BfTypedValue(mResult.mValue, ptrType, false);
			}
			else
				mResult = BfTypedValue(mResult.mValue, ptrType, false);
		}
		break;
	case BfUnaryOp_Dereference:
		{
			CheckResultForReading(mResult);
			if (!mResult.mType->IsPointer())
			{
				mResult = BfTypedValue();
				mModule->Fail("Cannot dereference non-pointer type", unaryOpExpr);
				return;
			}

			if (mResult.mValue.IsConst())
			{
				auto constant = mModule->mBfIRBuilder->GetConstant(mResult.mValue);
				bool isNull = constant->mTypeCode == BfTypeCode_NullPtr;

				if (constant->mConstType == BfConstType_ExtractValue)
				{
					auto constExtract = (BfConstantExtractValue*)constant;
					auto targetConst = mModule->mBfIRBuilder->GetConstantById(constExtract->mTarget);
					if (targetConst->mConstType == BfConstType_AggZero)
						isNull = true;
				}

				if (isNull)
				{
					mModule->Warn(0, "Cannot dereference a null pointer", unaryOpExpr);
					mResult = mModule->GetDefaultTypedValue(mResult.mType, false, BfDefaultValueKind_Addr);
					mResult = mModule->LoadValue(mResult);
				}
			}

			auto derefTarget = mModule->LoadValue(mResult);

			BfPointerType* pointerType = (BfPointerType*)derefTarget.mType;
			auto resolvedType = pointerType->mElementType;
			mModule->PopulateType(resolvedType);
			if (resolvedType->IsValuelessType())
				mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), resolvedType, true);
			else
				mResult = BfTypedValue(derefTarget.mValue, resolvedType, true);
		}
		break;
	case BfUnaryOp_PostIncrement:
	case BfUnaryOp_Increment:
		{
			CheckResultForReading(mResult);
			auto ptr = mResult;
			//if ((propDef == NULL) && (!mModule->CheckModifyValue(ptr, opToken)))
			if ((propDef == NULL) && (!CheckModifyResult(ptr, opToken, "increment")))
				return;
			BfTypedValue origTypedVal = mModule->LoadValue(ptr, NULL, mIsVolatileReference);
			BfIRValue origVal = origTypedVal.mValue;
			BfIRValue constValue = mModule->GetConstValue(1, ptr.mType);
			BfIRValue resultValue;

			if (ptr.mType->IsPointer())
			{
				BfPointerType* ptrType = (BfPointerType*)ptr.mType;
				BfType* intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				constValue = mModule->GetConstValue(ptrType->mElementType->GetStride(), intPtrType);

				auto i8PtrType = mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8));
				BfIRValue origPtrValue = mModule->mBfIRBuilder->CreateBitCast(origVal, i8PtrType);
				BfIRValue newPtrValue = mModule->mBfIRBuilder->CreateInBoundsGEP(origPtrValue, constValue);
				resultValue = mModule->mBfIRBuilder->CreateBitCast(newPtrValue, mModule->mBfIRBuilder->MapType(ptr.mType));
			}
			else
			{
				constValue = mModule->GetConstValue(1, ptr.mType);
				if (!constValue)
				{
					numericFail = true;
					break;
				}
				if ((ptr.mType->IsIntegral()) || (ptr.mType->IsEnum()) || (ptr.mType->IsFloat()))
				{
					resultValue = mModule->mBfIRBuilder->CreateAdd(origVal, constValue/*, "inc"*/);
				}
				else
				{
					numericFail = true;
					break;
				}
			}
			if ((propDef != NULL) && (!ptr.IsAddr()))
				writeToProp = BfTypedValue(resultValue, ptr.mType);
			else
				mModule->mBfIRBuilder->CreateAlignedStore(resultValue, ptr.mValue, ptr.mType->mAlign, mIsVolatileReference);
			if (unaryOp == BfUnaryOp_PostIncrement)
				mResult = BfTypedValue(origVal, ptr.mType, false);
			else
				mResult = BfTypedValue(resultValue, ptr.mType, false);
		}
		break;
	case BfUnaryOp_PostDecrement:
	case BfUnaryOp_Decrement:
		{
			CheckResultForReading(mResult);
			auto ptr = mResult;
			//if ((propDef == NULL) && (!mModule->CheckModifyValue(ptr, opToken)))
				//return;
			if ((propDef == NULL) && (!CheckModifyResult(ptr, opToken, "decrement")))
				return;
			BfTypedValue origTypedVal = mModule->LoadValue(ptr, NULL, mIsVolatileReference);
			BfIRValue origVal = origTypedVal.mValue;
			BfIRValue constValue = mModule->GetConstValue(1, ptr.mType);
			BfIRValue resultValue;

			if (ptr.mType->IsPointer())
			{
				BfPointerType* ptrType = (BfPointerType*)ptr.mType;
				BfType* intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
				constValue = mModule->GetConstValue(-ptrType->mElementType->GetStride(), intPtrType);

				auto i8PtrType = mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8));
				BfIRValue origPtrValue = mModule->mBfIRBuilder->CreateBitCast(origVal, i8PtrType);
				BfIRValue newPtrValue = mModule->mBfIRBuilder->CreateInBoundsGEP(origPtrValue, constValue);
				resultValue = mModule->mBfIRBuilder->CreateBitCast(newPtrValue, mModule->mBfIRBuilder->MapType(ptr.mType));
			}
			else
			{
				BfIRValue constValue = mModule->GetConstValue(1, ptr.mType);
				if (!constValue)
				{
					numericFail = true;
					break;
				}
				if ((ptr.mType->IsIntegral()) || (ptr.mType->IsEnum()))
				{
					resultValue = mModule->mBfIRBuilder->CreateSub(origVal, constValue);
				}
				else if (ptr.mType->IsFloat())
				{
					resultValue = mModule->mBfIRBuilder->CreateSub(origVal, constValue);
				}
				else
				{
					numericFail = true;
					break;
				}
			}
			if ((propDef != NULL) && (!ptr.IsAddr()))
				writeToProp = BfTypedValue(resultValue, ptr.mType);
			else
				mModule->mBfIRBuilder->CreateAlignedStore(resultValue, ptr.mValue, ptr.mType->mAlign, mIsVolatileReference);
			if (unaryOp == BfUnaryOp_PostDecrement)
				mResult = BfTypedValue(origVal, ptr.mType, false);
			else
				mResult = BfTypedValue(resultValue, ptr.mType, false);
		}
		break;
	case BfUnaryOp_Ref:
	case BfUnaryOp_Mut:
		{
			if (mAllowReadOnlyReference)
			{
				if (mResult.mKind == BfTypedValueKind_ReadOnlyAddr)
					mResult.mKind = BfTypedValueKind_Addr;
			}

			CheckResultForReading(mResult);

			if ((unaryOp == BfUnaryOp_Mut) && (!mResult.mType->IsValueType()) && (!mResult.mType->IsGenericParam()))
			{
				// Non-valuetypes types are already mutable, leave them alone...
				break;
			}

			if ((unaryOp != BfUnaryOp_Mut) || (mResult.mKind != BfTypedValueKind_MutableValue))
			{
				if (!CheckModifyResult(mResult, unaryOpExpr, StrFormat("use '%s' on", BfGetOpName(unaryOp)).c_str()))
				{
					// Just leave the non-ref version in mResult
 					return;
				}
			}

			if ((mBfEvalExprFlags & BfEvalExprFlags_AllowRefExpr) == 0)
			{
				mResult = BfTypedValue();
				mModule->Fail(StrFormat("Invalid usage of '%s' expression", BfGetOpName(unaryOp)), opToken);
				return;
			}

			ResolveGenericType();
			if (mResult.mType->IsVar())
				break;
			mResult = BfTypedValue(mResult.mValue, mModule->CreateRefType(mResult.mType, (unaryOp == BfUnaryOp_Ref) ? BfRefType::RefKind_Ref : BfRefType::RefKind_Mut));
		}
		break;
	case BfUnaryOp_Out:
		{
			if (!CheckModifyResult(mResult, unaryOpExpr, "use 'out' on"))
			{
				// Just leave the non-ref version in mResult
				return;
			}

			if ((mBfEvalExprFlags & BfEvalExprFlags_AllowOutExpr) == 0)
			{
				mModule->Fail("Invalid usage of 'out' expression", opToken);
				return;
			}

			if (mInsidePendingNullable)
			{
				// 'out' inside null conditionals never actually causes a definite assignment...
			}
			else
				MarkResultAssigned();

			MarkResultUsed();
			ResolveGenericType();
			if (mResult.mType->IsVar())
				break;
			mResult = BfTypedValue(mResult.mValue, mModule->CreateRefType(mResult.mType, BfRefType::RefKind_Out));
		}
		break;
	case BfUnaryOp_Params:
		{
			bool allowParams = (mBfEvalExprFlags & BfEvalExprFlags_AllowParamsExpr) != 0;
 			if (allowParams)
			{
				if ((mResultLocalVar != NULL) && (mResultLocalVar->mCompositeCount >= 0)) // Delegate params
				{
					allowParams = true;
				}
				else
				{
					auto isValid = false;
					auto genericTypeInst = mResult.mType->ToGenericTypeInstance();
					if ((genericTypeInst != NULL) && (genericTypeInst->IsInstanceOf(mModule->mCompiler->mSpanTypeDef)))
						isValid = true;
					else if ((mResult.mType->IsArray()) || (mResult.mType->IsSizedArray()))
						isValid = true;
					if (!isValid)
					{
						mModule->Fail(StrFormat("A 'params' expression cannot be used on type '%s'", mModule->TypeToString(mResult.mType).c_str()), opToken);
					}
				}
			}

 			if (allowParams)
 			{
 				mResult = mModule->LoadValue(mResult);
				if (mResult.IsSplat())
 					mResult.mKind = BfTypedValueKind_ParamsSplat;
				else
					mResult.mKind = BfTypedValueKind_Params;
 			}
 			else
			{
				mModule->Fail("Illegal use of 'params' expression", opToken);
			}
		}
		break;
	case BfUnaryOp_Cascade:
		{
			mModule->Fail("Illegal use of argument cascade expression", opToken);
		}
		break;
	case BfUnaryOp_FromEnd:
		{
			CheckResultForReading(mResult);
			auto value = mModule->Cast(unaryOpExpr, mResult, mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			value = mModule->LoadValue(value);
			if (value)
			{
				auto indexType = mModule->ResolveTypeDef(mModule->mCompiler->mIndexTypeDef);
				auto alloca = mModule->CreateAlloca(indexType);
				mModule->mBfIRBuilder->CreateStore(value.mValue, mModule->mBfIRBuilder->CreateInBoundsGEP(mModule->mBfIRBuilder->CreateInBoundsGEP(alloca, 0, 1), 0, 1));
				mModule->mBfIRBuilder->CreateStore(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int8, 1), mModule->mBfIRBuilder->CreateInBoundsGEP(alloca, 0, 2));
				mResult = BfTypedValue(alloca, indexType, BfTypedValueKind_Addr);
			}
		}
		break;
	case BfUnaryOp_PartialRangeUpTo:
		PerformBinaryOperation(NULL, unaryOpExpr, BfBinaryOp_Range, opToken, BfBinOpFlag_None);
		break;
	case BfUnaryOp_PartialRangeThrough:
		PerformBinaryOperation(NULL, unaryOpExpr, BfBinaryOp_ClosedRange, opToken, BfBinOpFlag_None);
		break;
	case BfUnaryOp_PartialRangeFrom:
		PerformBinaryOperation(unaryOpExpr, NULL, BfBinaryOp_ClosedRange, opToken, BfBinOpFlag_None);
		break;
	default:
		mModule->Fail(StrFormat("Illegal use of '%s' unary operator", BfGetOpName(unaryOp)), unaryOpExpr);
		break;
	}

	if (numericFail)
	{
		if (opToken == NULL)
		{
			BF_ASSERT(mModule->mBfIRBuilder->mIgnoreWrites);
		}
		else if ((mResult.mType != NULL) && (mResult.mType->IsInterface()))
		{
			mModule->Fail(
				StrFormat("Operator '%s' cannot be used on interface '%s'. Consider rewriting using generics and use this interface as a generic constraint.",
					BfTokenToString(opToken->mToken), mModule->TypeToString(mResult.mType).c_str()), opToken);
		}
		else
		{
			mModule->Fail(
				StrFormat("Operator '%s' cannot be used because type '%s' is neither a numeric type nor does it define an applicable operator overload",
					BfTokenToString(opToken->mToken), mModule->TypeToString(mResult.mType).c_str()), opToken);
		}
		mResult = BfTypedValue();
	}

	if (writeToProp)
	{
		auto setMethod = GetPropertyMethodDef(propDef, BfMethodType_PropertySetter, mPropCheckedKind, mPropTarget);
		if (setMethod == NULL)
		{
			mModule->Fail("Property has no setter", propSrc);
			return;
		}

		auto methodInstance = GetPropertyMethodInstance(setMethod);
		if (!methodInstance.mFunc)
			return;

		if (propSrc != NULL)
			mModule->UpdateExprSrcPos(propSrc);

		SizedArray<BfIRValue, 4> args;
		if (!setMethod->mIsStatic)
		{
			auto usePropTarget = propTarget;
			if (origPropTarget.mType == methodInstance.mMethodInstance->GetOwner())
				usePropTarget = origPropTarget;
			else
				BF_ASSERT(propTarget.mType == methodInstance.mMethodInstance->GetOwner());
			PushThis(propSrc, usePropTarget, methodInstance.mMethodInstance, args);
		}

		for (int paramIdx = 0; paramIdx < (int)indexerVals.size(); paramIdx++)
		{
			auto val = mModule->Cast(propSrc, indexerVals[paramIdx].mTypedValue, methodInstance.mMethodInstance->GetParamType(paramIdx));
			if (!val)
				return;
			PushArg(val, args);
		}

		PushArg(writeToProp, args);
		CreateCall(opToken, methodInstance.mMethodInstance, methodInstance.mFunc, false, args);
	}
}

void BfExprEvaluator::PerformBinaryOperation(BfExpression* leftExpression, BfExpression* rightExpression, BfBinaryOp binaryOp, BfTokenNode* opToken, BfBinOpFlags flags, BfTypedValue leftValue)
{
	BfTypedValue rightValue;
	if (rightExpression == NULL)
	{
		mModule->AssertErrorState();
		return;
	}
	if (!leftValue)
	{
		if (!rightValue)
			mModule->CreateValueFromExpression(rightExpression, mExpectingType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
		return;
	}
	if (leftValue.mType->IsRef())
		leftValue.mType = leftValue.mType->GetUnderlyingType();

	if ((binaryOp == BfBinaryOp_ConditionalAnd) || (binaryOp == BfBinaryOp_ConditionalOr))
	{
 		if (mModule->mCurMethodState->mDeferredLocalAssignData != NULL)
 			mModule->mCurMethodState->mDeferredLocalAssignData->BreakExtendChain();
		if (mModule->mCurMethodState->mCurScope->mScopeKind == BfScopeKind_StatementTarget)
			mModule->mCurMethodState->mCurScope->mScopeKind = BfScopeKind_StatementTarget_Conditional;

		bool isAnd = binaryOp == BfBinaryOp_ConditionalAnd;

		auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
		leftValue = mModule->Cast(leftExpression, leftValue, boolType);
		if (!leftValue)
		{
			mModule->CreateValueFromExpression(rightExpression);
			mResult = mModule->GetDefaultTypedValue(boolType, false, BfDefaultValueKind_Undef);
			return;
		}

		auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();

		SetAndRestoreValue<bool> prevInCondBlock(mModule->mCurMethodState->mInConditionalBlock, true);

		// The RHS is not guaranteed to be executed
		if ((mModule->mCurMethodState->mDeferredLocalAssignData != NULL) &&
			(mModule->mCurMethodState->mDeferredLocalAssignData->mIsIfCondition))
			mModule->mCurMethodState->mDeferredLocalAssignData->mIfMayBeSkipped = true;

		if (isAnd)
		{
			bool isConstBranch = false;
			bool constResult = false;
			if (leftValue.mValue.IsConst())
			{
				auto constValue = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
				if (constValue->mTypeCode == BfTypeCode_Boolean)
				{
					isConstBranch = true;
					constResult = constValue->mBool;
				}
			}

			if (isConstBranch)
			{
				if ((constResult) || (HasVariableDeclaration(rightExpression)))
				{
					// Only right side
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
					mResult = rightValue;
				}
				else
				{
					// Always false
					SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
					SetAndRestoreValue<bool> prevInConstIgnore(mModule->mCurMethodState->mCurScope->mInConstIgnore, true);
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), boolType);
				}
			}
			else
			{
				auto rhsBB = mModule->mBfIRBuilder->CreateBlock("land.rhs");
				auto endBB = mModule->mBfIRBuilder->CreateBlock("land.end");

				mModule->mBfIRBuilder->CreateCondBr(leftValue.mValue, rhsBB, endBB);

				mModule->AddBasicBlock(rhsBB);
				rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_CreateConditionalScope));
				mModule->mBfIRBuilder->CreateBr(endBB);

				auto endRhsBB = mModule->mBfIRBuilder->GetInsertBlock();
				mModule->AddBasicBlock(endBB);
				auto phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(boolType), 2);
				mModule->mBfIRBuilder->AddPhiIncoming(phi, mModule->GetConstValue(0, boolType), prevBB);
				if (rightValue)
					mModule->mBfIRBuilder->AddPhiIncoming(phi, rightValue.mValue, endRhsBB);
				mResult = BfTypedValue(phi, boolType);
			}
		}
		else
		{
			// Put variables in here into a 'possibly assigned' but never commit it.
			//  Because if we had "if ((Get(out a)) || (GetOther(out a))" then the LHS would already set it as defined, so
			//  the RHS is inconsequential
			BfDeferredLocalAssignData deferredLocalAssignData;
			deferredLocalAssignData.ExtendFrom(mModule->mCurMethodState->mDeferredLocalAssignData, false);
			deferredLocalAssignData.mVarIdBarrier = mModule->mCurMethodState->GetRootMethodState()->mCurLocalVarId;
			SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mModule->mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

			bool isConstBranch = false;
			bool constResult = false;
			if (leftValue.mValue.IsConst())
			{
				auto constValue = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
				if (constValue->mTypeCode == BfTypeCode_Boolean)
				{
					isConstBranch = true;
					constResult = constValue->mBool;
				}
			}

			if (isConstBranch)
			{
				if ((constResult) && (!HasVariableDeclaration(rightExpression)))
				{
					// Always true
					SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
					SetAndRestoreValue<bool> prevInConstIgnore(mModule->mCurMethodState->mCurScope->mInConstIgnore, true);
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), boolType);
				}
				else
				{
					// Only right side
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
					mResult = rightValue;
				}
			}
			else
			{
				auto rhsBB = mModule->mBfIRBuilder->CreateBlock("lor.rhs");
				auto endBB = mModule->mBfIRBuilder->CreateBlock("lor.end");

				// This makes any 'scope' allocs be dyn since we aren't sure if this will be short-circuited
				SetAndRestoreValue<bool> prevIsConditional(mModule->mCurMethodState->mCurScope->mIsConditional, true);

				mModule->mBfIRBuilder->CreateCondBr(leftValue.mValue, endBB, rhsBB);

				mModule->AddBasicBlock(rhsBB);
				rightValue = mModule->CreateValueFromExpression(rightExpression, boolType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
				mModule->mBfIRBuilder->CreateBr(endBB);

				auto endRhsBB = mModule->mBfIRBuilder->GetInsertBlock();
				mModule->AddBasicBlock(endBB);
				auto phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(boolType), 2);
				mModule->mBfIRBuilder->AddPhiIncoming(phi, mModule->GetConstValue(1, boolType), prevBB);
				if (rightValue)
					mModule->mBfIRBuilder->AddPhiIncoming(phi, rightValue.mValue, endRhsBB);
				mResult = BfTypedValue(phi, boolType);
			}
		}
		return;
	}

	BfType* wantType = leftValue.mType;
	BfType* origWantType = wantType;
	if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
		wantType = NULL; // Don't presume
	wantType = mModule->FixIntUnknown(wantType);

	if ((binaryOp == BfBinaryOp_NullCoalesce) && (PerformBinaryOperation_NullCoalesce(opToken, leftExpression, rightExpression, leftValue, wantType, NULL)))
		return;

	BfType* rightWantType = wantType;
	if (origWantType->IsIntUnknown())
		rightWantType = NULL;
	else if ((mExpectingType != NULL) && (wantType != NULL) && (mExpectingType->IsIntegral()) && (wantType->IsIntegral()) && (mExpectingType->mSize > wantType->mSize) &&
		((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_Multiply)))
		rightWantType = mExpectingType;
	rightValue = mModule->CreateValueFromExpression(rightExpression, rightWantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowIntUnknown));
	if ((rightWantType != wantType) && (rightValue.mType == rightWantType))
		wantType = rightWantType;
	if ((!leftValue) || (!rightValue))
		return;

	PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue, rightValue);
}

bool BfExprEvaluator::PerformBinaryOperation_NullCoalesce(BfTokenNode* opToken, BfExpression* leftExpression, BfExpression* rightExpression, BfTypedValue leftValue, BfType* wantType, BfTypedValue* assignTo)
{
	if ((leftValue) && ((leftValue.mType->IsPointer()) || (leftValue.mType->IsFunction()) || (leftValue.mType->IsObject())) || (leftValue.mType->IsNullable()))
	{
		leftValue = mModule->LoadOrAggregateValue(leftValue);

		BfType* nullableElementType = NULL;
		BfIRValue nullableHasValue;
		BfTypedValue nullableExtractedLeftValue;
		if (leftValue.mType->IsNullable())
		{
			nullableElementType = leftValue.mType->GetUnderlyingType();
			nullableHasValue = mModule->mBfIRBuilder->CreateExtractValue(leftValue.mValue, nullableElementType->IsValuelessType() ? 1 : 2); // has_value
			if (!nullableElementType->IsValuelessType())
				nullableExtractedLeftValue = BfTypedValue(mModule->mBfIRBuilder->CreateExtractValue(leftValue.mValue, 1), nullableElementType); // value
			else
				nullableExtractedLeftValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), nullableElementType);
		}

		if (leftValue.mValue.IsConst())
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
			if (constant->IsNull())
			{
				mResult = mModule->CreateValueFromExpression(rightExpression, wantType, (BfEvalExprFlags)(mBfEvalExprFlags & BfEvalExprFlags_InheritFlags));
				return true;
			}

			// Already have a value, we don't need the right side
			SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
			SetAndRestoreValue<bool> prevInConstIgnore(mModule->mCurMethodState->mCurScope->mInConstIgnore, true);
			mModule->CreateValueFromExpression(rightExpression, wantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_CreateConditionalScope));
			mResult = leftValue;
			return true;
		}

		auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();
		auto rhsBB = mModule->mBfIRBuilder->CreateBlock("nullc.rhs");
		auto endBB = mModule->mBfIRBuilder->CreateBlock("nullc.end");
		auto lhsBB = endBB;

		auto endLhsBB = prevBB;

		BfIRValue isNull;
		if (nullableHasValue)
			isNull = mModule->mBfIRBuilder->CreateCmpEQ(nullableHasValue, mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0));
		else if (leftValue.mType->IsFunction())
			isNull = mModule->mBfIRBuilder->CreateIsNull(
				mModule->mBfIRBuilder->CreateIntToPtr(leftValue.mValue, mModule->mBfIRBuilder->MapType(mModule->GetPrimitiveType(BfTypeCode_NullPtr))));
		else
			isNull = mModule->mBfIRBuilder->CreateIsNull(leftValue.mValue);

		mModule->AddBasicBlock(rhsBB);
		BfTypedValue rightValue;

		if (assignTo != NULL)
			rightValue = mModule->CreateValueFromExpression(rightExpression, wantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_CreateConditionalScope));
		else
			rightValue = mModule->CreateValueFromExpression(rightExpression, wantType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_CreateConditionalScope));

		if (!rightValue)
		{
			mModule->AssertErrorState();
			return true;
		}

		if ((assignTo == NULL) && (leftValue.mType->IsNullable()) && (!rightValue.mType->IsNullable()))
		{
			if (wantType == leftValue.mType)
				wantType = nullableElementType;
			leftValue = nullableExtractedLeftValue;
		}

		rightValue = mModule->LoadValue(rightValue);

		if (assignTo == NULL)
		{
			auto rightToLeftValue = mModule->CastToValue(rightExpression, rightValue, leftValue.mType, BfCastFlags_SilentFail);
			if (rightToLeftValue)
			{
				rightValue = BfTypedValue(rightToLeftValue, leftValue.mType);
			}
			else
			{
				lhsBB = mModule->mBfIRBuilder->CreateBlock("nullc.lhs", true);
				mModule->mBfIRBuilder->SetInsertPoint(lhsBB);

				auto leftToRightValue = mModule->CastToValue(leftExpression, leftValue, rightValue.mType, BfCastFlags_SilentFail);
				if (leftToRightValue)
				{
					leftValue = BfTypedValue(leftToRightValue, rightValue.mType);
				}
				else
				{
					// Note: Annoying trigraph split for '??'
					mModule->Fail(StrFormat("Operator '?" "?' cannot be applied to operands of type '%s' and '%s'",
						mModule->TypeToString(leftValue.mType).c_str(), mModule->TypeToString(rightValue.mType).c_str()), opToken);
					leftValue = mModule->GetDefaultTypedValue(rightValue.mType);
				}

				mModule->mBfIRBuilder->CreateBr(endBB);
				endLhsBB = mModule->mBfIRBuilder->GetInsertBlock();
				mModule->mBfIRBuilder->SetInsertPoint(rhsBB);
			}
		}

		if (assignTo != NULL)
			mModule->mBfIRBuilder->CreateStore(rightValue.mValue, assignTo->mValue);

		mModule->mBfIRBuilder->CreateBr(endBB);
		auto endRhsBB = mModule->mBfIRBuilder->GetInsertBlock();

		// Actually add CondBr at start
		mModule->mBfIRBuilder->SetInsertPoint(prevBB);
		mModule->mBfIRBuilder->CreateCondBr(isNull, rhsBB, lhsBB);

		mModule->AddBasicBlock(endBB);

		if (assignTo != NULL)
		{
			mResult = *assignTo;
		}
		else
		{
			auto phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(leftValue.mType), 2);
			mModule->mBfIRBuilder->AddPhiIncoming(phi, leftValue.mValue, endLhsBB);
			mModule->mBfIRBuilder->AddPhiIncoming(phi, rightValue.mValue, endRhsBB);
			mResult = BfTypedValue(phi, leftValue.mType);
		}

		return true;
	}

	return false;
}

bool BfExprEvaluator::PerformBinaryOperation_Numeric(BfAstNode* leftExpression, BfAstNode* rightExpression, BfBinaryOp binaryOp, BfAstNode* opToken, BfBinOpFlags flags, BfTypedValue leftValue, BfTypedValue rightValue)
{
	switch (binaryOp)
	{
	case BfBinaryOp_Add:
	case BfBinaryOp_Subtract:
	case BfBinaryOp_Multiply:
	case BfBinaryOp_Divide:
	case BfBinaryOp_Modulus:
		break;
	default:
		return false;
	}

	auto wantType = mExpectingType;
	if ((wantType == NULL) ||
		((!wantType->IsFloat()) && (!wantType->IsIntegral())))
		wantType = NULL;

	auto leftType = mModule->GetClosestNumericCastType(leftValue, mExpectingType);
	auto rightType = mModule->GetClosestNumericCastType(rightValue, mExpectingType);

	if (leftType != NULL)
	{
		if ((rightType == NULL) || (mModule->CanCast(mModule->GetFakeTypedValue(rightType), leftType)))
			wantType = leftType;
		else if ((rightType != NULL) && (mModule->CanCast(mModule->GetFakeTypedValue(leftType), rightType)))
			wantType = rightType;
	}
	else if (rightType != NULL)
		wantType = rightType;

	if (wantType == NULL)
		wantType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

	auto convLeftValue = mModule->Cast(opToken, leftValue, wantType, BfCastFlags_SilentFail);
	if (!convLeftValue)
		return false;

	auto convRightValue = mModule->Cast(opToken, rightValue, wantType, BfCastFlags_SilentFail);
	if (!convRightValue)
		return false;

	mResult = BfTypedValue();

	// Let the error come from here, if any - so we always return 'true' to avoid a second error
	PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, flags, convLeftValue, convRightValue);

	return true;
}

void BfExprEvaluator::PerformBinaryOperation(BfExpression* leftExpression, BfExpression* rightExpression, BfBinaryOp binaryOp, BfTokenNode* opToken, BfBinOpFlags flags)
{
	if ((binaryOp == BfBinaryOp_Range) || (binaryOp == BfBinaryOp_ClosedRange))
	{
		auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

		bool isIndexExpr = false;
		BfTypeDef* typeDef = NULL;
		if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(leftExpression))
			if (unaryOpExpr->mOp == BfUnaryOp_FromEnd)
				isIndexExpr = true;
		if (rightExpression == NULL)
			isIndexExpr = true;
		if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(rightExpression))
			if (unaryOpExpr->mOp == BfUnaryOp_FromEnd)
				isIndexExpr = true;

		if (isIndexExpr)
			typeDef = mModule->mCompiler->mIndexRangeTypeDef;
		else
			typeDef = (binaryOp == BfBinaryOp_Range) ? mModule->mCompiler->mRangeTypeDef : mModule->mCompiler->mClosedRangeTypeDef;

		auto allocType = mModule->ResolveTypeDef(typeDef)->ToTypeInstance();
		auto alloca = mModule->CreateAlloca(allocType);

		BfTypedValueExpression leftTypedValueExpr;
		BfTypedValueExpression rightTypedValueExpr;
		BfTypedValueExpression isClosedTypedValueExpr;

		SizedArray<BfExpression*, 2> argExprs;
		if (leftExpression != NULL)
		{
			argExprs.Add(leftExpression);
		}
		else
		{
			leftTypedValueExpr.mRefNode = opToken;
			leftTypedValueExpr.mTypedValue = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			argExprs.Add(&leftTypedValueExpr);
		}

		if (rightExpression != NULL)
		{
			argExprs.Add(rightExpression);
		}
		else
		{
			// Add as a `^1`
			auto indexType = mModule->ResolveTypeDef(mModule->mCompiler->mIndexTypeDef)->ToTypeInstance();
			mModule->PopulateType(indexType->mBaseType);

			BF_ASSERT_REL(indexType->mBaseType->mBaseType != NULL);

			rightTypedValueExpr.mRefNode = opToken;
			
			auto valueTypeEmpty = mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(indexType->mBaseType->mBaseType), {});
			SizedArray<BfIRValue, 8> enumMembers;
			enumMembers.Add(valueTypeEmpty);
			auto enumValue = mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(indexType->mBaseType), enumMembers);

			SizedArray<BfIRValue, 8> tupleMembers;
			tupleMembers.Add(valueTypeEmpty);
			tupleMembers.Add(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
			auto tupleValue = mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(indexType->mFieldInstances[0].mResolvedType), tupleMembers);

			SizedArray<BfIRValue, 8> indexMembers;
			indexMembers.Add(enumValue);
			indexMembers.Add(tupleValue);
			indexMembers.Add(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Int8, 1));
			auto indexValue = mModule->mBfIRBuilder->CreateConstAgg(mModule->mBfIRBuilder->MapType(indexType), indexMembers);

			rightTypedValueExpr.mTypedValue = BfTypedValue(indexValue, indexType);
			argExprs.Add(&rightTypedValueExpr);
		}

		if (isIndexExpr)
		{
			isClosedTypedValueExpr.mRefNode = opToken;
			isClosedTypedValueExpr.mTypedValue = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, (binaryOp == BfBinaryOp_ClosedRange) ? 1 : 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			argExprs.Add(&isClosedTypedValueExpr);
		}

		BfSizedArray<BfExpression*> args = argExprs;

		BfResolvedArgs argValues;
		argValues.Init(&args);
		ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);

		mResult = BfTypedValue(alloca, allocType, true);
		auto result = MatchConstructor(opToken, NULL, mResult, allocType, argValues, true, false);
		if ((result) && (!result.mType->IsVoid()))
			mResult = result;

		return;
	}

	BfTypedValue leftValue;
	if (leftExpression != NULL)
	{
		leftValue = mModule->CreateValueFromExpression(leftExpression, mExpectingType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowIntUnknown));
	}
	PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue);
}

bool BfExprEvaluator::CheckConstCompare(BfBinaryOp binaryOp, BfAstNode* opToken, const BfTypedValue& leftValue, const BfTypedValue& rightValue)
{
	if ((binaryOp < BfBinaryOp_Equality) || (binaryOp > BfBinaryOp_LessThanOrEqual))
		return false;

	// LHS is expected to be a value and RHS is expected to be a const
	if (!leftValue.mType->IsIntegral())
		return false;

	BF_ASSERT(rightValue.mValue.IsConst());
	auto rightConst = mModule->mBfIRBuilder->GetConstant(rightValue.mValue);
	if (!mModule->mBfIRBuilder->IsInt(rightConst->mTypeCode))
		return false;

	BfType* checkType = leftValue.mType;
	if (checkType->IsTypedPrimitive())
		checkType = checkType->GetUnderlyingType();
	if (!checkType->IsPrimitiveType())
		return false;

	BfTypeCode typeCode = ((BfPrimitiveType*)checkType)->mTypeDef->mTypeCode;

	int64 minValue = 0;
	int64 maxValue = 0;
	switch (typeCode)
	{
	case BfTypeCode_Int8:
		minValue = -0x80;
		maxValue =  0x7F;
		break;
	case BfTypeCode_Int16:
		minValue = -0x8000;
		maxValue =  0x7FFF;
		break;
	case BfTypeCode_Int32:
		minValue = -0x80000000LL;
		maxValue =  0x7FFFFFFF;
		break;
	case BfTypeCode_Int64:
		minValue = -0x8000000000000000LL;
		maxValue =  0x7FFFFFFFFFFFFFFFLL;
		break;
	case BfTypeCode_UInt8:
		maxValue = 0xFF;
		break;
	case BfTypeCode_UInt16:
		maxValue = 0xFFFF;
		break;
	case BfTypeCode_UInt32:
		maxValue = 0xFFFFFFFF;
		break;
	default:
		return false;
	}

	int constResult = -1;
	if (typeCode == BfTypeCode_UInt64)
	{
		switch (binaryOp)
		{
		case BfBinaryOp_Equality:
		case BfBinaryOp_StrictEquality:
			if (rightConst->mInt64 < minValue)
				constResult = 0;
			break;
		case BfBinaryOp_InEquality:
		case BfBinaryOp_StrictInEquality:
			if (rightConst->mInt64 < minValue)
				constResult = 1;
			break;
		case BfBinaryOp_LessThan:
			if (rightConst->mInt64 <= minValue)
				constResult = 0;
			break;
		case BfBinaryOp_LessThanOrEqual:
			if (rightConst->mInt64 < minValue)
				constResult = 0;
			break;
		default: break;
		}
		return false;
	}
	else
	{
		switch (binaryOp)
		{
		case BfBinaryOp_Equality:
		case BfBinaryOp_StrictEquality:
			if (rightConst->mInt64 < minValue)
				constResult = 0;
			else if (rightConst->mInt64 > maxValue)
				constResult = 0;
			break;
		case BfBinaryOp_InEquality:
		case BfBinaryOp_StrictInEquality:
			if (rightConst->mInt64 < minValue)
				constResult = 1;
			else if (rightConst->mInt64 > maxValue)
				constResult = 1;
			break;
		case BfBinaryOp_LessThan:
			if (rightConst->mInt64 <= minValue)
				constResult = 0;
			else if (rightConst->mInt64 > maxValue)
				constResult = 1;
			break;
		case BfBinaryOp_LessThanOrEqual:
			if (rightConst->mInt64 < minValue)
				constResult = 0;
			else if (rightConst->mInt64 >= maxValue)
				constResult = 1;
			break;
		case BfBinaryOp_GreaterThan:
			if (rightConst->mInt64 >= maxValue)
				constResult = 0;
			else if (rightConst->mInt64 < minValue)
				constResult = 1;
			break;
		case BfBinaryOp_GreaterThanOrEqual:
			if (rightConst->mInt64 > maxValue)
				constResult = 0;
			else if (rightConst->mInt64 <= minValue)
				constResult = 1;
			break;
		default: break;
		}
	}

	if (constResult == 0)
	{
		mModule->Warn(0, "The result of this operation is always 'false'", opToken);
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
		return true;
	}
	else  if (constResult == 1)
	{
		mModule->Warn(0, "The result of this operation is always 'true'", opToken);
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), mModule->GetPrimitiveType(BfTypeCode_Boolean));
		return true;
	}

	return false;
}

void BfExprEvaluator::AddStrings(const BfTypedValue& leftValue, const BfTypedValue& rightValue, BfAstNode* refNode)
{
	if ((leftValue.mValue.IsConst()) && (rightValue.mValue.IsConst()))
	{
		String* lhsStr = mModule->GetStringPoolString(leftValue.mValue, mModule->mBfIRBuilder);
		String* rhsStr = mModule->GetStringPoolString(rightValue.mValue, mModule->mBfIRBuilder);

		if ((lhsStr != NULL) && (rhsStr != NULL))
		{
			String resultStr = *lhsStr + *rhsStr;

			BfVariant variant;
			variant.mTypeCode = BfTypeCode_CharPtr;
			variant.mString = &resultStr;

			GetLiteral(refNode, variant);
			return;
		}
	}

	mModule->Fail("Strings can only be added when they are constants. Consider allocating a string and using Concat.", refNode);
	return;
}

void BfExprEvaluator::PerformBinaryOperation(BfAstNode* leftExpression, BfAstNode* rightExpression, BfBinaryOp binaryOp, BfAstNode* opToken, BfBinOpFlags flags, BfTypedValue leftValue, BfTypedValue rightValue)
{
	bool noClassify = (flags & BfBinOpFlag_NoClassify) != 0;
	bool forceLeftType = (flags & BfBinOpFlag_ForceLeftType) != 0;
	bool deferRight = (flags & BfBinOpFlag_DeferRight) != 0;

	if (deferRight)
	{
		rightValue = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
	}

	if ((rightValue.mValue.IsConst()) && (!leftValue.mValue.IsConst()))
	{
		if (CheckConstCompare(binaryOp, opToken, leftValue, rightValue))
			return;
	}
	else if ((leftValue.mValue.IsConst()) && (!rightValue.mValue.IsConst()))
	{
		if (CheckConstCompare(BfGetOppositeBinaryOp(binaryOp), opToken, rightValue, leftValue))
			return;
	}

	if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
	{
		forceLeftType = true;
	}

	if (rightValue.mType->IsRef())
		rightValue.mType = rightValue.mType->GetUnderlyingType();

	BfType* origLeftType = leftValue.mType;
	BfType* origRightType = rightValue.mType;

	mModule->FixIntUnknown(leftValue, rightValue);

	// Prefer floats, prefer chars
	int leftCompareSize = leftValue.mType->mSize;
	if (leftValue.mType->IsFloat())
		leftCompareSize += 0x10;
	if (leftValue.mType->IsChar())
		leftCompareSize += 0x100;
	if (!leftValue.mType->IsPrimitiveType())
		leftCompareSize += 0x1000;

	int rightCompareSize = rightValue.mType->mSize;
	if (rightValue.mType->IsFloat())
		rightCompareSize += 0x10;
	if (rightValue.mType->IsChar())
		rightCompareSize += 0x100;
	if (!rightValue.mType->IsPrimitiveType())
		rightCompareSize += 0x1000;

	if ((leftValue.mType->IsTypeInstance()) && (rightValue.mType->IsTypeInstance()))
	{
		int leftInheritDepth = leftValue.mType->ToTypeInstance()->mInheritDepth;
		int rightInheritDepth = rightValue.mType->ToTypeInstance()->mInheritDepth;
		if (leftInheritDepth < rightInheritDepth)
		{
			// If both are type instances then choose the type with the lowest inherit depth
			//  so we will choose the base type when applicable
			forceLeftType = true;
		}
	}

	auto resultType = leftValue.mType;
	if (!forceLeftType)
	{
		bool handled = false;

		BfType* expectingType = mExpectingType;

		if (leftValue.mType == rightValue.mType)
		{
			// All good
			handled = true;
		}
		else if ((expectingType != NULL) &&
			(mModule->CanCast(leftValue, expectingType, BfCastFlags_NoBox)) &&
			(mModule->CanCast(rightValue, expectingType, BfCastFlags_NoBox)) &&
			(!leftValue.mType->IsVar()) && (!rightValue.mType->IsVar()))
		{
			resultType = expectingType;
			handled = true;
		}
		else
		{
			// If one of these is a constant that can be converted into a smaller type, then do that
			if (rightValue.mValue.IsConst())
			{
				if (mModule->CanCast(rightValue, leftValue.mType, BfCastFlags_NoBox))
				{
					resultType = leftValue.mType;
					handled = true;
				}
			}

			// If left is an IntUnknown, allow the right to inform the type
			if (leftValue.mType->IsIntUnknown())
			{
				if (leftValue.mValue.IsConst())
				{
					if (mModule->CanCast(leftValue, rightValue.mType))
					{
						resultType = rightValue.mType;
						handled = true;
					}
				}
			}

			if ((leftValue.mType->IsPointer()) &&
				(rightValue.mType->IsPointer()))
			{
				BfPointerType* leftPointerType = (BfPointerType*)leftValue.mType;
				BfPointerType* rightPointerType = (BfPointerType*)rightValue.mType;

				// If one is a pointer to a sized array then use the other type
				if (leftPointerType->mElementType->IsSizedArray())
				{
					resultType = rightPointerType;
					handled = true;
				}
				else if (rightPointerType->mElementType->IsSizedArray())
				{
					resultType = leftPointerType;
					handled = true;
				}
			}
		}

		if (!handled)
		{
			if ((resultType->IsNull()) ||
				(rightCompareSize > leftCompareSize) ||
				(((rightCompareSize == leftCompareSize) && (!rightValue.mType->IsSigned()))) ||
				(rightValue.mType->IsTypedPrimitive()))
			{
				// Select the type with the "most information"
				if (!rightValue.mType->IsNull())
					resultType = rightValue.mType;
			}

			if ((!resultType->IsPointer()) && (rightValue.mType->IsPointer()))
				resultType = rightValue.mType;
		}
	}

	bool explicitCast = false;
	BfTypedValue* resultTypedValue;
	BfTypedValue* otherTypedValue;
	BfType* otherType;
	BfAstNode* resultTypeSrc;
	BfAstNode* otherTypeSrc;

	if (resultType == leftValue.mType)
	{
		resultTypedValue = &leftValue;
		resultTypeSrc = leftExpression;
		otherTypedValue = &rightValue;
		otherTypeSrc = rightExpression;
		otherType = otherTypedValue->mType;
	}
	else
	{
		resultTypedValue = &rightValue;
		resultTypeSrc = rightExpression;
		otherTypedValue = &leftValue;
		otherTypeSrc = leftExpression;
		otherType = otherTypedValue->mType;
	}

	auto _OpFail = [&]()
	{
		if ((rightValue.mType != NULL) && (leftValue.mType != NULL))
		{
			if (rightValue.mType != leftValue.mType)
				mModule->Fail(StrFormat("Cannot perform binary operation '%s' between types '%s' and '%s'",
					BfGetOpName(binaryOp), mModule->TypeToString(leftValue.mType).c_str(), mModule->TypeToString(rightValue.mType).c_str()), opToken);
			else
			{
				if (leftValue.mType->IsInterface())
				{
					mModule->Fail(StrFormat("Cannot perform binary operation '%s' between two instances of interface '%s'. Consider rewriting using generics and use this interface as a generic constraint.",
						BfGetOpName(binaryOp), mModule->TypeToString(leftValue.mType).c_str()), opToken);
				}
				else
				{
					mModule->Fail(StrFormat("Cannot perform binary operation '%s' between two instances of type '%s'",
						BfGetOpName(binaryOp), mModule->TypeToString(leftValue.mType).c_str()), opToken);
				}
			}
		}
		else
			mModule->Fail(StrFormat("Cannot perform binary operation '%s'", BfGetOpName(binaryOp)), opToken);
	};

	// This case fixes cases like "c == 0" where "0" is technically an int but can be reduced
	if (BfBinOpEqualityCheck(binaryOp))
	{
		if ((resultType != otherType) && (resultTypedValue->mValue.IsConst()) && (mModule->CanCast(*resultTypedValue, otherType)))
		{
			std::swap(resultTypedValue, otherTypedValue);
			std::swap(resultTypeSrc, otherTypeSrc);
			std::swap(resultType, otherType);
		}
	}

	BfIRValue convLeftValue;
	BfIRValue convRightValue;

	if (((resultType->IsVar()) || (otherType->IsVar())) && (!deferRight))
	{
		bool isComparison = (binaryOp >= BfBinaryOp_Equality) && (binaryOp <= BfBinaryOp_LessThanOrEqual);
		if (isComparison)
			mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Boolean), false, BfDefaultValueKind_Addr);
		else if (mExpectingType != NULL)
			mResult = mModule->GetDefaultTypedValue(mExpectingType, false, BfDefaultValueKind_Addr);
		else
			mResult = mModule->GetDefaultTypedValue(resultType, false, BfDefaultValueKind_Addr);
		return;
	}

	if ((otherType->IsNull()) && (BfBinOpEqualityCheck(binaryOp)))
	{
		bool isEquality = (binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality);

		if ((resultType->IsValueType()) && (!resultType->IsFunction()))
		{
			auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

			if (resultType->IsNullable())
			{
				auto elementType = resultType->GetUnderlyingType();
				mModule->PopulateType(elementType);
				if (elementType->IsValuelessType())
				{
					mModule->mBfIRBuilder->PopulateType(resultType);
					BfTypedValue nullableTypedVale = mModule->MakeAddressable(*resultTypedValue);
					BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(nullableTypedVale.mValue, 0, 1);
					BfIRValue hasValueValue = mModule->mBfIRBuilder->CreateAlignedLoad(hasValuePtr, 1);
					if (isEquality)
						hasValueValue = mModule->mBfIRBuilder->CreateNot(hasValueValue);
					mResult = BfTypedValue(hasValueValue, boolType);
				}
				else
				{
					mModule->mBfIRBuilder->PopulateType(resultType);
					BfTypedValue nullableTypedVale = mModule->MakeAddressable(*resultTypedValue);
					BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(nullableTypedVale.mValue, 0, 2);
					BfIRValue hasValueValue = mModule->mBfIRBuilder->CreateAlignedLoad(hasValuePtr, 1);
					if (isEquality)
						hasValueValue = mModule->mBfIRBuilder->CreateNot(hasValueValue);
					mResult = BfTypedValue(hasValueValue, boolType);
				}
				return;
			}

			if (resultType->IsNull())
			{
				// Null always equals null
				mResult = BfTypedValue(mModule->GetConstValue(isEquality ? 1 : 0, boolType), boolType);
				return;
			}

			if (!mModule->IsInSpecializedSection())
			{
				//CS0472: The result of the expression is always 'true' since a value of type 'int' is never equal to 'null' of type '<null>'
				mModule->Warn(BfWarning_CS0472_ValueTypeNullCompare,
					StrFormat("The result of the expression is always '%s' since a value of type '%s' can never be null",
						isEquality ? "false" : "true", mModule->TypeToString(resultType).c_str()), otherTypeSrc);
			}

			// Valuetypes never equal null
			mResult = BfTypedValue(mModule->GetConstValue(isEquality ? 0 : 1, boolType), boolType);
			return;
		}
	}

	// Check for constant equality checks (mostly for strings)
	if (BfBinOpEqualityCheck(binaryOp))
	{
		auto leftConstant = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
		auto rightConstant = mModule->mBfIRBuilder->GetConstant(rightValue.mValue);

		if ((leftConstant != NULL) && (rightConstant != NULL))
		{
			auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
			int leftStringPoolIdx = mModule->GetStringPoolIdx(leftValue.mValue, mModule->mBfIRBuilder);
			if (leftStringPoolIdx != -1)
			{
				int rightStringPoolIdx = mModule->GetStringPoolIdx(rightValue.mValue, mModule->mBfIRBuilder);
				if (rightStringPoolIdx != -1)
				{
					bool isEqual = leftStringPoolIdx == rightStringPoolIdx;
					if ((binaryOp == BfBinaryOp_InEquality) || (binaryOp == BfBinaryOp_StrictInEquality))
						isEqual = !isEqual;
					mResult = BfTypedValue(mModule->GetConstValue(isEqual ? 1 : 0, boolType), boolType);
					return;
				}
			}

			int eqResult = mModule->mBfIRBuilder->CheckConstEquality(leftValue.mValue, rightValue.mValue);
			if (eqResult != -1)
			{
				bool isEqual = eqResult == 1;
				if ((binaryOp == BfBinaryOp_InEquality) || (binaryOp == BfBinaryOp_StrictInEquality))
					isEqual = !isEqual;
				mResult = BfTypedValue(mModule->GetConstValue(isEqual ? 1 : 0, boolType), boolType);
				return;
			}
		}
	}

	if ((leftValue.mType->IsTypeInstance()) || (leftValue.mType->IsGenericParam()) ||
		(rightValue.mType->IsTypeInstance()) || (rightValue.mType->IsGenericParam()))
	{
		// As an optimization, we don't call user operator overloads for null checks
		bool skipOpOverload = false;
		if ((binaryOp == BfBinaryOp_StrictEquality) || (binaryOp == BfBinaryOp_StrictInEquality))
			skipOpOverload = true;
		else if (BfBinOpEqualityCheck(binaryOp))
		{
			if (!leftValue.IsAddr())
			{
				auto leftConstant = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
					if ((leftConstant != NULL) && (leftConstant->IsNull()))
						skipOpOverload = true;
			}

			if (!rightValue.IsAddr())
			{
				auto rightConstant = mModule->mBfIRBuilder->GetConstant(rightValue.mValue);
				if ((rightConstant != NULL) && (rightConstant->IsNull()))
					skipOpOverload = true;
			}
		}

		if ((binaryOp == BfBinaryOp_Add) && (resultType->IsInstanceOf(mModule->mCompiler->mStringTypeDef)))
		{
			// Allow failover to constant string addition
			if ((leftValue.mValue.IsConst()) && (rightValue.mValue.IsConst()))
				skipOpOverload = true;
		}

		if (!skipOpOverload)
		{
			BfBinaryOp findBinaryOp = binaryOp;

			bool isComparison = (binaryOp >= BfBinaryOp_Equality) && (binaryOp <= BfBinaryOp_LessThanOrEqual);

			for (int pass = 0; pass < 2; pass++)
			{
				BfBinaryOp oppositeBinaryOp = BfGetOppositeBinaryOp(findBinaryOp);
				BfBinaryOp overflowBinaryOp = BfBinaryOp_None;

				if (findBinaryOp == BfBinaryOp_OverflowAdd)
					overflowBinaryOp = BfBinaryOp_Add;
				else if (findBinaryOp == BfBinaryOp_OverflowSubtract)
					overflowBinaryOp = BfBinaryOp_Subtract;
				else if (findBinaryOp == BfBinaryOp_OverflowMultiply)
					overflowBinaryOp = BfBinaryOp_Multiply;

				bool foundOp = false;

				BfResolvedArg leftArg;
				leftArg.mExpression = leftExpression;
				leftArg.mTypedValue = leftValue;
				BfResolvedArg rightArg;
				rightArg.mExpression = rightExpression;
				rightArg.mTypedValue = rightValue;

				if (deferRight)
				{
					BfResolvedArgs argValues;
					SizedArray<BfExpression*, 2> argExprs;
					argExprs.push_back(BfNodeDynCast<BfExpression>(rightExpression));
					BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
					argValues.Init(&sizedArgExprs);
					ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);
					rightArg = argValues.mResolvedArgs[0];
				}

				SizedArray<BfResolvedArg, 2> args;

				if (pass == 0)
				{
					args.push_back(leftArg);
					args.push_back(rightArg);
				}
				else
				{
					args.push_back(rightArg);
					args.push_back(leftArg);
				}

				auto checkLeftType = leftValue.mType;
				auto checkRightType = rightValue.mType;

				BfMethodMatcher methodMatcher(opToken, mModule, "", args, BfMethodGenericArguments());
				methodMatcher.mAllowImplicitRef = true;
				methodMatcher.mBfEvalExprFlags = BfEvalExprFlags_NoAutoComplete;
				BfBaseClassWalker baseClassWalker(checkLeftType, checkRightType, mModule);

				bool invertResult = false;
				BfType* operatorConstraintReturnType = NULL;

				bool wasTransformedUsage = (pass == 1);

				while (true)
				{
					auto entry = baseClassWalker.Next();
					auto checkType = entry.mTypeInstance;
					if (checkType == NULL)
						break;

					bool foundExactMatch = false;
					SizedArray<BfOperatorDef*, 8> oppositeOperatorDefs;

					for (auto operatorDef : checkType->mTypeDef->mOperators)
					{
						bool allowOp = operatorDef->mOperatorDeclaration->mBinOp == findBinaryOp;
						if ((isComparison) && (operatorDef->mOperatorDeclaration->mBinOp == BfBinaryOp_Compare))
							allowOp = true;

						if (allowOp)
						{
							foundOp = true;

							if (!methodMatcher.IsMemberAccessible(checkType, operatorDef->mDeclaringType))
								continue;

							if ((flags & BfBinOpFlag_IsConstraintCheck) != 0)
							{
								if (operatorDef->mGenericParams.IsEmpty())
								{
									// Fast check
									auto returnType = mModule->CheckOperator(checkType, operatorDef, args[0].mTypedValue, args[1].mTypedValue);
									if (returnType != NULL)
									{
										operatorConstraintReturnType = returnType;
										methodMatcher.mBestMethodDef = operatorDef;
										methodMatcher.mBestMethodTypeInstance = checkType;
										foundExactMatch = true;
									}
								}
								else
								{
									if (methodMatcher.CheckMethod(NULL, checkType, operatorDef, false))
									{
										auto rawMethodInstance = mModule->GetRawMethodInstance(checkType, operatorDef);
										auto returnType = mModule->ResolveGenericType(rawMethodInstance->mReturnType, NULL, &methodMatcher.mBestMethodGenericArguments,
											mModule->mCurTypeInstance);
										if (returnType != NULL)
										{
											operatorConstraintReturnType = returnType;
											foundExactMatch = true;
										}
									}
								}
							}
							else
							{
								if (methodMatcher.CheckMethod(NULL, checkType, operatorDef, false))
								{
									methodMatcher.mSelfType = entry.mSrcType;
									if (operatorDef->mOperatorDeclaration->mBinOp == findBinaryOp)
										foundExactMatch = true;
								}
							}
						}
						else if ((operatorDef->mOperatorDeclaration->mBinOp == oppositeBinaryOp) || (operatorDef->mOperatorDeclaration->mBinOp == overflowBinaryOp))
							oppositeOperatorDefs.Add(operatorDef);
					}

					if ((((methodMatcher.mBestMethodDef == NULL) && (operatorConstraintReturnType == NULL)) || (!foundExactMatch)) && (!oppositeOperatorDefs.IsEmpty()))
					{
						foundOp = true;
						for (auto oppositeOperatorDef : oppositeOperatorDefs)
						{
							if ((flags & BfBinOpFlag_IsConstraintCheck) != 0)
							{
								if (oppositeOperatorDef->mGenericParams.IsEmpty())
								{
									// Fast check
									auto returnType = mModule->CheckOperator(checkType, oppositeOperatorDef, args[0].mTypedValue, args[1].mTypedValue);
									if (returnType != NULL)
									{
										operatorConstraintReturnType = returnType;
										methodMatcher.mBestMethodDef = oppositeOperatorDef;
										methodMatcher.mBestMethodTypeInstance = checkType;
										methodMatcher.mSelfType = entry.mSrcType;
										if (oppositeBinaryOp != BfBinaryOp_None)
											wasTransformedUsage = true;
									}
								}
								else
								{
									if (methodMatcher.CheckMethod(NULL, checkType, oppositeOperatorDef, false))
									{
										auto rawMethodInstance = mModule->GetRawMethodInstance(checkType, oppositeOperatorDef);
										auto returnType = mModule->ResolveGenericType(rawMethodInstance->mReturnType, NULL, &methodMatcher.mBestMethodGenericArguments,
											mModule->mCurTypeInstance);
										if (returnType != NULL)
										{
											operatorConstraintReturnType = returnType;
											methodMatcher.mSelfType = entry.mSrcType;
											if (oppositeBinaryOp != BfBinaryOp_None)
												wasTransformedUsage = true;
										}
									}
								}
							}
							else
							{
								if (methodMatcher.CheckMethod(NULL, checkType, oppositeOperatorDef, false))
								{
									methodMatcher.mSelfType = entry.mSrcType;
									if (oppositeBinaryOp != BfBinaryOp_None)
										wasTransformedUsage = true;
								}
							}
						}
					}
				}

				bool hadMatch = (methodMatcher.mBestMethodDef != NULL);

				if ((methodMatcher.mBestMethodDef != NULL) && ((flags & BfBinOpFlag_IgnoreOperatorWithWrongResult) != 0))
				{
					auto matchedOp = ((BfOperatorDeclaration*)methodMatcher.mBestMethodDef->mMethodDeclaration)->mBinOp;
					methodMatcher.mBestMethodInstance = GetSelectedMethod(methodMatcher.mTargetSrc, methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher);
					if ((methodMatcher.mBestMethodInstance.mMethodInstance->mReturnType != mExpectingType) &&
						((matchedOp == binaryOp) || (matchedOp == oppositeBinaryOp)))
					{
						if (binaryOp == BfBinaryOp_Equality)
							binaryOp = BfBinaryOp_StrictEquality;
						if (binaryOp == BfBinaryOp_InEquality)
							binaryOp = BfBinaryOp_StrictEquality;

						hadMatch = false;
						break;
					}
				}

				if (hadMatch)
				{
					methodMatcher.FlushAmbiguityError();

					auto matchedOp = ((BfOperatorDeclaration*)methodMatcher.mBestMethodDef->mMethodDeclaration)->mBinOp;
					bool invertResult = matchedOp == oppositeBinaryOp;

					auto methodDef = methodMatcher.mBestMethodDef;
					auto autoComplete = GetAutoComplete();
					bool wasCapturingMethodInfo = false;
					if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(opToken)))
					{
						auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDef->mMethodDeclaration);
						if ((operatorDecl != NULL) && (operatorDecl->mOpTypeToken != NULL))
							autoComplete->SetDefinitionLocation(operatorDecl->mOpTypeToken);
					}

					if ((wasTransformedUsage) && (methodDef->mCommutableKind != BfCommutableKind_Operator))
					{
						auto error = mModule->Warn(BfWarning_BF4206_OperatorCommutableUsage, "Transformed operator usage requires 'Commutable' attribute to be added to the operator declaration", opToken);
						if ((error != NULL) && (methodDef->GetRefNode() != NULL))
							mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See operator declaration"), methodDef->GetRefNode());
					}

					if (opToken != NULL)
					{
						if ((opToken->IsA<BfTokenNode>()) && (!noClassify) && (!baseClassWalker.mMayBeFromInterface))
							mModule->SetElementType(opToken, BfSourceElementType_Method);
					}
					if ((flags & BfBinOpFlag_IsConstraintCheck) != 0)
					{
						mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), operatorConstraintReturnType);
					}
					else
					{
						SetAndRestoreValue<BfEvalExprFlags> prevFlags(mBfEvalExprFlags, (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_NoAutoComplete));
						mResult = CreateCall(&methodMatcher, BfTypedValue());
					}
					if ((mResult.mType != NULL) && (methodMatcher.mSelfType != NULL) && (mResult.mType->IsSelf()))
					{
						BF_ASSERT(mModule->IsInGeneric());
						mResult = mModule->GetDefaultTypedValue(methodMatcher.mSelfType, false, BfDefaultValueKind_Value);
					}
					if ((invertResult) && (mResult.mType == mModule->GetPrimitiveType(BfTypeCode_Boolean)))
						mResult.mValue = mModule->mBfIRBuilder->CreateNot(mResult.mValue);
					if (pass == 1)
					{
						if (findBinaryOp == BfBinaryOp_Compare)
						{
							mResult = mModule->LoadValue(mResult);
							if (mResult.mType->IsIntegral())
								mResult.mValue = mModule->mBfIRBuilder->CreateNeg(mResult.mValue);
						}
					}

					if ((isComparison) && (matchedOp == BfBinaryOp_Compare))
					{
						auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
						mResult = mModule->LoadValue(mResult);
						if (mResult.mType != intType)
							mResult = mModule->GetDefaultTypedValue(intType);

						auto zeroVal = mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0);

						auto useBinaryOp = binaryOp;
						if (pass == 1)
							useBinaryOp = BfGetFlippedBinaryOp(useBinaryOp);

						auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
						switch (useBinaryOp)
						{
						case BfBinaryOp_Equality:
						case BfBinaryOp_StrictEquality:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(mResult.mValue, zeroVal), boolType);
							break;
						case BfBinaryOp_InEquality:
						case BfBinaryOp_StrictInEquality:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(mResult.mValue, zeroVal), boolType);
							break;
						case BfBinaryOp_GreaterThan:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGT(mResult.mValue, zeroVal, true), boolType);
							break;
						case BfBinaryOp_LessThan:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLT(mResult.mValue, zeroVal, true), boolType);
							break;
						case BfBinaryOp_GreaterThanOrEqual:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGTE(mResult.mValue, zeroVal, true), boolType);
							break;
						case BfBinaryOp_LessThanOrEqual:
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLTE(mResult.mValue, zeroVal, true), boolType);
							break;
						default: break;
						}
					}

					return;
				}

				auto _CheckBinaryOp = [&](BfGenericParamInstance* genericParam)
				{
					for (auto& opConstraint : genericParam->mOperatorConstraints)
					{
						BfType* returnType = genericParam->mExternType;
						bool works = false;
						if (opConstraint.mBinaryOp == findBinaryOp)
						{
							if ((mModule->CanCast(args[0].mTypedValue, opConstraint.mLeftType)) &&
								(mModule->CanCast(args[1].mTypedValue, opConstraint.mRightType)))
							{
								works = true;
							}
						}

						if ((isComparison) && (opConstraint.mBinaryOp == BfBinaryOp_Compare))
						{
							if ((mModule->CanCast(args[0].mTypedValue, opConstraint.mLeftType)) &&
								(mModule->CanCast(args[1].mTypedValue, opConstraint.mRightType)))
							{
								works = true;
							}
							else if ((mModule->CanCast(args[0].mTypedValue, opConstraint.mRightType)) &&
								(mModule->CanCast(args[1].mTypedValue, opConstraint.mLeftType)))
							{
								works = true;
							}

							if (works)
							{
								returnType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
							}
						}

						if (works)
						{
							BF_ASSERT(genericParam->mExternType != NULL);
							mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), returnType);
							return true;
						}
					}

					return false;
				};

				// Check method generic constraints
				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (int genericParamIdx = 0; genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
						if (_CheckBinaryOp(genericParam))
							return;
					}
				}

				// Check type generic constraints
				if ((mModule->mCurTypeInstance->IsGenericTypeInstance()) && (mModule->mCurTypeInstance->IsUnspecializedType()))
				{
					SizedArray<BfGenericParamInstance*, 4> genericParams;
					mModule->GetActiveTypeGenericParamInstances(genericParams);
					for (auto genericParam : genericParams)
					{
						if (_CheckBinaryOp(genericParam))
							return;
					}
				}

				if (pass == 1)
					break;

				auto flippedBinaryOp = BfGetFlippedBinaryOp(findBinaryOp);
				if (flippedBinaryOp != BfBinaryOp_None)
					findBinaryOp = flippedBinaryOp;
			}

			bool resultHandled = false;
			if (((origLeftType != NULL) && (origLeftType->IsIntUnknown())) ||
				((origRightType != NULL) && (origRightType->IsIntUnknown())))
			{
				if (!resultType->IsPrimitiveType())
				{
					BfType* numericCastType = mModule->GetClosestNumericCastType(*resultTypedValue, mExpectingType);
					if (numericCastType != NULL)
					{
						resultHandled = true;
						resultType = numericCastType;
					}
				}
			}

			if (!resultHandled)
			{
				auto prevResultType = resultType;
				if ((leftValue.mType->IsPrimitiveType()) && (!origLeftType->IsIntUnknown()) && (!rightValue.mType->IsTypedPrimitive()))
					resultType = leftValue.mType;
				if ((rightValue.mType->IsPrimitiveType()) && (!origRightType->IsIntUnknown()) && (!leftValue.mType->IsTypedPrimitive()))
					resultType = rightValue.mType;
			}
		}
	}

	if (deferRight)
	{
		auto expectedType = resultType;
		if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
			expectedType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
		rightValue = mModule->CreateValueFromExpression(BfNodeDynCast<BfExpression>(rightExpression), expectedType, (BfEvalExprFlags)((mBfEvalExprFlags & BfEvalExprFlags_InheritFlags) | BfEvalExprFlags_AllowSplat | BfEvalExprFlags_NoCast));
		if (rightValue)
			PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, (BfBinOpFlags)(flags & ~BfBinOpFlag_DeferRight), leftValue, rightValue);
		return;
	}

	if (mModule->IsUnboundGeneric(resultType))
	{
		mResult = mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		return;
	}

	if (resultType->IsPointer() && otherType->IsPointer())
	{
		if ((binaryOp == BfBinaryOp_Add) && (resultType == otherType) &&
			(resultType->GetUnderlyingType() == mModule->GetPrimitiveType(BfTypeCode_Char8)))
		{
			AddStrings(leftValue, rightValue, opToken);
			return;
		}

		//TODO: Allow all pointer comparisons, but only allow SUBTRACTION between equal pointer types
		if ((binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowSubtract))
		{
			if (!mModule->CanCast(*otherTypedValue, resultType))
			{
				mModule->Fail(StrFormat("Operands '%s' and '%s' are not comparable types.",
					mModule->TypeToString(leftValue.mType).c_str(), mModule->TypeToString(rightValue.mType).c_str()),
					opToken);
				return;
			}

			BfPointerType* resultPointerType = (BfPointerType*)resultType;
			BfType* intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			if (resultPointerType->mElementType->mSize == 0)
			{
				if (!mModule->IsInSpecializedSection())
					mModule->Warn(0, "Subtracting pointers to zero-sized elements will always result in zero", opToken);
				mResult = mModule->GetDefaultTypedValue(intPtrType);
			}
			else
			{
				convLeftValue = mModule->CastToValue(leftExpression, leftValue, intPtrType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler));
				convRightValue = mModule->CastToValue(rightExpression, rightValue, intPtrType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler));
				BfIRValue diffValue = mModule->mBfIRBuilder->CreateSub(convLeftValue, convRightValue);
				diffValue = mModule->mBfIRBuilder->CreateDiv(diffValue, mModule->GetConstValue(resultPointerType->mElementType->mSize, intPtrType), true);
				mResult = BfTypedValue(diffValue, intPtrType);
			}
			return;
		}
		else if ((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_StrictEquality) &&
			(binaryOp != BfBinaryOp_InEquality) && (binaryOp != BfBinaryOp_StrictInEquality) &&
			(binaryOp != BfBinaryOp_LessThan) && (binaryOp != BfBinaryOp_LessThanOrEqual) &&
			(binaryOp != BfBinaryOp_GreaterThan) && (binaryOp != BfBinaryOp_GreaterThanOrEqual))
		{
			if (mModule->PreFail())
				mModule->Fail("Invalid operation on pointers", opToken);
			return;
		}

		if ((!BfBinOpEqualityCheck(binaryOp)) || (resultType != otherType))
		{
			resultType = mModule->GetPrimitiveType(BfTypeCode_UIntPtr);
			explicitCast = true;
		}
	}
	else if (resultType->IsPointer())
	{
		if (otherType->IsNull())
		{
			if (!BfBinOpEqualityCheck(binaryOp))
			{
				if (mModule->PreFail())
					mModule->Fail(StrFormat("Invalid operation between '%s' and null", mModule->TypeToString(resultType).c_str()), opToken);
				return;
			}

			if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			else
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNotNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			return;
		}

		// One pointer
		if ((!otherType->IsIntegral()) ||
			((binaryOp != BfBinaryOp_Add) && (binaryOp != BfBinaryOp_Subtract) && (binaryOp != BfBinaryOp_OverflowAdd) && (binaryOp != BfBinaryOp_OverflowSubtract)))
		{
			_OpFail();
			return;
		}

		auto underlyingType = resultType->GetUnderlyingType();

		if ((underlyingType->IsSizedArray()) && (!mModule->IsInSpecializedSection()))
			mModule->Warn(0, "Performing arithmetic on a pointer to a sized array. Consider performing arithmetic on an element pointer if this is not intended.", resultTypeSrc);

		BfIRValue addValue = otherTypedValue->mValue;
		if ((!otherTypedValue->mType->IsSigned()) && (otherTypedValue->mType->mSize < mModule->mSystem->mPtrSize))
			addValue = mModule->mBfIRBuilder->CreateNumericCast(addValue, false, BfTypeCode_UIntPtr);
		if ((binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowSubtract))
		{
			if (resultTypeSrc == rightExpression)
				mModule->Fail("Cannot subtract a pointer from an integer", resultTypeSrc);

			addValue = mModule->mBfIRBuilder->CreateNeg(addValue);
		}

		mModule->PopulateType(underlyingType);
		if (underlyingType->IsValuelessType())
		{
			if (!mModule->IsInSpecializedSection())
				mModule->Warn(0, "Adding to a pointer to a zero-sized element has no effect", opToken);
			mResult = *resultTypedValue;
			return;
		}

		mModule->mBfIRBuilder->PopulateType(underlyingType);
		mResult = BfTypedValue(mModule->CreateIndexedValue(underlyingType, resultTypedValue->mValue, addValue), resultType);
		return;
	}

	if ((resultType->IsFunction()) || (resultType->IsPointer()) || (resultType->IsObject()) || (resultType->IsInterface()) || (resultType->IsGenericParam()))
	{
		if ((binaryOp == BfBinaryOp_Add) &&
			(resultType->IsInstanceOf(mModule->mCompiler->mStringTypeDef)) &&
			(otherType->IsInstanceOf(mModule->mCompiler->mStringTypeDef)))
		{
			AddStrings(leftValue, rightValue, opToken);
			return;
		}

		if (!BfGetBinaryOpPrecendence(binaryOp))
		{
			//mModule->Fail("Invalid operation for objects", opToken);
			_OpFail();
			return;
		}

		if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality) || (binaryOp == BfBinaryOp_InEquality) || (binaryOp == BfBinaryOp_StrictInEquality))
		{
			if (resultType->IsInterface())
			{
				// Compare as objects instead
				resultType = mModule->mContext->mBfObjectType;
					*resultTypedValue = mModule->Cast(resultTypeSrc, *resultTypedValue, resultType);
			}

			if (otherType->IsNull())
			{
				if (resultType->IsFunction())
				{
					if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
				}
				else
				{
					if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNotNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
				}
			}
			else
			{
				auto convertedValue = mModule->Cast(otherTypeSrc, *otherTypedValue, resultType, BfCastFlags_NoBox);
				if (!convertedValue)
					return;
				convertedValue = mModule->LoadValue(convertedValue);
				if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(resultTypedValue->mValue, convertedValue.mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
				else
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(resultTypedValue->mValue, convertedValue.mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			}

			return;
		}
	}

	if (resultType->IsTypedPrimitive())
	{
		bool needsOtherCast = true;
		if (otherType != resultType)
		{
			if ((otherType->IsPrimitiveType()) && (!otherType->IsValuelessType()))
			{
				// Allow zero comparisons to match all typed primitives
				if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
				{
					auto constant = mModule->mBfIRBuilder->GetConstant(otherTypedValue->mValue);
					if ((constant != NULL) && (mModule->mBfIRBuilder->IsInt(constant->mTypeCode)) && (constant->mInt64 == 0))
						needsOtherCast = false;
				}

				// Allow integer offsetting
				if ((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowAdd) || (binaryOp == BfBinaryOp_OverflowSubtract))
				{
					if (otherType->IsIntegral())
						needsOtherCast = false;
				}
			}

			if (needsOtherCast)
			{
				// The only purpose of this cast is to potentially throw a casting error
				BfIRValue otherCastResult = mModule->CastToValue(otherTypeSrc, *otherTypedValue, resultType, explicitCast ? BfCastFlags_Explicit : BfCastFlags_None);
				if (!otherCastResult)
					return;
			}
		}

		auto underlyingType = resultType->GetUnderlyingType();

		if ((binaryOp == BfBinaryOp_Subtract) && (otherTypedValue->mType == resultType))
		{
			intptr maxDist = 0;
			auto resultTypeInstance = resultType->ToTypeInstance();
			if ((resultTypeInstance != NULL) && (resultTypeInstance->mTypeInfoEx != NULL))
				maxDist = resultTypeInstance->mTypeInfoEx->mMaxValue - resultTypeInstance->mTypeInfoEx->mMinValue;

			if (maxDist >= 0x80000000UL)
				resultType = mModule->GetPrimitiveType(BfTypeCode_Int64);
			else if (maxDist >= 0x8000)
				resultType = mModule->GetPrimitiveType(BfTypeCode_Int32);
			else if (maxDist >= 0x80)
				resultType = mModule->GetPrimitiveType(BfTypeCode_Int16);
			else
				resultType = mModule->GetPrimitiveType(BfTypeCode_Int8);
			underlyingType = resultType;
		}

		BfIRValue convResultValue;
		if (resultTypedValue->mType == resultType)
			convResultValue = mModule->LoadValue(*resultTypedValue).mValue;
		else
			convResultValue = mModule->CastToValue(resultTypeSrc, *resultTypedValue, underlyingType, BfCastFlags_Explicit);

		BfIRValue convOtherValue;
		if (otherTypedValue->mType == resultType)
			convOtherValue = mModule->LoadValue(*otherTypedValue).mValue;
		else
			convOtherValue = mModule->CastToValue(otherTypeSrc, *otherTypedValue, underlyingType, BfCastFlags_Explicit);

		if ((!underlyingType->IsValuelessType()) && ((!convResultValue) || (!convOtherValue)))
			return;

		if (resultTypedValue == &leftValue)
			PerformBinaryOperation(underlyingType, convResultValue, convOtherValue, binaryOp, opToken);
		else
			PerformBinaryOperation(underlyingType, convOtherValue, convResultValue, binaryOp, opToken);
		if (mResult.mType == underlyingType)
			mResult.mType = resultType;
		return;
	}

	auto _CallValueTypeEquals = [&]()
	{
		BfModuleMethodInstance moduleMethodInstance;
		auto typeInst = leftValue.mType->ToTypeInstance();
		if (typeInst != NULL)
		{
			if ((binaryOp == BfBinaryOp_StrictEquality) || (binaryOp == BfBinaryOp_StrictInEquality))
				moduleMethodInstance = mModule->GetMethodByName(typeInst, BF_METHODNAME_DEFAULT_STRICT_EQUALS);
			else
				moduleMethodInstance = mModule->GetMethodByName(typeInst, BF_METHODNAME_DEFAULT_EQUALS);
		}
		else
		{
			BF_ASSERT(leftValue.mType->IsSizedArray() || leftValue.mType->IsMethodRef());
			auto valueTypeInst = mModule->ResolveTypeDef(mModule->mCompiler->mValueTypeTypeDef)->ToTypeInstance();
			BfMethodDef* equalsMethodDef = mModule->mCompiler->mValueTypeTypeDef->GetMethodByName("Equals");
			BfTypeVector typeVec;
			typeVec.push_back(leftValue.mType);
			moduleMethodInstance = mModule->GetMethodInstance(valueTypeInst, equalsMethodDef, typeVec);
		}

		if (moduleMethodInstance)
		{
			if ((opToken != NULL) && (!noClassify))
				mModule->SetElementType(opToken, BfSourceElementType_Method);

			SizedArray<BfResolvedArg, 4> argValues;
			BfResolvedArg resolvedArg;
			resolvedArg.mTypedValue = leftValue;
			argValues.push_back(resolvedArg);
			resolvedArg.mTypedValue = rightValue;
			argValues.push_back(resolvedArg);
			mResult = CreateCall(opToken, BfTypedValue(), BfTypedValue(), moduleMethodInstance.mMethodInstance->mMethodDef, moduleMethodInstance, BfCreateCallFlags_None, argValues);
			if ((mResult) &&
				((binaryOp == BfBinaryOp_InEquality) || (binaryOp == BfBinaryOp_StrictInEquality)))
				mResult.mValue = mModule->mBfIRBuilder->CreateNot(mResult.mValue);
			return true;
		}
		return false;
	};

	//if (((leftValue.mType->IsComposite()) || (leftValue.mType->IsObject())))

	if (((resultType->IsComposite()) || (resultType->IsObject())))
	{
		bool areEquivalentTuples = false;
		if ((leftValue.mType->IsTuple()) && (rightValue.mType->IsTuple()))
		{
			auto leftTupleType = (BfTypeInstance*)leftValue.mType;
			auto rightTupleType = (BfTypeInstance*)rightValue.mType;

			// We only do this for tuples, because we would allow an implicit struct
			//  truncation if we allow it for all structs, which would result in only
			//  the base class's fields being compared
			if (mModule->CanCast(rightValue, leftValue.mType))
				rightValue = mModule->Cast(opToken, rightValue, leftValue.mType, BfCastFlags_Explicit);
			else if (mModule->CanCast(leftValue, rightValue.mType))
				leftValue = mModule->Cast(opToken, leftValue, rightValue.mType, BfCastFlags_Explicit);
		}

		if (leftValue.mType == rightValue.mType)
		{
			if (BfBinOpEqualityCheck(binaryOp))
			{
				auto intCoercibleType = mModule->GetIntCoercibleType(leftValue.mType);
				if (intCoercibleType != NULL)
				{
					auto intLHS = mModule->GetIntCoercible(leftValue);
					auto intRHS = mModule->GetIntCoercible(rightValue);
					auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

					if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(intLHS.mValue, intRHS.mValue), boolType);
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(intLHS.mValue, intRHS.mValue), boolType);
					return;
				}

				// Valueless types always compare as 'equal' if we can ensure no members could have an equality operator overload
				if (leftValue.mType->IsComposite())
				{
					mModule->PopulateType(leftValue.mType);
					if (leftValue.mType->IsValuelessType())
					{
						bool mayHaveEqualOverload = false;
						auto leftTypeInst = leftValue.mType->ToTypeInstance();
						if (leftTypeInst != NULL)
						{
							std::function<bool(BfType*)> _HasTypeInstance = [&](BfType* type)
							{
								if (type == NULL)
									return false;

								if (type->IsTypeInstance())
									return true;

								if (type->IsSizedArray())
									return _HasTypeInstance(((BfSizedArrayType*)type)->mElementType);

								return false;
							};

							for (auto& fieldInstance : leftTypeInst->mFieldInstances)
							{
								if (_HasTypeInstance(fieldInstance.mResolvedType))
									mayHaveEqualOverload = true;
							}
						}

						if (!mayHaveEqualOverload)
						{
							auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
							bool isEqual = (binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality);
							mResult = BfTypedValue(mModule->GetConstValue(isEqual ? 1 : 0, boolType), boolType);
							return;
						}
					}
				}

				if (_CallValueTypeEquals())
					return;
			}

			if (PerformBinaryOperation_Numeric(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue, rightValue))
				return;

			if (mModule->PreFail())
			{
				mModule->Fail(StrFormat("Operator '%s' cannot be applied to operands of type '%s'",
					BfGetOpName(binaryOp),
					mModule->TypeToString(leftValue.mType).c_str()), opToken);
			}
			return;
		}
		else
		{
			bool handled = false;

			for (int pass = 0; pass < 2; pass++)
			{
				BfTypedValue& fromValue = (pass == 0) ? leftValue : rightValue;
				BfType* toType = (pass == 0) ? rightValue.mType : leftValue.mType;

				if (mModule->CanCast(fromValue, toType))
				{
					auto result = mModule->Cast(opToken, fromValue, toType);
					if (result)
					{
						resultType = toType;
						fromValue = result;
						handled = true;
						break;
					}
				}
			}

			if (!handled)
			{
				if ((leftValue.mType->IsUndefSizedArray()) || (rightValue.mType->IsUndefSizedArray()))
				{
					if ((leftValue.mType->IsSizedArray()) && (rightValue.mType->IsSizedArray() &&
						(leftValue.mType->GetUnderlyingType() == rightValue.mType->GetUnderlyingType())))
					{
						if (BfBinOpEqualityCheck(binaryOp))
						{
							auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
							mResult = mModule->GetDefaultTypedValue(boolType, false, BfDefaultValueKind_Undef);
							return;
						}
					}
				}

				if (PerformBinaryOperation_Numeric(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue, rightValue))
					return;

				mModule->Fail(StrFormat("Operator '%s' cannot be applied to operands of type '%s' and '%s'",
					BfGetOpName(binaryOp),
					mModule->TypeToString(leftValue.mType).c_str(),
					mModule->TypeToString(rightValue.mType).c_str()), opToken);
				return;
			}
		}
	}

	if (resultType->IsMethodRef() && otherType->IsMethodRef())
	{
		if (BfBinOpEqualityCheck(binaryOp))
		{
			auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

			BfMethodRefType* lhsMethodRefType = (BfMethodRefType*)leftValue.mType;
			BfMethodRefType* rhsMethodRefType = (BfMethodRefType*)rightValue.mType;
			if (lhsMethodRefType->mMethodRef != rhsMethodRefType->mMethodRef)
			{
				mResult = BfTypedValue(mModule->GetConstValue(((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality)) ? 0 : 1, boolType), boolType);
				return;
			}

			if (_CallValueTypeEquals())
				return;
		}
	}

	if (resultType->IsIntegral())
	{
		if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
		{
			if (rightValue.mValue.IsConst())
			{
				auto constVal = mModule->mBfIRBuilder->GetConstant(rightValue.mValue);
				if ((constVal->mInt64 < 0) || (constVal->mInt64 >= 8 * resultType->mSize))
				{
					mModule->Fail(StrFormat("Shift value '%lld' is out of range for type '%s'", constVal->mInt64, mModule->TypeToString(resultType).c_str()), opToken);
				}
			}
		}

		// We're trying a simplified scheme that doesn't always try to up-convert into an 'int'
		if (leftValue.mType != rightValue.mType)
		{
			bool isBitwiseExpr =
				(binaryOp == BfBinaryOp_BitwiseAnd) ||
				(binaryOp == BfBinaryOp_BitwiseOr) ||
				(binaryOp == BfBinaryOp_ExclusiveOr) ||
				(binaryOp == BfBinaryOp_LeftShift) ||
				(binaryOp == BfBinaryOp_RightShift) ||
				(binaryOp == BfBinaryOp_Equality) ||
				(binaryOp == BfBinaryOp_InEquality) ||
				(binaryOp == BfBinaryOp_StrictEquality) ||
				(binaryOp == BfBinaryOp_StrictInEquality);

			if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
			{
				// For shifts we have more lenient rules - shifts are naturally limited so any int type is equally valid
				if (rightValue.mType->IsIntegral())
					explicitCast = true;
			}
			else if (((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowAdd) || (binaryOp == BfBinaryOp_OverflowSubtract)) && (resultType->IsChar()) && (otherType->IsInteger()))
			{
				// charVal += intVal;
				explicitCast = true;
			}
			else if ((!resultType->IsSigned()) && (otherType->IsSigned()))
			{
				if (mModule->CanCast(*otherTypedValue, resultType))
				{
					// If we can convert the 'other' value implicitly then it's a convertible literal, leave as uint
				}
				else
				{
					mModule->Fail(StrFormat("Operator cannot be applied to operands of type '%s' and '%s'",
						mModule->TypeToString(leftValue.mType).c_str(),
						mModule->TypeToString(rightValue.mType).c_str()), opToken);
					return;
				}
			}
			else if ((isBitwiseExpr) && (otherType->IsIntegral()) && (resultType->mSize == otherType->mSize))
			{
				// Forget about signed/unsigned mismatches for bitwise operations
				explicitCast = true;
			}
			else
			{
				if (((binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowSubtract)) &&
					(resultType->IsChar()) && (otherType->IsChar()))
				{
					// "wchar - char" subtraction will always fit into int32, because of unicode range
					resultType = mModule->GetPrimitiveType(BfTypeCode_Int32);
					explicitCast = true;
				}
				else if ((otherType->IsChar()) &&
					((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowAdd) || (binaryOp == BfBinaryOp_OverflowSubtract)))
				{
					mModule->Fail(StrFormat("Cannot perform operation between types '%s' and '%s'",
						mModule->TypeToString(leftValue.mType).c_str(),
						mModule->TypeToString(rightValue.mType).c_str()), opToken);
				}
			}
		}
		else if ((!resultType->IsSigned()) && (binaryOp == BfBinaryOp_Subtract) && (!forceLeftType))
		{
			if ((mExpectingType == NULL) || (mExpectingType->IsSigned()) || (resultType->IsChar()))
			{
				if ((resultType->IsChar()) && (resultType->mSize == 4))
				{
					// "wchar - wchar" subtraction will always fit into int32, because of unicode range
					resultType = mModule->GetPrimitiveType(BfTypeCode_Int32);
				}
				else
				{
					// The result of uint8 - uint8 is int16 (for example)
					switch (resultType->mSize)
					{
					case 1:
						resultType = mModule->GetPrimitiveType(BfTypeCode_Int16);
						break;
					case 2:
						resultType = mModule->GetPrimitiveType(BfTypeCode_Int32);
						break;
					case 4:
						resultType = mModule->GetPrimitiveType(BfTypeCode_Int64);
						break;
					}
				}
				explicitCast = true;
			}
		}
		else if (resultType->IsChar())
		{
			bool canDoOp =
				(binaryOp == BfBinaryOp_BitwiseAnd) ||
				(binaryOp == BfBinaryOp_BitwiseOr) ||
				((binaryOp >= BfBinaryOp_Equality) && (binaryOp <= BfBinaryOp_Compare));
			if (!canDoOp)
			{
				mModule->Fail(StrFormat("Cannot perform operation on type '%s'", mModule->TypeToString(resultType).c_str()), opToken);
				return;
			}
		}
	}

	if (!convLeftValue)
		convLeftValue = mModule->CastToValue(leftExpression, leftValue, resultType,
			explicitCast ? (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler) : BfCastFlags_None);
	if (!convRightValue)
		convRightValue = mModule->CastToValue(rightExpression, rightValue, resultType,
			explicitCast ? (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler) : BfCastFlags_None);

	PerformBinaryOperation(resultType, convLeftValue, convRightValue, binaryOp, opToken);
}

void BfExprEvaluator::PerformBinaryOperation(BfType* resultType, BfIRValue convLeftValue, BfIRValue convRightValue, BfBinaryOp binaryOp, BfAstNode* opToken)
{
	if (resultType->IsValuelessType())
	{
		switch (binaryOp)
		{
		case BfBinaryOp_Equality:
		case BfBinaryOp_StrictEquality:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1),
				mModule->GetPrimitiveType(BfTypeCode_Boolean));
			return;
		case BfBinaryOp_InEquality:
		case BfBinaryOp_StrictInEquality:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0),
				mModule->GetPrimitiveType(BfTypeCode_Boolean));
			return;
		default:
			break;
		}
	}

	if ((!convLeftValue) || (!convRightValue))
		return;

	if (resultType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)resultType;
		if (primType->mTypeDef->mTypeCode == BfTypeCode_Boolean)
		{
			bool passThrough = false;
			switch (binaryOp)
			{
			case BfBinaryOp_Equality:
			case BfBinaryOp_StrictEquality:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_InEquality:
			case BfBinaryOp_StrictInEquality:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_BitwiseAnd:
			case BfBinaryOp_ConditionalAnd:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateAnd(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_BitwiseOr:
			case BfBinaryOp_ConditionalOr:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateOr(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_ExclusiveOr:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateXor(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_Compare:
				passThrough = true;
				break;
			default:
				if (mModule->PreFail())
					mModule->Fail("Invalid operation for booleans", opToken);
				break;
			}
			if (!passThrough)
				return;
		}
	}

	if ((!resultType->IsIntegralOrBool()) && (!resultType->IsFloat()))
	{
		if (mModule->PreFail())
			mModule->Fail(StrFormat("Cannot perform operation on type '%s'", mModule->TypeToString(resultType).c_str()), opToken);
		return;
	}

	if (resultType->IsIntegral())
	{
		switch (binaryOp)
		{
		case BfBinaryOp_BitwiseAnd:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateAnd(convLeftValue, convRightValue), resultType);
			return;
		case BfBinaryOp_BitwiseOr:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateOr(convLeftValue, convRightValue), resultType);
			return;
		case BfBinaryOp_ExclusiveOr:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateXor(convLeftValue, convRightValue), resultType);
			return;
		case BfBinaryOp_LeftShift:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateShl(convLeftValue, convRightValue), resultType);
			mModule->CheckRangeError(resultType, opToken);
			return;
		case BfBinaryOp_RightShift:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateShr(convLeftValue, convRightValue, resultType->IsSigned()), resultType);
			return;
		default: break;
		}
	}

	if ((resultType->IsChar()) &&
		((binaryOp == BfBinaryOp_Multiply) ||
		 (binaryOp == BfBinaryOp_OverflowMultiply) ||
		 (binaryOp == BfBinaryOp_Divide) ||
		 (binaryOp == BfBinaryOp_Modulus)))
	{
		mModule->Fail(StrFormat("Cannot perform operation on type '%s'", mModule->TypeToString(resultType).c_str()), opToken);
		return;
	}

	auto _GetOverflowKind = [&](bool wantOverflow)
	{
		if (resultType->IsFloat())
			return BfOverflowCheckKind_None;
		if (!wantOverflow)
			return BfOverflowCheckKind_None;
		if (mModule->GetDefaultCheckedKind() != BfCheckedKind_Checked)
			return BfOverflowCheckKind_None;

		bool arithmeticChecks = mModule->mCompiler->mOptions.mArithmeticChecks;
		auto typeOptions = mModule->GetTypeOptions();
		if (typeOptions != NULL)
			arithmeticChecks = typeOptions->Apply(arithmeticChecks, BfOptionFlags_ArithmeticCheck);
		if (!arithmeticChecks)
			return BfOverflowCheckKind_None;

		BfOverflowCheckKind overflowCheckKind = (resultType->IsSigned()) ? BfOverflowCheckKind_Signed : BfOverflowCheckKind_Unsigned;
		if (!mModule->IsOptimized())
			overflowCheckKind = (BfOverflowCheckKind)(overflowCheckKind | BfOverflowCheckKind_Flag_UseAsm);
		return overflowCheckKind;
	};

	switch (binaryOp)
	{
	case BfBinaryOp_Add:
	case BfBinaryOp_OverflowAdd:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateAdd(convLeftValue, convRightValue, _GetOverflowKind(binaryOp == BfBinaryOp_Add)), resultType);
		if (binaryOp != BfBinaryOp_OverflowAdd)
			mModule->CheckRangeError(resultType, opToken);
		break;
	case BfBinaryOp_Subtract:
	case BfBinaryOp_OverflowSubtract:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateSub(convLeftValue, convRightValue, _GetOverflowKind(binaryOp == BfBinaryOp_Subtract)), resultType);
		if (binaryOp != BfBinaryOp_OverflowSubtract)
			mModule->CheckRangeError(resultType, opToken);
		break;
	case BfBinaryOp_Multiply:
	case BfBinaryOp_OverflowMultiply:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateMul(convLeftValue, convRightValue, _GetOverflowKind(binaryOp == BfBinaryOp_Multiply)), resultType);
		if (binaryOp != BfBinaryOp_OverflowMultiply)
			mModule->CheckRangeError(resultType, opToken);
		break;
	case BfBinaryOp_Divide:
		{
			bool isZero = false;
			if (convRightValue.IsConst())
			{
				auto constVal = mModule->mBfIRBuilder->GetConstant(convRightValue);
				if (BfIRBuilder::IsInt(constVal->mTypeCode))
					isZero = constVal->mInt64 == 0;
			}
			if (isZero)
			{
				mModule->Fail("Divide by zero", opToken);
				mResult = mModule->GetDefaultTypedValue(resultType);
			}
			else
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateDiv(convLeftValue, convRightValue, resultType->IsSigned()), resultType);
		}
		break;
	case BfBinaryOp_Modulus:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateRem(convLeftValue, convRightValue, resultType->IsSigned()), resultType);
		break;
	case BfBinaryOp_Equality:
	case BfBinaryOp_StrictEquality:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(convLeftValue, convRightValue),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_InEquality:
	case BfBinaryOp_StrictInEquality:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(convLeftValue, convRightValue),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_LessThan:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSigned()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_LessThanOrEqual:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLTE(convLeftValue, convRightValue, resultType->IsSigned()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_GreaterThan:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSigned()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_GreaterThanOrEqual:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGTE(convLeftValue, convRightValue, resultType->IsSigned()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));
		break;
	case BfBinaryOp_Compare:
		{
			auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			if ((convLeftValue.IsConst()) && (convRightValue.IsConst()))
			{
				auto cmpLtVal = mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSigned());
				auto ltConstant = mModule->mBfIRBuilder->GetConstant(cmpLtVal);
				if (ltConstant->mBool)
				{
					mResult = BfTypedValue(mModule->GetConstValue(-1, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
				}
				else
				{
					auto cmpGtVal = mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSigned());
					auto rtConstant = mModule->mBfIRBuilder->GetConstant(cmpGtVal);
					if (rtConstant->mBool)
						mResult = BfTypedValue(mModule->GetConstValue(1, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
					else
						mResult = BfTypedValue(mModule->GetConstValue(0, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
				}
			}
			else if ((resultType->IsIntegralOrBool()) && (resultType->mSize < intType->mSize))
			{
				auto leftIntValue = mModule->mBfIRBuilder->CreateNumericCast(convLeftValue, resultType->IsSigned(), BfTypeCode_IntPtr);
				auto rightIntValue = mModule->mBfIRBuilder->CreateNumericCast(convRightValue, resultType->IsSigned(), BfTypeCode_IntPtr);
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateSub(leftIntValue, rightIntValue), intType);
			}
			else
			{
				BfIRBlock checkGtBlock = mModule->mBfIRBuilder->CreateBlock("cmpCheckGt");
				BfIRBlock eqBlock = mModule->mBfIRBuilder->CreateBlock("cmpEq");
				BfIRBlock endBlock = mModule->mBfIRBuilder->CreateBlock("cmpEnd");

				auto startBlock = mModule->mBfIRBuilder->GetInsertBlock();

				auto cmpLtVal = mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSigned());
				mModule->mBfIRBuilder->CreateCondBr(cmpLtVal, endBlock, checkGtBlock);

				mModule->mBfIRBuilder->AddBlock(checkGtBlock);
				mModule->mBfIRBuilder->SetInsertPoint(checkGtBlock);
				auto cmpGtVal = mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSigned());
				mModule->mBfIRBuilder->CreateCondBr(cmpGtVal, endBlock, eqBlock);

				mModule->mBfIRBuilder->AddBlock(eqBlock);
				mModule->mBfIRBuilder->SetInsertPoint(eqBlock);
				mModule->mBfIRBuilder->CreateBr(endBlock);

				mModule->mBfIRBuilder->AddBlock(endBlock);
				mModule->mBfIRBuilder->SetInsertPoint(endBlock);
				auto phiVal = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(intType), 3);
				mModule->mBfIRBuilder->AddPhiIncoming(phiVal, mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, -1), startBlock);
				mModule->mBfIRBuilder->AddPhiIncoming(phiVal, mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1), checkGtBlock);
				mModule->mBfIRBuilder->AddPhiIncoming(phiVal, mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), eqBlock);

				mResult = BfTypedValue(phiVal, intType);
			}
		}
		break;
	default:
		if (mModule->PreFail())
			mModule->Fail("Invalid operation", opToken);
		break;
	}
}

void BfExprEvaluator::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	BfAutoParentNodeEntry autoParentNodeEntry(mModule, binOpExpr);

	// There are a few binary operations that could actually be casts followed by an unary operation
	// We can't determine that until we know whether the identifier in the parens is a typename or not
	// (double)-1.0  (intptr)&val  (BaseStruct)*val
 	BfUnaryOp unaryOp = BfUnaryOp_None;
	switch (binOpExpr->mOp)
	{
	case BfBinaryOp_Add: unaryOp = BfUnaryOp_Positive; break;
	case BfBinaryOp_Subtract: unaryOp = BfUnaryOp_Negate; break;
	case BfBinaryOp_Multiply: unaryOp = BfUnaryOp_Dereference; break;
	case BfBinaryOp_BitwiseAnd: unaryOp = BfUnaryOp_AddressOf; break;
	default: break;
	}

	if (unaryOp != BfUnaryOp_None)
	{
		if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(binOpExpr->mLeft))
		{
			if (auto castTypeExpr = BfNodeDynCast<BfIdentifierNode>(parenExpr->mExpression))
			{
				SetAndRestoreValue<bool> prevIgnoreError(mModule->mIgnoreErrors, true);
				auto resolvedType = mModule->ResolveTypeRef(castTypeExpr, NULL);
				prevIgnoreError.Restore();

				if (resolvedType != NULL)
				{
					if (auto rightBinOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>(binOpExpr->mRight))
					{
						int leftPrecedence = BfGetBinaryOpPrecendence(binOpExpr->mOp);
						int rightPrecedence = BfGetBinaryOpPrecendence(rightBinOpExpr->mOp);
						// Do we have a precedence order issue due to mis-parsing this?
						//  An example is: "(int)-5.5 * 10"
						if (rightPrecedence > leftPrecedence)
						{
							mModule->FailAfter("Cast target must be wrapped in parentheses", binOpExpr->mLeft);
						}
					}

					PerformUnaryOperation(binOpExpr->mRight, unaryOp, binOpExpr->mOpToken, BfUnaryOpFlag_None);
					if (mResult)
					{
						mResult = mModule->LoadValue(mResult);
						mResult = mModule->Cast(binOpExpr, mResult, resolvedType, BfCastFlags_Explicit);
					}
					return;
				}
			}
		}
	}

	if ((binOpExpr->mOp == BfBinaryOp_LeftShift) || (binOpExpr->mOp == BfBinaryOp_RightShift) ||
		(binOpExpr->mOp == BfBinaryOp_BitwiseAnd) || (binOpExpr->mOp == BfBinaryOp_BitwiseOr) ||
		(binOpExpr->mOp == BfBinaryOp_ExclusiveOr))
	{
		for (int side = 0; side < 2; side++)
		{
			if (auto innerBinOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>((side == 0) ? binOpExpr->mLeft : binOpExpr->mRight))
			{
				if ((innerBinOpExpr->mOp == BfBinaryOp_Add) || (innerBinOpExpr->mOp == BfBinaryOp_Subtract))
				{
					mModule->Warn(BfWarning_C4554_PossiblePrecedenceError, "Check operator precedence for possible error. Consider using parentheses to clarify precedence", innerBinOpExpr);
				}
			}
		}
	}

	if (binOpExpr->mRight == NULL)
	{
		// We visit the children for autocompletion only
		if (binOpExpr->mLeft != NULL)
			VisitChild(binOpExpr->mLeft);

		if (mResult)
		{
			auto autoComplete = GetAutoComplete();
			if (autoComplete != NULL)
				autoComplete->CheckEmptyStart(binOpExpr->mOpToken, mResult.mType);
		}

		if (binOpExpr->mRight != NULL)
			VisitChild(binOpExpr->mRight);
		return;
	}

	PerformBinaryOperation(binOpExpr->mLeft, binOpExpr->mRight, binOpExpr->mOp, binOpExpr->mOpToken, BfBinOpFlag_None);
}