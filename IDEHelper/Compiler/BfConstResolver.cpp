#pragma warning(push)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4146)
#pragma warning(disable:4267)
#pragma warning(disable:4291)

#include "BfCompiler.h"
#include "BfConstResolver.h"
#include "BfAutoComplete.h"
#include "BfResolvePass.h"
#include "llvm/IR/GlobalVariable.h"
#include "BfExprEvaluator.h"

#pragma warning(pop)

USING_NS_BF;
using namespace llvm;

bool BfConstResolver::CheckAllowValue(const BfTypedValue& typedValue, BfAstNode* refNode)
{
	if (typedValue.mValue.IsConst())
		return true;

	mModule->Fail("Expression does not evaluate to a constant value", refNode);
	return false;
}

BfConstResolver::BfConstResolver(BfModule* bfModule) : BfExprEvaluator(bfModule)
{
	mIsInvalidConstExpr = false;
	mAllowGenericConstValue = false;
}

BfTypedValue BfConstResolver::Resolve(BfExpression* expr, BfType* wantType, BfConstResolveFlags flags)
{
	mBfEvalExprFlags = (BfEvalExprFlags)(mBfEvalExprFlags | BfEvalExprFlags_Comptime);

	// Handle the 'int[?] val = .(1, 2, 3)' case
	if ((flags & BfConstResolveFlag_ArrayInitSize) != 0)
	{
		if (auto uninitExpr = BfNodeDynCast<BfUninitializedExpression>(expr))
		{
			BfAstNode* initializer = NULL;
			int arraySize = -1;

			if (mModule->mContext->mCurTypeState != NULL)
			{
				if (mModule->mContext->mCurTypeState->mCurFieldDef != NULL)
					initializer = mModule->mContext->mCurTypeState->mCurFieldDef->mInitializer;
				if (mModule->mContext->mCurTypeState->mCurVarInitializer != NULL)
					initializer = mModule->mContext->mCurTypeState->mCurVarInitializer;
				if (mModule->mContext->mCurTypeState->mArrayInitializerSize != -1)
					arraySize = mModule->mContext->mCurTypeState->mArrayInitializerSize;
			}

			if (initializer != NULL)
			{
				if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(initializer))
				{
					if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(invocationExpr->mTarget))
					{
						// Dot-initialized
						if (memberRefExpr->mTarget == NULL)
							arraySize = (int)invocationExpr->mArguments.size();
					}					
					else if (auto indexerExpr = BfNodeDynCast<BfIndexerExpression>(invocationExpr->mTarget))
					{
						if (indexerExpr->mArguments.size() == 0)
						{
							// Inferred-type sized array initializer
							arraySize = (int)invocationExpr->mArguments.size();
						}						
					}
				}
			}

			if (arraySize != -1)
			{				
				mResult = BfTypedValue(mModule->GetConstValue(arraySize), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
				return mResult;
			}
			else
			{				
				mResult = BfTypedValue(mModule->mBfIRBuilder->GetUndefConstValue(mModule->mBfIRBuilder->GetPrimitiveType(BfTypeCode_IntPtr)), mModule->GetPrimitiveType(BfTypeCode_IntPtr));
				return mResult;
			}
		}
	}

	bool explicitCast = (flags & BfConstResolveFlag_ExplicitCast) != 0;
	bool noCast = (flags & BfConstResolveFlag_NoCast) != 0;
	bool allowSoftFail = (flags & BfConstResolveFlag_AllowSoftFail) != 0;
	bool wantIgnoreWrites = mModule->mBfIRBuilder->mIgnoreWrites;
	/*if (BfNodeDynCastExact<BfTypeOfExpression>(expr) != NULL)
	{
		// Some specific expressions should be allowed to do writes like creating global variables
	}
	else*/
	{
		wantIgnoreWrites = true;
	}

	SetAndRestoreValue<bool> prevIgnoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, wantIgnoreWrites);
	
	auto prevInsertBlock = mModule->mBfIRBuilder->GetInsertBlock();

	mNoBind = true;
	if (wantType != NULL)
		mExpectingType = wantType;
	VisitChildNoRef(expr);
	
	mResult = GetResult();
	if ((mResult) && (wantType != NULL))
	{
		auto typeInst = mResult.mType->ToTypeInstance();
		if ((typeInst != NULL) && (typeInst->mTypeDef == mModule->mCompiler->mStringTypeDef))
		{
			BfType* toType = wantType;
			if (toType == NULL)
				toType = mResult.mType;

			if ((mResult.mValue.IsConst()) &&
				(((toType->IsPointer()) && (toType->GetUnderlyingType() == mModule->GetPrimitiveType(BfTypeCode_Char8))) ||
					(toType == mResult.mType)))
			{
				auto constant = mModule->mBfIRBuilder->GetConstant(mResult.mValue);
				
				if (constant->mTypeCode == BfTypeCode_NullPtr)
				{
					return mModule->GetDefaultTypedValue(toType);
				}
				else
				{
					int stringId = mModule->GetStringPoolIdx(mResult.mValue);					
					if (stringId != -1)
					{
						if ((flags & BfConstResolveFlag_ActualizeValues) != 0)
						{
							prevIgnoreWrites.Restore();
							mModule->mBfIRBuilder->PopulateType(mResult.mType);
							return BfTypedValue(mModule->GetStringObjectValue(stringId, false, true), mResult.mType);
						}

						return BfTypedValue(mModule->mBfIRBuilder->CreateConst(BfTypeCode_StringId, stringId), toType);
					}
				}
			}
		}

		if (noCast)
		{
			//
		}
		else if (allowSoftFail)
		{
			SetAndRestoreValue<bool> prevIgnoreFail(mModule->mIgnoreErrors, true);
			auto convValue = mModule->Cast(expr, mResult, wantType, explicitCast ? BfCastFlags_Explicit : BfCastFlags_None);
			if (convValue)
				mResult = convValue;
		}
		else
		{			
			mResult = mModule->Cast(expr, mResult, wantType, (BfCastFlags)(BfCastFlags_NoConversionOperator | (explicitCast ? BfCastFlags_Explicit : BfCastFlags_None)));
		}				
	}

	if (mResult.mKind == BfTypedValueKind_GenericConstValue)
	{
		if (mAllowGenericConstValue)
			return mResult;
		
		auto genericParamDef = mModule->GetGenericParamInstance((BfGenericParamType*)mResult.mType);
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

	if (mResult)
	{
		bool isConst = mResult.mValue.IsConst();

		if (isConst)
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(mResult.mValue);
			if ((constant->mConstType == BfConstType_GlobalVar) && ((mBfEvalExprFlags & BfConstResolveFlag_AllowGlobalVariable) != 0))
				isConst = false;			
		}
		
		if ((!isConst) && ((mBfEvalExprFlags & BfEvalExprFlags_AllowNonConst) == 0))
		{			
			mModule->Fail("Expression does not evaluate to a constant value", expr);

			if (wantType != NULL)
				mResult = mModule->GetDefaultTypedValue(wantType);
			else
				mResult = BfTypedValue();
		}
	}
	else
	{
		if (wantType != NULL)
			mResult = mModule->GetDefaultTypedValue(wantType);
	}	

	if (prevInsertBlock)
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	/*ignoreWrites.Restore();
	if ((!mModule->mBfIRBuilder->mIgnoreWrites) && (prevInsertBlock))
	{
		BF_ASSERT(!prevInsertBlock.IsFake());
		mModule->mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	}*/

	mModule->FixIntUnknown(mResult);	

	if ((flags & BfConstResolveFlag_NoActualizeValues) == 0)
		mModule->FixValueActualization(mResult, !prevIgnoreWrites.mPrevVal || ((flags & BfConstResolveFlag_ActualizeValues) != 0));

	return mResult;
}

bool BfConstResolver::PrepareMethodArguments(BfAstNode* targetSrc, BfMethodMatcher* methodMatcher, Array<BfIRValue>& llvmArgs)
{
	int argIdx = 0;
	int paramIdx = 0;

	int extendedParamIdx = 0;

	SetAndRestoreValue<bool> ignoreWrites(mModule->mBfIRBuilder->mIgnoreWrites, true);
	
	llvm::SmallVector<BfIRValue, 4> expandedParamsConstValues;
	BfType* expandedParamsElementType = NULL;
	
	// We don't do GetMethodInstance in mModule, because our module may not have init finished yet
	//auto targetModule = methodMatcher->mBestMethodTypeInstance->mModule;
	auto targetModule = mModule->mContext->mUnreifiedModule;
	auto moduleMethodInstance = targetModule->GetMethodInstance(methodMatcher->mBestMethodTypeInstance, methodMatcher->mBestMethodDef, methodMatcher->mBestMethodGenericArguments);
	auto methodInstance = moduleMethodInstance.mMethodInstance;

	if (methodInstance->mReturnType == NULL)
	{
		mModule->AssertErrorState();
		return false;
	}

	auto methodDef = methodMatcher->mBestMethodDef;
	auto& arguments = methodMatcher->mArguments;

	mModule->AddDependency(methodInstance->mReturnType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
	for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)	
	{
		auto paramType = methodInstance->GetParamType(paramIdx);
		mModule->AddDependency(paramType, mModule->mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);
	}

	while (true)
	{
		if (paramIdx >= (int)methodInstance->GetParamCount())
		{
			if (argIdx < (int)arguments.size())
			{				
				BfAstNode* errorRef = arguments[methodInstance->GetParamCount()].mExpression;
				if (errorRef->GetSourceData() == NULL)
					errorRef = targetSrc;
				mModule->Fail(StrFormat("Too many arguments. Expected %d fewer.", (int)arguments.size() - methodInstance->GetParamCount()), errorRef);
				if (methodInstance->mMethodDef->mMethodDeclaration != NULL)
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("See method declaration"), methodInstance->mMethodDef->mMethodDeclaration);
				return false;
			}
			break;
		}				

		BfType* wantType = NULL;
		if (expandedParamsElementType != NULL)
		{
			wantType = expandedParamsElementType;
		}
		else
		{
			wantType = methodInstance->GetParamType(paramIdx);

			if (methodInstance->GetParamKind(paramIdx) == BfParamKind_Params)
			{
				//TODO: Check to see if it's a direct array pass

				bool isDirectPass = false;
				if (argIdx < (int)arguments.size())
				{
					if (!arguments[argIdx].mTypedValue.mValue)
						return false;
					if (mModule->CanCast(arguments[argIdx].mTypedValue, wantType))
						isDirectPass = true;
				}

				if (!isDirectPass)
				{
					BfArrayType* arrayType = (BfArrayType*)wantType;
					if (arrayType->IsIncomplete())
						mModule->PopulateType(arrayType, BfPopulateType_DataAndMethods);
					expandedParamsElementType = arrayType->mGenericTypeInfo->mTypeGenericArguments[0];
					continue;
				}
			}
		}

		BfTypedValue argValue;
		BfAstNode* argExpr = NULL;
		if (argIdx < (int)arguments.size())
		{
			argExpr = arguments[argIdx].mExpression;
		}
		else
		{
			if (expandedParamsElementType != NULL)
				break;

			if ((argIdx >= (int)methodInstance->mDefaultValues.size()) || (!methodInstance->mDefaultValues[argIdx]))
			{
				BfAstNode* refNode = targetSrc;
				if (arguments.size() > 0)
					refNode = arguments.back().mExpression;

				BfAstNode* prevNode = NULL;
#ifdef BF_AST_HAS_PARENT_MEMBER
				if (auto attributeDirective = BfNodeDynCast<BfAttributeDirective>(targetSrc->mParent))
				{
					BF_ASSERT(mModule->mParentNodeEntry->mNode == attributeDirective);
				}
#endif
				if (mModule->mParentNodeEntry != NULL)
				{
					if (auto attributeDirective = BfNodeDynCast<BfAttributeDirective>(mModule->mParentNodeEntry->mNode))
					{
						if (attributeDirective->mCommas.size() > 0)
							prevNode = attributeDirective->mCommas.back();
						else
							prevNode = attributeDirective->mCtorOpenParen;
						if (attributeDirective->mCtorCloseParen != NULL)
							refNode = attributeDirective->mCtorCloseParen;
					}
				}				

				auto autoComplete = GetAutoComplete();
				if (autoComplete != NULL)
				{
					if (prevNode != NULL)
					{
						autoComplete->CheckEmptyStart(prevNode, wantType);
					}
				}
				
				if (mModule->PreFail())
					mModule->Fail(StrFormat("Not enough parameters specified. Expected %d fewer.", methodInstance->GetParamCount() - (int)arguments.size()), refNode);
				return false;
			}

			auto foreignDefaultVal = methodInstance->mDefaultValues[argIdx];
			auto foreignConst = methodInstance->GetOwner()->mConstHolder->GetConstant(foreignDefaultVal.mValue);			
			argValue = mModule->GetTypedValueFromConstant(foreignConst, methodInstance->GetOwner()->mConstHolder, foreignDefaultVal.mType);
		}

		if ((!argValue) && (argIdx < arguments.size()))
		{
			argValue = arguments[argIdx].mTypedValue;
			auto& arg = arguments[argIdx];
			if ((arg.mArgFlags & BfArgFlag_DeferredEval) != 0)
			{
				mExpectingType = arg.mExpectedType;
				if (mExpectingType == NULL)
					mExpectingType = wantType;

				if (auto expr = BfNodeDynCast<BfExpression>(argExpr))
					argValue = Resolve(expr, mExpectingType);
				arg.mArgFlags = BfArgFlag_None;
			}
		}
		
		if (!argValue)
			return BfTypedValue();

		if (argExpr != NULL)
		{
			argValue = mModule->Cast(argExpr, argValue, wantType);
			if (!argValue)
				return false;
		}

		if (!argValue)
		{
			mModule->Fail("Invalid expression type", argExpr);
			return false;
		}

		if (expandedParamsElementType != NULL)
		{
			expandedParamsConstValues.push_back(argValue.mValue);
			extendedParamIdx++;
		}
		else
		{	
			bool requiresConst = false;
			if ((mModule->mCurMethodInstance == NULL) || (mModule->mCurMethodInstance->mMethodDef->mMethodType != BfMethodType_Mixin))
				requiresConst = true;

			if ((requiresConst) && (argValue.mValue.IsFake()) && (!argValue.mType->IsValuelessType()))
			{				
				mModule->Fail("Expression does not evaluate to a constant value", argExpr);				
			}

			if (!argValue.mType->IsVar())
			{
				if ((!requiresConst) || (argValue.mValue.IsConst()) || (argValue.mType->IsValuelessType()))
					llvmArgs.push_back(argValue.mValue);
				else
					llvmArgs.push_back(mModule->GetDefaultValue(argValue.mType));
			}
			paramIdx++;
		}
		argIdx++;
	}

	if (expandedParamsElementType != NULL)
	{				
		auto arrayType = mModule->mBfIRBuilder->GetSizedArrayType(mModule->mBfIRBuilder->MapType(expandedParamsElementType), (int)expandedParamsConstValues.size());
		auto constArray = mModule->mBfIRBuilder->CreateConstAgg(arrayType, expandedParamsConstValues);
		llvmArgs.push_back(constArray);
	}	

	return true;
}