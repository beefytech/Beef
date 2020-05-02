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
#include "BfResolvePass.h"
#include "BfUtil.h"
#include "BfDeferEvalChecker.h"
#include "BfVarDeclChecker.h"

#pragma warning(pop)

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

BfMethodMatcher::BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, const StringImpl& methodName, SizedArrayImpl<BfResolvedArg>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments) :
	mArguments(arguments)
{
	mTargetSrc = targetSrc;
	mModule = module;
	mMethodName = methodName;
	Init(/*arguments, */methodGenericArguments);
}

BfMethodMatcher::BfMethodMatcher(BfAstNode* targetSrc, BfModule* module, BfMethodInstance* interfaceMethodInstance, SizedArrayImpl<BfResolvedArg>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments) :
	mArguments(arguments)
{
	mTargetSrc = targetSrc;
	mModule = module;
	Init(/*arguments, */methodGenericArguments);	
	mInterfaceMethodInstance = interfaceMethodInstance;
	mMethodName = mInterfaceMethodInstance->mMethodDef->mName;
}

void BfMethodMatcher::Init(/*SizedArrayImpl<BfResolvedArg>& arguments, */BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments)
{
	//mArguments = arguments;	
	mActiveTypeDef = NULL;
	mBestMethodDef = NULL;	
	mBackupMethodDef = NULL;
	mBestMethodTypeInstance = NULL;
	mExplicitInterfaceCheck = NULL;
	mSelfType = NULL;
	mMethodType = BfMethodType_Normal;
	mHadExplicitGenericArguments = false;
	mHasVarArguments = false;
	mInterfaceMethodInstance = NULL;
	mFakeConcreteTarget = false;
	mBypassVirtual = false;
	mAllowStatic = true;
	mAllowNonStatic = true;
	mSkipImplicitParams = false;
	mAllowImplicitThis = false;
	mMethodCheckCount = 0;
	mInferGenericProgressIdx = 0;
	mCheckedKind = BfCheckedKind_NotSet;
	mMatchFailKind = MatchFailKind_None;

	for (auto& arg : mArguments)
	{
		auto bfType = arg.mTypedValue.mType;
		if (bfType != NULL)
			mHasVarArguments |= bfType->IsVar();
	}

	if (methodGenericArguments != NULL)
	{
		for (BfTypeReference* genericArg : *methodGenericArguments)
		{
			auto genericArgType = mModule->ResolveTypeRef(genericArg);
// 			if (genericArgType == NULL)
// 				return;
			mExplicitMethodGenericArguments.push_back(genericArgType);
		}
		mHadExplicitGenericArguments = true;
	}
}

bool BfMethodMatcher::IsMemberAccessible(BfTypeInstance* typeInst, BfTypeDef* declaringType)
{
	if (mActiveTypeDef == NULL)
		mActiveTypeDef = mModule->GetActiveTypeDef();
	if (!typeInst->IsTypeMemberIncluded(declaringType, mActiveTypeDef, mModule))
		return false;

	// This may not be completely correct - BUT if we don't have this then even Dictionary TKey's operator == won't be considered accessible
	if (!mModule->IsInSpecializedSection())
	{
		if (!typeInst->IsTypeMemberAccessible(declaringType, mActiveTypeDef))
			return false;
	}
	return true;
}

bool BfMethodMatcher::InferGenericArgument(BfMethodInstance* methodInstance, BfType* argType, BfType* wantType, BfIRValue argValue, HashSet<BfType*>& checkedTypeSet)
{
	if (argType == NULL)
		return false;
	if (argType->IsVar())
		return false;
	
	if (!wantType->IsUnspecializedType())
		return true;

	bool alreadyChecked = false;
	auto _AddToCheckedSet = [](BfType* type, HashSet<BfType*>& checkedTypeSet, bool& alreadyChecked)
	{
		if (alreadyChecked)
			return true;
		alreadyChecked = true;
		return checkedTypeSet.Add(type);
	};

	if (wantType->IsGenericParam())
	{
		auto wantGenericParam = (BfGenericParamType*)wantType;

		BfType* methodGenericTypeConstraint = NULL;
		auto _SetGeneric = [&]()
		{	
			BF_ASSERT((argType == NULL) || (!argType->IsVar()));

			if (argType != NULL)
			{
				// Disallow illegal types
				if (argType->IsRef())
					return;
			}

			if (mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] != argType)
			{
				if (methodGenericTypeConstraint != NULL)
				{
					if (methodGenericTypeConstraint->IsGenericTypeInstance())
					{						
						auto wantGenericType = (BfGenericTypeInstance*)methodGenericTypeConstraint;

						auto checkArgType = argType;
						while (checkArgType != NULL)
						{
							if (checkArgType->IsGenericTypeInstance())
							{
								auto argGenericType = (BfGenericTypeInstance*)checkArgType;
								if (argGenericType->mTypeDef == wantGenericType->mTypeDef)
								{
									for (int genericArgIdx = 0; genericArgIdx < (int)argGenericType->mTypeGenericArguments.size(); genericArgIdx++)
										InferGenericArgument(methodInstance, argGenericType->mTypeGenericArguments[genericArgIdx], wantGenericType->mTypeGenericArguments[genericArgIdx], BfIRValue(), checkedTypeSet);
								}
							}
							else if (checkArgType->IsSizedArray())
							{
								auto sizedArrayType = (BfSizedArrayType*)checkArgType;
								if (wantGenericType->mTypeDef == mModule->mCompiler->mSizedArrayTypeDef)
								{
									InferGenericArgument(methodInstance, sizedArrayType->mElementType, wantGenericType->mTypeGenericArguments[0], BfIRValue(), checkedTypeSet);
									auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
									BfTypedValue arraySize = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)sizedArrayType->mElementCount), intType);
									InferGenericArgument(methodInstance, mModule->CreateConstExprValueType(arraySize), wantGenericType->mTypeGenericArguments[1], BfIRValue(), checkedTypeSet);
								}
							}
							else if (checkArgType->IsPointer())
							{
								auto pointerType = (BfPointerType*)checkArgType;
								if (wantGenericType->mTypeDef == mModule->mCompiler->mPointerTTypeDef)
								{
									InferGenericArgument(methodInstance, pointerType->mElementType, wantGenericType->mTypeGenericArguments[0], BfIRValue(), checkedTypeSet);
								}
							}

							auto checkTypeInst = checkArgType->ToTypeInstance();
							if ((checkTypeInst == NULL) || (!checkTypeInst->mHasParameterizedBase))
								break;
							checkArgType = checkTypeInst->mBaseType;
						}
					}
				}
				
				mInferGenericProgressIdx++;
				mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = argType;
			}
			mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = argType;
			mPrevArgValues[wantGenericParam->mGenericParamIdx] = argValue;			
		};
		
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
					argType = genericParamInst->mTypeConstraint;
				}
			}

			auto prevGenericMethodArg = mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx];
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
			mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = NULL;
			return false;
		}
		return true;
	}

	if ((wantType->IsGenericTypeInstance()) && (wantType->IsUnspecializedTypeVariation()))
	{
		auto wantGenericType = (BfGenericTypeInstance*)wantType;
		if (!argType->IsGenericTypeInstance())
			return true;		
		auto argGenericType = (BfGenericTypeInstance*)argType;
		if (argGenericType->mTypeDef != wantGenericType->mTypeDef)
			return true;
		
		for (int genericArgIdx = 0; genericArgIdx < (int)argGenericType->mTypeGenericArguments.size(); genericArgIdx++)
		{
			BfType* wantGenericArgument = wantGenericType->mTypeGenericArguments[genericArgIdx];
			if (!wantGenericArgument->IsUnspecializedType())
				continue;
			if (!_AddToCheckedSet(argType, checkedTypeSet, alreadyChecked))
				return true;
			InferGenericArgument(methodInstance, argGenericType->mTypeGenericArguments[genericArgIdx], wantGenericArgument, BfIRValue(), checkedTypeSet);
		}
		return true;
	}

	if (wantType->IsRef())
	{
		auto wantRefType = (BfRefType*)wantType;
		if (!argType->IsRef())
		{
			// Match to non-ref
			InferGenericArgument(methodInstance, argType, wantRefType->mElementType, BfIRValue(), checkedTypeSet);
			return true;
		}		
		auto argRefType = (BfRefType*)argType;
		//TODO: We removed this check so we still infer even if we have the wrong ref kind
		//if (wantRefType->mRefKind != argRefType->mRefKind)
			//return true;
		InferGenericArgument(methodInstance, argRefType->mElementType, wantRefType->mElementType, BfIRValue(), checkedTypeSet);
		return true;
	}

	if (wantType->IsPointer())
	{
		if (!argType->IsPointer())
			return true;
		auto wantPointerType = (BfPointerType*) wantType;
		auto argPointerType = (BfPointerType*) argType;		
		InferGenericArgument(methodInstance, argPointerType->mElementType, wantPointerType->mElementType, BfIRValue(), checkedTypeSet);
		return true;
	}

	if (wantType->IsUnknownSizedArray())
	{
		auto wantArrayType = (BfUnknownSizedArrayType*)wantType;		
		if (argType->IsUnknownSizedArray())
		{
			auto argArrayType = (BfUnknownSizedArrayType*)argType;
			InferGenericArgument(methodInstance, argArrayType->mElementCountSource, wantArrayType->mElementCountSource, BfIRValue(), checkedTypeSet);
		}
		else if (argType->IsSizedArray())
		{
			auto argArrayType = (BfSizedArrayType*)argType;
			BfTypedValue sizeValue(mModule->GetConstValue(argArrayType->mElementCount), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
			auto sizedType = mModule->CreateConstExprValueType(sizeValue);
			InferGenericArgument(methodInstance, sizedType, wantArrayType->mElementCountSource, BfIRValue(), checkedTypeSet);
		}
	}

	if (wantType->IsSizedArray())
	{		
		if (argType->IsSizedArray())			
		{
			auto wantArrayType = (BfSizedArrayType*)wantType;
			auto argArrayType = (BfSizedArrayType*)argType;
			InferGenericArgument(methodInstance, argArrayType->mElementType, wantArrayType->mElementType, BfIRValue(), checkedTypeSet);
		}
	}

	if ((wantType->IsDelegate()) || (wantType->IsFunction()))
	{
		if (((argType->IsDelegate()) || (argType->IsFunction())) &&
			(wantType->IsDelegate() == argType->IsDelegate()))
		{
			if (!_AddToCheckedSet(argType, checkedTypeSet, alreadyChecked))
				return true;

			auto argInvokeMethod = mModule->GetRawMethodByName(argType->ToTypeInstance(), "Invoke");
			auto wantInvokeMethod = mModule->GetRawMethodByName(wantType->ToTypeInstance(), "Invoke");
			
			if ((argInvokeMethod != NULL) && (wantInvokeMethod != NULL) && (argInvokeMethod->GetParamCount() == wantInvokeMethod->GetParamCount()))
			{
				InferGenericArgument(methodInstance, argInvokeMethod->mReturnType, wantInvokeMethod->mReturnType, BfIRValue(), checkedTypeSet);
				for (int argIdx = 0; argIdx < (int)argInvokeMethod->GetParamCount(); argIdx++)				
					InferGenericArgument(methodInstance, argInvokeMethod->GetParamType(argIdx), wantInvokeMethod->GetParamType(argIdx), BfIRValue(), checkedTypeSet);
			}
		}
	}
	
	return true;
}

void BfMethodMatcher::CompareMethods(BfMethodInstance* prevMethodInstance, BfTypeVector* prevGenericArgumentsSubstitute,
	BfMethodInstance* newMethodInstance, BfTypeVector* genericArgumentsSubstitute,
	bool* outNewIsBetter, bool* outNewIsWorse, bool allowSpecializeFail)
{
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
	
	int newImplicitParamCount = newMethodInstance->GetImplicitParamCount();
	if (newMethodInstance->mMethodDef->mHasAppend)
		newImplicitParamCount++;
	int prevImplicitParamCount = prevMethodInstance->GetImplicitParamCount();
	if (prevMethodInstance->mMethodDef->mHasAppend)
		prevImplicitParamCount++;

	bool hadEnoughArgs = newMethodInstance->GetParamCount() - newImplicitParamCount < (int)mArguments.size();
	bool prevHadEnoughArgs = prevMethodInstance->GetParamCount() - prevImplicitParamCount < (int)mArguments.size();
	RETURN_BETTER_OR_WORSE(hadEnoughArgs, prevHadEnoughArgs);

	bool chainSkip = (newMethodInstance->mChainType == BfMethodChainType_ChainMember) || (newMethodInstance->mChainType == BfMethodChainType_ChainSkip);
	bool prevChainSkip = (prevMethodInstance->mChainType == BfMethodChainType_ChainMember) || (prevMethodInstance->mChainType == BfMethodChainType_ChainSkip);
	RETURN_BETTER_OR_WORSE(!chainSkip, !prevChainSkip);

	// If one of these methods is local to the current extension then choose that one
	auto activeDef = mModule->GetActiveTypeDef();
	RETURN_BETTER_OR_WORSE(newMethodDef->mDeclaringType == activeDef, prevMethodDef->mDeclaringType == activeDef);
	RETURN_BETTER_OR_WORSE(newMethodDef->mDeclaringType->IsExtension(), prevMethodDef->mDeclaringType->IsExtension());

	if ((!isBetter) && (!isWorse))
	{
		bool betterByGenericParam = false;
		bool worseByGenericParam = false;

		for (argIdx = 0; argIdx < (int)mArguments.size(); argIdx++)
		{
			BfResolvedArg resolvedArg = mArguments[argIdx];;
			BfTypedValue arg = resolvedArg.mTypedValue;

			int newArgIdx = argIdx + newImplicitParamCount;
			int prevArgIdx = argIdx + prevImplicitParamCount;

			bool wasGenericParam = newMethodInstance->WasGenericParam(newArgIdx);
			bool prevWasGenericParam = prevMethodInstance->WasGenericParam(prevArgIdx);

			BfType* paramType = newMethodInstance->GetParamType(newArgIdx);
			BfType* prevParamType = prevMethodInstance->GetParamType(prevArgIdx);

			numUsedParams++;
			prevNumUsedParams++;

			BfType* origParamType = paramType;
			BfType* origPrevParamType = prevParamType;

			if ((genericArgumentsSubstitute != NULL) && (paramType->IsUnspecializedType()))
				paramType = mModule->ResolveGenericType(paramType, *genericArgumentsSubstitute, allowSpecializeFail);
			if ((prevGenericArgumentsSubstitute != NULL) && (prevParamType->IsUnspecializedType()))
				prevParamType = mModule->ResolveGenericType(prevParamType, *prevGenericArgumentsSubstitute, allowSpecializeFail);

			if ((wasGenericParam) || (prevWasGenericParam))
			{
				if ((wasGenericParam) && (!prevWasGenericParam))
					worseByGenericParam = true;
				else if ((!wasGenericParam) && (prevWasGenericParam))
					betterByGenericParam = true;
			}
						
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

				if ((!isBetter) && (!isWorse))
					SET_BETTER_OR_WORSE(paramType->IsConstExprValue(), prevParamType->IsConstExprValue());

				if ((!isBetter) && (!isWorse) && (!isUnspecializedParam) && (!isPrevUnspecializedParam))
				{
					SET_BETTER_OR_WORSE((paramType != NULL) && (!paramType->IsUnspecializedType()),
						(prevParamType != NULL) && (!prevParamType->IsUnspecializedType()));
					if ((!isBetter) && (!isWorse))
					{
						// The resolved argument type may actually match for both considered functions. IE:
						// Method(int8 val) and Method(int16 val) called with Method(0) will create arguments that match their param types
						if ((paramType == arg.mType) && (prevParamType != resolvedArg.mBestBoundType))
							isBetter = true;
						else if ((prevParamType == arg.mType) && (paramType != arg.mType))
							isWorse = true;
						else
						{
							bool canCastFromCurToPrev = mModule->CanCast(mModule->GetFakeTypedValue(paramType), prevParamType);
							bool canCastFromPrevToCur = mModule->CanCast(mModule->GetFakeTypedValue(prevParamType), paramType);

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
						}
						

						/*if ((paramType->IsIntegral()) && (prevParamType->IsIntegral()))
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
						else
						{
							if (mModule->CanCast(mModule->GetFakeTypedValue(paramType), prevParamType))
								isBetter = true;
							if (mModule->CanCast(mModule->GetFakeTypedValue(prevParamType), paramType))
								isWorse = true;
						}*/
					}
				}
			}
			else if ((wasGenericParam) || (prevWasGenericParam))
			{
				if (!wasGenericParam)
					isBetter = true;
				else if (!prevWasGenericParam)
					isWorse = true;
			}

			if (newMethodInstance->GetParamKind(newArgIdx) == BfParamKind_Params)
				usedExtendedForm = true;
			if (prevMethodInstance->GetParamKind(prevArgIdx) == BfParamKind_Params)
				prevUsedExtendedForm = true;

			if ((usedExtendedForm) || (prevUsedExtendedForm))
				break;
		}

		if ((!isBetter) && (!isWorse))
		{
			isBetter = betterByGenericParam;
			isWorse = worseByGenericParam;
		}

		if ((isBetter) || (isWorse))
		{
			RETURN_RESULTS;
		}
	}

	// Check for unused extended params as next param - that still counts as using extended form	
	usedExtendedForm = newMethodInstance->HasParamsArray();	
	prevUsedExtendedForm = prevMethodInstance->HasParamsArray();	

	// Non-generic is better than generic.
	//  The only instance where we can disambiguate is when we have the same number of generic arguments, and the
	//  constraints for one method's generic argument is a superset of another's. More specific is better.	

	//TODO: WE changed this to compare the constraints of the ARGUMENTS
// 	if (newMethodInstance->GetNumGenericArguments() == prevMethodInstance->GetNumGenericArguments())
// 	{
// 		if (newMethodInstance->GetNumGenericArguments() != 0)
// 		{
// 			for (int genericIdx = 0; genericIdx < (int)newMethodInstance->mMethodInfoEx->mMethodGenericArguments.size(); genericIdx++)
// 			{
// 				auto newMethodGenericParam = newMethodInstance->mMethodInfoEx->mGenericParams[genericIdx];
// 				auto prevMethodGenericParam = prevMethodInstance->mMethodInfoEx->mGenericParams[genericIdx];
// 				RETURN_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
// 			}
// 		}
// 	}
// 	else
	
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

	// Check specificity of args
	
	std::function<void(BfType*, BfType*)> _CompareParamTypes = [&](BfType* newType, BfType* prevType)
	{
		if ((newType->IsGenericParam()) && (prevType->IsGenericParam()))
		{
			auto newGenericParamType = (BfGenericParamType*)newType;
			auto prevGenericParamType = (BfGenericParamType*)prevType;
			if ((newGenericParamType->mGenericParamKind == BfGenericParamKind_Method) && (prevGenericParamType->mGenericParamKind == BfGenericParamKind_Method))
			{
				auto newMethodGenericParam = newMethodInstance->mMethodInfoEx->mGenericParams[newGenericParamType->mGenericParamIdx];
				auto prevMethodGenericParam = prevMethodInstance->mMethodInfoEx->mGenericParams[prevGenericParamType->mGenericParamIdx];
				SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
			}
		}
		else if (newType == prevType)
		{
			if ((newType->IsUnspecializedType()) && (newType->IsGenericTypeInstance()))
			{
				BfGenericTypeInstance* newGenericType = (BfGenericTypeInstance*)newType;
				BfGenericTypeInstance* prevGenericType = (BfGenericTypeInstance*)prevType;

				for (int genericArgIdx = 0; genericArgIdx < (int)newGenericType->mTypeGenericArguments.size(); genericArgIdx++)
				{
					_CompareParamTypes(newGenericType->mTypeGenericArguments[genericArgIdx], prevGenericType->mTypeGenericArguments[genericArgIdx]);
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
		_CompareParamTypes(newMethodInstance->GetParamType(newArgIdx), prevMethodInstance->GetParamType(prevArgIdx));
	}	

	// Do generic constraint subset test directly to handle cases like "NotDisposed<T>()" vs "NOtDisposed<T>() where T : IDisposable"
	if ((newMethodInstance->GetNumGenericArguments() > 0) && (newMethodInstance->GetNumGenericArguments() == prevMethodInstance->GetNumGenericArguments()))
	{
		for (int genericParamIdx = 0; genericParamIdx < (int)newMethodInstance->GetNumGenericArguments(); genericParamIdx++)
		{
			auto newMethodGenericParam = newMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			auto prevMethodGenericParam = prevMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			SET_BETTER_OR_WORSE(mModule->AreConstraintsSubset(prevMethodGenericParam, newMethodGenericParam), mModule->AreConstraintsSubset(newMethodGenericParam, prevMethodGenericParam));
		}
	}
	
	if ((isBetter) || (isWorse))
	{
		RETURN_RESULTS;
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
		auto genericOwner = (BfGenericTypeInstance*)owner;
		if (genericOwner->mGenericExtensionInfo != NULL)
		{			
			BfGenericExtensionEntry* newGenericExtesionEntry = NULL;
			BfGenericExtensionEntry* prevGenericExtesionEntry = NULL;
			if ((genericOwner->mGenericExtensionInfo->mExtensionMap.TryGetValue(newMethodDef->mDeclaringType, &newGenericExtesionEntry)) &&
				(genericOwner->mGenericExtensionInfo->mExtensionMap.TryGetValue(prevMethodDef->mDeclaringType, &prevGenericExtesionEntry)))
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

	RETURN_RESULTS;
}

BfTypedValue BfMethodMatcher::ResolveArgTypedValue(BfResolvedArg& resolvedArg, BfType* checkType, BfTypeVector* genericArgumentsSubstitute)
{	
	BfTypedValue argTypedValue = resolvedArg.mTypedValue;
	if ((resolvedArg.mArgFlags & BfArgFlag_DelegateBindAttempt) != 0)
	{
		//TODO: See if we can bind it to a delegate type		
		BfExprEvaluator exprEvaluator(mModule);		
		exprEvaluator.mExpectingType = checkType;
		BF_ASSERT(resolvedArg.mExpression->IsA<BfDelegateBindExpression>());
		auto delegateBindExpr = BfNodeDynCast<BfDelegateBindExpression>(resolvedArg.mExpression);
		BfMethodInstance* boundMethodInstance = NULL;
		if (exprEvaluator.CanBindDelegate(delegateBindExpr, &boundMethodInstance))
		{
			if (delegateBindExpr->mNewToken == NULL)
			{
				resolvedArg.mExpectedType = checkType;
				auto methodRefType = mModule->CreateMethodRefType(boundMethodInstance);
				mModule->AddDependency(methodRefType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
				mModule->AddCallDependency(boundMethodInstance);
				argTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), methodRefType);
			}
			else
				argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);				
		}
	}
	else if ((resolvedArg.mArgFlags & BfArgFlag_LambdaBindAttempt) != 0)
	{				
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
							resolvedArg.mTypedValue = mModule->CreateValueFromExpression(lambdaBindExpr, checkType, BfEvalExprFlags_NoCast);
						}
						argTypedValue = resolvedArg.mTypedValue;
					}
					else
						argTypedValue = BfTypedValue(BfTypedValueKind_UntypedValue);
						//argTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), checkType);
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
						expectType = mModule->ResolveGenericType(expectType, *genericArgumentsSubstitute, true);
				}
				
				exprEvaluator.mExpectingType = expectType;

				exprEvaluator.Evaluate(resolvedArg.mExpression);				
				argTypedValue = exprEvaluator.GetResult();
				
				if (mModule->mCurMethodState != NULL)
					mModule->mCurMethodState->mNoBind = prevNoBind;

				if (argTypedValue)
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
		if ((mBypassVirtual) && (checkMethod->mProtection == BfProtection_Protected) && (mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, startTypeInstance)))
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

bool BfMethodMatcher::InferFromGenericConstraints(BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs)
{
	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Equals) == 0)
		return false;

	if (!genericParamInst->mExternType->IsGenericParam())
		return false;

	auto genericParamType = (BfGenericParamType*)genericParamInst->mExternType;
	if (genericParamType->mGenericParamKind != BfGenericParamKind_Method)
		return false;	

	BfType* checkArgType = NULL;
	
	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Equals_Type) != 0)
	{
		checkArgType = genericParamInst->mTypeConstraint;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Equals_IFace) != 0)
	{
		if (!genericParamInst->mInterfaceConstraints.IsEmpty())
			checkArgType = genericParamInst->mInterfaceConstraints[0];
	}	

	for (auto& checkOpConstraint : genericParamInst->mOperatorConstraints)
	{
		auto leftType = checkOpConstraint.mLeftType;
		if ((leftType != NULL) && (leftType->IsUnspecializedType()))
			leftType = mModule->ResolveGenericType(leftType, *methodGenericArgs);
		if (leftType != NULL)
			leftType = mModule->FixIntUnknown(leftType);

		auto rightType = checkOpConstraint.mRightType;
		if ((rightType != NULL) && (rightType->IsUnspecializedType()))
			rightType = mModule->ResolveGenericType(rightType, *methodGenericArgs);
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
			BfExprEvaluator exprEvaluator(mModule);

			BfTypedValue leftValue(mModule->mBfIRBuilder->GetFakeVal(), leftType);
			BfTypedValue rightValue(mModule->mBfIRBuilder->GetFakeVal(), rightType);

			//
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
				SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
				exprEvaluator.PerformBinaryOperation(NULL, NULL, checkOpConstraint.mBinaryOp, NULL, BfBinOpFlag_NoClassify, leftValue, rightValue);
			}

			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Equals_Op) != 0)
				checkArgType = exprEvaluator.mResult.mType;
		}
		else
		{
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
					exprEvaluator.PerformUnaryOperation(NULL, checkOpConstraint.mUnaryOp, NULL);
					
					if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Equals_Op) != 0)
						checkArgType = exprEvaluator.mResult.mType;					
				}
			}			
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

	bool hadMatch = false;

	// Never consider overrides - they only get found at original method declaration
	//  mBypassVirtual gets set when we are doing an explicit "base" call, or when we are a struct -- 
	//  because on structs we know the exact type
	if ((checkMethod->mIsOverride) && (!mBypassVirtual) && (!typeInstance->IsValueType()))
		return false;		
		
	mMethodCheckCount++;

	BfMethodInstance* methodInstance = mModule->GetRawMethodInstance(typeInstance, checkMethod);
	if (methodInstance == NULL)
	{		
		BF_FATAL("Failed to get raw method in BfMethodMatcher::CheckMethod");
		return false;
	}

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
			auto implMethod = (BfMethodInstance*)vEntry.mImplementingMethod;
			if (implMethod != methodInstance)
			{
				SetAndRestoreValue<bool> prevBypassVirtual(mBypassVirtual, true);
				return CheckMethod(targetTypeInstance, implMethod->GetOwner(), implMethod->mMethodDef, isFailurePass);
			}
		}
		else
		{
			// Being in autocomplete mode is the only excuse for not having the virtual method table slotted
			if ((!mModule->mCompiler->IsAutocomplete()) && (!targetTypeInstance->mTypeFailed))
			{
				mModule->AssertErrorState();
			}
		}
	}

	HashSet<int> allowEmptyGenericSet;
	BfAutoComplete* autoComplete = NULL;
	if ((mModule->mCompiler->mResolvePassData != NULL) && (!isFailurePass))
		autoComplete = mModule->mCompiler->mResolvePassData->mAutoComplete;

	if (((checkMethod->mIsStatic) && (!mAllowStatic)) ||
		((!checkMethod->mIsStatic) && (!mAllowNonStatic)))
	{
		if (!typeInstance->IsFunction())
			autoComplete = NULL;
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
	
	bool needInferGenericParams = (checkMethod->mGenericParams.size() != 0) && (!mHadExplicitGenericArguments);
	int paramIdx = 0;
	BfType* paramsElementType = NULL;
	if (checkMethod->mHasAppend)
		paramIdx++;

// 	int outmostGenericCount = 0;
// 	if (checkMethod->mIsLocalMethod)
// 		outmostGenericCount = (int)mModule->mCurMethodState->GetRootMethodState()->mMethodInstance->mGenericParams.size();

	int uniqueGenericStartIdx = mModule->GetLocalInferrableGenericArgCount(checkMethod);

	if ((mHadExplicitGenericArguments) && (checkMethod->mGenericParams.size() != mExplicitMethodGenericArguments.size() + uniqueGenericStartIdx))
		goto NoMatch;
	
	for (auto& checkGenericArgRef : mCheckMethodGenericArguments)
		checkGenericArgRef = NULL;

	mCheckMethodGenericArguments.resize(checkMethod->mGenericParams.size());
	mPrevArgValues.resize(checkMethod->mGenericParams.size());
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
	}
	else if (needInferGenericParams)
		genericArgumentsSubstitute = &mCheckMethodGenericArguments;
	
	if (mSkipImplicitParams)
	{
		//paramOfs = methodInstance->GetImplicitParamCount();
		//paramIdx += paramOfs;
	}	
	
	if (needInferGenericParams)
	{
		int paramOfs = methodInstance->GetImplicitParamCount();
		int paramCount = methodInstance->GetParamCount();
		if (checkMethod->mHasAppend)
			paramOfs++;
		for (int argIdx = 0; argIdx < (int)mArguments.size(); argIdx++)
		{
			if (argIdx >= (int)checkMethod->mParams.size())
				break;
			int paramIdx = argIdx + paramOfs;
			if (paramIdx >= paramCount)
				break; // Possible for delegate 'params' type methods
			auto wantType = methodInstance->GetParamType(argIdx + paramOfs);
			
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
					checkType = genericParamInstance->mTypeConstraint;
			}

			if ((checkType != NULL) && (genericArgumentsSubstitute != NULL) && (checkType->IsUnspecializedType()))
			{
				checkType = mModule->ResolveGenericType(origCheckType, *genericArgumentsSubstitute);				
			}

			if (wantType->IsUnspecializedType())
			{
				BfTypedValue argTypedValue = ResolveArgTypedValue(mArguments[argIdx], checkType, genericArgumentsSubstitute);
				if (!argTypedValue.IsUntypedValue())
				{
					auto type = argTypedValue.mType;
					if (!argTypedValue)
						goto NoMatch;
					HashSet<BfType*> checkedTypeSet;
					if (!InferGenericArgument(methodInstance, type, wantType, argTypedValue.mValue, checkedTypeSet))
						goto NoMatch;
				}
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

		//
		for (int genericArgIdx = uniqueGenericStartIdx; genericArgIdx < (int)checkMethod->mGenericParams.size(); genericArgIdx++)
		{
			auto& genericArg = mCheckMethodGenericArguments[genericArgIdx];
			if (genericArg == NULL)
			{
				auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[genericArgIdx];
				InferFromGenericConstraints(genericParam, &mCheckMethodGenericArguments);
				if (genericArg != NULL)
					continue;
				if (!allowEmptyGenericSet.Contains(genericArgIdx))
					goto NoMatch;
			}
		}
	}

	// Iterate through params
	while (true)
	{
		// Too many arguments
		if (paramIdx >= (int)methodInstance->GetParamCount())
		{			
			break;
		}
		
		bool isDeferredEval = false;
		
		if ((methodInstance->GetParamKind(paramIdx) == BfParamKind_Params) && (paramsElementType == NULL))
		{
			if (paramIdx >= (int) mArguments.size())
				break; // No params			

			BfTypedValue argTypedValue = ResolveArgTypedValue(mArguments[argIdx], NULL, genericArgumentsSubstitute);
			if (!argTypedValue)
				goto NoMatch;

			if ((!argTypedValue.HasType()) && (!mArguments[argIdx].IsDeferredEval()))
				goto NoMatch;

			auto paramsArrayType = methodInstance->GetParamType(paramIdx);
			
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
						
			if (paramsArrayType->IsArray())
			{
				auto arrayType = (BfArrayType*)paramsArrayType;
				paramsElementType = arrayType->mTypeGenericArguments[0];

				while (argIdx < (int)mArguments.size())
				{
					argTypedValue = ResolveArgTypedValue(mArguments[argIdx], paramsElementType, genericArgumentsSubstitute);
					if (!argTypedValue.HasType())
						goto NoMatch;
					if (!mModule->CanCast(argTypedValue, paramsElementType))
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
			auto resolvedType = mModule->ResolveGenericType(wantType, *genericArgumentsSubstitute);
			if (resolvedType == NULL)
				goto NoMatch;
			wantType = resolvedType;
		}
		if (wantType->IsSelf())
			wantType = typeInstance;

		if ((mArguments[argIdx].mArgFlags & BfArgFlag_ParamsExpr) != 0)
		{
			// We had a 'params' expression but this method didn't have a params slot in this parameter
			goto NoMatch;
		}
		BfTypedValue argTypedValue = ResolveArgTypedValue(mArguments[argIdx], wantType, genericArgumentsSubstitute);

		if (!argTypedValue.IsUntypedValue())
		{
			if (!argTypedValue.HasType())
			{
				goto NoMatch;
			}
			else if (!mModule->CanCast(argTypedValue, wantType))
				goto NoMatch;
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

// 			if (genericArg->IsPrimitiveType())
// 			{
// 				auto primType = (BfPrimitiveType*) genericArg;
// 				genericArg = mModule->GetPrimitiveStructType(primType->mTypeDef->mTypeCode);
// 			}

			if (genericArg == NULL)
				goto NoMatch;

			//SetAndRestoreValue<bool> ignoreError(mModule->mIgnoreErrors, true);
			if (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance), genericArg, NULL, genericParams[checkGenericIdx], genericArgumentsSubstitute, NULL))
			{				
				goto NoMatch;
			}
		}
	}

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
				BfAmbiguousEntry ambiguousEntry;
				ambiguousEntry.mMethodInstance = methodInstance;
				if (genericArgumentsSubstitute != NULL)
					ambiguousEntry.mBestMethodGenericArguments = *genericArgumentsSubstitute;
				if (methodInstance->GetNumGenericParams() != 0)
				{
					BF_ASSERT(!ambiguousEntry.mBestMethodGenericArguments.empty());
				}
				mAmbiguousEntries.push_back(ambiguousEntry);				
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

	for (auto& arg : mArguments)
		arg.mBestBoundType = arg.mTypedValue.mType;

NoMatch:
	if (!hadMatch)
	{	
		if (mBestMethodDef != NULL)
			return false;
		
		/*if ((mHadExplicitGenericArguments) && (mBestMethodGenericArguments.size() != checkMethod->mGenericParams.size()))
			return false;*/

		if (mBackupMethodDef != NULL)
		{
			int prevParamDiff = (int)mBackupMethodDef->GetExplicitParamCount() - (int)mArguments.size();
			int paramDiff = (int)checkMethod->GetExplicitParamCount() - (int)mArguments.size();
			if ((prevParamDiff < 0) && (prevParamDiff > paramDiff))
				return false;
			if ((prevParamDiff >= 0) && ((paramDiff < 0) || (prevParamDiff < paramDiff)))
				return false;
			
			if (paramDiff == prevParamDiff)
			{
				if (argMatchCount < mBackupArgMatchCount)
					return false;
				else if (argMatchCount == mBackupArgMatchCount)
				{
					// We search from the most specific type, so don't prefer a less specific type
					if (mBestMethodTypeInstance != typeInstance)
						return false;
				}
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
				mBestMethodGenericArguments = *genericArgumentsSubstitute;
#ifdef _DEBUG
				for (auto arg : mBestMethodGenericArguments)
					BF_ASSERT((arg == NULL) || (!arg->IsVar()));
#endif
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
		BfError* error;
		if (!mMethodName.empty())
			error = mModule->Fail(StrFormat("Ambiguous method call for '%s'", mMethodName.c_str()), mTargetSrc);
		else
			error = mModule->Fail("Ambiguous method call", mTargetSrc);
		if (error != NULL)
		{
			BfMethodInstance* bestMethodInstance = mModule->GetRawMethodInstance(mBestMethodTypeInstance, mBestMethodDef);
			mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(bestMethodInstance, BfMethodNameFlag_ResolveGenericParamNames,
                mBestMethodGenericArguments.empty() ? NULL : &mBestMethodGenericArguments).c_str()),
				bestMethodInstance->mMethodDef->GetRefNode());
		
			for (auto& ambiguousEntry : mAmbiguousEntries)
			{
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(ambiguousEntry.mMethodInstance, BfMethodNameFlag_ResolveGenericParamNames,
                    ambiguousEntry.mBestMethodGenericArguments.empty() ? NULL : &ambiguousEntry.mBestMethodGenericArguments).c_str()),
					ambiguousEntry.mMethodInstance->mMethodDef->GetRefNode());
			}
		}

		mAmbiguousEntries.Clear();
	}
}

// This method checks all base classes before checking interfaces.  Is that correct?
bool BfMethodMatcher::CheckType(BfTypeInstance* typeInstance, BfTypedValue target, bool isFailurePass)
{	
	auto curTypeInst = typeInstance;
	auto curTypeDef = typeInstance->mTypeDef;
	
	int checkInterfaceIdx = 0;
	
	bool allowExplicitInterface = curTypeInst->IsInterface() && mBypassVirtual;
	auto activeTypeDef = mModule->GetActiveTypeDef();
	bool isDelegate = typeInstance->IsDelegate();

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
		curTypeDef->PopulateMemberSets();
		BfMethodDef* nextMethodDef = NULL;
		BfMemberSetEntry* entry;
		if (curTypeDef->mMethodSet.TryGetWith(mMethodName, &entry))
			nextMethodDef = (BfMethodDef*)entry->mMemberDef;

		BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
		while (nextMethodDef != NULL)
		{
			auto checkMethod = nextMethodDef;
			nextMethodDef = nextMethodDef->mNextWithSameName;

			if (mModule->mContext->mResolvingVarField)
			{
				bool isResolvingVarField = false;

				auto checkTypeState = mModule->mContext->mCurTypeState;
				while (checkTypeState != NULL)
				{
					if ((checkTypeState->mResolveKind == BfTypeState::ResolveKind_ResolvingVarType) &&
						(checkTypeState->mTypeInstance == typeInstance))
						isResolvingVarField = true;
					checkTypeState = checkTypeState->mPrevState;
				}

				if (isResolvingVarField)				
				{
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

			if (!isDelegate)
			{				
				if ((!curTypeInst->IsTypeMemberIncluded(checkMethod->mDeclaringType, activeTypeDef, mModule)) ||
					(!curTypeInst->IsTypeMemberAccessible(checkMethod->mDeclaringType, activeTypeDef)))
					continue;
			}

			MatchFailKind matchFailKind = MatchFailKind_None;			
			if (!mModule->CheckProtection(protectionCheckFlags, curTypeInst, checkMethod->mDeclaringType->mProject, checkMethod->mProtection, typeInstance))
			{
				if ((mBypassVirtual) && (checkMethod->mProtection == BfProtection_Protected) && (mModule->TypeIsSubTypeOf(mModule->mCurTypeInstance, typeInstance)))
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

		if (mBestMethodDef != NULL)
		{			
			FlushAmbiguityError();
			return true;
		}

		auto baseType = curTypeInst->mBaseType;
		if (baseType == NULL)
		{
			//TODO: Why were we doing the interface checking?

			if ((curTypeInst != mModule->mContext->mBfObjectType) && (!curTypeInst->IsInterface()))
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

	if ((mBestMethodDef == NULL) && (!target) && (mAllowImplicitThis))
	{
		// No explicit target - maybe this was a static call in the outer type?
		auto outerType = mModule->GetOuterType(typeInstance);
		if (outerType != NULL)
			CheckOuterTypeStaticMethods(outerType, isFailurePass);
	}

	FlushAmbiguityError();

	return mBestMethodDef != NULL;
}

void BfMethodMatcher::TryDevirtualizeCall(BfTypedValue target, BfTypedValue* origTarget, BfTypedValue* staticResult)
{		
	if ((mBestMethodDef == NULL) || (target.mType == NULL))
		return;

	if ((mModule->mCompiler->IsAutocomplete()) || (mModule->mContext->mResolvingVarField))
		return;

	if (mModule->mBfIRBuilder->mIgnoreWrites)
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

			if (mBestMethodTypeInstance->mTypeDef == mModule->mCompiler->mIHashableTypeDef)
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
						mModule->CompareDeclTypes(iface.mDeclaringType, bestIFaceEntry->mDeclaringType, isBetter, isWorse);						
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

				if ((checkTypeInst == NULL) && (checkType->HasWrappedRepresentation()))
				{
					auto underlyingType = checkType->GetUnderlyingType();
					if ((underlyingType != NULL) && (underlyingType->IsWrappableType()))
						checkTypeInst = mModule->GetWrappedStructType(underlyingType);
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
			useModule->AddDependency(boxedType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);
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
			if ((checkMethod->mMethodType != BfMethodType_Normal) || (!checkMethod->mIsStatic))
				continue;
			if (checkMethod->mName != mMethodName)
				continue;			

			if ((!isFailurePass) && (!mModule->CheckProtection(checkMethod->mProtection, allowProtected, allowPrivate)))
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

BfType* BfExprEvaluator::BindGenericType(BfAstNode* node, BfType* bindType)
{	
	if ((mModule->mCurMethodState == NULL) || (mModule->mCurMethodInstance == NULL) || (bindType == NULL))
		return bindType;

	BF_ASSERT(!mModule->mCurMethodInstance->mIsUnspecializedVariation);

	auto parser = node->GetSourceData()->ToParserData();
	if (parser == NULL)
		return bindType;
	int64 nodeId = ((int64)parser->mDataId << 32) + node->GetSrcStart();

	if (mModule->mCurMethodInstance->mIsUnspecialized)
	{
		if (!bindType->IsGenericParam())
			return bindType;

		(*mModule->mCurMethodState->mGenericTypeBindings)[nodeId] = bindType;
		return bindType;
	}
	else
	{
		if (mModule->mCurMethodState->mGenericTypeBindings == NULL)
			return bindType;
		
		/*auto itr = mModule->mCurMethodState->mGenericTypeBindings->find(nodeId);
		if (itr != mModule->mCurMethodState->mGenericTypeBindings->end())
			return itr->second;*/

		BfType** typePtr = NULL;
		if (mModule->mCurMethodState->mGenericTypeBindings->TryGetValue(nodeId, &typePtr))
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

	BfPendingNullConditional* pendingNullCond = NULL;
	if (mModule->mCurMethodState != NULL)
	{
		pendingNullCond = mModule->mCurMethodState->mPendingNullConditional;
		if (!propogateNullConditional)
			mModule->mCurMethodState->mPendingNullConditional = NULL;
	}
	mInsidePendingNullable = pendingNullCond != NULL;

	astNode->Accept(this);			
	GetResult();

	if ((mResultIsTempComposite) && (mResult.IsAddr()))
		mResult.mKind = BfTypedValueKind_TempAddr;

	if ((!allowSplat) && (mResult.IsSplat()))
		mResult = mModule->AggregateSplat(mResult);	

	if ((mBfEvalExprFlags & BfEvalExprFlags_AllowIntUnknown) == 0)
		mModule->FixIntUnknown(mResult);

	if ((!propogateNullConditional) && (mModule->mCurMethodState != NULL))
	{
		if (mModule->mCurMethodState->mPendingNullConditional != NULL)
			mResult = mModule->FlushNullConditional(mResult, ignoreNullConditional);
		mModule->mCurMethodState->mPendingNullConditional = pendingNullCond;
	}
}

void BfExprEvaluator::Visit(BfTypeReference* typeRef)
{
	mResult.mType = ResolveTypeRef(typeRef, BfPopulateType_Declaration);
}

void BfExprEvaluator::Visit(BfAttributedExpression* attribExpr)
{
	BfAttributeState attributeState;
	attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_Invocation | BfAttributeTargets_MemberAccess);
	attributeState.mCustomAttributes = mModule->GetCustomAttributes(attribExpr->mAttributes, attributeState.mTarget);
	
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);
	VisitChild(attribExpr->mExpression);
	if (!attributeState.mUsed)
	{
		mModule->Fail("Unused attributes", attribExpr->mAttributes);
	}	
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
	else if (auto lastExpr = BfNodeDynCast<BfExpression>(blockExpr->mChildArr.GetLast()))
	{
		// Expression		
	}
	else
	{
		mModule->Fail("Expression blocks must end with an expression", blockExpr);
	}			

	mModule->VisitEmbeddedStatement(blockExpr, this);
}

bool BfExprEvaluator::CheckVariableDeclaration(BfAstNode* checkNode, bool requireSimpleIfExpr, bool exprMustBeTrue, bool silentFail)
{
	BfAstNode* checkChild = checkNode;
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

	while (parentNodeEntry != NULL)
	{		
		BfAstNode* checkParent = parentNodeEntry->mNode;

		if (auto binOpExpr = BfNodeDynCastExact<BfBinaryOperatorExpression>(checkParent))
		{
			if (binOpExpr->mOp == BfBinaryOp_ConditionalAnd)
			{
				// This is always okay
			}
			else if ((binOpExpr->mOp == BfBinaryOp_ConditionalOr) && (!exprMustBeTrue))
			{
				bool matches = false;
				auto checkRight = binOpExpr->mRight;
				while (checkRight != NULL)
				{
					if (checkRight == checkChild)
					{
						matches = true;
						break;
					}

					if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(checkRight))
						checkRight = parenExpr->mExpression;
					else
					{
						break;
					}
				}

				if (matches)
				{
					if (!silentFail)
						mModule->Fail("Conditional short-circuiting may skip variable initialization", binOpExpr->mOpToken);
					return false;
				}
			}
			else
			{
				if (exprMustBeTrue)
				{
					if (!silentFail)
						mModule->Fail("Operator cannot be used with variable initialization", binOpExpr->mOpToken);
					return false;
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
				if (!silentFail)
					mModule->Fail("Operator cannot be used with variable initialization", unaryOp->mOpToken);
				return false;
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
				if (!silentFail)
					mModule->Fail("Variable declaration expression can only be contained in simple 'if' expressions", checkNode);
				return false;
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
    	caseValAddr = mModule->CreateValueFromExpression(caseExpr->mValueExpression);	

	if ((caseValAddr.mType != NULL) && (caseValAddr.mType->IsPointer()))
	{
		caseValAddr = mModule->LoadValue(caseValAddr);
		caseValAddr = BfTypedValue(caseValAddr.mValue, caseValAddr.mType->GetUnderlyingType(), true);
	}

	if (caseValAddr.mType != NULL)
		mModule->mBfIRBuilder->PopulateType(caseValAddr.mType);
	
	if (mModule->mCurMethodState->mDeferredLocalAssignData != NULL)
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
					if (unaryOpExpr->mOpToken->mToken == BfToken_Out)
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
			mResult = mModule->TryCaseEnumMatch(caseValAddr, enumTagVal, caseExpr->mCaseExpression, NULL, NULL, NULL, uncondTagId, hadConditional, clearOutOnMismatch);
		}
		else
		{
			mResult = mModule->TryCaseTupleMatch(caseValAddr, tupleExpr, NULL, NULL, NULL, hadConditional, clearOutOnMismatch);
		}
		
		if (mResult)
			return;

	}

	if ((caseValAddr) && (caseValAddr.mType->IsVar()))
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
			caseMatch = mModule->CreateValueFromExpression(caseExpr->mCaseExpression, NULL);
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

				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConstArray(mModule->mBfIRBuilder->MapType(sizedArray), charValues), sizedArray);
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
			if (mModule->mSystem->DoesLiteralFit(primType->mTypeDef->mTypeCode, variant.mInt64))
			{
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, variant.mUInt64), mExpectingType);
				break;
			}
		}				

		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mUInt64), mModule->GetPrimitiveType(variant.mTypeCode));		
		break;

	case BfTypeCode_Single:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mSingle), mModule->GetPrimitiveType(variant.mTypeCode));
		break;
	case BfTypeCode_Double:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(variant.mTypeCode, variant.mDouble), mModule->GetPrimitiveType(variant.mTypeCode));
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
		mModule->Warn(BfWarning_BF4201_Only7Hex, "Only 7 hex digits specified.  Add a leading zero to clarify intention.", literalExpr);
		break;
	case BfWarning_BF4202_TooManyHexForInt:
		mModule->Warn(BfWarning_BF4202_TooManyHexForInt, "Too many hex digits for an int, but too few for a long.  Use 'L' suffix if a long was intended.", literalExpr);
		break;
	}

	GetLiteral(literalExpr, literalExpr->mValue);
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
		if ((!preferValue) && (varDecl->mAddr))
			localResult = BfTypedValue(varDecl->mAddr, varDecl->mResolvedType, BfTypedValueKind_SplatHead);
		else if (!varDecl->mResolvedType->IsValuelessType())
			localResult = BfTypedValue(varDecl->mValue, varDecl->mResolvedType, BfTypedValueKind_SplatHead);
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

			localResult = BfTypedValue(varDecl->mValue, innerType, BfTypedValueKind_Addr);
		}
		else
		{
			BfTypedValueKind kind;
			if ((varDecl->mResolvedType->IsComposite()) && (varDecl->IsParam()) && (mModule->mCurMethodState->mMixinState == NULL))
				kind = varDecl->mIsReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr;
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
			localResult = BfTypedValue(mModule->mBfIRBuilder->CreateAlignedLoad(varDecl->mAddr, varDecl->mResolvedType->mAlign), varDecl->mResolvedType, true);
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

			int varSkipCount = 0;
			StringT<128> wantName;
			wantName.Reference(findName);
			if (findName.StartsWith('@'))
			{
				wantName = findName;
				while (wantName.StartsWith("@"))
				{
					varSkipCount++;
					wantName.Remove(0);
				}
			}

			if (wantName.IsEmpty())
			{
				mModule->Fail("Shadowed variable name expected after '@'", refNode);
			}

			BfLocalVarEntry* entry;
			if (checkMethodState->mLocalVarSet.TryGetWith<StringImpl&>(wantName, &entry))
			{
				auto varDecl = entry->mLocalVar;

				while ((varSkipCount > 0) && (varDecl != NULL))
				{
					varDecl = varDecl->mShadowedLocal;
					varSkipCount--;
				}

				if ((varDecl != NULL) && (varDecl->mNotCaptured))
				{
					mModule->Fail("Local variable is not captured", refNode);
				}

				if ((varSkipCount == 0) && (varDecl != NULL))
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
						 	(mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCurMethodInstance != NULL))
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
				int varSkipCount = 0;
				StringT<128> wantName;
				wantName.Reference(findName);
				if (findName.StartsWith('@'))
				{
					wantName = findName;
					while (wantName.StartsWith("@"))
					{
						varSkipCount++;
						wantName.Remove(0);
					}
				}

				closureTypeInst->mTypeDef->PopulateMemberSets();
				BfMemberSetEntry* memberSetEntry = NULL;
				if (closureTypeInst->mTypeDef->mFieldSet.TryGetWith((StringImpl&)wantName, &memberSetEntry))
				{					
					auto fieldDef = (BfFieldDef*)memberSetEntry->mMemberDef;
					while ((varSkipCount > 0) && (fieldDef != NULL))
					{
						fieldDef = fieldDef->mNextWithSameName;
						varSkipCount--;
					}

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
					if (checkTypeState->mTypeInstance == mModule->mCurTypeInstance)
						resolvingFieldDef = checkTypeState->mCurFieldDef;
				}
				checkTypeState = checkTypeState->mPrevState;
			}

			if ((resolvingFieldDef != NULL) && (resolvingFieldDef->mIdx > 0))
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

	BfTypedValue thisValue = mModule->GetThis();

	bool forcedIFaceLookup = false;
	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsForeignMethodDef))
	{
		thisValue.mType = mModule->mCurMethodInstance->GetForeignType();
		forcedIFaceLookup = true;
	}

	if (thisValue)
	{
		if (findName == "this")
			return thisValue;
		if (findName == "base")
		{
			auto baseValue = thisValue;
			if (baseValue.IsThis())
				baseValue.ToBase();

			if (mModule->GetActiveTypeDef()->mTypeCode != BfTypeCode_Extension)
			{
				MakeBaseConcrete(baseValue);
			}			
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
		thisValue = BfTypedValue(mModule->mCurTypeInstance);	
	BfTypedValue result = LookupField(identifierNode, thisValue, findName, BfLookupFieldFlag_IsImplicitThis);
	if (mPropDef != NULL)
	{
		if (forcedIFaceLookup)
		{

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
	}

	if ((!result) && (identifierNode != NULL))
		result = mModule->TryLookupGenericConstVaue(identifierNode, mExpectingType);

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
			if (mModule->mCurMethodInstance != NULL)
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

BfTypedValue BfExprEvaluator::LookupField(BfAstNode* targetSrc, BfTypedValue target, const StringImpl& fieldName, BfLookupFieldFlags flags)
{
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
				startCheckType = ((BfPointerType*) target.mType)->mElementType->ToTypeInstance();
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
		
		bool isBaseLookup = false;
		while (curCheckType != NULL)
		{			
			curCheckType->mTypeDef->PopulateMemberSets();
			BfFieldDef* nextField = NULL;
			BfMemberSetEntry* entry;
			if (curCheckType->mTypeDef->mFieldSet.TryGetWith(findName, &entry))
				nextField = (BfFieldDef*)entry->mMemberDef;

			while ((varSkipCount > 0) && (nextField != NULL))
			{
				nextField = nextField->mNextWithSameName;
			}

			BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
			BfFieldDef* matchedField = NULL;
			while (nextField != NULL)
			{
				auto field = nextField;
				nextField = nextField->mNextWithSameName;
								
				if (((flags & BfLookupFieldFlag_IgnoreProtection) == 0) &&  (!isFailurePass) &&
					(!mModule->CheckProtection(protectionCheckFlags, curCheckType, field->mDeclaringType->mProject, field->mProtection, startCheckType)))
				{					
					continue;					
				}				
				
				bool isResolvingFields = curCheckType->mResolvingConstField || curCheckType->mResolvingVarField;
				
				if (curCheckType->mFieldInstances.IsEmpty())
				{					
					mModule->PopulateType(curCheckType, BfPopulateType_Data);
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

				if (!curCheckType->IsTypeMemberAccessible(field->mDeclaringType, activeTypeDef))
					continue;

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
			{
				auto field = matchedField;
				auto fieldInstance = &curCheckType->mFieldInstances[field->mIdx];
				
				bool isResolvingFields = curCheckType->mResolvingConstField || curCheckType->mResolvingVarField;
				
				if (field->mIsVolatile)
					mIsVolatileReference = true;

				if (isFailurePass)
				{					
					mModule->Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", mModule->TypeToString(curCheckType).c_str(), field->mName.c_str()), targetSrc);					
				}					

				auto resolvePassData = mModule->mCompiler->mResolvePassData;
				if ((resolvePassData != NULL) && (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field))
				{
					resolvePassData->HandleFieldReference(targetSrc, curCheckType->mTypeDef, field);
				}								

				if ((!curCheckType->mTypeFailed) && (!isResolvingFields) && (curCheckType->IsIncomplete()))
				{
					if ((fieldInstance->mResolvedType == NULL) ||
						(!field->mIsStatic))
						mModule->PopulateType(curCheckType, BfPopulateType_Data);
				}

				if (fieldInstance->mResolvedType == NULL)
				{					
					BF_ASSERT((curCheckType->mTypeFailed) || (isResolvingFields));
					return BfTypedValue();
				}

				if (fieldInstance->mFieldIdx == -1)
				{
					mModule->AssertErrorState();
					return BfTypedValue();
				}

				mResultFieldInstance = fieldInstance;

				// Are we accessing a 'var' field that has not yet been resolved?
				if (fieldInstance->mResolvedType->IsVar())
				{
					// This can happen if we have one var field referencing another var field
					fieldInstance->mResolvedType = mModule->ResolveVarFieldType(curCheckType, fieldInstance, field);
					if (fieldInstance->mResolvedType == NULL)
						return BfTypedValue();

					if ((fieldInstance->mResolvedType->IsVar()) && (mModule->mCompiler->mIsResolveOnly))
						mModule->Fail("Field type reference failed to resolve", targetSrc);
				}
					
				auto resolvedFieldType = fieldInstance->mResolvedType;
				if (fieldInstance->mIsEnumPayloadCase)
				{
					resolvedFieldType = curCheckType;						
				}
					
				mModule->PopulateType(resolvedFieldType, BfPopulateType_Data);					
				mModule->AddDependency(curCheckType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);

				auto autoComplete = GetAutoComplete();
				if (autoComplete != NULL)
					autoComplete->CheckFieldRef(BfNodeDynCast<BfIdentifierNode>(targetSrc), fieldInstance);

				if (field->mIsStatic)
				{
					if ((target) && ((flags & BfLookupFieldFlag_IsImplicitThis) == 0) && (!curCheckType->mTypeDef->IsGlobalsContainer()))
					{
						//CS0176: Member 'Program.sVal' cannot be accessed with an instance reference; qualify it with a type name instead
						mModule->Fail(StrFormat("Member '%s.%s' cannot be accessed with an instance reference; qualify it with a type name instead", 
							mModule->TypeToString(curCheckType).c_str(), field->mName.c_str()), targetSrc);
					}

					// Target must be an implicit 'this', or an error (accessing a static with a non-static target).  
					//  Not actually needed in either case since this is a static lookup.
					mResultLocalVar = NULL;					
				}

				bool isConst = false;
				if (field->mIsConst)
				{
					isConst = true;
					auto fieldDef = fieldInstance->GetFieldDef();
					if ((resolvedFieldType->IsPointer()) && (fieldDef->mIsExtern))
						isConst = false;
				}

				if (resolvedFieldType->IsValuelessType())
				{
					return BfTypedValue(BfIRValue::sValueless, resolvedFieldType, true);
				}

				if (isConst)
				{
					if (fieldInstance->mIsEnumPayloadCase)
					{
						auto dscrType = curCheckType->GetDiscriminatorType();

						mModule->mBfIRBuilder->PopulateType(curCheckType);
						int tagIdx = -fieldInstance->mDataIdx - 1;
						if ((mBfEvalExprFlags & BfEvalExprFlags_AllowEnumId) != 0)
						{								
							return BfTypedValue(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), fieldInstance->mOwner);
						}

						mModule->PopulateType(fieldInstance->mOwner, BfPopulateType_Data);
// 						auto agg = mModule->mBfIRBuilder->CreateUndefValue(mModule->mBfIRBuilder->MapType(fieldInstance->mOwner));							
// 						agg = mModule->mBfIRBuilder->CreateInsertValue(agg, mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), 2);
// 
// 						if (fieldInstance->mResolvedType->mSize != 0)
// 						{
// 							mModule->FailAfter("Enum case parameters expected", targetSrc);
// 						}
// 
// 						return BfTypedValue(agg, fieldInstance->mOwner);

						auto agg = mModule->CreateAlloca(fieldInstance->mOwner);
						auto gep = mModule->mBfIRBuilder->CreateInBoundsGEP(agg, 0, 2);
						mModule->mBfIRBuilder->CreateStore(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), gep);
 						 
 						if (fieldInstance->mResolvedType->mSize != 0)
 						{
 							mModule->FailAfter("Enum case parameters expected", targetSrc);
 						}
 
 						return BfTypedValue(agg, fieldInstance->mOwner, true);
					}

					if (fieldInstance->mConstIdx == -1)
					{							
						curCheckType->mModule->ResolveConstField(curCheckType, fieldInstance, field);
						if (fieldInstance->mConstIdx == -1)
							return BfTypedValue(mModule->GetDefaultValue(resolvedFieldType), resolvedFieldType);
					}

					BF_ASSERT(fieldInstance->mConstIdx != -1);						
						
					auto foreignConst = curCheckType->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
					auto retVal = mModule->ConstantToCurrent(foreignConst, curCheckType->mConstHolder, resolvedFieldType);						
					return BfTypedValue(retVal, resolvedFieldType);
				}
				else if (field->mIsStatic)
				{
					mModule->CheckStaticAccess(curCheckType);
					auto retVal = mModule->ReferenceStaticField(fieldInstance);						
					bool isStaticCtor = (mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor) &&
						(mModule->mCurMethodInstance->mMethodDef->mIsStatic);
					if ((field->mIsReadOnly) && (!isStaticCtor))
						retVal = mModule->LoadValue(retVal, NULL, mIsVolatileReference);
					else
						mIsHeapReference = true;
					return retVal;
				}
				else if (!target)
				{											
					if ((flags & BfLookupFieldFlag_CheckingOuter) != 0)
						mModule->Fail(StrFormat("An instance reference is required to reference non-static outer field '%s.%s'", mModule->TypeToString(curCheckType).c_str(), field->mName.c_str()),
							targetSrc);
					else if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend))
						mModule->Fail(StrFormat("Cannot reference field '%s' before append allocations", field->mName.c_str()), targetSrc);
					else
						mModule->Fail(StrFormat("Cannot reference non-static field '%s' from a static method", field->mName.c_str()), targetSrc);
					return mModule->GetDefaultTypedValue(resolvedFieldType, false, BfDefaultValueKind_Addr);
				}

				if ((mResultLocalVar != NULL) && (fieldInstance->mMergedDataIdx != -1))
				{
					int minMergedDataIdx = fieldInstance->mMergedDataIdx;
					int maxMergedDataIdx = minMergedDataIdx + 1;
					if (resolvedFieldType->IsStruct())
						maxMergedDataIdx = minMergedDataIdx + resolvedFieldType->ToTypeInstance()->mMergedFieldDataCount;

					if (curCheckType->mIsUnion)
					{
						for (auto& checkFieldInstance : curCheckType->mFieldInstances)
						{
							if (&checkFieldInstance == fieldInstance)
								continue;

							if (checkFieldInstance.mDataIdx == fieldInstance->mDataIdx)
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

					int fieldIdx = mResultLocalVarField - 1;
					if (fieldIdx == -1)
					{							
						mResultLocalVarField = minMergedDataIdx + 1;
					}
					else
					{
						fieldIdx += minMergedDataIdx;
						mResultLocalVarField = fieldIdx + 1;							
					}
					mResultLocalVarFieldCount = maxMergedDataIdx - minMergedDataIdx;						
					mResultLocalVarRefNode = targetSrc;

					/*int fieldIdx = (mResultLocalVarIdx >> 16) - 1;
					if (fieldIdx == -1)
					{							
						mResultLocalVarField = fieldInstance->mMergedDataIdx + 1;
					}
					else
					{
						fieldIdx += fieldInstance->mMergedDataIdx;
						mResultLocalVarField = fieldIdx + 1;							
					}
					mResultLocalVarFieldCount = 1;
					if (fieldInstance->mType->IsStruct())
					{
						auto fieldTypeInstance = fieldInstance->mType->ToTypeInstance();
						mResultLocalVarFieldCount = fieldTypeInstance->mMergedFieldDataCount;
					}
					mResultLocalVarRefNode = targetSrc;*/

						
				}

				if ((curCheckType->IsIncomplete()) && (!curCheckType->mNeedsMethodProcessing))
				{
					BF_ASSERT(curCheckType->mTypeFailed || (mModule->mCurMethodState == NULL) || (mModule->mCurMethodState->mTempKind != BfMethodState::TempKind_None) || (mModule->mCompiler->IsAutocomplete()));
					return mModule->GetDefaultTypedValue(resolvedFieldType);
				}

				bool isTemporary = target.IsTempAddr();
				bool wantsLoadValue = false;
				if ((field->mIsReadOnly) && ((mModule->mCurMethodInstance->mMethodDef->mMethodType != BfMethodType_Ctor) || (!target.IsThis())))
					wantsLoadValue = true;					
										
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
				if ((isBaseLookup) && (!target.IsSplat()))
				{
					if ((!isComposite) || (target.IsAddr()))
						targetValue = BfTypedValue(mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->MapTypeInstPtr(curCheckType)), curCheckType);
					else
					{
						BfIRValue curVal = target.mValue;
						auto baseCheckType = target.mType->ToTypeInstance();
						while (baseCheckType != curCheckType)
						{
							curVal = mModule->mBfIRBuilder->CreateExtractValue(curVal, 0);
							baseCheckType = baseCheckType->mBaseType;
						}
						targetValue = BfTypedValue(curVal, curCheckType);
					}
				}
				else
					targetValue = target;

				bool doAccessCheck = true;

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
					BF_ASSERT(curCheckType->mTypeFailed);
					return mModule->GetDefaultTypedValue(resolvedFieldType);
				}
									
				BfTypedValue retVal;
				if (target.IsSplat())
				{	
					retVal = mModule->ExtractValue(targetValue, fieldInstance, fieldInstance->mDataIdx);
				}
				else if ((target.mType->IsStruct()) && (!target.IsAddr()))
				{
					retVal = BfTypedValue(mModule->mBfIRBuilder->CreateExtractValue(targetValue.mValue, fieldInstance->mDataIdx/*, field->mName*/),
						resolvedFieldType);
				}
				else
				{
					mModule->mBfIRBuilder->PopulateType(curCheckType);

					if ((targetValue.IsAddr()) && (!curCheckType->IsValueType()))
						targetValue = mModule->LoadValue(targetValue);

					retVal = BfTypedValue(mModule->mBfIRBuilder->CreateInBoundsGEP(targetValue.mValue, 0, fieldInstance->mDataIdx/*, field->mName*/),
						resolvedFieldType, target.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
				}

				if (!retVal.IsSplat())
				{
					if (curCheckType->mIsUnion)
					{
						BfIRType llvmPtrType = mModule->mBfIRBuilder->GetPointerTo(mModule->mBfIRBuilder->MapType(resolvedFieldType));
						retVal.mValue = mModule->mBfIRBuilder->CreateBitCast(retVal.mValue, llvmPtrType);
					}
				}

				if (wantsLoadValue)
					retVal = mModule->LoadValue(retVal, NULL, mIsVolatileReference);
				else
					mIsHeapReference = true;

				if (isTemporary)
				{
					if (retVal.IsAddr())
						retVal.mKind = BfTypedValueKind_TempAddr;
				}				

				return retVal;				
			}
			
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

					if ((!isFailurePass) && (!mModule->CheckProtection(protectionCheckFlags, curCheckType, prop->mDeclaringType->mProject, prop->mProtection, startCheckType)))
					{
						continue;
					}
					
					if (!prop->mMethods.IsEmpty())
					{
						BfMethodDef* checkMethod = prop->mMethods[0];;
						// Properties with explicit interfaces or marked as overrides can only be called indirectly
						if ((checkMethod->mExplicitInterface != NULL) || (checkMethod->mIsOverride))							
							continue;
					}
					
					if ((!target.IsStatic()) || (prop->mIsStatic))
					{						
						if ((!curCheckType->IsTypeMemberIncluded(prop->mDeclaringType, activeTypeDef, mModule)) ||
							(!curCheckType->IsTypeMemberAccessible(prop->mDeclaringType, activeTypeDef)))
							continue;

						if (matchedProp != NULL)
						{
							auto error = mModule->Fail(StrFormat("Ambiguous reference to property '%s.%s'", mModule->TypeToString(curCheckType).c_str(), fieldName.c_str()), targetSrc);
							if (error != NULL)
							{
								mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See property declaration in project '%s'", matchedProp->mDeclaringType->mProject->mName.c_str()), matchedProp->mFieldDeclaration);
								mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See property declaration in project '%s'", prop->mDeclaringType->mProject->mName.c_str()), prop->mFieldDeclaration);
								break;
							}
						}

						matchedProp = prop;
					}
				}

				if (matchedProp != NULL)
				{
					auto prop = matchedProp;
					gPropIdx++;

					mModule->SetElementType(targetSrc, BfSourceElementType_Method);

					mPropSrc = targetSrc;
					mPropDef = prop;
					mPropCheckedKind = checkedKind;
					if (isInlined)
						mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_ForceInline);

					if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL))
					{
						if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mFriendAttributeTypeDef))
							mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_Friend);
						if (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mDisableObjectAccessChecksAttributeTypeDef))
							mPropGetMethodFlags = (BfGetMethodInstanceFlags)(mPropGetMethodFlags | BfGetMethodInstanceFlag_DisableObjectAccessChecks);
					}

					if (mPropDef->mIsStatic)
					{
						if ((target) && ((flags & BfLookupFieldFlag_IsImplicitThis) == 0) && (!curCheckType->mTypeDef->IsGlobalsContainer()))
						{
							//CS0176: Member 'Program.sVal' cannot be accessed with an instance reference; qualify it with a type name instead
							mModule->Fail(StrFormat("Property '%s.%s' cannot be accessed with an instance reference; qualify it with a type name instead",
								mModule->TypeToString(curCheckType).c_str(), mPropDef->mName.c_str()), targetSrc);
						}
					}
					
					if (prop->mIsStatic)
						mPropTarget = BfTypedValue(curCheckType);
					else if (isBaseLookup)
					{
						mPropTarget = mModule->Cast(targetSrc, target, curCheckType);
						BF_ASSERT(mPropTarget);
					}
					else
						mPropTarget = target;

					if (mPropTarget.mType->IsStructPtr())
					{
						mPropTarget = mModule->LoadValue(mPropTarget);
						mPropTarget = BfTypedValue(mPropTarget.mValue, mPropTarget.mType->GetUnderlyingType(), mPropTarget.IsReadOnly() ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
					}

					mOrigPropTarget = mPropTarget;

					auto autoComplete = GetAutoComplete();
					auto resolvePassData = mModule->mCompiler->mResolvePassData;
					if (((autoComplete != NULL) && (autoComplete->mIsGetDefinition)) ||
						((resolvePassData != NULL) && (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Property)))
					{
						BfPropertyDef* basePropDef = mPropDef;
						BfTypeInstance* baseTypeInst = curCheckType;
						mModule->GetBasePropertyDef(basePropDef, baseTypeInst);
						resolvePassData->HandlePropertyReference(targetSrc, baseTypeInst->mTypeDef, basePropDef);
						if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(targetSrc)))
						{
							autoComplete->mDefProp = basePropDef;
							autoComplete->mDefType = baseTypeInst->mTypeDef;
						}
					}

					// Check for direct auto-property access
					if (startCheckType == mModule->mCurTypeInstance)
					{
						if (auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(mPropDef->mFieldDeclaration))
						{
							if (curCheckType->mTypeDef->HasAutoProperty(propertyDeclaration))
							{
								bool hasSetter = GetPropertyMethodDef(mPropDef, BfMethodType_PropertySetter, BfCheckedKind_NotSet) != NULL;
								auto autoFieldName = curCheckType->mTypeDef->GetAutoPropertyName(propertyDeclaration);
								auto result = LookupField(targetSrc, target, autoFieldName, BfLookupFieldFlag_IgnoreProtection);
								if (result)
								{
									if (!hasSetter)
									{
										if (((mModule->mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)) &&
											(startCheckType == mModule->mCurTypeInstance))
										{
											// Allow writing inside ctor
										}
										else
											result.MakeReadOnly();
									}
									mPropDef = NULL;
									mPropSrc = NULL;
									mOrigPropTarget = NULL;									
									return result;
								}
							}
						}
					}

					SetAndRestoreValue<BfTypedValue> prevResult(mResult, target);
					CheckResultForReading(mResult);
					return BfTypedValue();
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

void BfExprEvaluator::ResolveArgValues(BfResolvedArgs& resolvedArgs, BfResolveArgFlags flags)
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
		if (flags & BfResolveArgFlag_DeferFixits)
			autoComplete->mIgnoreFixits = true;
	}

	for (int argIdx = 0; argIdx < argCount ; argIdx++)
	{
		//printf("Args: %p %p %d\n", resolvedArgs.mArguments, resolvedArgs.mArguments->mVals, resolvedArgs.mArguments->mSize);

		BfExpression* argExpr = NULL;
		if (argIdx < resolvedArgs.mArguments->size())
			argExpr = (*resolvedArgs.mArguments)[argIdx];		

		if (argExpr == NULL)
		{
			if (argIdx == 0)
			{
				if (resolvedArgs.mOpenToken != NULL)
					mModule->FailAfter("Expression expected", resolvedArgs.mOpenToken);
			}
			else if (resolvedArgs.mCommas != NULL)
				mModule->FailAfter("Expression expected", (*resolvedArgs.mCommas)[argIdx - 1]);
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
		BfExprEvaluator exprEvaluator(mModule);
		exprEvaluator.mResolveGenericParam = (flags & BfResolveArgFlag_AllowUnresolvedTypes) == 0;
		exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_AllowOutExpr);
		bool handled = false;
		bool evaluated = false;

		bool deferParamEval = false;
		if ((flags & BfResolveArgFlag_DeferParamEval) != 0)
		{
			if (argExpr != NULL)
			{
				BfDeferEvalChecker deferEvalChecker;
				argExpr->Accept(&deferEvalChecker);
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
			bool deferParamValues = (flags & BfResolveArgFlag_DeferParamValues) != 0;
			SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, mModule->mBfIRBuilder->mIgnoreWrites || deferParamValues);
			auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
			if (deferParamValues)			
				resolvedArg.mArgFlags = (BfArgFlags)(resolvedArg.mArgFlags | BfArgFlag_DeferredValue);
			
			if (!evaluated)
			{
				exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_AllowParamsExpr);
				exprEvaluator.Evaluate(argExpr, false, false, true);
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
						auto argValue = exprEvaluator.LoadLocal(compositeLocalVar);
						if (argValue)
						{
							if (argValue.mType->IsRef())
								argValue.mKind = BfTypedValueKind_Value;
							else if (!argValue.mType->IsStruct())
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
				//resolvedArg.mResolvedType = mModule->ResolveGenericType(argValue.mType);

				resolvedArg.mResolvedType = argValue.mType;
				if (resolvedArg.mResolvedType->IsRef())
					argValue.mKind = BfTypedValueKind_Value;
				else if ((!resolvedArg.mResolvedType->IsStruct()) && (!resolvedArg.mResolvedType->IsSizedArray()) && (!resolvedArg.mResolvedType->IsValuelessType()))
					argValue = mModule->LoadValue(argValue, NULL, exprEvaluator.mIsVolatileReference);
			}
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
		mModule->CheckErrorAttributes(methodInstance->GetOwner(), methodInstance, customAttributes, targetSrc);	
}

BfTypedValue BfExprEvaluator::CreateCall(BfMethodInstance* methodInstance, BfIRValue func, bool bypassVirtual, SizedArrayImpl<BfIRValue>& irArgs, BfTypedValue* sret, bool isTailCall)
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

	if ((methodInstance->GetOwner()->mTypeDef == mModule->mCompiler->mDeferredCallTypeDef) &&
		(methodInstance->mMethodDef->mName == "Cancel"))
	{
		if (mModule->mCurMethodState != NULL)
			mModule->mCurMethodState->mCancelledDeferredCall = true;
	}

	if (methodDef->mNoReturn)
	{
		mModule->mCurMethodState->SetHadReturn(true);
		mModule->mCurMethodState->mLeftBlockUncond = true;
	}

	if (mModule->mCurTypeInstance != NULL)
	{
		bool isVirtual = (methodInstance->mVirtualTableIdx != -1) && (!bypassVirtual);
		mModule->AddDependency(methodInstance->mMethodInstanceGroup->mOwner, mModule->mCurTypeInstance, isVirtual ? BfDependencyMap::DependencyFlag_VirtualCall : BfDependencyMap::DependencyFlag_Calls);
		mModule->AddCallDependency(methodInstance, bypassVirtual);
	}

	if (methodInstance->GetOwner()->IsUnspecializedType())
	{
		BF_ASSERT((!methodInstance->mIRFunction) || (methodInstance == mModule->mCurMethodInstance));
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
		if (returnType->IsRef())
		{
			auto result = mModule->GetDefaultTypedValue(returnType->GetUnderlyingType(), true, BfDefaultValueKind_Addr);
			if (methodDef->mIsReadOnly)
				result.mKind = BfTypedValueKind_ReadOnlyAddr;
			return result;
		}
		else
		{						
			auto val = mModule->GetDefaultTypedValue(returnType, true, methodInstance->HasStructRet() ? BfDefaultValueKind_Addr : BfDefaultValueKind_Value);
			if (val.mKind == BfTypedValueKind_Addr)
				val.mKind = BfTypedValueKind_TempAddr;
			return val;
		}
	};

	mModule->PopulateType(origReturnType, BfPopulateType_Data);
	if (methodInstance->HasStructRet())
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
			sretVal = BfTypedValue(mModule->CreateAlloca(returnType), returnType, BfTypedValueKind_TempAddr);
			sret = &sretVal;
		}
	}
	else
	{
		BF_ASSERT(sret == NULL);
	}

	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mCapturing))
	{
		return _GetDefaultReturnValue();
	}

	BF_ASSERT(!methodInstance->mDisallowCalling);

	if ((mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		// In an autocomplete pass we may have stale method references that need to be resolved
		//  in the full classify pass, and in the full classify pass while just refreshing internals, we 
		//  may have NULL funcs temporarily.  We simply skip generating the method call here.		
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

		BfTypedValue result;
		if (sret != NULL)
			result = *sret;
 		else
            result = _GetDefaultReturnValue();
		return result;
	}

	if (((!func) && (methodInstance->mIsUnspecialized)) || (mModule->mBfIRBuilder->mIgnoreWrites))
	{
		// We don't actually submit method calls for unspecialized methods
		//  - this includes all methods in unspecialized types
		return _GetDefaultReturnValue();
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
				if ((declaringMethodInstance->mMethodInstanceGroup->mOnDemandKind < BfMethodOnDemandKind_InWorkList) || (!methodInstance->mIsReified))
					mModule->GetMethodInstance(declaringMethodInstance);
			}

			auto funcType = mModule->mBfIRBuilder->MapMethod(methodInstance);
			auto funcPtrType1 = mModule->mBfIRBuilder->GetPointerTo(funcType);
			auto funcPtrType2 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType1);
			auto funcPtrType3 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType2);
			auto funcPtrType4 = mModule->mBfIRBuilder->GetPointerTo(funcPtrType3);

			if (methodInstance->mMethodInstanceGroup->mOwner->IsInterface())
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
			if (!mModule->mCurMethodInstance->mIsUnspecialized)
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
			if ((targetType->IsValueType()) && (targetType->IsSplattable()) && (!methodDef->HasNoThisSplat()))
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

	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized))
	{
		// Don't actually do the call - our target may be a generic param
		return _GetDefaultReturnValue();
	}

	if (mDeferCallRef != NULL)
	{
		mModule->AddDeferredCall(BfModuleMethodInstance(methodInstance, func), irArgs, mDeferScopeAlloc, mDeferCallRef);
		//return _GetDefaultReturnValue();
		return mModule->GetFakeTypedValue(returnType);
	}

	if (!funcCallInst)
	{
		if ((mModule->HasCompiledOutput()) || (mModule->mCompiler->mOptions.mExtraResolveChecks))
		{
			// This can happen either from an error, or from the resolver while doing Internals_Changed processing
			mModule->AssertErrorState();
		}
		//return mModule->GetDefaultTypedValue(returnType,/*, true*/false, returnType->IsComposite());
		return _GetDefaultReturnValue();
	}
	
	bool hasResult = !methodInstance->mReturnType->IsValuelessType();

	BfIRValue firstArg;
	if (irArgs.size() != 0)
		firstArg = irArgs[0];
	
	auto methodInstOwner = methodInstance->GetOwner();
	auto expectCallingConvention = mModule->GetIRCallingConvention(methodInstOwner, methodDef);
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
	
	BfIRValue callInst;
	if (sret != NULL)
	{
		SizedArray<BfIRValue, 8> sretIRArgs; 
		sretIRArgs.push_back(sret->mValue);
		if (!irArgs.IsEmpty())
			sretIRArgs.Insert(sretIRArgs.size(), &irArgs[0], irArgs.size());
		callInst = mModule->mBfIRBuilder->CreateCall(funcCallInst, sretIRArgs);
	}
	else
	{
		callInst = mModule->mBfIRBuilder->CreateCall(funcCallInst, irArgs);
		if (hasResult)
			mModule->mBfIRBuilder->SetName(callInst, methodDef->mName);
	}
	if (expectCallingConvention != BfIRCallingConv_CDecl)
		mModule->mBfIRBuilder->SetCallCallingConv(callInst, expectCallingConvention);
	
	if (methodDef->mNoReturn)
		mModule->mBfIRBuilder->Call_AddAttribute(callInst, -1, BfIRAttribute_NoReturn);

	bool hadAttrs = false;	
	int paramIdx = 0;
	bool doingThis = !methodDef->mIsStatic;
	int argIdx = 0;
	if (sret != NULL)
	{
		mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_StructRet);
		argIdx++;
	}

	int paramCount = methodInstance->GetParamCount();

	for ( ; argIdx < (int)irArgs.size(); /*argIdx++*/)
	{	
		if (argIdx >= paramCount)
			break;

		auto _HandleParamType = [&] (BfType* paramType)
		{
			if (paramType->IsStruct())
			{
				if ((!doingThis) || (!methodDef->mIsMutating && !methodDef->mNoSplat))
				{
					auto loweredTypeCode = paramType->GetLoweredType();
					if (loweredTypeCode != BfTypeCode_None)
					{
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
					if ((typeInst != NULL) && (typeInst->mIsCRepr) && (typeInst->IsSplattable()))
					{
						// We're splatting
					}
					else
					{
						mModule->mBfIRBuilder->Call_AddAttribute(callInst, argIdx + 1, BfIRAttribute_NoCapture);						
						addDeref = paramType->mSize;
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
			paramType = methodInstance->GetOwner();
			if (paramType->IsValuelessType())
			{
				doingThis = false;
				continue;
			}
			bool isSplatted = methodInstance->GetParamIsSplat(-1); // (resolvedTypeRef->IsSplattable()) && (!methodDef->mIsMutating);
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
			paramType = methodInstance->GetParamType(paramIdx);
			if ((paramType->IsValuelessType()) && (!paramType->IsMethodRef()))
			{
				paramIdx++;
				continue;
			}
			//if (resolvedTypeRef->IsSplattable())
			if (methodInstance->GetParamIsSplat(paramIdx))
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
		
	if (isTailCall)
		mModule->mBfIRBuilder->SetTailCall(callInst);		

	if (methodDef->mNoReturn)
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
		if (methodInstance->mReturnType->GetLoweredType() != BfTypeCode_None)
		{
			auto loweredType = mModule->GetPrimitiveType(methodInstance->mReturnType->GetLoweredType());
			auto loweredPtrType = mModule->CreatePointerType(loweredType);

			auto retVal = mModule->CreateAlloca(methodInstance->mReturnType);
			auto castedRetVal = mModule->mBfIRBuilder->CreateBitCast(retVal, mModule->mBfIRBuilder->MapType(loweredPtrType));
			mModule->mBfIRBuilder->CreateStore(callInst, castedRetVal);
			result = BfTypedValue(retVal, methodInstance->mReturnType, BfTypedValueKind_TempAddr);
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
	auto moduleMethodInstance = mModule->GetMethodInstance(methodMatcher->mBestMethodTypeInstance, methodMatcher->mBestMethodDef, methodMatcher->mBestMethodGenericArguments);
	if (moduleMethodInstance.mMethodInstance == NULL)
		return BfTypedValue();
	if ((target) && (target.mType != moduleMethodInstance.mMethodInstance->GetOwner()))
	{
		auto castedTarget = mModule->Cast(methodMatcher->mTargetSrc, target, moduleMethodInstance.mMethodInstance->GetOwner());
		BF_ASSERT(castedTarget);
		target = castedTarget;
	}
	return CreateCall(methodMatcher->mTargetSrc, target, BfTypedValue(), methodMatcher->mBestMethodDef, moduleMethodInstance, false, methodMatcher->mArguments);
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
			irArgs.push_back(loadedVal.mValue);
		}
	};

	checkTypeLambda(value);
}

void BfExprEvaluator::PushArg(BfTypedValue argVal, SizedArrayImpl<BfIRValue>& irArgs, bool disableSplat, bool disableLowering)
{
	MakeBaseConcrete(argVal);
	
	if (argVal.mType->IsValuelessType())
		return;

	bool wantSplat = false;
	if (argVal.mType->IsSplattable())
	{
		auto argTypeInstance = argVal.mType->ToTypeInstance();
		if ((argTypeInstance != NULL) && (argTypeInstance->mIsCRepr))
		{
			// Always splat for crepr splattables
			wantSplat = true;
		}
		else if ((!disableSplat) && (int)irArgs.size() + argVal.mType->GetSplatCount() <= mModule->mCompiler->mOptions.mMaxSplatRegs)
			wantSplat = true;
	}

	if (wantSplat)
	{
		SplatArgs(argVal, irArgs);
	}
	else
	{
		if (argVal.mType->IsComposite())
		{
			argVal = mModule->MakeAddressable(argVal);
			
			if (!disableLowering)
			{
				auto loweredTypeCode = argVal.mType->GetLoweredType();
				if (loweredTypeCode != BfTypeCode_None)
				{
					auto primType = mModule->GetPrimitiveType(loweredTypeCode);
					auto ptrType = mModule->CreatePointerType(primType);
					BfIRValue primPtrVal = mModule->mBfIRBuilder->CreateBitCast(argVal.mValue, mModule->mBfIRBuilder->MapType(ptrType));
					auto primVal = mModule->mBfIRBuilder->CreateLoad(primPtrVal);
					irArgs.push_back(primVal);
					return;
				}
			}			
		}
		else
			argVal = mModule->LoadValue(argVal);
		irArgs.push_back(argVal.mValue);
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

	if ((argVal.mType->IsTypedPrimitive()) && (methodDef->HasNoThisSplat()))
	{
		argVal = mModule->MakeAddressable(argVal);
		irArgs.push_back(argVal.mValue);
		return;
	}

	auto thisType = methodInstance->GetParamType(-1);
	PushArg(argVal, irArgs, methodDef->HasNoThisSplat(), thisType->IsPointer());
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
			mModule->AddDependency(genericArg, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
		}
	}
}

//TODO: delete argumentsZ
BfTypedValue BfExprEvaluator::CreateCall(BfAstNode* targetSrc, const BfTypedValue& inTarget, const BfTypedValue& origTarget, BfMethodDef* methodDef, BfModuleMethodInstance moduleMethodInstance, bool bypassVirtual, SizedArrayImpl<BfResolvedArg>& argValues, bool skipThis)
{
	static int sCallIdx = 0;
	if (!mModule->mCompiler->mIsResolveOnly)
		sCallIdx++;
	int callIdx = sCallIdx;
	if (callIdx == 0x0000042B)
	{
		NOP;
	}

	// Temporarily disable so we don't capture calls in params	
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);

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

	BfTypedValue target = inTarget;

	if (!skipThis)
	{
		if (!methodDef->mIsStatic)
		{
			if (!target)
			{
				FinishDeferredEvals(argValues);
				mModule->Fail(StrFormat("An instance reference is required to %s the non-static method '%s'",
					(prevBindResult.mPrevVal != NULL) ? "bind" : "invoke",
					mModule->MethodToString(methodInstance).c_str()), targetSrc);
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

				PushThis(targetSrc, target, moduleMethodInstance.mMethodInstance, irArgs, skipMutCheck);
			}
		}
		else if ((target) && (target.mType->IsFunction()))
		{
			CheckResultForReading(target);
			target = mModule->LoadValue(target);
			auto funcType = mModule->mBfIRBuilder->MapMethod(moduleMethodInstance.mMethodInstance);
			auto funcPtrType = mModule->mBfIRBuilder->GetPointerTo(funcType);
			moduleMethodInstance.mFunc = mModule->mBfIRBuilder->CreateIntToPtr(target.mValue, funcPtrType);
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
		FinishDeferredEvals(argValues);
		mModule->EmitEnsureInstructionAt();
		return mModule->GetDefaultTypedValue(returnType);
	}

	int argIdx = 0;
	int paramIdx = 0;
	
	BfIRValue expandedParamAlloca;
	BfTypedValue expandedParamsArray;
	BfType* expandedParamsElementType = NULL;
	int extendedParamIdx = 0;

	AddCallDependencies(methodInstance);
	
	auto autoComplete = GetAutoComplete();
	if (autoComplete != NULL)
	{
		// Set to false to make sure we don't capture method match info from 'params' array creation
		autoComplete->mIsCapturingMethodMatchInfo = false;
	}

	BfScopeData* boxScopeData = mDeferScopeAlloc;
	if ((boxScopeData == NULL) && (mModule->mCurMethodState != NULL))
		boxScopeData = mModule->mCurMethodState->mCurScope;

	bool failed = false;
	while (true)
	{
		bool isDirectPass = false;

		if (paramIdx >= (int)methodInstance->GetParamCount())
		{
			if (methodInstance->IsVarArgs())
			{
				if (argIdx >= (int)argValues.size())
					break;

				BfTypedValue argValue = ResolveArgValue(argValues[argIdx], NULL);				
				if (argValue)					
				{
					auto typeInst = argValue.mType->ToTypeInstance();

					if (argValue.mType == mModule->GetPrimitiveType(BfTypeCode_Single))
						argValue = mModule->Cast(argValues[argIdx].mExpression, argValue, mModule->GetPrimitiveType(BfTypeCode_Double));

					if ((typeInst != NULL) && (typeInst->mTypeDef == mModule->mCompiler->mStringTypeDef))
					{
						BfType* charType = mModule->GetPrimitiveType(BfTypeCode_Char8);
						BfType* charPtrType = mModule->CreatePointerType(charType);
						argValue = mModule->Cast(argValues[argIdx].mExpression, argValue, charPtrType);
					}
					PushArg(argValue, irArgs, true, false);
				}				
				argIdx++;
				continue;
			}			

			if (argIdx < (int)argValues.size())
			{				
				BfAstNode* errorRef = argValues[argIdx].mExpression;				
				if (errorRef == NULL)
					errorRef = targetSrc;				

				BfError* error = mModule->Fail(StrFormat("Too many arguments, expected %d fewer.", (int)argValues.size() - argIdx), errorRef);
				if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
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
			wantsSplat = methodInstance->GetParamIsSplat(paramIdx);
			if (methodInstance->IsImplicitCapture(paramIdx))
			{
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
						else
						{														
							irArgs.push_back(lookupVal.mValue);
						}
					}
				}
				paramIdx++;
				continue;
			}
			
			wantType = methodInstance->GetParamType(paramIdx);
			if (wantType->IsSelf())
				wantType = methodInstance->GetOwner();
			if (wantType->IsVar())
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
					if (wantType->IsArray())
					{
						BfArrayType* arrayType = (BfArrayType*)wantType;
						mModule->PopulateType(arrayType, BfPopulateType_DataAndMethods);
						expandedParamsElementType = arrayType->mTypeGenericArguments[0];

						int arrayClassSize = arrayType->mInstSize - expandedParamsElementType->mSize;

						int numElements = (int)argValues.size() - argIdx;
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
						
						auto addr = mModule->mBfIRBuilder->CreateInBoundsGEP(arrayBits, 0, 1);
						if (arrayLengthBitCount == 64)
							mModule->mBfIRBuilder->CreateAlignedStore(mModule->GetConstValue64(numElements), addr, 8);
						else
							mModule->mBfIRBuilder->CreateAlignedStore(mModule->GetConstValue32(numElements), addr, 4);

						PushArg(expandedParamsArray, irArgs);
					}
					else
					{
						int numElements = (int)argValues.size() - argIdx;
						auto genericTypeInst = wantType->ToGenericTypeInstance();
						expandedParamsElementType = genericTypeInst->mTypeGenericArguments[0];

						expandedParamsArray = BfTypedValue(mModule->CreateAlloca(wantType), wantType, true);						
						expandedParamAlloca = mModule->CreateAlloca(genericTypeInst->mTypeGenericArguments[0], true, NULL, mModule->GetConstValue(numElements));
						mModule->mBfIRBuilder->CreateStore(expandedParamAlloca, mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamsArray.mValue, 0, 1));
						mModule->mBfIRBuilder->CreateStore(mModule->GetConstValue(numElements), mModule->mBfIRBuilder->CreateInBoundsGEP(expandedParamsArray.mValue, 0, 2));						

						PushArg(expandedParamsArray, irArgs);
					}

					continue;
				}
			}
		}
		
		BfAstNode* arg = NULL;
		bool hadMissingArg = false;
		if (argIdx < (int)argValues.size())
		{
			arg = argValues[argIdx].mExpression;
			if ((arg == NULL) && (argValues[0].mExpression != NULL))
				hadMissingArg = true;			
		}
		else
			hadMissingArg = true;
		
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
						error = mModule->Fail(StrFormat("No parameterless constructor is available for base class. Consider calling base constructor '%s'.",
							mModule->MethodToString(methodInstance).c_str()), refNode);
					}					
				}

				if (error == NULL)
					error = mModule->Fail(StrFormat("Not enough parameters specified, expected %d more.", methodInstance->GetParamCount() - paramIdx), refNode);
				if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
				failed = true;
				break;
			}			

			auto foreignDefaultVal = methodInstance->mDefaultValues[argIdx];
			auto foreignConst = methodInstance->GetOwner()->mConstHolder->GetConstant(methodInstance->mDefaultValues[argIdx]);		

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

			if (!argValue)
			{				
				argValue = mModule->GetTypedValueFromConstant(foreignConst, methodInstance->GetOwner()->mConstHolder, wantType);
				if (!argValue)
					mModule->Fail("Default parameter value failed", targetSrc);
			}			
		}
		else
		{			
			argValue = argValues[argIdx].mTypedValue;			
			
			if ((argValue.IsParams()) && (!isDirectPass))
			{
				BfAstNode* refNode = argValues[argIdx].mExpression;
				if (auto unaryOperatorExpr = BfNodeDynCast<BfUnaryOperatorExpression>(refNode))
					refNode = unaryOperatorExpr->mOpToken;
				mModule->Warn(0, "Unused 'params' expression", refNode);
			}

			if (wantType->IsMethodRef())
			{
				auto expr = argValues[argIdx].mExpression;
				if (expr != NULL)
					SetMethodElementType(expr);

				if (!argValue)
 					argValue = mModule->CreateValueFromExpression(BfNodeDynCast<BfExpression>(argValues[argIdx].mExpression), wantType, BfEvalExprFlags_NoCast);

				// Add any implicit captures now
				auto methodRefType = (BfMethodRefType*)wantType;
				BfMethodInstance* useMethodInstance = methodRefType->mMethodRef;

				for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
				{
					int paramIdx = methodRefType->GetParamIdxFromDataIdx(dataIdx);
					auto lookupVal = DoImplicitArgCapture(argValues[argIdx].mExpression, useMethodInstance, paramIdx, failed, BfImplicitParamKind_General, argValue);
					if (lookupVal)
					{
					 	if (methodRefType->WantsDataPassedAsSplat(dataIdx))
					 		SplatArgs(lookupVal, irArgs);
						else if (lookupVal.mType->IsComposite())
						{
							irArgs.push_back(lookupVal.mValue);
						}
						else
					 		irArgs.push_back(lookupVal.mValue);
					}
				}

				paramIdx++;
				argIdx++;
				continue;
			} 			
			else
			{	
				BfParamKind paramKind = BfParamKind_Normal;
				if (paramIdx < methodInstance->GetParamCount())
					paramKind = methodInstance->GetParamKind(paramIdx);

				argValues[argIdx].mExpectedType = wantType;
				argValue = ResolveArgValue(argValues[argIdx], wantType, NULL, paramKind);
			}
		}
		
		if (!argValue)
		{
			failed = true;			
		}

		if ((argValue) && (arg != NULL))
		{	
			if (mModule->mCurMethodState != NULL)
			{
				SetAndRestoreValue<BfScopeData*> prevScopeData(mModule->mCurMethodState->mOverrideScope, boxScopeData);
				argValue = mModule->Cast(arg, argValue, wantType);
			}
			else
				argValue = mModule->Cast(arg, argValue, wantType);

			if (!argValue)
			{
				failed = true;				
			}
			else if ((wantType->IsComposite()) && (!expandedParamsArray))
			{
				// We need to make a temp and get the addr of that
				if ((!wantsSplat) && (!argValue.IsValuelessType()) && (!argValue.IsAddr()))
				{					
					argValue = mModule->MakeAddressable(argValue);
				}
			}
			else if (!wantType->IsRef())
				argValue = mModule->LoadValue(argValue);
		}
		
	
		if (expandedParamsArray)
		{
			if (argValue)
			{
				if (expandedParamAlloca)
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
						auto storeInst = mModule->mBfIRBuilder->CreateAlignedStore(argValue.mValue, indexedAddr, argValue.mType->mAlign);
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
				if (underlyingType->IsValuelessType())
				{
					// We don't actually pass a 'this' pointer for mut methods on valueless structs
					argIdx++;
					paramIdx++;
					continue;
				}
			}

			if (argValue)
			{
				if (wantsSplat)				
					SplatArgs(argValue, irArgs);				
				else
					PushArg(argValue, irArgs, true, false);
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
			if ((argValues[argIdx].mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt | BfArgFlag_DeferredEval | BfArgFlag_VariableDeclaration)) != 0)
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
		
		mModule->AssertErrorState();
		return mModule->GetDefaultTypedValue(returnType, false, BfDefaultValueKind_Addr);
	}

	prevBindResult.Restore();

	if (!methodDef->mIsStatic)
	{
		if ((methodInstance->GetOwner()->IsInterface()) && (!target.mType->IsGenericParam()) && (!target.mType->IsConcreteInterfaceType()))
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
					else if ((!mModule->mCurMethodInstance->mIsUnspecialized))
					{
						// Compiler error?
						mModule->Fail(StrFormat("Unable to dynamically dispatch '%s'", mModule->MethodToString(methodInstance).c_str()), targetSrc);
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

	auto func = moduleMethodInstance.mFunc;	
	BfTypedValue result = CreateCall(methodInstance, func, bypassVirtual, irArgs);	
	if ((result.mType != NULL) && (result.mType->IsVar()) && (mModule->mCompiler->mIsResolveOnly))		
		mModule->Fail("Method return type reference failed to resolve", targetSrc);
	return result;
}

BfTypedValue BfExprEvaluator::MatchConstructor(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, BfTypeInstance* targetType, BfResolvedArgs& argValues, bool callCtorBodyOnly, bool allowAppendAlloc, BfTypedValue* appendIndexValue)
{
	// Temporarily disable so we don't capture calls in params
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);

	static int sCtorCount = 0;
	sCtorCount++;			
	
	BfMethodMatcher methodMatcher(targetSrc, mModule, "", argValues.mResolvedArgs, NULL);
		
	BfTypeVector typeGenericArguments;

	auto curTypeInst = targetType;
	auto curTypeDef = targetType->mTypeDef;
		
	BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;
	
	auto activeTypeDef = mModule->GetActiveTypeDef();
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

			if ((!curTypeInst->IsTypeMemberIncluded(checkMethod->mDeclaringType, activeTypeDef, mModule)) ||
				(!curTypeInst->IsTypeMemberAccessible(checkMethod->mDeclaringType, activeTypeDef)))
				continue;
			
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
					if (checkProt == BfProtection_Protected) // Treat protected constructors as private
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
		mModule->mCompiler->mResolvePassData->HandleMethodReference(targetSrc, curTypeInst->mTypeDef, methodDef);

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
	}


	// There should always be a constructor
	BF_ASSERT(methodMatcher.mBestMethodDef != NULL);
	
	auto moduleMethodInstance = mModule->GetMethodInstance(methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher.mBestMethodGenericArguments);
	
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
					resolvedArg.mTypedValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), intPtrType);
				}
				else
				{					
					BF_FATAL("Bad");
				}
			}
			methodMatcher.mArguments.Insert(0, resolvedArg);
		}
	}
	
	if (isFailurePass)
		mModule->Fail(StrFormat("'%s' is inaccessible due to its protection level", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str()), targetSrc);	
	prevBindResult.Restore();
	return CreateCall(methodMatcher.mTargetSrc, target, BfTypedValue(), methodMatcher.mBestMethodDef, moduleMethodInstance, false, methodMatcher.mArguments);	
}

static int sInvocationIdx = 0;

BfTypedValue BfExprEvaluator::ResolveArgValue(BfResolvedArg& resolvedArg, BfType* wantType, BfTypedValue* receivingValue, BfParamKind paramKind)
{	
	BfTypedValue argValue = resolvedArg.mTypedValue;
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
				BfEvalExprFlags flags = BfEvalExprFlags_NoCast;
				if ((paramKind == BfParamKind_Params) || (paramKind == BfParamKind_DelegateParam))
					flags = (BfEvalExprFlags)(flags | BfEvalExprFlags_AllowParamsExpr);

				argValue = mModule->CreateValueFromExpression(exprEvaluator, expr, wantType, flags);

				if ((argValue) && (argValue.mType != wantType) && (wantType != NULL))
				{
					if ((mDeferScopeAlloc != NULL) && (wantType == mModule->mContext->mBfObjectType))
					{
						BfAllocTarget allocTarget(mDeferScopeAlloc);
						argValue = mModule->BoxValue(expr, argValue, wantType, allocTarget);
					}
					else
						argValue = mModule->Cast(expr, argValue, wantType);
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
		argValue = mModule->CreateValueFromExpression(expr, wantType, (BfEvalExprFlags)(BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_AllowOutExpr));
		if ((argValue) && (wantType != NULL))
			argValue = mModule->Cast(expr, argValue, wantType);
	}
	else if ((resolvedArg.mArgFlags & (BfArgFlag_UntypedDefault)) != 0)
	{
		argValue = mModule->GetDefaultTypedValue(wantType);
	}
	else if ((resolvedArg.mArgFlags & (BfArgFlag_VariableDeclaration)) != 0)
	{
		auto variableDeclaration = BfNodeDynCast<BfVariableDeclaration>(resolvedArg.mExpression);

		auto variableType = wantType;

		bool isLet = variableDeclaration->mTypeRef->IsExact<BfLetTypeReference>();
		bool isVar = variableDeclaration->mTypeRef->IsExact<BfVarTypeReference>();

		if (mModule->mCurMethodState->mPendingNullConditional != NULL)
		{
			mModule->Fail("Variables cannot be declared in method arguments inside null conditional expressions", variableDeclaration);
		}

		if ((!isLet) && (!isVar))
		{
			mModule->Fail("Only 'ref' or 'var' variables can be declared in method arguments", variableDeclaration);

			auto autoComplete = GetAutoComplete();
			if (autoComplete != NULL)
				autoComplete->CheckTypeRef(variableDeclaration->mTypeRef, true, true);
		}

		if (wantType->IsRef())
		{
			auto refType = (BfRefType*)wantType;
			variableType = refType->mElementType;
		}

		if (variableDeclaration->mInitializer != NULL)
		{
			mModule->Fail("Initializers cannot be used when declaring variables for 'out' parameters", variableDeclaration->mEqualsNode);
			mModule->CreateValueFromExpression(variableDeclaration->mInitializer, variableType, BfEvalExprFlags_NoCast);
		}

		if (variableDeclaration->mNameNode == NULL)
			return argValue;

		BfLocalVariable* localVar = new BfLocalVariable();
		localVar->mName = variableDeclaration->mNameNode->ToString();
		localVar->mNameNode = BfNodeDynCast<BfIdentifierNode>(variableDeclaration->mNameNode);
		localVar->mResolvedType = variableType;
		if (!variableType->IsValuelessType())
			localVar->mAddr = mModule->CreateAlloca(variableType);
		localVar->mIsReadOnly = isLet;
		localVar->mReadFromId = 0;
		localVar->mWrittenToId = 0;
		localVar->mIsAssigned = true;		
		mModule->CheckVariableDef(localVar);
		localVar->Init();
		mModule->AddLocalVariableDef(localVar, true);	

		CheckVariableDeclaration(resolvedArg.mExpression, false, false, false);
		
		argValue = BfTypedValue(localVar->mAddr, mModule->CreateRefType(variableType, BfRefType::RefKind_Out));
	}
	return argValue;
}

BfTypedValue BfExprEvaluator::CheckEnumCreation(BfAstNode* targetSrc, BfTypeInstance* enumType, const StringImpl& caseName, BfResolvedArgs& argValues)
{
	auto activeTypeDef = mModule->GetActiveTypeDef();

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

			if ((mReceivingValue != NULL) && (mReceivingValue->mType == enumType) && (mReceivingValue->IsAddr()))
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
			auto tupleType = (BfTupleType*)fieldInstance->mResolvedType;
			mModule->mBfIRBuilder->PopulateType(tupleType);
	
			BfIRValue fieldPtr;
			BfIRValue tuplePtr;

			if (!tupleType->IsValuelessType())
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
				
				// Used receiving value?
				if (argValue.mValue == receivingValue.mValue)
					continue;

				if ((!argValue) || (argValue.IsValuelessType()))
					continue;
				argValue = mModule->AggregateSplat(argValue);
				argValues.mResolvedArgs[tupleFieldIdx].mExpectedType = resolvedFieldType;
				if ((argValues.mResolvedArgs[tupleFieldIdx].mArgFlags & (BfArgFlag_DelegateBindAttempt | BfArgFlag_LambdaBindAttempt | BfArgFlag_UnqualifiedDotAttempt)) != 0)
				{
					auto expr = BfNodeDynCast<BfExpression>(argValues.mResolvedArgs[tupleFieldIdx].mExpression);
					BF_ASSERT(expr != NULL);
					argValue = mModule->CreateValueFromExpression(expr, resolvedFieldType);
				}

				if (argValue)
				{
					// argValue can have a value even if tuplePtr does not have a value. This can happen if we are assigning to a (void) tuple,
					//  but we have a value that needs to be attempted to be casted to void
										
					argValue = mModule->Cast(argValues.mResolvedArgs[tupleFieldIdx].mExpression, argValue, resolvedFieldType);
					if (tupleFieldPtr)
					{
						argValue = mModule->LoadValue(argValue);
						if (argValue)
							mModule->mBfIRBuilder->CreateAlignedStore(argValue.mValue, tupleFieldPtr, resolvedFieldType->mAlign);
					}
				}
			}

			if ((intptr)argValues.mResolvedArgs.size() > tupleType->mFieldInstances.size())
			{
				BfAstNode* errorRef = argValues.mResolvedArgs[tupleType->mFieldInstances.size()].mExpression;
				if (errorRef == NULL)
					errorRef = targetSrc;

				BfError* error = mModule->Fail(StrFormat("Too many arguments, expected %d fewer.", argValues.mResolvedArgs.size() - tupleType->mFieldInstances.size()), errorRef);
				if (error != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See enum declaration"), fieldDef->mFieldDeclaration);
			}			

			//auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(enumValue, 0, 2);

			auto dscrType = enumType->GetDiscriminatorType();
			auto dscrField = &enumType->mFieldInstances.back();

			int tagIdx = -fieldInstance->mDataIdx - 1;
			auto dscFieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(enumValue, 0, dscrField->mDataIdx);
			mModule->mBfIRBuilder->CreateAlignedStore(mModule->mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagIdx), dscFieldPtr, 4);
			return result;
		}
	}

	return BfTypedValue();
}

bool BfExprEvaluator::CheckGenericCtor(BfGenericParamType* genericParamType, BfResolvedArgs& argValues, BfAstNode* targetSrc)
{
	auto genericConstraint = mModule->GetGenericParamInstance(genericParamType);
	bool success = true;
	
	if ((genericConstraint->mGenericParamFlags & BfGenericParamFlag_New) == 0)
	{
		mModule->Fail(StrFormat("Must add 'where %s : new' constraint to generic parameter to instantiate type", genericConstraint->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}
	if ((genericConstraint->mGenericParamFlags & BfGenericParamFlag_Struct) == 0)
	{
		mModule->Fail(StrFormat("Must add 'where %s : struct' constraint to generic parameter to instantiate type without allocator", genericConstraint->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}
	if ((argValues.mArguments != NULL) && (argValues.mArguments->size() != 0))
	{
		mModule->Fail(StrFormat("Only default parameterless constructors can be called on generic argument '%s'", genericConstraint->GetGenericParamDef()->mName.c_str()), targetSrc);
		success = false;
	}

	return success;
}

BfTypedValue BfExprEvaluator::MatchMethod(BfAstNode* targetSrc, BfMethodBoundExpression* methodBoundExpr, BfTypedValue target, bool allowImplicitThis, bool bypassVirtual, const StringImpl& methodName, 
	BfResolvedArgs& argValues, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments, BfCheckedKind checkedKind)
{
	BP_ZONE("MatchMethod");

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
		if ((!target.mType->IsGenericParam()) && (!target.IsSplat()) && (!target.mType->IsVar()))
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

	// Temporarily disable so we don't capture calls in params
	SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(mFunctionBindResult, NULL);

	sInvocationIdx++;
	
	bool wantCtor = methodName.IsEmpty();

	BfAutoComplete::MethodMatchInfo* restoreCapturingMethodMatchInfo = NULL;

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		if ((!targetSrc->IsFromParser(mModule->mCompiler->mResolvePassData->mParser)) ||
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
		else if (target.mType->IsVar())
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
		if (mModule->mCurMethodInstance->mIsUnspecialized)
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

	auto targetType = target.mType;
	BfTypeDef* curTypeDef = NULL;
	BfTypeInstance* targetTypeInst = NULL;
	bool checkNonStatic = true;
	if (target)
	{
		if (targetType->IsVar())		
			return mModule->GetDefaultTypedValue(targetType);		
		targetTypeInst = targetType->ToTypeInstance();				
		if (targetTypeInst != NULL)
			curTypeDef = targetTypeInst->mTypeDef;		
	}
	else if (targetType != NULL) // Static targeted
	{		
		if (targetType->IsWrappableType())
		{
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

	BfMethodDef* methodDef = NULL;
	BfTypeVector checkMethodGenericArguments;

	BfTypeInstance* curTypeInst = targetTypeInst;
	
	BfMethodMatcher methodMatcher(targetSrc, mModule, methodName, argValues.mResolvedArgs, methodGenericArguments);
	methodMatcher.mCheckedKind = checkedKind;
	methodMatcher.mAllowImplicitThis = allowImplicitThis;
	methodMatcher.mAllowStatic = !target.mValue;
	methodMatcher.mAllowNonStatic = !methodMatcher.mAllowStatic;
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
					autoComplete->SetDefinitionLocation(methodDef->GetRefNode());				
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
					FinishDeferredEvals(argValues.mResolvedArgs);

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
						if (ifaceInst->IsUnspecializedType())
							ifaceInst = mModule->ResolveType(ifaceInst);

						BfTypeInstance* typeInst = ifaceInst->ToTypeInstance();
						BF_ASSERT(typeInst != NULL);
						if (methodMatcher.CheckType(typeInst, target, false))
							methodMatcher.mSelfType = lookupType;
					}
				};

				auto genericParamInstance = mModule->GetGenericParamInstance(genericParamTarget);
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
	methodMatcher.TryDevirtualizeCall(target, &origTarget, &staticResult);
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

	// If we call "GetType" on a value type, statically determine the type rather than boxing and then dispatching 
	if ((methodDef) && (target) && (curTypeInst == mModule->mContext->mBfObjectType) && 
		(methodDef->mName == "GetType") && (target.mType->IsValueType()))
	{
		BfType* targetType = target.mType;
		if (origTarget)
			targetType = origTarget.mType;

		auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);
		mModule->AddDependency(targetType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
		return BfTypedValue(mModule->CreateTypeDataRef(targetType), typeType);		
	}	

	bool skipThis = false;

	// Fail, check for delegate field invocation
	if ((methodDef == NULL) && ((methodGenericArguments == NULL) || (methodGenericArguments->size() == 0)))
	{
		// Check for payload enum initialization first		
		BfTypedValue enumResult;
		BfTypeInstance* enumType = NULL;
		if ((!target) && (mModule->mCurTypeInstance->IsPayloadEnum()))
		{			
			enumType = mModule->mCurTypeInstance;
			//enumResult = CheckEnumCreation(targetSrc, mModule->mCurTypeInstance, methodName, argValues);
		}
		else if ((!target) && (target.HasType()) && (targetType->IsPayloadEnum()))
		{
			enumType = targetType->ToTypeInstance();			
			//enumResult = CheckEnumCreation(targetSrc, enumType, methodName, argValues);
		}

		if (enumType != NULL)
		{			
			enumResult = CheckEnumCreation(targetSrc, enumType, methodName, argValues);
		}

		if (enumResult)
		{
			if ((mModule->mCompiler->mResolvePassData != NULL) && 								
				(targetSrc->IsFromParser(mModule->mCompiler->mResolvePassData->mParser)) &&
				(mModule->mCompiler->mResolvePassData->mSourceClassifier != NULL))
			{
				mModule->mCompiler->mResolvePassData->mSourceClassifier->SetElementType(targetSrc, BfSourceElementType_Normal);
			}
			return enumResult;
		}

		BfTypedValue fieldVal;
		if (allowImplicitThis)
		{			
			auto identifierNode = BfNodeDynCast<BfIdentifierNode>(targetSrc);
			if (identifierNode != NULL)
				fieldVal = LookupIdentifier(identifierNode);
		}
		else
		{
			fieldVal = LookupField(targetSrc, target, methodName);
		}

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
					((typeInstConstraint->mTypeDef == mModule->mCompiler->mDelegateTypeDef) || (typeInstConstraint->mTypeDef == mModule->mCompiler->mFunctionTypeDef)))
				{
					MarkResultUsed();
					
// 					if (argValues.mResolvedArgs.size() > 0)
// 					{
// 						if ((argValues.mResolvedArgs[0].mArgFlags & BfArgFlag_FromParamComposite) != 0)
// 							delegateFailed = false;
// 					}
// 					else 
						
					if (argValues.mArguments->size() == 1)
					{
						BfExprEvaluator exprEvaluator(mModule);
						exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_AllowParamsExpr;
						exprEvaluator.Evaluate((*argValues.mArguments)[0]);
						if ((mModule->mCurMethodState != NULL) && (exprEvaluator.mResultLocalVar != NULL) && (exprEvaluator.mResultLocalVarRefNode != NULL))
						{	
							/*if (exprEvaluator.mResult.mKind != BfTypedValueKind_Params)
								mModule->Warn(0, "'params' token expected", (*argValues.mArguments)[0]);*/

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
				return MatchMethod(targetSrc, NULL, fieldVal, false, false, "Invoke", argValues, methodGenericArguments, checkedKind);				
			}					
			if (fieldVal.mType->IsVar())			
				return BfTypedValue(mModule->GetDefaultValue(fieldVal.mType), fieldVal.mType);		
			if (fieldVal.mType->IsGenericParam())
			{				
				auto genericParam = mModule->GetGenericParamInstance((BfGenericParamType*)fieldVal.mType);
				if ((genericParam->mTypeConstraint != NULL) && 
					((genericParam->mTypeConstraint->IsDelegate()) || (genericParam->mTypeConstraint->IsFunction())))
				{
					BfMethodInstance* invokeMethodInstance = mModule->GetRawMethodInstanceAtIdx(genericParam->mTypeConstraint->ToTypeInstance(), 0, "Invoke");

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
			if ((!resolvedTypeInstance->IsStruct()) && (!resolvedTypeInstance->IsTypedPrimitive()))
			{
				mModule->Fail("Objects must be allocated through 'new' or 'scope'", targetSrc);
				return BfTypedValue();
			}
			
			if (auto identifier = BfNodeDynCastExact<BfIdentifierNode>(targetSrc))
				mModule->SetElementType(identifier, BfSourceElementType_TypeRef);
			if (mModule->mCompiler->mResolvePassData != NULL)
				mModule->mCompiler->mResolvePassData->HandleTypeReference(targetSrc, resolvedTypeInstance->mTypeDef);

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
			MatchConstructor(targetSrc, methodBoundExpr, structInst, resolvedTypeInstance, argValues, false, false);
			mModule->ValidateAllocation(resolvedTypeInstance, targetSrc);
			
			mModule->AddDependency(resolvedTypeInstance, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

			if (mUsedAsStatement)
			{
				mModule->Warn(0, "Struct constructor being used as a statement", targetSrc);
			}
						
			return structInst;
		}
	}

	if ((methodDef == NULL) && (!target) && (mModule->mContext->mCurTypeState != NULL))
	{	
		//BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mTypeInstance);

		BfGlobalLookup globalLookup;
		globalLookup.mKind = BfGlobalLookup::Kind_Method;
		globalLookup.mName = methodName;
		mModule->PopulateGlobalContainersList(globalLookup);
		for (auto& globalContainer : mModule->mContext->mCurTypeState->mGlobalContainers)
		{
			if (globalContainer.mTypeInst == NULL)
				continue;
			methodMatcher.CheckType(globalContainer.mTypeInst, BfTypedValue(), false);
			if (methodMatcher.mBestMethodDef != NULL)
			{
				isFailurePass = false;
				curTypeInst = methodMatcher.mBestMethodTypeInstance;
				methodDef = methodMatcher.mBestMethodDef;
				break;
			}
		}
	}

	if (methodDef == NULL)
	{
		FinishDeferredEvals(argValues.mResolvedArgs);
		auto compiler = mModule->mCompiler;
		if ((compiler->IsAutocomplete()) && (compiler->mResolvePassData->mAutoComplete->CheckFixit(targetSrc)))
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

				BfTypeVector paramTypes;
				for (int argIdx = 0; argIdx < (int)argValues.mResolvedArgs.size(); argIdx++)
				{
					auto& resolvedArg = argValues.mResolvedArgs[argIdx];
					paramTypes.Add(resolvedArg.mTypedValue.mType);
				}

				autoComplete->FixitAddMethod(typeInst, methodName, mExpectingType, paramTypes, wantStatic);				
			}			
		}

		if (methodName.IsEmpty())
		{
			// Would have caused a parsing error
		}
		else if (target.mType != NULL)
			mModule->Fail(StrFormat("Method '%s' does not exist in type '%s'", methodName.c_str(), mModule->TypeToString(target.mType).c_str()), targetSrc);
		else
			mModule->Fail(StrFormat("Method '%s' does not exist", methodName.c_str()), targetSrc);				
		return BfTypedValue();
	}		
			
	if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized))
	{		
		for (auto& arg : methodMatcher.mBestMethodGenericArguments)
		{
			if ((arg != NULL) && (arg->IsVar()))
				return mModule->GetDefaultTypedValue(mModule->GetPrimitiveType(BfTypeCode_Var));
		}
	}

	if ((prevBindResult.mPrevVal != NULL) && (methodMatcher.mMethodCheckCount > 1))
		prevBindResult.mPrevVal->mCheckedMultipleMethods = true;

	BfModuleMethodInstance moduleMethodInstance = GetSelectedMethod(targetSrc, curTypeInst, methodDef, methodMatcher);
	if ((bypassVirtual) && (!target.mValue) && (target.mType->IsInterface()))
	{
		target = mModule->GetThis();
	}
	if (!moduleMethodInstance)
		return BfTypedValue();

	bool isSkipCall = moduleMethodInstance.mMethodInstance->IsSkipCall(bypassVirtual);
	
	if (methodDef->IsEmptyPartial())
	{
		// Ignore call
		return mModule->GetDefaultTypedValue(moduleMethodInstance.mMethodInstance->mReturnType);
	}	
	
	if ((moduleMethodInstance.mMethodInstance->mMethodDef->mIsStatic) && (moduleMethodInstance.mMethodInstance->GetOwner()->IsInterface()))
	{
		bool isConstrained = false;
		if (target.mType != NULL)
			isConstrained = target.mType->IsGenericParam();
		if ((target.mType == NULL) && ((mModule->mCurMethodInstance->mIsForeignMethodDef) || (mModule->mCurTypeInstance->IsInterface())))
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
			FinishDeferredEvals(argValues.mResolvedArgs);
			return mModule->GetDefaultTypedValue(moduleMethodInstance.mMethodInstance->mReturnType);
		}
	}

	// 'base' could really mean 'this' if we're in an extension
	if ((target.IsBase()) && (targetTypeInst == mModule->mCurTypeInstance))
		target.ToThis();
	else
		MakeBaseConcrete(target);

	BfTypedValue callTarget;
	if (isSkipCall)
	{
		// Just a fake value so we can continue on without generating any code (boxing, conversion, etc)
		if (target)
			callTarget = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), targetTypeInst);
	}
	else if (targetTypeInst == curTypeInst)
	{
		if ((target) && (methodDef->HasNoThisSplat()))
		{
			callTarget = mModule->MakeAddressable(target);
		}
		else
			callTarget = target;
	}
	else if (target)
	{
		if (methodMatcher.mFakeConcreteTarget)
		{
			BF_ASSERT(curTypeInst->IsInterface());
			callTarget = mModule->GetDefaultTypedValue(mModule->CreateConcreteInterfaceType(curTypeInst));
		}
		else if (((target.mType->IsGenericParam()) || (target.mType->IsConcreteInterfaceType())) && (curTypeInst->IsInterface()))
		{
			// Leave as generic
			callTarget = target;
		}
		else
		{
			bool handled = false;

			if ((target.mType->IsTypedPrimitive()) && (curTypeInst->IsTypedPrimitive()))
			{
				handled = true;
				callTarget = target;
			}
			else if ((target.mType->IsStructOrStructPtr()) || (target.mType->IsTypedPrimitive()))
			{
				//BF_ASSERT(target.IsAddr());
				if (curTypeInst->IsObjectOrInterface())
				{
					// Box it
					callTarget = mModule->Cast(targetSrc, target, curTypeInst, BfCastFlags_Explicit);
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
				callTarget = mModule->Cast(targetSrc, target, curTypeInst, BfCastFlags_Explicit);
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
		if ((identifierNode != NULL) && (methodDef->mIdx >= 0) && (!isIndirectMethodCall))
		{
			mModule->mCompiler->mResolvePassData->HandleMethodReference(identifierNode, curTypeInst->mTypeDef, methodDef);
			auto autoComplete = GetAutoComplete();
			if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(identifierNode)))
			{		
				autoComplete->SetDefinitionLocation(methodDef->GetRefNode());

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
					autoComplete->mDefMethod = methodDef;
					autoComplete->mDefType = curTypeInst->mTypeDef;
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

		if (needsMut)
		{
			String err = StrFormat("call mutating method '%s' on", mModule->MethodToString(moduleMethodInstance.mMethodInstance).c_str());
			CheckModifyResult(callTarget, targetSrc, err.c_str(), true);
		}
	}

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

	auto result = CreateCall(targetSrc, callTarget, origTarget, methodDef, moduleMethodInstance, bypassVirtual, argValues.mResolvedArgs, skipThis);
	if (result)
	{
		if (result.mType->IsRef())
			result = mModule->RemoveRef(result);
		if (result.mType->IsSelf())
		{
			if (methodMatcher.mSelfType != NULL)
			{
				BF_ASSERT(mModule->IsInGeneric());
				result = mModule->GetDefaultTypedValue(methodMatcher.mSelfType);
			}
			else
			{
				// Will be an error
				result = mModule->GetDefaultTypedValue(methodMatcher.mBestMethodTypeInstance);
			}
		}
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
					auto wasGetDefinition = (autoComplete != NULL) && (autoComplete->mIsGetDefinition);
					if (wasGetDefinition)
						autoComplete->mIsGetDefinition = false;
					result = mModule->MakeAddressable(result);
					BfResolvedArgs resolvedArgs;
					MatchMethod(targetSrc, NULL, result, false, false, "ReturnValueDiscarded", resolvedArgs, NULL);
					if (wasGetDefinition)
						autoComplete->mIsGetDefinition = true;
				}
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
			if (mPropDef != NULL)
			{
				mPropDefBypassVirtual = true;
			}
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

	//if (mResult.mType->IsVar())
		//ResolveGenericType();	

	auto origResult = mResult;
	auto lookupType = BindGenericType(nameNode, mResult.mType);	
	if (mResult.mType->IsGenericParam())
	{
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)mResult.mType);
		if (mModule->mCurMethodInstance->mIsUnspecialized)
		{			
			if (genericParamInst->mTypeConstraint != NULL)
				mResult.mType = genericParamInst->mTypeConstraint;
			else				
				mResult.mType = mModule->mContext->mBfObjectType;

			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				mResult.mType = mModule->GetPrimitiveType(BfTypeCode_Var);
			}
		}
		else
		{
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				//mResult.mType = mModule->ResolveGenericType(mResult.mType);
			}
			else if (genericParamInst->mTypeConstraint != NULL)
			{
				mResult = mModule->Cast(nameNode, mResult, genericParamInst->mTypeConstraint);
				BF_ASSERT(mResult);
			}
			else
			{
				// This shouldn't occur - this would infer that we are accessing a member of Object or something...
				//mResult.mType = mModule->ResolveGenericType(mResult.mType);
			}
		}
	}

	if (mResult.mType->IsVar())
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(mResult.mType), mResult.mType, true);
		return;
	}

	if (!mResult.mType->IsTypeInstance())
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
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)lookupType);
		SizedArray<BfTypeInstance*, 8> searchConstraints;
		for (auto ifaceConstraint : genericParamInst->mInterfaceConstraints)
		{
			//if (std::find(searchConstraints.begin(), searchConstraints.end(), ifaceConstraint) == searchConstraints.end())
			if (!searchConstraints.Contains(ifaceConstraint))
			{
				searchConstraints.push_back(ifaceConstraint);

				for (auto& innerIFaceEntry : ifaceConstraint->mInterfaces)
				{
					auto innerIFace = innerIFaceEntry.mInterfaceType;
					//if (std::find(searchConstraints.begin(), searchConstraints.end(), innerIFace) == searchConstraints.end())
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
			//auto lookupVal = mModule->GetDefaultTypedValue(ifaceConstraint, origResult.IsAddr());
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
		if ((CheckIsBase(nameNode->mLeft)) || (wasBaseLookup))
		{
			mPropDefBypassVirtual = true;
		}		
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
	mModule->Fail("Unable to find member", nameNode->mRight);		
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
			if (mPropDef != NULL)
			{
				mPropDefBypassVirtual = true;
			}
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

	//if (mResult.mType->IsVar())
	//ResolveGenericType();	

	auto origResult = mResult;
	auto lookupType = BindGenericType(nameNode, mResult.mType);
	if (mResult.mType->IsGenericParam())
	{
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)mResult.mType);
		if (mModule->mCurMethodInstance->mIsUnspecialized)
		{
			if (genericParamInst->mTypeConstraint != NULL)
				mResult.mType = genericParamInst->mTypeConstraint;
			else
				mResult.mType = mModule->mContext->mBfObjectType;

			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				mResult.mType = mModule->GetPrimitiveType(BfTypeCode_Var);
			}
		}
		else
		{
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
			{
				//mResult.mType = mModule->ResolveGenericType(mResult.mType);
			}
			else if (genericParamInst->mTypeConstraint != NULL)
			{
				mResult = mModule->Cast(nameNode, mResult, genericParamInst->mTypeConstraint);
				BF_ASSERT(mResult);
			}
			else
			{
				// This shouldn't occur - this would infer that we are accessing a member of Object or something...
				//mResult.mType = mModule->ResolveGenericType(mResult.mType);
			}
		}
	}

	if (mResult.mType->IsVar())
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(mResult.mType), mResult.mType, true);
		return;
	}

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
			if (hadError != NULL)
				*hadError = true;
			mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameLeft);
			mResult = BfTypedValue();
			return;
		}
	}

	BfTypedValue lookupVal = mResult;
	mResult = LookupField(nameRight, lookupVal, fieldName);
	if ((!mResult) && (mPropDef == NULL) && (lookupType->IsGenericParam()))
	{
		auto genericParamInst = mModule->GetGenericParamInstance((BfGenericParamType*)lookupType);
		SizedArray<BfTypeInstance*, 8> searchConstraints;
		for (auto ifaceConstraint : genericParamInst->mInterfaceConstraints)
		{
			//if (std::find(searchConstraints.begin(), searchConstraints.end(), ifaceConstraint) == searchConstraints.end())
			if (!searchConstraints.Contains(ifaceConstraint))
			{
				searchConstraints.push_back(ifaceConstraint);

				for (auto& innerIFaceEntry : ifaceConstraint->mInterfaces)
				{
					auto innerIFace = innerIFaceEntry.mInterfaceType;
					//if (std::find(searchConstraints.begin(), searchConstraints.end(), innerIFace) == searchConstraints.end())
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
			//auto lookupVal = mModule->GetDefaultTypedValue(ifaceConstraint, origResult.IsAddr());
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
	}

	if (mPropDef != NULL)
	{
		mOrigPropTarget = origResult;
		if ((CheckIsBase(nameLeft)) || (wasBaseLookup))
		{
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
	mModule->Fail("Unable to find member", nameRight);
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

	mResult = LookupField(nameNode->mRight, mResult, fieldName);
	if ((mResult) || (mPropDef != NULL))
		return;

	mModule->Fail("Unable to find member", nameNode->mRight);
}

void BfExprEvaluator::LookupQualifiedStaticField(BfAstNode* nameNode, BfIdentifierNode* nameLeft, BfIdentifierNode* nameRight, bool ignoreIdentifierNotFoundError)
{
	// Lookup left side as a type
	{		
		BfType* type = NULL;
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
			type = mModule->ResolveTypeRef(nameLeft, NULL, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);			
		}
		if (type != NULL)
		{
			BfTypedValue lookupType;
			if (type->IsWrappableType())
				lookupType = BfTypedValue(mModule->GetWrappedStructType(type));
			else
				lookupType = BfTypedValue(type);
			auto findName = nameRight->ToString();
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
		mResult.mType = structPtrType->mElementType;
	}

	if (!mResult)
		return;

	if (!mResult.mType->IsTypeInstance())
	{
		mModule->Fail(StrFormat("Type '%s' has no fields", mModule->TypeToString(mResult.mType).c_str()), nameLeft);
		return;
	}

	mResult = LookupField(nameRight, mResult, fieldName);
	if ((mResult) || (mPropDef != NULL))
		return;

	mModule->CheckTypeRefFixit(nameLeft);
	mModule->Fail("Unable to find member", nameRight);
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

	auto baseType = mModule->mCurTypeInstance->mBaseType;
	if (baseType == NULL)
		baseType = mModule->mContext->mBfObjectType;	

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(baseExpr)))
		autoComplete->SetDefinitionLocation(baseType->mTypeDef->GetRefNode());

	mModule->PopulateType(baseType, BfPopulateType_Data);
	mResult = mModule->Cast(baseExpr, mResult, baseType, BfCastFlags_Explicit);
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

void BfExprEvaluator::Visit(BfCollectionInitializerExpression* arrayInitExpr)
{	
	mModule->Fail("Collection initializer not usable here", arrayInitExpr);
}

void BfExprEvaluator::Visit(BfParamsExpression* paramsExpr)
{
	mModule->Fail("Params expression is only usable as a call parameter", paramsExpr);
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
	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeOfExpr->mTypeRef))
	{
		type = mModule->ResolveTypeRefAllowUnboundGenerics(typeOfExpr->mTypeRef, BfPopulateType_Identity);
	}
	else
	{
		type = ResolveTypeRef(typeOfExpr->mTypeRef, BfPopulateType_Identity);
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
	// We ignore errors because we go through the normal Visit(BfTypeOfExpression) if this fails, which will throw the error again
	SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);

	auto typeType = mModule->ResolveTypeDef(mModule->mCompiler->mTypeTypeDef);
	
	BfType* type;
	if (auto genericTypeRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeOfExpr->mTypeRef))
	{
		type = mModule->ResolveTypeRefAllowUnboundGenerics(typeOfExpr->mTypeRef, BfPopulateType_Identity);
	}
	else
	{
		type = ResolveTypeRef(typeOfExpr->mTypeRef, BfPopulateType_Identity);
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
		mResult = BfTypedValue(mModule->GetConstValue8(val ? 1 : 0), mModule->GetPrimitiveType(BfTypeCode_Boolean));
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
	else if (memberName == "TypeId")
		_Int32Result(type->mTypeId);
	else if (memberName == "GenericParamCount")
	{
		auto genericTypeInst = type->ToGenericTypeInstance();
		_Int32Result((genericTypeInst != NULL) ? (int)genericTypeInst->mTypeGenericArguments.size() : 0);
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
	else if ((memberName == "MinValue") || (memberName == "MaxValue"))
	{
		bool isMin = memberName == "MinValue";
				
		BfType* checkType = typeInstance;
		if (checkType->IsTypedPrimitive())
			checkType = checkType->GetUnderlyingType();

		if (checkType->IsPrimitiveType())
		{
			auto primType = (BfPrimitiveType*)checkType;

			if (typeInstance->IsEnum())
			{
				if (typeInstance->mTypeInfoEx != NULL)
				{
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)typeInstance->mTypeInfoEx->mMinValue : (uint64)typeInstance->mTypeInfoEx->mMaxValue), typeInstance);
					return true;
				}
			}
			else
			{
				switch (primType->mTypeDef->mTypeCode)
				{				
				case BfTypeCode_Int8:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? -0x80 : 0x7F), typeInstance);
					return true;
				case BfTypeCode_Int16:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? -0x8000 : 0x7FFF), typeInstance);
					return true;
				case BfTypeCode_Int32:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x80000000LL : 0x7FFFFFFF), typeInstance);
					return true;
				case BfTypeCode_Int64:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? (uint64)-0x8000000000000000LL : (uint64)0x7FFFFFFFFFFFFFFFLL), typeInstance);
					return true;
				case BfTypeCode_UInt8:
				case BfTypeCode_Char8:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : 0xFF), typeInstance);
					return true;
				case BfTypeCode_UInt16:
				case BfTypeCode_Char16:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : 0xFFFF), typeInstance);
					return true;
				case BfTypeCode_UInt32:
				case BfTypeCode_Char32:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFLL), typeInstance);
					return true;
				case BfTypeCode_UInt64:
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, isMin ? 0 : (uint64)0xFFFFFFFFFFFFFFFFLL), typeInstance);
					return true;
				default: break;
				}
			}
		}
		
		if (typeInstance->IsEnum())
		{
			mModule->Fail("'MinValue' cannot be used on enums with payloads", propName);			
		}
		else 
		{
			mModule->Fail(StrFormat("'%s' cannot be used on type '%s'", memberName.c_str(), mModule->TypeToString(typeInstance).c_str()), propName);
		}
	}	
	else
		return false;
	
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (typeOfExpr->mTypeRef != NULL))
	{
		autoComplete->CheckTypeRef(typeOfExpr->mTypeRef, false, true);
	}

	return true;
}

void BfExprEvaluator::DoTypeIntAttr(BfTypeReference* typeRef, BfToken token)
{	
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
		mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(BfTypeCode_Int32), sizeType);
	}
	else
		mResult = BfTypedValue(mModule->GetConstValue(attrVal, sizeType), sizeType);
}

void BfExprEvaluator::Visit(BfSizeOfExpression* sizeOfExpr)
{
	DoTypeIntAttr(sizeOfExpr->mTypeRef, BfToken_SizeOf);
}

void BfExprEvaluator::Visit(BfAlignOfExpression* alignOfExpr)
{
	DoTypeIntAttr(alignOfExpr->mTypeRef, BfToken_AlignOf);
}

void BfExprEvaluator::Visit(BfStrideOfExpression* strideOfExpr)
{
	DoTypeIntAttr(strideOfExpr->mTypeRef, BfToken_StrideOf);
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
	auto targetValue = mModule->CreateValueFromExpression(checkTypeExpr->mTarget);
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

	auto targetType = mModule->ResolveTypeRefAllowUnboundGenerics(checkTypeExpr->mTypeRef);
	if (!targetType)
	{
		mModule->AssertErrorState();
		return;
	}

	mModule->AddDependency(targetType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);

	auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
	if (targetValue.mType->IsValueTypeOrValueTypePtr())
	{
		auto typeInstance = targetValue.mType->ToTypeInstance();
		if (targetValue.mType->IsWrappableType())
			typeInstance = mModule->GetWrappedStructType(targetValue.mType);

		bool matches = (targetValue.mType == targetType) || (mModule->mContext->mBfObjectType == targetType);

		if (!matches)
			matches = mModule->TypeIsSubTypeOf(typeInstance, targetType->ToTypeInstance());

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
					mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary cast, the value is already type '%s'",
						mModule->TypeToString(srcTypeInstance).c_str()), checkTypeExpr->mIsToken);
				else
					mModule->Warn(BfWarning_BF4203_UnnecessaryDynamicCast, StrFormat("Unnecessary cast, '%s' is a subtype of '%s'",
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
	irb->CreateStore(irb->CreateConst(BfTypeCode_Boolean, 0), boolResult);

	mModule->EmitDynamicCastCheck(targetValue, targetType, matchBB, endBB);

	mModule->AddBasicBlock(matchBB);
	irb->CreateStore(irb->CreateConst(BfTypeCode_Boolean, 1), boolResult);
	irb->CreateBr(endBB);
	
	mModule->AddBasicBlock(endBB);	
	mResult = BfTypedValue(irb->CreateLoad(boolResult), boolType);	
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
		//wasGenericParamType = false; // "was", not "is"
		auto genericParamType = (BfGenericParamType*) targetType;
		auto genericParam = mModule->GetGenericParamInstance(genericParamType);
		auto typeConstraint = genericParam->mTypeConstraint;
		if ((typeConstraint == NULL) && (genericParam->mGenericParamFlags & BfGenericParamFlag_Class))
			typeConstraint = mModule->mContext->mBfObjectType;

		if ((typeConstraint == NULL) || (!typeConstraint->IsObject()))
		{
			mModule->Fail(StrFormat("The type parameter '%s' cannot be used with the 'as' operator because it does not have a class type constraint nor a 'class' constraint",
				genericParam->GetGenericParamDef()->mName.c_str()), dynCastExpr->mTypeRef);
			return;
		}

		targetType = typeConstraint;
		targetValue = mModule->GetDefaultTypedValue(targetType);
	}

	if ((!targetType->IsObjectOrInterface()) && (!targetType->IsNullable()))
	{
		mModule->Fail(StrFormat("The as operator must be used with a reference type or nullable type ('%s' is a non-nullable value type)",
			mModule->TypeToString(targetType).c_str()), dynCastExpr->mTypeRef);
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

		//targetType = typeConstraint;
		targetValue = mModule->GetDefaultTypedValue(typeConstraint);
	}

	auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

	if ((targetValue.mType->IsNullable()) && (targetType->IsInterface()))
	{		
		mResult = mModule->Cast(dynCastExpr, targetValue, targetType, BfCastFlags_SilentFail);
		if (!mResult)
		{
			mModule->Warn(0, StrFormat("Conversion from '%s' to '%s' will always be null", 
				mModule->TypeToString(targetValue.mType).c_str(), mModule->TypeToString(targetType).c_str()), dynCastExpr->mAsToken);
			mResult = BfTypedValue(mModule->GetDefaultValue(targetType), targetType);
		}
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
		return;
	}
	else if ((!targetType->IsInterface()) && (!mModule->TypeIsSubTypeOf(targetTypeInstance, srcTypeInstance)))
	{
		mModule->Fail(StrFormat("Cannot convert type '%s' to '%s' via any conversion", 
			mModule->TypeToString(targetValue.mType).c_str(), mModule->TypeToString(targetTypeInstance).c_str()), dynCastExpr->mAsToken);
	}

	if (autoComplete != NULL)	
	{
		mResult = mModule->GetDefaultTypedValue(targetType);
		return;
	}

	auto irb = mModule->mBfIRBuilder;
	auto objectType = mModule->mContext->mBfObjectType;	
	mModule->PopulateType(objectType, BfPopulateType_Full);

	auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();	
	auto endBB = mModule->mBfIRBuilder->CreateBlock("as.end");
	auto matchBlock = irb->CreateBlock("as.match");

	BfIRValue targetVal = mModule->CreateAlloca(targetType);
	irb->CreateStore(irb->CreateConstNull(irb->MapType(targetType)), targetVal);
	
	mModule->EmitDynamicCastCheck(targetValue, targetType, matchBlock, endBB);

	mModule->AddBasicBlock(matchBlock);
	BfIRValue castedCallResult = mModule->mBfIRBuilder->CreateBitCast(targetValue.mValue, mModule->mBfIRBuilder->MapType(targetType));
	irb->CreateStore(castedCallResult, targetVal);
	irb->CreateBr(endBB);

	mModule->AddBasicBlock(endBB);	
	mResult = BfTypedValue(irb->CreateLoad(targetVal), targetType);
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

	mResult = mModule->CreateValueFromExpression(castExpr->mExpression, resolvedType, (BfEvalExprFlags)(BfEvalExprFlags_ExplicitCast));
}

bool BfExprEvaluator::IsExactMethodMatch(BfMethodInstance* methodA, BfMethodInstance* methodB, bool ignoreImplicitParams)
{	
	if (methodA->mReturnType != methodB->mReturnType)
		return false;

	int implicitParamCountA = methodA->GetImplicitParamCount();
	int implicitParamCountB = methodB->GetImplicitParamCount();

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

bool BfExprEvaluator::CanBindDelegate(BfDelegateBindExpression* delegateBindExpr, BfMethodInstance** boundMethod)
{
	if (mExpectingType == NULL)
	{
		return false;
	}
	
	auto typeInstance = mExpectingType->ToTypeInstance();
	if ((typeInstance == NULL) || (!typeInstance->mTypeDef->mIsDelegate))
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

	for (int i = 0; i < (int) methodInstance->GetParamCount(); i++)
	{
		auto typedValueExpr = &typedValueExprs[i];
		typedValueExpr->mTypedValue.mValue = BfIRValue(BfIRValueFlags_Value, -1);
		typedValueExpr->mTypedValue.mType = methodInstance->GetParamType(i);
		typedValueExpr->mRefNode = NULL;
		args[i] = typedValueExpr;
	}

	BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments = NULL;
	if (delegateBindExpr->mGenericArgs != NULL)
		methodGenericArguments = &delegateBindExpr->mGenericArgs->mGenericArgs;
	
	BfFunctionBindResult bindResult;
	bindResult.mSkipMutCheck = true; // Allow operating on copies
	mFunctionBindResult = &bindResult;
	SetAndRestoreValue<bool> ignoreError(mModule->mIgnoreErrors, true);
	DoInvocation(delegateBindExpr->mTarget, delegateBindExpr, args, methodGenericArguments);
	mFunctionBindResult = NULL;
	if (bindResult.mMethodInstance == NULL)
		return false;	
	if (boundMethod != NULL)
		*boundMethod = bindResult.mMethodInstance;
	return IsExactMethodMatch(methodInstance, bindResult.mMethodInstance, true);
}

BfTypedValue BfExprEvaluator::DoImplicitArgCapture(BfAstNode* refNode, BfIdentifierNode* identifierNode)
{
	String findName = identifierNode->ToString();
	
	if (mModule->mCurMethodState != NULL)
	{
		auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();

		auto checkMethodState = mModule->mCurMethodState;
		bool isMixinOuterVariablePass = false;

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
// 						if ((closureTypeInst != NULL) && (wantName == "this"))
// 							break;

					if (varDecl->mNameNode == identifierNode)
					{
						BfTypedValue localResult = LoadLocal(varDecl);
//							auto autoComplete = GetAutoComplete();
// 							if (identifierNode != NULL)
// 							{
// 								if (autoComplete != NULL)
// 									autoComplete->CheckLocalRef(identifierNode, varDecl);
// 								if (((mModule->mCurMethodState->mClosureState == NULL) || (mModule->mCurMethodState->mClosureState->mCapturing)) &&
// 									(mModule->mCompiler->mResolvePassData != NULL) && (mModule->mCurMethodInstance != NULL))
// 									mModule->mCompiler->mResolvePassData->HandleLocalReference(identifierNode, varDecl->mNameNode, mModule->mCurTypeInstance->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, varDecl->mLocalVarId);
// 							}
// 
							if (!isMixinOuterVariablePass)
							{
								mResultLocalVar = varDecl;
								mResultFieldInstance = NULL;
								mResultLocalVarRefNode = identifierNode;
							}
						return localResult;
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
			BF_ASSERT(methodRefTarget.IsAddr());
			if (paramType->IsComposite())
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
			lookupVal = DoImplicitArgCapture(refNode, identifierNode);
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
	BfAllocTarget allocTarget = ResolveAllocTarget(delegateBindExpr->mNewToken, newToken);

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
	
	typedValueExprs.resize(methodInstance->GetParamCount());
	args.resize(methodInstance->GetParamCount());

	for (int i = 0; i < (int)methodInstance->GetParamCount(); i++)
	{
		auto typedValueExpr = &typedValueExprs[i];
		typedValueExpr->mTypedValue.mValue = BfIRValue(BfIRValueFlags_Value, -1);
		typedValueExpr->mTypedValue.mType = methodInstance->GetParamType(i);
		typedValueExpr->mRefNode = NULL;
		args[i] = typedValueExpr;
	}

	BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments = NULL;
	if (delegateBindExpr->mGenericArgs != NULL)
		methodGenericArguments = &delegateBindExpr->mGenericArgs->mGenericArgs;

	if (delegateBindExpr->mTarget == NULL)
	{
		mModule->AssertErrorState();
		return;
	}

	if (GetAutoComplete() != NULL)
		GetAutoComplete()->CheckNode(delegateBindExpr->mTarget);

	if ((!delegateBindExpr->mTarget->IsA<BfIdentifierNode>()) &&
		(!delegateBindExpr->mTarget->IsA<BfMemberReferenceExpression>()))
	{		
		mModule->Fail(StrFormat("Invalid %s binding target, %s can only bind to method references. Consider wrapping expression in a lambda.", bindTypeName, bindTypeName), delegateBindExpr->mTarget);		
		mModule->CreateValueFromExpression(delegateBindExpr->mTarget);
		return; 
	}

	BfFunctionBindResult bindResult;
	bindResult.mSkipMutCheck = true; // Allow operating on copies
	//
	{
		SetAndRestoreValue<BfType*> prevExpectingType(mExpectingType, methodInstance->mReturnType);
		mFunctionBindResult = &bindResult;
		DoInvocation(delegateBindExpr->mTarget, delegateBindExpr, args, methodGenericArguments);
		mFunctionBindResult = NULL;
	}	
	
	SetMethodElementType(delegateBindExpr->mTarget);

	if (bindResult.mMethodInstance == NULL)
	{
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
		if (!IsExactMethodMatch(methodInstance, bindMethodInstance, true))
		{
			if (bindResult.mCheckedMultipleMethods)
			{
				mModule->Fail(StrFormat("No overload for '%s' matches %s '%s'", bindMethodInstance->mMethodDef->mName.c_str(), bindTypeName,
					mModule->TypeToString(delegateTypeInstance).c_str()), delegateBindExpr->mTarget);
			}
			else
			{
				mModule->Fail(StrFormat("Method '%s' does not match %s '%s'", mModule->MethodToString(bindMethodInstance).c_str(), bindTypeName,
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

	bool hasIncompatibleCallingConventions = !mModule->mSystem->IsCompatibleCallingConvention(methodInstance->mMethodDef->mCallingConvention, bindMethodInstance->mMethodDef->mCallingConvention);

	auto _GetInvokeMethodName = [&]()
	{
		String methodName = "Invoke@";
		methodName += mModule->mCurMethodInstance->mMethodDef->mName;
		int prevSepPos = (int)methodName.LastIndexOf('$');
		if (prevSepPos != -1)
		{
			methodName.RemoveToEnd(prevSepPos);
		}

		auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
		HashContext hashCtx;
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
		return methodName;
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
			if (delegateTypeInstance->IsFunction())
			{
				auto intPtrVal = mModule->mBfIRBuilder->CreatePtrToInt(bindResult.mFunc, BfTypeCode_IntPtr);
				mResult = BfTypedValue(intPtrVal, mExpectingType);
				return;
			}
		}
		
		if (mExpectingType->IsFunction())
		{
			BfIRValue result;
			if ((hasIncompatibleCallingConventions) && (mModule->HasCompiledOutput()))
			{
				//
				{
					SetAndRestoreValue<bool> prevIgnore(mModule->mBfIRBuilder->mIgnoreWrites, true);
					result = mModule->CastToFunction(delegateBindExpr->mTarget, bindResult.mMethodInstance, mExpectingType);
				}

				if (result)
				{
					String methodName = _GetInvokeMethodName();

					SizedArray<BfIRType, 8> irParamTypes;
					BfIRType irReturnType;
					bindMethodInstance->GetIRFunctionInfo(mModule, irReturnType, irParamTypes);

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
					if (mModule->mCompiler->mOptions.mAllowHotSwapping)
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
				result = mModule->CastToFunction(delegateBindExpr->mTarget, bindResult.mMethodInstance, mExpectingType);
			}			
			if (result)
				mResult = BfTypedValue(result, mExpectingType);
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
					target = mModule->LoadValue(bindResult.mTarget);
				
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
	bool needsSplat = (isStructTarget) && (!bindMethodInstance->mMethodDef->mIsMutating) && (!bindMethodInstance->mMethodDef->mNoSplat);
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
		(mModule->HasCompiledOutput()))
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
		BfType* resolvedClosureType = mModule->ResolveType(checkClosureType, BfPopulateType_Identity);
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
		(mModule->HasCompiledOutput()))
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
			irParamTypes[0] = mModule->mBfIRBuilder->MapType(useTypeInstance);
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

		auto srcCallingConv = mModule->GetIRCallingConvention(methodInstance);
		if ((!hasThis) && (methodInstance->mMethodDef->mCallingConvention == BfCallingConvention_Stdcall))
			srcCallingConv = BfIRCallingConv_StdCall;
		mModule->mBfIRBuilder->SetFuncCallingConv(funcValue, srcCallingConv);
		
		mModule->mBfIRBuilder->SetActiveFunction(funcValue);
		auto entryBlock = mModule->mBfIRBuilder->CreateBlock("entry", true);
		mModule->mBfIRBuilder->SetInsertPoint(entryBlock);

		fieldIdx = 0;
		SizedArray<BfIRValue, 8> irArgs;
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
			auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mModule->mBfIRBuilder->GetArgument(0), 0, gepIdx);
			BfTypedValue typedVal(fieldPtr, fieldType, true);
			PushArg(typedVal, irArgs, disableSplat);
			fieldIdx++;
		}

		int argIdx = hasThis ? 1 : 0;
		for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
		{
			auto paramType = methodInstance->GetParamType(paramIdx);
			if (paramType->IsSplattable())
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { irArgs.push_back(mModule->mBfIRBuilder->GetArgument(argIdx++)); }, paramType);
			}
			else if (!paramType->IsValuelessType())
			{
				irArgs.push_back(mModule->mBfIRBuilder->GetArgument(argIdx++));
			}
		}

		auto bindFuncVal = bindResult.mFunc;
		if (mModule->mCompiler->mOptions.mAllowHotSwapping)
			bindFuncVal = mModule->mBfIRBuilder->RemapBindFunction(bindFuncVal);
		auto result = mModule->mBfIRBuilder->CreateCall(bindFuncVal, irArgs);
		auto destCallingConv = mModule->GetIRCallingConvention(bindMethodInstance);
		if (destCallingConv != BfIRCallingConv_CDecl)
			mModule->mBfIRBuilder->SetCallCallingConv(result, destCallingConv);
		if (methodInstance->mReturnType->IsValuelessType())
		{
			mModule->mBfIRBuilder->CreateRetVoid();
		}
		else
		{
			mModule->mBfIRBuilder->CreateRet(result);
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
	if (mModule->HasCompiledOutput())
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
			if ((mModule->HasCompiledOutput()) && (!mModule->mBfIRBuilder->mIgnoreWrites))
				mModule->AssertErrorState();
			return;
		}

		if ((mModule->mCompiler->mOptions.mAllowHotSwapping) && (!bindResult.mMethodInstance->mMethodDef->mIsVirtual))
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
		mModule->CreateValueFromExpression(bodyExpr);
	
	while (fieldDtor != NULL)
	{
		mModule->VisitChild(fieldDtor->mBody);
		fieldDtor = fieldDtor->mNextFieldDtor;
	}
}

BfLambdaInstance* BfExprEvaluator::GetLambdaInstance(BfLambdaBindExpression* lambdaBindExpr, BfAllocTarget& allocTarget)
{
	auto rootMethodState = mModule->mCurMethodState->GetRootMethodState();
	BfLambdaInstance* lambdaInstance = NULL;
	if (rootMethodState->mLambdaCache.TryGetValue(lambdaBindExpr, &lambdaInstance))	
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
								totalNames += invokeMethodInstance->GetParamName(checkIdx);
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
				autoComplete->mIsCapturingMethodMatchInfo = (wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo);
		}
	);	

	if (lambdaBindExpr->mBody == NULL)
	{
		mModule->AssertErrorState();
		return NULL;
	}

	if ((lambdaBindExpr->mNewToken == NULL) || (isFunctionBind))
	{
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

	BfMethodState methodState;
	methodState.mPrevMethodState = mModule->mCurMethodState;
	BfIRFunctionType funcType;
	auto voidType = mModule->GetPrimitiveType(BfTypeCode_None);
	SizedArray<BfIRType, 0> paramTypes;
	funcType = mModule->mBfIRBuilder->CreateFunctionType(mModule->mBfIRBuilder->MapType(voidType), paramTypes, false);

	auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();
	BF_ASSERT(prevInsertBlock);
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
			localVar->mIsAssigned = true;
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

	for (auto& captureEntry : allocTarget.mCaptureInfo.mCaptures)
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
			for (auto& captureEntry : allocTarget.mCaptureInfo.mCaptures)
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

	if (mModule->mCurMethodInstance->mIsUnspecialized)
	{
		prevIgnoreWrites.Restore();
		mModule->mBfIRBuilder->RestoreDebugLocation();

		mResult = mModule->GetDefaultTypedValue(delegateTypeInstance);
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
		if (allocTarget.mCaptureInfo.mCaptures.IsEmpty())
			return BfCaptureType_Copy;

		for (auto& captureEntry : allocTarget.mCaptureInfo.mCaptures)
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
					if (outerLocal->mName == "this")
						capturedEntry.mName = "__this";
					else
						capturedEntry.mName = outerLocal->mName;
					capturedEntry.mNameNode = outerLocal->mNameNode;					
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

	for (auto& captureEntry : allocTarget.mCaptureInfo.mCaptures)
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
	if ((capturedEntries.size() != 0) || (lambdaBindExpr->mDtor != NULL) || (copyOuterCaptures))
	{
		hashCtx.MixinStr(curProject->mName);

		if (copyOuterCaptures)
		{
// 			String typeName = mModule->DoTypeToString(outerClosure, BfTypeNameFlag_DisambiguateDups);
// 			hashCtx.MixinStr(typeName);
			hashCtx.Mixin(outerClosure->mTypeId);
		}

		for (auto& capturedEntry : capturedEntries)
		{
// 			String typeName = mModule->DoTypeToString(capturedEntry.mType, BfTypeNameFlag_DisambiguateDups);
// 			hashCtx.MixinStr(typeName);
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
		BfType* resolvedClosureType = mModule->ResolveType(checkClosureType, BfPopulateType_Identity);
		closureTypeInst = (BfClosureType*)resolvedClosureType;
		if (checkClosureType == resolvedClosureType)
		{
			// This is a new closure type
			closureTypeInst->Init(curProject);
			closureTypeInst->mTypeDef->mProject = curProject;

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

	// If we are allowing hot swapping, we need to always mangle the name to non-static because if we add a capture
	//  later then we need to have the mangled names match
	methodDef->mIsStatic = (closureTypeInst == NULL) && (!mModule->mCompiler->mOptions.mAllowHotSwapping);

	SizedArray<BfIRType, 3> newTypes;
	SizedArray<BfIRType, 8> origParamTypes;
	BfIRType origReturnType;

	if (!methodDef->mIsStatic)
		newTypes.push_back(mModule->mBfIRBuilder->MapType(useTypeInstance));

	if (invokeMethodInstance != NULL)
	{
		auto invokeFunctionType = mModule->mBfIRBuilder->MapMethod(invokeMethodInstance);
		invokeMethodInstance->GetIRFunctionInfo(mModule, origReturnType, origParamTypes, methodDef->mIsStatic);
	}
	else
	{
		origReturnType = mModule->mBfIRBuilder->MapType(mModule->GetPrimitiveType(BfTypeCode_None));
	}

	for (int i = methodDef->mIsStatic ? 0 : 1; i < (int)origParamTypes.size(); i++)
		newTypes.push_back(origParamTypes[i]);
	auto closureFuncType = mModule->mBfIRBuilder->CreateFunctionType(origReturnType, newTypes, false);

	prevMethodState.Restore();
	mModule->mBfIRBuilder->SetActiveFunction(prevActiveFunction);
	if (!prevInsertBlock.IsFake())
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

// 	if (closureTypeInst != NULL)
// 	{
// 		StringT<128> typeInstName;
// 		BfMangler::Mangle(typeInstName,mModule->mCompiler->GetMangleKind(), closureTypeInst);
// 		closureHashCtx.MixinStr(typeInstName);
// 	}

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
	rootMethodState->mLambdaCache[lambdaBindExpr] = lambdaInstance;
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

	if (mModule->HasCompiledOutput())
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
	if (mModule->mCompiler->IsSkippingExtraResolveChecks())
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
		if (mModule->mCompiler->IsSkippingExtraResolveChecks())
			dtorFunc = BfIRFunction();
		
		if (dtorMethodInstance->mIsReified)
			mModule->CheckHotMethod(dtorMethodInstance, dtorMangledName);
		if ((dtorMethodInstance->mHotMethod != NULL) && (mModule->mCurMethodState->mHotDataReferenceBuilder))
			mModule->mCurMethodState->mHotDataReferenceBuilder->mInnerMethods.Add(dtorMethodInstance->mHotMethod);
		lambdaInstance->mDtorMethodInstance = dtorMethodInstance;
		lambdaInstance->mDtorFunc = dtorFunc;
	}

	prevClosureState.Restore();
	if (!prevInsertBlock.IsFake())
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);		

	for (auto& capturedEntry : capturedEntries)
	{
		BfLambdaCaptureInfo lambdaCapture;
		if (capturedEntry.mName == "__this")
			lambdaCapture.mName = "this";
		else
			lambdaCapture.mName = capturedEntry.mName;
// 		if (capturedEntry.mLocalVarDef != NULL)
// 			lambdaCapture.mLocalIdx = capturedEntry.mLocalVarDef->mLocalVarId;
// 		lambdaCapture.mCopyField = capturedEntry.mCopyField;

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
// 	if (lambdaBindExpr->ToString() == "new (addr, byteCount, addrType) => { DoCreateMemoryBreakpoint(addr, byteCount, addrType, showOptions); }")
// 	{
// 		NOP;
// 	}

	BfTokenNode* newToken = NULL;
	BfAllocTarget allocTarget = ResolveAllocTarget(lambdaBindExpr->mNewToken, newToken);

	if ((mModule->mCurMethodState != NULL) && (mModule->mCurMethodState->mClosureState != NULL) && (mModule->mCurMethodState->mClosureState->mBlindCapturing))
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
	auto baseDelegate = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(baseDelegateType));

	// >> delegate.mTarget = bindResult.mTarget
	auto nullPtrType = mModule->GetPrimitiveType(BfTypeCode_NullPtr);
	BfIRValue valPtr;
	if (!lambdaInstance->mIsStatic)
		valPtr = mModule->mBfIRBuilder->CreateBitCast(mResult.mValue, mModule->mBfIRBuilder->MapType(nullPtrType));		
	else
		valPtr = mModule->GetDefaultValue(nullPtrType);
	auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, 2);
	mModule->mBfIRBuilder->CreateStore(valPtr, fieldPtr);

	// >> delegate.mFuncPtr = bindResult.mFunc
	if (lambdaInstance->mClosureFunc)
	{
		auto nullPtrType = mModule->GetPrimitiveType(BfTypeCode_NullPtr);		
		auto valPtr = mModule->mBfIRBuilder->CreateBitCast(lambdaInstance->mClosureFunc, mModule->mBfIRBuilder->MapType(nullPtrType));
		auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(baseDelegate, 0, 1);
		mModule->mBfIRBuilder->CreateStore(valPtr, fieldPtr);
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
					capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
					auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, fieldInstance.mDataIdx);
					mModule->mBfIRBuilder->CreateStore(capturedValue, fieldPtr);
					
					fieldIdx++;
				}
			}
		}

// 		for (auto& capturedEntry : capturedEntries)
// 		{
// 			BfIRValue capturedValue;
// 			if (capturedEntry.mLocalVarDef != NULL)
// 			{
// 				BfTypedValue localResult = LoadLocal(capturedEntry.mLocalVarDef, true);
// 
// 				if (!capturedEntry.mType->IsRef())
// 				{					
// 					if (localResult.IsSplat())
// 					{
// 						auto aggResult = mModule->AggregateSplat(localResult);
// 						capturedValue = aggResult.mValue;
// 					}
// 					else
// 					{						
// 						localResult = mModule->LoadValue(localResult);
// 						capturedValue = localResult.mValue;
// 						if ((capturedEntry.mLocalVarDef->mResolvedType->IsRef()) && (!capturedEntry.mType->IsRef()))
// 							capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 					}
// 				}
// 				else
// 				{
// 					if (capturedEntry.mLocalVarDef->mResolvedType->IsRef())
// 						localResult = mModule->LoadValue(localResult);
// 					capturedValue = localResult.mValue;
// 				}
// 			}
// 			else
// 			{
// 				// Read captured field from outer lambda ('this')
// 				auto localVar = mModule->mCurMethodState->mLocals[0];
// 				capturedValue = mModule->mBfIRBuilder->CreateInBoundsGEP(localVar->mValue, 0, capturedEntry.mCopyField->mDataIdx);
// 				if ((capturedEntry.mCopyField->mResolvedType->IsRef()) || (!capturedEntry.mType->IsRef()))
// 				{
// 					capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 					if ((capturedEntry.mCopyField->mResolvedType->IsRef()) && (!capturedEntry.mType->IsRef()))
// 						capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 				}
// 			}
// 			if (capturedValue)
// 			{
// 				auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, closureTypeInst->mFieldInstances[fieldIdx].mDataIdx);
// 				mModule->mBfIRBuilder->CreateStore(capturedValue, fieldPtr);
// 			}
// 			fieldIdx++;
// 		}

		int captureIdx = 0;
		//for (auto& capturedEntry : lambdaInstance->mCaptures)
		for (int captureIdx = 0; captureIdx < (int)lambdaInstance->mCaptures.size(); captureIdx++)
		{
			auto& capturedEntry = lambdaInstance->mCaptures[captureIdx];
			BfIdentifierNode* identifierNode = lambdaInstance->mMethodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries[captureIdx].mNameNode;
			//if (captureIdx < lambdaInstance->mMethodInstance->mClosureInstanceInfo->mCaptureNodes.size)

			BfIRValue capturedValue;
// 			if (capturedEntry.mLocalVarDef != NULL)
// 			{
// 				BfTypedValue localResult = LoadLocal(capturedEntry.mLocalVarDef, true);
// 
// 				if (!capturedEntry.mType->IsRef())
// 				{
// 					if (localResult.IsSplat())
// 					{
// 						auto aggResult = mModule->AggregateSplat(localResult);
// 						capturedValue = aggResult.mValue;
// 					}
// 					else
// 					{
// 						localResult = mModule->LoadValue(localResult);
// 						capturedValue = localResult.mValue;
// 						if ((capturedEntry.mLocalVarDef->mResolvedType->IsRef()) && (!capturedEntry.mType->IsRef()))
// 							capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 					}
// 				}
// 				else
// 				{
// 					if (capturedEntry.mLocalVarDef->mResolvedType->IsRef())
// 						localResult = mModule->LoadValue(localResult);
// 					capturedValue = localResult.mValue;
// 				}
// 			}
// 			else
// 			{
// 				// Read captured field from outer lambda ('this')
// 				auto localVar = mModule->mCurMethodState->mLocals[0];
// 				capturedValue = mModule->mBfIRBuilder->CreateInBoundsGEP(localVar->mValue, 0, capturedEntry.mCopyField->mDataIdx);
// 				if ((capturedEntry.mCopyField->mResolvedType->IsRef()) || (!capturedEntry.mType->IsRef()))
// 				{
// 					capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 					if ((capturedEntry.mCopyField->mResolvedType->IsRef()) && (!capturedEntry.mType->IsRef()))
// 						capturedValue = mModule->mBfIRBuilder->CreateLoad(capturedValue);
// 				}
// 			}

			auto fieldInstance = &closureTypeInst->mFieldInstances[fieldIdx];
			BfTypedValue capturedTypedVal;
			if (identifierNode != NULL)
				capturedTypedVal = DoImplicitArgCapture(NULL, identifierNode);
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
				auto fieldPtr = mModule->mBfIRBuilder->CreateInBoundsGEP(mResult.mValue, 0, fieldInstance->mDataIdx);
				mModule->mBfIRBuilder->CreateStore(capturedValue, fieldPtr);
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

			mModule->mBfIRBuilder->CreateStore(dtorThunk, fieldPtr);
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
		(afterNode->IsFromParser(mModule->mCompiler->mResolvePassData->mParser)) &&
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
				expectingType = arrayType->mTypeGenericArguments[0];
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
	auto autoComplete = GetAutoComplete();	
	if ((autoComplete != NULL) && (objCreateExpr->mTypeRef != NULL))
	{
		autoComplete->CheckTypeRef(objCreateExpr->mTypeRef, false, true);
	}

	//if (objCreateExpr->mArraySizeSpecifier == NULL)
		CheckObjectCreateTypeRef(mExpectingType, objCreateExpr->mNewNode);			

	BfAttributeState attributeState;	
	attributeState.mTarget = BfAttributeTargets_Alloc;
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, &attributeState);	
	BfTokenNode* newToken = NULL;
	BfAllocTarget allocTarget = ResolveAllocTarget(objCreateExpr->mNewNode, newToken, &attributeState.mCustomAttributes);	
	
	bool isScopeAlloc = newToken->GetToken() == BfToken_Scope;
	bool isAppendAlloc = newToken->GetToken() == BfToken_Append;
	bool isStackAlloc = (newToken->GetToken() == BfToken_Stack) || (isScopeAlloc);
	bool isArrayAlloc = false;// (objCreateExpr->mArraySizeSpecifier != NULL);
	bool isRawArrayAlloc = (objCreateExpr->mStarToken != NULL);

	if (isScopeAlloc)
	{
		if ((mBfEvalExprFlags & BfEvalExprFlags_FieldInitializer) != 0)
		{
			mModule->Warn(0, "This allocation will only be in scope during the constructor. Consider using a longer-term allocation such as 'new'", objCreateExpr->mNewNode);
		}
		
		if (objCreateExpr->mNewNode == newToken) // Scope, no target specified
		{
			if (mModule->mParentNodeEntry != NULL)
			{
				if (auto assignExpr = BfNodeDynCastExact<BfAssignmentExpression>(mModule->mParentNodeEntry->mNode))
				{
					if (mModule->mCurMethodState->mCurScope->mCloseNode == NULL)
					{
						// If we are assigning this to a property then it's possible the property setter can actually deal with a temporary allocation so no warning in that case
						if ((mBfEvalExprFlags & BfEvalExprFlags_PendingPropSet) == 0)
							mModule->Warn(0, "This allocation will immediately go out of scope. Consider specifying a wider scope target such as 'scope::'", objCreateExpr->mNewNode);
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
	if (objCreateExpr->mTypeRef == NULL)
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
		}

		if (unresolvedTypeRef == NULL)
		{
			if (auto arrayTypeRef = BfNodeDynCast<BfArrayTypeRef>(objCreateExpr->mTypeRef))
			{
				isArrayAlloc = true;
				if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(arrayTypeRef->mElementType))
				{
					if ((mExpectingType != NULL) && 
						((mExpectingType->IsArray()) || (mExpectingType->IsSizedArray())))
						unresolvedTypeRef = mExpectingType->GetUnderlyingType();
				}
				if (unresolvedTypeRef == NULL)
					unresolvedTypeRef = mModule->ResolveTypeRef(arrayTypeRef->mElementType);
				if (unresolvedTypeRef == NULL)
					unresolvedTypeRef = mModule->mContext->mBfObjectType;

				int dimensions = 1;

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

						if (!dimLength)
						{
							dimLength = mModule->GetDefaultTypedValue(intType);
						}
						dimLengthVals.push_back(dimLength.mValue);
					}
				} 

				if ((arrayTypeRef->mParams.size() == 0) && (objCreateExpr->mOpenToken == NULL))
					mModule->Fail("Array size or array initializer expected", arrayTypeRef->mOpenBracket);

				if (dimensions > 4)
					dimensions = 4;

				if (!isRawArrayAlloc)
					arrayType = mModule->CreateArrayType(unresolvedTypeRef, dimensions);
			}

			if (unresolvedTypeRef == NULL)
			{
				unresolvedTypeRef = ResolveTypeRef(objCreateExpr->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);				
			}
		}

		resolvedTypeRef = unresolvedTypeRef;		
		if ((resolvedTypeRef != NULL) && (resolvedTypeRef->IsVar()))
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
			mModule->Fail("Append allocations are only allowed in classes", objCreateExpr->mNewNode);
			isAppendAlloc = false;
		}
		else if ((mBfEvalExprFlags & BfEvalExprFlags_VariableDeclaration) == 0)
		{
			mModule->Fail("Append allocations are only allowed as local variable initializers in constructor body", objCreateExpr->mNewNode);
			isAppendAlloc = false;
		}
		else
		{
			auto methodDef = mModule->mCurMethodInstance->mMethodDef;
			if (methodDef->mMethodType == BfMethodType_CtorCalcAppend)
			{
				mModule->Fail("Append allocations are only allowed as local variable declarations in the main method body", objCreateExpr->mNewNode);
				isAppendAlloc = false;
			}
			else if (!methodDef->mHasAppend)
			{
				mModule->Fail("Append allocations can only be used on constructors with [AllowAppend] specified", objCreateExpr->mNewNode);
				isAppendAlloc = false;
			}
			else if (methodDef->mMethodType != BfMethodType_Ctor)
			{
				mModule->Fail("Append allocations are only allowed in constructors", objCreateExpr->mNewNode);
				isAppendAlloc = false;
			}
			else if (methodDef->mIsStatic)
			{
				mModule->Fail("Append allocations are only allowed in non-static constructors", objCreateExpr->mNewNode);
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
							dimLengths.push_back(dimLength);
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

		std::function<void(BfIRValue addr, int curDim, const BfSizedArray<BfExpression*>& valueExprs)> _HandleInitExprs = [&](BfIRValue addr, int curDim, const BfSizedArray<BfExpression*>& valueExprs)
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
						_HandleInitExprs(addr, curDim + 1, innerTupleExpr->mValues);
					}
					else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(initExpr))
					{
						SizedArray<BfExpression*, 1> values;
						values.Add(parenExpr->mExpression);						
						_HandleInitExprs(addr, curDim + 1, values);
					}
					else if (auto innerInitExpr = BfNodeDynCast<BfCollectionInitializerExpression>(initExpr))
					{
						_HandleInitExprs(addr, curDim + 1, innerInitExpr->mValues);
					}

					dimWriteIdx++;
					continue;
				}

				auto elemAddr = mModule->CreateIndexedValue(resultType, addr, writeIdx);
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
					storeValue = mModule->LoadValue(storeValue);
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
			if ((isUninit) && (mModule->IsOptimized()))
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
		};
		
		if (resultType->IsVar())
		{
			SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
			mResult = BfTypedValue(BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), mModule->GetPrimitiveType(BfTypeCode_Var)));
			_HandleInitExprs(mResult.mValue, 0, objCreateExpr->mArguments);
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

			_HandleInitExprs(arrayValue.mValue, 0, objCreateExpr->mArguments);
			
			mResult = arrayValue;
			return;
		}

		if (dimLengthVals.size() > 4)
		{
			dimLengthVals.RemoveRange(4, dimLengthVals.size() - 4);
			mModule->Fail("Too many array dimensions, consider using a jagged array.", objCreateExpr);
		}

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

		if (autoComplete != NULL)
		{
			SetAndRestoreValue<bool> prevCapturing(autoComplete->mIsCapturingMethodMatchInfo, false);
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
		_HandleInitExprs(addr, 0, objCreateExpr->mArguments);
		
		return;
	}
	else
	{
		if ((!resolvedTypeRef->IsObjectOrInterface()) && (!resolvedTypeRef->IsGenericParam()))
		{
			resultType = mModule->CreatePointerType(resolvedTypeRef);
		}
	}
	
	if ((isStackAlloc) && (mModule->mCurMethodState == NULL))
	{
		mModule->Fail("Cannot use 'stack' here", objCreateExpr->mNewNode);
		isStackAlloc = false;
		isScopeAlloc = false;
	}
	
	bool isGenericParam = unresolvedTypeRef->IsGenericParam();
	if (resolvedTypeRef->IsGenericParam())
	{
		auto genericConstraint = mModule->GetGenericParamInstance((BfGenericParamType*)resolvedTypeRef);
		if (genericConstraint->mTypeConstraint == NULL)
		{
			if ((genericConstraint->mGenericParamFlags & BfGenericParamFlag_New) == 0)
			{
				mModule->Fail(StrFormat("Must add 'where %s : new' constraint to generic parameter to instantiate type", genericConstraint->GetGenericParamDef()->mName.c_str()), objCreateExpr->mTypeRef);
			}
			if (objCreateExpr->mArguments.size() != 0)
			{
				mModule->Fail(StrFormat("Only default parameterless constructors can be called on generic argument '%s'", genericConstraint->GetGenericParamDef()->mName.c_str()), objCreateExpr->mTypeRef);
			}
		}
		
		resultType = resolvedTypeRef;		
		bool isValueType = ((genericConstraint->mGenericParamFlags & BfGenericParamFlag_Struct) != 0);
		if (genericConstraint->mTypeConstraint != NULL)
			isValueType = genericConstraint->mTypeConstraint->IsValueType();

		if (isValueType)		
			resultType = mModule->CreatePointerType(resultType);		
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
	//BfTypedValue emtpyThis(BfIRValue(), resolvedTypeRef, resolvedTypeRef->IsStruct());
	BfTypedValue emtpyThis(mModule->mBfIRBuilder->GetFakeVal(), resolvedTypeRef, resolvedTypeRef->IsStruct());
	
	BfResolvedArgs argValues(objCreateExpr->mOpenToken, &objCreateExpr->mArguments, &objCreateExpr->mCommas, objCreateExpr->mCloseToken);	
	ResolveArgValues(argValues, BfResolveArgFlag_DeferParamEval); ////
	
	/*SetAndRestoreValue<BfAttributeState*> prevAttributeState(mModule->mAttributeState, mPrefixedAttributeState);
	if (mPrefixedAttributeState != NULL)
		mPrefixedAttributeState->mUsed = true;*/
	if (typeInstance == NULL)
	{						
		// No CTOR needed
		if (objCreateExpr->mArguments.size() != 0)
		{
			mModule->Fail(StrFormat("Only default parameterless constructors can be called on primitive type '%s'", mModule->TypeToString(resolvedTypeRef).c_str()), objCreateExpr->mTypeRef);
		}
	}
	else if ((autoComplete != NULL) && (objCreateExpr->mOpenToken != NULL))
	{
		auto wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;
		autoComplete->CheckInvocation(objCreateExpr, objCreateExpr->mOpenToken, objCreateExpr->mCloseToken, objCreateExpr->mCommas);		
		MatchConstructor(objCreateExpr->mTypeRef, objCreateExpr, emtpyThis, typeInstance, argValues, false, true);
		if ((wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo))
		{
			autoComplete->mIsCapturingMethodMatchInfo = true;
			BF_ASSERT(autoComplete->mMethodMatchInfo != NULL);
		}
		else
			autoComplete->mIsCapturingMethodMatchInfo = false;
	}
	else
	{
		MatchConstructor(objCreateExpr->mTypeRef, objCreateExpr, emtpyThis, typeInstance, argValues, false, true);	
	}	
	mModule->ValidateAllocation(typeInstance, objCreateExpr->mTypeRef);

	//prevAttributeState.Restore();
	prevBindResult.Restore();
	
	/*if ((typeInstance != NULL) && (bindResult.mMethodInstance == NULL))
	{
		mModule->AssertErrorState();
		return;
	}*/

	int allocAlign = resolvedTypeRef->mAlign;
	if (typeInstance != NULL)
		allocAlign = typeInstance->mInstAlign;
	int appendAllocAlign = 0;

	if ((bindResult.mMethodInstance != NULL) && (bindResult.mMethodInstance->mMethodDef->mHasAppend))
	{	
		if (!bindResult.mFunc)
		{
			BF_ASSERT((!mModule->HasCompiledOutput()) || (mModule->mBfIRBuilder->mIgnoreWrites));
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
								
				appendSizeTypedValue = CreateCall(calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, irArgs);
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
	if (resolvedTypeRef->IsVar())
	{
		mResult = mModule->GetDefaultTypedValue(resultType);
		return;
	}
	else
	{
		if (isAppendAlloc)
			allocValue = mModule->AppendAllocFromType(resolvedTypeRef, appendSizeValue, appendAllocAlign);
		else
		{
			allocValue = mModule->AllocFromType(resolvedTypeRef, allocTarget, appendSizeValue, BfIRValue(), 0, BfAllocFlags_None, allocAlign);
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
				bool wantsCtorClear = true;
				if (mModule->mCompiler->mOptions.mEnableRealtimeLeakCheck)
				{
					// Dbg_ObjectAlloc clears internally so we don't need to call CtorClear for those
					if ((!isStackAlloc) && (!allocTarget.mCustomAllocator) && (allocTarget.mScopedInvocationTarget == NULL))
						wantsCtorClear = false;
				}
				
				if (wantsCtorClear)
				{
					auto ctorClear = mModule->GetMethodByName(typeInstance, "__BfCtorClear");
					if (!ctorClear)
					{
						mModule->AssertErrorState();
					}
					else
					{
						SizedArray<BfIRValue, 1> irArgs;
						irArgs.push_back(mResult.mValue);
						CreateCall(ctorClear.mMethodInstance, ctorClear.mFunc, false, irArgs);
					}
				}

				if ((isStackAlloc) && (mModule->mCompiler->mOptions.mEnableRealtimeLeakCheck))
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
										mModule->AddDeferredCall(removeStackObjMethod, irArgs, allocTarget.mScopeData, objCreateExpr);
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
			CreateCall(bindResult.mMethodInstance, bindResult.mFunc, false, bindResult.mIRArgs);
		}
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
	BfAllocTarget allocTarget = ResolveAllocTarget(boxExpr->mAllocNode, newToken);

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

		if (exprValue.mType->IsGenericParam())
		{
			BF_ASSERT(mModule->mCurMethodInstance->mIsUnspecialized);

			auto genericParamTarget = (BfGenericParamType*)exprValue.mType;
			auto genericParamInstance = mModule->GetGenericParamInstance(genericParamTarget);
			
			if ((genericParamInstance->mGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr | BfGenericParamFlag_Class)) == BfGenericParamFlag_Class)
				doWarn = true;

			if ((genericParamInstance->mTypeConstraint != NULL) && (genericParamInstance->mTypeConstraint->IsObjectOrInterface()))
				doWarn = true;
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
		
		BfType* boxedType = mModule->CreateBoxedType(exprValue.mType);
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

BfAllocTarget BfExprEvaluator::ResolveAllocTarget(BfAstNode* allocNode, BfTokenNode*& newToken, BfCustomAttributes** outCustomAttributes)
{
	auto autoComplete = GetAutoComplete();
	BfAttributeDirective* attributeDirective = NULL;

	BfAllocTarget allocTarget;
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
					autoComplete->CheckLabel(targetIdentifier, scopeNode->mColonToken);
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
		auto customAttrs = mModule->GetCustomAttributes(attributeDirective, BfAttributeTargets_Alloc, true, &allocTarget.mCaptureInfo);
		if (customAttrs != NULL)
		{
			for (auto& attrib : customAttrs->mAttributes)
			{
				if (attrib.mType->mTypeDef == mModule->mCompiler->mAlignAttributeTypeDef)
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
				else if (attrib.mType->mTypeDef == mModule->mCompiler->mFriendAttributeTypeDef)
					allocTarget.mIsFriend = true;
			}

			if (outCustomAttributes != NULL)
				*outCustomAttributes = customAttrs;
			else
				delete customAttrs;
		}
	}

	return allocTarget;
}

BfTypedValue BfExprEvaluator::MakeCallableTarget(BfAstNode* targetSrc, BfTypedValue target)
{		
	if (((target.mType->IsRef()) || (target.mType->IsPointer())) &&
		(target.mType->GetUnderlyingType()->IsStruct()))
	{		
		auto pointerType = (BfPointerType*) target.mType;
		target = mModule->LoadValue(target);
		target.mType = pointerType->mElementType;
		target.mKind = BfTypedValueKind_Addr;
	}

	if ((target.mType->IsStruct()) && (!target.IsAddr()))
	{
		target = mModule->MakeAddressable(target);
	}

	if (target.mType->IsVar())
	{
		target.mType = mModule->mContext->mBfObjectType;
		return target;
	}

	if ((target.mType->IsPointer()) && (target.mType->GetUnderlyingType()->IsObjectOrInterface()))
	{
		mModule->Fail(StrFormat("Methods cannot be called on type '%s' because the type is a pointer to a reference type (ie: a double-reference).", 
			mModule->TypeToString(target.mType).c_str()), targetSrc);
		target.mType = mModule->mContext->mBfObjectType;
		return target;
	}

	if (target.mType->IsWrappableType())
	{
		auto primStructType = mModule->GetWrappedStructType(target.mType);
		if (primStructType != NULL)
		{
			mModule->PopulateType(primStructType);
			target.mType = primStructType;
			if ((primStructType->IsSplattable()) && (!primStructType->IsTypedPrimitive()))
			{				
				if (target.IsAddr())
				{
					auto ptrType = mModule->CreatePointerType(primStructType);
					target = BfTypedValue(mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->MapType(ptrType)), primStructType, true);
				}
				else
					target.mKind = BfTypedValueKind_SplatHead;
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

BfModuleMethodInstance BfExprEvaluator::GetSelectedMethod(BfAstNode* targetSrc, BfTypeInstance* curTypeInst, BfMethodDef* methodDef, BfMethodMatcher& methodMatcher)
{
	bool failed = false;

	auto resolvedCurTypeInst = curTypeInst;//mModule->ResolveGenericType(curTypeInst)->ToTypeInstance();
	bool hasDifferentResolvedTypes = resolvedCurTypeInst != curTypeInst;
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
				if ((errorNode == NULL) && (!invocationExpr->mGenericArgs->mCommas.IsEmpty()))
					errorNode = invocationExpr->mGenericArgs->mCommas[(int)methodDef->mGenericParams.size() - 1];
			}
			mModule->Fail(StrFormat("Too many generic arguments, expected %d fewer", genericArgCountDiff), errorNode);
		}
		else if (genericArgCountDiff < 0)
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
				if (checkGenericIdx < rootMethodInstance->mMethodInfoEx->mMethodGenericArguments.size())
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
				genericArg = genericParam->mTypeConstraint;
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
 								auto constant = curTypeInst->mConstHolder->GetConstant(defaultVal); 								
								constExprVal.mValue = mModule->ConstantToCurrent(constant, curTypeInst->mConstHolder, genericParam->mTypeConstraint);
								genericArg = mModule->CreateConstExprValueType(constExprVal);
							}
						}
					}
				}

				if (genericArg == NULL)
				{
					failed = true;
					mModule->Fail(StrFormat("Unable to determine generic argument '%s'", methodDef->mGenericParams[checkGenericIdx]->mName.c_str()).c_str(), targetSrc);
					if ((genericParam->mTypeConstraint != NULL) && (!genericParam->mTypeConstraint->IsUnspecializedType()))
						genericArg = genericParam->mTypeConstraint;
					else
						genericArg = mModule->mContext->mBfObjectType;
				}
			}
			resolvedGenericArguments.push_back(genericArg);
		}
		else
		{	
			if (genericArg->IsIntUnknown())
				genericArg = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

			auto resolvedGenericArg = genericArg;//mModule->ResolveGenericType(genericArg);
			if (resolvedGenericArg != genericArg)
				hasDifferentResolvedTypes = true;
			resolvedGenericArguments.push_back(resolvedGenericArg);
		}
	}	

	BfTypeInstance* foreignType = NULL;
	BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None;

	if ((mModule->mAttributeState != NULL) && (mModule->mAttributeState->mCustomAttributes != NULL) && (mModule->mAttributeState->mCustomAttributes->Contains(mModule->mCompiler->mInlineAttributeTypeDef)))
	{
		flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForceInline);
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

	BfModuleMethodInstance methodInstance;
	if (methodMatcher.mBestMethodInstance)
	{
		methodInstance = methodMatcher.mBestMethodInstance;
	}
	else
	{		
		methodInstance = mModule->GetMethodInstance(resolvedCurTypeInst, methodDef, resolvedGenericArguments, flags, foreignType);	
	}
	if (mModule->mCompiler->IsSkippingExtraResolveChecks())
	{
		//BF_ASSERT(methodInstance.mFunc == NULL);
	}
	if (methodInstance.mMethodInstance == NULL)
		return NULL;	

	if (methodDef->IsEmptyPartial())
		return methodInstance;
	
	for (int checkGenericIdx = 0; checkGenericIdx < (int)methodMatcher.mBestMethodGenericArguments.size(); checkGenericIdx++)
	{
		auto& genericParams = methodInstance.mMethodInstance->mMethodInfoEx->mGenericParams;
		auto genericArg = methodMatcher.mBestMethodGenericArguments[checkGenericIdx];
		if (genericArg->IsVar())
			continue;
		
		BfAstNode* paramSrc;
		if (methodMatcher.mBestMethodGenericArgumentSrcs.size() == 0)
		{
			paramSrc = targetSrc;
		}
		else
			paramSrc = methodMatcher.mArguments[methodMatcher.mBestMethodGenericArgumentSrcs[checkGenericIdx]].mExpression;

		BfError* error = NULL;
		if ((!failed) && (!mModule->CheckGenericConstraints(BfGenericParamSource(methodInstance.mMethodInstance), genericArg, paramSrc, genericParams[checkGenericIdx], &methodMatcher.mBestMethodGenericArguments, &error)))
		{
			if (methodInstance.mMethodInstance->IsSpecializedGenericMethod())
			{
				// We mark this as failed to make sure we don't try to process a method that doesn't even follow the constraints
				methodInstance.mMethodInstance->mFailedConstraints = true;
			}
			if (methodInstance.mMethodInstance->mMethodDef->mMethodDeclaration != NULL)
			{
				if (error != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance.mMethodInstance->mMethodDef->GetRefNode());
			}
		}
	}

	return methodInstance;
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

void BfExprEvaluator::InjectMixin(BfAstNode* targetSrc, BfTypedValue target, bool allowImplicitThis, const StringImpl& name, const BfSizedArray<BfExpression*>& arguments, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArgs)
{
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
		if (argExpr != NULL)
			exprEvaluator->Evaluate(argExpr, false, false, true);
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
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo) && (autoComplete->mMethodMatchInfo->mInstanceList.size() != 0))
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

	if ((methodMatcher.mBestMethodDef == NULL) && (mModule->mContext->mCurTypeState != NULL))
	{
		BF_ASSERT(mModule->mCurTypeInstance == mModule->mContext->mCurTypeState->mTypeInstance);
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
	//
	{
		bool hasCircularRef = false;

		auto checkMethodState = curMethodState;
		while (checkMethodState != NULL)
		{			
			auto curMixinState = checkMethodState->mMixinState;
			while (curMixinState != NULL)
			{
				if (curMixinState->mSource == targetSrc)				
					hasCircularRef = true;									
				curMixinState = curMixinState->mPrevMixinState;
			}

			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mActiveDeferredLocalMethod != NULL))
			{
				for (auto& mixinRecord : checkMethodState->mClosureState->mActiveDeferredLocalMethod->mMixinStateRecords)
				{
					if (mixinRecord.mSource == targetSrc)					
						hasCircularRef = true;					
				}
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}

		if (hasCircularRef)
		{
			mModule->Fail("Circular reference detected between mixins", targetSrc);
			return;
		}
	}
	
	auto moduleMethodInstance = GetSelectedMethod(targetSrc, methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher);
	if (!moduleMethodInstance)
		return;
	auto methodInstance = moduleMethodInstance.mMethodInstance;

	// Check circular ref based on methodInstance
 	{
 		bool hasCircularRef = false;
 
 		auto checkMethodState = curMethodState;
 		while (checkMethodState != NULL)
 		{			
 			if (checkMethodState->mMethodInstance == methodInstance)
 				hasCircularRef = true;

			auto curMixinState = checkMethodState->mMixinState;
			while (curMixinState != NULL)
			{
				if (curMixinState->mMixinMethodInstance == methodInstance)				
					hasCircularRef = true;				
				curMixinState = curMixinState->mPrevMixinState;
			}


 			checkMethodState = checkMethodState->mPrevMethodState;
 		}
 
 		if (hasCircularRef)
 		{
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
			mModule->Fail(StrFormat("An instance reference is required to invoke the non-static mixin '%s'",
				mModule->MethodToString(methodInstance).c_str()), targetSrc);
		}
	}
	else
	{
		if (target)
		{
			mModule->Fail(StrFormat("Mixin '%s' cannot be accessed with an instance reference; qualify it with a type name instead", 
				mModule->MethodToString(methodInstance).c_str()), targetSrc);
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
		if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
			mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->GetRefNode());
		return;
	}
	else if ((int)args.size() > explicitParamCount)
	{
		BfError* error = mModule->Fail(StrFormat("Too many arguments specified, expected %d fewer.", (int)arguments.size() - explicitParamCount), targetSrc);
		if ((error != NULL) && (methodInstance->mMethodDef->mMethodDeclaration != NULL))
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
			mixinState->mTargetScope = targetScope;
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
	curMethodState->mIsEmbedded = false;
	// We can't flush scope state because we extend params in as arbitrary values
	mModule->NewScopeState(true, false);

	bool wantsDIData = (mModule->mBfIRBuilder->DbgHasInfo()) && (mModule->mHasFullDebugInfo);
	DISubprogram* diFunction = NULL;
	
	int mixinLocalVarIdx = -1;
	int startLocalIdx = (int)mModule->mCurMethodState->mLocals.size();
	int endLocalIdx = startLocalIdx;

	if (wantsDIData)
	{		
		BfIRMDNode diFuncType = mModule->mBfIRBuilder->DbgCreateSubroutineType(SizedArray<BfIRMDNode, 0>());

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
			curMethodState->mCurScope->mDIScope = mModule->mBfIRBuilder->DbgCreateFunction(diParentType, methodDef->mName + "!", "", mModule->mCurFilePosition.mFileInstance->mDIFile,
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

			newLocalVar->mIsAssigned = inLocalVar->mIsAssigned;
			newLocalVar->mUnassignedFieldFlags = inLocalVar->mUnassignedFieldFlags;
			newLocalVar->mReadFromId = inLocalVar->mReadFromId;
			newLocalVar->mIsReadOnly = inLocalVar->mIsReadOnly;

			if ((!newLocalVar->mIsAssigned) && (mModule->mCurMethodState->mDeferredLocalAssignData != NULL))
			{
				for (auto deferredAssign : mModule->mCurMethodState->mDeferredLocalAssignData->mAssignedLocals)
				{
					if (deferredAssign.mLocalVar == inLocalVar)
						newLocalVar->mIsAssigned = true;
				}
			}
		}
		else
		{
			mModule->AddLocalVariableDef(newLocalVar, hasConstValue);
		}

		if ((wantsDIData) && (!mModule->mBfIRBuilder->mIgnoreWrites))
		{
			mModule->UpdateSrcPos(methodDeclaration->mNameNode);
			if (hasConstValue)
			{
				// Already handled
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

						found = true;
						break;
					}

					checkMethodState = checkMethodState->mPrevMethodState;
				}
			}
			else if (mModule->IsTargetingBeefBackend())
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
						mModule->mBfIRBuilder->DbgInsertValueIntrinsic(aliasValue, diVariable);
						//mModule->mBfIRBuilder->DbgInsertValueIntrinsic(newLocalVar->mValue, diVariable);
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
	};

	argExprEvaluatorItr = argExprEvaluators.begin();
	for (int argIdx = 0; argIdx < (int)explicitParamCount; argIdx++)
	{
		int paramIdx = argIdx;

		auto exprEvaluator = *argExprEvaluatorItr;
		auto paramDef = methodDef->mParams[paramIdx];
		auto arg = &args[argIdx];

		if (!arg->mTypedValue)
		{
			auto wantType = methodInstance->GetParamType(paramIdx);
			if (wantType->IsVar())
				wantType = mModule->mContext->mBfObjectType;
			arg->mTypedValue = mModule->GetDefaultTypedValue(wantType);
		}

		BfLocalVariable* localVar = new BfLocalVariable();		
		localVar->mName = paramDef->mName;		
		localVar->mResolvedType = arg->mTypedValue.mType;				
		if (arg->mTypedValue.mType->IsRef())
		{
			auto refType = (BfRefType*)localVar->mResolvedType;
			localVar->mAddr = mModule->LoadValue(arg->mTypedValue).mValue;
			localVar->mResolvedType = arg->mTypedValue.mType->GetUnderlyingType();			
			localVar->mIsAssigned = refType->mRefKind != BfRefType::RefKind_Out;
		}
		else if (arg->mTypedValue.IsAddr())
			localVar->mAddr = arg->mTypedValue.mValue;
		else
		{
			if (!arg->mTypedValue.mValue)
			{
				// Untyped value				
			}
			else if (arg->mTypedValue.mValue.IsConst())
			{
				localVar->mConstValue = arg->mTypedValue.mValue;
			}
			else if (arg->mTypedValue.IsSplat())
			{
				localVar->mValue = arg->mTypedValue.mValue;
				localVar->mIsSplat = true;
			}
			else
			{
				if (arg->mTypedValue.IsAddr())
					localVar->mAddr = arg->mTypedValue.mValue;
				else
					localVar->mValue = arg->mTypedValue.mValue;
			}

			localVar->mIsReadOnly = arg->mTypedValue.IsReadOnly();
		}

		_AddLocalVariable(localVar, exprEvaluator);

		endLocalIdx++;
		++argExprEvaluatorItr;
	}

	if (auto blockBody = BfNodeDynCast<BfBlock>(methodDef->mBody))
		mModule->VisitCodeBlock(blockBody);

	if (mixinState->mResultExpr != NULL)
	{
		if (auto exprNode = BfNodeDynCast<BfExpression>(mixinState->mResultExpr))
		{
			//if ((exprNode->mTrailingSemicolon == NULL) && (!exprNode->IsA<BfBlock>()))
			if (!exprNode->IsA<BfBlock>())
			{
				// Mixin expression result
				mModule->UpdateSrcPos(exprNode);
				VisitChild(exprNode);
				FinishExpressionResult();
				ResolveGenericType();
				//mResult = mModule->LoadValue(mResult);				
			}
		}
	}

	if (!mResult)
	{
		// If we didn't have an expression body then just make the result "void"
		mResult = BfTypedValue(BfIRValue(), mModule->GetPrimitiveType(BfTypeCode_None));
	}
	
	int localIdx = startLocalIdx;
	if (mixinLocalVarIdx != -1)
	{
		BfLocalVariable* localVar = curMethodState->mLocals[localIdx];
		if (mResultLocalVar != NULL)
		{
			auto inLocalVar = mResultLocalVar;
			if (localVar->mIsAssigned)						
				inLocalVar->mIsAssigned = true;
			if (localVar->mReadFromId != -1)
				inLocalVar->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}

		localIdx++;
	}

	argExprEvaluatorItr = argExprEvaluators.begin();
	for ( ; localIdx < endLocalIdx; localIdx++)
	{
		auto exprEvaluator = *argExprEvaluatorItr;
		BfLocalVariable* localVar = curMethodState->mLocals[localIdx];
		//TODO: Merge unassigned flags together
		if ((exprEvaluator != NULL) && (exprEvaluator->mResultLocalVar != NULL))
		{
			auto inLocalVar = exprEvaluator->mResultLocalVar;
			if (localVar->mIsAssigned)			
				inLocalVar->mIsAssigned = true;
			if (localVar->mReadFromId != -1)
				inLocalVar->mReadFromId = mModule->mCurMethodState->GetRootMethodState()->mCurAccessId++;
		}

		++argExprEvaluatorItr;
	}

	if (auto blockBody = BfNodeDynCast<BfBlock>(methodDef->mBody))
		if (blockBody->mCloseBrace != NULL)
			mModule->UpdateSrcPos(blockBody->mCloseBrace);
	
	mModule->RestoreScopeState();	
	
	prevMixinState.Restore();

	if (mixinState->mHasDeferredUsage)
	{
// 		if (target)
// 		{
// 			if (target.mType->IsValuelessType())
// 				mixinState->mTarget = target;
// 			else
// 			{
// 				target = mModule->LoadValue(target);
// 				auto savedTarget = BfTypedValue(mModule->CreateAlloca(target.mType, false), target.mType, true);
// 				mModule->mBfIRBuilder->CreateStore(target.mValue, savedTarget.mValue);
// 				mixinState->mTarget = savedTarget;
// 			}				
// 		}
		mixinState->mTarget = BfTypedValue();
	}
	else
	{
		BF_ASSERT(rootMethodState->mMixinStates.back() == mixinState);
		rootMethodState->mMixinStates.pop_back();
		delete mixinState;
	}

	if ((scopedInvocationTarget != NULL) && (scopedInvocationTarget->mScopeName != NULL) && (!mixinState->mUsedInvocationScope))
	{
		mModule->Warn(0, "Scope specifier was not referenced in mixin", scopedInvocationTarget->mScopeName);
	}	

	// It's tempting to do this, but it is really just covering up other issues. 
	//mModule->mBfIRBuilder->SetCurrentDebugLocation(prevDebugLoc);	

	// But does THIS work?
	mModule->mBfIRBuilder->RestoreDebugLocation();
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

void BfExprEvaluator::DoInvocation(BfAstNode* target, BfMethodBoundExpression* methodBoundExpr, const BfSizedArray<BfExpression*>& args, BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments, BfTypedValue* outCascadeValue)
{
	// Just a check
	mModule->mBfIRBuilder->GetInsertBlock();

	bool wasCapturingMethodInfo = false;
	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (methodGenericArguments != NULL))
	{
		for (BfTypeReference* methodGenericArg : *methodGenericArguments)
			autoComplete->CheckTypeRef(methodGenericArg, false, true);
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
				autoComplete->CheckLabel(identifier, scopedTarget->mColonToken);
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
			if (mExpectingType != NULL)
			{				
				if (mExpectingType->IsSizedArray())
				{
					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							InitializedSizedArray((BfSizedArrayType*)mExpectingType, invocationExpr->mOpenParen, invocationExpr->mArguments, invocationExpr->mCommas, invocationExpr->mCloseParen);
							return;
						}
					}
				}
				else if (mExpectingType->IsStruct())
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

							BfResolveArgFlags resolveArgsFlags = BfResolveArgFlag_DeferParamEval;
							ResolveArgValues(argValues, resolveArgsFlags);

							if ((mReceivingValue != NULL) && (mReceivingValue->mType == mExpectingType) && (mReceivingValue->IsAddr()))
							{
								mResult = *mReceivingValue;
								mReceivingValue = NULL;
							}
							else
								mResult = BfTypedValue(mModule->CreateAlloca(mExpectingType), mExpectingType, BfTypedValueKind_TempAddr);
							MatchConstructor(target, methodBoundExpr, mResult, mExpectingType->ToTypeInstance(), argValues, false, false);
							mModule->ValidateAllocation(mExpectingType, invocationExpr->mTarget);

							return;
						}
					}
				}
				else if (mExpectingType->IsGenericParam())
				{
					if (mModule->mParentNodeEntry != NULL)
					{
						if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(mModule->mParentNodeEntry->mNode))
						{
							BfResolvedArgs argValues(invocationExpr->mOpenParen, &invocationExpr->mArguments, &invocationExpr->mCommas, invocationExpr->mCloseParen);

							BfResolveArgFlags resolveArgsFlags = BfResolveArgFlag_None;
							ResolveArgValues(argValues, resolveArgsFlags);

							CheckGenericCtor((BfGenericParamType*)mExpectingType, argValues, invocationExpr->mTarget);
							mResult = mModule->GetDefaultTypedValue(mExpectingType);
							return;
						}
					}
				}
				else
				{
					gaveUnqualifiedDotError = true;
					mModule->Fail(StrFormat("Cannot use inferred constructor on type '%s'", mModule->TypeToString(mExpectingType).c_str()), memberRefExpression->mDotToken);
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
			attributeState.mCustomAttributes = mModule->GetCustomAttributes(attrIdentifier->mAttributes, attributeState.mTarget);
			if (attrIdentifier->mIdentifier != NULL)
				targetFunctionName = attrIdentifier->mIdentifier->ToString();
		}
		else if (memberRefExpression->mMemberName != NULL)
			targetFunctionName = memberRefExpression->mMemberName->ToString();

		if (memberRefExpression->mTarget == NULL)
		{
			if (mExpectingType)
				mResult = BfTypedValue(mExpectingType);
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
				type = mModule->ResolveTypeRef(qualifiedName->mLeft, NULL, BfPopulateType_DataAndMethods, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_IgnoreLookupError));
			}

			if (type == NULL)
			{
				//SetAndRestoreValue<bool> prevIgnoreErrors(mModule->mIgnoreErrors, true);
				
				type = mModule->ResolveTypeRef(qualifiedName, methodGenericArguments, BfPopulateType_DataAndMethods, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_IgnoreLookupError));
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
			thisValue = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
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
				if ((typeInst != NULL) && (typeInst->mTypeDef == mModule->mCompiler->mStringTypeDef))
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
			if (invocationTypeInst->mTypeDef->mIsDelegate)
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
		InjectMixin(methodNodeSrc, thisValue, allowImplicitThis, targetFunctionName, args, methodGenericArguments);

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
	if (thisValue.mType != NULL)
	{
		auto checkTypeInst = thisValue.mType->ToTypeInstance();
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
					methodDef = methodDef->mNextWithSameName;
				}
			}
			checkTypeInst = checkTypeInst->mBaseType;
		}
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

	BfResolveArgFlags resolveArgsFlags = (BfResolveArgFlags)(BfResolveArgFlag_DeferFixits | BfResolveArgFlag_AllowUnresolvedTypes);	
	resolveArgsFlags = (BfResolveArgFlags)(resolveArgsFlags | BfResolveArgFlag_DeferParamEval);
	if (mayBeSkipCall)
		resolveArgsFlags = (BfResolveArgFlags)(resolveArgsFlags | BfResolveArgFlag_DeferParamValues);

	static int sCallIdx = 0;
	sCallIdx++;
	int callIdx = sCallIdx;
	if (callIdx == 1557)
	{
		NOP;
	}
	
	BfCheckedKind checkedKind = BfCheckedKind_NotSet;
	if (attributeState.mCustomAttributes != NULL)
	{
		if (attributeState.mCustomAttributes->Contains(mModule->mCompiler->mCheckedAttributeTypeDef))
			checkedKind = BfCheckedKind_Checked;
		if (attributeState.mCustomAttributes->Contains(mModule->mCompiler->mUncheckedAttributeTypeDef))
			checkedKind = BfCheckedKind_Unchecked;
	}

	SetAndRestoreValue<bool> prevUsedAsStatement(mUsedAsStatement, mUsedAsStatement || isCascade);
	ResolveArgValues(argValues, resolveArgsFlags);
	mResult = MatchMethod(methodNodeSrc, methodBoundExpr, thisValue, allowImplicitThis, bypassVirtual, targetFunctionName, argValues, methodGenericArguments, checkedKind);		
	argValues.HandleFixits(mModule);

	if (isCascade)
	{
		if (outCascadeValue != NULL)
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

				if (indexerExpr->mArguments.size() != 0)
				{
					BfConstResolver constResolver(mModule);
					auto arg = indexerExpr->mArguments[0];

					if (arg != NULL)
						constResolver.Resolve(arg);

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
	BfSizedArray<ASTREF(BfTypeReference*)>* methodGenericArguments = NULL;
	if (invocationExpr->mGenericArgs != NULL)
		methodGenericArguments = &invocationExpr->mGenericArgs->mGenericArgs;
	SizedArray<BfExpression*, 8> copiedArgs;
	for (BfExpression* arg : invocationExpr->mArguments)
		copiedArgs.push_back(arg);			

	BfTypedValue cascadeValue;
	DoInvocation(invocationExpr->mTarget, invocationExpr, copiedArgs, methodGenericArguments, &cascadeValue);

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

BfMethodDef* BfExprEvaluator::GetPropertyMethodDef(BfPropertyDef* propDef, BfMethodType methodType, BfCheckedKind checkedKind)
{
	BfMethodDef* matchedMethod = NULL;
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
	return matchedMethod;
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
				// This is an explicit call to a default static interface method.  We pull the methodDef into our own concrete type.			
				mPropTarget = mModule->GetThis();
				return mModule->GetMethodInstance(mModule->mCurTypeInstance, methodDef, BfTypeVector(), BfGetMethodInstanceFlag_ForeignMethodDef, curTypeInst);
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
			auto rawMethodInstance = mModule->GetRawMethodInstance(propTypeInst, methodDef);

			auto useTypeInst = mOrigPropTarget.mType->ToTypeInstance();			
			auto virtualMethod = (BfMethodInstance*)useTypeInst->mVirtualMethodTable[rawMethodInstance->mVirtualTableIdx].mImplementingMethod;
			return mModule->ReferenceExternalMethodInstance(virtualMethod);
		}
	}
	
	if ((mOrigPropTarget) && (mOrigPropTarget.mType != mPropTarget.mType) &&
		((!mOrigPropTarget.mType->IsGenericParam()) && (mPropTarget.mType->IsInterface())))
	{
		auto checkType = mOrigPropTarget.mType;
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

				for (auto& iface : checkTypeInst->mInterfaces)
				{
					if (!checkTypeInst->IsTypeMemberAccessible(iface.mDeclaringType, activeTypeDef))
						continue;

					if (iface.mInterfaceType == mPropTarget.mType)
					{
						if (bestIFaceEntry == NULL)
						{
							bestIFaceEntry = &iface;
							continue;
						}

						bool isBetter;
						bool isWorse;
						mModule->CompareDeclTypes(iface.mDeclaringType, bestIFaceEntry->mDeclaringType, isBetter, isWorse);						
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
					mPropTarget.mType = checkTypeInst;
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
			auto genericTypeInst = (BfGenericTypeInstance*)mPropTarget.mType;
			if (genericTypeInst->mTypeDef == mModule->mCompiler->mSizedArrayTypeDef)
			{				
				if (mPropDef->mName == "Count")
				{
					auto sizedType = genericTypeInst->mTypeGenericArguments[1];
					if (sizedType->IsConstExprValue())
					{
						auto constExprType = (BfConstExprValueType*)sizedType;						
						mResult = BfTypedValue(mModule->GetConstValue(constExprType->mValue.mInt64), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
						handled = true;						
					}
					else
					{
						BF_ASSERT(mModule->mCurMethodInstance->mIsUnspecialized);
						mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(BfTypeCode_IntPtr), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
						handled = true;
					}
				}
			}
		}

		if (!handled)
		{
			BfMethodDef* matchedMethod = GetPropertyMethodDef(mPropDef, BfMethodType_PropertyGetter, mPropCheckedKind);
			if (matchedMethod == NULL)
			{
				mModule->Fail("Property has no getter", mPropSrc);
				return mResult;
			}

			auto methodInstance = GetPropertyMethodInstance(matchedMethod);
			if (methodInstance.mMethodInstance == NULL)
				return mResult;
			if (!mModule->mBfIRBuilder->mIgnoreWrites)
			{
				BF_ASSERT(!methodInstance.mFunc.IsFake());
			}

			if (mPropSrc != NULL)
				mModule->UpdateExprSrcPos(mPropSrc);
			
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
					if ((mPropDefBypassVirtual) && (mPropTarget.mType != methodInstance.mMethodInstance->GetOwner()))
						mPropTarget = mModule->Cast(mPropSrc, mOrigPropTarget, methodInstance.mMethodInstance->GetOwner());

					if ((mPropGetMethodFlags & BfGetMethodInstanceFlag_DisableObjectAccessChecks) == 0)
						mModule->EmitObjectAccessCheck(mPropTarget);
					PushThis(mPropSrc, mPropTarget, methodInstance.mMethodInstance, args);
				}

				bool failed = false;
				for (int paramIdx = 0; paramIdx < (int)mIndexerValues.size(); paramIdx++)
				{
					auto refNode = mIndexerValues[paramIdx].mExpression;
					if (refNode == NULL)
						refNode = mPropSrc;
					auto val = mModule->Cast(refNode, mIndexerValues[paramIdx].mTypedValue, methodInstance.mMethodInstance->GetParamType(paramIdx));
					if (!val)
						failed = true;
					else
						PushArg(val, args);
				}

				if (mPropDefBypassVirtual)
				{
					auto methodDef = methodInstance.mMethodInstance->mMethodDef;
					if ((methodDef->mIsAbstract) && (mPropDefBypassVirtual))
					{
						mModule->Fail(StrFormat("Abstract base property method '%s' cannot be invoked", mModule->MethodToString(methodInstance.mMethodInstance).c_str()), mPropSrc);
					}
				}

				if (failed)
				{
					auto returnType = methodInstance.mMethodInstance->mReturnType;
					auto methodDef = methodInstance.mMethodInstance->mMethodDef;
					if (returnType->IsRef())
					{
						auto result = mModule->GetDefaultTypedValue(returnType->GetUnderlyingType(), true, BfDefaultValueKind_Addr);
						if (methodDef->mIsReadOnly)
							result.mKind = BfTypedValueKind_ReadOnlyAddr;
						return result;
					}
					else
					{
						auto val = mModule->GetDefaultTypedValue(returnType, true, methodInstance.mMethodInstance->HasStructRet() ? BfDefaultValueKind_Addr : BfDefaultValueKind_Value);
						if (val.mKind == BfTypedValueKind_Addr)
							val.mKind = BfTypedValueKind_TempAddr;
						return val;
					}
				}
				else
					mResult = CreateCall(methodInstance.mMethodInstance, methodInstance.mFunc, mPropDefBypassVirtual, args);
				if (mResult.mType != NULL)
				{
					if ((mResult.mType->IsVar()) && (mModule->mCompiler->mIsResolveOnly))
						mModule->Fail("Property type reference failed to resolve", mPropSrc);
					BF_ASSERT(!mResult.mType->IsRef());
				}
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

			if (!localVar->mIsAssigned)
			{
				mModule->TryLocalVariableInit(localVar);
			}

			if (!localVar->mIsAssigned)
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
							int assignedFieldIdx = (assignedVar.mLocalVarField) - 1;
							if (assignedFieldIdx >= 0)
								undefinedFieldFlags &= ~((int64)1 << assignedFieldIdx);
							else
								undefinedFieldFlags = 0;
						}
					}
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
		int fieldIdx = mResultLocalVarField  - 1;
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

	auto autoComplete = GetAutoComplete();
	if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(checkNode)))
	{
		if ((mModule->mCurTypeInstance != NULL) && (mModule->mCurTypeInstance->mBaseType != NULL))
			autoComplete->SetDefinitionLocation(mModule->mCurTypeInstance->mBaseType->mTypeDef->GetRefNode());
	}

	return checkNode->Equals("base");
}

bool BfExprEvaluator::CheckModifyResult(BfTypedValue typedVal, BfAstNode* refNode, const char* modifyType, bool onlyNeedsMut)
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

	bool canModify = (((typedVal.IsAddr()) || (typedVal.mType->IsValuelessType())) &&
		(!typedVal.IsReadOnly()));

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
							error = mModule->Fail(StrFormat("Cannot %s 'this' within struct lambda. Consider adding 'mut' specifier to this method.", modifyType), refNode);
						else
							error = mModule->Fail(StrFormat("Cannot %s 'this' within struct lambda. Consider adding by-reference capture specifier [&] to lambda.", modifyType), refNode);
					}
					else
					{
						error = mModule->Fail(StrFormat("Cannot %s 'this' within struct method '%s'. Consider adding 'mut' specifier to this method.", modifyType,
			                mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					}
					return false;
				}
				else if (mResultFieldInstance != NULL)
				{															
					if (isCapturedLocal)
					{
						error = mModule->Fail(StrFormat("Cannot %s read-only captured local variable '%s'. Consider adding by-reference capture specifier [&] to lambda and ensuring that captured value is not read-only.", modifyType,
							mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
					}
					else if (isClosure)
					{
						if (!mModule->mCurMethodState->mClosureState->mDeclaringMethodIsMutating)
							error = mModule->Fail(StrFormat("Cannot %s field '%s.%s' within struct lambda. Consider adding 'mut' specifier to this method.", modifyType,
								mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
						else
							error = mModule->Fail(StrFormat("Cannot %s field '%s.%s' within struct lambda. Consider adding by-reference capture specifier [&] to lambda.", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str()), refNode);
					}
					else if (mResultFieldInstance->GetFieldDef()->mIsReadOnly)
					{
						error = mModule->Fail(StrFormat("Cannot %s readonly field '%s.%s' within method '%s'", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str(),
							mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					}
					else if (auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(mResultFieldInstance->GetFieldDef()->mFieldDeclaration))
					{
						String propNam;
						if (propertyDeclaration->mNameNode != NULL)
							propertyDeclaration->mNameNode->ToString(propNam);

						error = mModule->Fail(StrFormat("Cannot %s auto-implemented property '%s.%s' without set accessor", modifyType,
							mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), propNam.c_str()), refNode);
					}
					else
					{
						error = mModule->Fail(StrFormat("Cannot %s field '%s.%s' within struct method '%s'. Consider adding 'mut' specifier to this method.", modifyType,
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
					if ((localVar->mResolvedType->IsGenericParam()) && (onlyNeedsMut))
						error = mModule->Fail(StrFormat("Cannot %s parameter '%s'. Consider adding 'mut' or 'ref' specifier to parameter or copying to a local variable.", modifyType,
							localVar->mName.c_str(), mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					else
						error = mModule->Fail(StrFormat("Cannot %s parameter '%s'. Consider adding 'ref' specifier to parameter or copying to a local variable.", modifyType,
							localVar->mName.c_str(), mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);
					return false;
				}
			}
			else
			{
				error = mModule->Fail(StrFormat("Cannot %s read-only local variable '%s'.", modifyType,
					localVar->mName.c_str()), refNode);
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
		auto error = mModule->Fail(StrFormat("Cannot %s static readonly field '%s.%s' within method '%s'", modifyType,
			mModule->TypeToString(mResultFieldInstance->mOwner).c_str(), mResultFieldInstance->GetFieldDef()->mName.c_str(),
			mModule->MethodToString(mModule->mCurMethodInstance).c_str()), refNode);

		return false;
	}
	

	return mModule->CheckModifyValue(typedVal, refNode, modifyType);
}

void BfExprEvaluator::Visit(BfConditionalExpression* condExpr)
{
	static int sCallCount = 0;
	sCallCount++;
	
	auto condResult = mModule->CreateValueFromExpression(condExpr->mConditionExpression, mModule->GetPrimitiveType(BfTypeCode_Boolean));
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

		BfTypedValue actualValue = mModule->CreateValueFromExpression(actualExpr, mExpectingType, BfEvalExprFlags_NoCast);
		BfTypedValue ignoredValue;
		//
		{			
			auto curBlock = mModule->mBfIRBuilder->GetInsertBlock();
			SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
			ignoredValue = mModule->CreateValueFromExpression(ignoredExpr, mExpectingType, BfEvalExprFlags_NoCast);
			mModule->mBfIRBuilder->SetInsertPoint(curBlock);
		}

		if (!actualValue)
			return;
		if ((ignoredValue) && (ignoredValue.mType != actualValue.mType))
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

	mModule->AddBasicBlock(trueBB);	
	auto trueValue = mModule->CreateValueFromExpression(condExpr->mTrueExpression, mExpectingType, (BfEvalExprFlags)(BfEvalExprFlags_NoCast | BfEvalExprFlags_CreateConditionalScope));
	if ((mExpectingType != NULL) && (trueValue) && (trueValue.mType != mExpectingType))
	{
		// In some cases like typed primitives - we CAN individually cast each value which it's a constant still, but not after the merging
		// IE: Color c = isOver ? 0xFF000000 : 0xFFFFFFFF;
		//  Otherwise the resulting value would just be 'int' which cannot implicitly convert to Color, but each of those ints can be 
		//  a uint32 if converted separately		
		auto checkTrueValue = mModule->Cast(condExpr->mTrueExpression, trueValue, mExpectingType, BfCastFlags_SilentFail);
		if (checkTrueValue)
			trueValue = checkTrueValue;		
	}
	auto trueBlockPos = mModule->mBfIRBuilder->GetInsertBlock();

	mModule->AddBasicBlock(falseBB);
	auto falseValue = mModule->CreateValueFromExpression(condExpr->mFalseExpression, mExpectingType, (BfEvalExprFlags)(BfEvalExprFlags_NoCast | BfEvalExprFlags_CreateConditionalScope));
	auto falseBlockPos = mModule->mBfIRBuilder->GetInsertBlock();
 	if ((mExpectingType != NULL) && (falseValue) && (falseValue.mType != mExpectingType))
 	{
 		auto checkFalseValue = mModule->Cast(condExpr->mFalseExpression, falseValue, mExpectingType, BfCastFlags_SilentFail);
 		if (checkFalseValue)
 			falseValue = checkFalseValue; 		
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
		phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(trueValue.mType), 2);
		mModule->mBfIRBuilder->AddPhiIncoming(phi, trueValue.mValue, trueBlockPos);
		mModule->mBfIRBuilder->AddPhiIncoming(phi, falseValue.mValue, falseBlockPos);		
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

				auto setMethod = GetPropertyMethodDef(exprEvaluator->mPropDef, BfMethodType_PropertySetter, mPropCheckedKind);
				if (setMethod != NULL)
				{
					auto methodInstance = mModule->GetMethodInstance(propTypeInst, setMethod, BfTypeVector());
					resultType = methodInstance.mMethodInstance->GetParamType(0);
				}
				else 
				{
					auto getMethod = GetPropertyMethodDef(exprEvaluator->mPropDef, BfMethodType_PropertyGetter, mPropCheckedKind);
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

	BfTupleType* tupleType = mModule->CreateTupleType(fieldTypes, fieldNames);
	deferredTupleAssignData.mTupleType = tupleType;	
}

void BfExprEvaluator::AssignDeferrredTupleAssignData(BfAssignmentExpression* assignExpr, DeferredTupleAssignData& deferredTupleAssignData, BfTypedValue rightValue)
{	
	BF_ASSERT(rightValue.mType->IsTuple());
	auto tupleType = (BfTupleType*)rightValue.mType;
	for (int valueIdx = 0; valueIdx < (int)deferredTupleAssignData.mChildren.size(); valueIdx++)
	{			
		auto& child = deferredTupleAssignData.mChildren[valueIdx];
		BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[valueIdx];		
		BfTypedValue elementValue;
		if (fieldInstance->mDataIdx >= 0)
		{	
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

		if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(child.mExpr))
		{
			if (!elementValue)
				elementValue = mModule->GetDefaultTypedValue(fieldInstance->GetResolvedType());
			mModule->HandleVariableDeclaration(varDecl, elementValue);			
		}
	}
}

void BfExprEvaluator::DoTupleAssignment(BfAssignmentExpression* assignExpr)
{
	auto tupleExpr = BfNodeDynCast<BfTupleExpression>(assignExpr->mLeft);

	DeferredTupleAssignData deferredTupleAssignData;
	PopulateDeferrredTupleAssignData(tupleExpr, deferredTupleAssignData);
	BfTupleType* tupleType = deferredTupleAssignData.mTupleType;
	
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
	if ((autoComplete != NULL) && (autoComplete->mResolveType == BfResolveType_GetFixits))																								 
	{
		SetAndRestoreValue<bool> ignoreFixits(autoComplete->mIgnoreFixits, true);
		VisitChild(targetNode);
		deferredFixits = true;
	}
	else if (!evaluatedLeft)
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
	auto ptr = mResult;	
	mResult = BfTypedValue();
	if (mPropDef == NULL)
	{
		if (!CheckModifyResult(ptr, assignExpr->mOpToken, "assign to"))
		{
			if (assignExpr->mRight != NULL)
				mModule->CreateValueFromExpression(assignExpr->mRight, ptr.mType, BfEvalExprFlags_NoCast);
			return;
		}
	}	
		
	if (mPropDef != NULL)
	{
		bool hasLeftVal = false;

		auto propDef = mPropDef;
		auto propTarget = mPropTarget;

		auto setMethod = GetPropertyMethodDef(mPropDef, BfMethodType_PropertySetter, mPropCheckedKind);
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
			BF_ASSERT(methodInstance.mMethodInstance->mMethodDef == setMethod);
			CheckPropFail(setMethod, methodInstance.mMethodInstance, (mPropGetMethodFlags & BfGetMethodInstanceFlag_Friend) == 0);	

			BfTypedValue convVal;
			if (binaryOp != BfBinaryOp_None)
			{
				PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, BfBinOpFlag_ForceLeftType);
				if (!mResult)
					return;
				convVal = mResult;
				mResult = BfTypedValue();
				if (!convVal)
					return;
			}			
			else
			{
				auto wantType = methodInstance.mMethodInstance->GetParamType(methodInstance.mMethodInstance->GetParamCount() - 1);
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
					convVal = mModule->CreateValueFromExpression(assignExpr->mRight, wantType, (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_PendingPropSet));
				}
				if (!convVal)
				{
					mPropDef = NULL;
					return;
				}
			}					

			if (mPropSrc != NULL)
				mModule->UpdateExprSrcPos(mPropSrc);

			SizedArray<BfIRValue, 4> args;
			if (!setMethod->mIsStatic)
			{
				mModule->EmitObjectAccessCheck(mPropTarget);
				PushThis(mPropSrc, mPropTarget, methodInstance.mMethodInstance, args);				
			}
	
			for (int paramIdx = 0; paramIdx < (int)mIndexerValues.size(); paramIdx++)
			{
				auto refNode = mIndexerValues[paramIdx].mExpression;
				if (refNode == NULL)
					refNode = mPropSrc;
				auto val = mModule->Cast(refNode, mIndexerValues[paramIdx].mTypedValue, methodInstance.mMethodInstance->GetParamType(paramIdx));
				if (!val)
				{
					mPropDef = NULL;
					return;
				}
				PushArg(val, args);
			}
	
			PushArg(convVal, args);

			if (methodInstance)
				CreateCall(methodInstance.mMethodInstance, methodInstance.mFunc, mPropDefBypassVirtual, args);
	
			mPropDef = NULL;
			mResult = convVal;
			return;
		}
	}

	auto toType = ptr.mType;
	if (toType->IsRef())
	{
		auto refType = (BfRefType*)toType;
		toType = refType->mElementType;
	}
	
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
		
		auto expectedType = ptr.mType;
		if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
			expectedType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

		if ((!rightValue) && (assignExpr->mRight != NULL))
		{
			rightValue = mModule->CreateValueFromExpression(assignExpr->mRight, expectedType, (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_NoCast));
		}

		bool handled = false;

		if (rightValue)
		{
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
					if (!mModule->CanCast(rightValue, paramType))
						continue;

					auto moduleMethodInstance = mModule->GetMethodInstance(checkTypeInst, operatorDef, BfTypeVector());

					BfExprEvaluator exprEvaluator(mModule);
					SizedArray<BfIRValue, 1> args;
					exprEvaluator.PushThis(assignExpr->mLeft, leftValue, moduleMethodInstance.mMethodInstance, args);
					exprEvaluator.PushArg(rightValue, args);
					exprEvaluator.CreateCall(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, args);
					convVal = leftValue;
					handled = true;
					break;
				}

				if (handled)
					break;
				checkTypeInst = mModule->GetBaseType(checkTypeInst);
			}

			if (!handled)
			{
				leftValue = mModule->LoadValue(leftValue);
				PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, BfBinOpFlag_ForceLeftType, leftValue, rightValue);
			}
		}
		
		if (!handled)
		{
			convVal = mResult;
			mResult = BfTypedValue();			
		}

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
					//ptr = mModule->LoadValue(ptr);
					BF_ASSERT(ptr.IsAddr());
					convVal = mModule->LoadValue(convVal);
					auto storeInst = mModule->mBfIRBuilder->CreateAlignedStore(convVal.mValue, ptr.mValue, ptr.mType->mAlign, mIsVolatileReference);
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
			if ((initCountDiff != 0) && (!failedAt.Contains(depth)))
			{
				if (initCountDiff > 0)
				{
					mModule->Fail(StrFormat("Too many initializers, expected %d fewer", initCountDiff), valueExprs[(int)checkArrayType->mElementCount]);
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
						bool tryDefer = false;						
 						if ((checkArrayType->IsComposite()) && 
 							((expr->IsA<BfInvocationExpression>()) || (expr->IsExact<BfTupleExpression>())))
 						{
							// We evaluate with a new scope because this expression may create variables that we don't want to be visible to other
							//  non-deferred evaluations (since the value may actually be a FakeVal)
							SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
							elementValue = mModule->CreateValueFromExpression(expr, checkArrayType->mElementType, BfEvalExprFlags_CreateConditionalScope);
							deferredValue = !prevIgnoreWrites.mPrevVal && elementValue.mValue.IsFake();
 						}
						else
						{
							elementValue = mModule->CreateValueFromExpression(expr, checkArrayType->mElementType);
						}

						if (!elementValue)
							elementValue = mModule->GetDefaultTypedValue(checkArrayType->mElementType);						

						if ((!elementValue) || (!CheckAllowValue(elementValue, expr)))
							elementValue = mModule->GetDefaultTypedValue(checkArrayType->mElementType);

						// For now, we can't properly create const-valued non-size-aligned composites
						if (checkArrayType->mElementType->NeedsExplicitAlignment())
							isAllConst = false;

						if (!elementValue.mValue.IsConst())
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
			
			return mModule->mBfIRBuilder->CreateConstArray(mModule->mBfIRBuilder->MapType(checkArrayType), members);
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
			_CreateMemArray(mResult, openToken, valueExprs, commas, closeToken);
		}
	}
}

void BfExprEvaluator::Visit(BfTupleExpression* tupleExpr)
{	
	BfTupleType* tupleType = NULL;
	bool hadFullMatch = false;
	if ((mExpectingType != NULL) && (mExpectingType->IsTuple()))
	{
		tupleType = (BfTupleType*)mExpectingType;
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
				fieldNames.push_back(requestedName->mNameNode->ToString());
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
		curTupleValue = mModule->CreateAlloca(tupleType);
		mResultIsTempComposite = true;
		mResult = BfTypedValue(curTupleValue, tupleType, BfTypedValueKind_TempAddr);
	}

	for (int valueIdx = 0; valueIdx < (int)typedValues.size(); valueIdx++)
	{
		BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[valueIdx];
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
			
			if (typedVal.IsSplat())
				mModule->AggregateSplatIntoAddr(typedVal, memberVal);
			else
				mModule->mBfIRBuilder->CreateStore(typedVal.mValue, memberVal);
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
	
	auto opResult = PerformUnaryOperation_TryOperator(thisValue, NULL, BfUnaryOp_NullConditional, dotToken);
	if (opResult)
		thisValue = opResult;

	//TODO: But make null conditional work for Nullable types
	if (thisValue.mType->IsNullable())
	{
		// Success
	}
	else if ((thisValue.mType->IsPointer()) || (thisValue.mType->IsObjectOrInterface()))
	{
		// Also good
	}
	else
	{		
		mModule->Warn(0, StrFormat("Null conditional reference is unnecessary since value type '%s' can never be null", mModule->TypeToString(thisValue.mType).c_str()), dotToken);		
		return thisValue;
	}	

	thisValue = mModule->LoadValue(thisValue);

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
		BfGenericTypeInstance* nullableType = (BfGenericTypeInstance*)thisValue.mType->ToTypeInstance();
		auto elementType = nullableType->GetUnderlyingType();
		if (elementType->IsValuelessType())
		{
			thisValue = mModule->MakeAddressable(thisValue);
			BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 1); // mHasValue
			isNotNull = mModule->mBfIRBuilder->CreateLoad(hasValuePtr);			
			thisValue = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), elementType, true);
		}
		else
		{
			thisValue = mModule->MakeAddressable(thisValue);
			BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 2); // mHasValue
			isNotNull = mModule->mBfIRBuilder->CreateLoad(hasValuePtr);
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
			autoComplete->CheckMemberReference(memberRefExpr->mTarget, memberRefExpr->mDotToken, memberRefExpr->mMemberName);

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
			mModule->Fail("Unqualified dot syntax can only be used when the result type can be inferred", nameRefNode);
			return;
		}

		if (expectingTypeInst == NULL)
		{
			mModule->Fail(StrFormat("Unqualified dot syntax cannot be used with type '%s'", mModule->TypeToString(mExpectingType).c_str()), nameRefNode);
			return;
		}

		if (mExpectingType->IsVar())
		{
			mResult = mModule->GetDefaultTypedValue(mExpectingType);
			return;
		}

		BfTypedValue expectingVal(expectingTypeInst);
		mResult = LookupField(memberRefExpr->mMemberName, expectingVal, findName);
		if ((mResult) || (mPropDef != NULL))
			return;
	}

	bool isNullCondLookup = (memberRefExpr->mDotToken != NULL) && (memberRefExpr->mDotToken->GetToken() == BfToken_QuestionDot);
	bool isCascade = ((memberRefExpr->mDotToken != NULL) && (memberRefExpr->mDotToken->GetToken() == BfToken_DotDot));
		
	BfIdentifierNode* nameLeft = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mTarget);
	BfIdentifierNode* nameRight = BfIdentifierCast(memberRefExpr->mMemberName);
	if ((nameLeft != NULL) && (nameRight != NULL) && (!isNullCondLookup) && (!isCascade))
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
				thisValue = BfTypedValue(mModule->ResolveTypeRef(targetIdentifier, NULL));
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
			mModule->Fail("Unable to find member", nameRefNode);
	}

	if (isNullCondLookup)
		mResult = GetResult();

	if (isCascade)
	{
		if (outCascadeValue != NULL)
			*outCascadeValue = thisValue;
		else
			mModule->Fail("Unexpected cascade operation. Chaining can only be used for method invocations", memberRefExpr->mDotToken);
	}
}

void BfExprEvaluator::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	DoMemberReference(memberRefExpr, NULL);
}

void BfExprEvaluator::Visit(BfIndexerExpression* indexerExpr)
{
	VisitChild(indexerExpr->mTarget);
	ResolveGenericType();
	auto target = GetResult(true);
	if (!target)
		return;
	
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
		for (BfExpression* expr : indexerExpr->mArguments)
		{
			if (expr == NULL)
				return;
			auto argVal = mModule->CreateValueFromExpression(expr);
			if (!argVal)
			{
				mModule->AssertErrorState();
				argVal = mModule->GetDefaultTypedValue(mModule->mContext->mBfObjectType);
			}
			BfResolvedArg resolvedArg;
			resolvedArg.mExpression = expr;
			resolvedArg.mTypedValue = argVal;
			mIndexerValues.push_back(resolvedArg);
		}

		BfMethodMatcher methodMatcher(indexerExpr->mTarget, mModule, "[]", mIndexerValues, NULL);
		methodMatcher.mCheckedKind = checkedKind;
		//methodMatcher.CheckType(target.mType->ToTypeInstance(), target, false);
		
		BfMethodDef* methodDef = NULL;
		
		auto startCheckTypeInst = target.mType->ToTypeInstance();

		for (int pass = 0; pass < 2; pass++)
		{
			bool isFailurePass = pass == 1;

			auto curCheckType = startCheckTypeInst;
			while (curCheckType != NULL)
			{
				BfProtectionCheckFlags protectionCheckFlags = BfProtectionCheckFlag_None;

				BfPropertyDef* foundProp = NULL;
				BfTypeInstance* foundPropTypeInst = NULL;

				int matchedIndexCount = 0;

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

						if (checkMethod->mExplicitInterface != NULL)
							continue;
						if (checkMethod->mIsOverride)
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
						methodMatcher.CheckMethod(startCheckTypeInst, curCheckType, checkMethod, false);						

						if ((methodMatcher.mBestMethodDef == checkMethod) ||
							((foundProp == NULL) && (methodMatcher.mBackupMethodDef == checkMethod)))
						{
							foundPropTypeInst = curCheckType;
							foundProp = prop;
							matchedIndexCount = (int)checkMethod->mParams.size();
						}
					}
				}

				if (foundProp != NULL)
				{
					int indexDiff = matchedIndexCount - (int)mIndexerValues.size();
					if (indexDiff > 0)
					{
						mModule->Fail(StrFormat("Expected %d more indices", indexDiff), indexerExpr->mTarget);
						//mModule->mCompiler->mPassInstance->MoreInfo("See method declaration", methodInstance->mMethodDef->mMethodDeclaration);
					}
					else if (indexDiff < 0)
					{
						mModule->Fail(StrFormat("Expected %d fewer indices", indexDiff), indexerExpr->mTarget);
					}
					else
					{
						mPropSrc = indexerExpr->mOpenBracket;
						mPropDef = foundProp;
						if (foundProp->mIsStatic)
						{
							mPropTarget = BfTypedValue(curCheckType);
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
					}
					return;
				}

				curCheckType = curCheckType->mBaseType;
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

	auto indexArgument = mModule->CreateValueFromExpression(indexerExpr->mArguments[0]);
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
			mModule->Fail("Expected integer index", indexerExpr->mArguments[0]);
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
				mModule->Fail(StrFormat("Index '%d' is out of bounds for type '%s'", indexConst->mInt32, mModule->TypeToString(target.mType).c_str()), indexerExpr->mArguments[0]);
				mResult = _GetDefaultResult();
				return;
			}
		}
		else if ((mModule->HasCompiledOutput()) && (wantsChecks))
		{
			if (checkedKind == BfCheckedKind_NotSet)
				checkedKind = mModule->GetDefaultCheckedKind();
			if (checkedKind == BfCheckedKind_Checked)
			{
				auto oobBlock = mModule->mBfIRBuilder->CreateBlock("oob", true);
				auto contBlock = mModule->mBfIRBuilder->CreateBlock("cont", true);

				auto indexType = (BfPrimitiveType*)indexArgument.mType;

				if (!mModule->mSystem->DoesLiteralFit(indexType->mTypeDef->mTypeCode, sizedArrayType->mElementCount))
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
					/*if (!mModule->mCompiler->mIsResolveOnly)
					{
						OutputDebugStrF("-OOB %d %d\n", oobFunc.mFunc.mId, oobFunc.mFunc.mFlags);
					}*/

					SizedArray<BfIRValue, 1> args;
					args.push_back(mModule->GetConstValue(0));
					mModule->mBfIRBuilder->CreateCall(oobFunc.mFunc, args);
					mModule->mBfIRBuilder->CreateUnreachable();

					mModule->mBfIRBuilder->SetInsertPoint(contBlock);
				}
				else
				{
					mModule->Fail("System.Internal class must contain method 'ThrowIndexOutOfRange'");
				}
			}
		}
		
		// If this is a 'bag of bytes', we should try hard not to have to make this addressable
		if ((!target.IsAddr()) && (!target.mType->IsSizeAligned()))
			mModule->MakeAddressable(target);

		mModule->PopulateType(underlyingType);
		if (sizedArrayType->IsUnknownSizedArray())
		{			
			mResult = mModule->GetDefaultTypedValue(underlyingType);
		}
		else if (sizedArrayType->IsValuelessType())				
		{
			if (underlyingType->IsValuelessType())
            	mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), underlyingType, true);
			else
			{
				mResult = mModule->GetDefaultTypedValue(underlyingType);
				if (sizedArrayType->mElementCount != 0)
					mModule->AssertErrorState();
			}
		}
		else if (target.IsAddr())
		{			
			if (target.mType->IsSizeAligned())
			{
				auto ptrType = mModule->CreatePointerType(underlyingType);
				auto ptrValue = mModule->mBfIRBuilder->CreateBitCast(target.mValue, mModule->mBfIRBuilder->MapType(ptrType));
				auto gepResult = mModule->mBfIRBuilder->CreateInBoundsGEP(ptrValue, indexArgument.mValue);
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
	PerformUnaryOperation(unaryOpExpr->mExpression, unaryOpExpr->mOp, unaryOpExpr->mOpToken);
}

void BfExprEvaluator::PerformUnaryOperation(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken)
{
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
		
	
	BfExprEvaluator::PerformUnaryOperation_OnResult(unaryOpExpr, unaryOp, opToken);
}

BfTypedValue BfExprEvaluator::PerformUnaryOperation_TryOperator(const BfTypedValue& inValue, BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken)
{
	if ((!inValue.mType->IsTypeInstance()) && (!inValue.mType->IsGenericParam()))
		return BfTypedValue();
	
	SizedArray<BfResolvedArg, 1> args;
	BfResolvedArg resolvedArg;
	resolvedArg.mTypedValue = inValue;
	args.push_back(resolvedArg);
	BfMethodMatcher methodMatcher(opToken, mModule, "", args, NULL);
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
				if (methodMatcher.CheckMethod(NULL, checkType, operatorDef, false))
					methodMatcher.mSelfType = entry.mSrcType;
			}
		}
	}

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
						if (mModule->CanCast(args[0].mTypedValue, opConstraint.mRightType))
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
			auto genericTypeInst = (BfGenericTypeInstance*)mModule->mCurTypeInstance;
			for (int genericParamIdx = 0; genericParamIdx < genericTypeInst->mGenericParams.size(); genericParamIdx++)
			{
				auto genericParam = mModule->GetGenericTypeParamInstance(genericParamIdx);
				for (auto& opConstraint : genericParam->mOperatorConstraints)
				{
					if (opConstraint.mUnaryOp == findOp)
					{
						if (mModule->CanCast(args[0].mTypedValue, opConstraint.mRightType))
						{
							return BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), genericParam->mExternType);							
						}
					}
				}
			}
		}

		return BfTypedValue();
	}
	
	if (!baseClassWalker.mMayBeFromInterface)
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
	auto result = CreateCall(&methodMatcher, BfTypedValue());

	if ((result.mType != NULL) && (methodMatcher.mSelfType != NULL) && (result.mType->IsSelf()))
	{
		BF_ASSERT(mModule->IsInGeneric());
		result = mModule->GetDefaultTypedValue(methodMatcher.mSelfType);
	}

	if (isPostOp)
		result = args[0].mTypedValue;
	return result;
}

void BfExprEvaluator::PerformUnaryOperation_OnResult(BfExpression* unaryOpExpr, BfUnaryOp unaryOp, BfTokenNode* opToken)
{
	BfAstNode* propSrc = mPropSrc;
	BfTypedValue propTarget = mPropTarget;
	BfPropertyDef* propDef = mPropDef;
	SizedArray<BfResolvedArg, 2> indexerVals = mIndexerValues;
	BfTypedValue writeToProp;

	GetResult();
	if (!mResult)
		return;	

	if (mResult.mType->IsRef())
		mResult.mType = mResult.mType->GetUnderlyingType();

	if (mResult.mType->IsVar())
	{
		mResult = BfTypedValue(mModule->GetDefaultValue(mResult.mType), mResult.mType);
		return;
	}

	if (BfCanOverloadOperator(unaryOp))
	{
		auto opResult = PerformUnaryOperation_TryOperator(mResult, unaryOpExpr, unaryOp, opToken);
		if (opResult)
		{
			mResult = opResult;
			return;
		}
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
				return;
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
				return;

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
						return;
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
			if ((!CheckModifyResult(mResult, unaryOpExpr, "take address of")) || (mResult.mType->IsValuelessType()))
			{
				// Sentinel value
				auto val = mModule->mBfIRBuilder->CreateIntToPtr(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1), mModule->mBfIRBuilder->MapType(ptrType));
				mResult = BfTypedValue(val, ptrType);
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

			auto derefTarget = mModule->LoadValue(mResult);

			BfPointerType* pointerType = (BfPointerType*)derefTarget.mType;				
			auto resolvedType = mModule->ResolveType(pointerType->mElementType);
			if (resolvedType == NULL)
			{
				mResult = BfTypedValue();
				return;
			}

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
				mModule->mBfIRBuilder->CreateStore(resultValue, ptr.mValue, mIsVolatileReference);
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
				mModule->mBfIRBuilder->CreateStore(resultValue, ptr.mValue, mIsVolatileReference);
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
			
			if ((unaryOp == BfUnaryOp_Mut) && (!mResult.mType->IsComposite()) && (!mResult.mType->IsGenericParam()))
			{
				// Non-composite types are already mutable, leave them alone...
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
				mModule->Fail(StrFormat("Invalid usage of '%s' expression", BfGetOpName(unaryOp)), opToken);
				return;
			}

			ResolveGenericType();
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
			mResult = BfTypedValue(mResult.mValue, mModule->CreateRefType(mResult.mType, BfRefType::RefKind_Out));
		}
		break;
	case BfUnaryOp_Params:				
		{	
			bool allowParams = (mBfEvalExprFlags & BfEvalExprFlags_AllowParamsExpr) != 0;
			if (mResultLocalVar == NULL)
				allowParams = false;
 			if (allowParams)
			{												
				if (mResultLocalVar->mCompositeCount >= 0) // Delegate params
				{
					allowParams = true;
				}
				else
				{
					auto isValid = false;
					auto genericTypeInst = mResult.mType->ToGenericTypeInstance();
					if ((genericTypeInst != NULL) && (genericTypeInst->mTypeDef == mModule->mCompiler->mSpanTypeDef))
						isValid = true;
					else if (mResult.mType->IsArray())
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
		
	default:
		mModule->Fail("INTERNAL ERROR: Unhandled unary operator", unaryOpExpr);
		break;
	}		

	if (numericFail)
	{
		if (mResult.mType->IsInterface())
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
		auto setMethod = GetPropertyMethodDef(propDef, BfMethodType_PropertySetter, mPropCheckedKind);
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
			PushThis(propSrc, propTarget, methodInstance.mMethodInstance, args);
			//args.push_back(propTarget.mValue);

		for (int paramIdx = 0; paramIdx < (int)indexerVals.size(); paramIdx++)
		{
			auto val = mModule->Cast(propSrc, indexerVals[paramIdx].mTypedValue, methodInstance.mMethodInstance->GetParamType(paramIdx));
			if (!val)
				return;			
			PushArg(val, args);
		}

		PushArg(writeToProp, args);
		CreateCall(methodInstance.mMethodInstance, methodInstance.mFunc, false, args);
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
			mModule->CreateValueFromExpression(rightExpression, mExpectingType);
		return;
	}
	if (leftValue.mType->IsRef())
		leftValue.mType = leftValue.mType->GetUnderlyingType();

	if ((binaryOp == BfBinaryOp_ConditionalAnd) || (binaryOp == BfBinaryOp_ConditionalOr))
	{
 		if (mModule->mCurMethodState->mDeferredLocalAssignData != NULL)
 			mModule->mCurMethodState->mDeferredLocalAssignData->BreakExtendChain();

		bool isAnd = binaryOp == BfBinaryOp_ConditionalAnd;

		auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);
		leftValue = mModule->Cast(leftExpression, leftValue, boolType);
		if (!leftValue)
		{
			mModule->CreateValueFromExpression(rightExpression);
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
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);
					mResult = rightValue;
				}
				else
				{
					// Always false
					SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);					
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), boolType);
				}
			}
			else
			{				
				auto rhsBB = mModule->mBfIRBuilder->CreateBlock("land.rhs");
				auto endBB = mModule->mBfIRBuilder->CreateBlock("land.end");

				// This makes any 'scope' allocs be dyn since we aren't sure if this will be short-circuited
				SetAndRestoreValue<bool> prevIsConditional(mModule->mCurMethodState->mCurScope->mIsConditional, true);

				mModule->mBfIRBuilder->CreateCondBr(leftValue.mValue, rhsBB, endBB);

				mModule->AddBasicBlock(rhsBB);
				rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);				
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
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), boolType);
				}
				else
				{
					// Only right side
					rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);
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
				rightValue = mModule->CreateValueFromExpression(rightExpression, boolType);				
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
	if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
		wantType = NULL; // Don't presume
	rightValue = mModule->CreateValueFromExpression(rightExpression, wantType, BfEvalExprFlags_NoCast);
	if ((!leftValue) || (!rightValue))
		return;

	PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue, rightValue);
}

void BfExprEvaluator::PerformBinaryOperation(BfExpression* leftExpression, BfExpression* rightExpression, BfBinaryOp binaryOp, BfTokenNode* opToken, BfBinOpFlags flags)
{	
	BfTypedValue leftValue;
	if (leftExpression != NULL)
	{
		leftValue = mModule->CreateValueFromExpression(leftExpression, mExpectingType, (BfEvalExprFlags)(BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowIntUnknown));
	}
	return PerformBinaryOperation(leftExpression, rightExpression, binaryOp, opToken, flags, leftValue);	
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
			if (rightConst->mInt64 < minValue)
				constResult = 0;			
			break;
		case BfBinaryOp_InEquality:
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
			if (rightConst->mInt64 < minValue)
				constResult = 0;
			else if (rightConst->mInt64 > maxValue)
				constResult = 0;
			break;
		case BfBinaryOp_InEquality:
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
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), mModule->GetPrimitiveType(BfTypeCode_Boolean));
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

	if ((binaryOp == BfBinaryOp_NullCoalesce) && ((leftValue.mType->IsPointer()) || (leftValue.mType->IsObject())))
	{
		auto prevBB = mModule->mBfIRBuilder->GetInsertBlock();

		auto rhsBB = mModule->mBfIRBuilder->CreateBlock("nullc.rhs");
		auto endBB = mModule->mBfIRBuilder->CreateBlock("nullc.end");

		auto isNull = mModule->mBfIRBuilder->CreateIsNull(leftValue.mValue);
		mModule->mBfIRBuilder->CreateCondBr(isNull, rhsBB, endBB);

		mModule->AddBasicBlock(rhsBB);		
		if (!rightValue)			
		{
			mModule->AssertErrorState();
			return;
		}
		else
		{
			auto rightToLeftValue = mModule->CastToValue(rightExpression, rightValue, leftValue.mType, BfCastFlags_SilentFail);
			if (rightToLeftValue)
			{
				rightValue = BfTypedValue(rightToLeftValue, leftValue.mType);
			}
			else
			{
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
			}
		}

		mModule->mBfIRBuilder->CreateBr(endBB);

		auto endRhsBB = mModule->mBfIRBuilder->GetInsertBlock();
		mModule->AddBasicBlock(endBB);		
		auto phi = mModule->mBfIRBuilder->CreatePhi(mModule->mBfIRBuilder->MapType(leftValue.mType), 2);
		mModule->mBfIRBuilder->AddPhiIncoming(phi, leftValue.mValue, prevBB);
		mModule->mBfIRBuilder->AddPhiIncoming(phi, rightValue.mValue, endRhsBB);
		mResult = BfTypedValue(phi, leftValue.mType);
		
		return;
	}	
	
	if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
	{
		forceLeftType = true;		
	}

	if (rightValue.mType->IsRef())
		rightValue.mType = rightValue.mType->GetUnderlyingType();

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
		// If one of these is a constant that can be converted into a smaller type, then do that
		if (rightValue.mValue.IsConst())
		{
			if (mModule->CanCast(rightValue, leftValue.mType))			
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
	if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
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

	if ((resultType->IsVar()) || (otherType->IsVar()))
	{
		mResult = mModule->GetDefaultTypedValue(resultType);
		return;
	}
	
	if ((otherType->IsNull()) && ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality)))
	{
		bool isEquality = (binaryOp == BfBinaryOp_Equality);

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
					BfIRValue hasValueValue = mModule->mBfIRBuilder->CreateLoad(hasValuePtr);
					if (isEquality)
						hasValueValue = mModule->mBfIRBuilder->CreateNot(hasValueValue);
					mResult = BfTypedValue(hasValueValue, boolType);
				}
				else
				{
					mModule->mBfIRBuilder->PopulateType(resultType);
					BfTypedValue nullableTypedVale = mModule->MakeAddressable(*resultTypedValue);
					BfIRValue hasValuePtr = mModule->mBfIRBuilder->CreateInBoundsGEP(nullableTypedVale.mValue, 0, 2);
					BfIRValue hasValueValue = mModule->mBfIRBuilder->CreateLoad(hasValuePtr);
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
	if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
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
					if (binaryOp == BfBinaryOp_InEquality)
						isEqual = !isEqual;
					mResult = BfTypedValue(mModule->GetConstValue(isEqual ? 1 : 0, boolType), boolType);
					return;
				}
			}

			int eqResult = mModule->mBfIRBuilder->CheckConstEquality(leftValue.mValue, rightValue.mValue);
			if (eqResult != -1)
			{				
				bool isEqual = eqResult == 1;
				if (binaryOp == BfBinaryOp_InEquality)
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
		if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
		{
			auto leftConstant = mModule->mBfIRBuilder->GetConstant(leftValue.mValue);
			auto rightConstant = mModule->mBfIRBuilder->GetConstant(rightValue.mValue);
			
			if ((leftConstant != NULL) && (leftConstant->IsNull()))
				skipOpOverload = true;
			if ((rightConstant != NULL) && (rightConstant->IsNull()))
				skipOpOverload = true;
		}

		if (!skipOpOverload)
		{
			BfBinaryOp findBinaryOp = binaryOp;

			bool isComparison = (binaryOp >= BfBinaryOp_Equality) && (binaryOp <= BfBinaryOp_LessThanOrEqual);

			for (int pass = 0; pass < 2; pass++)
			{
				bool foundOp = false;				

				SizedArray<BfResolvedArg, 2> args;
				BfResolvedArg leftArg;
				leftArg.mExpression = leftExpression;
				leftArg.mTypedValue = leftValue;				
				BfResolvedArg rightArg;
				rightArg.mExpression = rightExpression;
				rightArg.mTypedValue = rightValue;

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
				BfMethodMatcher methodMatcher(opToken, mModule, "", args, NULL);
				BfBaseClassWalker baseClassWalker(leftValue.mType, rightValue.mType, mModule);

				bool invertResult = false;				
				
				BfBinaryOp oppositeBinaryOp = BfGetOppositeBinaryOp(findBinaryOp);				

				while (true)
				{
					auto entry = baseClassWalker.Next();
					auto checkType = entry.mTypeInstance;
					if (checkType == NULL)
						break;

					bool foundExactMatch = false;					
					Array<BfMethodDef*> oppositeOperatorDefs;

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

							if (methodMatcher.CheckMethod(NULL, checkType, operatorDef, false))
							{
								methodMatcher.mSelfType = entry.mSrcType;
								if (operatorDef->mOperatorDeclaration->mBinOp == findBinaryOp)
									foundExactMatch = true;
							}
						}
						else if (operatorDef->mOperatorDeclaration->mBinOp == oppositeBinaryOp)
							oppositeOperatorDefs.Add(operatorDef);							
					}

					if (((methodMatcher.mBestMethodDef == NULL) || (!foundExactMatch)) && (!oppositeOperatorDefs.IsEmpty()))
					{
						foundOp = true;
						for (auto oppositeOperatorDef : oppositeOperatorDefs)
						{
							if (methodMatcher.CheckMethod(NULL, checkType, oppositeOperatorDef, false))
								methodMatcher.mSelfType = entry.mSrcType;
						}
					}
				}
				if (methodMatcher.mBestMethodDef != NULL)
				{
					methodMatcher.FlushAmbiguityError();

					auto matchedOp = ((BfOperatorDeclaration*)methodMatcher.mBestMethodDef->mMethodDeclaration)->mBinOp;
					bool invertResult = matchedOp == oppositeBinaryOp;

					auto methodDef = methodMatcher.mBestMethodDef;
					auto autoComplete = GetAutoComplete();
					if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(opToken)))
					{
						auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDef->mMethodDeclaration);
						if ((operatorDecl != NULL) && (operatorDecl->mOpTypeToken != NULL))
							autoComplete->SetDefinitionLocation(operatorDecl->mOpTypeToken);
					}

					if (opToken != NULL)
					{
						if ((opToken->IsA<BfTokenNode>()) && (!noClassify) && (!baseClassWalker.mMayBeFromInterface))
							mModule->SetElementType(opToken, BfSourceElementType_Method);
					}
					mResult = CreateCall(&methodMatcher, BfTypedValue());
					if ((mResult.mType != NULL) && (methodMatcher.mSelfType != NULL) && (mResult.mType->IsSelf()))
					{
						BF_ASSERT(mModule->IsInGeneric());
						mResult = mModule->GetDefaultTypedValue(methodMatcher.mSelfType);
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
							mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(mResult.mValue, zeroVal), boolType);
							break;
						case BfBinaryOp_InEquality:
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

				// Check method generic constraints
				if ((mModule->mCurMethodInstance != NULL) && (mModule->mCurMethodInstance->mIsUnspecialized) && (mModule->mCurMethodInstance->mMethodInfoEx != NULL))
				{
					for (int genericParamIdx = 0; genericParamIdx < mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->mCurMethodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
						for (auto& opConstraint : genericParam->mOperatorConstraints)
						{
							if (opConstraint.mBinaryOp == findBinaryOp)
							{
								if ((mModule->CanCast(args[0].mTypedValue, opConstraint.mLeftType)) &&
									(mModule->CanCast(args[1].mTypedValue, opConstraint.mRightType)))
								{									
									BF_ASSERT(genericParam->mExternType != NULL);
									mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), genericParam->mExternType);									
									return;
								}
							}
						}
					}
				}
				
				// Check type generic constraints
				if ((mModule->mCurTypeInstance->IsGenericTypeInstance()) && (mModule->mCurTypeInstance->IsUnspecializedType()))
				{
					auto genericTypeInst = (BfGenericTypeInstance*)mModule->mCurTypeInstance;
					for (int genericParamIdx = 0; genericParamIdx < genericTypeInst->mGenericParams.size(); genericParamIdx++)
					{
						auto genericParam = mModule->GetGenericTypeParamInstance(genericParamIdx);
						for (auto& opConstraint : genericParam->mOperatorConstraints)
						{
							if (opConstraint.mBinaryOp == findBinaryOp)
							{
								if ((mModule->CanCast(args[0].mTypedValue, opConstraint.mLeftType)) &&
									(mModule->CanCast(args[1].mTypedValue, opConstraint.mRightType)))
								{
									mResult = BfTypedValue(mModule->mBfIRBuilder->GetFakeVal(), genericParam->mExternType);
									return;
								}
							}
						}
					}
				}

				if ((!foundOp) || (pass == 1))
					break;
								
				switch (findBinaryOp)
				{
				case BfBinaryOp_Equality:
				case BfBinaryOp_InEquality:			
				case BfBinaryOp_Compare:
					// Still works
					break;
				case BfBinaryOp_LessThan:
					findBinaryOp = BfBinaryOp_GreaterThanOrEqual;
					break;
				case BfBinaryOp_LessThanOrEqual:
					findBinaryOp = BfBinaryOp_GreaterThan;
					break;
				case BfBinaryOp_GreaterThan:
					findBinaryOp = BfBinaryOp_LessThanOrEqual;
					break;
				case BfBinaryOp_GreaterThanOrEqual:
					findBinaryOp = BfBinaryOp_LessThan;
					break;
				default:
					findBinaryOp = BfBinaryOp_None;
					break;
				}

				if (findBinaryOp == BfBinaryOp_None)
					break;
			}			
		}
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
		if (binaryOp == BfBinaryOp_Subtract)
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
			convLeftValue = mModule->CastToValue(leftExpression, leftValue, intPtrType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler));
			convRightValue = mModule->CastToValue(rightExpression, rightValue, intPtrType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_FromCompiler));
			BfIRValue diffValue = mModule->mBfIRBuilder->CreateSub(convLeftValue, convRightValue);
			diffValue = mModule->mBfIRBuilder->CreateDiv(diffValue, mModule->GetConstValue(resultPointerType->mElementType->mSize, intPtrType), true);
			mResult = BfTypedValue(diffValue, intPtrType);
			return;
		}
		else if ((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_InEquality) &&
			(binaryOp != BfBinaryOp_LessThan) && (binaryOp != BfBinaryOp_LessThanOrEqual) &&
			(binaryOp != BfBinaryOp_GreaterThan) && (binaryOp != BfBinaryOp_GreaterThanOrEqual))
		{
			mModule->Fail("Invalid operation on pointers", opToken);
			return;
		}				
		
		if (((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_InEquality)) ||
			(resultType != otherType))
		{
			resultType = mModule->GetPrimitiveType(BfTypeCode_UIntPtr);
			explicitCast = true;
		}			
	}
	else if (resultType->IsPointer())
	{
		if (otherType->IsNull())
		{
			if ((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_InEquality))
			{
				mModule->Fail(StrFormat("Invalid operation between '%s' and null", mModule->TypeToString(resultType).c_str()), opToken);
				return;
			}

			if (binaryOp == BfBinaryOp_Equality)
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			else
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNotNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			return;
		}

		// One pointer
		if ((!otherType->IsIntegral()) ||
			((binaryOp != BfBinaryOp_Add) && (binaryOp != BfBinaryOp_Subtract)))
		{
			_OpFail();
			return;
		}
		
		auto underlyingType = resultType->GetUnderlyingType();
		BfIRValue addValue = otherTypedValue->mValue;		
		if ((!otherTypedValue->mType->IsSigned()) && (otherTypedValue->mType->mSize < mModule->mSystem->mPtrSize))		
			addValue = mModule->mBfIRBuilder->CreateNumericCast(addValue, false, BfTypeCode_UIntPtr);
		if (binaryOp == BfBinaryOp_Subtract)
		{
			if (resultTypeSrc == rightExpression)			
				mModule->Fail("Cannot subtract a pointer from an integer", resultTypeSrc);

			addValue = mModule->mBfIRBuilder->CreateNeg(addValue);
		}

		mModule->PopulateType(underlyingType);
		if (underlyingType->IsValuelessType())
		{
			if (!mModule->IsInSpecializedSection())
			{
				mModule->Warn(0, "Adding to a pointer to a zero-sized element has no effect", opToken);
			}
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
		
		if ((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_InEquality))
		{
			//mModule->Fail("Invalid operation for objects", opToken);
			_OpFail();
			return;
		}		

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
				if (binaryOp == BfBinaryOp_Equality)
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
				else
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(mModule->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			}
			else
			{
				if (binaryOp == BfBinaryOp_Equality)
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
				else
					mResult = BfTypedValue(mModule->mBfIRBuilder->CreateIsNotNull(resultTypedValue->mValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			}
		}
		else
		{
			auto convertedValue = mModule->CastToValue(otherTypeSrc, *otherTypedValue, resultType);
			if (!convertedValue)
				return;
			if (binaryOp == BfBinaryOp_Equality)
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(resultTypedValue->mValue, convertedValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
			else
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(resultTypedValue->mValue, convertedValue), mModule->GetPrimitiveType(BfTypeCode_Boolean));
		}

		return;
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
				if ((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract))
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
		
		BfIRValue convResultValue = mModule->CastToValue(resultTypeSrc, *resultTypedValue, underlyingType, BfCastFlags_Explicit);
		BfIRValue convOtherValue = mModule->CastToValue(otherTypeSrc, *otherTypedValue, underlyingType, BfCastFlags_Explicit);

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
			mResult = CreateCall(opToken, BfTypedValue(), BfTypedValue(), moduleMethodInstance.mMethodInstance->mMethodDef, moduleMethodInstance, false, argValues);
			if ((mResult) && (binaryOp == BfBinaryOp_InEquality))
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
			auto leftTupleType = (BfTupleType*)leftValue.mType;
			auto rightTupleType = (BfTupleType*)rightValue.mType;

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
			if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
			{
				auto intCoercibleType = mModule->GetIntCoercibleType(leftValue.mType);
				if (intCoercibleType != NULL)
				{
					auto intLHS = mModule->GetIntCoercible(leftValue);
					auto intRHS = mModule->GetIntCoercible(rightValue);
					auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

					if (binaryOp == BfBinaryOp_Equality)
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(intLHS.mValue, intRHS.mValue), boolType);
					else
						mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(intLHS.mValue, intRHS.mValue), boolType);
					return;
				}

				if (_CallValueTypeEquals())
					return;
			}

			mModule->Fail(StrFormat("Operator '%s' cannot be applied to operands of type '%s'",
				BfGetOpName(binaryOp),
				mModule->TypeToString(leftValue.mType).c_str()), opToken);
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
		if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
		{
			auto boolType = mModule->GetPrimitiveType(BfTypeCode_Boolean);

			BfMethodRefType* lhsMethodRefType = (BfMethodRefType*)leftValue.mType;
			BfMethodRefType* rhsMethodRefType = (BfMethodRefType*)rightValue.mType;
			if (lhsMethodRefType->mMethodRef != rhsMethodRefType->mMethodRef)
			{
				mResult = BfTypedValue(mModule->GetConstValue((binaryOp == BfBinaryOp_Equality) ? 0 : 1, boolType), boolType);
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
				(binaryOp == BfBinaryOp_BitwiseAnd) |
				(binaryOp == BfBinaryOp_BitwiseOr) |
				(binaryOp == BfBinaryOp_ExclusiveOr) |
				(binaryOp == BfBinaryOp_LeftShift) |
				(binaryOp == BfBinaryOp_RightShift) |
				(binaryOp == BfBinaryOp_Equality) |
				(binaryOp == BfBinaryOp_InEquality);

			if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
			{				
				// For shifts we have more lenient rules - shifts are naturally limited so any int type is equally valid
				if (rightValue.mType->IsIntegral())
					explicitCast = true;
			}			
			else if (((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract)) && (resultType->IsChar()) && (otherType->IsInteger()))
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
				if ((binaryOp == BfBinaryOp_Subtract) && (resultType->IsChar()) && (otherType->IsChar()))
				{
					// "wchar - char" subtraction will always fit into int32, because of unicode range
					resultType = mModule->GetPrimitiveType(BfTypeCode_Int32);
					explicitCast = true;
				}
				else if ((otherType->IsChar()) &&
					((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract)))
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
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1),
				mModule->GetPrimitiveType(BfTypeCode_Boolean));
			break;
		case BfBinaryOp_InEquality:
			mResult = BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0),
				mModule->GetPrimitiveType(BfTypeCode_Boolean));
			break;
		default:
			mModule->Fail("Invalid operation for void", opToken);
			break;
		}
		return;		
	}

	if ((!convLeftValue) || (!convRightValue))
		return;
	
	if (resultType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)resultType;
		if (primType->mTypeDef->mTypeCode == BfTypeCode_Boolean)
		{
			switch (binaryOp)
			{
			case BfBinaryOp_Equality:
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(convLeftValue, convRightValue),
					mModule->GetPrimitiveType(BfTypeCode_Boolean));
				break;
			case BfBinaryOp_InEquality:
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
			default:
				mModule->Fail("Invalid operation for booleans", opToken);
				break;
			}
			return;
		}
	}

	if ((!resultType->IsIntegral()) && (!resultType->IsFloat()))
	{
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
		 (binaryOp == BfBinaryOp_Divide) ||
		 (binaryOp == BfBinaryOp_Modulus)))
	{
		mModule->Fail(StrFormat("Cannot perform operation on type '%s'", mModule->TypeToString(resultType).c_str()), opToken);
		return;
	}

	switch (binaryOp)
	{
	case BfBinaryOp_Add:		
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateAdd(convLeftValue, convRightValue), resultType);		
		mModule->CheckRangeError(resultType, opToken);
		break;
	case BfBinaryOp_Subtract:		
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateSub(convLeftValue, convRightValue), resultType);
		mModule->CheckRangeError(resultType, opToken);
		break;
	case BfBinaryOp_Multiply:		
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateMul(convLeftValue, convRightValue), resultType);
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
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpEQ(convLeftValue, convRightValue),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_InEquality:	
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpNE(convLeftValue, convRightValue),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_LessThan:			
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSignedInt()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_LessThanOrEqual:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpLTE(convLeftValue, convRightValue, resultType->IsSignedInt()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_GreaterThan:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSignedInt()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_GreaterThanOrEqual:
		mResult = BfTypedValue(mModule->mBfIRBuilder->CreateCmpGTE(convLeftValue, convRightValue, resultType->IsSignedInt()),
			mModule->GetPrimitiveType(BfTypeCode_Boolean));	
		break;
	case BfBinaryOp_Compare:				
		{
			auto intType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);
			if ((convLeftValue.IsConst()) && (convRightValue.IsConst()))
			{
				auto cmpLtVal = mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSignedInt());
				auto ltConstant = mModule->mBfIRBuilder->GetConstant(cmpLtVal);
				if (ltConstant->mBool)
				{
					mResult = BfTypedValue(mModule->GetConstValue(-1, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
				}
				else
				{
					auto cmpGtVal = mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSignedInt());
					auto rtConstant = mModule->mBfIRBuilder->GetConstant(cmpGtVal);
					if (rtConstant->mBool)
						mResult = BfTypedValue(mModule->GetConstValue(1, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
					else
						mResult = BfTypedValue(mModule->GetConstValue(0, mModule->GetPrimitiveType(BfTypeCode_IntPtr)), intType);
				}
			}
			else if ((resultType->IsIntegral()) && (resultType->mSize < intType->mSize))
			{
				auto leftIntValue = mModule->mBfIRBuilder->CreateNumericCast(convLeftValue, resultType->IsSignedInt(), BfTypeCode_IntPtr);
				auto rightIntValue = mModule->mBfIRBuilder->CreateNumericCast(convRightValue, resultType->IsSignedInt(), BfTypeCode_IntPtr);							
				mResult = BfTypedValue(mModule->mBfIRBuilder->CreateSub(leftIntValue, rightIntValue), intType);
			}
			else
			{
				BfIRBlock checkGtBlock = mModule->mBfIRBuilder->CreateBlock("cmpCheckGt");
				BfIRBlock eqBlock = mModule->mBfIRBuilder->CreateBlock("cmpEq");
				BfIRBlock endBlock = mModule->mBfIRBuilder->CreateBlock("cmpEnd");

				auto startBlock = mModule->mBfIRBuilder->GetInsertBlock();

				auto cmpLtVal = mModule->mBfIRBuilder->CreateCmpLT(convLeftValue, convRightValue, resultType->IsSignedInt());
				mModule->mBfIRBuilder->CreateCondBr(cmpLtVal, endBlock, checkGtBlock);

				mModule->mBfIRBuilder->AddBlock(checkGtBlock);
				mModule->mBfIRBuilder->SetInsertPoint(checkGtBlock);
				auto cmpGtVal = mModule->mBfIRBuilder->CreateCmpGT(convLeftValue, convRightValue, resultType->IsSignedInt());
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

					PerformUnaryOperation(binOpExpr->mRight, unaryOp, binOpExpr->mOpToken);
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
