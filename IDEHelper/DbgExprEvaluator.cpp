#include "DbgExprEvaluator.h"
#include "WinDebugger.h"
#include "Compiler/BfUtil.h"
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"
#include "Compiler/BfPrinter.h"
#include "Compiler/BfDemangler.h"
#include "DWARFInfo.h"
#include "DebugManager.h"
#include "DebugVisualizers.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/StackHelper.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;
using namespace llvm;

//////////////////////////////////////////////////////////////////////////

DwMethodMatcher::DwMethodMatcher(BfAstNode* targetSrc, DbgExprEvaluator* exprEvaluator, const StringImpl& methodName, SizedArrayImpl<DbgTypedValue>& arguments, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments) :
	mArguments(arguments)
{
	mTargetSrc = targetSrc;
	mExprEvaluator = exprEvaluator;
	mMethodName = methodName;
	mArguments = arguments;
	mBestMethodDef = NULL;
	mBackupMethodDef = NULL;
	mBestMethodTypeInstance = NULL;
	mExplicitInterfaceCheck = NULL;
	mHadExplicitGenericArguments = false;
	mTargetIsConst = false;

	if (methodGenericArguments != NULL)
	{
		for (auto genericArg : *methodGenericArguments)
		{
			if (genericArg == NULL)
				return;
			auto genericArgType = mExprEvaluator->ResolveTypeRef(genericArg);
			if (genericArgType == NULL)
				return;
			mBestMethodGenericArguments.push_back(genericArgType);
			//mBestMethodGenericArgumentSrcs.push_back(genericArg);
		}
		mHadExplicitGenericArguments = true;
	}
}

/*bool DwMethodMatcher::InferGenericArgument(DbgType* argType, DbgType* wantType)
{
	if (argType == NULL)
		return false;

	if (wantType->IsGenericParam())
	{
		auto wantGenericParam = (BfGenericParamType*)wantType;
		if (wantGenericParam->mGenericParamKind == BfGenericParamKind_Method)
		{
			auto prevGenericMethodArg = mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx];
			if (prevGenericMethodArg == NULL)
			{
				mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = argType;
				return true;
			}

			// Prev is already best
			if (mModule->CanCast(DbgTypedValue(NULL, argType), prevGenericMethodArg))
				return true;

			// New best?
			if (mModule->CanCast(DbgTypedValue(NULL, prevGenericMethodArg), argType))
			{
				mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = argType;
				return true;
			}

			// No implicit conversion, FAIL!
			mCheckMethodGenericArguments[wantGenericParam->mGenericParamIdx] = NULL;
			return false;
		}
		return true;
	}

	if (wantType->IsGenericTypeInstance())
	{
		if (!argType->IsGenericTypeInstance())
			return true;
		auto wantGenericType = (BfTypeInstance*)wantType;
		auto argGenericType = (BfTypeInstance*)argType;
		if (argGenericType->mTypeDef != wantGenericType->mTypeDef)
			return true;

		for (int genericArgIdx = 0; genericArgIdx < (int)argGenericType->mTypeGenericArguments.size(); genericArgIdx++)
			InferGenericArgument(argGenericType->mTypeGenericArguments[genericArgIdx], wantGenericType->mTypeGenericArguments[genericArgIdx]);
		return true;
	}

	if (wantType->IsRef())
	{
		if (!argType->IsRef())
			return true;
		auto wantRefType = (BfRefType*)wantType;
		auto argRefType = (BfRefType*)argType;
		if (wantRefType->mIsOut != argRefType->mIsOut)
			return true;
		InferGenericArgument(argRefType->mElementType, wantRefType->mElementType);
		return true;
	}

	if (wantType->IsPointer())
	{
		if (!argType->IsPointer())
			return true;
		auto wantPointerType = (BfPointerType*) wantType;
		auto argPointerType = (BfPointerType*) argType;
		InferGenericArgument(argPointerType->mElementType, wantPointerType->mElementType);
		return true;
	}

	return true;
}*/

void DwMethodMatcher::CompareMethods(DbgSubprogram* prevMethodInstance, DwTypeVector* prevGenericArgumentsSubstitute,
	DbgSubprogram* newMethodInstance, DwTypeVector* genericArgumentsSubstitute,
	bool* outNewIsBetter, bool* outNewIsWorse, bool allowSpecializeFail)
{
	int numUsedParams = 0;
	int prevNumUsedParams = 0;
	bool usedExtendedForm = false;
	bool prevUsedExtendedForm = false;

	bool isBetter = false;
	bool isWorse = false;
	int argIdx = 0;

	DbgSubprogram* prevMethodDef = prevMethodInstance;
	DbgSubprogram* newMethodDef = newMethodInstance;

#define SET_BETTER_OR_WORSE(lhs, rhs) \
		if ((lhs) && !(rhs)) isBetter = true; \
		if (!(lhs) && (rhs)) isWorse = true;

	int newArgOffset = newMethodInstance->mHasThis ? 1 : 0;
	int prevArgOffset = prevMethodInstance->mHasThis ? 1 : 0;

	DbgVariable* param = newMethodInstance->mParams.mHead;
	if (newMethodInstance->mHasThis)
		param = param->mNext;
	DbgVariable* prevParam = prevMethodInstance->mParams.mHead;
	if (prevMethodInstance->mHasThis)
		prevParam = prevParam->mNext;

	for (argIdx = 0; argIdx < (int) mArguments.size(); argIdx++)
	{
		DbgTypedValue arg = mArguments[argIdx];
		DbgType* paramType = param->mType;
		DbgType* prevParamType = prevParam->mType;

		numUsedParams++;
		prevNumUsedParams++;

		/*if ((genericArgumentsSubstitute != NULL) && (paramType->IsUnspecializedType()))
			paramType = mModule->SpecializeMethodType(paramType, *genericArgumentsSubstitute, allowSpecializeFail);
		if (prevParamType->IsUnspecializedType())
			prevParamType = mModule->SpecializeMethodType(prevParamType, *prevGenericArgumentsSubstitute, allowSpecializeFail);*/

		//paramType = paramType->RemoveModifiers();
		//prevParamType = prevParamType->RemoveModifiers();

		if (paramType != prevParamType)
		{
			/*if ((paramType->IsGenericParam()) || (prevParamType->IsGenericParam()))
			{
				BF_ASSERT(allowSpecializeFail);
				if (!prevParamType->IsGenericParam())
					isBetter = true;
				else if (!paramType->IsGenericParam())
					isWorse = true;
			}
			else*/ if ((paramType->IsInteger()) && (prevParamType->IsInteger()))
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
				if (mExprEvaluator->CanCast(DbgTypedValue::GetValueless(paramType), prevParamType))
					isBetter = true;
				if (mExprEvaluator->CanCast(DbgTypedValue::GetValueless(prevParamType), paramType))
					isWorse = true;
			}
		}

		/*if (newMethodDef->mParams[argIdx]->mParamType == BfParamType_Params)
			usedExtendedForm = true;
		if (prevMethodDef->mParams[argIdx]->mParamType == BfParamType_Params)
			prevUsedExtendedForm = true;*/

		if ((usedExtendedForm) || (prevUsedExtendedForm))
			break;

		param = param->mNext;
		prevParam = prevParam->mNext;
	}

	// Check for unused extended params as next param - that still counts as using extended form
	/*if ((argIdx < (int) newMethodDef->mParams.size()) && (newMethodDef->mParams[argIdx]->mParamType == BfParamType_Params))
		usedExtendedForm = true;
	if ((argIdx < (int) prevMethodDef->mParams.size()) && (prevMethodDef->mParams[argIdx]->mParamType == BfParamType_Params))
		prevUsedExtendedForm = true;*/

	// Non-generic trumps generic
	/*if ((!isBetter) && (!isWorse))
	{
		SET_BETTER_OR_WORSE(newMethodInstance->mMethodGenericArguments.size() == 0, prevMethodInstance->mMethodGenericArguments.size() == 0);
	}*/

	// Normal form trumps extended form
	if ((!isBetter) && (!isWorse))
	{
		SET_BETTER_OR_WORSE(!usedExtendedForm, !prevUsedExtendedForm);
	}

	// More used params trumps less params
	if ((!isBetter) && (!isWorse))
	{
		int paramDiff = (int) numUsedParams - (int) prevNumUsedParams;
		SET_BETTER_OR_WORSE(paramDiff > 0, paramDiff < 0);
	}

	// Fewer defaults trumps more defaults
	/*if ((!isBetter) && (!isWorse))
	{
		// Since we know the number of used params is the same (previous check), we infer that the rest are defaults
		int paramDiff = (int) newMethodInstance->mParamTypes.size() - (int) prevMethodInstance->mParamTypes.size();
		SET_BETTER_OR_WORSE(paramDiff < 0, paramDiff > 0);
	}*/

	// Check specificity of args
	if ((!isBetter) && (!isWorse))
	{
		param = newMethodInstance->mParams.mHead;
		if (newMethodInstance->mHasThis)
			param = param->mNext;
		prevParam = prevMethodInstance->mParams.mHead;
		if (prevMethodInstance->mHasThis)
			prevParam = prevParam->mNext;

		for (argIdx = 0; argIdx < (int) mArguments.size(); argIdx++)
		{
			DbgType* paramType = param->mType;
			DbgType* prevParamType = prevParam->mType;

			//TODO:
			//isBetter |= mModule->IsTypeMoreSpecific(paramType, prevParamType);
			//isWorse |= mModule->IsTypeMoreSpecific(prevParamType, paramType);

			param = param->mNext;
			prevParam = prevParam->mNext;
		}
	}

	// Prefer virtual - this helps us bind to a virtual override instead of the concrete method implementation so we can ignore it and
	//  continue searching through the base types to find the original virtual method
	if ((!isBetter) && (!isWorse))
	{
		SET_BETTER_OR_WORSE(newMethodInstance->mVirtual, prevMethodInstance->mVirtual);
	}

	if ((!isBetter) && (!isWorse))
	{
		SET_BETTER_OR_WORSE(newMethodInstance->mBlock.mLowPC != 0, prevMethodInstance->mBlock.mLowPC != 0);
	}

	if ((!isBetter) && (!isWorse))
	{
		if ((newMethodInstance->mHasThis) && (prevMethodInstance->mHasThis))
		{
			SET_BETTER_OR_WORSE(
				newMethodInstance->mParams.mHead->mType->IsConst() == mTargetIsConst,
				prevMethodInstance->mParams.mHead->mType->IsConst() == mTargetIsConst);
		}
	}

	*outNewIsBetter = isBetter;
	*outNewIsWorse = isWorse;
}

bool DwMethodMatcher::CheckMethod(DbgType* typeInstance, DbgSubprogram* checkMethod)
{
	bool hadMatch = false;

	checkMethod->PopulateSubprogram();

	// Never consider overrides - they only get found at original method declaration
	/*if ((checkMethod->mVirtual) && (checkMethod->mVTableLoc == -1))
		return true;*/

	//BfMethodInstance* methodInstance = mModule->GetRawMethodInstanceAtIdx(typeInstance, checkMethod->mIdx);
	DbgSubprogram* methodInstance = checkMethod;

	DwAutoComplete* autoComplete = mExprEvaluator->mAutoComplete;
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
	{
		DwAutoComplete::MethodMatchEntry methodMatchEntry;
		methodMatchEntry.mDwSubprogram = methodInstance;
		autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);
	}

	/*if ((mHadExplicitGenericArguments) && (checkMethod->mGenericParams.size() != mBestMethodGenericArguments.size()))
		goto NoMatch;*/

	for (auto& checkGenericArgRef : mCheckMethodGenericArguments)
		checkGenericArgRef = NULL;

	/*mCheckMethodGenericArguments.resize(checkMethod->mGenericParams.size());
	for (auto& genericArgRef : mCheckMethodGenericArguments)
		genericArgRef = NULL;*/

	int argIdx = 0;
	int paramIdx = 0;
	DbgType* paramsElementType = NULL;

	//bool needInferGenericParams = (checkMethod->mGenericParams.size() != 0) && (!mHadExplicitGenericArguments);

	DwTypeVector* genericArgumentsSubstitute = NULL;
	/*if (mHadExplicitGenericArguments)
		genericArgumentsSubstitute = &mBestMethodGenericArguments;
	else if (needInferGenericParams)
		genericArgumentsSubstitute = &mCheckMethodGenericArguments;

	if (needInferGenericParams)
	{
		for (int argIdx = 0; argIdx < (int)mArguments.size(); argIdx++)
		{
			if (argIdx >= (int)checkMethod->mParams.size())
				break;
			auto wantType = methodInstance->mParamTypes[argIdx];
			if (wantType->IsUnspecializedType())
			{
				if (!InferGenericArgument(mArguments[argIdx].mType, wantType))
					goto NoMatch;
			}
		}

		for (int checkArgIdx = 0; checkArgIdx < (int) checkMethod->mGenericParams.size(); checkArgIdx++)
			if (mCheckMethodGenericArguments[checkArgIdx] == NULL)
				goto NoMatch;
	}*/

	// Iterate through params

	int paramOffset = checkMethod->mHasThis ? 1 : 0;

	while (true)
	{
		// Too many arguments
		if (paramIdx >= checkMethod->mParams.Size() - paramOffset)
		{
			break;
		}

		DbgVariable* paramDef = checkMethod->mParams[paramIdx + paramOffset];

		//TODO:
		/*if ((paramDef->mParamType == BfParamType_Params) && (paramsElementType == NULL))
		{
			if (paramIdx >= (int) mArguments.size())
				break; // No params

			if (!mArguments[argIdx])
				goto NoMatch;

			// Check to see if we're directly passing the params type (like an int[])
			auto paramsArrayType = methodInstance->mParamTypes[paramIdx];
			if (mModule->CanCast(mArguments[argIdx], paramsArrayType))
			{
				argIdx++;
				paramIdx++;
				break;
			}

			BF_ASSERT(paramsArrayType->IsArray());
			auto arrayType = (BfArrayType*)paramsArrayType;
			paramsElementType = arrayType->mTypeGenericArguments[0];

			while (argIdx < (int)mArguments.size())
			{
				if (!mArguments[argIdx])
					goto NoMatch;
				if (!mModule->CanCast(mArguments[argIdx], paramsElementType))
					goto NoMatch;
				argIdx++;
			}
			break;
		}*/

		if (paramIdx >= (int) mArguments.size())
		{
			// We have defaults the rest of the way, so that's cool
			//TODO:
			/*if (paramDef->mParamDeclaration->mInitializer != NULL)
				break;*/

			// We have unused params left over
			goto NoMatch;
		}

		auto wantType = paramDef->mType;
		/*if ((genericArgumentsSubstitute != NULL) && (wantType->IsUnspecializedType()))
			wantType = mModule->SpecializeMethodType(wantType, *genericArgumentsSubstitute);*/

		if (!mArguments[argIdx])
			goto NoMatch;

		if (!mExprEvaluator->CanCast(mArguments[argIdx], wantType))
			goto NoMatch;

		paramIdx++;
		argIdx++;

		if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo))
		{
			auto methodMatchInfo = autoComplete->mMethodMatchInfo;
			if (!methodMatchInfo->mHadExactMatch)
			{
				bool isBetter = false;
				bool isWorse = false;
				int methodIdx = (int)methodMatchInfo->mInstanceList.size() - 1;

				if (methodMatchInfo->mBestIdx < (int)methodMatchInfo->mInstanceList.size())
				{
					auto prevMethodMatchEntry = &methodMatchInfo->mInstanceList[methodMatchInfo->mBestIdx];
					if (checkMethod->mParams.Size() < (int)mArguments.size())
					{
						isWorse = true;
					}
					else if (prevMethodMatchEntry->mDwSubprogram->mParams.Size() < (int) mArguments.size())
					{
						isBetter = true;
					}
					else
					{
						//BfMethodInstance* prevMethodInstance = mModule->GetRawMethodInstanceAtIdx(prevMethodMatchEntry->mTypeInstance, prevMethodMatchEntry->mMethodDef->mIdx);
						DbgSubprogram* prevMethodInstance = prevMethodMatchEntry->mDwSubprogram;
						CompareMethods(prevMethodInstance, &prevMethodMatchEntry->mDwGenericArguments,
							methodInstance, genericArgumentsSubstitute, &isBetter, &isWorse, true);
					}
				}

				if ((argIdx > methodMatchInfo->mMostParamsMatched) ||
					((argIdx == methodMatchInfo->mMostParamsMatched) && (isBetter)) ||
					((argIdx == methodMatchInfo->mMostParamsMatched) && (methodIdx == methodMatchInfo->mPrevBestIdx) && (!isWorse)))
				{
					methodMatchInfo->mBestIdx = methodIdx;
					methodMatchInfo->mMostParamsMatched = argIdx;
				}
			}
		}
	}

	//TODO: Does this ever get hit?
	// Not enough arguments?
	if (argIdx < (int)mArguments.size())
	{
		goto NoMatch;
	}

	// Method is applicable, check to see which method is better
	if (mBestMethodDef != NULL)
	{
		bool isBetter = false;
		bool isWorse = false;
		//BfMethodInstance* prevMethodInstance = mModule->GetRawMethodInstanceAtIdx(mBestMethodTypeInstance, mBestMethodDef->mIdx);
		DbgSubprogram* prevMethodInstance = mBestMethodDef;
		CompareMethods(prevMethodInstance, &mBestMethodGenericArguments, methodInstance, genericArgumentsSubstitute, &isBetter, &isWorse, false);

		// If we had both a 'better' and 'worse', that's ambiguous because the methods are each better in different ways (not allowed)
		//  And if neither better nor worse then they are equally good, which is not allowed either
		if (((!isBetter) && (!isWorse)) || ((isBetter) && (isWorse)))
		{
			// When we have [Checked] and [Unchecked] both, they will confict here but be equal
			if (!prevMethodInstance->Equals(methodInstance))
			{
				mExprEvaluator->Fail(StrFormat("Ambiguous method call between '%s' and '%s'", prevMethodInstance->ToString().c_str(),
					methodInstance->ToString().c_str()), mTargetSrc);
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

	hadMatch = true;
	mBestMethodDef = checkMethod;

NoMatch:
	if (!hadMatch)
	{
		if (mBestMethodDef != NULL)
			return true;

		//TODO:
		/*if ((mHadExplicitGenericArguments) && (mBestMethodGenericArguments.size() != checkMethod->mGenericParams.size()))
			return true;*/

		// At least prefer a backup method that we have an address for
		if ((mBackupMethodDef != NULL) && (mBackupMethodDef->mBlock.mLowPC != 0))
			return true;

		mBackupMethodDef = checkMethod;
		// Lie temporarily to store at least one candidate (but mBestMethodDef is still NULL)
		hadMatch = true;
	}

	if (hadMatch)
	{
		mBestMethodTypeInstance = typeInstance;
		if (!mHadExplicitGenericArguments)
		{
			mBestMethodGenericArguments = mCheckMethodGenericArguments;
		}
	}

Done:
	if ((autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo) && (genericArgumentsSubstitute != NULL))
	{
		auto methodMatchInfo = autoComplete->mMethodMatchInfo;
		methodMatchInfo->mInstanceList[methodMatchInfo->mInstanceList.size() - 1].mDwGenericArguments = *genericArgumentsSubstitute;
	}

	return true;
}

bool DwMethodMatcher::CheckType(DbgType* typeInstance, bool isFailurePass)
{
	typeInstance = typeInstance->RemoveModifiers();
	if (typeInstance->IsPointer())
		typeInstance = typeInstance->mTypeParam;
	typeInstance = typeInstance->GetPrimaryType();

	//bool allowPrivate = typeInstance == mExprEvaluator->GetCurrentType();
	//bool allowProtected = allowPrivate || mExprEvaluator->TypeIsSubTypeOf(mExprEvaluator->GetCurrentType(), typeInstance);
	bool allowPrivate = true;
	bool allowProtected = true;

	auto curType = typeInstance;

	bool wantCtor = false;

	auto baseItr = typeInstance->mBaseTypes.begin();
	auto altItr = typeInstance->mAlternates.begin();

	while (true)
	{
		bool foundMethodInType = false;

		curType->PopulateType();
		// Why did we have this "&&" check on there?
		//  It made it so invocations would fail unless we had autocompleted the type
		if ((curType->mNeedsGlobalsPopulated)
            /*&& ((curType->IsNamespace()) || (curType->IsRoot()))*/)
		{
			// These aren't proper TPI types so we don't have any method declarations until we PopulateTypeGlobals
			mExprEvaluator->mDbgModule->PopulateTypeGlobals(curType);
		}

		for (auto methodNameEntry : curType->mMethodNameList)
		{
			if ((methodNameEntry->mCompileUnitId != -1) && (methodNameEntry->mName == mMethodName))
			{
				// If we hot-replaced this type then we replaced and parsed all the methods too
				if (!curType->mCompileUnit->mDbgModule->IsObjectFile())
					curType->mCompileUnit->mDbgModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
				methodNameEntry->mCompileUnitId = -1;
			}
		}

		auto checkMethod = curType->mMethodList.mHead;
		while (checkMethod != NULL)
		{
			/*if ((!checkMethod->mHasThis) && (!checkStatic))
				continue;
			if ((checkMethod->mHasThis) && (!checkNonStatic))
				continue;*/

			// These can only be invoked when the target itself is the interface
			/*if (checkMethod->mExplicitInterface != NULL)
				continue;*/

			if ((wantCtor) && (checkMethod->mMethodType != DbgMethodType_Ctor))
			{
				checkMethod = checkMethod->mNext;
				continue;
			}
			if (!wantCtor)
			{
				if (checkMethod->mMethodType != DbgMethodType_Normal)
				{
					checkMethod = checkMethod->mNext;
					continue;
				}
				if ((checkMethod->mName == NULL) || (checkMethod->mName != mMethodName))
				{
					checkMethod = checkMethod->mNext;
					continue;
				}
			}

			if ((!isFailurePass) && (!CheckProtection(checkMethod->mProtection, allowProtected, allowPrivate)))
			{
				checkMethod = checkMethod->mNext;
				continue;
			}

			if (!curType->mHasGlobalsPopulated)
				mExprEvaluator->mDbgModule->PopulateTypeGlobals(curType);

			/*if (!foundMethodInType)
			{
				if (curType->mCompileUnit->mLanguage != DbgLanguage_Beef)
				{
					String fullMethodName = String(curType->mTypeName) + "::" + mMethodName;
					curType->mCompileUnit->mDbgModule->EnsureMethodMapped(fullMethodName.c_str());
				}
				foundMethodInType = true;
			}*/

			if (!CheckMethod(curType, checkMethod))
				return false;

			checkMethod = checkMethod->mNext;
		}

		if (mBestMethodDef != NULL)
		{
			if ((mBestMethodDef->mVirtual) && (mBestMethodDef->mVTableLoc == -1))
			{
				// It's an override, keep searching through the base types to find the original method declaration
				mBestMethodDef = NULL;
				mBackupMethodDef = NULL;
			}
			else
				return true;
		}

		/*if (baseItr != typeInstance->mBaseTypes.end())
		{
			auto baseTypeEntry = *baseItr;
			curType = baseTypeEntry->mBaseType;
			baseItr++;
			continue;;
		}*/

		if (altItr != typeInstance->mAlternates.end())
		{
			curType = *altItr;
			++altItr;
			continue;
		}

		/*allowPrivate = false;

		if ((isFailurePass) && (mBackupMethodDef != NULL))
			break;*/
		break;
	}

	if (mBestMethodDef == NULL)
	{
		// FAILED, but select the first method which will fire an actual error on param type matching
		mBestMethodDef = mBackupMethodDef;

		for (auto subType : typeInstance->mSubTypeList)
		{
			if ((subType->mName == NULL) || (subType->IsGlobalsContainer()))
			{
				if (!CheckType(subType, isFailurePass))
					return false;
				if (mBestMethodDef != NULL)
					break;
			}
		}

		if (mBestMethodDef == NULL)
		{
			for (auto baseTypeEntry : typeInstance->mBaseTypes)
			{
				if (!CheckType(baseTypeEntry->mBaseType, isFailurePass))
					return false;
				if (mBestMethodDef != NULL)
					break;
			}
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

DbgExprEvaluator::DbgExprEvaluator(WinDebugger* winDebugger, DbgModule* dbgModule, BfPassInstance* passInstance, int callStackIdx, int cursorPos)
{
	mCountResultOverride = -1;
	mCapturingChildRef = true;
	mPassInstance = passInstance;
	mDebugger = winDebugger;
	mLanguage = DbgLanguage_NotSet;
	mOrigDbgModule = dbgModule;
	if (dbgModule != NULL)
	{
		mDebugTarget = dbgModule->mDebugTarget;
		mDbgModule = dbgModule->GetLinkedModule();
	}
	else
	{
		mDebugTarget = NULL;
		mDbgModule = NULL;
	}

	mDbgCompileUnit = NULL;
	mExplicitThisExpr = NULL;
	mExpectingType = NULL;
	mCurMethod = NULL;
	mIgnoreErrors = false;
	mCallStackIdx = callStackIdx;
	mCursorPos = cursorPos;
	mAutoComplete = NULL;
	mIsEmptyTarget = (dbgModule == NULL) || (dbgModule->mDebugTarget->mIsEmpty);
	mExpressionFlags = DwEvalExpressionFlag_None;
	mHadSideEffects = false;
	mBlockedSideEffects = false;
	mReferenceId = NULL;
	mIsComplexExpression = false;
	mHadMemberReference = false;
	mCreatedPendingCall = false;
	mStackSearch = NULL;
	mValidateOnly = false;
	mReceivingValue = NULL;
	mCallResults = NULL;
	mCallResultIdx = 0;
	mCallStackPreservePos = 0;
	mPropGet = NULL;
	mPropSet = NULL;
	mPropSrc = NULL;
}

DbgExprEvaluator::~DbgExprEvaluator()
{
	delete mStackSearch;
}

DbgTypedValue DbgExprEvaluator::GetInt(int value)
{
	DbgTypedValue dbgValue;
	dbgValue.mType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
	dbgValue.mInt32 = value;
	return dbgValue;
}

DbgTypedValue DbgExprEvaluator::GetString(const StringImpl& str)
{
	String* resultPtr;
	mDebugger->mLiteralSet.TryAdd(str, &resultPtr);

	DbgTypedValue dbgValue;

	auto language = GetLanguage();
	auto charPtrType = mDbgModule->GetPrimitiveType((language == DbgLanguage_Beef) ? DbgType_UChar : DbgType_SChar, language);
	if (charPtrType != NULL)
		charPtrType = mDbgModule->GetPointerType(charPtrType);
	if (charPtrType != NULL)
	{
		dbgValue.mType = charPtrType;
		dbgValue.mLocalPtr = resultPtr->c_str();
		dbgValue.mIsLiteral = true;
		dbgValue.mDataLen = resultPtr->mLength;
	}

	return dbgValue;
}

void DbgExprEvaluator::Fail(const StringImpl& error, BfAstNode* node)
{
	if ((!mIgnoreErrors) && (mPassInstance != NULL))
		mPassInstance->Fail(error, node);
}

void DbgExprEvaluator::Warn(const StringImpl& error, BfAstNode * node)
{
	if ((!mIgnoreErrors) && (mPassInstance != NULL))
		mPassInstance->Warn(0, error, node);
}

DbgType* DbgExprEvaluator::GetExpectingType()
{
	if ((mExpectingType == NULL) && (!mExpectingTypeName.empty()))
	{
		mExpectingType = ResolveTypeRef(mExpectingTypeName);
		mExpectingTypeName.clear();
	}
	return mExpectingType;
}

void DbgExprEvaluator::GetNamespaceSearch()
{
	if (!mNamespaceSearch.empty())
		return;

	auto currentMethod = GetCurrentMethod();
	if (currentMethod != NULL)
	{
		auto parent = currentMethod->GetParent();
		while (parent != NULL)
		{
			parent = parent->GetPrimaryType();
			mNamespaceSearch.push_back(parent);
			parent = parent->mParent;
		}
	}

	if (!mNamespaceSearchStr.empty())
	{
		auto language = GetLanguage();
		int lastComma = -1;
		for (int i = 0; i < (int)mNamespaceSearchStr.length(); i++)
		{
			if (mNamespaceSearchStr[i] == ',')
			{
				String namespaceEntry = mNamespaceSearchStr.Substring(lastComma + 1, i - lastComma - 1);
				auto dbgTypeEntry = mDbgModule->mTypeMap.Find(namespaceEntry.c_str(), language);
				if (dbgTypeEntry != NULL)
					mNamespaceSearch.push_back(dbgTypeEntry->mValue);
				lastComma = i;
			}
		}

		String namespaceEntry = mNamespaceSearchStr.Substring(lastComma + 1);
		auto dbgTypeEntry = mDbgModule->mTypeMap.Find(namespaceEntry.c_str(), language);
		if (dbgTypeEntry != NULL)
			mNamespaceSearch.push_back(dbgTypeEntry->mValue);
	}
}

DbgType* DbgExprEvaluator::ResolveSubTypeRef(DbgType* checkType, const StringImpl& name)
{
	checkType->PopulateType();

	for (auto subType : checkType->mSubTypeList)
	{
		if (strcmp(subType->mTypeName, name.c_str()) == 0)
			return subType;
	}

	for (auto checkBaseType : checkType->mBaseTypes)
	{
		DbgType* subType = ResolveSubTypeRef(checkBaseType->mBaseType, name);
		if (subType != NULL)
			return subType;
	}

	return NULL;
}

DbgType* DbgExprEvaluator::FixType(DbgType* dbgType)
{
	if (dbgType->IsBfObject())
		dbgType = mDbgModule->GetPointerType(dbgType);
	return dbgType;
}

DbgTypedValue DbgExprEvaluator::FixThis(const DbgTypedValue& thisVal)
{
	/*if ((thisVal.mType != NULL) && (thisVal.mType->GetLanguage() == DbgLanguage_Beef) &&
		(!thisVal.mType->IsBfObjectPtr()) && (thisVal.mType->IsPointer()))
	{
		// Beef structs expect 'this' as a valuetype, not a pointer
		DbgTypedValue fixedThisVal;
		fixedThisVal.mType = thisVal.mType->mTypeParam;
		fixedThisVal.mSrcAddress = thisVal.mPtr;
		fixedThisVal.mPtr = thisVal.mPtr;
		int byteCount = fixedThisVal.mType->GetByteCount();
		if (byteCount <= 8) // For typed primitive loads
			mDebugger->ReadMemory(fixedThisVal.mSrcAddress, byteCount, &fixedThisVal.mInt64);
		return fixedThisVal;
	}*/
	return thisVal;
}

DbgType* DbgExprEvaluator::ResolveTypeRef(BfTypeReference* typeRef)
{
	mDbgModule->ParseTypeData();

	if (auto ptrTypeRef = BfNodeDynCastExact<BfPointerTypeRef>(typeRef))
	{
		// If it's a pointer type ref then create our own type
		auto innerType = ResolveTypeRef(ptrTypeRef->mElementType);
		if (innerType == NULL)
			return NULL;
		return mDbgModule->GetPointerType(innerType);
	}

	if (auto arrayTypeRef = BfNodeDynCastExact<BfArrayTypeRef>(typeRef))
	{
		if (arrayTypeRef->mParams.size() == 1)
		{
			if (auto literalExpr = BfNodeDynCastExact<BfLiteralExpression>(arrayTypeRef->mParams[0]))
			{
				auto elementType = ResolveTypeRef(arrayTypeRef->mElementType);
				if (elementType == NULL)
					return NULL;

				int count = 1;
				if ((literalExpr->mValue.mTypeCode >= BfTypeCode_Int16) && (literalExpr->mValue.mTypeCode <= BfTypeCode_UIntUnknown))
				{
					count = literalExpr->mValue.mInt32;
					if (count < 0)
					{
						Fail(StrFormat("Illegal array size"), arrayTypeRef->mParams[0]);
						count = 1;
					}
				}
				else
				{
					Fail(StrFormat("Illegal array size type"), arrayTypeRef->mParams[0]);
				}

				return mDbgModule->GetSizedArrayType(elementType, count);
			}
		}
	}

	if (auto declTypeRef = BfNodeDynCastExact<BfExprModTypeRef>(typeRef))
	{
		mResult = DbgTypedValue();
		VisitChild(declTypeRef->mTarget);
		auto type = mResult.mType;
		mResult = DbgTypedValue();
		return type;
	}

	String name = typeRef->ToString();
	if (name.StartsWith("_T_"))
	{
		auto dbgModule = mDbgModule;

		char* endPtr = NULL;
		int typeIdx = strtol(name.c_str() + 3, &endPtr, 10);
		if ((endPtr != NULL) && (*endPtr == '_'))
		{
			int moduleIdx = typeIdx;
			typeIdx = atoi(endPtr + 1);
			mDebugTarget->mDbgModuleMap.TryGetValue(moduleIdx, &dbgModule);
		}

		if ((dbgModule != NULL) && (typeIdx >= 0) && (typeIdx < (int)dbgModule->mTypes.size()))
			return dbgModule->mTypes[typeIdx];
	}

	auto entry = mDbgModule->mTypeMap.Find(name.c_str(), GetLanguage());
	if (entry != NULL)
		return FixType(entry->mValue);

	if (mExplicitThis)
	{
		bool isPtr = typeRef->IsA<BfPointerTypeRef>();
		if (isPtr)
		{
			BfPointerTypeRef* ptrTypeRef = (BfPointerTypeRef*)typeRef;
			BfTypeReference* innerTypeRef = ptrTypeRef->mElementType;
			name = innerTypeRef->ToString();
		}

		auto checkType = mExplicitThis.mType;
		if (checkType->IsPointer())
			checkType = checkType->mTypeParam;

		ResolveSubTypeRef(checkType, name);
	}

	auto currentType = GetCurrentType();
	if (currentType != NULL)
	{
		GetNamespaceSearch();
		for (auto usingNamespace : mNamespaceSearch)
		{
			auto usingNamespaceString = usingNamespace->ToString();
			entry = mDbgModule->mTypeMap.Find((usingNamespaceString + "." + name).c_str(), GetLanguage());
			if (entry != NULL)
				return FixType(entry->mValue);
		}
	}

	Fail(StrFormat("Unable to locate type"), typeRef);
	return NULL;
}

DbgType* DbgExprEvaluator::ResolveTypeRef(BfAstNode* typeRef, BfAstNode** parentChildRef)
{
	StringT<128> name = typeRef->ToString();
	if ((name.StartsWith("_T_")) && ((int)name.IndexOf('.') == -1))
	{
		int endIdx = name.length();
		for (int i = 3; i < (int)name.length(); i++)
		{
			char c = name[i];
			if (((c < '0') || (c > '9')) && (c != '_'))
			{
				endIdx = i;
				break;
			}
		}

		auto dbgModule = mDbgModule;

		char* endPtr = NULL;
		int typeIdx = strtol(name.c_str() + 3, &endPtr, 10);
		if ((endPtr != NULL) && (*endPtr == '_'))
		{
			int moduleIdx = typeIdx;
			typeIdx = atoi(endPtr + 1);
			mDebugTarget->mDbgModuleMap.TryGetValue(moduleIdx, &dbgModule);
		}

		if ((dbgModule != NULL) && (typeIdx >= 0) && (typeIdx < (int)dbgModule->mTypes.size()))
		{
			if ((mExplicitThisExpr != NULL) && (parentChildRef != NULL))
				mDeferredInsertExplicitThisVector.push_back(NodeReplaceRecord(typeRef, parentChildRef, true));
			DbgType* dbgType = dbgModule->mTypes[typeIdx];
			for (int i = endIdx; i < (int)name.length(); i++)
			{
				if (name[i] == '*')
					dbgType = dbgModule->GetPointerType(dbgType);
			}
			return dbgType;
		}
	}

	/*for (int i = 1; i < (int)name.length(); i++)
	{
		if ((name[i] == ':') && (name[i - 1] == ':'))
		{
			name[i] = '.';
			name.erase(name.begin() + i - 1);
			i--;
		}
	}*/

	auto language = GetLanguage();
	auto entry = mDbgModule->FindType(name.c_str(), language);
	if (entry != NULL)
		return entry->mValue;
	auto currentType = GetCurrentType();
	if (currentType != NULL)
	{
		GetNamespaceSearch();
		for (auto usingNamespace : mNamespaceSearch)
		{
			auto usingNamespaceString = usingNamespace->ToString();
			entry = mDbgModule->FindType((usingNamespaceString + "." + name).c_str(), language);
			if (entry != NULL)
				return entry->mValue;
		}
	}
	Fail("Unable to locate type", typeRef);
	return NULL;
}

DbgType* DbgExprEvaluator::ResolveTypeRef(const StringImpl& typeName)
{
	auto entry = mDbgModule->mTypeMap.Find(typeName.c_str(), GetLanguage());
	if (entry != NULL)
		return entry->mValue;
	mPassInstance->Fail("Unable to locate type: " + typeName);
	return NULL;
}

bool DbgExprEvaluator::TypeIsSubTypeOf(DbgType* srcType, DbgType* wantType, int* thisOffset, addr_target* thisAddr)
{
	if (srcType == NULL)
		return false;

	srcType = srcType->GetPrimaryType();
	wantType = wantType->GetPrimaryType();

	if ((wantType->IsPrimitiveType()) && (srcType->IsPrimitiveType()))
	{
		return wantType->mTypeCode == srcType->mTypeCode;
	}

	if ((srcType == NULL) || (wantType == NULL))
		return false;
	if (srcType->Equals(wantType))
		return true;

	if (srcType->mTypeCode == DbgType_TypeDef)
		return TypeIsSubTypeOf(srcType->mTypeParam, wantType);
	if (wantType->mTypeCode == DbgType_TypeDef)
		return TypeIsSubTypeOf(srcType, wantType->mTypeParam);

	srcType->PopulateType();
	for (auto srcBaseType : srcType->mBaseTypes)
	{
		bool isSubtype = TypeIsSubTypeOf(srcBaseType->mBaseType, wantType);
		if (isSubtype)
		{
			if (srcBaseType->mVTableOffset != -1)
			{
				if (thisAddr == NULL)
					return false;
				int32 virtThisOffset = 0;

				addr_target vtableAddr = 0;
				gDebugger->ReadMemory(*thisAddr, sizeof(addr_target), &vtableAddr);
				gDebugger->ReadMemory(vtableAddr + srcBaseType->mVTableOffset * sizeof(int32), sizeof(int32), &virtThisOffset);

				*thisOffset += virtThisOffset;
			}
			else if (thisOffset != NULL)
				*thisOffset += srcBaseType->mThisOffset;
			return true;
		}
	}

	return false;
}

DbgTypedValue DbgExprEvaluator::GetBeefTypeById(int typeId)
{
	if (mDebugTarget->mTargetBinary == NULL)
		return DbgTypedValue();

	if (mDebugTarget->mTargetBinary->mBfTypesInfoAddr == 0)
	{
		mDebugTarget->mTargetBinary->mBfTypesInfoAddr = -1;

		mDebugTarget->mTargetBinary->ParseTypeData();
		auto typeTypeEntry = mDebugTarget->mTargetBinary->FindType("System.Type", DbgLanguage_Beef);
		if ((typeTypeEntry != NULL) && (typeTypeEntry->mValue != NULL))
		{
			auto typeType = typeTypeEntry->mValue;
			mDebugTarget->mTargetBinary->mBfTypeType = typeType;
			if (typeType->mNeedsGlobalsPopulated)
				typeType->mCompileUnit->mDbgModule->PopulateTypeGlobals(typeType);

			for (auto member : typeType->mMemberList)
			{
				if ((member->mIsStatic) && (member->mName != NULL) && (strcmp(member->mName, "sTypes") == 0) && (member->mLocationData != NULL))
				{
					auto stackFrame = GetStackFrame();
					DbgAddrType addrType;
					mDebugTarget->mTargetBinary->mBfTypesInfoAddr = member->mCompileUnit->mDbgModule->EvaluateLocation(NULL, member->mLocationData, member->mLocationLen, stackFrame, &addrType);
				}
			}

			if (mDebugTarget->mTargetBinary->mBfTypesInfoAddr <= 0)
			{
				mDebugTarget->mTargetBinary->ParseSymbolData();
				auto entry = mDebugTarget->mTargetBinary->mSymbolNameMap.Find(
#ifdef BF_DBG_64
					"?sTypes@Type@System@bf@@2PEAPEAV123@A"
#else
					"?sTypes@Type@System@bf@@2PAPAV123@A"
#endif
					);

				if (entry)
					mDebugTarget->mTargetBinary->mBfTypesInfoAddr = entry->mValue->mAddress;
			}
		}
	}

	if (mDebugTarget->mTargetBinary->mBfTypesInfoAddr > 0)
	{
		DbgTypedValue typedVal;
		typedVal.mType = mDebugTarget->mTargetBinary->mBfTypeType;
		addr_target addr = mDebugTarget->mTargetBinary->mBfTypesInfoAddr + typeId * sizeof(addr_target);
		typedVal.mSrcAddress = mDebugger->ReadMemory<addr_target>(addr);
		return typedVal;
	}

	return DbgTypedValue();
}

void DbgExprEvaluator::BeefStringToString(addr_target addr, String& outStr)
{
	mDebugTarget->GetCompilerSettings();
	int objectSize = mDebugTarget->mBfObjectSize;

	int32 strLen = 0;
	addr_target charPtr = 0;
	if (!mDebugTarget->mBfHasLargeStrings)
	{
		strLen = mDebugger->ReadMemory<int32>(addr + objectSize);
		int32 allocSizeAndFlags = mDebugger->ReadMemory<int32>(addr + objectSize + 4);
		if ((allocSizeAndFlags & 0x40000000) != 0)
			charPtr = mDebugger->ReadMemory<addr_target>(addr + objectSize + 4 + 4);
		else
			charPtr = addr + objectSize + 4 + 4;
	}
	else
	{
		strLen = mDebugger->ReadMemory<int32>(addr + objectSize);
		int64 allocSizeAndFlags = mDebugger->ReadMemory<int64>(addr + objectSize + 8);
		if ((allocSizeAndFlags & 0x40000000'00000000LL) != 0)
			charPtr = mDebugger->ReadMemory<addr_target>(addr + objectSize + 8 + 8);
		else
			charPtr = addr + objectSize + 4 + 4;
	}

	if ((strLen > 0) && (strLen < 4096))
	{
		outStr.Append('?', strLen);
		mDebugger->ReadMemory(charPtr, strLen, &outStr[outStr.length() - strLen]);
	}
}

void DbgExprEvaluator::BeefStringToString(const DbgTypedValue& val, String& outStr)
{
	mDebugTarget->GetCompilerSettings();

	auto useVal = val;
	if (useVal.mType->IsBfObjectPtr())
	{
		useVal.mType = useVal.mType->mTypeParam;
		useVal.mSrcAddress = useVal.mPtr;
	}

	BeefStringToString(useVal.mSrcAddress, outStr);
}

void DbgExprEvaluator::BeefTypeToString(const DbgTypedValue& val, String& outStr)
{
	if (!val)
		return;

	mDebugTarget->GetCompilerSettings();

	auto useVal = val;
	if (useVal.mType->IsBfObjectPtr())
	{
		useVal.mType = useVal.mType->mTypeParam;
		useVal.mSrcAddress = useVal.mPtr;
	}

	typedef int32 _TypeId;
	typedef uint32 _TypeFlags;
	typedef int8 _TypeCode;

#pragma pack(push, 1)
	struct _Type
	{
		int32 mSize;
		_TypeId mTypeId;
		_TypeId mBoxedType;
		_TypeFlags mTypeFlags;
		int32 mMemberDataOffset;
		_TypeCode mTypeCode;
		uint8 mAlign;
	};

	struct _PointerType : _Type
	{
		int16 mPadding0;
		_TypeCode mElementType;
	};

	struct _SizedArrayType : _Type
	{
		int16 mPadding0;
		_TypeId mElementType;
		int32 mElementCount;
	};

	struct _String
	{
	};
	struct _MethodData
	{
	};
	struct _FieldData
	{
	};
	struct _ClassVData
	{
	};

	struct _TypeInstance : public _Type
	{
#ifdef BF_DBG_32
		int16 mPadding0;
#else
		int16 mPadding0;
#endif

		addr_target mTypeClassVData;
		addr_target mName;
		addr_target mNamespace;
		int32 mInstSize;
		int32 mInstAlign;
		int32 mCustomAttributesIdx;
		_TypeId mBaseType;
		_TypeId mUnderlyingType;
		_TypeId mOuterType;
		int32 mInheritanceId;
		int32 mInheritanceCount;

		uint8 mInterfaceSlot;
		uint8 mInterfaceCount;
		int16 mInterfaceMethodCount;
		int16 mMethodDataCount;
		int16 mPropertyDataCount;
		int16 mFieldDataCount;

#ifdef BF_DBG_32
		int16 mPadding1;
#else
		int8 mPadding1[6];
#endif

		addr_target mInterfaceDataPtr;
		addr_target mInterfaceMethodTable;
		addr_target mMethodDataPtr;
		addr_target mPropertyDataPtr;
		addr_target mFieldDataPtr;
		addr_target mCustomAttrDataPtr;
	};

	struct _SpecializedGenericType : _TypeInstance
	{
		_TypeId mUnspecializedType;
#ifdef BF_DBG_64
		int32 mPadding0;
#endif
		addr_target mResolvedTypeRefs;
	};

	struct _ArrayType : _SpecializedGenericType
	{
		int32 mElementSize;
		uint8 mRank;
		uint8 mElemensDataOffset;
	};
#pragma pack(pop)

	int typeIdSize = sizeof(_TypeId);
	int ptrSize = (int)sizeof(addr_target);
	int objectSize = mDebugTarget->mBfObjectSize;
	int typeSize = sizeof(_Type);

	int typeInstanceSize = objectSize + sizeof(_TypeInstance);

	auto addr = useVal.mSrcAddress;
	auto typeAddr = addr + objectSize;

	_TypeFlags typeFlags = mDebugger->ReadMemory<_TypeFlags>(typeAddr + offsetof(_Type, mTypeFlags));

	if ((typeFlags & BfTypeFlags_Array) != 0)
	{
		_TypeId unspecializedTypeId = mDebugger->ReadMemory<_TypeId>(typeAddr + offsetof(_SpecializedGenericType, mUnspecializedType));
		addr_target elementsArrayAddr = mDebugger->ReadMemory<addr_target>(typeAddr + offsetof(_SpecializedGenericType, mResolvedTypeRefs));
		_TypeId elementTypeId = mDebugger->ReadMemory<_TypeId>(elementsArrayAddr);

		auto elementType = GetBeefTypeById(elementTypeId);
		BeefTypeToString(elementType, outStr);

		outStr += "[";
		int rank = mDebugger->ReadMemory<uint8>(typeAddr + offsetof(_ArrayType, mRank));
		for (int commaIdx = 0; commaIdx < rank - 1; commaIdx++)
			outStr += ",";
		outStr += "]";
	}
	else if ((typeFlags & BfTypeFlags_Pointer) != 0)
	{
		_TypeId elementTypeId = mDebugger->ReadMemory<_TypeId>(typeAddr + offsetof(_PointerType, mElementType));
		auto elementType = GetBeefTypeById(elementTypeId);
		BeefTypeToString(elementType, outStr);
		outStr += "*";
	}
 	else if ((typeFlags & BfTypeFlags_Delegate) != 0)
 	{
		outStr += "delegate";
 	}
	else if ((typeFlags & BfTypeFlags_Function) != 0)
	{
		outStr += "function";
	}
// 	else if ((typeFlags & BfTypeFlags_Tuple) != 0)
// 	{
// 		outStr += "function";
// 	}
	else if ((typeFlags & BfTypeFlags_SizedArray) != 0)
	{
		_TypeId elementTypeId = mDebugger->ReadMemory<_TypeId>(typeAddr + objectSize + offsetof(_SizedArrayType, mElementType));
		auto elementType = GetBeefTypeById(elementTypeId);
		BeefTypeToString(elementType, outStr);
		int elementCount = mDebugger->ReadMemory<int32>(typeAddr + objectSize + offsetof(_SizedArrayType, mElementCount));
		outStr += StrFormat("[%d]", elementCount);
	}
	else if (((typeFlags & BfTypeFlags_Struct) != 0) || ((typeFlags & BfTypeFlags_TypedPrimitive) != 0) || ((typeFlags & BfTypeFlags_Object) != 0))
	{
		addr_target addr0 = typeAddr;
		addr_target addr1 = typeAddr + offsetof(_Type, mTypeFlags);
		addr_target addr2 = typeAddr + offsetof(_Type, mAlign);
		addr_target addr3 = typeAddr + offsetof(_TypeInstance, mPadding0);
		addr_target addr4 = typeAddr + offsetof(_TypeInstance, mTypeClassVData);

		addr_target namePtr = mDebugger->ReadMemory<addr_target>(typeAddr + offsetof(_TypeInstance, mName));
		addr_target namespacePtr = mDebugger->ReadMemory<addr_target>(typeAddr + offsetof(_TypeInstance, mNamespace));
		int outerTypeId = mDebugger->ReadMemory<_TypeId>(typeAddr + offsetof(_TypeInstance, mOuterType));
		DbgTypedValue outerType;

		if (outerTypeId != 0)
		{
			outerType = GetBeefTypeById(outerTypeId);
			BeefTypeToString(outerType, outStr);
			outStr += ".";
		}
		else if (namespacePtr != 0)
		{
			String namespaceName;
			BeefStringToString(namespacePtr, namespaceName);
			if (!namespaceName.empty())
			{
				outStr += namespaceName;
                outStr += ".";
			}
		}
		BeefStringToString(namePtr, outStr);

		if ((typeFlags & BfTypeFlags_SpecializedGeneric) != 0)
		{
			int unspecializedTypeId = mDebugger->ReadMemory<_TypeId>(addr + typeInstanceSize);
			auto unspecializedType = GetBeefTypeById(unspecializedTypeId);

			outStr += "<";

			if (unspecializedType)
			{
				int startGenericIdx = 0;
				int genericCount = mDebugger->ReadMemory<uint8>(unspecializedType.mSrcAddress + typeInstanceSize);

				if (outerType)
				{
					int outerUnspecializedTypeId = mDebugger->ReadMemory<_TypeId>(outerType.mSrcAddress + typeInstanceSize);
					auto outerUnspecializedType = GetBeefTypeById(outerUnspecializedTypeId);
					startGenericIdx = mDebugger->ReadMemory<uint8>(outerUnspecializedType.mSrcAddress + typeInstanceSize);
				}

				addr_target genericParamsPtr = mDebugger->ReadMemory<addr_target>(addr + BF_ALIGN(typeInstanceSize + typeIdSize, ptrSize));
				for (int i = startGenericIdx; i < genericCount; i++)
				{
					int genericParamTypeId = mDebugger->ReadMemory<_TypeId>(genericParamsPtr + i * typeIdSize);
					if (i > startGenericIdx)
						outStr += ", ";
					BeefTypeToString(GetBeefTypeById(genericParamTypeId), outStr);
				}
			}

			outStr += ">";
		}
	}
	else
	{
		BfTypeCode typeCode = (BfTypeCode)mDebugger->ReadMemory<uint8>(typeAddr + (int)offsetof(_Type, mTypeCode));
		if (typeCode == BfTypeCode_Pointer)
		{
			_TypeId elementType = mDebugger->ReadMemory<_TypeId>(typeAddr + offsetof(_PointerType, mElementType));
			BeefTypeToString(GetBeefTypeById(elementType), outStr);
			outStr += "*";
			return;
		}

		switch (typeCode)
		{
		case BfTypeCode_None: outStr += "void"; break;
		case BfTypeCode_CharPtr: outStr += "char8*"; break;
		case BfTypeCode_Pointer: outStr += "*"; break;
		case BfTypeCode_NullPtr: outStr += ""; break;
		case BfTypeCode_Var: outStr += "var"; break;
		case BfTypeCode_Let: outStr += "let"; break;
		case BfTypeCode_Boolean: outStr += "bool"; break;
		case BfTypeCode_Int8: outStr += "int8"; break;
		case BfTypeCode_UInt8: outStr += "uint8"; break;
		case BfTypeCode_Int16: outStr += "int16"; break;
		case BfTypeCode_UInt16: outStr += "uint16"; break;
		case BfTypeCode_Int32: outStr += "int32"; break;
		case BfTypeCode_UInt32: outStr += "uint32"; break;
		case BfTypeCode_Int64: outStr += "int64"; break;
		case BfTypeCode_UInt64: outStr += "uint64"; break;
		case BfTypeCode_IntPtr: outStr += "int"; break;
		case BfTypeCode_UIntPtr: outStr += "uint"; break;
		case BfTypeCode_IntUnknown: outStr += "int unknown"; break;
		case BfTypeCode_UIntUnknown: outStr += "uint unknown"; break;
		case BfTypeCode_Char8: outStr += "char8"; break;
		case BfTypeCode_Char16: outStr += "char16"; break;
		case BfTypeCode_Char32: outStr += "char32"; break;
		case BfTypeCode_Float: outStr += "float"; break;
		case BfTypeCode_Double: outStr += "double"; break;
		}
	}
}

CPUStackFrame* DbgExprEvaluator::GetStackFrame()
{
	if (mIsEmptyTarget)
		return NULL;

	static CPUStackFrame emptyStackFrame;
	if (mCallStackIdx == -1)
		return &emptyStackFrame;

	if (mDebugger->mRunState == RunState_NotStarted)
		return &emptyStackFrame;

	if (mDebugger->mCallStack.size() == 0)
		mDebugger->UpdateCallStack();
	if (mCallStackIdx >= (int)mDebugger->mCallStack.size())
		return &emptyStackFrame;

	return mDebugger->mCallStack[mCallStackIdx];
}

CPURegisters* DbgExprEvaluator::GetRegisters()
{
	if (mIsEmptyTarget)
		return NULL;
	auto stackFrame = GetStackFrame();
	if (stackFrame != NULL)
		return &stackFrame->mRegisters;
	return NULL;
}

DbgTypedValue DbgExprEvaluator::GetRegister(const StringImpl& regName)
{
	if (mIsEmptyTarget)
		return DbgTypedValue();
	if (!mDebugger->mIsRunning)
		return DbgTypedValue();

	Array<RegForm>* regForms = NULL;
	if (mCallStackIdx != -1)
	{
		if (mCallStackIdx < (int)mDebugger->mCallStack.size())
			regForms = &mDebugger->mCallStack[mCallStackIdx]->mRegForms;
	}
	DbgTypedValue result = mDebugger->GetRegister(regName, GetLanguage(), GetRegisters(), regForms);
	if ((result) && (mReferenceId != NULL))
	{
		int regNum = CPURegisters::GetCompositeRegister(result.mRegNum);
		const char* regName = CPURegisters::GetRegisterName(regNum);
		if (regName != NULL)
			*mReferenceId = String("$") + regName;
	}
	return result;
}

DbgSubprogram* DbgExprEvaluator::GetCurrentMethod()
{
	if (mIsEmptyTarget)
		return NULL;

	if (!mDebugger->mIsRunning)
		return NULL;

	if (mCallStackIdx == -1)
		return NULL;

	mDebugger->UpdateCallStackMethod(mCallStackIdx);
	if (mCallStackIdx >= (int)mDebugger->mCallStack.size())
		return NULL;
	auto callStack = mDebugger->mCallStack[mCallStackIdx];
	auto subProgram = callStack->mSubProgram;
	if (subProgram != NULL)
		subProgram->PopulateSubprogram();
	return subProgram;
}

DbgType* DbgExprEvaluator::GetCurrentType()
{
	auto curMethod = GetCurrentMethod();
	if (curMethod != NULL)
		return curMethod->mParentType;
	return NULL;
}

DbgTypedValue DbgExprEvaluator::GetThis()
{
	if (mExplicitThis)
		return mExplicitThis;

	String findName = "this";
	CPUStackFrame* stackFrame = GetStackFrame();
	if (stackFrame != NULL)
	{
		intptr valAddr;
		DbgType* valType;
		DbgAddrType addrType = DbgAddrType_Value;
		if (mDebugTarget->GetValueByName(GetCurrentMethod(), findName, stackFrame, &valAddr, &valType, &addrType))
		{
			//valType = valType->RemoveModifiers();
			return FixThis(ReadTypedValue(NULL, valType, valAddr, addrType));
		}

		if (mDebugTarget->GetValueByName(GetCurrentMethod(), "__closure", stackFrame, &valAddr, &valType, &addrType))
		{
			DbgTypedValue result = ReadTypedValue(NULL, valType, valAddr, addrType);
			if (!result)
				return result;
			SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
			return FixThis(LookupField(NULL, result, "__this"));
		}
	}
	return DbgTypedValue();
}

DbgTypedValue DbgExprEvaluator::GetDefaultTypedValue(DbgType* srcType)
{
	DbgTypedValue typedValue;
	typedValue.mType = srcType;
	return typedValue;
}

String DbgExprEvaluator::TypeToString(DbgType* type)
{
	return type->ToString();
}

bool DbgExprEvaluator::CheckHasValue(DbgTypedValue typedValue, BfAstNode* refNode)
{
	if ((!typedValue) && (typedValue.mType != NULL))
	{
		mResult = DbgTypedValue();
		Fail("Value required", refNode);
		return false;
	}

	return true;
}

//TODO: Expand this
bool DbgExprEvaluator::CanCast(DbgTypedValue typedVal, DbgType* toType, BfCastFlags castFlags)
{
	DbgType* fromType = typedVal.mType;

	if (fromType == toType)
		return true;

	if ((fromType->IsConst()) && (!toType->IsConst()))
		return false;

	fromType = fromType->RemoveModifiers();
	toType = toType->RemoveModifiers();

	// Ptr -> const Ptr
	if ((fromType->IsPointer()) && (toType->IsPointer()))
	{
		if (toType->mTypeParam->IsConst())
		{
			auto primaryFrom = fromType->mTypeParam->GetPrimaryType();
			auto primaryTo = toType->mTypeParam->mTypeParam->GetPrimaryType();
			if ((primaryFrom->IsCompositeType()) || (primaryTo->IsCompositeType()))
			{
				if (primaryFrom == primaryTo)
					return true;
			}
			else if (primaryFrom->IsPrimitiveType())
			{
				return primaryFrom->mTypeCode == primaryTo->mTypeCode;
			}
		}
	}

	if ((fromType->IsCompositeType()) && ((toType->IsCompositeType() || (toType->IsInterface()))))
	{
		bool allowCast = false;

		//TODO: Allow explicit interface casts on any object
		//TODO: Insert cast checking code

		int thisOffset = 0;
		addr_target thisVal = typedVal.mSrcAddress;
		if (TypeIsSubTypeOf(fromType, toType, &thisOffset, &thisVal))
		{
			return true;
		}
	}

	if ((fromType->IsPrimitiveType()) && (toType->IsPrimitiveType()))
	{
		DbgTypeCode fromTypeCode = fromType->mTypeCode;
		DbgTypeCode toTypeCode = toType->mTypeCode;
		if ((fromTypeCode == toTypeCode))
			return true;

		// Must be from a default int to do an implicit constant cast, not casted by user, ie: (ushort)123
		if ((toType->IsInteger()) /*&& (typedVal.mValue != NULL) && (fromTypeCode == DbgType_i32)*/)
		{
			// Allow constant ints to be implicitly downcasted if they fit
			if (typedVal.mIsLiteral)
			{
				int64 srcVal = typedVal.GetInt64();
				if (toType->IsSigned())
				{
					int64 minVal = -(1LL << (8 * toType->mSize - 1));
					int64 maxVal = (1LL << (8 * toType->mSize - 1)) - 1;
					if ((srcVal >= minVal) && (srcVal <= maxVal))
						return true;
				}
				else if (toType->mSize == 8) // ulong
				{
					if (srcVal > 0)
						return true;
				}
				else
				{
					int64 minVal = 0;
					int64 maxVal = (1LL << (8 * toType->mSize)) - 1;
					if ((srcVal >= minVal) && (srcVal <= maxVal))
						return true;
				}
			}
		}

		switch (toTypeCode)
		{
		case DbgType_u8:
		case DbgType_UChar:
			switch (fromTypeCode)
			{
			case DbgType_UChar:
			case DbgType_u8:
				return true;
			}
			break;

		case DbgType_SChar16:
		case DbgType_i16:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_SChar:
			case DbgType_SChar16:
				return true;
			case DbgType_u8:
			case DbgType_UChar:
				return true;
			}
			break;
		case DbgType_u16:
			switch (fromTypeCode)
			{
			case DbgType_i8:
				return true;
			case DbgType_u8:
			case DbgType_UChar:
			case DbgType_UChar16:
				return true;
			}
			break;
		case DbgType_SChar32:
		case DbgType_i32:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
				return true;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_UChar:
			case DbgType_UChar16:
				return true;
			}
			break;
		case DbgType_UChar32:
		case DbgType_u32:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_SChar:
			case DbgType_SChar16:
				return true;

			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				return true;
			}
			break;
		case DbgType_i64:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_i64:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return true;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				return true;
			}
			break;
		case DbgType_u64:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return true;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar32:
				return true;
			}
			break;
		case DbgType_Single:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
				return true;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar32:
				return true;
			}
			break;
		case DbgType_Double:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_i64:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return true;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				return true;
			case DbgType_Single:
				return true;
			}
			break;
		}
	}

	return false;
}

DbgTypedValue DbgExprEvaluator::Cast(BfAstNode* srcNode, const DbgTypedValue& typedVal, DbgType* toType, bool explicitCast, bool silentFail)
{
	if (!typedVal)
		return typedVal;

	DbgType* fromType = typedVal.mType;
	fromType = fromType->RemoveModifiers();
	toType = toType->RemoveModifiers();

	fromType = fromType->GetPrimaryType();
	toType = toType->GetPrimaryType();

	if ((toType->IsBfObject()) && (fromType->IsPointer()))
		toType = toType->GetDbgModule()->GetPointerType(toType);

	if (toType->IsPrimitiveType())
	{
		if (fromType->mTypeCode == toType->mTypeCode)
			return typedVal;

		if ((fromType->IsStruct()) || (fromType->IsEnum()))
		{
			if (fromType->mTypeParam != NULL)
			{
				DbgTypedValue underlyingTypedValue = typedVal;
				underlyingTypedValue.mType = fromType->mTypeParam;
				auto castedVal = Cast(srcNode, underlyingTypedValue, toType, explicitCast, true);
				if (castedVal)
					return castedVal;
			}
		}
	}

	if (fromType == toType)
		return typedVal;

	if ((fromType->mTypeCode == DbgType_Null) &&
		(toType->mTypeCode == DbgType_Ptr))
	{
		DbgTypedValue val;
		val.mPtr = typedVal.mPtr;
		val.mType = toType;
		return val;
	}

	auto curTypeInstance = GetCurrentType();

	if ((fromType->IsCompositeType()) && ((toType->IsCompositeType() || (toType->IsInterface()))))
	{
		bool allowCast = false;

		//TODO: Allow explicit interface casts on any object
		//TODO: Insert cast checking code

		int thisOffset = 0;
		addr_target thisVal = typedVal.mSrcAddress;
		if (TypeIsSubTypeOf(fromType, toType, &thisOffset, &thisVal))
		{
			allowCast = true;
		}
		else if ((explicitCast) && ((toType->IsInterface()) || (TypeIsSubTypeOf(toType, fromType, &thisOffset))))
		{
			thisOffset = -thisOffset;
			allowCast = true;
		}

		if (fromType->IsBfPayloadEnum())
		{
			if (!allowCast)
			{
				for (auto member : fromType->mMemberList)
				{
					if (member->mType->Equals(toType))
					{
						allowCast = true;
						break;
					}
				}
			}
		}

		if (allowCast)
		{
			DbgTypedValue val;
			//val.mPtr = typedVal.mPtr + thisOffset;
			val.mInt64 = typedVal.mInt64;
			val.mSrcAddress = thisVal + thisOffset;
			val.mType = toType;
			// Doing this allows us to modify a typed value that is tied to a register through its '[base]'
			if ((typedVal.mRegNum != -1) && (val.mType->GetByteCount() == typedVal.mType->GetByteCount()))
				val.mRegNum = typedVal.mRegNum;
			return val;
		}
	}

	// Payload discriminator
	if ((fromType->IsBfPayloadEnum()) && (toType->IsInteger()))
	{
		intptr valAddr;
		DbgType* valType;
		DbgAddrType addrType = DbgAddrType_Value;
		String findName = "$";
		//BF_ASSERT(typedVal.mVariable != NULL);
		if (typedVal.mVariable != NULL)
			findName += typedVal.mVariable->mName;
		findName += "$d";
		if (mDebugTarget->GetValueByName(GetCurrentMethod(), findName, GetStackFrame(), &valAddr, &valType, &addrType))
		{
			return ReadTypedValue(srcNode, valType, valAddr, addrType);
		}
	}

	// Optimized composite - probably stored in register
	if ((toType->IsCompositeType()) && (fromType->IsInteger()) && (explicitCast))
	{
		if (toType->GetByteCount() <= 8)
		{
			DbgTypedValue val;
			val.mType = toType;
			val.mInt64 = typedVal.mInt64;
			val.mSrcAddress = typedVal.mSrcAddress;
			return val;
		}
	}

	// Ptr -> const Ptr
	if ((fromType->IsPointer()) && (toType->IsPointer()))
	{
		if (toType->mTypeParam->IsConst())
		{
			bool canCast = false;
			auto primaryFrom = fromType->mTypeParam->GetPrimaryType();
			auto primaryTo = toType->mTypeParam->mTypeParam->GetPrimaryType();
			if ((primaryFrom->IsCompositeType()) || (primaryTo->IsCompositeType()))
			{
				if (primaryFrom == primaryTo)
					canCast = true;
			}
			else if (primaryFrom->IsPrimitiveType())
			{
				canCast = primaryFrom->mTypeCode == primaryTo->mTypeCode;
			}

			if (canCast)
			{
				DbgTypedValue val = typedVal;
				val.mType = toType;
				return val;
			}
		}
	}

	if ((fromType->IsPointer()) && (fromType->mTypeParam->IsCompositeType()) &&
		(toType->IsPointer()) && (toType->mTypeParam->IsCompositeType()))
	{
		auto fromInnerType = fromType->mTypeParam->RemoveModifiers();
		auto toInnerType = toType->mTypeParam->RemoveModifiers();

		/*DbgTypedValue fromVal;
		fromVal.mType = fromInnerTytpe;
		fromVal.mSrcAddress = typedVal.mPtr;

		DbgTypedValue toVal = Cast(srcNode, fromVal, toInnerType, explicitCast, silentFail);
		if (toVal)
		{
			// Back to pointer form
			toVal.mType = toType;
			toVal.mPtr = toVal.mSrcAddress;
			toVal.mSrcAddress = 0;
		}
		return toVal;*/

		bool allowCast = false;

		//TODO: Allow explicit interface casts on any object
		//TODO: Insert cast checking code

		int thisOffset = 0;
		addr_target thisVal = typedVal.mSrcAddress;
		if (TypeIsSubTypeOf(fromInnerType, toInnerType, &thisOffset, &thisVal))
		{
			allowCast = true;
		}
		else if ((explicitCast) && ((toInnerType->IsInterface()) || (TypeIsSubTypeOf(toInnerType, fromInnerType, &thisOffset))))
		{
			thisOffset = -thisOffset;
			allowCast = true;
		}

		if (allowCast)
		{
			DbgTypedValue toVal;
			toVal.mType = toType;
			toVal.mPtr = typedVal.mPtr + thisOffset;
			toVal.mSrcAddress = 0;
			return toVal;
		}
	}

	// IFace -> object|IFace
	if ((fromType->IsInterface()) ||
		((fromType->IsPointer()) && (fromType->mTypeParam->IsInterface())))
	{
		if (toType->IsBfObject())
		{
			DbgTypedValue val;
			val.mPtr = typedVal.GetPointer();
			val.mType = toType;
			return val;
		}
	}

	// Int|Ptr|Arr -> Int|Ptr|Arr
	if (((fromType->IsInteger()) || (fromType->IsPointer()) || (fromType->IsSizedArray())) &&
		((toType->IsInteger()) || (toType->IsPointer()) || (toType->IsSizedArray())))
	{
		bool allowCast = explicitCast;
		if (allowCast)
		{
			DbgTypedValue val;

			if (toType->IsSizedArray())
				val.mSrcAddress = typedVal.GetPointer();
			else
				val.mPtr = typedVal.GetPointer();
			val.mType = toType;
			return val;
		}
	}

	// Enum -> Int
	if ((((fromType->IsBfEnum()) || (fromType->IsEnum())) && (toType->IsInteger())) &&
		((explicitCast) || (fromType == curTypeInstance)))
	{
		DbgTypedValue result = typedVal;
		result.mType = toType;
		return result;
	}

	// Int -> Enum
	if (((fromType->IsInteger()) && ((toType->IsBfEnum()) || (toType->IsEnum()))) &&
		((explicitCast) || (toType == curTypeInstance)))
	{
		DbgTypedValue result = typedVal;
		result.mType = toType;
		return result;
	}

	// TypedPrimitive -> Primitive
	if ((fromType->IsTypedPrimitive()) && (toType->IsPrimitiveType()))
	{
		DbgTypedValue fromValue = typedVal;
		fromValue.mType = fromType->GetBaseType();
		return Cast(srcNode, fromValue, toType, explicitCast);
	}

	// TypedPrimitive -> TypedPrimitive
	if ((fromType->IsTypedPrimitive()) && (toType->IsTypedPrimitive()))
	{
		DbgTypedValue fromValue = typedVal;
		fromValue.mType = fromType->GetBaseType();
		DbgTypedValue primTypedVal = Cast(srcNode, fromValue, toType->GetBaseType(), explicitCast, silentFail);
		if (primTypedVal)
		{
			primTypedVal.mType = toType;
			return primTypedVal;
		}
	}

	// Primitive -> TypedPrimitive
	if ((fromType->IsPrimitiveType()) && (toType->IsTypedPrimitive()))
	{
		DbgTypedValue primTypedVal = Cast(srcNode, typedVal, toType->GetBaseType(), true);
		if (primTypedVal)
		{
			primTypedVal.mType = toType;
			return primTypedVal;
		}
	}

	if ((fromType->IsPrimitiveType()) && (toType->IsPrimitiveType()))
	{
		DbgTypeCode fromTypeCode = fromType->mTypeCode;
		DbgTypeCode toTypeCode = toType->mTypeCode;

		if ((toType->IsInteger()) && (fromType->IsIntegral()))
		{
			// Allow constant ints to be implicitly shrunk if they fit
			if (typedVal.mIsLiteral)
			{
				int64 srcVal = typedVal.GetInt64();
				/*if (toType->IsSigned())
				{
					int64 minVal = -(1LL << (8 * toType->mSize - 1));
					int64 maxVal = (1LL << (8 * toType->mSize - 1)) - 1;
					if ((srcVal >= minVal) && (srcVal <= maxVal))
						explicitCast = true;
				}
				else if (toType->mSize == 8) // ulong
				{
					if (srcVal > 0)
						explicitCast = true;
				}
				else
				{
					int64 minVal = 0;
					int64 maxVal = (1LL << (8 * toType->mSize)) - 1;
					if ((srcVal >= minVal) && (srcVal <= maxVal))
						explicitCast = true;
				}*/

				if (toType->IsChar())
				{
					if (srcVal == 0)
						explicitCast = true;
				}
				else if ((fromType->IsChar()) && (!toType->IsChar()))
				{
					// Never allow this
				}
				else if ((fromTypeCode == BfTypeCode_UInt64) && (srcVal < 0))
				{
					// There's nothing that this could fit into
				}
				else if (toType->IsSigned())
				{
					int64 minVal = -(1LL << (8 * toType->mSize - 1));
					int64 maxVal = (1LL << (8 * toType->mSize - 1)) - 1;
					if ((srcVal >= minVal) && (srcVal <= maxVal))
						explicitCast = true;
				}
				else if (toType->mSize == 8) // ulong
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
		}

		DbgTypedValue result;
		result.mType = toType;
		if (((fromType->IsInteger()) || (fromType->IsChar())) &&
			(toType->IsInteger()) || (toType->IsChar()))
		{
			result.mInt64 = typedVal.GetInt64();
			if (explicitCast)
				return result;
		}

		switch (toTypeCode)
		{
		case DbgType_u8:
		case DbgType_UChar:
			switch (fromTypeCode)
			{
			case DbgType_u8:
			case DbgType_UChar:
				return result;
			}
			break;

		case DbgType_i16:
		case DbgType_SChar16:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_SChar:
			case DbgType_SChar16:
				return result;
			case DbgType_u8:
			case DbgType_UChar:
				return result;
			}
			break;
		case DbgType_u16:
		case DbgType_UChar16:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_SChar:
				return result;
			case DbgType_u8:
			case DbgType_UChar:
				return result;
			}
			break;
		case DbgType_i32:
		case DbgType_SChar32:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_UChar:
			case DbgType_UChar16:
				return result;
			}
			break;
		case DbgType_UChar32:
		case DbgType_u32:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_SChar:
			case DbgType_SChar16:
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				return result;
			}
			break;
		case DbgType_i64:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				return result;
			}
			break;
		case DbgType_u64:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_UChar:
			case DbgType_UChar32:
				return result;
			}
			break;
		case DbgType_Single:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_i64:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				result.mSingle = typedVal.GetInt64();
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_u64:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				result.mSingle = typedVal.GetInt64();
				return result;
			}
			break;
		case DbgType_Double:
			switch (fromTypeCode)
			{
			case DbgType_i8:
			case DbgType_i16:
			case DbgType_i32:
			case DbgType_i64:
			case DbgType_SChar:
			case DbgType_SChar16:
			case DbgType_SChar32:
				result.mDouble = typedVal.GetInt64();
				return result;
			case DbgType_u8:
			case DbgType_u16:
			case DbgType_u32:
			case DbgType_u64:
			case DbgType_UChar:
			case DbgType_UChar16:
			case DbgType_UChar32:
				result.mDouble = typedVal.GetInt64();
				return result;
			case DbgType_Single:
				result.mDouble = typedVal.mSingle;
				return result;
			}
			break;
		}

		if (explicitCast)
		{
			if (toType->IsInteger())
			{
				if (typedVal.mType->IsInteger())
				{
					return result;
				}
				else if (typedVal.mType->IsFloat())
				{
					if (typedVal.mType->mTypeCode == DbgType_Single)
						result.mInt64 = typedVal.mSingle;
					else
						result.mInt64 = (int64)typedVal.mDouble;
					return result;
				}
			}

			switch (toTypeCode)
			{
			case DbgType_Single:
				switch (fromTypeCode)
				{
				case DbgType_i8:
				case DbgType_i16:
				case DbgType_i32:
				case DbgType_SChar:
				case DbgType_SChar16:
				case DbgType_SChar32:
					result.mSingle = (float)typedVal.GetInt64();
					return result;
				case DbgType_u8:
				case DbgType_u16:
				case DbgType_u32:
				case DbgType_UChar:
				case DbgType_UChar16:
				case DbgType_UChar32:
					result.mSingle = (float)typedVal.GetInt64();
					return result;
				case DbgType_Double:
					result.mSingle = (float)typedVal.mDouble;
					return result;
				}
				break;
			}
		}
	}

	if (fromType->mTypeCode == DbgType_TypeDef)
	{
		DbgTypedValue realTypedVal = typedVal;
		realTypedVal.mType = fromType->mTypeParam;
		return Cast(srcNode, realTypedVal, toType, explicitCast, silentFail);
	}

	if (toType->mTypeCode == DbgType_TypeDef)
		return Cast(srcNode, typedVal, toType->mTypeParam, explicitCast, silentFail);

	if (!silentFail)
	{
		auto language = GetLanguage();

		const char* errStr = explicitCast ?
			"Unable to cast '%s' to '%s'" :
			"Unable to implicitly cast '%s' to '%s'";
		Fail(StrFormat(errStr, fromType->ToString(language).c_str(), toType->ToString(language).c_str()), srcNode);
	}
	return DbgTypedValue();
}

bool DbgExprEvaluator::HasField(DbgType* curCheckType, const StringImpl& fieldName)
{
	curCheckType = curCheckType->RemoveModifiers();
	curCheckType = curCheckType->GetPrimaryType();
	curCheckType->PopulateType();

	for (auto checkMember : curCheckType->mMemberList)
	{
		if (checkMember->mName == NULL)
		{
			auto checkResult = HasField(checkMember->mType, fieldName);
			if (checkResult)
				return checkResult;
		}
		else if (checkMember->mName == fieldName)
			return true;
	}

	for (auto baseTypeEntry : curCheckType->mBaseTypes)
	{
		auto baseType = baseTypeEntry->mBaseType;
		auto result = HasField(baseType, fieldName);
		if (result)
			return result;
	}

	//if (wantsStatic)
	{
		// Look for statics in anonymous inner classes
		for (auto checkSubType : curCheckType->mSubTypeList)
		{
			if (checkSubType->mTypeName == NULL)
			{
				auto result = HasField(checkSubType, fieldName);
				if (result)
					return result;
			}
		}

		for (auto altType : curCheckType->mAlternates)
		{
			auto result = HasField(altType, fieldName);
			if (result)
				return result;
		}
	}

	return false;
}

DbgTypedValue DbgExprEvaluator::DoLookupField(BfAstNode* targetSrc, DbgTypedValue target, DbgType* curCheckType, const StringImpl& fieldName, CPUStackFrame* stackFrame, bool allowImplicitThis)
{
	if (mStackSearch != NULL)
	{
		if (mStackSearch->mIdentifier == targetSrc)
		{
			if (!mStackSearch->mSearchedTypes.Add(curCheckType))
				return DbgTypedValue();
		}
	}

	bool wantsStatic = (!target) || (target.mHasNoValue);
	auto flavor = GetFlavor();
	auto language = GetLanguage();
	if (language != DbgLanguage_Beef)
	{
		// C allows static lookups by non-static member reference
		wantsStatic = true;
	}

	curCheckType = curCheckType->RemoveModifiers();
	curCheckType = curCheckType->GetPrimaryType();
	curCheckType->PopulateType();

	//BfLogDbgExpr("DoLookupField %s %s Priority=%d\n", fieldName.c_str(), curCheckType->mName, curCheckType->mPriority);

	if ((curCheckType->mNeedsGlobalsPopulated) && ((curCheckType->IsNamespace()) || (curCheckType->IsRoot())))
	{
		// Global variables don't show up in any type declaration like static fields do, so we need to populate here
		mDbgModule->PopulateTypeGlobals(curCheckType);
	}

	/*auto checkMember = curCheckType->mMemberList.mHead;
	while (checkMember != NULL)*/

	String findFieldName = fieldName;
	if ((language == DbgLanguage_Beef) && (flavor == DbgFlavor_MS) && (target.mHasNoValue))
	{
		//if ((curCheckType->IsRoot()) || (curCheckType->IsNamespace()))
			//findFieldName.insert(0, "bf__");
	}

	//for (auto checkMember : curCheckType->mMemberList)
	auto nextMember = curCheckType->mMemberList.mHead;
	while (nextMember != NULL)
	{
		auto checkMember = nextMember;
		nextMember = checkMember->mNext;

		if (checkMember->mName == NULL)
		{
			//TODO: Check inside anonymous type
			DbgTypedValue innerTarget;

			addr_target targetPtr = target.mSrcAddress;
			if ((target.mType != NULL) && (target.mType->HasPointer()))
				targetPtr = target.mPtr;
			innerTarget.mSrcAddress = targetPtr + checkMember->mMemberOffset;
			/*if (target.mPtr != 0)
				innerTarget.mPtr = target.mPtr + checkMember->mMemberOffset;*/
			innerTarget.mType = checkMember->mType;

			auto checkResult = LookupField(targetSrc, innerTarget, fieldName);
			if (checkResult)
				return checkResult;
		}
		else if (checkMember->mName == findFieldName)
		{
			//BfLogDbgExpr(" Got Match\n");

			//TODO:
			/*if (field->mIsConst)
			{
			if (fieldInstance->mStaticValue == NULL)
			mModule->ResolveConstField(curCheckType, field);
			return DbgTypedValue(fieldInstance->mStaticValue, fieldInstance->mType);
			}*/

			//checkMember->mMemberOffset

			if (mReferenceId != NULL)
			{
				if (curCheckType->IsRoot())
					*mReferenceId = fieldName;
				else if (curCheckType->IsEnum())
					*mReferenceId = curCheckType->ToString();
				else
					*mReferenceId = curCheckType->ToString() + "." + fieldName;
			}

			if ((checkMember->mLocationLen == 0) && (checkMember->mIsStatic) && (!checkMember->mIsConst))
			{
				if (checkMember->mCompileUnit->mDbgModule->mDbgFlavor == DbgFlavor_MS)
				{
					if (curCheckType->mNeedsGlobalsPopulated)
					{
						mDbgModule->PopulateTypeGlobals(curCheckType);
						return DoLookupField(targetSrc, target, curCheckType, fieldName, stackFrame, allowImplicitThis);
					}

					/*String memberName = curCheckType->ToString() + "::" + checkMember->mName;
					mDbgModule->ParseSymbolData();
					auto entry = mDbgModule->mSymbolNameMap.Find(memberName.c_str());
					if (entry != NULL)
					{
						auto sym = entry->mValue;
						int locLen = 1 + sizeof(addr_target);
						uint8* locData = checkMember->mCompileUnit->mDbgModule->mAlloc.AllocBytes(locLen);
						locData[0] = DW_OP_addr_noRemap;
						*((addr_target*)(locData + 1)) = sym->mAddress;
						checkMember->mLocationData = locData;
						checkMember->mLocationLen = locLen;
					}
					else
					{
						BF_ASSERT("Static field not found" == 0);
					}*/
				}
				else
				{
					continue;

					//BF_FATAL("Unhandled");

					//TODO: Is this needed anymore since we have 'primary types'?
					/*auto altType = mDebugTarget->FindType(target.mType->ToString(true));
					if (altType != NULL)
					{
						auto altChildNode = altType->mAlternates.mHead;
						while ((altType != NULL) && (checkMember->mLocationLen == 0))
						{
							for (auto altMember : altType->mMemberList)
							{
								if (strcmp(altMember->mName, checkMember->mName))
								{
									if (altMember->mLocationData != NULL)
									{
										checkMember->mLinkName = altMember->mLinkName;
										checkMember->mLocationData = altMember->mLocationData;
										checkMember->mLocationLen = altMember->mLocationLen;
										break;
									}
								}
							}

							if (altChildNode != NULL)
							{
								altType = altChildNode->mValue;
								altChildNode = altChildNode->mNext;
							}
							else
								break;
						}
					}*/
				}
			}

			if (checkMember->mLocationLen != 0)
			{
				DbgAddrType addrType;
				intptr valAddr = checkMember->mCompileUnit->mDbgModule->EvaluateLocation(NULL, checkMember->mLocationData, checkMember->mLocationLen, stackFrame, &addrType);
				if ((language == DbgLanguage_Beef) && (checkMember->mType->IsConst()) && (checkMember->mType->mTypeParam->IsSizedArray()))
				{
					// We need an extra deref
					valAddr = mDebugger->ReadMemory<addr_target>(valAddr);
				}

				if (checkMember->mIsConst)
					addrType = DbgAddrType_Value;
				return ReadTypedValue(targetSrc, checkMember->mType, valAddr, addrType);
			}
			else if (checkMember->mIsConst)
			{
				return ReadTypedValue(targetSrc, checkMember->mType, (uint64)&checkMember->mConstValue, DbgAddrType_Local);
			}
			else if (checkMember->mIsStatic)
			{
				if (curCheckType->mNeedsGlobalsPopulated)
				{
					mDbgModule->PopulateTypeGlobals(curCheckType);
					nextMember = curCheckType->mMemberList.mHead;
					continue;
				}

				Fail("Static variable not found, may have be optimized out", targetSrc);
				//BfLogDbgExpr(" Static variable optimized out\n");
			}
			else
			{
				if ((allowImplicitThis) && (target.mPtr == 0))
					target = GetThis();

				if (!target)
				{
					Fail("Cannot reference a non-static member from a static method", targetSrc);
					return DbgTypedValue();
				}
				addr_target targetPtr = target.mSrcAddress;

				DbgTypedValue typedValue;
				if (targetPtr == -1)
				{
					LookupSplatMember(target, fieldName);
					typedValue = mResult;
					mResult = DbgTypedValue();
				}
				else if ((targetPtr == 0) && (target.mPtr != 0) && (!target.mType->HasPointer()))
				{
					typedValue = ReadTypedValue(targetSrc, checkMember->mType, (uint64)&target.mPtr + checkMember->mMemberOffset, DbgAddrType_Local);
				}
				else
				{
					if (target.mType->HasPointer())
						targetPtr = target.mPtr;

					typedValue = ReadTypedValue(targetSrc, checkMember->mType, targetPtr + checkMember->mMemberOffset, DbgAddrType_Target);
				}

				if (checkMember->mBitSize != 0)
				{
					int expectedShift = (checkMember->mType->mSize * 8) - checkMember->mBitSize;
					int shift = expectedShift - checkMember->mBitOffset;

					typedValue.mUInt64 = typedValue.mUInt64 >> shift;
					uint64 mask = 0;
					for (int i = 0; i < checkMember->mBitSize; i++)
						mask |= (1ULL << i);
					typedValue.mUInt64 &= mask;

					if ((checkMember->mType->IsSigned()) && ((typedValue.mUInt64 & (1LL << (checkMember->mBitSize - 1))) != 0))
					{
						// Sign extend
						typedValue.mUInt64 |= ~mask;
					}
				}

				return typedValue;
			}
		}

		//checkMember = checkMember->mNext;
	}

	BF_ASSERT((mPropGet == NULL) && (mPropSet == NULL));

	if (curCheckType->mMethodList.IsEmpty())
	{
		bool hasProps = false;
		for (auto methodNameEntry : curCheckType->mMethodNameList)
		{
			const char* methodName = methodNameEntry->mName;
			if (((methodName[0] == 'g') || (methodName[0] == 's')) &&
				((strncmp(methodName, "get__", 5) == 0) ||
				(strncmp(methodName, "set__", 5) == 0)))
			{
				if (strcmp(methodName + 5, fieldName.c_str()) == 0)
				{
					curCheckType->mCompileUnit->mDbgModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
					methodNameEntry->mCompileUnitId = -1;
				}
			}
		}
	}

	for (int pass = 0; pass < 2; pass++)
	{
		for (auto method : curCheckType->mMethodList)
		{
			if (method->mName != NULL)
			{
				if (((method->mName[0] == 'g') || (method->mName[0] == 's')) &&
					((strncmp(method->mName, "get__", 5) == 0) ||
					(strncmp(method->mName, "set__", 5) == 0)))
				{
					if (strcmp(method->mName + 5, fieldName.c_str()) == 0)
					{
						bool isGetter = method->mName[0] == 'g';

						method->PopulateSubprogram();

						if (fieldName.IsEmpty())
						{
							int expectedParams = (int)mIndexerValues.size();
							if (!isGetter)
								expectedParams++;
							if (method->GetParamCount() != expectedParams)
								continue;
						}

						DbgSubprogram*& subprogramRef = isGetter ? mPropGet : mPropSet;

						if ((subprogramRef == NULL) ||
							((!method->mHasThis) && (wantsStatic)) ||
							((method->mHasThis) && (!wantsStatic || allowImplicitThis)))
						{
							if ((method->mHasThis) && (allowImplicitThis) && (!target))
							{
								target = GetThis();
							}

							if (subprogramRef == NULL)
								subprogramRef = method;
							else if ((method->mVTableLoc != -1) || (subprogramRef->mVTableLoc == -1))
								subprogramRef = method;
						}
					}
				}
			}
		}

		bool loadedCompileUnit = false;
		for (int methodIdx = 0; methodIdx < 2; methodIdx++)
		{
			auto method = (methodIdx == 0) ? mPropGet : mPropSet;
			if (method == NULL)
				continue;

			if (method->mBlock.mLowPC == 0)
			{
				if (!curCheckType->mHasGlobalsPopulated)
					mDbgModule->PopulateTypeGlobals(curCheckType);
				for (auto methodNameEntry : curCheckType->mMethodNameList)
				{
					const char* methodName = methodNameEntry->mName;
					if ((methodNameEntry->mCompileUnitId >= 0) && (strcmp(methodName, method->mName) == 0))
					{
						curCheckType->mCompileUnit->mDbgModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
						loadedCompileUnit = true;
					}
				}
			}
		}

		if (!loadedCompileUnit)
			break;
	}

	// Found a result
	if ((mPropGet != NULL) || (mPropSet != NULL))
	{
		mPropSrc = targetSrc;
		mPropTarget = target;
		return DbgTypedValue();
	}

	//TODO:
	/*for (auto prop : curCheckType->mTypeDef->mProperties)
	{
	if (prop->mName == fieldName)
	{
	mModule->Fail("Cannot reference property");
	return DbgTypedValue();
	}
	}*/

	//curCheckType = curCheckType->GetBaseType();

	for (auto baseTypeEntry : curCheckType->mBaseTypes)
	{
		auto baseType = baseTypeEntry->mBaseType;

		DbgTypedValue baseTarget = target;
		addr_target* ptrRef = NULL;
		if ((baseTarget.mPtr != 0) && (target.mType->HasPointer()))
			ptrRef = &baseTarget.mPtr;
		else if (baseTarget.mSrcAddress != 0)
			ptrRef = &baseTarget.mSrcAddress;

		if (ptrRef != NULL)
		{
			if (baseTypeEntry->mVTableOffset != -1)
			{
				addr_target vtableAddr = mDebugger->ReadMemory<addr_target>(target.mPtr);
				int32 virtThisOffset = mDebugger->ReadMemory<int32>(vtableAddr + baseTypeEntry->mVTableOffset * sizeof(int32));
				*ptrRef += virtThisOffset;
			}
			else
				*ptrRef += baseTypeEntry->mThisOffset;
		}

		//BF_ASSERT(baseTypeEntry->mVTableOffset == -1);
		auto result = DoLookupField(targetSrc, baseTarget, baseType, fieldName, stackFrame, allowImplicitThis);
		if (result)
			return result;
	}

	if (wantsStatic)
	{
		// Look for statics in anonymous inner classes
		for (auto checkSubType : curCheckType->mSubTypeList)
		{
			bool includeSubType = ((checkSubType->mTypeName == NULL) || (checkSubType->IsGlobalsContainer()));
			if ((language == DbgLanguage_C) && (checkSubType->IsEnum()))
				includeSubType = true;
			if (includeSubType)
			{
				if ((checkSubType->IsGlobalsContainer()) && (checkSubType->mPriority <= DbgTypePriority_Normal))
					continue;
				auto rawCheckSubType = checkSubType->RemoveModifiers();
				auto result = DoLookupField(targetSrc, DbgTypedValue(), rawCheckSubType, fieldName, stackFrame, false);
				if (result)
					return result;
			}
		}

		//Turn alternates back on!
		/*for (auto altType : curCheckType->mAlternates)
		{
			auto result = DoLookupField(targetSrc, DbgTypedValue(), altType, fieldName, stackFrame, false);
			if (result)
				return result;
		}*/
	}

	return DbgTypedValue();
}

DbgTypedValue DbgExprEvaluator::LookupField(BfAstNode* targetSrc, DbgTypedValue target, const StringImpl& fieldName)
{
	CPUStackFrame* stackFrame = GetStackFrame();

	DbgType* curCheckType = NULL;
	/*if (currentMethod != NULL)
		curCheckType = currentMethod->mParentType;*/
	DbgType* curMethodType = curCheckType;

	if (target)
	{
		if (target.mType->IsPrimitiveType())
		{
			curCheckType = mDbgModule->GetPrimitiveStructType(target.mType->mTypeCode);
			if (curCheckType == NULL)
				return DbgTypedValue();
		}
		else
		{
			curCheckType = target.mType;
		}
		BF_ASSERT(curCheckType != NULL);
	}
	else if (target.mType != NULL)
		curCheckType = target.mType;

	if (curCheckType != NULL)
	{
		curCheckType = curCheckType->RemoveModifiers();
		if (curCheckType->mTypeCode == DbgType_Ptr)
			curCheckType = curCheckType->mTypeParam;
	}

	bool allowImplicitThis = false;
	if (curCheckType == NULL)
	{
		auto currentMethod = GetCurrentMethod();
		if (currentMethod != NULL)
		{
			curCheckType = currentMethod->GetTargetType();
			allowImplicitThis = currentMethod->mHasThis;
		}
	}

	//SetAndRestoreValue<DbgType*> prevTypeInstance(curMethodType, curCheckType);

	if (curCheckType == NULL)
		return DbgTypedValue();

	return DoLookupField(targetSrc, target, curCheckType, fieldName, stackFrame, allowImplicitThis);
}

void DbgExprEvaluator::Visit(BfAstNode* node)
{
	Fail("Invalid debug expression", node);
}

DbgTypedValue DbgExprEvaluator::ReadTypedValue(BfAstNode* targetSrc, DbgType* dbgType, uint64 valAddr, DbgAddrType addrType)
{
	bool failedReadMemory = false;

	if (addrType == DbgAddrType_Alias)
	{
		String findName = (const char*)valAddr;

		if (targetSrc == NULL)
			return DbgTypedValue();

		auto source = targetSrc->GetSourceData();
		auto bfParser = source->ToParser();
		if (bfParser == NULL)
			return DbgTypedValue();

		auto identifierNode = source->mAlloc.Alloc<BfIdentifierNode>();
		int srcStart = bfParser->AllocChars(findName.length());
		memcpy((char*)source->mSrc + srcStart, findName.c_str(), findName.length());
		identifierNode->Init(srcStart, srcStart, srcStart + findName.length());

		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
		auto result = LookupIdentifier(identifierNode);
		if (result)
			return result;

		// Allow lookup in calling method if we are inlined (for mixin references)
		auto currentMethod = GetCurrentMethod();
		if ((currentMethod != NULL) && (currentMethod->mInlineeInfo != NULL))
		{
			SetAndRestoreValue<int> preCallstackIdx(mCallStackIdx);
			mCallStackIdx++;
			result = LookupIdentifier(identifierNode);
		}

		return result;
	}

	if (addrType == DbgAddrType_LocalSplat)
	{
		DbgTypedValue val;
		val.mVariable = (DbgVariable*)valAddr;
		val.mType = dbgType;
		val.mSrcAddress = -1;
		val.mIsReadOnly = true;
		return val;
	}

	if (addrType == DbgAddrType_TargetDeref)
	{
		DbgTypedValue result = ReadTypedValue(targetSrc, dbgType, valAddr, DbgAddrType_Target);
		if ((result.mType != NULL) && (result.mType->IsPointer()))
		{
			result.mType = result.mType->mTypeParam;
			result.mSrcAddress = result.mPtr;
		}
		return result;
	}

	if (addrType == DbgAddrType_NoValue)
	{
		mPassInstance->Fail(StrFormat("No value", valAddr));
		return DbgTypedValue();
	}
	else if (addrType == DbgAddrType_OptimizedOut)
	{
		mPassInstance->Fail(StrFormat("Optimized out", valAddr));
		return DbgTypedValue();
	}

	bool hadRef = false;
	DbgType* origDwType = dbgType;
	while (true)
	{
		if (dbgType->mTypeCode == DbgType_Const)
		{
			dbgType = mDbgModule->GetInnerTypeOrVoid(dbgType);
		}
		else if ((dbgType->mTypeCode == DbgType_TypeDef) || (dbgType->mTypeCode == DbgType_Volatile))
		{
			dbgType = mDbgModule->GetInnerTypeOrVoid(dbgType);
		}
		else if ((dbgType->mTypeCode == DbgType_Ref) || (dbgType->mTypeCode == DbgType_RValueReference))
		{
			hadRef = true;
			auto innerType = dbgType->mTypeParam;
			if ((!innerType->IsCompositeType()) || (addrType == DbgAddrType_Target))
			{
				if (addrType == DbgAddrType_Target)
					valAddr = mDebugger->ReadMemory<addr_target>(valAddr);
				if (addrType == DbgAddrType_Register)
				{
					auto registers = GetRegisters();
					valAddr = registers->mIntRegsArray[valAddr];
				}

				//valIsAddr = true;
				addrType = DbgAddrType_Target;
				//local = false;
			}
			else
			{
				//local = false;
				//valIsAddr = true;
				if (addrType == DbgAddrType_Register)
				{
					auto registers = GetRegisters();
					valAddr = registers->mIntRegsArray[valAddr];
				}
				addrType = DbgAddrType_Target;
			}
			dbgType = dbgType->mTypeParam;
		}
		else
			break;
	}

	dbgType = dbgType->GetPrimaryType();

	if (dbgType->GetByteCount() == 0)
	{
		DbgTypedValue result;
		result.mType = dbgType;
		return result;
	}

	if (dbgType->mTypeCode == DbgType_Bitfield)
	{
		DbgType* underlyingType = dbgType->mTypeParam;
		DbgTypedValue result = ReadTypedValue(targetSrc, dbgType->mTypeParam, valAddr, addrType);
		result.mType = dbgType;

		auto dbgBitfieldType = (DbgBitfieldType*)dbgType;

		result.mUInt64 = result.mUInt64 >> dbgBitfieldType->mPosition;

		uint64 mask = ((uint64)1 << dbgBitfieldType->mLength) - 1;
		result.mUInt64 &= mask;

		if ((underlyingType->IsSigned()) && ((result.mUInt64 & (1LL << (dbgBitfieldType->mLength - 1))) != 0))
		{
			// Sign extend
			result.mUInt64 |= ~mask;
		}

		return result;
	}

	bool local = addrType == DbgAddrType_Local;
	bool valIsAddr = (addrType == DbgAddrType_Local) || (addrType == DbgAddrType_Target);

	DbgTypedValue result;
	result.mType = origDwType;

	if (addrType == DbgAddrType_Register)
	{
		result.mRegNum = (int)valAddr;

		auto registers = GetRegisters();
		if (result.mRegNum < CPURegisters::kNumIntRegs)
			result.mUInt64 = registers->mIntRegsArray[result.mRegNum];
		else if ((result.mRegNum >= CPUReg_XMMREG_FIRST) && (result.mRegNum <= CPUReg_XMMREG_LAST))
		{
			auto dwType = origDwType->RemoveModifiers();
			if (dwType->IsTypedPrimitive())
				dwType = dwType->GetUnderlyingType();
			if (dwType->mTypeCode == DbgType_Single)
				result.mSingle = *(float*)((float*)registers->mXmmRegsArray + (result.mRegNum - CPUReg_XMMREG_FIRST));
			else if (dwType->mTypeCode == DbgType_Double)
				result.mDouble = *(double*)((float*)registers->mXmmRegsArray + (result.mRegNum - CPUReg_XMMREG_FIRST));
			else
				BF_ASSERT("Not implemented" == 0);
		}

		auto primaryType = result.mType->GetPrimaryType();
		result.mType = primaryType;

		// In release builds, VC will place int-sized structs into a single register.  This code was breaking that.
		/*if ((primaryType->IsCompositeType()) && (!primaryType->IsTypedPrimitive()))
		{
			result.mSrcAddress = result.mPtr;
			result.mPtr = 0;
		}*/

		return result;
	}

	if (dbgType->mTypeCode == DbgType_Bitfield)
		dbgType = dbgType->mTypeParam;

	switch (dbgType->mTypeCode)
	{
	case DbgType_Void:
		break;

	case DbgType_Bool:
	case DbgType_i8:
	case DbgType_u8:
	case DbgType_SChar:
	case DbgType_UChar:
		if (valIsAddr)
			result.mInt8 = mDebugger->ReadMemory<int8>(valAddr, local, &failedReadMemory);
		else
			result.mUInt8 = (uint8)valAddr;
		break;
	case DbgType_i16:
	case DbgType_u16:
	case DbgType_SChar16:
	case DbgType_UChar16:
		if (valIsAddr)
			result.mInt16 = mDebugger->ReadMemory<int16>(valAddr, local, &failedReadMemory);
		else
			result.mUInt16 = (uint16)valAddr;
		break;
	case DbgType_i32:
	case DbgType_u32:
	case DbgType_SChar32:
	case DbgType_UChar32:
		if (valIsAddr)
			result.mInt32 = mDebugger->ReadMemory<int32>(valAddr, local, &failedReadMemory);
		else
			result.mUInt32 = (uint32)valAddr;
		break;
	case DbgType_i64:
	case DbgType_u64:
		if (valIsAddr)
			result.mInt64 = mDebugger->ReadMemory<int64>(valAddr, local, &failedReadMemory);
		else
			result.mUInt64 = (uint64)valAddr;
		break;

	case DbgType_Single:
		result.mSingle = mDebugger->ReadMemory<float>(valAddr, local, &failedReadMemory);
		break;
	case DbgType_Double:
		result.mDouble = mDebugger->ReadMemory<double>(valAddr, local, &failedReadMemory);
		break;

	case DbgType_Struct:
	case DbgType_Class:
	case DbgType_Union:
	case DbgType_SizedArray:
		if (((dbgType->IsTypedPrimitive()) || (local)) && (dbgType->GetByteCount() <= 8))
		{
			mDebugger->ReadMemory(valAddr, dbgType->GetByteCount(), &result.mInt64, local);
			result.mSrcAddress = (addr_target)valAddr;
		}
		else
		{
			BF_ASSERT(!local);
			if (hadRef)
				result.mPtr = (addr_target)valAddr;
			else
				result.mSrcAddress = (addr_target)valAddr;
		}
			//result.mPtr = (addr_target)valAddr;
		break;

	//TODO: Why was it treated as a pointer
	//case DbgType_SizedArray:
	case DbgType_Ptr:
		if (valIsAddr)
			result.mPtr = mDebugger->ReadMemory<addr_target>(valAddr, local, &failedReadMemory);
		else
			result.mPtr = valAddr;
		break;

	case DbgType_Enum:
		result = ReadTypedValue(targetSrc, dbgType->mTypeParam, valAddr, addrType);
		if (result)
			result.mType = dbgType;
		break;

	case DbgType_Subroutine:
		{
			auto funcPtr = result.mPtr;

			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			if (mDebugTarget->FindSymbolAt(funcPtr, &symbolName, &offset, &dwarf))
			{
#ifdef BF_DBG_32
				if ((symbolName.length() > 0) && (symbolName[0] == '_'))
					symbolName = symbolName.Substring(1);
#endif
				static String demangledName;
				demangledName = BfDemangler::Demangle(symbolName, dbgType->GetLanguage());

				DbgTypedValue result;
				result.mCharPtr = demangledName.c_str();
			}
			else
			{
				result.mCharPtr = NULL;
			}
		}
		break;

	default:
		if (mPassInstance != NULL)
			mPassInstance->Fail("Invalid data type");
	}

	if ((!local) && (valIsAddr))
	{
		result.mSrcAddress = valAddr;
	}

	if ((failedReadMemory) && (!mValidateOnly))
	{
		if (mPassInstance == NULL)
			return DbgTypedValue();

		if (addrType == DbgAddrType_NoValue)
			mPassInstance->Fail("No value");
		else if ((valAddr == 0) && (local))
			mPassInstance->Fail("Optimized out");
		else if (!mBlockedSideEffects)
			mPassInstance->Fail(StrFormat("Failed to read from address %s", EncodeDataPtr(valAddr, true).c_str()));
	}

	return result;
}

bool DbgExprEvaluator::CheckTupleCreation(addr_target receiveAddr, BfAstNode* targetSrc, DbgType* tupleType, const BfSizedArray<BfExpression*>& argValues, BfSizedArray<BfTupleNameNode*>* names)
{
	int memberIdx = 0;

	tupleType = tupleType->RemoveModifiers();
	tupleType = tupleType->GetPrimaryType();

	for (auto member : tupleType->mMemberList)
	{
		if (member->mIsStatic)
			continue;

		DbgTypedValue receivingValue;
		receivingValue.mSrcAddress = receiveAddr + member->mMemberOffset;
		receivingValue.mType = member->mType;

		if (memberIdx < argValues.size())
		{
			if ((names != NULL) && (memberIdx < names->size()) && ((*names)[memberIdx] != NULL))
			{
				String name;
				if (auto tupleName = BfNodeDynCast<BfTupleNameNode>((*names)[memberIdx]))
					name = tupleName->mNameNode->ToString();
				else
					name = (*names)[memberIdx]->ToString();
				if ((member->mName != NULL) && (!isdigit(member->mName[0])) && (name != member->mName))
				{
					Fail(StrFormat("Name '%s' does not match expect name '%s'", name.c_str(), member->mName), (*names)[memberIdx]);
				}
			}

			StoreValue(receivingValue, argValues[memberIdx]);
		}
		else
		{
			Fail("Not enough arguments", targetSrc);
		}

		memberIdx++;
	}

	if (memberIdx < argValues.size())
	{
		Fail("Too many arguments", argValues[memberIdx]);
	}

	return false;
}

DbgTypedValue DbgExprEvaluator::CheckEnumCreation(BfAstNode* targetSrc, DbgType* enumType, const StringImpl& caseName, const BfSizedArray<BfExpression*>& argValues)
{
	BF_ASSERT(enumType->IsBfPayloadEnum());

	addr_target receiveAddr = 0;
	if (mReceivingValue != NULL)
	{
		receiveAddr = mReceivingValue->mSrcAddress;
	}

	if (receiveAddr == 0)
	{
		Fail("Address for enum cannot be inferred", targetSrc);
		return DbgTypedValue();
	}

	enumType = enumType->RemoveModifiers();
	enumType = enumType->GetPrimaryType();

	int caseNum = -1;
	DbgVariable* matchedMember = NULL;
	for (auto member : enumType->mMemberList)
	{
		if ((member->mName[0] == '_') && (member->mName[1] >= '0') && (member->mName[1] <= '9'))
		{
			for (int i = 1; true; i++)
			{
				if (member->mName[i] == '_')
				{
					if (caseName == member->mName + i + 1)
					{
						caseNum = atoi(member->mName + 1);
						matchedMember = member;
					}
					break;
				}
				else if (member->mName[i] == 0)
					break;
			}
		}
		if (strcmp(member->mName, "__bftag") == 0)
		{
			if (mReceivingValue != NULL)
				mReceivingValue = NULL;

			mDebugger->WriteMemory(receiveAddr + member->mMemberOffset, &caseNum, member->mType->GetByteCount());
			CheckTupleCreation(receiveAddr + matchedMember->mMemberOffset, targetSrc, matchedMember->mType, argValues, NULL);

			DbgTypedValue typedValue;
			typedValue.mType = enumType;
			typedValue.mSrcAddress = receiveAddr;
			return typedValue;
		}
	}

	return DbgTypedValue();
}

bool DbgExprEvaluator::IsAutoCompleteNode(BfAstNode* node, int lengthAdd)
{
	if (node == NULL)
		return false;
	return ((mCursorPos >= node->GetSrcStart()) && (mCursorPos < node->GetSrcEnd() + lengthAdd));
}

void DbgExprEvaluator::AutocompleteCheckType(BfTypeReference* typeReference)
{
	if (!IsAutoCompleteNode(typeReference))
		return;

	if (auto namedTypeRef = BfNodeDynCast <BfNamedTypeReference>(typeReference))
	{
		String filter = namedTypeRef->ToString();
		mAutoComplete->mInsertStartIdx = namedTypeRef->GetSrcStart();
		mAutoComplete->mInsertEndIdx = namedTypeRef->GetSrcEnd();

		AutocompleteAddTopLevelTypes(filter);
	}
	else if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeReference))
	{
		if (IsAutoCompleteNode(qualifiedTypeRef->mLeft))
		{
			AutocompleteCheckType(qualifiedTypeRef->mLeft);
			return;
		}

		auto dbgType = ResolveTypeRef(qualifiedTypeRef->mLeft);
		if (dbgType != NULL)
		{
			String filter;
			if (qualifiedTypeRef->mRight != NULL)
			{
				filter = qualifiedTypeRef->mRight->ToString();
				mAutoComplete->mInsertStartIdx = qualifiedTypeRef->mRight->GetSrcStart();
				mAutoComplete->mInsertEndIdx = qualifiedTypeRef->mRight->GetSrcEnd();
			}
			else
			{
				mAutoComplete->mInsertStartIdx = qualifiedTypeRef->mDot->GetSrcEnd();
				mAutoComplete->mInsertEndIdx = qualifiedTypeRef->mDot->GetSrcEnd();
			}
			AutocompleteAddMembers(dbgType, false, false, filter);
		}
	}
	else if (auto elementedTypeRef = BfNodeDynCast<BfElementedTypeRef>(typeReference))
	{
		AutocompleteCheckType(elementedTypeRef->mElementType);
	}
}

void DbgExprEvaluator::AutocompleteAddTopLevelTypes(const StringImpl& filter)
{
	GetNamespaceSearch();
	for (auto usingNamespace : mNamespaceSearch)
	{
		usingNamespace = usingNamespace->GetPrimaryType();
		AutocompleteAddMembers(usingNamespace, true, false, filter);
	}

	mDbgModule->ParseGlobalsData();
	int methodEntryCount = 0;
	//for (auto compileUnit : mDbgModule->mCompileUnits)
	for (int compileIdx = 0; compileIdx < (int)mDbgModule->mCompileUnits.size(); compileIdx++)
	{
		auto compileUnit = mDbgModule->mCompileUnits[compileIdx];
		if (mDbgCompileUnit == NULL)
			continue;
		if ((compileUnit->mLanguage != DbgLanguage_Unknown) && (compileUnit->mLanguage != mDbgCompileUnit->mLanguage))
			continue;

		AutocompleteAddMembers(compileUnit->mGlobalType, true, false, filter);

		/*for (auto dwMethod : compileUnit->mGlobalType.mMethodList)
		{
		if (dwMethod->mName != NULL)
		{
		methodEntryCount++;
		mAutoComplete->AddEntry(AutoCompleteEntry("method", dwMethod->mName), filter);
		}
		}*/
	}

	auto language = GetLanguage();

	if (language == DbgLanguage_Beef)
	{
		for (auto primType : mDbgModule->mBfPrimitiveTypes)
		{
			if (primType != NULL)
				mAutoComplete->AddEntry(AutoCompleteEntry("valuetype", primType->mName), filter);
		}

		char* primNames[2] = { "int", "uint" };
		for (auto primName : primNames)
			mAutoComplete->AddEntry(AutoCompleteEntry("valuetype", primName), filter);
	}
	else
	{
		for (auto primType : mDbgModule->mCPrimitiveTypes)
		{
			if (primType != NULL)
				mAutoComplete->AddEntry(AutoCompleteEntry("valuetype", primType->mName), filter);
		}
	}

	int typeEntrCount = 0;

	//TODO: Does this get everything?
	/*for (auto dwTypeEntry : mDbgModule->mTypeMap)
	{
	auto dbgType = dwTypeEntry->mValue;
	if ((dbgType->mParent == NULL) && (dbgType->mTypeParam == NULL) &&
	(dbgType->mTypeCode != DbgType_Ptr) && (dbgType->mTypeCode != DbgType_Ref))
	{
	String typeStr = dbgType->ToString();
	if (typeStr[typeStr.length() - 1] == '^')
	continue;
	mAutoComplete->AddEntry(AutoCompleteEntry("type", typeStr), filter);
	typeEntrCount++;
	}
	}*/
}

DbgTypedValue DbgExprEvaluator::DoLookupIdentifier(BfAstNode* identifierNode, bool ignoreInitialError, bool* hadError)
{
	if (!mDebugger->mIsRunning)
		return DbgTypedValue();

	auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(identifierNode);
	if (qualifiedNameNode != NULL)
	{
		LookupQualifiedName(qualifiedNameNode, ignoreInitialError, hadError);
		auto qualifiedResult = mResult;
		mResult = DbgTypedValue();
		return qualifiedResult;
	}

	String findName = identifierNode->ToString();
	if ((findName.StartsWith('$')) && (findName != "$prim"))
	{
		if (IsAutoCompleteNode(identifierNode))
		{
			String filter = identifierNode->ToString();

			const char* identifiers[] =
			{
				"$ThreadId", "$ThreadName", "$TargetName", "$TargetPath", "$ModuleName", "$ModulePath",
				"$HitCount", "$ProcessId"
			};
			for (int idx = 0; idx < BF_ARRAY_COUNT(identifiers); idx++)
				mAutoComplete->AddEntry(AutoCompleteEntry("dbg", identifiers[idx]), filter);

			mAutoComplete->mInsertStartIdx = identifierNode->GetSrcStart();
			mAutoComplete->mInsertEndIdx = identifierNode->GetSrcEnd();
		}

		if (findName == "$this")
			return GetThis();
		else if (findName == "$ThreadId")
			return GetInt(mDebugger->mActiveThread->mThreadId);
		else if (findName == "$ThreadName")
			return GetString(mDebugger->mActiveThread->mName);
		else if (findName == "$TargetName")
			return GetString(GetFileName(mDebugTarget->mTargetPath));
		else if (findName == "LaunchName")
			return GetString(GetFileName(mDebugTarget->mLaunchBinary->mFilePath));
		else if (findName == "$TargetPath")
			return GetString(mDebugTarget->mTargetPath);
		else if (findName == "$ModuleName")
			return GetString(GetFileName(mDbgModule->mFilePath));
		else if (findName == "$ModulePath")
			return GetString(mDbgModule->mFilePath);
		else if (findName == "$HitCount")
		{
			if (mDebugger->mActiveBreakpoint != NULL)
				return GetInt(mDebugger->mActiveBreakpoint->GetHeadBreakpoint()->mHitCount);
			return GetInt(0);
		}
		else if (findName == "$BreakpointCondition")
		{
			if ((mDebugger->mActiveBreakpoint != NULL) && (mDebugger->mActiveBreakpoint->mCondition != NULL))
				return GetString(mDebugger->mActiveBreakpoint->GetHeadBreakpoint()->mCondition->mExpr);
			return GetString("");
		}
		else if (findName == "$ProcessId")
			return GetInt(mDebugger->mProcessInfo.dwProcessId);

		bool mayBeRegister = true;
		for (int i = 1; i < findName.length(); i++)
			if (findName[i] == '$')
				mayBeRegister = false;
		if (mayBeRegister)
		{
			DbgTypedValue val = GetRegister(findName.Substring(1));
			if (val)
				return val;
		}
	}

	DbgTypedValue result;
	if (mExplicitThis)
	{
		if ((mExplicitThis.mSrcAddress == -1) && ((DbgVariable*)mExplicitThis.mVariable != NULL))
		{
			mResult = DbgTypedValue();
			LookupSplatMember(mExplicitThis, findName);
			result = mResult;
			mResult = DbgTypedValue();
		}
		else if (!result)
		{
			mExplicitThis.mType = mExplicitThis.mType->RemoveModifiers();
			result = LookupField(identifierNode, mExplicitThis, findName);
		}

		if (result)
		{
			if (mExplicitThisExpr != NULL)
				mDeferredInsertExplicitThisVector.push_back(NodeReplaceRecord(identifierNode, mCurChildRef));
			return result;
		}
	}

	CPUStackFrame* stackFrame = GetStackFrame();
	auto language = GetLanguage();

	intptr valAddr;
	DbgType* valType;
	DbgAddrType addrType = DbgAddrType_None;

	if (IsAutoCompleteNode(identifierNode))
	{
		String filter = identifierNode->ToString();

		DbgType* dbgType = NULL;

		auto currentMethod = GetCurrentMethod();
		auto thisVal = GetThis();
		auto targetType = thisVal.mType;
		if (currentMethod != NULL)
		{
	        if (targetType != NULL)
				dbgType = targetType;
			else if (!thisVal)
				dbgType = currentMethod->GetTargetType();
		}

		Array<String> capturedNames;
		Array<DbgType*> capturedTypes;
		mDebugTarget->mCapturedNamesPtr = &capturedNames;
		mDebugTarget->mCapturedTypesPtr = &capturedTypes;
		{
			mDebugTarget->GetValueByName(GetCurrentMethod(), "*", stackFrame, &valAddr, &valType, &addrType);
		}
		mDebugTarget->mCapturedNamesPtr = NULL;
		mDebugTarget->mCapturedTypesPtr = NULL;
		auto language = GetLanguage();
		BF_ASSERT(capturedTypes.size() == capturedNames.size());
		{
			//for (auto capturedName : capturedNames)
			for (int i = 0; i < (int)capturedNames.size(); i++)
			{
				const String capturedName = capturedNames[i];
				if (language == DbgLanguage_Beef)
				{
					// Don't put splats
					if ((capturedName.length() > 0) && (capturedName[0] == '$'))
						continue;
				}
				//TODO: Show the type icon
				mAutoComplete->AddEntry(AutoCompleteEntry(GetTypeName(capturedTypes[i]), capturedNames[i]), filter);
			}
		}

		if (!mSubjectExpr.IsEmpty())
		{
			mAutoComplete->AddEntry(AutoCompleteEntry("valuetype", "_"), filter);
		}

		if (dbgType != NULL)
		{
			dbgType = dbgType->RemoveModifiers();
			if (dbgType->IsPointerOrRef())
			{
                dbgType = dbgType->mTypeParam;
				dbgType = dbgType->RemoveModifiers();
			}
			if (dbgType->mIsDeclaration)
				dbgType = dbgType->GetPrimaryType();

			bool wantsStatic = true;
			bool wantsNonStatic = currentMethod->mHasThis;
			// In Beef, we can only access statics by class name
			if (dbgType->GetLanguage() != DbgLanguage_Beef)
				wantsStatic = !wantsNonStatic;
			AutocompleteAddMembers(dbgType, wantsStatic, wantsNonStatic, filter);
			if (currentMethod != NULL)
			{
				if (language == DbgLanguage_C)
				{
					// C++ closures
					auto currentMethod = GetCurrentMethod();
					if (currentMethod != NULL)
					{
						if (strstr(currentMethod->mName, "operator()") != NULL)
						{
							if (mDebugTarget->GetValueByName(currentMethod, "this", stackFrame, &valAddr, &valType, &addrType))
							{
								valType = valType->RemoveModifiers();
								if (valType->IsPointer())
									valType = valType->mTypeParam;
								valType = valType->RemoveModifiers();
								AutocompleteAddMembers(valType, true, true, filter, true);
							}
						}
					}
				}
				else
				{
					// If in a Beef closure...
					if (mDebugTarget->GetValueByName(currentMethod, "__closure", stackFrame, &valAddr, &valType, &addrType))
					{
						valType = valType->RemoveModifiers();
						if (valType->IsPointer())
							valType = valType->mTypeParam;
						AutocompleteAddMembers(valType, true, true, filter, true);
					}
				}

				auto targetType = currentMethod->GetTargetType();
				if ((targetType != dbgType) && (targetType != NULL))
				{
					AutocompleteAddMembers(targetType, wantsStatic, wantsNonStatic, filter);
				}
			}
		}

		mAutoComplete->mInsertStartIdx = identifierNode->GetSrcStart();
		mAutoComplete->mInsertEndIdx = identifierNode->GetSrcEnd();

		AutocompleteAddTopLevelTypes(filter);
	}

	if (language == DbgLanguage_C)
	{
		// For C++ lambdas, captured a "this" is named "__this", so allow lookups into that
		auto currentMethod = GetCurrentMethod();
		if (currentMethod != NULL)
		{
			if (strstr(currentMethod->mName, "operator()") != NULL)
			{
				DbgTypedValue rootThisValue = GetThis();
				if (rootThisValue)
				{
					DbgTypedValue thisValue = LookupField(identifierNode, rootThisValue, "__this");
					if (thisValue)
					{
						if (findName == "this")
							return thisValue;

						auto fieldValue = LookupField(identifierNode, thisValue, findName);
						if (fieldValue)
							return fieldValue;
					}
				}
			}
		}
	}

	if (findName == "this")
		return GetThis();
	if (findName == "_")
	{
		if (mSubjectValue)
		{
			if (mSubjectValue.mSrcAddress != 0)
			{
				auto refreshVal = ReadTypedValue(identifierNode, mSubjectValue.mType, mSubjectValue.mSrcAddress, DbgAddrType_Target);
				if (refreshVal)
					mSubjectValue = refreshVal;
			}
			return mSubjectValue;
		}

		if (!mSubjectExpr.IsEmpty())
		{
			DwFormatInfo formatInfo;
			formatInfo.mLanguage = language;
			DbgEvaluationContext dbgEvaluationContext(mDebugger, mDbgModule, mSubjectExpr, &formatInfo);
			mSubjectValue = dbgEvaluationContext.EvaluateInContext(mExplicitThis);
			if (!mSubjectValue)
			{
				Fail("Failed to generate subject value", identifierNode);
			}

			if (mSubjectValue)
				return mSubjectValue;
		}
	}

	if (stackFrame != NULL)
	{
		if (mDebugTarget->GetValueByName(GetCurrentMethod(), findName, stackFrame, &valAddr, &valType, &addrType))
		{
			//BF_ASSERT(valType != NULL);

			if (valType == NULL)
			{
				if (sizeof(addr_target) == 8)
					valType = mDbgModule->GetPrimitiveType(DbgType_i64, DbgLanguage_C);
				else
					valType = mDbgModule->GetPrimitiveType(DbgType_i32, DbgLanguage_C);
			}

			if (valType != NULL)
			{
				if (mReferenceId != NULL)
					*mReferenceId = findName;

				//bool isLocal = !valIsAddr;
				//if (valType->IsCompositeType())
					//isLocal = false;
				return ReadTypedValue(identifierNode, valType, valAddr, addrType);
			}
		}
	}

	bool isClosure = false;

	if (language == DbgLanguage_Beef)
	{
		intptr valAddr;
		DbgType* valType;
		//bool valIsAddr = false;
		if (mDebugTarget->GetValueByName(GetCurrentMethod(), "__closure", stackFrame, &valAddr, &valType, &addrType))
		{
			DbgTypedValue closureValue = ReadTypedValue(identifierNode, valType, valAddr, addrType);
			if (closureValue)
			{
				SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
				DbgTypedValue fieldValue = LookupField(identifierNode, closureValue, findName);
				if (fieldValue)
					return fieldValue;

				DbgTypedValue thisValue = LookupField(identifierNode, closureValue, "__this");
				if (thisValue)
				{
					fieldValue = LookupField(identifierNode, thisValue, findName);
					if (fieldValue)
						return fieldValue;
				}
			}
		}
	}

	/*if (mModule->mCurMethodState != NULL)
	{
		for (int localIdx = mModule->mCurMethodState->mLocals.size() - 1; localIdx >= 0; localIdx--)
		{
			auto& varDecl = mModule->mCurMethodState->mLocals[localIdx];
			if (varDecl.mName == findName)
			{
				if (varDecl.mAddr != NULL)
				{
					return DbgTypedValue(varDecl.mAddr, varDecl.mUnresolvedType, true);
				}
				else
					return DbgTypedValue(varDecl.mArgument, varDecl.mUnresolvedType, false);
				return DbgTypedValue();
			}
		}
	}*/

	if (!isClosure)
	{
		// This uses an incorrect 'this' in the case of a closure
		result = LookupField(identifierNode, DbgTypedValue(), findName);
		if ((result) || (HasPropResult()))
			return result;
	}

	result = GetRegister(findName);
	if ((result) || (HasPropResult()))
	{
		return result;
	}

	mDbgModule->ParseGlobalsData();
	for (auto compileUnit : mDbgModule->mCompileUnits)
	{
		if (mDbgCompileUnit == NULL)
			continue;
		if ((compileUnit->mLanguage != DbgLanguage_Unknown) && (compileUnit->mLanguage != mDbgCompileUnit->mLanguage))
			continue;

		result = DoLookupField(identifierNode, DbgTypedValue(), compileUnit->mGlobalType, findName, NULL, false);
		if ((result) || (HasPropResult()))
			return result;
	}

	auto currentMethod = GetCurrentMethod();
	if ((currentMethod != NULL) && (currentMethod->mParentType == NULL) && (currentMethod->mHasQualifiedName))
	{
		currentMethod->mCompileUnit->mDbgModule->MapCompileUnitMethods(currentMethod->mCompileUnit);
	}

	if ((currentMethod != NULL) && (currentMethod->mParentType != NULL))
	{
		auto dbgType = currentMethod->mParentType;
		if (dbgType->IsPointerOrRef())
			dbgType = dbgType->mTypeParam;

		auto curAlloc = &dbgType->mCompileUnit->mDbgModule->mAlloc;

		result = DoLookupField(identifierNode, DbgTypedValue(), dbgType, findName, NULL, false);
		if ((result) || (HasPropResult()))
			return result;

		GetNamespaceSearch();
		for (auto usingNamespace : mNamespaceSearch)
		{
			usingNamespace = usingNamespace->GetPrimaryType();
			result = DoLookupField(identifierNode, DbgTypedValue(), usingNamespace, findName, NULL, false);
			if ((result) || (HasPropResult()))
				return result;

			for (auto checkNamespace : usingNamespace->mAlternates)
			{
				result = DoLookupField(identifierNode, DbgTypedValue(), checkNamespace, findName, NULL, false);
				if ((result) || (HasPropResult()))
					return result;
			}
		}
	}

	return DbgTypedValue();
}

DbgTypedValue DbgExprEvaluator::LookupIdentifier(BfAstNode* identifierNode, bool ignoreInitialError, bool* hadError)
{
	if ((mStackSearch != NULL) && (mStackSearch->mIdentifier == NULL))
	{
		mStackSearch->mIdentifier = identifierNode;
		mStackSearch->mStartingStackIdx = mCallStackIdx;

		int skipCount = 0;

		StringT<256> findStr;
		for (int i = 0; i < mStackSearch->mSearchStr.mLength; i++)
		{
			char c = mStackSearch->mSearchStr[i];

			if (c == '^')
			{
				skipCount = atoi(mStackSearch->mSearchStr.c_str() + i + 1);
				break;
			}

			if (c == '.')
			{
				findStr += ':';
				findStr += ':';
			}
			else
				findStr += c;
		}

		while (true)
		{
			auto stackFrame = GetStackFrame();
			bool matches = true;

			if (mStackSearch->mSearchStr != "*")
			{
				mDebugger->UpdateCallStackMethod(mCallStackIdx);
				if (stackFrame->mSubProgram != NULL)
				{
					int strLen = strlen(stackFrame->mSubProgram->mName);
					if (strLen >= findStr.mLength)
					{
						if (strncmp(stackFrame->mSubProgram->mName + strLen - findStr.mLength, findStr.c_str(), findStr.mLength) == 0)
						{
							if (strLen > findStr.mLength)
							{
								char endC = stackFrame->mSubProgram->mName[strLen - findStr.mLength - 1];
								if (endC != ':')
									matches = false;
							}
						}
						else
							matches = false;
					}
					else
						matches = false;
				}
				else
					matches = false;
			}

			if (matches)
			{
				if (skipCount > 0)
				{
					skipCount--;
				}
				else
				{
					auto result = DoLookupIdentifier(identifierNode, ignoreInitialError, hadError);
					if (result)
						return result;
				}
			}

			mCallStackIdx++;
			if (mCallStackIdx >= mDebugger->mCallStack.mSize)
				mDebugger->UpdateCallStack();
			if (mCallStackIdx >= mDebugger->mCallStack.mSize)
				return DbgTypedValue();
		}
	}

	return DoLookupIdentifier(identifierNode, ignoreInitialError, hadError);
}

void DbgExprEvaluator::Visit(BfAssignmentExpression* assignExpr)
{
	mHadSideEffects = true;

	auto binaryOp = BfAssignOpToBinaryOp(assignExpr->mOp);

	//auto ptr = mModule->GetOrCreateVarAddr(assignExpr->mLeft);
	VisitChild(assignExpr->mLeft);
	if ((!mResult) && (!HasPropResult()))
		return;

	if (HasPropResult())
	{
		if (mPropSet == NULL)
		{
			Fail("Property has no setter", mPropSrc);
			return;
		}

		auto propSrc = mPropSrc;
		auto propSet = mPropSet;
		auto propTarget = mPropTarget;
		auto indexerValues = mIndexerValues;

		auto indexerExprValues = mIndexerExprValues;
		mPropGet = NULL;
		mPropSet = NULL;
		mPropSrc = NULL;
		mPropTarget = DbgTypedValue();
		mIndexerValues.clear();
		mIndexerExprValues.clear();

		auto valueParam = propSet->mParams.back();
		if (valueParam == NULL)
		{
			Fail("Invalid property setter", mPropSrc);
			return;
		}

		DbgTypedValue convVal;
		if (binaryOp != BfBinaryOp_None)
		{
			PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, true);
			if (!mResult)
				return;
			convVal = mResult;
		}
		else
		{
			convVal = CreateValueFromExpression(assignExpr->mRight, valueParam->mType);
		}
		if (!convVal)
			return;

// 		SizedArray<DbgTypedValue, 4> argPushQueue;
// 		if (propSet->mHasThis)
// 			argPushQueue.push_back(propTarget);
// 		for (auto indexer : indexerValues)
// 			argPushQueue.push_back(indexer);
// 		argPushQueue.push_back(convVal);
// 		if (propSet->mParams.Size() == argPushQueue.size())
// 		{
// 			mResult = CreateCall(propSet, argPushQueue, false);
//
//		}

		indexerExprValues.push_back(assignExpr->mRight);
		indexerValues.push_back(convVal);
		mResult = CreateCall(propSrc, propTarget, propSet, false, indexerExprValues, indexerValues);

		return;
	}

	GetResult();
	auto ptr = mResult;
	mResult = DbgTypedValue();

	if (binaryOp != NULL)
	{
		PerformBinaryOperation(assignExpr->mLeft, assignExpr->mRight, binaryOp, assignExpr->mOpToken, true);
	}
	else
	{
		SetAndRestoreValue<DbgTypedValue*> prevReceiveValue(mReceivingValue, &ptr);

		mResult = DbgTypedValue();
 		mResult = CreateValueFromExpression(assignExpr->mRight, ptr.mType, DbgEvalExprFlags_NoCast);

		if (mReceivingValue == NULL)
		{
			// Receiving value used
			mResult = ptr;
			return;
		}
	}
	if (!mResult)
		return;

	if ((ptr.mType->mTypeCode == DbgType_Ref) || (ptr.mType->mTypeCode == DbgType_RValueReference))
		ptr.mType = ptr.mType->mTypeParam;

	if ((mResult.mType->mTypeCode == DbgType_i32) && (mResult.mIsLiteral) && (ptr.mType->IsPointer()))
	{
		// Allow for literal pointer setting
		mResult.mType = ptr.mType;
	}

	if ((mResult.mType->IsPointer()) && (mResult.mIsLiteral))
	{
		Fail("Cannot assign from literal value", assignExpr->mRight);
		return;
	}

	mResult = Cast(assignExpr->mRight, mResult, ptr.mType, true);
	if (!mResult)
		return;

	if ((mExpressionFlags & DwEvalExpressionFlag_AllowSideEffects) == 0)
	{
		mBlockedSideEffects = true;
		return;
	}

	StoreValue(ptr, mResult, assignExpr->mLeft);
}

bool DbgExprEvaluator::StoreValue(DbgTypedValue& ptr, DbgTypedValue& value, BfAstNode* refNode)
{
	if ((!HasPropResult()) && ((ptr.mSrcAddress == 0) || (ptr.mIsReadOnly)) && (ptr.mRegNum < 0))
	{
		Fail("Cannot assign to value", refNode);
		return false;
	}

	if (ptr.mSrcAddress)
	{
		if (ptr.mType->mTypeCode == DbgType_Bitfield)
		{
			auto dbgBitfieldType = (DbgBitfieldType*)ptr.mType;

			uint64 tempVal = 0;
			mDebugger->ReadMemory(ptr.mSrcAddress, ptr.mType->GetByteCount(), &tempVal);

			uint64 srcMask = ((uint64)1 << dbgBitfieldType->mLength) - 1;
			uint64 destMask = srcMask << dbgBitfieldType->mPosition;
			value.mUInt64 &= srcMask;
			tempVal &= ~destMask;
			tempVal |= value.mUInt64 << dbgBitfieldType->mPosition;

			if (!mDebugger->WriteMemory(ptr.mSrcAddress, &tempVal, ptr.mType->GetByteCount()))
				mPassInstance->Fail("Failed to write to memory");

			if ((dbgBitfieldType->mTypeParam->IsSigned()) && ((value.mUInt64 & (1LL << (dbgBitfieldType->mLength - 1))) != 0))
			{
				// Sign extend
				value.mUInt64 |= ~srcMask;
			}
		}
		else
		{
			if (!mDebugger->WriteMemory(ptr.mSrcAddress, &value.mInt8, ptr.mType->GetByteCount()))
				mPassInstance->Fail("Failed to write to memory");
		}
	}
	else
	{
		String error;
		if (!mDebugger->AssignToReg(mCallStackIdx, ptr, value, error))
			mPassInstance->Fail(error);
	}
	return true;
}

bool DbgExprEvaluator::StoreValue(DbgTypedValue& ptr, BfExpression* expr)
{
	auto receiveAddr = ptr.mSrcAddress;
	if (receiveAddr == 0)
	{
		Fail("Unable to determine receiving address", expr);
		return false;
	}

	SetAndRestoreValue<DbgTypedValue*> prevReceivingValue(mReceivingValue, &ptr);

	auto wantType = ptr.mType->RemoveModifiers();
	auto result = Resolve(expr, ptr.mType);

	if (mReceivingValue == NULL)
		return true; // Already written
	if (!result)
		return false;

	return StoreValue(ptr, result, expr);;
}

void DbgExprEvaluator::Visit(BfParenthesizedExpression* parenExpr)
{
	VisitChild(parenExpr->mExpression);
}

const char* DbgExprEvaluator::GetTypeName(DbgType* type)
{
	if (type != NULL)
	{
		if (type->IsBfObjectPtr())
			return "object";
		if (type->IsPointer())
			return "pointer";
	}
	return "value";
}

DbgTypedValue DbgExprEvaluator::GetResult()
{
	if (!mResult)
	{
		if (HasPropResult())
		{
			if (mPropGet == NULL)
			{
				Fail("Property has no getter", mPropSrc);
			}
			else
			{
// 				SizedArray<DbgTypedValue, 4> argPushQueue;
// 				auto curParam = mPropGet->mParams.mHead;
// 				if (mPropGet->mHasThis)
// 				{
// 					argPushQueue.push_back(mPropTarget);
// 					if (curParam != NULL)
// 						curParam = curParam->mNext;
// 				}
// 				bool failed = false;
// 				for (int indexerIdx = 0; indexerIdx < (int)mIndexerValues.size(); indexerIdx++)
// 				{
// 					auto val = mIndexerValues[indexerIdx];
// 					if (curParam != NULL)
// 					{
// 						val = Cast(mPropSrc, val, curParam->mType);
// 						if (!val)
// 						{
// 							failed = true;
// 							break;
// 						}
// 					}
// 					argPushQueue.push_back(val);
//
// 					if (curParam != NULL)
// 						curParam = curParam->mNext;
// 				}
// 				if (!failed)
// 				{
// 					if (mPropGet->mParams.Size() == argPushQueue.size())
// 					{
// 						mResult = CreateCall(mPropGet, argPushQueue, false);
// 					}
// 					else
// 					{
// 						Fail("Indexer parameter count mismatch", mPropSrc);
// 					}
// 				}

				SetAndRestoreValue<DwEvalExpressionFlags> prevFlags(mExpressionFlags);
				if ((mExpressionFlags & DwEvalExpressionFlag_AllowPropertyEval) != 0)
					mExpressionFlags = (DwEvalExpressionFlags)(mExpressionFlags | DwEvalExpressionFlag_AllowCalls);
				mResult = CreateCall(mPropSrc, mPropTarget, mPropGet, false, mIndexerExprValues, mIndexerValues);
			}
		}
	}

	if ((mResult) && (mResult.mType->IsConst()) && (GetLanguage() == DbgLanguage_Beef))
	{
		mResult.mIsReadOnly = true; // Pretend we don't really have the address
	}

	mPropGet = NULL;
	mPropSet = NULL;
	mPropSrc = NULL;
	mIndexerValues.clear();
	mIndexerExprValues.clear();

	return mResult;
}

bool DbgExprEvaluator::HasPropResult()
{
	return (mPropGet != NULL) || (mPropSet != NULL);
}

DbgLanguage DbgExprEvaluator::GetLanguage()
{
	if (mLanguage != DbgLanguage_NotSet)
		return mLanguage;
	if ((mDbgCompileUnit != NULL) && (mDbgCompileUnit->mLanguage != DbgLanguage_Unknown))
		return mDbgCompileUnit->mLanguage;
	if (mExplicitThis)
		return mExplicitThis.mType->mLanguage;
	auto currentMethod = GetCurrentMethod();
	if (currentMethod != NULL)
		return currentMethod->GetLanguage();
	return DbgLanguage_Beef; // Default to Beef
}

DbgFlavor DbgExprEvaluator::GetFlavor()
{
	if (mDbgCompileUnit != NULL)
		return mDbgCompileUnit->mDbgModule->mDbgFlavor;
	if (mExplicitThis)
		return mExplicitThis.mType->mCompileUnit->mDbgModule->mDbgFlavor;
	return DbgFlavor_Unknown;
}

void DbgExprEvaluator::AutocompleteAddMethod(const char* methodName, const StringImpl& filter)
{
	const char* atPos = strchr(methodName, '@');
	if (atPos == NULL)
	{
		if ((strncmp(methodName, "get__", 5) == 0) ||
			(strncmp(methodName, "set__", 5) == 0))
		{
			const char* propName = methodName + 5;
			if (*propName != (char)0)
				mAutoComplete->AddEntry(AutoCompleteEntry("property", propName), filter);
		}
		else if (strncmp(methodName, "__", 2) != 0)
			mAutoComplete->AddEntry(AutoCompleteEntry("method", methodName), filter);
	}
}

void DbgExprEvaluator::AutocompleteAddMembers(DbgType* dbgType, bool wantsStatic, bool wantsNonStatic, const StringImpl& filter, bool isCapture)
{
	if (mStackSearch != NULL)
	{
		if (!mStackSearch->mAutocompleteSearchedTypes.Add(dbgType))
			return;
	}

	DbgLanguage language = GetLanguage();
	DbgFlavor flavor = GetFlavor();
	if ((mDbgCompileUnit != NULL) && (dbgType->mLanguage != DbgLanguage_Unknown) && (dbgType->mLanguage != language))
		return;

	dbgType->PopulateType();
	if (dbgType->mNeedsGlobalsPopulated)
		mDbgModule->PopulateTypeGlobals(dbgType);

	// Don't get primary type for namespace, causes mAlternates infinite loop
	if (!dbgType->IsNamespace())
		dbgType = dbgType->GetPrimaryType();

	// false/false means just add subtypes
	if ((wantsStatic) || (!wantsNonStatic))
	{
		auto subType = dbgType->mSubTypeList.mHead;
		while (subType != NULL)
		{
			if (subType->mLanguage != language)
			{
				// Ignore
			}
			else if (subType->mTypeName == NULL)
			{
				AutocompleteAddMembers(subType, wantsStatic, wantsNonStatic, filter);
			}
			else
			{
				bool allowType = true;
				for (const char* cPtr = subType->mTypeName; *cPtr != '\0'; cPtr++)
				{
					char c = *cPtr;
					if ((c == '`') || (c == '[') || (c == '.') || (c == '$') || (c == '('))
						allowType = false;
				}

				if ((allowType) && (subType->IsNamespace()))
				{
					// Is it a "using" declaration?
					if ((!dbgType->IsRoot()) && (!dbgType->IsNamespace()))
						allowType = false;
				}

				if ((!subType->IsAnonymous()) && (!subType->IsSizedArray()) && (allowType))
				{
					mDbgModule->TempRemoveTemplateStr(subType->mTypeName, subType->mTemplateNameIdx);
					// We set the true mEntryType after the filter passes.  This is because IsBfObject() is a "heavyweight" operation that we don't want to
					//  just run on ALL types immediately
					AutoCompleteEntry* entry = mAutoComplete->AddEntry(AutoCompleteEntry("type", subType->mTypeName), filter);
					if (entry != NULL)
					{
						if (subType->IsBfObject())
							entry->mEntryType = "class";
						else if (subType->IsNamespace())
							entry->mEntryType = "namespace";
						else
							entry->mEntryType = "valuetype";
					}
					mDbgModule->ReplaceTemplateStr(subType->mTypeName, subType->mTemplateNameIdx);
				}
				else if (subType->IsGlobalsContainer())
				{
					if (subType->mPriority > DbgTypePriority_Normal)
						AutocompleteAddMembers(subType, wantsStatic, wantsNonStatic, filter);
				}

				if ((subType->mTypeCode == DbgType_Enum) && (language == DbgLanguage_C))
				{
					AutocompleteAddMembers(subType, wantsStatic, wantsNonStatic, filter);
				}
			}
			subType = subType->mNext;
		}

		if (dbgType->mTypeCode == DbgType_Namespace)
		{
			for (auto altType : dbgType->mAlternates)
			{
				AutocompleteAddMembers(altType, wantsStatic, wantsNonStatic, filter);
			}
		}
	}

	// In beef, all globals are stored inside global containers so we can skip the rest
	bool skipMembers = (language == DbgLanguage_Beef) && (dbgType->mTypeCode == DbgType_Root);
	if (skipMembers)
		return;

	for (auto member : dbgType->mMemberList)
	{
		if (((member->mIsStatic) && (wantsStatic)) ||
			((!member->mIsStatic) && (wantsNonStatic)))
		{
			const char* name = member->mName;
			if (name != NULL)
			{
				if (member->mName[0] == '?')
					continue;

				mAutoComplete->AddEntry(AutoCompleteEntry(GetTypeName(member->mType), name), filter);

				if ((isCapture) && (strcmp(member->mName, "__this") == 0))
				{
					mAutoComplete->AddEntry(AutoCompleteEntry(GetTypeName(member->mType), "this"), filter);
					auto thisType = member->mType;
					if (thisType->IsPointer())
						thisType = thisType->mTypeParam;
					thisType = thisType->RemoveModifiers();
					AutocompleteAddMembers(thisType, wantsStatic, wantsNonStatic, filter);
				}
			}
			else
			{
				AutocompleteAddMembers(member->mType, wantsStatic, wantsNonStatic, filter);
			}
		}
	}

	if ((dbgType->IsNamespace()) || (dbgType->IsRoot()))
	{
		// We only add these ones because we can be certain they are all static, otherwise we need the full
		//  method information which is in the compile units
		for (auto methodNameEntry : dbgType->mMethodNameList)
		{
			const char* methodName = methodNameEntry->mName;
			AutocompleteAddMethod(methodName, filter);
		}
	}
	else
	{
		//TODO: Not needed since we added the declarations now, right?
		//dbgType->EnsureMethodsMapped();
	}

	for (auto method : dbgType->mMethodList)
	{
		if (((!method->mHasThis) && (wantsStatic)) ||
			((method->mHasThis) && (wantsNonStatic)))
		{
			if ((method->mName != NULL) && (strcmp(method->mName, "this") != 0))
			{
				mDbgModule->FindTemplateStr(method->mName, method->mTemplateNameIdx);
				if (method->mTemplateNameIdx == -1)
				{
					AutocompleteAddMethod(method->mName, filter);
				}
			}
		}
	}

	/*if (!dbgType->mMethodNameList.IsEmpty())
	{
		BF_ASSERT(!dbgType->mMethodList.IsEmpty());
	}*/

	/*
	for (auto methodNameEntry : dbgType->mMethodNameList)
	{
		const char* methodName = methodNameEntry->mName;
		AutocompleteAddMethod(methodName, filter);
	}*/

	for (auto baseTypeEntry : dbgType->mBaseTypes)
	{
		AutocompleteAddMembers(baseTypeEntry->mBaseType, wantsStatic, wantsNonStatic, filter);
	}
}

void DbgExprEvaluator::AutocompleteCheckMemberReference(BfAstNode* target, BfAstNode* dotToken, BfAstNode* memberName)
{
	if ((IsAutoCompleteNode(dotToken)) || (IsAutoCompleteNode(memberName)))
	{
		mDbgModule->ParseGlobalsData();
		bool isCType = dotToken->ToString() == "::";

		String filter;
		if (memberName != NULL)
		{
			filter = memberName->ToString();
			mAutoComplete->mInsertStartIdx = memberName->GetSrcStart();
			mAutoComplete->mInsertEndIdx = memberName->GetSrcEnd();
		}
		else
		{
			mAutoComplete->mInsertStartIdx = dotToken->GetSrcEnd();
			mAutoComplete->mInsertEndIdx = dotToken->GetSrcEnd();
		}

		SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
		if (target == NULL)
		{
			mResult.mType = GetExpectingType();
			if (mResult.mType != NULL)
				mResult.mHasNoValue = true;
		}
		else if (auto typeRef = BfNodeDynCast<BfTypeReference>(target))
		{
			mResult.mType = ResolveTypeRef(typeRef);
			mResult.mHasNoValue = true;
		}
		else
		{
			VisitChild(target);
			GetResult();
		}

		if (mResult.mType != NULL)
		{
			mResult.mType = mResult.mType->RemoveModifiers();

			auto dbgType = mResult.mType;
// 			if (dbgType->IsPrimitiveType())
// 			{
// 				//TODO: Boxed primitive structs
// 			}

			//dbgType = dbgType->GetPrimaryType();
			if ((dbgType != NULL) && (dbgType->IsPointerOrRef()))
			{
				dbgType = dbgType->mTypeParam;
				dbgType = dbgType->RemoveModifiers();
			}
			//if (dbgType->mIsDeclaration)
			dbgType = dbgType->GetPrimaryType();

			if ((dbgType != NULL) /*&& ((dbgType->IsCompositeType()) || (dbgType->IsEnum()))*/)
			{
				bool wantsStatic = mResult.mHasNoValue;
				bool wantsNonStatic = !mResult.mHasNoValue;
				// C allows static lookups by non-static member reference
				if (dbgType->GetLanguage() != DbgLanguage_Beef)
					wantsStatic = true;

				AutocompleteAddMembers(dbgType, wantsStatic, wantsNonStatic, filter);

				if ((wantsStatic) && (dbgType->IsBfPayloadEnum()))
				{
					for (auto member : dbgType->mMemberList)
					{
						if ((member->mName[0] == '_') && (member->mName[1] >= '0') && (member->mName[1] <= '9'))
						{
							int val = atoi(member->mName + 1);
							for (int i = 1; true; i++)
							{
								if (member->mName[i] == '_')
								{
									mAutoComplete->AddEntry(AutoCompleteEntry("method", member->mName + i + 1), filter);
									break;
								}
								if (member->mName[i] == 0)
									break;
							}
						}
					}
				}
			}

			//String prefixStr = dbgType->ToString();
			//AutocompleteAddMembersFromNamespace(prefixStr, filter, isCType);
			mResult = DbgTypedValue();
		}
		else
		{
			//return AutocompleteAddMembersFromNamespace(memberRefExpr->mTarget->ToString(), filter, isCType);
		}
	}
}

void DbgExprEvaluator::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	DbgTypedValue thisValue;
	/*if (auto typeRef = BfNodeDynCast<DwTypeReference>(memberRefExpr->mTarget))
	{
		// Look up static field
		//thisValue = DbgTypedValue(NULL, mModule->ResolveTypeRef(typeRef));
		thisValue.mType = ResolveTypeRef(typeRef);
	}*/

	auto flavor = GetFlavor();

	String findName;
	BfAstNode* nameRefNode = memberRefExpr->mMemberName;
	if (auto attrIdentifierExpr = BfNodeDynCast<BfAttributedIdentifierNode>(memberRefExpr->mMemberName))
	{
		nameRefNode = attrIdentifierExpr->mIdentifier;
		if (nameRefNode != NULL)
			findName = attrIdentifierExpr->mIdentifier->ToString();
	}
	else if (memberRefExpr->mMemberName != NULL)
		findName = memberRefExpr->mMemberName->ToString();

	AutocompleteCheckMemberReference(memberRefExpr->mTarget, memberRefExpr->mDotToken, nameRefNode);

	//
	{
		SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
		mResult.mType = ResolveTypeRef(memberRefExpr);
		if (mResult.mType != NULL)
		{
			mResult.mHasNoValue = true;
			return;
		}
	}

	if (memberRefExpr->mMemberName == NULL)
	{
		return;
	}

	if (memberRefExpr->mTarget == NULL)
	{
		auto expectingType = GetExpectingType();
		if (expectingType != NULL)
		{
			DbgTypedValue expectingVal;
			expectingVal.mType = expectingType;
			mResult = LookupField(memberRefExpr->mMemberName, expectingVal, findName);
			if ((mResult) || (HasPropResult()))
				return;
		}
	}

	{
		SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
		if (memberRefExpr->mTarget == NULL)
			thisValue.mType = mExpectingType;
		else
			thisValue.mType = ResolveTypeRef(memberRefExpr->mTarget, (BfAstNode**)&(memberRefExpr->mTarget));
		if (thisValue.mType != NULL)
			thisValue.mHasNoValue = true;
	}

	if (thisValue.mType == NULL)
	{
		if (auto exprTarget = BfNodeDynCast<BfExpression>(memberRefExpr->mTarget))
		{
			//thisValue = mModule->CreateValueFromExpression(exprTarget);
			VisitChild(memberRefExpr->mTarget);
			GetResult();
			if (mResult.mType == NULL)
				return;
			mResult.mType = mResult.mType->RemoveModifiers();
			thisValue = mResult;
		}
		else if (memberRefExpr->mTarget == NULL)
		{
			Fail("Identifier expected", memberRefExpr->mDotToken);
		}
		else
		{
			Fail("Unable to resolve target", memberRefExpr->mTarget);
		}
	}
	/*else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberRefExpr->mTarget))
	{
		// Look up static field
		thisValue.mType = ResolveTypeRef(typeRef);
	}*/

	if (thisValue.mSrcAddress == -1)
	{
		LookupSplatMember(memberRefExpr->mTarget, memberRefExpr, thisValue, findName);
	}
	else
		mResult = LookupField(memberRefExpr->mMemberName, thisValue, findName);
	/*if (mResult)
	{
		if (mReferenceId != NULL)
			*mReferenceId = thisValue.mType->ToString() + "." + findName;
	}*/

	if ((!mResult) && (!HasPropResult()))
	{
		//String fullTokenString = memberRefExpr->ToString();
		//mDbgModule->EnsureMethodsMapped(fullTokenString.c_str());
		mResult = LookupField(memberRefExpr->mMemberName, thisValue, findName);
	}

	//TODO: Do type lookup?

	if ((!mResult) && (!HasPropResult()))
	{
		if (thisValue.mHasNoValue)
		{
			for (auto altDwType : thisValue.mType->mAlternates)
			{
				auto altThisValue = thisValue;
				altThisValue.mType = altDwType;
				mResult = LookupField(memberRefExpr->mMemberName, altThisValue, findName);
				if (mResult)
					return;
			}
		}

		Fail("Unable to find member", memberRefExpr->mMemberName);
	}
}

DbgTypedValue DbgExprEvaluator::RemoveRef(DbgTypedValue typedValue)
{
	bool hadRef = false;
	typedValue.mType = typedValue.mType->RemoveModifiers(&hadRef);
	if (hadRef)
		typedValue.mSrcAddress = typedValue.mPtr;
	return typedValue;
}

void DbgExprEvaluator::Visit(BfIndexerExpression* indexerExpr)
{
	VisitChild(indexerExpr->mTarget);
	GetResult();
	if (!mResult)
		return;
	DbgTypedValue collection = RemoveRef(mResult);

	bool indexerFailed = false;

	mIndexerValues.clear();
	mIndexerExprValues.clear();
	SizedArray<BfExpression*, 2> indexerExprValues;
	SizedArray<DbgTypedValue, 2> indexerValues;
	for (auto indexExpr : indexerExpr->mArguments)
	{
		indexerExprValues.push_back(indexExpr);
		VisitChild(indexExpr);
		GetResult();
		if (!mResult)
			indexerFailed = true;
		indexerValues.push_back(mResult);
		mResult = DbgTypedValue();
	}

	if (indexerFailed)
	{
		mResult = DbgTypedValue();
		return;
	}

	bool isBfArrayIndex = false;
	if ((collection.mType->IsBfObjectPtr()) || (collection.mType->IsStruct()))
	{
		DbgType* bfType = collection.mType->mTypeParam;
		if (bfType != NULL)
		{
			DbgType* baseType = bfType->GetBaseType();
			if ((baseType != NULL) && (strcmp(baseType->mName, "System.Array") == 0))
			{
				isBfArrayIndex = true;
			}
		}

		if (!isBfArrayIndex)
		{
			mIndexerExprValues = indexerExprValues;
			mIndexerValues = indexerValues;
			mResult = LookupField(indexerExpr->mTarget, collection, "");
			if (HasPropResult())
			{
				// Only use this if we actually have the method. Otherwise fall through so we can try a debug visualizer or something
				if ((mPropGet != NULL) && (mPropGet->mBlock.mLowPC != 0))
					return;
			}
// 			else
// 			{
// 				mResult = DbgTypedValue();
// 				Fail(StrFormat("Unable to index type '%s'", TypeToString(collection.mType).c_str()), indexerExpr->mOpenBracket);
// 			}
// 			return;
		}
	}

	if (indexerValues.size() != 1)
	{
		Fail("Expected single index", indexerExpr->mOpenBracket);
		return;
	}
	DbgTypedValue indexArgument = indexerValues[0];
	indexArgument.mType = indexArgument.mType->RemoveModifiers();
	if (!indexArgument.mType->IsInteger())
	{
		mResult = DbgTypedValue();
		Fail("Expected integer index", indexerExpr->mArguments[0]);
		return;
	}

	if (mReferenceId != NULL)
		*mReferenceId += "[]";

	if (isBfArrayIndex)
	{
		DbgType* bfType = collection.mType->mTypeParam;
		bfType = bfType->GetPrimaryType();

		DbgType* baseType = bfType->GetBaseType();
		DbgVariable* lenVariable = baseType->mMemberList.mHead;
		DbgVariable* typeVariable = bfType->mMemberList.mTail;

		if ((lenVariable != NULL) && (typeVariable != NULL))
		{
			int len = mDebugger->ReadMemory<int>(collection.mPtr + lenVariable->mMemberOffset);
			int idx = (int)indexArgument.GetInt64();
			if ((idx < 0) || (idx >= len))
			{
				Fail("Index out of range", indexerExpr->mArguments[0]);
				return;
			}

			auto result = ReadTypedValue(indexerExpr, typeVariable->mType, collection.mPtr + typeVariable->mMemberOffset + (idx * typeVariable->mType->GetStride()), DbgAddrType_Target);
			if (mResult.mIsReadOnly)
				result.mIsReadOnly = true;
			mResult = result;
			return;
		}
	}

	SetAndRestoreValue<String*> prevReferenceId(mReferenceId, NULL);

	Array<String> dbgVisWildcardCaptures;
	auto debugVis = mDebugger->FindVisualizerForType(collection.mType, &dbgVisWildcardCaptures);
	if (debugVis != NULL)
	{
		if ((debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array) && (debugVis->mLowerDimSizes.size() == 0))
		{
			auto debugVisualizers = mDebugger->mDebugManager->mDebugVisualizers;
			DbgTypedValue sizeValue = mDebugger->EvaluateInContext(mDbgCompileUnit, collection, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures));
			Array<int> lowerDimSizes;
			if (sizeValue)
			{
				int len = (int)sizeValue.GetInt64();
				int idx = (int)indexArgument.GetInt64();
				if ((idx < 0) || (idx >= len))
				{
					Fail("Index out of range", indexerExpr->mArguments[0]);
					return;
				}

				addr_target dataPtr = (collection.mType->IsPointer()) ? collection.mPtr : collection.mSrcAddress;
				String ptrUseDataTypeStr = collection.mType->ToStringRaw();
				String ptrUseDataStr = StrFormat("(%s)", ptrUseDataTypeStr.c_str()) + EncodeDataPtr(dataPtr, true);
				//String evalStr = "(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) + StrFormat("[%d]), this=", idx);
				String evalStr = "*(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) + StrFormat(" + %d), this=", idx) + ptrUseDataStr;
				evalStr += ptrUseDataStr;
				auto result = mDebugger->EvaluateInContext(mDbgCompileUnit, collection, evalStr);
				if (mResult.mIsReadOnly)
					result.mIsReadOnly = true;
				mResult = result;
				if (mResult)
					return;
			}
		}
	}

	addr_target target = collection.mPtr;
	if (collection.mType->mTypeCode == DbgType_SizedArray)
		target = collection.mSrcAddress;

	if ((!collection.mType->IsPointer()) && (collection.mType->mTypeCode != DbgType_SizedArray))
	{
		mResult = DbgTypedValue();
		//Fail("Expected pointer type", indexerExpr->mTarget);
		Fail(StrFormat("Unable to index type '%s'", TypeToString(collection.mType).c_str()), indexerExpr->mOpenBracket);
		return;
	}

	if (collection.mType->mTypeCode == DbgType_SizedArray)
	{
		int innerSize = collection.mType->mTypeParam->GetStride();
		int len = 0;
		if (innerSize > 0)
			len = collection.mType->GetStride() / innerSize;
		int idx = (int)indexArgument.GetInt64();
		if ((idx < 0) || (idx >= len))
		{
			Fail("Index out of range", indexerExpr->mArguments[0]);
			return;
		}
	}

	if ((collection.mType->IsBfObjectPtr()) || (collection.mType->IsStruct()))
	{
		// This should have been handled by some other cases
		mResult = DbgTypedValue();
		Fail(StrFormat("Unable to index type '%s'", TypeToString(collection.mType).c_str()), indexerExpr->mOpenBracket);
	}

	auto memberType = collection.mType->mTypeParam;
	auto result = ReadTypedValue(indexerExpr, memberType, target + indexArgument.GetInt64() * memberType->GetStride(), DbgAddrType_Target);
	if (mResult.mIsReadOnly)
		result.mIsReadOnly = true;
	mResult = result;
}

void DbgExprEvaluator::Visit(BfThisExpression* thisExpr)
{
	if (mExplicitThis)
	{
		if (mExplicitThisExpr != NULL)
			mDeferredInsertExplicitThisVector.push_back(NodeReplaceRecord(thisExpr, mCurChildRef));
		mResult = mExplicitThis;

		/*if (mReferenceId != NULL)
		{
			auto checkType = mExplicitThis.mType;
			if (checkType->IsPointer())
				checkType = checkType->mTypeParam;
			*mReferenceId = checkType->ToString(true);
		}*/

		return;
	}

	bool hadError;
	mResult = LookupIdentifier(thisExpr, false, &hadError);
	if (!mResult)
	{
		if (GetCurrentMethod() != NULL)
			Fail("Static methods don't have 'this'", thisExpr);
		else
			Fail("Execution must be paused to retrieve 'this'", thisExpr);
	}
}

void DbgExprEvaluator::Visit(BfIdentifierNode* identifierNode)
{
	//BfLogDbgExpr("Visit BfIdentifierNode %s\n", identifierNode->ToString().c_str());

	mResult = LookupIdentifier(identifierNode, false, NULL);
	if (!mResult)
	{
		//??
		//mDbgModule->EnsureMethodsMapped(identifierNode->ToString().c_str());
		//mResult = LookupIdentifier(identifierNode, false, NULL);
	}

	if ((mResult) || (HasPropResult()))
		return;

	{
		SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
		mResult.mType = ResolveTypeRef(identifierNode);
		if (mResult.mType != NULL)
		{
			mResult.mHasNoValue = true;
			return;
		}
	}

	Fail("Identifier not found", identifierNode);
}

void DbgExprEvaluator::Visit(BfAttributedIdentifierNode* node)
{
	VisitChild(node->mIdentifier);
}

void DbgExprEvaluator::Visit(BfMixinExpression* mixinExpr)
{
	mResult = LookupIdentifier(mixinExpr, false, NULL);
	if (mResult)
		return;
	Fail("Identifier not found", mixinExpr);
}

void DbgExprEvaluator::LookupSplatMember(const DbgTypedValue& target, const StringImpl& fieldName)
{
	BF_ASSERT(target.mSrcAddress == -1);

	DbgVariable* dbgVariable = (DbgVariable*)target.mVariable;
	if (dbgVariable == NULL)
		return;

	auto wantType = target.mType->RemoveModifiers();
	auto checkType = dbgVariable->mType->RemoveModifiers();

	// Use real name, in case of aliases
	String findName = target.mVariable->mName;

	bool foundWantType = false;

	/*if (checkType != wantType)
	{
		checkType = valType;
		foundWantType = false;

		if (checkType->IsBfPayloadEnum())
		{
			if (wasCast)
			{
				findName += "$u";
				checkType = wantType;
				foundWantType = true;
			}
		}
	}*/

	bool found = false;
	while (checkType != NULL)
	{
		checkType = checkType->GetPrimaryType();
		if ((!foundWantType) && (checkType->Equals(wantType)))
			foundWantType = true;

		if (foundWantType)
		{
			for (auto member : checkType->mMemberList)
			{
				if ((member->mName != NULL) && (member->mName == fieldName))
				{
					found = true;
					break;
				}
			}
		}

		if (found)
			break;
		checkType = checkType->GetBaseType();
		findName += "$b";
	}

	if (found)
	{
		findName = "$" + findName + "$m$" + fieldName;

		CPUStackFrame* stackFrame = GetStackFrame();
		intptr valAddr;
		DbgType* valType;
		DbgAddrType addrType = DbgAddrType_Value;

		if (mDebugTarget->GetValueByName(GetCurrentMethod(), findName, stackFrame, &valAddr, &valType, &addrType))
		{
			BF_ASSERT(valType != NULL);

			if (valType != NULL)
			{
				if (mReferenceId != NULL)
					*mReferenceId = findName;
				mResult = ReadTypedValue(NULL, valType, valAddr, addrType);
				return;
			}
		}
	}
}

void DbgExprEvaluator::LookupSplatMember(BfAstNode* targetNode, BfAstNode* lookupNode, const DbgTypedValue& target, const StringImpl& fieldName, String* outFindName, bool* outIsConst, StringImpl* forceName)
{
	/*DbgVariable* dbgVariable = (DbgVariable*)target.mVariable;
	if (dbgVariable != NULL)
		return LookupSplatMember(target, fieldName);*/

	auto curMethod = GetCurrentMethod();

	while (auto parenNode = BfNodeDynCast<BfParenthesizedExpression>(lookupNode))
		lookupNode = parenNode->mExpression;

	SplatLookupEntry* splatLookupEntry = NULL;
	if (lookupNode != NULL)
	{
		if (mSplatLookupMap.TryAdd(lookupNode, NULL, &splatLookupEntry))
		{
			//
		}
		else if (forceName == NULL)
		{
			if (outFindName != NULL)
				*outFindName = splatLookupEntry->mFindName;
			if (outIsConst != NULL)
				*outIsConst = splatLookupEntry->mIsConst;
			mResult = splatLookupEntry->mResult;
			return;
		}
	}

	String findName;

	bool wasCast = false;
	mResult = DbgTypedValue();
	BfAstNode* checkNode = targetNode;
	BfAstNode* parentNode = NULL;
	while (true)
	{
		if (checkNode == NULL)
			return;
		if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkNode))
		{
			parentNode = qualifiedNameNode->mLeft;
			findName = qualifiedNameNode->mRight->ToString();
			break;
		}
		else if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(checkNode))
		{
			parentNode = memberRefExpr->mTarget;
			findName = memberRefExpr->mMemberName->ToString();
			break;
		}
		else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(checkNode))
		{
			findName = identifier->ToString();
			break;
		}
		else if (auto thisExpr = BfNodeDynCast<BfThisExpression>(checkNode))
		{
			findName = thisExpr->ToString();
			break;
		}
		else if (auto castExpr = BfNodeDynCast<BfCastExpression>(checkNode))
		{
			wasCast = true;
			checkNode = castExpr->mExpression;
		}
		else if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(checkNode))
		{
			checkNode = parenExpr->mExpression;
		}
		else
			return;
	}

	if (forceName != NULL)
		findName = *forceName;

	CPUStackFrame* stackFrame = GetStackFrame();
	intptr valAddr;
	DbgType* valType = NULL;
	DbgAddrType addrType = DbgAddrType_Value;

	bool foundWantType = true;
	DbgType* wantType = target.mType;
	auto checkType = target.mType;
	checkType = checkType->RemoveModifiers();
	bool valWasConst = false;

	bool foundParent = false;
	if (target.mSrcAddress == -1)
	{
		if (parentNode != NULL)
		{
			DbgTypedValue origTarget;
			origTarget.mSrcAddress = -1;
			origTarget.mType = target.mVariable->mType;
			origTarget.mVariable = target.mVariable;

			bool parentIsConst = false;
			String parentFindName;
			LookupSplatMember(parentNode, targetNode, origTarget, findName, &parentFindName, &parentIsConst);
			if (!parentFindName.IsEmpty())
			{
				foundParent = true;
				findName = parentFindName;
				valType = mResult.mType;
				valWasConst = parentIsConst;
			}
		}
	}

	if (!foundParent)
	{
		if (!mDebugTarget->GetValueByName(curMethod, findName, stackFrame, &valAddr, &valType, &addrType))
			return;

		if (addrType == DbgAddrType_Alias)
		{
			findName = (const char*)valAddr;

			if (!mDebugTarget->GetValueByName(curMethod, findName, stackFrame, &valAddr, &valType, &addrType))
			{
				if (curMethod->mInlineeInfo != NULL)
				{
					// Look outside to inline
					SetAndRestoreValue<int> prevStackIdx(mCallStackIdx, mCallStackIdx + 1);
					LookupSplatMember(targetNode, lookupNode, target, fieldName, outFindName, outIsConst, &findName);
					return;
				}

				return;
			}
		}

		if (addrType == DbgAddrType_LocalSplat)
		{
			DbgVariable* dbgVariable = (DbgVariable*)valAddr;
			// Use real name, in case of aliases
			findName = dbgVariable->mName;
		}
	}

	if ((wasCast) && (valType != NULL))
	{
		checkType = valType->RemoveModifiers();
		foundWantType = false;

		if (checkType->IsBfPayloadEnum())
		{
			if (wasCast)
			{
				findName += "$u";
				checkType = wantType;
				foundWantType = true;
			}
		}
	}

	mResult = DbgTypedValue();
	if (valType != NULL)
		valWasConst |= valType->IsConst();
	DbgType* memberType = NULL;
	bool found = false;
	bool wasUnion = false;
	while (checkType != NULL)
	{
		checkType = checkType->GetPrimaryType();
		if ((!foundWantType) && (checkType->Equals(wantType)))
			foundWantType = true;

		if (foundWantType)
		{
			for (auto member : checkType->mMemberList)
			{
				if ((member->mName != NULL) && (member->mName == fieldName))
				{
					wasUnion = checkType->IsBfUnion();
					memberType = member->mType;
					found = true;
					break;
				}
			}
		}

		if (found)
			break;
		checkType = checkType->GetBaseType();
		findName += "$b";
	}

	if (found)
	{
		bool wantConst = ((valWasConst) && (!memberType->IsConst()) && (!memberType->IsRef()));
		if (!findName.StartsWith("$"))
			findName = "$" + findName;
		if (wasUnion)
			findName += "$u";
		else
			findName += "$m$" + fieldName;

		if (outFindName != NULL)
			*outFindName = findName;
		if (outIsConst != NULL)
			*outIsConst = wantConst;

		if (mDebugTarget->GetValueByName(curMethod, findName, stackFrame, &valAddr, &valType, &addrType))
		{
			BF_ASSERT(valType != NULL);

			if (valType != NULL)
			{
				if (mReferenceId != NULL)
					*mReferenceId = findName;
				/*if ((valType->IsConst()) && (!memberType->IsConst()))
				{
					// Set readonly if the original value was const
					memberType = mDbgModule->GetConstType(valType);
				}*/
				mResult = ReadTypedValue(targetNode, memberType, valAddr, addrType);
				if (wantConst)
				{
					// Set readonly if the original value was const
					mResult.mIsReadOnly = true;
				}
			}
		}
		else
		{
 			if (target.mSrcAddress == -1)
 			{
// 				if (curMethod->mInlineeInfo != NULL)
// 				{
// 					// Look outside to inline
// 					SetAndRestoreValue<int> prevStackIdx(mCallStackIdx, mCallStackIdx + 1);
// 					LookupSplatMember(targetNode, lookupNode, target, fieldName, outFindName, outIsConst, true);
// 					return;
// 				}

				if (!memberType->IsStruct())
					Fail("Failed to lookup splat member", (lookupNode != NULL) ? lookupNode : targetNode);

				if ((target.mVariable == NULL) && (target.mType->GetByteCount() != 0))
					Fail("Splat variable not found", (lookupNode != NULL) ? lookupNode : targetNode);

 				//BF_ASSERT((target.mVariable != NULL) || (target.mType->GetByteCount() == 0));
 				mResult = target;
 				mResult.mType = memberType;
 			}
		}

		if (splatLookupEntry != NULL)
		{
			splatLookupEntry->mFindName = findName;
			splatLookupEntry->mIsConst = wantConst;
			splatLookupEntry->mResult = mResult;
		}
	}
}

void DbgExprEvaluator::LookupQualifiedName(BfQualifiedNameNode* nameNode, bool ignoreInitialError, bool* hadError)
{
	String fieldName = nameNode->mRight->ToString();

	mResult = LookupIdentifier(nameNode->mLeft, ignoreInitialError, hadError);
	mResult = GetResult();

	if (!mResult)
	{
		if (!ignoreInitialError)
			Fail("Identifier not found", nameNode->mLeft);
		return;
	}

	if (!mResult)
		return;

	/*if (!mResult.mType->IsTypeInstance())
	{
		Fail("Type has no fields", nameNode->mLeft);
		return;
	}*/

	if (mResult.mSrcAddress == -1)
	{
		auto target = mResult;
		mResult = DbgTypedValue();
		LookupSplatMember(nameNode->mLeft, nameNode, target, fieldName);
	}
	else
	{
		mResult = LookupField(nameNode, mResult, fieldName);
		if (mPropSrc != NULL)
		{
			if (nameNode->mLeft->ToString() == "base")
			{
				//mPropDefBypassVirtual = true;
			}
		}
	}
	if ((mResult) || (mPropSrc != NULL))
		return;

	if (hadError != NULL)
		*hadError = true;
	Fail("Unable to find member", nameNode->mRight);
}

DbgType* DbgExprEvaluator::FindSubtype(DbgType* type, const StringImpl& name)
{
	for (auto subType : type->mSubTypeList)
	{
		if ((subType->mTypeName != NULL) && (strcmp(subType->mTypeName, name.c_str()) == 0))
			return subType;
	}
	for (auto baseType : type->mBaseTypes)
	{
		auto subType = FindSubtype(baseType->mBaseType->GetPrimaryType(), name);
		if (subType != NULL)
			return subType;
	}
	return NULL;
}

void DbgExprEvaluator::LookupQualifiedStaticField(BfQualifiedNameNode* nameNode, bool ignoreIdentifierNotFoundError)
{
	// Lookup left side as a type
	{
		DbgType* type;
		{
			SetAndRestoreValue<bool> prevIgnoreErro(mIgnoreErrors, true);
			type = ResolveTypeRef(nameNode->mLeft);
		}
		if (type != NULL)
		{
			DbgTypedValue lookupType;
			/*if (type->IsPrimitiveType())
				lookupType.mType = mModule->GetPrimitiveStructType(type));
			else*/
			lookupType.mType = type;
			lookupType.mHasNoValue = true;
			mResult = LookupField(nameNode->mRight, lookupType, nameNode->mRight->ToString());
			if ((mResult) || (mPropSrc != NULL))
				return;

			DbgType* fullType = NULL;
			//
			{
				SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
				fullType = ResolveTypeRef(nameNode);
				if ((fullType == NULL) && (!type->IsNamespace()))
				{
					String subName = nameNode->mRight->ToString();
					fullType = FindSubtype(type, subName);
				}
			}

			if (fullType != NULL)
			{
				mResult.mType = fullType;
				mResult.mHasNoValue = true;
				return;
			}

			Fail("Field not found", nameNode->mRight);
			return;
		}
	}

	String fieldName = nameNode->mRight->ToString();

	if (auto qualifiedLeftName = BfNodeDynCast<BfQualifiedNameNode>(nameNode->mLeft))
		LookupQualifiedStaticField(qualifiedLeftName);
	else
		VisitChild(nameNode->mLeft);
	GetResult();

	if (!mResult)
	{
		Fail("Identifier not found", nameNode->mLeft);
		return;
	}

	if (!mResult)
		return;

	auto checkType = mResult.mType->RemoveModifiers();
	if (checkType->IsPointer())
		checkType = checkType->mTypeParam;
	if ((checkType == NULL) || (!checkType->IsCompositeType()))
	{
		Fail("Type has no fields", nameNode->mLeft);
		return;
	}

	mResult = LookupField(nameNode, mResult, fieldName);
	if ((mResult) || (mPropSrc != NULL))
		return;

	Fail("Unable to find member", nameNode->mRight);
}

bool DbgExprEvaluator::EnsureRunning(BfAstNode* astNode)
{
	if (mDebugger->mIsRunning)
		return true;
	Fail("Target is not running", astNode);
	return false;
}

void DbgExprEvaluator::Visit(BfQualifiedNameNode* nameNode)
{
	AutocompleteCheckMemberReference(nameNode->mLeft, nameNode->mDot, nameNode->mRight);

	bool hadError = false;
	LookupQualifiedName(nameNode, true, &hadError);
	if ((mResult) || (mPropSrc != NULL))
		return;
	if (hadError)
		return;

	LookupQualifiedStaticField(nameNode);
}

void DbgExprEvaluator::Visit(BfDefaultExpression* defaultExpr)
{
	mIsComplexExpression = true;

	/*auto type = mModule->ResolveTypeRef(defaultExpr->mTypeRef);
	if (!type)
		return;
	mResult = DbgTypedValue(mModule->GetDefaultValue(type), type);*/
}

void DbgExprEvaluator::Visit(BfLiteralExpression* literalExpr)
{
	mIsComplexExpression = true;

	mResult.mIsLiteral = true;

	auto language = GetLanguage();
	switch (literalExpr->mValue.mTypeCode)
	{
	case BfTypeCode_NullPtr:
		{
			mResult.mPtr = 0;
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Null, GetLanguage());
		}
		break;
	case BfTypeCode_CharPtr:
		mResult = GetString(*literalExpr->mValue.mString);
		break;
	case BfTypeCode_Boolean:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		mResult.mBool = literalExpr->mValue.mBool;
		break;
	case BfTypeCode_Char8:
		mResult.mType = mDbgModule->GetPrimitiveType((language == DbgLanguage_Beef) ? DbgType_UChar : DbgType_SChar, language);
		mResult.mInt8 = literalExpr->mValue.mInt8;
		break;
	case BfTypeCode_Char16:
		mResult.mType = mDbgModule->GetPrimitiveType((language == DbgLanguage_Beef) ? DbgType_UChar16 : DbgType_SChar16, language);
		mResult.mInt16 = literalExpr->mValue.mInt16;
		break;
	case BfTypeCode_Char32:
		mResult.mType = mDbgModule->GetPrimitiveType((language == DbgLanguage_Beef) ? DbgType_UChar32 : DbgType_SChar32, language);
		mResult.mInt32 = literalExpr->mValue.mInt32;
		break;
	case BfTypeCode_Int8:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i8, GetLanguage());
		mResult.mUInt8 = literalExpr->mValue.mUInt8;
		break;
	case BfTypeCode_UInt8:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u8, GetLanguage());
		mResult.mUInt8 = literalExpr->mValue.mUInt8;
		break;
	case BfTypeCode_Int16:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i16, GetLanguage());
		mResult.mInt16 = literalExpr->mValue.mInt16;
		break;
	case BfTypeCode_UInt16:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u16, GetLanguage());
		mResult.mUInt16 = literalExpr->mValue.mUInt16;
		break;
	case BfTypeCode_Int32:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
		mResult.mInt32 = literalExpr->mValue.mInt32;
		break;
	case BfTypeCode_UInt32:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u32, GetLanguage());
		mResult.mUInt32 = literalExpr->mValue.mUInt32;
		break;
	case BfTypeCode_Int64:
	case BfTypeCode_IntUnknown:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i64, GetLanguage());
		mResult.mInt64 = literalExpr->mValue.mInt64;
		break;
	case BfTypeCode_UInt64:
	case BfTypeCode_UIntUnknown:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u64, GetLanguage());
		mResult.mUInt64 = literalExpr->mValue.mUInt64;
		break;
	case BfTypeCode_IntPtr:
		if (sizeof(addr_target) == 8)
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i64, GetLanguage());
			mResult.mInt64 = literalExpr->mValue.mInt64;
		}
		else
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
			mResult.mInt32 = literalExpr->mValue.mInt32;
		}
		break;
	case BfTypeCode_UIntPtr:
		if (sizeof(addr_target) == 8)
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u64, GetLanguage());
			mResult.mUInt64 = literalExpr->mValue.mUInt64;
		}
		else
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_u32, GetLanguage());
			mResult.mUInt32 = literalExpr->mValue.mUInt32;
		}
		break;
	case BfTypeCode_Float:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Single, GetLanguage());
		mResult.mSingle = literalExpr->mValue.mSingle;
		break;
	case BfTypeCode_Double:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Double, GetLanguage());
		mResult.mDouble = literalExpr->mValue.mDouble;
		break;
	default:
		Fail("Invalid literal", literalExpr);
		break;
	}
}

void DbgExprEvaluator::Visit(BfCastExpression* castExpr)
{
	mIsComplexExpression = true;

	if (castExpr->mTypeRef == NULL)
		return; // Error

	AutocompleteCheckType(castExpr->mTypeRef);
	auto resolvedType = ResolveTypeRef(castExpr->mTypeRef);
	if (resolvedType == NULL)
		return;

	if (mExplicitThisExpr != NULL)
		mDeferredInsertExplicitThisVector.push_back(NodeReplaceRecord(castExpr->mTypeRef, (BfAstNode**)&castExpr->mTypeRef));

	mResult = CreateValueFromExpression(castExpr->mExpression);
	if (!mResult)
		return;
	mResult = Cast(castExpr, mResult, resolvedType, true);
}

// We pass by reference to support "RAW" debug expression simplification
DbgTypedValue DbgExprEvaluator::CreateValueFromExpression(ASTREF(BfExpression*)& expr, DbgType* castToType, DbgEvalExprFlags flags)
{
	//
	{
		BP_ZONE("DbgExprEvaluator::CreateValueFromExpression:CheckStack");

		StackHelper stackHelper;
		if (!stackHelper.CanStackExpand(64 * 1024))
		{
			DbgTypedValue result;
			if (!stackHelper.Execute([&]()
			{
				result = CreateValueFromExpression(expr, castToType, flags);
			}))
			{
				Fail("Expression too complex", expr);
			}
			return result;
		}
	}

	mExpectingType = castToType;
	VisitChild(expr);
	GetResult();
	auto result = mResult;
	if ((result) && (castToType != NULL) && ((flags & DbgEvalExprFlags_NoCast) == 0))
		result = Cast(expr, result, castToType, false);
	mResult = DbgTypedValue();
	if ((result.mHasNoValue) && ((flags & DbgEvalExprFlags_AllowTypeResult) == 0))
		Fail("Value expected", expr);
	return result;
}

void DbgExprEvaluator::PerformBinaryOperation(ASTREF(BfExpression*)& leftExpression, ASTREF(BfExpression*)& rightExpression, BfBinaryOp binaryOp, BfTokenNode* opToken, bool forceLeftType)
{
	DbgTypedValue leftValue;
	if (leftExpression != NULL)
		leftValue = CreateValueFromExpression(leftExpression, mExpectingType, DbgEvalExprFlags_NoCast);
	DbgTypedValue rightValue;
	if (rightExpression == NULL)
	{
		return;
	}
	if (!leftValue)
	{
		VisitChild(rightExpression);
		return;
	}

	leftValue.mType = leftValue.mType->RemoveModifiers();

	/*if (leftValue.mType->IsRef())
		leftValue.mType = leftValue.mType->GetUnderlyingType();*/

	if ((binaryOp == BfBinaryOp_ConditionalAnd) || (binaryOp == BfBinaryOp_ConditionalOr))
	{
		bool isAnd = binaryOp == BfBinaryOp_ConditionalAnd;

		if (!leftValue.mType->IsBoolean())
		{
			Fail(StrFormat("Operator requires boolean operand. Left hand side is '%s", leftValue.mType->ToString().c_str()), opToken);
			return;
		}

		auto boolType = leftValue.mType;

		if (isAnd)
		{
			if (!leftValue.mBool)
			{
				mResult = leftValue;
				return;
			}

			rightValue = CreateValueFromExpression(rightExpression);
			if (rightValue)
				rightValue = Cast(rightExpression, rightValue, boolType);
			mResult = rightValue;
		}
		else
		{
			if (leftValue.mBool)
			{
				mResult = leftValue;
				return;
			}

			rightValue = CreateValueFromExpression(rightExpression);
			if (rightValue)
				rightValue = Cast(rightExpression, rightValue, boolType);
			mResult = rightValue;
		}
		return;
	}

	if ((binaryOp == BfBinaryOp_NullCoalesce) && ((leftValue.mType->IsPointer()) || (leftValue.mType->IsBfObject())))
	{
		if (leftValue.mPtr != 0)
		{
			mResult = leftValue;
			return;
		}

		CreateValueFromExpression(rightExpression, leftValue.mType);
		return;
	}

	rightValue = CreateValueFromExpression(rightExpression, mExpectingType, DbgEvalExprFlags_NoCast);
	if ((!leftValue) || (!rightValue))
		return;

	rightValue.mType = rightValue.mType->RemoveModifiers();

	if (leftValue.mType->IsTypedPrimitive())
		leftValue.mType = leftValue.mType->GetUnderlyingType();
	if (rightValue.mType->IsTypedPrimitive())
		rightValue.mType = rightValue.mType->GetUnderlyingType();

	// Prefer floats, prefer unsigned
	int leftCompareSize = leftValue.mType->GetByteCount();
	if (leftValue.mType->IsFloat())
		leftCompareSize += 16;
	if (!leftValue.mType->IsPrimitiveType())
		leftCompareSize += 0x100;
	if ((leftValue.mIsLiteral) && (leftValue.mType->IsPointer()))
		leftCompareSize += 0x200;

	int rightCompareSize = rightValue.mType->GetByteCount();
	if (rightValue.mType->IsFloat())
		rightCompareSize += 16;
	if (!rightValue.mType->IsPrimitiveType())
		rightCompareSize += 0x100;
	if ((rightValue.mIsLiteral) && (rightValue.mType->IsPointer()))
		rightCompareSize += 0x200;

	/*if ((leftValue.mType->IsTypedPrimitive()) && (rightValue.mType->IsTypedPrimitive()))
	{
		int leftInheritDepth = leftValue.mType->ToTypeInstance()->mInheritDepth;
		int rightInheritDepth = rightValue.mType->ToTypeInstance()->mInheritDepth;
		if (leftInheritDepth < rightInheritDepth)
		{
			// If both are typed primitives then choose the type with the lowest inherit depth
			//  so we will choose the base type when applicable
			forceLeftType = true;
		}
	}*/

	auto resultType = leftValue.mType;
	if (!forceLeftType)
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

	//bool explicitCast = false;
	bool explicitCast = true;
	DbgTypedValue* resultTypedValue;
	DbgTypedValue* otherTypedValue;
	DbgType* otherType;
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

	if ((resultTypedValue->mIsLiteral) && (resultType->IsPointer()))
	{
		// If we're comparing against a string literal like 'str == "Hey!"', handle that
		if (BfBinOpEqualityCheck(binaryOp))
		{
			DbgType* useType = otherType;
			useType = useType->RemoveModifiers();
			if (useType->IsPointer())
				useType = useType->mTypeParam;

			DbgTypedValue useTypedValue = *otherTypedValue;
			if (useTypedValue.mType->IsPointer())
			{
				useTypedValue.mSrcAddress = useTypedValue.mPtr;
				useTypedValue.mType = useTypedValue.mType->mTypeParam;
			}

			auto language = useType->GetLanguage();
			auto dbgCompileUnit = useType->mCompileUnit;
			auto dbgModule = useType->GetDbgModule();
			Array<String> dbgVisWildcardCaptures;
			auto debugVis = mDebugger->FindVisualizerForType(useType, &dbgVisWildcardCaptures);
			if (debugVis != NULL)
			{
				auto& displayStringList = debugVis->mStringViews;
				String displayString;
				for (auto displayEntry : displayStringList)
				{
					auto& displayStringList = debugVis->mStringViews;

					DwFormatInfo formatInfo;
					formatInfo.mRawString = true;
					formatInfo.mLanguage = language;
					if (!displayEntry->mCondition.empty())
					{
						if (!mDebugger->EvalCondition(debugVis, dbgCompileUnit, useTypedValue, formatInfo, displayEntry->mCondition, dbgVisWildcardCaptures, displayString))
							continue;
					}

					String displayStr = mDebugger->mDebugManager->mDebugVisualizers->DoStringReplace(displayEntry->mString, dbgVisWildcardCaptures);
					mDebugger->ProcessEvalString(dbgCompileUnit, useTypedValue, displayStr, displayString, formatInfo, debugVis, false);

					bool isEq = displayString == resultTypedValue->mCharPtr;

					auto boolType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
					mResult.mType = boolType;
					mResult.mBool = isEq == ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality));
					return;
				}
			}
		}

		Fail("Cannot perform operation with a literal value", resultTypeSrc);
		return;
	}

	DbgTypedValue convLeftValue;
	DbgTypedValue convRightValue;

	/*if ((resultType->IsVar()) || (otherType->IsVar()))
	{
		mResult = DbgTypedValue(mModule->GetDefaultValue(resultType), resultType);
		return;
	}*/

	if ((otherType->IsNull()) && BfBinOpEqualityCheck(binaryOp))
	{
		bool isEquality = (binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality);

		if (resultType->IsValueType())
		{
			/*if (!mModule->IsInSpecializedGeneric())
			{
				//CS0472: The result of the expression is always 'true' since a value of type 'int' is never equal to 'null' of type '<null>'
				mModule->mCompiler->mPassInstance->Warn(BfWarning_CS0472_ValueTypeNullCompare,
					StrFormat("The result of the expression is always '%s' since a value of type '%s' can never be null",
						isEquality ? "false" : "true", mModule->TypeToString(resultType).c_str()), otherTypeSrc);
			}*/

			// Valuetypes never equal null
			auto boolType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			mResult.mType = boolType;
			mResult.mBool = !isEquality;
			return;
		}
	}

	//TODO: Use operator methods?
	/*if ((leftValue.mType->IsTypeInstance()) || (rightValue.mType->IsTypeInstance()))
	{
		SmallVector<BfResolvedArg, 2> args;
		BfResolvedArg leftArg;
		leftArg.mExpression = leftExpression;
		leftArg.mTypedValue = leftValue;
		args.push_back(leftArg);
		BfResolvedArg rightArg;
		rightArg.mExpression = rightExpression;
		rightArg.mTypedValue = rightValue;
		args.push_back(rightArg);
		BfMethodMatcher methodMatcher(opToken, mModule, "", args, NULL);
		BfBaseClassWalker baseClassWalker(leftValue.mType->ToTypeInstance(), rightValue.mType->ToTypeInstance());
		bool invertResult = false;

		while (auto checkType = baseClassWalker.Next())
		{
			BfOperatorDef* oppositeOperatorDef = NULL;
			BfBinaryOp oppositeBinaryOp = GetOppositeBinaryOp(binaryOp);

			for (auto operatorDef : checkType->mTypeDef->mOperators)
			{
				if (operatorDef->mOperatorDeclaration->mBinOp == binaryOp)
					methodMatcher.CheckMethod(checkType, operatorDef);
				else if (operatorDef->mOperatorDeclaration->mBinOp == oppositeBinaryOp)
					oppositeOperatorDef = operatorDef;
			}

			if ((methodMatcher.mBestMethodDef == NULL) && (oppositeOperatorDef != NULL))
			{
				if (methodMatcher.CheckMethod(checkType, oppositeOperatorDef))
					invertResult = true;
			}
		}
		if (methodMatcher.mBestMethodDef != NULL)
		{
			SetElementType(opToken, BfSourceElementType_Method);
			mResult = CreateCall(&methodMatcher, DbgTypedValue());
			if ((invertResult) && (mResult.mType == mModule->GetPrimitiveType(BfTypeCode_Boolean)))
				mResult.mValue = mModule->mIRBuilder->CreateNot(mResult.mValue);
			return;
		}
	}*/

	if (resultType->IsPointer() && otherType->IsPointer())
	{
		//TODO: Allow all pointer comparisons, but only allow SUBTRACTION between equal pointer types
		if (binaryOp == BfBinaryOp_Subtract)
		{
			if (!resultType->Equals(otherType))
			{
				Fail(StrFormat("Operands must be the same type, '%s' doesn't match '%s'",
					leftValue.mType->ToString().c_str(), rightValue.mType->ToString().c_str()),
					opToken);
				return;
			}

			DbgType* intPtrType = mDbgModule->GetPrimitiveType(DbgType_IntPtr_Alias, GetLanguage());
			long ptrDiff = leftValue.mPtr - rightValue.mPtr;
			int elemSize = resultType->mTypeParam->GetStride();
			if (elemSize == 0)
			{
				Fail("Invalid operation on void values", opToken);
				return;
			}
			//BF_ASSERT((ptrDiff % elemSize) == 0);
			mResult.mInt64 = ptrDiff / elemSize;
			mResult.mType = intPtrType;
			return;
		}
		else if ((binaryOp != BfBinaryOp_Equality) && (binaryOp != BfBinaryOp_StrictEquality) &
			(binaryOp != BfBinaryOp_InEquality) && (binaryOp != BfBinaryOp_StrictInEquality) &&
			(binaryOp != BfBinaryOp_LessThan) && (binaryOp != BfBinaryOp_LessThanOrEqual) &&
			(binaryOp != BfBinaryOp_GreaterThan) && (binaryOp != BfBinaryOp_GreaterThanOrEqual))
		{
			Fail("Invalid operation on pointers", opToken);
			return;
		}

		// Compare them as ints
		resultType = mDbgModule->GetPrimitiveType(DbgType_UIntPtr_Alias, GetLanguage());
		explicitCast = true;
	}
	else if (resultType->IsPointer())
	{
		if (otherType->IsNull())
		{
			if (!BfBinOpEqualityCheck(binaryOp))
			{
				Fail(StrFormat("Invalid operation between '%s' and null", resultType->ToString().c_str()), opToken);
				return;
			}

			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			if (binaryOp == BfBinaryOp_Equality)
				mResult.mBool = resultTypedValue->mPtr == NULL;
			else
				mResult.mBool = resultTypedValue->mPtr != NULL;
			return;
		}

		bool isCompare = (binaryOp >= BfBinaryOp_Equality) && (binaryOp <= BfBinaryOp_LessThanOrEqual);

		// One pointer
		if ((!otherType->IsInteger()) ||
			((binaryOp != BfBinaryOp_Add) && (binaryOp != BfBinaryOp_Subtract) && (binaryOp != BfBinaryOp_OverflowAdd) && (binaryOp != BfBinaryOp_OverflowSubtract) && (!isCompare)))
		{
			Fail("Can only add or subtract integer values from pointers", rightExpression);
			return;
		}

		if ((binaryOp == BfBinaryOp_Add) || (binaryOp == BfBinaryOp_Subtract) || (binaryOp == BfBinaryOp_OverflowAdd) || (binaryOp == BfBinaryOp_OverflowSubtract))
		{
			auto underlyingType = otherType->GetUnderlyingType();
			mResult.mType = resultType;

			DbgType* innerType = resultType->mTypeParam;
			if (binaryOp == BfBinaryOp_Subtract)
			{
				if (resultTypeSrc == rightExpression)
					Fail("Cannot subtract a pointer from an integer", resultTypeSrc);

				mResult.mPtr = resultTypedValue->mPtr - otherTypedValue->GetInt64() * innerType->GetStride();
			}
			else
			{
				mResult.mPtr = resultTypedValue->mPtr + otherTypedValue->GetInt64() * innerType->GetStride();
			}

			if (innerType->GetByteCount() == 0)
			{
				Warn("Adding to a pointer to a zero-sized element has no effect", opToken);
			}

			return;
		}
	}

	if ((resultType->IsPointer()) || (resultType->IsBfObject()) || (resultType->IsInterface()) /*|| (resultType->IsGenericParam())*/)
	{
		if (!BfBinOpEqualityCheck(binaryOp))
		{
			if (resultType->IsPointer())
				Fail("Invalid operation for pointers", opToken);
			else
				Fail("Invalid operation for objects", opToken);
			return;
		}

		if (otherType->IsNull())
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
				mResult.mBool = resultTypedValue->mPtr == NULL;
			else
				mResult.mBool = resultTypedValue->mPtr != NULL;
		}
		else
		{
			bool explicitCast = true;
			if (otherType->IsInteger())
				explicitCast = true;

			auto convertedValue = Cast(otherTypeSrc, *otherTypedValue, resultType, explicitCast);
			if (!convertedValue)
				return;

			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_StrictEquality))
				mResult.mBool = resultTypedValue->mPtr == convertedValue.mPtr;
			else
				mResult.mBool = resultTypedValue->mPtr != convertedValue.mPtr;
		}

		return;
	}
	/*else if (resultType->IsTypedPrimitive())
	{
		bool needsOtherCast = true;
		if (otherType != resultType)
		{
			if (otherType->IsPrimitiveType())
			{
				// Allow zero comparisons to match all typed primitives
				if ((binaryOp == BfBinaryOp_Equality) || (binaryOp == BfBinaryOp_InEquality))
				{
					auto intConstant = dyn_cast<ConstantInt>(otherTypedValue->mValue);
					if (intConstant != NULL)
					{
						if (intConstant->getSExtValue() == 0)
						{
							needsOtherCast = false;
						}
					}
				}
			}

			if (needsOtherCast)
			{
				// The only purpose of this cast is to potentially throw a casting error
				Value* otherCastResult = mModule->CastToValue(otherTypeSrc, *otherTypedValue, resultType, explicitCast);
				if (otherCastResult == NULL)
					return;
			}
		}

		auto underlyingType = resultType->GetUnderlyingType();

		Value* convResultValue = mModule->CastToValue(resultTypeSrc, *resultTypedValue, underlyingType, true);
		Value* convOtherValue = mModule->CastToValue(otherTypeSrc, *otherTypedValue, underlyingType, true);

		if ((!underlyingType->IsValuelessType()) && ((convResultValue == NULL) || (convOtherValue == NULL)))
			return;

		if (resultTypedValue == &leftValue)
			PerformBinaryOperation(underlyingType, convResultValue, convOtherValue, binaryOp, opToken);
		else
			PerformBinaryOperation(underlyingType, convOtherValue, convResultValue, binaryOp, opToken);
		if (mResult.mType == underlyingType)
			mResult.mType = resultType;
		return;
	}*/
	else if (((leftValue.mType->IsStruct()) || (leftValue.mType->IsBfObject())) && (!resultType->IsEnum()))
	{
		if (leftValue.mType == rightValue.mType)
		{
			Fail(StrFormat("Operator '%s' cannot be applied to operands of type '%s'",
				opToken->ToString().c_str(),
				TypeToString(leftValue.mType).c_str()), opToken);
		}
		else
		{
			Fail(StrFormat("Operator '%s' cannot be applied to operands of type '%s' and '%s'",
				opToken->ToString().c_str(),
				TypeToString(leftValue.mType).c_str(),
				TypeToString(rightValue.mType).c_str()), opToken);
		}
		return;
	}

	if ((resultType->IsInteger()) && (!forceLeftType))
	{
		bool isBitwiseExpr =
			(binaryOp == BfBinaryOp_BitwiseAnd) |
			(binaryOp == BfBinaryOp_BitwiseOr) |
			(binaryOp == BfBinaryOp_ExclusiveOr) |
			(binaryOp == BfBinaryOp_LeftShift) |
			(binaryOp == BfBinaryOp_RightShift);

		// For binary expressions we don't promote to 'int' unless the types don't match
		if ((!isBitwiseExpr) || (leftValue.mType != rightValue.mType))
		{
			if (binaryOp == BfBinaryOp_Subtract)
			{
			}
			else if (resultType->GetByteCount() < 4)
			{
				resultType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
			}
			else if (leftValue.mType != rightValue.mType)
			{
				if ((binaryOp == BfBinaryOp_LeftShift) || (binaryOp == BfBinaryOp_RightShift))
				{
					// For shifts we leave it in the target type if we're already at 'int' or higher
				}
				else if ((!resultType->IsSigned()) && (otherType->IsSigned()))
				{
					/*if (CanCast(*otherTypedValue, resultType))
					{
						// If we can convert the 'other' value implicitly then it's a convertible literal, leave as uint
					}
					else if (resultType->mSize == 4)
					{
						resultType = mDbgModule->GetPrimitiveType(DbgType_i64);
					}
					else
					{
						Fail(StrFormat("Operator cannot be applied to operands of type '%s' and '%s'",
							TypeToString(leftValue.mType).c_str(),
							TypeToString(rightValue.mType).c_str()), opToken);
						return;
					}*/

					explicitCast = true;
				}
			}
		}
	}

	if (convLeftValue == NULL)
		convLeftValue = Cast(leftExpression, leftValue, resultType, explicitCast);
	if (convRightValue == NULL)
		convRightValue = Cast(rightExpression, rightValue, resultType, explicitCast);

	PerformBinaryOperation(resultType, convLeftValue, convRightValue, binaryOp, opToken);
}

void DbgExprEvaluator::PerformBinaryOperation(DbgType* resultType, DbgTypedValue convLeftValue, DbgTypedValue convRightValue, BfBinaryOp binaryOp, BfTokenNode* opToken)
{
	if (resultType->IsValuelessType())
	{
		switch (binaryOp)
		{
		case BfBinaryOp_Equality:
		case BfBinaryOp_StrictEquality:
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			mResult.mBool = true;
			break;
		case BfBinaryOp_InEquality:
		case BfBinaryOp_StrictInEquality:
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			mResult.mBool = false;
			break;
		default:
			Fail("Invalid operation for void", opToken);
			break;
		}
		return;
	}

	if ((convLeftValue == NULL) || (convRightValue == NULL))
		return;

	if ((resultType->IsEnum()) || (resultType->IsTypedPrimitive()))
		resultType = resultType->GetUnderlyingType();

	if (resultType->IsPrimitiveType())
	{
		auto primType = resultType;
		if (primType->mTypeCode == DbgType_Bool)
		{
			mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			switch (binaryOp)
			{
			case BfBinaryOp_Equality:
			case BfBinaryOp_StrictEquality:
				mResult.mBool = convLeftValue.mBool == convRightValue.mBool;
				break;
			case BfBinaryOp_InEquality:
			case BfBinaryOp_StrictInEquality:
				mResult.mBool = convLeftValue.mBool != convRightValue.mBool;
				break;
			case BfBinaryOp_ConditionalAnd:
				mResult.mBool = convLeftValue.mBool && convRightValue.mBool;
				break;
			case BfBinaryOp_ConditionalOr:
				mResult.mBool = convLeftValue.mBool | convRightValue.mBool;
				break;
			case BfBinaryOp_ExclusiveOr:
				mResult.mBool = convLeftValue.mBool ^ convRightValue.mBool;
				break;
			default:
				Fail("Invalid operation for booleans", opToken);
				break;
			}
			return;
		}
	}

	if ((!resultType->IsIntegral()) && (!resultType->IsFloat()))
	{
		Fail(StrFormat("Cannot perform operation on type '%s'", TypeToString(resultType).c_str()), opToken);
		return;
	}

	mResult.mType = resultType;
	if (resultType->IsInteger())
	{
		switch (binaryOp)
		{
		case BfBinaryOp_BitwiseAnd:
			mResult.mInt64 = convLeftValue.GetInt64() & convRightValue.GetInt64();
			return;
		case BfBinaryOp_BitwiseOr:
			mResult.mInt64 = convLeftValue.GetInt64() | convRightValue.GetInt64();
			return;
		case BfBinaryOp_ExclusiveOr:
			mResult.mInt64 = convLeftValue.GetInt64() ^ convRightValue.GetInt64();
			return;
		case BfBinaryOp_LeftShift:
			mResult.mInt64 = convLeftValue.GetInt64() << convRightValue.GetInt64();
			return;
		case BfBinaryOp_RightShift:
			mResult.mInt64 = convLeftValue.GetInt64() >> convRightValue.GetInt64();
			return;
		}
	}

	switch (binaryOp)
	{
	case BfBinaryOp_Add:
	case BfBinaryOp_OverflowAdd:
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mSingle = convLeftValue.mSingle + convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mDouble = convLeftValue.mDouble + convRightValue.mDouble;
		else
			mResult.mInt64 = convLeftValue.GetInt64() + convRightValue.GetInt64();
		break;
	case BfBinaryOp_Subtract:
	case BfBinaryOp_OverflowSubtract:
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mSingle = convLeftValue.mSingle - convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mDouble = convLeftValue.mDouble - convRightValue.mDouble;
		else
			mResult.mInt64 = convLeftValue.GetInt64() - convRightValue.GetInt64();
		break;
	case BfBinaryOp_Multiply:
	case BfBinaryOp_OverflowMultiply:
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mSingle = convLeftValue.mSingle * convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mDouble = convLeftValue.mDouble * convRightValue.mDouble;
		else
			mResult.mInt64 = convLeftValue.GetInt64() * convRightValue.GetInt64();
		break;
	case BfBinaryOp_Divide:
		{
			bool isZero = false;
			if (resultType->mTypeCode == DbgType_Single)
			{
				mResult.mSingle = convLeftValue.mSingle / convRightValue.mSingle;
			}
			else if (resultType->mTypeCode == DbgType_Double)
			{
				mResult.mDouble = convLeftValue.mDouble / convRightValue.mDouble;
			}
			else
			{
				if (convRightValue.GetInt64() == 0)
					isZero = true;
				else
					mResult.mInt64 = convLeftValue.GetInt64() / convRightValue.GetInt64();
			}
			if (isZero)
				Fail("Divide by zero", opToken);
		}
		break;
	case BfBinaryOp_Modulus:
		{
			bool isZero = false;
			if (resultType->mTypeCode == DbgType_Single)
				mResult.mSingle = fmod(convLeftValue.mSingle, convRightValue.mSingle);
			else if (resultType->mTypeCode == DbgType_Double)
				mResult.mDouble = fmod(convLeftValue.mDouble, convRightValue.mDouble);
			else
			{
				if (convRightValue.GetInt64() == 0)
					isZero = true;
				else
					mResult.mInt64 = convLeftValue.GetInt64() % convRightValue.GetInt64();
			}
			if (isZero)
				Fail("Divide by zero", opToken);
		}
		break;
	case BfBinaryOp_Equality:
	case BfBinaryOp_StrictEquality:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle == convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble == convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() == convRightValue.GetInt64();
		break;
	case BfBinaryOp_InEquality:
	case BfBinaryOp_StrictInEquality:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle != convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble != convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() != convRightValue.GetInt64();
		break;
	case BfBinaryOp_LessThan:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle < convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble < convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() < convRightValue.GetInt64();
		break;
	case BfBinaryOp_LessThanOrEqual:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle <= convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble <= convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() <= convRightValue.GetInt64();
		break;
	case BfBinaryOp_GreaterThan:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle > convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble > convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() > convRightValue.GetInt64();
		break;
	case BfBinaryOp_GreaterThanOrEqual:
		mResult.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
		if (resultType->mTypeCode == DbgType_Single)
			mResult.mBool = convLeftValue.mSingle >= convRightValue.mSingle;
		else if (resultType->mTypeCode == DbgType_Double)
			mResult.mBool = convLeftValue.mDouble >= convRightValue.mDouble;
		else
			mResult.mBool = convLeftValue.GetInt64() >= convRightValue.GetInt64();
		break;
	default:
		Fail("Invalid operation", opToken);
		break;
	}
}

void DbgExprEvaluator::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	//mIsComplexExpression = true;
	//PerformBinaryOperation(binOpExpr->mLeft, binOpExpr->mRight, binOpExpr->mOp, binOpExpr->mOpToken, false);

	// Check for "(double)-1.0" style cast, misidentified as a binary operation
	if ((binOpExpr->mOp == BfBinaryOp_Add) || (binOpExpr->mOp == BfBinaryOp_Subtract) ||
		(binOpExpr->mOp == BfBinaryOp_BitwiseAnd) || (binOpExpr->mOp == BfBinaryOp_Multiply))
	{
		if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(binOpExpr->mLeft))
		{
			BfAstNode* castTypeExpr = BfNodeDynCast<BfIdentifierNode>(parenExpr->mExpression);
			if (castTypeExpr == NULL)
				castTypeExpr = BfNodeDynCast<BfMemberReferenceExpression>(parenExpr->mExpression);
			if (castTypeExpr != NULL)
			{
				SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
				auto resolvedType = ResolveTypeRef(castTypeExpr);
				prevIgnoreError.Restore();

				if (resolvedType != NULL)
				{
					if (binOpExpr->mOp == BfBinaryOp_BitwiseAnd)
					{
						PerformUnaryExpression(binOpExpr->mOpToken, BfUnaryOp_AddressOf, binOpExpr->mRight);
					}
					else if (binOpExpr->mOp == BfBinaryOp_Multiply)
					{
						PerformUnaryExpression(binOpExpr->mOpToken, BfUnaryOp_Dereference, binOpExpr->mRight);
					}
					else
					{
						VisitChild(binOpExpr->mRight);
						if (!mResult)
							return;
					}

					if ((mResult) && (binOpExpr->mOp == BfBinaryOp_Subtract))
					{
						if (mResult.mType->mTypeCode == DbgType_Single)
							mResult.mSingle = -mResult.mSingle;
						else if (mResult.mType->mTypeCode == DbgType_Double)
							mResult.mDouble = -mResult.mDouble;
						else
							mResult.mInt64 = -mResult.GetInt64();
					}

					mResult = Cast(binOpExpr, mResult, resolvedType, true);
					return;
				}
			}
		}
	}

	if (binOpExpr->mRight == NULL)
	{
		//mModule->AssertErrorState();

		// We visit the children for autocompletion only
		if (binOpExpr->mLeft != NULL)
			VisitChild(binOpExpr->mLeft);

		if (mResult)
		{
			//if (mAutoComplete != NULL)
				//mAutoComplete->CheckEmptyStart(binOpExpr->mOpToken, mResult.mType);
		}

		if (binOpExpr->mRight != NULL)
			VisitChild(binOpExpr->mRight);
		return;
	}

	PerformBinaryOperation(binOpExpr->mLeft, binOpExpr->mRight, binOpExpr->mOp, binOpExpr->mOpToken, false);
}

void DbgExprEvaluator::PerformUnaryExpression(BfAstNode* opToken, BfUnaryOp unaryOp, ASTREF(BfExpression*)& expr)
{
	if (unaryOp != BfUnaryOp_Dereference)
		mIsComplexExpression = true;

	VisitChild(expr);
	GetResult();
	if (!mResult)
		return;

	bool wasLiteral = mResult.mIsLiteral;
	mResult.mIsLiteral = false;

	switch (unaryOp)
	{
	case BfUnaryOp_Decrement:
	case BfUnaryOp_PostDecrement:
	case BfUnaryOp_Increment:
	case BfUnaryOp_PostIncrement:
		auto ptr = mResult;
		ptr.mType = ptr.mType->RemoveModifiers();

		if (ptr.mType->IsEnum())
			ptr.mType = ptr.mType->mTypeParam;

		auto val = mResult;
		int add = ((unaryOp == BfUnaryOp_Decrement) || (unaryOp == BfUnaryOp_PostDecrement)) ? -1 : 1;
		switch (ptr.mType->mTypeCode)
		{
		case DbgType_UChar:
		case DbgType_SChar:
		case DbgType_i8:
		case DbgType_u8:
		case DbgType_Utf8:
			val.mUInt8 += add;
			break;
		case DbgType_UChar16:
		case DbgType_SChar16:
		case DbgType_i16:
		case DbgType_u16:
		case DbgType_Utf16:
			val.mUInt16 += add;
			break;
		case DbgType_UChar32:
		case DbgType_SChar32:
		case DbgType_i32:
		case DbgType_u32:
		case DbgType_Utf32:
			val.mUInt32 += add;
			break;
		case DbgType_i64:
		case DbgType_u64:
			val.mUInt64 += add;
			break;
		case DbgType_Single:
			val.mSingle += add;
			break;
		case DbgType_Double:
			val.mDouble += add;
			break;
		default:
			Fail(StrFormat("Unable to perform operation on type '%s'", ptr.mType->ToString()), expr);
			return;
		}

		if ((mExpressionFlags & DwEvalExpressionFlag_AllowSideEffects) == 0)
		{
			mBlockedSideEffects = true;
			return;
		}
		else
		{
			StoreValue(mResult, val, opToken);
		}

		if ((unaryOp == BfUnaryOp_Decrement) || (unaryOp == BfUnaryOp_Increment))
			mResult = val;
		return;
	}

	bool numericFail = false;
	switch (unaryOp)
	{
	case BfUnaryOp_Dereference:
		{
			auto type = mResult.mType->RemoveModifiers();
			if (!type->IsPointer())
			{
				Fail("Operator can only be used on pointer values", opToken);
				return;
			}
			mResult = ReadTypedValue(opToken, type->mTypeParam, mResult.mPtr, DbgAddrType_Target);
		}
		break;
	case BfUnaryOp_AddressOf:
		{
			if ((mResult.mSrcAddress == 0) || (mResult.mSrcAddress == -1))
			{
				if (mResult.mRegNum != -1)
				{
					Fail(StrFormat("Cannot take address of register '%s'", CPURegisters::GetRegisterName(mResult.mRegNum)), opToken);
					return;
				}

				Fail("Cannot take address of value", opToken);
				return;
			}

			mResult.mType = mResult.mType->RemoveModifiers();

			mResult.mType = mDbgModule->GetPointerType(mResult.mType);
			mResult.mPtr = mResult.mSrcAddress;
			mResult.mSrcAddress = 0;
		}
		break;
	case BfUnaryOp_Not:
		{
			auto boolType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
			if (mResult.mType->mTypeCode != DbgType_Bool)
			{
				Fail("Operator can only be used on boolean values", opToken);
				return;
			}
			mResult.mIsLiteral = wasLiteral;
			mResult.mBool = !mResult.mBool;
		}
		break;
	case BfUnaryOp_Negate:
		{
			if (mResult.mType->IsInteger())
			{
				auto primType = mResult.mType;
				auto wantType = primType;

				if (wasLiteral)
				{
					// This is a special case where the user entered -0x80000000 (maxint) but we thought "0x80000000" was a uint in the parser
					//  which would get expanded to an int64 for this negate.  Properly bring back down to an int32
					if ((primType->mTypeCode == DbgType_u32) && (mResult.GetInt64() == -0x80000000LL))
					{
						//mResult = DbgTypedValue(mModule->GetConstValue((int) i64Val), mModule->GetPrimitiveType(DwTypeCode_Int32));
						mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
						return;
					}
				}

				if (!primType->IsSigned())
				{
					if (primType->mSize == 1)
						wantType = mDbgModule->GetPrimitiveType(DbgType_i16, GetLanguage());
					else if (primType->mSize == 2)
						wantType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
					else if (primType->mSize == 4)
						wantType = mDbgModule->GetPrimitiveType(DbgType_i64, GetLanguage());
				}

				mResult.mIsLiteral = wasLiteral;
				mResult.mInt64 = -mResult.GetInt64();
				mResult.mType = wantType;
				return;
			}

			mResult.mIsLiteral = wasLiteral;
			if (mResult.mType->mTypeCode == DbgType_Single)
				mResult.mSingle = -mResult.mSingle;
			else if (mResult.mType->mTypeCode == DbgType_Double)
				mResult.mDouble = -mResult.mDouble;
			else
				numericFail = true;
		}
		break;
	case BfUnaryOp_InvertBits:
		{
			mResult.mIsLiteral = wasLiteral;
			if (mResult.mType->IsInteger())
			{
				switch (mResult.mType->mSize)
				{
				case 1:
					mResult.mUInt8 = ~mResult.mUInt8;
					break;
				case 2:
					mResult.mUInt16 = ~mResult.mUInt16;
					break;
				case 4:
					mResult.mUInt32 = ~mResult.mUInt32;
					break;
				case 8:
					mResult.mUInt64 = ~mResult.mUInt64;
					break;
				}
			}
		}
		break;
	default:
		Fail("Invalid operator for debug expression", opToken);
		break;
	}

	if (numericFail)
	{
		Fail("Operator can only be used on numeric types", opToken);
		mResult = DbgTypedValue();
	}
}

void DbgExprEvaluator::Visit(BfUnaryOperatorExpression* unaryOpExpr)
{
	PerformUnaryExpression(unaryOpExpr->mOpToken, unaryOpExpr->mOp, unaryOpExpr->mExpression);
}

bool DbgExprEvaluator::ResolveArgValues(const BfSizedArray<ASTREF(BfExpression*)>& arguments, SizedArrayImpl<DbgTypedValue>& outArgValues)
{
	for (int argIdx = 0; argIdx < (int) arguments.size(); argIdx++)
	{
		//BfExprEvaluator exprEvaluator(mModule);
		//exprEvaluator.Evaluate(arguments[argIdx]);
		auto arg = arguments[argIdx];
		DbgTypedValue argValue;
		if (arg != NULL)
			argValue = Resolve(arg);
		if (argValue)
		{
			/*if (argValue.mType->IsRef())
			{
				argValue.mIsAddr = false;
			}
			else if (!argValue.mType->IsStruct())
				argValue = mModule->LoadValue(argValue);*/
		}
		outArgValues.push_back(argValue);
	}
	return true;
}

DbgTypedValue DbgExprEvaluator::CreateCall(DbgSubprogram* method, DbgTypedValue thisVal, DbgTypedValue structRetVal, bool bypassVirtual, CPURegisters* registers)
{
	// Why did we have the Not Runing thing for Exception?  It means that when we crash we can't execute functions anymore.
	//  That doesn't seem good
	/*if (mDebugger->mRunState == RunState_Exception)
	{
	   	mPassInstance->Fail("Not running");
		return DbgTypedValue();
	}*/

#ifdef BF_WANTS_LOG_DBGEXPR
	auto _GetResultString = [&](DbgTypedValue val)
	{
		String result;
		if (val.mSrcAddress != 0)
		{
			result += StrFormat("0x%p ", val.mSrcAddress);

			int64 vals[4] = { 0 };
			mDebugger->ReadMemory(val.mSrcAddress, 4 * 8, vals);

			result += StrFormat("%lld %lld %lld %lld", vals[0], vals[1], vals[2], vals[3]);
		}
		else
		{
			result += StrFormat("%lld", val.mInt64);
		}
		return result;
	};
#endif

	int curCallResultIdx = mCallResultIdx++;
	if (((mExpressionFlags & DwEvalExpressionFlag_AllowCalls) == 0) || (mCreatedPendingCall))
	{
		BfLogDbgExpr(" BlockedSideEffects\n");

		mBlockedSideEffects = true;
		return GetDefaultTypedValue(method->mReturnType);
	}
	mHadSideEffects = true;

	// Our strategy is to create "pending results" for each call in a debug expression, and then we continually re-evaluate the
	//  entire expression using the actual returned results for each of those calls until we can finally evaluate it as a whole
	if (curCallResultIdx < (int)mCallResults->size() - 1)
	{
		auto callResult = (*mCallResults)[curCallResultIdx];
		auto result = callResult.mResult;

		if (!callResult.mSRetData.IsEmpty())
		{
			result = structRetVal;
			mDebugger->WriteMemory(result.mSrcAddress, &callResult.mSRetData[0], (int)callResult.mSRetData.size());
		}

		BfLogDbgExpr(" using cached results %s\n", _GetResultString(result).c_str());

		return result;
	}
	else if (curCallResultIdx == (int)mCallResults->size() - 1)
	{
		auto& callResult = (*mCallResults)[curCallResultIdx];

		CPURegisters newPhysRegisters;
		mDebugger->PopulateRegisters(&newPhysRegisters);

		DbgTypedValue returnVal;
		if (method->mReturnType != 0)
		{
			if (callResult.mStructRetVal)
				returnVal = callResult.mStructRetVal;
			else
				returnVal = mDebugger->ReadReturnValue(&newPhysRegisters, method->mReturnType);
			bool hadRef = false;
			auto returnType = method->mReturnType->RemoveModifiers(&hadRef);
			if (hadRef)
			{
				returnVal = ReadTypedValue(NULL, returnType, returnVal.mPtr, DbgAddrType_Target);
				returnVal.mType = returnType;
			}
		}
		else
		{
			returnVal.mType = mDbgModule->GetPrimitiveType(DbgType_Void, GetLanguage());
		}

		BfLogDbgExpr(" using new results %s\n", _GetResultString(returnVal).c_str());

		mDebugger->RestoreAllRegisters();

		if ((method->mReturnType->IsCompositeType()) && (mDebugger->CheckNeedsSRetArgument(method->mReturnType)))
		{
			callResult.mSRetData.Resize(method->mReturnType->GetByteCount());
			mDebugger->ReadMemory(returnVal.mSrcAddress, method->mReturnType->GetByteCount(), &callResult.mSRetData[0]);
		}
		callResult.mResult = returnVal;

		return returnVal;
	}
	else
	{
		BfLogDbgExpr(" new call\n");
	}

	// It's a new call

	addr_target startAddr = method->mBlock.mLowPC;
	if ((method->mVTableLoc != -1) && (thisVal))
	{
		addr_target thisPtr;
		bool isBfObject = false;
		if (thisVal.mType->IsCompositeType())
		{
			isBfObject = thisVal.mType->IsBfObject();
			thisPtr = thisVal.mSrcAddress;
		}
		else
		{
			isBfObject = thisVal.mType->IsBfObjectPtr();
			thisPtr = thisVal.mPtr;
		}
		addr_target vtablePtr = mDebugger->ReadMemory<addr_target>(thisPtr);
		if (isBfObject)
		{
			mDebugTarget->GetCompilerSettings();
			if (mDebugTarget->mBfObjectHasFlags)
				vtablePtr &= ~0xFF;
		}
		if (method->mVTableLoc >= 0x100000)
		{
			// Virtual ext method, double indirection
			addr_target extAddr = mDebugger->ReadMemory<addr_target>(vtablePtr + (uint)((method->mVTableLoc>>20) - 1)*sizeof(addr_target));
			startAddr = mDebugger->ReadMemory<addr_target>(extAddr + (uint)(method->mVTableLoc & 0xFFFFF)*sizeof(addr_target));
		}
		else
			startAddr = mDebugger->ReadMemory<addr_target>(vtablePtr + method->mVTableLoc);
	}
	else
	{
		if ((startAddr == 0) && (method->mLinkName != NULL))
		{
			startAddr = mDebugTarget->FindSymbolAddr(method->mLinkName);
		}
		else if ((startAddr == 0) && (method->mName != NULL))
		{
			startAddr = mDebugTarget->FindSymbolAddr(method->mName);
		}
	}

	if ((startAddr == (addr_target)0) || (startAddr == (addr_target)-1))
	{
		mPassInstance->Fail("Unable to find address for method, possibly due to compiler optimizations.");
		return DbgTypedValue();
	}

	mDebugger->SaveAllRegisters();

	BfLogDbg("Starting RunState_DebugEval on thread %d\n", mDebugger->mActiveThread->mThreadId);

	addr_target prevSP = registers->GetSP();

	//registers->mIntRegs.efl = 0x244;

	//mDebugger->PushValue(registers, 0);
	mDebugger->PushValue(registers, 42);

	*(registers->GetPCRegisterRef()) = startAddr;

	mDebugger->mDebugEvalSetRegisters = *registers;
	// Set trap flag, which raises "single-step" exception
	//  If we step ONTO the PC we set, then that means we have returned from kernel mode
	//  so we need to set the registers again (they would have been clobbered due to SetThreadContext bugs).
	// If we step past the PC then we're all good
	registers->mIntRegs.efl |= 0x100;
	mDebugger->SetRegisters(registers);
	//::ResumeThread(mDebugger->mActiveThread->mHThread);

	//TODO: For debugging stepping:

	if ((mExpressionFlags & DwEvalExpressionFlag_StepIntoCalls) != 0)
	{
		mDebugger->mDebugManager->mOutMessages.push_back("rehupLoc");
		return DbgTypedValue();
	}
	/*if (true)
	{
		return DbgTypedValue();
	}*/

// 	mDebugger->mActiveThread->mIsAtBreakpointAddress = 0;
// 	mDebugger->mActiveThread->mBreakpointAddressContinuing = 0;
	auto prevRunState = mDebugger->mRunState;
	mDebugger->mRunState = RunState_DebugEval;
	mDebugger->mExplicitStopThread = NULL;
	mDebugger->mRequestedStackFrameIdx = -1;

	BfLogDbg("DbgExprEvaluator::CreateCall %p in thread %d\n", startAddr, mDebugger->mActiveThread->mThreadId);

	DbgCallResult callResult;
	callResult.mSubProgram = method;
	callResult.mStructRetVal = structRetVal;
	mCallResults->push_back(callResult);

	mCreatedPendingCall = true;
	return GetDefaultTypedValue(method->mReturnType);
}

DbgTypedValue DbgExprEvaluator::CreateCall(BfAstNode* targetSrc, DbgTypedValue target, DbgSubprogram* method, bool bypassVirtual, const BfSizedArray<ASTREF(BfExpression*)>& arguments, SizedArrayImpl<DbgTypedValue>& argValues)
{
	HashSet<String> splatParams;
	SizedArray<DbgMethodArgument, 4> argPushQueue;

	//
	{
		for (auto variable : method->mBlock.mVariables)
		{
			if ((variable->mName == NULL) && (variable->mName[0] != '$'))
				continue;

			const char* dollarPos = strchr(variable->mName + 1, '$');
			if (dollarPos == NULL)
				continue;

			String varName = String(variable->mName + 1, dollarPos - variable->mName - 1);
			splatParams.Add(varName);
		}
	}

	int argIdx = 0;
	int paramIdx = 0;

	auto _PushArg = [&](const DbgTypedValue& typedValue, DbgVariable* param)
	{
		if (typedValue.mType->GetByteCount() == 0)
			return;

		if ((param != NULL) && (param->mType != NULL) && (param->mType->IsCompositeType()))
		{
			if ((param->mName != NULL) && (splatParams.Contains(param->mName)))
			{
				std::function<void(const DbgTypedValue& typedVal)> _SplatArgs = [&](const DbgTypedValue& typedVal)
				{
					auto type = typedVal.mType->RemoveModifiers();

					type = type->GetPrimaryType();

					if (type->IsValuelessType())
						return;

					if (!type->IsCompositeType())
					{
						auto elemTypedValue = ReadTypedValue(targetSrc, type, typedVal.mSrcAddress, DbgAddrType_Target);
						DbgMethodArgument methodArg;
						methodArg.mTypedValue = elemTypedValue;
						argPushQueue.push_back(methodArg);
						return;
					}

					auto baseType = type->GetBaseType();
					if (baseType != NULL)
					{
						DbgTypedValue baseValue = typedVal;
						baseValue.mType = baseType;
						_SplatArgs(baseValue);
					}

					for (auto member : type->mMemberList)
					{
						if ((member->mIsStatic) || (member->mIsConst))
							continue;

						DbgTypedValue memberValue;
						memberValue.mSrcAddress = typedVal.mSrcAddress + member->mMemberOffset;
						memberValue.mType = member->mType;
						_SplatArgs(memberValue);
					}
				};

				_SplatArgs(typedValue);

				return;
			}
		}

		DbgMethodArgument methodArg;
		methodArg.mTypedValue = typedValue;
		methodArg.mWantsRef = param->mType->IsRef();
		argPushQueue.push_back(methodArg);
	};

	bool thisByValue = false;
	int methodParamCount = method->mParams.Size();
	if (methodParamCount > 0)
	{
		method->mParams;
	}

	if (method->mHasThis)
	{
		auto param = method->mParams[paramIdx];
		if ((param->mType != NULL) && (param->mType->IsValueType()))
			thisByValue = true;

		if (!target)
		{
			Fail(StrFormat("An object reference is required to invoke the non-static method '%s'", method->ToString().c_str()), targetSrc);
			return DbgTypedValue();
		}

		_PushArg(target, param);
		methodParamCount--;
	}
	else
	{
		if (target.mPtr != 0)
		{
			Fail(StrFormat("Method '%s' cannot be accessed with an instance reference; qualify it with a type name instead", method->ToString().c_str()), targetSrc);
			return DbgTypedValue();
		}
	}

	DbgTypedValue expandedParamsArray;
	DbgType* expandedParamsElementType = NULL;
	int extendedParamIdx = 0;

	int paramOffset = method->mHasThis ? 1 : 0;
	while (true)
	{
		if (paramIdx >= (int)method->mParams.Size() - paramOffset)
		{
			if (argIdx < (int)arguments.size())
			{
				int showArgIdx = (int)method->mParams.Size();
				if (method->mHasThis)
					showArgIdx--;

				//WTF- what was this "Calling constructor" error about?!
				/*if (methodInstance->mMethodDef->mMethodDeclaration != NULL)
					mModule->mSystem->Warn(StrFormat("Trying to call constructor '%s'", mModule->MethodToString(methodInstance).c_str()), methodInstance->mMethodDef->GetRefNode());*/
				Fail(StrFormat("Too many arguments. Expected %d fewer.", (int)arguments.size() - (methodParamCount)), arguments[showArgIdx]);
				/*if (methodInstance->mMethodDef->mMethodDeclaration != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->mMethodDeclaration);*/
				return DbgTypedValue();
			}
			break;
		}

		DbgVariable* param = NULL;
		DbgType* wantType = NULL;
		if (expandedParamsElementType != NULL)
		{
			wantType = expandedParamsElementType;
		}
		else
		{
			param = method->mParams[paramIdx + paramOffset];
			wantType = param->mType;

			//TODO:
			/*if (method->mParams[paramIdx]->mParamType == BfParamType_Params)
			{
				//TODO: Check to see if it's a direct array pass

				bool isDirectPass = false;
				if (argIdx < (int)arguments.size())
				{
					if (argValues[argIdx].mValue == NULL)
						return DbgTypedValue();
					if (mModule->CanCast(argValues[argIdx], wantType))
						isDirectPass = true;
				}

				if (!isDirectPass)
				{
					BfArrayType* arrayType = (BfArrayType*) wantType;
					if (arrayType->IsIncomplete())
						mModule->PopulateType(arrayType, true);
					expandedParamsElementType = arrayType->mTypeGenericArguments[0];

					int arrayClassSize = arrayType->mInstSize - expandedParamsElementType->mSize;

					int numElements = (int) arguments.size() - argIdx;

					Value* sizeValue = mModule->GetConstValue(arrayClassSize);
					Value* elementDataSize = mModule->mIRBuilder->CreateMul(mModule->GetConstValue(expandedParamsElementType->mSize), mModule->GetConstValue(numElements));
					sizeValue = mModule->mIRBuilder->CreateAdd(sizeValue, elementDataSize);

					auto paramsData = mModule->GetHeadIRBuilder()->CreateAlloca(Type::getInt8Ty(*mModule->mLLVMContext), sizeValue, "paramsData");

					expandedParamsArray = DbgTypedValue(mModule->mIRBuilder->CreateBitCast(paramsData, arrayType->mLLVMType),
						arrayType, false);

					MatchConstructor(targetSrc, expandedParamsArray, arrayType, SizedArray<BfExpression*, 1>(), false);

					//TODO: Assert 'length' var is at slot 1
					auto arrayBits = mModule->mIRBuilder->CreateBitCast(expandedParamsArray.mValue, arrayType->mBaseType->mPtrLLVMType);
					if ((arrayType->mBaseType->mFieldInstances.size() < 0) || (arrayType->mBaseType->mFieldInstances[0].GetFieldDef()->mName != "mLength"))
					{
						mModule->Fail("INTERNAL ERROR: Unable to find array 'length' field", targetSrc);
						return DbgTypedValue();
					}
					auto addr = mModule->mIRBuilder->CreateConstInBoundsGEP2_32(arrayBits, 0, 1, "length");
					mModule->mIRBuilder->CreateStore(mModule->GetConstValue(numElements), addr);

					llvmArgs.push_back(expandedParamsArray.mValue);

					continue;
				}
			}*/
		}

		BfExpression* arg = NULL;
		DbgTypedValue argValue;
		if (argIdx < (int) arguments.size())
		{
			arg = arguments[argIdx];
			argValue = argValues[argIdx];
		}
		else if (param->mIsConst)
		{
			argValue.mType = param->mType;
			argValue.mInt64 = param->mConstValue;
		}
		else if (mPassInstance != NULL)
		{
			if (expandedParamsArray)
				break;

			//TODO: Default values
			/*if ((argIdx >= (int) methodInstance->mDefaultValues.size()) || (methodInstance->mDefaultValues[argIdx] == NULL))
			{
				BfAstNode* refNode = targetSrc;
				if (arguments.size() > 0)
					refNode = arguments.back();
				mModule->Fail(StrFormat("Not enough parameters specified. Got %d, expected %d.", arguments.size(), methodInstance->mParamTypes.size()), refNode);
				return DbgTypedValue();
			}
			llvmArgs.push_back(methodInstance->mDefaultValues[argIdx]);*/
			BfAstNode* refNode = targetSrc;
			if (arguments.size() > 0)
				refNode = arguments.back();
			Fail(StrFormat("Not enough parameters specified. Expected %d more.", methodParamCount - (int)arguments.size()), refNode);
			return DbgTypedValue();

			argIdx++;
			paramIdx++;
			continue;
		}

		if (argValue.mType == NULL)
			return DbgTypedValue();

		if (arg != NULL)
		{
			/*if (wantType->IsGenericParam())
			{
				auto genericParamType = (BfGenericParamType*) wantType;
				if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
					wantType = moduleMethodInstance.mMethodInstance->mMethodGenericArguments[genericParamType->mGenericParamIdx];
			}*/

			if ((wantType->mTypeCode == DbgType_Struct) ||
				(wantType->mTypeCode == DbgType_Class) ||
				(wantType->mTypeCode == DbgType_Union))
			{
				if (GetLanguage() != DbgLanguage_Beef)
				{
					Fail(StrFormat("Calling methods that take structs, classes, or unions by value is not supported"), arg);
					return DbgTypedValue();
				}
			}

			if (wantType->IsRef())
				wantType = wantType->mTypeParam;
			/*{
				if (argValue.mSrcAddress == 0)
				{
					Fail(StrFormat("Unable to get address of argument %s", method->GetParamName(paramIdx).c_str()), arg);
					return DbgTypedValue();
				}

				wantType = mDbgModule->GetPointerType(wantType->mTypeParam);
				argValue.mPtr = argValue.mSrcAddress;
				argValue.mSrcAddress = 0;
				argValue.mType = wantType;
			}*/

			argValue = Cast(arg, argValue, wantType);
			if (!argValue)
				return DbgTypedValue();

			/*if (wantType->IsStruct())
			{
				// We need to make a temp and get the addr of that
				if (!argValue.mIsAddr)
				{
					auto tempVar = mModule->GetHeadIRBuilder()->CreateAlloca(argValue.mType->mLLVMType, NULL, "agg.tmp");
					int align = argValue.mType->mAlign;
					tempVar->setAlignment(align);
					mModule->mIRBuilder->CreateStore(argValue.mValue, tempVar);
					//mModule->mIRBuilder->CreateMemCpy(tempVar, argValue.mValue, mModule->GetConstValue(argValue.mType->mDataSize), align, argValue.mType->IsVolatile());
					argValue = DbgTypedValue(tempVar, argValue.mType, true);
				}
			}
			else if (!wantType->IsRef())
				argValue = mModule->LoadValue(argValue);*/
		}

		if (!argValue)
		{
			Fail("Invalid expression type", arg);
			return DbgTypedValue();
		}

		//TODO:
		/*if (expandedParamsArray)
		{
			auto firstAddr = mModule->mIRBuilder->CreateConstInBoundsGEP2_32(expandedParamsArray.mValue, 0, 1);
			auto indexedAddr = mModule->mIRBuilder->CreateConstInBoundsGEP1_32(firstAddr, extendedParamIdx);
			mModule->mIRBuilder->CreateStore(argValue.mValue, indexedAddr);
			extendedParamIdx++;
		}
		else*/
		{
			//llvmArgs.push_back(argValue.mValue);
			_PushArg(argValue, param);
			paramIdx++;
		}
		argIdx++;
	}

	return CreateCall(method, argPushQueue, bypassVirtual);
}

DbgTypedValue DbgExprEvaluator::CreateCall(DbgSubprogram* method, SizedArrayImpl<DbgMethodArgument>& argPushQueue, bool bypassVirtual)
{
	BfLogDbgExpr("CreateCall #%d %s", mCallResultIdx, method->mName);

	if (mDebugger->IsMiniDumpDebugger())
	{
		Fail("Cannot call functions in a minidump", NULL);
		return GetDefaultTypedValue(method->mReturnType);
	}

	if ((mExpressionFlags & DwEvalExpressionFlag_AllowCalls) == 0)
	{
		mBlockedSideEffects = true;
		return GetDefaultTypedValue(method->mReturnType);
	}
	mHadSideEffects = true;

	// We need current physical registers to make sure we push params in correct stack frame
	CPURegisters registers;
	bool canSetRegisters = mDebugger->PopulateRegisters(&registers);

	auto* regSP = registers.GetSPRegisterRef();
	if (mCallStackPreservePos != 0)
	{
		if (mCallStackPreservePos <= *regSP)
			*regSP = mCallStackPreservePos;
	}

	int paramIdx = argPushQueue.size() - 1;
	if (method->mHasThis)
		paramIdx--;

	bool thisByValue = false;
	if ((method->mHasThis) && (!method->mParams.IsEmpty()))
	{
		auto param = method->mParams[0];
		if ((param->mType != NULL) && (param->mType->IsValueType()))
			thisByValue = true;
	}

	DbgTypedValue structRetVal;
	if (mDebugger->CheckNeedsSRetArgument(method->mReturnType))
	{
		int retSize = BF_ALIGN(method->mReturnType->GetByteCount(), 16);
		*regSP -= retSize;
		structRetVal.mType = method->mReturnType;
		structRetVal.mSrcAddress = *regSP;

		// For chained calls we need to leave the sret form the previous calls intact
		mCallStackPreservePos = *regSP;
	}

	for (int i = (int)argPushQueue.size() - 1; i >= 0; i--)
	{
		auto& arg = argPushQueue[i];

		// If we have something like an int constant when we need to pass by int&, then we need to allocate
		//  on the stack and pass that address
		if (arg.mWantsRef)
		{
			auto& typedValue = arg.mTypedValue;
			auto rootType = typedValue.mType->RemoveModifiers();
			if (arg.mTypedValue.mSrcAddress != 0)
			{
				typedValue.mPtr = typedValue.mSrcAddress;
			}
			else
			{
				int rootSize = rootType->GetByteCount();
				*regSP -= BF_ALIGN(rootSize, 16);
				mDebugger->WriteMemory(*regSP, &arg.mTypedValue.mInt64, rootSize); // Write from debugger memory to target
				typedValue.mPtr = *regSP;
			}

			typedValue.mType = mDbgModule->GetPointerType(rootType);
			typedValue.mSrcAddress = 0;
		}
	}

	if (structRetVal.mSrcAddress != 0)
	{
		BfLogDbgExpr(" SRet:0x%p", structRetVal.mSrcAddress);
		mDebugger->AddParamValue(0, method->mHasThis && !thisByValue, &registers, structRetVal);
		paramIdx++;
	}

	DbgTypedValue thisVal;
	for (int padCount = 0; true; padCount++)
	{
		auto regSave = registers;
		int paramIdxSave = paramIdx;

		*regSP -= padCount * sizeof(addr_target);

		for (int i = (int)argPushQueue.size() - 1; i >= 0; i--)
		{
			auto& arg = argPushQueue[i];
			if ((i == 0) && (method->mHasThis) && (!method->ThisIsSplat()))
			{
				thisVal = arg.mTypedValue;
				addr_target thisAddr;
				if (thisVal.mType->IsCompositeType())
					thisAddr = thisVal.mSrcAddress;
				else
					thisAddr = thisVal.mPtr;
				mDebugger->SetThisRegister(&registers, thisAddr);

				if (padCount == 0)
					BfLogDbgExpr(" This:0x%p", thisAddr);
			}
			else
			{
				mDebugger->AddParamValue(paramIdx, method->mHasThis, &registers, arg.mTypedValue);
				paramIdx--;
			}
		}

#ifdef BF_DBG_64
		// Stack must be 16-byte aligned
		if ((*regSP & 0xF) == 0)
			break;
#else
		break;
#endif

		registers = regSave;
		paramIdx = paramIdxSave;

		BF_ASSERT(padCount < 3);
	}

	return CreateCall(method, thisVal, structRetVal, bypassVirtual, &registers);
}

DbgTypedValue DbgExprEvaluator::MatchMethod(BfAstNode* targetSrc, DbgTypedValue target, bool allowImplicitThis, bool bypassVirtual, const StringImpl& methodName,
	const BfSizedArray<ASTREF(BfExpression*)>& arguments, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments, bool& failed)
{
	SetAndRestoreValue<String*> prevReferenceId(mReferenceId, NULL);

	bool wantCtor = methodName.IsEmpty();

	mDbgModule->ParseGlobalsData();
	//mDbgModule->ParseSymbolData();

	SizedArray<DbgTypedValue, 4> argValues;
	if (!ResolveArgValues(arguments, argValues))
		return DbgTypedValue();

	if ((methodName == "__cast") || (methodName == "__bitcast"))
	{
		if (argValues.size() > 0)
		{
			String typeName = "";
			for (int argIdx = 0; argIdx < (int)argValues.size() - 1; argIdx++)
			{
				auto arg = argValues[argIdx];
				if (!arg)
					return DbgTypedValue();
				if ((arg.mType->IsPointer()) && ((arg.mType->mTypeParam->mTypeCode == DbgType_UChar) || (arg.mType->mTypeParam->mTypeCode == DbgType_SChar)))
				{
					typeName += arg.mCharPtr;
				}
				else if ((arg.mType->mTypeCode == DbgType_i32) || (arg.mType->mTypeCode == DbgType_i64))
				{
					if (typeName.IsEmpty())
					{
						// Fake this int as a type pointer
						arg.mSrcAddress = arg.mUInt64;
						BeefTypeToString(arg, typeName);
					}
					else
						typeName += BfTypeUtils::HashEncode64(arg.mInt64);
				}
				else
				{
					BeefTypeToString(arg, typeName);
				}
			}

			auto castedType = mDbgModule->FindType(typeName, NULL, GetLanguage(), true);
			if (castedType == NULL)
			{
				if (typeName.EndsWith('*'))
				{
					auto noPtrTypeEntry = mDbgModule->FindType(typeName.Substring(0, typeName.length() - 1), NULL, DbgLanguage_Beef, true);
					if (noPtrTypeEntry != NULL)
						castedType = mDbgModule->GetPointerType(noPtrTypeEntry);
				}
			}

			int targetArgIdx = argValues.size() - 1;
			if (castedType == NULL)
			{
				Fail(StrFormat("Unable to find type: '%s'", typeName.c_str()), targetSrc);
				return argValues[targetArgIdx];
			}

			if (methodName == "__bitcast")
			{
				DbgTypedValue result = argValues[targetArgIdx];
				result.mType = castedType;
				return result;
			}

			return Cast(arguments[targetArgIdx], argValues[targetArgIdx], castedType, true);
		}
	}
	else if (methodName == "__hasField")
	{
		if (argValues.size() == 2)
		{
			auto checkType = argValues[0].mType;
			if ((checkType != NULL) && (checkType->IsPointer()))
				checkType = checkType->mTypeParam;
			if (checkType != NULL)
			{
				//TODO: Protect
				String findFieldName = argValues[1].mCharPtr;

				DbgTypedValue result;

				result.mType = mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage());
				result.mBool = HasField(checkType, findFieldName);
				return result;
			}
		}
	}
	else if (methodName == "__desc")
	{
		if (argValues.size() == 1)
		{
			auto checkType = argValues[0].mType;
			if ((checkType != NULL) && (checkType->IsPointer()))
				checkType = checkType->mTypeParam;

			if (checkType != NULL)
			{
				checkType = checkType->GetPrimaryType();

				String dsc = "DESCRIPTION OF ";
				dsc += checkType->ToString();
				dsc += "\n";

				dsc += "From Compile Unit: ";
				dsc += checkType->mCompileUnit->mName;
				dsc += "\n";

				dsc += StrFormat("Size: %d\n", checkType->GetByteCount());
				dsc += StrFormat("Align: %d\n", checkType->GetAlign());

				for (auto member : checkType->mMemberList)
				{
					dsc += "  ";
					if (member->mIsConst)
						dsc += "const ";
					if (member->mIsExtern)
						dsc += "extern ";
					if (member->mIsStatic)
						dsc += "static ";
					dsc += member->mType->ToString();
					dsc += " ";
					dsc += member->mName;
					if (!member->mIsStatic)
					{
						dsc += StrFormat(" Offset:%d", member->mMemberOffset);
					}
					dsc += StrFormat(" Size:%d", member->mType->GetByteCount());
					if (member->mLinkName != NULL)
						dsc += member->mLinkName;
					dsc += "\n";
				}
				if ((mExpressionFlags & DwEvalExpressionFlag_AllowCalls) != 0)
					mDebugger->OutputMessage(dsc);
				return DbgTypedValue();
			}
		}
	}
	else if (methodName == "__demangleMethod")
	{
		if (argValues.size() == 2)
		{
			auto checkType = argValues[0].mType;
			if (checkType->IsPointer())
				checkType = checkType->mTypeParam;

			//TODO: Protect
			String findMethodName = argValues[1].mCharPtr;

			checkType->PopulateType();
			for (auto checkMethod : checkType->mMethodList)
			{
				if (checkMethod->mName == findMethodName)
				{
					static String demangledName;
					demangledName = BfDemangler::Demangle(checkMethod->mLinkName, checkType->GetLanguage());

					DbgTypedValue result;
					result.mType = argValues[1].mType;
					result.mCharPtr = demangledName.c_str();
					result.mIsLiteral = true;
					return result;
				}
			}
		}
	}
	else if (methodName == "__demangle")
	{
		if (argValues.size() == 1)
		{
			auto rawTextType = mDbgModule->GetPrimitiveType(DbgType_RawText, GetLanguage());

			String mangledName = argValues[0].mCharPtr;

			static String demangledName;
			demangledName = BfDemangler::Demangle(mangledName, DbgLanguage_Unknown);

			if (demangledName.StartsWith("bf."))
				demangledName.Remove(0, 3);

			DbgTypedValue result;
			result.mType = rawTextType;
			result.mCharPtr = demangledName.c_str();
			result.mIsLiteral = true;
			return result;
		}
	}
	else if (methodName == "__demangleFakeMember")
	{
		if (argValues.size() == 1)
		{
			auto checkType = argValues[0].mType;
			if (checkType->IsPointer())
				checkType = checkType->mTypeParam;

			auto rawTextType = mDbgModule->GetPrimitiveType(DbgType_RawText, GetLanguage());

			checkType->PopulateType();
			auto firstMember = checkType->mMemberList.mHead;

			static String demangledName;
			demangledName = BfDemangler::Demangle(firstMember->mName, checkType->GetLanguage());

			if (demangledName.StartsWith("bf."))
				demangledName.Remove(0, 3);

			DbgTypedValue result;
			result.mType = rawTextType;
			result.mCharPtr = demangledName.c_str();
			result.mIsLiteral = true;
			return result;
		}
	}
	else if (methodName == "__funcName")
	{
		if (argValues.size() == 1)
		{
			auto funcPtr = argValues[0].mPtr;

			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			static String demangledName;
			auto subProgram = mDebugTarget->FindSubProgram(funcPtr);
			if (subProgram != NULL)
			{
				demangledName = subProgram->ToString();
			}
			else if (mDebugTarget->FindSymbolAt(funcPtr, &symbolName, &offset, &dwarf))
			{
				demangledName = BfDemangler::Demangle(symbolName, GetLanguage());
			}
			else
			{
				demangledName = StrFormat("0x%@", funcPtr);
			}

			DbgTypedValue result;
			result.mType = mDbgModule->GetPrimitiveType(DbgType_Subroutine, GetLanguage());
			result.mCharPtr = demangledName.c_str();
			return result;
		}
	}
	else if (methodName == "__funcTarget")
	{
		if (argValues.size() == 2)
		{
			auto funcPtr = argValues[0].mPtr;
			auto funcTarget = argValues[1].mPtr;

			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			if (mDebugTarget->FindSymbolAt(funcPtr, &symbolName, &offset, &dwarf))
			{
				String firstParamType = BfDemangler::Demangle(symbolName, GetLanguage(), BfDemangler::Flag_CaptureTargetType);
				auto targetType = mDbgModule->FindType(firstParamType, NULL, DbgLanguage_BeefUnfixed);
				if (targetType)
				{
					if (!targetType->IsPointer())
						targetType = mDbgModule->GetPointerType(targetType);

					DbgTypedValue result;
					result.mType = targetType;
					result.mPtr = funcTarget;
					return result;
				}
			}

			return argValues[1];
		}
	}
	else if (methodName == "__stringView")
	{
		if (argValues.size() >= 2)
		{
			if ((argValues[1].mType != NULL) && (argValues[1].mType->IsInteger()))
				mCountResultOverride = (intptr)argValues[1].GetInt64();
			return argValues[0];
		}
	}
	else if (methodName == "__getHighBits")
	{
		if ((argValues.size() == 2) && (argValues[0]) && (argValues[1]))
		{
			uint64 val = argValues[0].mUInt64;
			int bitCount = (int)argValues[1].GetInt64();

			DbgTypedValue result;
			result.mType = argValues[0].mType;
			result.mInt64 = val >> (argValues[0].mType->GetByteCount() * 8 - bitCount);
			return result;
		}
	}
	else if (methodName == "__clearHighBits")
	{
		if ((argValues.size() == 2) && (argValues[0]) && (argValues[1]))
		{
			uint64 val = argValues[0].mUInt64;
			int bitCount = (int)argValues[1].GetInt64();

			if (bitCount <= 0)
				return argValues[0];

			int64 andBits = (0x8000000000000000LL) >> ((argValues[0].mType->GetByteCount() - 8) * 8 + bitCount - 1);
			DbgTypedValue result;
			result.mType = argValues[0].mType;
			result.mInt64 = val & ~andBits;
			return result;
		}
	}
	else if (methodName == "__parseCompileUnits")
	{
		for (auto dbgModule : mDebugTarget->mDbgModules)
		{
			dbgModule->ParseCompileUnits();
		}
	}

	DbgType* curTypeDef;
	DbgType* targetTypeInst = NULL;
	bool checkNonStatic = true;
	if (target)
	{
		targetTypeInst = target.mType;
		curTypeDef = targetTypeInst;
	}
	else if (target.mType) // Static targeted
	{
		if (target.mType->IsPrimitiveType())
		{
			//TODO ;
			//targetTypeInst = mModule->GetPrimitiveStructType(target.mType);
		}
		else
			targetTypeInst = target.mType;
		if (targetTypeInst == NULL)
		{
			Fail("No static methods available", targetSrc);
			return DbgTypedValue();
		}
		curTypeDef = targetTypeInst;
		checkNonStatic = false;
	}
	else // Current scope
	{
		curTypeDef = GetCurrentType();
		targetTypeInst = GetCurrentType();
		auto currentMethod = GetCurrentMethod();

		checkNonStatic = (currentMethod != NULL) && (currentMethod->mHasThis);
	}

	DbgSubprogram* methodDef = NULL;
	BfTypeVector checkMethodGenericArguments;

	bool isFailurePass = false;
	DbgType* curTypeInst = targetTypeInst;
	DwMethodMatcher methodMatcher(targetSrc, this, methodName, argValues, methodGenericArguments);
	if (targetTypeInst != NULL)
		methodMatcher.mTargetIsConst = targetTypeInst->IsConst();
	if (targetTypeInst != NULL)
	{
		methodMatcher.CheckType(targetTypeInst, false);
		if (methodMatcher.mBestMethodDef == NULL)
		{
			isFailurePass = true;
			methodMatcher.CheckType(targetTypeInst, true);
		}
	}

	if (methodMatcher.mBestMethodDef != NULL)
	{
		//methodGenericArguments = methodMatcher.mBestMethodGenericArguments;
		curTypeInst = methodMatcher.mBestMethodTypeInstance;
		methodDef = methodMatcher.mBestMethodDef;
	}

	if ((methodDef) && (methodDef->mHasThis) && (!target) && (allowImplicitThis))
	{
		target = GetThis();
		/*if (!target)
		{
			mModule->Fail("Target required for non-static method", targetSrc);
			return DbgTypedValue();
		}*/
	}

	// Fail, check for delegate invocation
	if (methodDef == NULL)
	{
		DbgTypedValue fieldVal;
		if (allowImplicitThis)
		{
			//auto thisValue = mModule->GetThis();
			//LookupField(targetSrc, thisValue, methodName);
			fieldVal = LookupIdentifier(BfNodeDynCast<BfIdentifierNode>(targetSrc));
		}
		else
		{
			fieldVal = LookupField(targetSrc, target, methodName);
		}

		//TODO:
		/*if (mPropDef != NULL)
			fieldVal = GetResult();*/

		/*if (fieldVal)
		{
			if (fieldVal.mType->IsTypeInstance())
			{
				auto fieldTypeInst = fieldVal.mType->ToTypeInstance();
				if (fieldTypeInst->mTypeDef->mIsDelegate)
					return MatchMethod(targetSrc, fieldVal, false, false, "Invoke", arguments, methodGenericArguments);
			}
			fieldVal.mType = mModule->ResolveGenericParam(fieldVal.mType);
			if (fieldVal.mType->IsVar())
				return DbgTypedValue(mModule->GetDefaultValue(fieldVal.mType), fieldVal.mType);
			mModule->Fail(StrFormat("Cannot perform invocation on type '%s'", mModule->TypeToString(fieldVal.mType).c_str()), targetSrc);
			return DbgTypedValue();
		}*/
	}

	/*if ((!methodDef) && (!target))
	{
		String checkTypeName = methodName;
		int wantNumGenericArgs = 0;
		if ((methodGenericArguments != NULL) && (methodGenericArguments->size() > 0))
			wantNumGenericArgs = (int)methodGenericArguments->size();
		auto typeDef = mModule->mSystem->FindTypeDef(checkTypeName, wantNumGenericArgs, mModule->mCurTypeInstance->mTypeDef->mNamespaceSearch);
		if (typeDef != NULL)
		{
			BfTypeVector genericArgs;
			if (methodGenericArguments != NULL)
			{
				for (auto genericArg : *methodGenericArguments)
				{
					auto typeDef = mModule->ResolveTypeRef(genericArg);
					if (typeDef == NULL)
						return DbgTypedValue();
					genericArgs.push_back(typeDef);
				}
			}
			auto resolvedType = mModule->ResolveTypeDef(typeDef, genericArgs)->ToTypeInstance();
			if (resolvedType != NULL)
			{
				if (!resolvedType->IsStruct())
				{
					mModule->Fail("Objects must be allocated through 'new' or 'stack'", targetSrc);
					return DbgTypedValue();
				}

				DbgTypedValue structInst = DbgTypedValue(mModule->mIRBuilder->CreateAlloca(resolvedType->mLLVMType),
					resolvedType, false);
				MatchConstructor(targetSrc, structInst, resolvedType, arguments, false);
				return DbgTypedValue(mModule->mIRBuilder->CreateLoad(structInst.mValue), resolvedType, false);
			}
		}
	}*/

	if ((methodDef == NULL) && (!target))
	{
		for (int compileUnitIdx = 0; compileUnitIdx < (int)mDbgModule->mCompileUnits.size(); compileUnitIdx++)
		{
			auto compileUnit = mDbgModule->mCompileUnits[compileUnitIdx];
			methodMatcher.CheckType(compileUnit->mGlobalType, isFailurePass);
			if (methodMatcher.mBestMethodDef != NULL)
			{
				isFailurePass = false;
				curTypeInst = methodMatcher.mBestMethodTypeInstance;
				methodDef = methodMatcher.mBestMethodDef;
				break;
			}
		}
	}

	if ((methodDef == NULL) && ((methodGenericArguments == NULL) || (methodGenericArguments->IsEmpty())))
	{
		DbgTypedValue enumResult;
		DbgType* enumType = target.mType;
		if ((enumType != NULL) && (enumType->IsBfPayloadEnum()))
		{
			enumResult = CheckEnumCreation(targetSrc, enumType, methodName, arguments);
			if (enumResult)
				return enumResult;
		}
	}

	if (methodDef == NULL)
	{
		if (target)
		{
			std::function<DbgTypedValue(DbgTypedValue)> _CheckUsingFields = [&](DbgTypedValue checkTarget)
			{
				auto curCheckType = checkTarget.mType->RemoveModifiers();
				curCheckType = curCheckType->GetPrimaryType();
				curCheckType->PopulateType();

				auto nextMember = curCheckType->mMemberList.mHead;
				while (nextMember != NULL)
				{
					auto checkMember = nextMember;
					nextMember = checkMember->mNext;

					if (checkMember->mName == NULL)
					{
						//TODO: Check inside anonymous type
						DbgTypedValue innerTarget;

						addr_target targetPtr = checkTarget.mSrcAddress;
						if ((checkTarget.mType != NULL) && (checkTarget.mType->HasPointer()))
							targetPtr = checkTarget.mPtr;
						innerTarget.mSrcAddress = targetPtr + checkMember->mMemberOffset;
						innerTarget.mType = checkMember->mType;

						failed = false;
						auto result = MatchMethod(targetSrc, innerTarget, false, bypassVirtual, methodName, arguments, methodGenericArguments, failed);
						if (!failed)
							return result;
					}
				}

				for (auto baseTypeEntry : curCheckType->mBaseTypes)
				{
					auto baseType = baseTypeEntry->mBaseType;

					DbgTypedValue baseTarget = target;
					addr_target* ptrRef = NULL;
					if ((baseTarget.mPtr != 0) && (checkTarget.mType->HasPointer()))
						ptrRef = &baseTarget.mPtr;
					else if (baseTarget.mSrcAddress != 0)
						ptrRef = &baseTarget.mSrcAddress;

					if (ptrRef != NULL)
					{
						if (baseTypeEntry->mVTableOffset != -1)
						{
							addr_target vtableAddr = mDebugger->ReadMemory<addr_target>(checkTarget.mPtr);
							int32 virtThisOffset = mDebugger->ReadMemory<int32>(vtableAddr + baseTypeEntry->mVTableOffset * sizeof(int32));
							*ptrRef += virtThisOffset;
						}
						else
							*ptrRef += baseTypeEntry->mThisOffset;
					}

					baseTarget.mType = baseType;
					auto result = _CheckUsingFields(baseTarget);
					if (!failed)
						return result;
				}

				failed = true;
				return DbgTypedValue();
			};

			auto result = _CheckUsingFields(target);
			if (!failed)
				return result;
		}

		failed = true;
		return DbgTypedValue();
	}

	// Don't execute the method if we've had a failure (like an ambiguous lookup)
	if ((mPassInstance != NULL) && (mPassInstance->HasFailed()))
		return DbgTypedValue();

	DbgTypedValue callTarget;
	if (targetTypeInst == curTypeInst)
	{
		callTarget = target;
	}
	else if (target)
	{
		bool handled = false;
		if (target.mType->IsCompositeType())
		{
			//BF_FATAL("Not implemented");
			callTarget = target;
			/*if (curTypeInst->IsObject())
			{
				// Box it
				callTarget = mModule->Cast(targetSrc, target, curTypeInst, true);
				handled = true;
			}*/
		}
		/*else
			target = mModule->LoadValue(target);*/
		if (!handled)
		{
			/*Value* targetValue = target.mValue;
			callTarget = DbgTypedValue(mModule->mIRBuilder->CreateBitCast(targetValue, curTypeInst->mPtrLLVMType), curTypeInst);*/
			callTarget = target;
		}
	}

	if (prevReferenceId.mPrevVal != NULL)
	{
		if (prevReferenceId.mPrevVal->IsEmpty())
			*prevReferenceId.mPrevVal += ".";
		*prevReferenceId.mPrevVal += methodDef->ToString();
	}

	return CreateCall(targetSrc, callTarget, methodDef, bypassVirtual, arguments, argValues);
}

void DbgExprEvaluator::DoInvocation(BfAstNode* target, BfSizedArray<ASTREF(BfExpression*)>& args, BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments, bool& failed)
{
	bool allowImplicitThis = false;
	BfAstNode* methodNodeSrc = target;

	bool bypassVirtual = false;
	String targetFunctionName;
	DbgTypedValue thisValue;
	//TODO: This may just be a fully qualified static method name, so let's check that also
	if (auto memberRefExpression = BfNodeDynCast<BfMemberReferenceExpression>(target))
	{
		//GetAutoComplete()->CheckMemberReference(memberRefExpression->mTarget, memberRefExpression->mDotToken, memberRefExpression->mMemberName);

		if (memberRefExpression->mMemberName == NULL)
			return;

		if (memberRefExpression->IsA<BfBaseExpression>())
			bypassVirtual = true;

		methodNodeSrc = memberRefExpression->mMemberName;

		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(memberRefExpression->mMemberName))
		{
			methodNodeSrc = attrIdentifier->mIdentifier;
			if (attrIdentifier->mIdentifier != NULL)
				targetFunctionName = attrIdentifier->mIdentifier->ToString();
		}
		else
			targetFunctionName = memberRefExpression->mMemberName->ToString();

		if (memberRefExpression->mTarget == NULL)
		{
			// Dot-qualified
			if ((mExpectingType != NULL) && (mExpectingType->IsStruct()))
			{
				mResult = DbgTypedValue();
				mResult.mType = mExpectingType;
			}
			else
				Fail("Unqualified dot syntax can only be used when the result type can be inferred", memberRefExpression->mDotToken);
		}
		else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberRefExpression->mTarget))
		{
			// Static method
			mResult = DbgTypedValue();
			mResult.mType = ResolveTypeRef(typeRef);
		}
		else
			VisitChild(memberRefExpression->mTarget);
		GetResult();
		if (mResult.mType == NULL)
			return;
		thisValue = mResult;
		mResult = DbgTypedValue();
	}
	else if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(target))
	{
		/*if (GetAutoComplete() != NULL)
			GetAutoComplete()->CheckMemberReference(qualifiedName->mLeft, qualifiedName->mDot, qualifiedName->mRight);*/

		String leftName = qualifiedName->mLeft->ToString();
		if (leftName == "base")
			bypassVirtual = true;

		methodNodeSrc = qualifiedName->mRight;
		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(qualifiedName->mRight))
		{
			methodNodeSrc = attrIdentifier->mIdentifier;
			targetFunctionName = attrIdentifier->mIdentifier->ToString();
		}
		else
			targetFunctionName = qualifiedName->mRight->ToString();

		bool hadError = false;
		thisValue = LookupIdentifier(qualifiedName->mLeft, true, &hadError);
		if (!thisValue)
			thisValue = GetResult();
		if (hadError)
			return;
		if (!thisValue)
		{
			// Identifier not found. Static method? Just check speculatively don't throw error
			DbgType* type;
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
				type = ResolveTypeRef(qualifiedName->mLeft);
			}

			/*if (type->IsGenericParam())
			{
				auto genericParamInstance = mModule->GetGenericParamInstance((BfGenericParamType*)type);
				type = genericParamInstance->mTypeConstraint;
			}*/

			if (type != NULL)
				thisValue.mType = type;
			else if (auto qualifiedLeft = BfNodeDynCast<BfQualifiedNameNode>(qualifiedName->mLeft))
			{
				LookupQualifiedStaticField(qualifiedLeft, true);
				thisValue = mResult;
				mResult = DbgTypedValue();
			}
			thisValue.mHasNoValue = true;
		}
		if (!thisValue.mType)
		{
			Fail("Identifier not found", qualifiedName->mLeft);
			return;
		}
		mResult = DbgTypedValue();
	}
	else if (auto identiferExpr = BfNodeDynCast<BfIdentifierNode>(target))
	{
		allowImplicitThis = true;

		targetFunctionName = target->ToString();
	}
	else if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(target))
	{
		// It appears we're attempting to invoke a delegate returned from another method
		Visit(invocationExpr);
		auto innerInvocationResult = mResult;
		mResult = DbgTypedValue();

		if (!innerInvocationResult)
			return;

		/*if (innerInvocationResult.mType->IsTypeInstance())
		{
			auto invocationTypeInst = innerInvocationResult.mType->ToTypeInstance();
			if (invocationTypeInst->mTypeDef->mIsDelegate)
			{
				thisValue = innerInvocationResult;
				targetFunctionName = "Invoke";
			}
		}*/

		if (!thisValue)
		{
			Fail(StrFormat("Cannot perform invocation on type '%s'", innerInvocationResult.mType->ToString().c_str()), invocationExpr->mTarget);
			return;
		}
	}
	else
	{
		Fail("Invalid invocation target", target);
		return;
	}

	if (thisValue)
	{
		if (thisValue.mType->IsPointer())
		{
			//BF_ASSERT(thisValue.mIsAddr);
			thisValue.mType = thisValue.mType->mTypeParam;
			thisValue.mSrcAddress = thisValue.mPtr;
		}

		if (thisValue.mType->IsPrimitiveType())
		{
			//TODO:
			/*auto primStructType = GetPrimitiveStructType(thisValue.mType);
			//thisValue = mModule->Cast(target, thisValue, primStructType);

			auto srcAlloca = mModule->GetHeadIRBuilder()->CreateAlloca(primStructType->mInstLLVMType, 0);
			srcAlloca->setAlignment(primStructType->mAlign);
			auto primPtr = mModule->mIRBuilder->CreateConstInBoundsGEP2_32(srcAlloca, 0, 0);
			mModule->mIRBuilder->CreateStore(thisValue.mValue, primPtr);

			thisValue = DbgTypedValue(srcAlloca, primStructType, true);*/
		}

		if (thisValue.mType->IsEnum())
		{
			//auto enumStructType = thisValue.mType->ToTypeInstance();

			/*auto srcAlloca = mModule->GetHeadIRBuilder()->CreateAlloca(enumStructType->mInstLLVMType, 0);
			srcAlloca->setAlignment(enumStructType->mAlign);
			auto enumPtr = mModule->mIRBuilder->CreateConstInBoundsGEP2_32(srcAlloca, 0, 0);
			mModule->mIRBuilder->CreateStore(thisValue.mValue, enumPtr);*/

			//thisValue = DbgTypedValue(mModule->mIRBuilder->CreateBitCast(srcAlloca, enumStructType->mBaseType->mPtrLLVMType), enumStructType->mBaseType, true);
		}

		/*if (!thisValue.mType->IsTypeInstance())
		{
			Fail(StrFormat("Invalid target type: '%s'", mModule->TypeToString(thisValue.mType).c_str()), target);
			return;
		}*/
	}


	mResult = MatchMethod(methodNodeSrc, thisValue, allowImplicitThis, bypassVirtual, targetFunctionName, args, methodGenericArguments, failed);
}

void DbgExprEvaluator::Visit(BfInvocationExpression* invocationExpr)
{
	mIsComplexExpression = true;

	auto wasCapturingMethodInfo = (mAutoComplete != NULL) && (mAutoComplete->mIsCapturingMethodMatchInfo);
	/*if (mAutoComplete != NULL)
		mAutoComplete->CheckInvocation(invocationExpr, invocationExpr->mOpenParen, invocationExpr->mCloseParen, invocationExpr->mCommas);*/

	BfSizedArray<ASTREF(BfAstNode*)>* methodGenericArguments = NULL;
	if (invocationExpr->mGenericArgs != NULL)
		methodGenericArguments = &invocationExpr->mGenericArgs->mGenericArgs;
	bool failed = false;
	DoInvocation(invocationExpr->mTarget, invocationExpr->mArguments, methodGenericArguments, failed);
	if (failed)
		Fail("Method does not exist", invocationExpr->mTarget);

	if ((wasCapturingMethodInfo) && (!mAutoComplete->mIsCapturingMethodMatchInfo))
	{
		mAutoComplete->mIsCapturingMethodMatchInfo = true;
		BF_ASSERT(mAutoComplete->mMethodMatchInfo != NULL);
	}
	else if (mAutoComplete != NULL)
		mAutoComplete->mIsCapturingMethodMatchInfo = false;
}

void DbgExprEvaluator::Visit(BfConditionalExpression* condExpr)
{
	VisitChild(condExpr->mConditionExpression);
	GetResult();
	if (!mResult)
		return;

	auto boolResult = Cast(condExpr->mConditionExpression, mResult, mDbgModule->GetPrimitiveType(DbgType_Bool, GetLanguage()));
	if (!boolResult)
		return;

	if (boolResult.mBool)
	{
		if (condExpr->mTrueExpression != NULL)
			VisitChild(condExpr->mTrueExpression);
		else
			mResult = DbgTypedValue();
	}
	else
	{
		if (condExpr->mFalseExpression != NULL)
			VisitChild(condExpr->mFalseExpression);
		else
			mResult = DbgTypedValue();
	}
}

void DbgExprEvaluator::Visit(BfTypeAttrExpression* typeAttrExpr)
{
	if (typeAttrExpr->mTypeRef == NULL)
		return;

	AutocompleteCheckType(typeAttrExpr->mTypeRef);
	auto dbgType = ResolveTypeRef(typeAttrExpr->mTypeRef);
	if (dbgType == NULL)
		return;
	mResult.mType = mDbgModule->GetPrimitiveType(DbgType_i32, GetLanguage());
	switch (typeAttrExpr->mToken->GetToken())
	{
	case BfToken_SizeOf:
		mResult.mInt64 = dbgType->GetByteCount();
		break;
	case BfToken_AlignOf:
		mResult.mInt64 = dbgType->GetAlign();
		break;
	case BfToken_StrideOf:
		mResult.mInt64 = dbgType->GetStride();
		break;
	default:
		Fail("Invalid attribute expression", typeAttrExpr);
		break;
	}
}

void DbgExprEvaluator::Visit(BfTupleExpression* tupleExpr)
{
	if (mReceivingValue == NULL)
	{
		Fail("Tuple expressions can only be used when the receiving address can be inferred", tupleExpr);
		return;
	}

	if (mReceivingValue->mIsReadOnly)
	{
		Fail("Cannot write to read-only value", tupleExpr);
		return;
	}

	auto type = mReceivingValue->mType->RemoveModifiers();
	if (!type->IsBfTuple())
	{
		Fail(StrFormat("Tuple expressions cannot be used for type '%s'", mReceivingValue->mType->ToString().c_str()), tupleExpr);
		return;
	}

	CheckTupleCreation(mReceivingValue->mSrcAddress, tupleExpr, mReceivingValue->mType, tupleExpr->mValues, &tupleExpr->mNames);
	mReceivingValue = NULL;
}

DbgTypedValue DbgExprEvaluator::Resolve(BfExpression* expr, DbgType* wantType)
{
	//BfLogDbgExpr("Dbg Evaluate %s\n", expr->ToString().c_str());

	BF_ASSERT(!HasPropResult());

	mCallStackPreservePos = 0;

	SetAndRestoreValue<DbgType*> prevType(mExpectingType, wantType);
	SetAndRestoreValue<DbgTypedValue> prevResult(mResult, DbgTypedValue());

	if ((mExpressionFlags & DwEvalExpressionFlag_AllowCalls) != 0)
	{
		BF_ASSERT(mCallResults != NULL);
	}

	if (mExplicitThis)
		mExplicitThis = FixThis(mExplicitThis);

	mExpectingType = wantType;
	VisitChildNoRef(expr);
	GetResult();
	while (true)
	{
		if (!mResult)
			break;
		if (//(mResult.mType->mTypeCode == DbgType_Const) ||
			(mResult.mType->mTypeCode == DbgType_Volatile))
			mResult.mType = mResult.mType->mTypeParam;
		else
			break;
	}

	if ((mResult) && (wantType != NULL))
		mResult = Cast(expr, mResult, wantType);

	if ((mReferenceId != NULL) && (mIsComplexExpression))
		mReferenceId->clear();

	return mResult;
}

BfAstNode* DbgExprEvaluator::FinalizeExplicitThisReferences(BfAstNode* headNode)
{
	//return headNode;
	if (mExplicitThisExpr == NULL)
		return headNode;
	auto explicitThisExpr = mExplicitThisExpr;
	mExplicitThisExpr = NULL;

	BfReducer bfReducer;

	BfPrinter bfPrinter(explicitThisExpr->GetSourceData()->mRootNode, NULL, NULL);
	bfPrinter.mIgnoreTrivia = true;
	bfPrinter.mReformatting = true;
	bfPrinter.VisitChild(explicitThisExpr);

	bool doWrap = false;
	String thisExprStr = bfPrinter.mOutString;

	if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(explicitThisExpr))
	{
		if ((unaryOpExpr->mOp != BfUnaryOp_AddressOf) && (unaryOpExpr->mOp != BfUnaryOp_Dereference))
			doWrap = true;
	}
	if ((explicitThisExpr->IsA<BfCastExpression>()) ||
		(explicitThisExpr->IsA<BfBinaryOperatorExpression>()) ||
		(explicitThisExpr->IsA<BfUnaryOperatorExpression>()))
		doWrap = true;
	if (doWrap)
	{
		thisExprStr = "(" + thisExprStr + ")";
	}

	auto bfParser = headNode->GetSourceData()->ToParser();
	BF_ASSERT(bfParser != NULL);

	auto source = headNode->GetSourceData();
	for (auto& replaceNodeRecord : mDeferredInsertExplicitThisVector)
	{
		auto replaceNode = replaceNodeRecord.mNode;

		DbgType* castType = NULL;

		auto typeRef = BfNodeDynCast<BfTypeReference>(replaceNode);
		if ((typeRef != NULL) || (replaceNodeRecord.mForceTypeRef))
		{
			castType = ResolveTypeRef(replaceNode);
			if (castType == NULL)
				continue;
		}

		// Use an identifier as a text placeholder for the whole 'this' expression
		//  We must do this because we can't insert the actual 'this' node into multiple parents
		auto thisExprNode = source->mAlloc.Alloc<BfIdentifierNode>();
		BfAstNode* newNode = NULL;

		if (castType != NULL)
		{
			bfReducer.ReplaceNode(replaceNode, thisExprNode);
			newNode = thisExprNode;
		}
		else if (auto thisNode = BfNodeDynCast<BfThisExpression>(replaceNode))
		{
			bfReducer.ReplaceNode(replaceNode, thisExprNode);
			newNode = thisExprNode;
		}
		else if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(replaceNode))
		{
			auto memberRefNode = source->mAlloc.Alloc<BfMemberReferenceExpression>();
			bfReducer.ReplaceNode(replaceNode, memberRefNode);
			memberRefNode->mMemberName = identifierNode;

			auto tokeNode = source->mAlloc.Alloc<BfTokenNode>();
			tokeNode->SetToken(BfToken_Dot);
			bfReducer.MoveNode(tokeNode, memberRefNode);
			memberRefNode->mDotToken = tokeNode;

			bfReducer.MoveNode(thisExprNode, memberRefNode);
			memberRefNode->mTarget = thisExprNode;

			newNode = memberRefNode;
		}

		if (newNode != NULL)
		{
			if (replaceNodeRecord.mNodeRef != NULL)
				*replaceNodeRecord.mNodeRef = newNode;
			if (replaceNode == headNode)
				headNode = newNode;
		}

		String replaceString;
		if (castType != NULL)
		{
			replaceString = castType->ToString();
		}
		else
			replaceString = thisExprStr;

		int thisLen = (int)replaceString.length();
		int srcStart = bfParser->AllocChars(thisLen);
		memcpy((char*)source->mSrc + srcStart, replaceString.c_str(), thisLen);
		thisExprNode->Init(srcStart, srcStart, srcStart + thisLen);
	}

	return headNode;
}