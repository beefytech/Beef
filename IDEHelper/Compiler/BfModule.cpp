//#define USE_THUNKED_MddLLOC..

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(disable:4996)
#pragma warning(push) // 6

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfCodeGen.h"
#include "BfExprEvaluator.h"
#include "../Backend/BeLibManger.h"
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
#include "BfDeferEvalChecker.h"
#include "CeMachine.h"
#include "CeDebugger.h"
#include <fcntl.h>
#include <time.h>

#pragma warning(pop)

//////////////////////////////////////////////////////////////////////////

static bool gDebugStuff = false;

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

void BfLocalVariable::Init()
{
	if (mResolvedType->IsValuelessType())
	{
		mAssignedKind = BfLocalVarAssignKind_Unconditional;
		return;
	}

	if (mAssignedKind != BfLocalVarAssignKind_None)
		return;

	bool isStruct = mResolvedType->IsStruct();
	if (mResolvedType->IsRef())
		isStruct = mResolvedType->GetUnderlyingType()->IsStruct();

	if ((isStruct) || ((mIsThis) && (mResolvedType->IsStructOrStructPtr())))
	{
		auto resolvedTypeRef = mResolvedType;
		if ((resolvedTypeRef->IsPointer()) || (resolvedTypeRef->IsRef()))
			resolvedTypeRef = resolvedTypeRef->GetUnderlyingType();
		auto typeInstance = resolvedTypeRef->ToTypeInstance();
		mUnassignedFieldFlags = (1 << typeInstance->mMergedFieldDataCount) - 1;
		if ((mIsThis) && (typeInstance->mBaseType != NULL))
		{
			// Base ctor is responsible for initializing its own fields
			mUnassignedFieldFlags &= ~(((int64)1 << typeInstance->mBaseType->mMergedFieldDataCount) - 1);
		}
		if (mUnassignedFieldFlags == 0)
			mAssignedKind = BfLocalVarAssignKind_Unconditional;
	}
	else
	{
		mUnassignedFieldFlags = 1;
	}
}

BfLocalMethod::~BfLocalMethod()
{
	BfLogSys(mSystem, "~BfLocalMethod %p\n", this);
	if (mMethodDeclaration != NULL)
	{
		mSource->mRefCount--;
		BF_ASSERT(mSource->mRefCount >= 0);
	}

	delete mMethodInstanceGroup;
	delete mMethodDef;
}

void BfLocalMethod::Dispose()
{
	if (mMethodInstanceGroup == NULL)
		return;
	if (mMethodInstanceGroup->mDefault != NULL)
		mMethodInstanceGroup->mDefault->Dispose();

	if (mMethodInstanceGroup->mMethodSpecializationMap != NULL)
	{
		for (auto& kv : *mMethodInstanceGroup->mMethodSpecializationMap)
			kv.mValue->Dispose();
	}
}

void BfDeferredLocalAssignData::ExtendFrom(BfDeferredLocalAssignData* outerLocalAssignData, bool doChain)
{
	mIsChained = doChain;
	if (outerLocalAssignData == NULL)
		return;
	mChainedAssignData = outerLocalAssignData;
	if (!doChain)
	{
		outerLocalAssignData->BreakExtendChain();
		mAssignedLocals = outerLocalAssignData->mAssignedLocals;
	}
	mVarIdBarrier = outerLocalAssignData->mVarIdBarrier;
}

// The "extend chain" is broken when we have a conditional where the variable may not be defined after the block.
// IE: "a" will be defined after the following, but "b" will not necessarily be defined.
//  if ((GetValue(out a)) && (GetValue(out b)) {}
void BfDeferredLocalAssignData::BreakExtendChain()
{
	if (!mIsChained)
		return;
	mIsChained = false;
	if (mChainedAssignData == NULL)
		return;
	mChainedAssignData->BreakExtendChain();
	mAssignedLocals = mChainedAssignData->mAssignedLocals;
}

void BfDeferredLocalAssignData::SetIntersection(const BfDeferredLocalAssignData& otherLocalAssignData)
{
	BreakExtendChain();

	if (otherLocalAssignData.mLeftBlockUncond)
	{
		// Intersection of self and infinity is self
	}
	else if (mLeftBlockUncond)
	{
		// Intersection of infinity and other is other
		mAssignedLocals = otherLocalAssignData.mAssignedLocals;
	}
	else
	{
		for (int i = 0; i < (int)mAssignedLocals.size(); )
		{
			auto& local = mAssignedLocals[i];

			bool wantRemove = true;
			bool foundOtherFields = false;
			for (auto& otherLocalAssignData : otherLocalAssignData.mAssignedLocals)
			{
				if (otherLocalAssignData.mLocalVar == local.mLocalVar)
				{
					if ((otherLocalAssignData.mLocalVarField == local.mLocalVarField) || (otherLocalAssignData.mLocalVarField == -1))
					{
						if (otherLocalAssignData.mAssignKind == BfLocalVarAssignKind_Conditional)
							local.mAssignKind = BfLocalVarAssignKind_Conditional;
						wantRemove = false;
					}
					else
						foundOtherFields = true;
				}
			}

			if ((wantRemove) && (foundOtherFields))
			{
				for (auto& otherLocalAssignData : otherLocalAssignData.mAssignedLocals)
				{
					if (otherLocalAssignData.mLocalVar == local.mLocalVar)
					{
						mAssignedLocals.Add(otherLocalAssignData);
					}
				}
			}

			if (wantRemove)
			{
				mAssignedLocals.RemoveAt(i);
			}
			else
				i++;
		}
	}

	mHadFallthrough = mHadFallthrough && otherLocalAssignData.mHadFallthrough;
	mLeftBlockUncond = mLeftBlockUncond && otherLocalAssignData.mLeftBlockUncond;
}

void BfDeferredLocalAssignData::Validate() const
{
	for (auto var : mAssignedLocals)
	{
		BF_ASSERT((uintptr)var.mLocalVar->mName.length() < 100000);
	}
}

void BfDeferredLocalAssignData::SetUnion(const BfDeferredLocalAssignData& otherLocalAssignData)
{
	BreakExtendChain();

	Validate();
	otherLocalAssignData.Validate();

	auto otherItr = otherLocalAssignData.mAssignedLocals.begin();
	while (otherItr != otherLocalAssignData.mAssignedLocals.end())
	{
		if (!mAssignedLocals.Contains(*otherItr))
			mAssignedLocals.push_back(*otherItr);
		++otherItr;
	}
	mHadFallthrough = mHadFallthrough || otherLocalAssignData.mHadFallthrough;
	mLeftBlockUncond = mLeftBlockUncond || otherLocalAssignData.mLeftBlockUncond;
}

BfMethodState::~BfMethodState()
{
	BF_ASSERT(mPendingNullConditional == NULL);

	if (mPrevMethodState != NULL)
	{
		BF_ASSERT(mCurAccessId == 1);
		BF_ASSERT(mCurLocalVarId <= 0);
	}

	for (auto local : mLocals)
	{
		if (local->mIsBumpAlloc)
			local->~BfLocalVariable();
		else
			delete local;
	}

	for (auto& kv : mLambdaCache)
		delete kv.mValue;
}

BfMethodState* BfMethodState::GetMethodStateForLocal(BfLocalVariable* localVar)
{
	auto checkMethodState = this;
	while (checkMethodState != NULL)
	{
		if ((localVar->mLocalVarIdx < checkMethodState->mLocals.size()) &&
			(checkMethodState->mLocals[localVar->mLocalVarIdx] == localVar))
			return checkMethodState;
		checkMethodState = checkMethodState->mPrevMethodState;
	}

	BF_FATAL("Failed to find method state for localvar");
	return NULL;
}

void BfMethodState::LocalDefined(BfLocalVariable* localVar, int fieldIdx, BfLocalVarAssignKind assignKind, bool isFromDeferredAssignData)
{
	auto localVarMethodState = GetMethodStateForLocal(localVar);
	if (localVarMethodState != this)
	{
		return;
	}

	if (localVar->mAssignedKind == BfLocalVarAssignKind_None)
	{
		BfDeferredLocalAssignData* ifDeferredLocalAssignData = NULL;

		// 'a' is always defined, but 'b' isn't:
		//  if (Check(out a) || Check(out b)) { } int z = a;
		auto deferredLocalAssignData = mDeferredLocalAssignData;
		if ((deferredLocalAssignData != NULL) &&
			(deferredLocalAssignData->mIsIfCondition) &&
			(!deferredLocalAssignData->mIfMayBeSkipped))
		{
			ifDeferredLocalAssignData = deferredLocalAssignData;
			deferredLocalAssignData = deferredLocalAssignData->mChainedAssignData;
		}

		while ((deferredLocalAssignData != NULL) &&
			((deferredLocalAssignData->mIsChained) || (deferredLocalAssignData->mIsUnconditional)))
			deferredLocalAssignData = deferredLocalAssignData->mChainedAssignData;

		if (assignKind == BfLocalVarAssignKind_None)
			assignKind = ((deferredLocalAssignData != NULL) && (deferredLocalAssignData->mLeftBlock)) ? BfLocalVarAssignKind_Conditional : BfLocalVarAssignKind_Unconditional;

		if (localVar->mAssignedKind >= assignKind)
		{
			// Leave it alone
		}
		else if ((deferredLocalAssignData == NULL) || (localVar->mLocalVarId >= deferredLocalAssignData->mVarIdBarrier))
		{
			if (fieldIdx >= 0)
			{
				localVar->mUnassignedFieldFlags &= ~((int64)1 << fieldIdx);

				if (localVar->mResolvedType->IsUnion())
				{
					// We need more 'smarts' to determine assignment of unions
					localVar->mUnassignedFieldFlags = 0;
				}

				if (localVar->mUnassignedFieldFlags == 0)
				{
					if (localVar->mAssignedKind == BfLocalVarAssignKind_None)
						localVar->mAssignedKind = assignKind;
				}
			}
			else
			{
				localVar->mAssignedKind = assignKind;
			}
		}
		else
		{
			BF_ASSERT(deferredLocalAssignData->mVarIdBarrier != -1);

			BfAssignedLocal defineVal = {localVar, fieldIdx, assignKind};
			if (!deferredLocalAssignData->Contains(defineVal))
				deferredLocalAssignData->mAssignedLocals.push_back(defineVal);

			if (ifDeferredLocalAssignData != NULL)
			{
				if (!ifDeferredLocalAssignData->Contains(defineVal))
					ifDeferredLocalAssignData->mAssignedLocals.push_back(defineVal);
			}
		}
	}
	localVar->mWrittenToId = GetRootMethodState()->mCurAccessId++;
}

void BfMethodState::ApplyDeferredLocalAssignData(const BfDeferredLocalAssignData& deferredLocalAssignData)
{
	BF_ASSERT(&deferredLocalAssignData != mDeferredLocalAssignData);

	if (deferredLocalAssignData.mLeftBlockUncond)
	{
		for (int localIdx = 0; localIdx < (int)mLocals.size(); localIdx++)
		{
			auto localDef = mLocals[localIdx];
			if (localDef->mAssignedKind == BfLocalVarAssignKind_None)
			{
				bool hadAssignment = false;
				if (mDeferredLocalAssignData != NULL)
				{
					for (auto& entry : mDeferredLocalAssignData->mAssignedLocals)
						if (entry.mLocalVar == localDef)
							hadAssignment = true;
				}
				if (!hadAssignment)
				{
					LocalDefined(localDef);
				}
			}
		}
	}
	else
	{
		for (auto& assignedLocal : deferredLocalAssignData.mAssignedLocals)
		{
			LocalDefined(assignedLocal.mLocalVar, assignedLocal.mLocalVarField, assignedLocal.mAssignKind, true);
		}
	}
}

void BfMethodState::Reset()
{
	mHeadScope.mDeferredCallEntries.DeleteAll();
}

//////////////////////////////////////////////////////////////////////////

void BfAmbiguityContext::Add(int id, BfTypeInterfaceEntry* interfaceEntry, int methodIdx, BfMethodInstance* candidateA, BfMethodInstance* candidateB)
{
	Entry* entry = NULL;

	if (mEntries.TryAdd(id, NULL, &entry))
	{
		entry->mInterfaceEntry = interfaceEntry;
		entry->mMethodIdx = methodIdx;
	}
	if (!entry->mCandidates.Contains(candidateA))
		entry->mCandidates.push_back(candidateA);
	if (!entry->mCandidates.Contains(candidateB))
		entry->mCandidates.push_back(candidateB);
}

void BfAmbiguityContext::Remove(int id)
{
	mEntries.Remove(id);
}

void BfAmbiguityContext::Finish()
{
	for (auto& entryPair : mEntries)
	{
		int id = entryPair.mKey;
		auto entry = &entryPair.mValue;

		if (id >= 0)
		{
			auto declMethodInstance = mTypeInstance->mVirtualMethodTable[id].mDeclaringMethod;
			auto error = mModule->Fail(StrFormat("Method '%s' has ambiguous overrides", mModule->MethodToString(declMethodInstance).c_str()), declMethodInstance->mMethodDef->GetRefNode());
			if (error != NULL)
			{
				for (auto candidate : entry->mCandidates)
				{
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate",
						mModule->MethodToString(candidate, (BfMethodNameFlags)(BfMethodNameFlag_ResolveGenericParamNames | BfMethodNameFlag_IncludeReturnType)).c_str()), candidate->mMethodDef->GetRefNode());
				}
			}
		}
		else
		{
			auto iMethodInst = entry->mInterfaceEntry->mInterfaceType->mMethodInstanceGroups[entry->mMethodIdx].mDefault;
			auto error = mModule->Fail(StrFormat("Interface method '%s' has ambiguous implementations", mModule->MethodToString(iMethodInst).c_str()), entry->mInterfaceEntry->mDeclaringType->GetRefNode());
			if (error != NULL)
			{
				for (auto candidate : entry->mCandidates)
				{
					mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate",
						mModule->MethodToString(candidate, (BfMethodNameFlags)(BfMethodNameFlag_ResolveGenericParamNames | BfMethodNameFlag_IncludeReturnType)).c_str()), candidate->mMethodDef->GetRefNode());
				}
			}
		}
	}
	mEntries.Clear();
}

//////////////////////////////////////////////////////////////////////////

class HasAppendAllocVisitor : BfElementVisitor
{
public:
	bool mHas;

public:
	HasAppendAllocVisitor()
	{
		mHas = false;
	}

	void Visit(BfObjectCreateExpression* objCreateExpr)
	{
		if (auto newToken = BfNodeDynCast<BfTokenNode>(objCreateExpr->mNewNode))
		{
			if (newToken->GetToken() == BfToken_Append)
			{
				mHas = true;
			}
		}
	}

	bool HasAppendAlloc(BfAstNode* node)
	{
		mHas = false;
		VisitChild(node);
		return mHas;
	}
};

class AppendAllocVisitor : public BfStructuralVisitor
{
public:
	BfModule* mModule;
	HasAppendAllocVisitor mHasAppendAllocVisitor;
	BfTypedValue mConstAccum;
	bool mFailed;
	bool mIsFirstConstPass;
	int mCurAppendAlign;

public:
	AppendAllocVisitor()
	{
		mFailed = false;
		mIsFirstConstPass = false;
		mCurAppendAlign = 1;
	}

	void EmitAppendAlign(int align, int sizeMultiple = 0)
	{
		if (align <= 1)
			return;

		if (mIsFirstConstPass)
			mModule->mCurMethodInstance->mAppendAllocAlign = BF_MAX((int)mModule->mCurMethodInstance->mAppendAllocAlign, align);

		if (sizeMultiple == 0)
			sizeMultiple = align;
		if (mCurAppendAlign == 0)
			mCurAppendAlign = sizeMultiple;
		else
		{
			if (sizeMultiple % align == 0)
				mCurAppendAlign = align;
			else
				mCurAppendAlign = sizeMultiple % align;
		}

		BF_ASSERT(mCurAppendAlign > 0);

		if (mConstAccum)
		{
			auto constant = mModule->mBfIRBuilder->GetConstant(mConstAccum.mValue);
			if (constant != NULL)
				mConstAccum.mValue = mModule->GetConstValue(BF_ALIGN(constant->mInt64, align));
			else
				BF_ASSERT(mIsFirstConstPass);
			return;
		}

		mModule->EmitAppendAlign(align, sizeMultiple);
	}

	void Visit(BfAstNode* astNode) override
	{
		mModule->VisitChild(astNode);
	}

	void DoObjectCreate(BfObjectCreateExpression* objCreateExpr, BfVariableDeclaration* variableDecl)
	{
		if (auto newToken = BfNodeDynCast<BfTokenNode>(objCreateExpr->mNewNode))
		{
			if (newToken->GetToken() == BfToken_Append)
			{
				//auto variableDecl = BfNodeDynCast<BfVariableDeclaration>(objCreateExpr->mParent);

				auto accumValuePtr = mModule->mCurMethodState->mRetVal.mValue;
				auto intPtrType = mModule->GetPrimitiveType(BfTypeCode_IntPtr);

				BfArrayType* arrayType = NULL;
				bool isArrayAlloc = false;
				bool isRawArrayAlloc = objCreateExpr->mStarToken != NULL;

				SizedArray<BfIRValue, 2> dimLengthVals;

				BfType* origResolvedTypeRef = NULL;
				if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(objCreateExpr->mTypeRef))
				{
					if (variableDecl != NULL)
					{
						origResolvedTypeRef = mModule->ResolveTypeRef(variableDecl->mTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_NoResolveGenericParam);

						if (origResolvedTypeRef->IsObject())
						{
							if (origResolvedTypeRef->IsArray())
							{
								arrayType = (BfArrayType*)origResolvedTypeRef;
								origResolvedTypeRef = origResolvedTypeRef->GetUnderlyingType();
								isArrayAlloc = true;
							}
						}
						else if (origResolvedTypeRef->IsPointer())
						{
							origResolvedTypeRef = origResolvedTypeRef->GetUnderlyingType();
						}
					}
				}
				else if (auto arrayTypeRef = BfNodeDynCast<BfArrayTypeRef>(objCreateExpr->mTypeRef))
				{
					isArrayAlloc = true;

					bool handled = false;

					if (auto dotTypeRef = BfNodeDynCast<BfDotTypeReference>(arrayTypeRef->mElementType))
					{
						if (variableDecl->mTypeRef != NULL)
						{
							auto variableType = mModule->ResolveTypeRef(variableDecl->mTypeRef);
							if (variableType != NULL)
							{
								if (variableType->IsArray())
									origResolvedTypeRef = variableType->GetUnderlyingType();
							}
							handled = true;
						}
					}

					if (!handled)
					{
						origResolvedTypeRef = mModule->ResolveTypeRef(arrayTypeRef->mElementType);
					}

					if (origResolvedTypeRef == NULL)
					{
						mModule->AssertErrorState();
						return;
					}

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
										mModule->Fail("Sized arrays cannot be multidimensional", tokenNode);
										continue;
									}

									dimensions++;
									continue;
								}
							}
							auto expr = BfNodeDynCast<BfExpression>(arg);
							if ((isRawArrayAlloc) && (!dimLengthVals.IsEmpty()))
							{
								mModule->CreateValueFromExpression(expr, intType);
								continue;
							}

							BfTypedValue dimLength;
							if (expr == NULL)
							{
								// Not specified
								dimLengthVals.push_back(BfIRValue());
								continue;
							}

							if (arg != NULL)
								dimLength = mModule->CreateValueFromExpression(expr, intType);
							if (!dimLength)
							{
								dimLength = mModule->GetDefaultTypedValue(intType);
							}
							dimLengthVals.push_back(dimLength.mValue);
						}
					}

					if (!isRawArrayAlloc)
						arrayType = mModule->CreateArrayType(origResolvedTypeRef, dimensions);
				}

				if (origResolvedTypeRef == NULL)
					origResolvedTypeRef = mModule->ResolveTypeRef(objCreateExpr->mTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_NoResolveGenericParam);
				if (origResolvedTypeRef == NULL)
				{
					mModule->AssertErrorState();
					return;
				}

				bool isGenericParam = origResolvedTypeRef->IsGenericParam();
				auto resolvedTypeRef = origResolvedTypeRef;
				auto resultType = resolvedTypeRef;

				BfIRValue sizeValue;
				int curAlign = 0;

				if (isArrayAlloc)
				{
					int dimensions = 1;
					if (arrayType != NULL)
						dimensions = arrayType->mDimensions;
					bool sizeFailed = false;

					//
					{
						BfAstNode* initNode = objCreateExpr;

						for (int dim = 0; dim < dimensions; dim++)
						{
							BfAstNode* nextInitNode = NULL;
							int dimSize = -1;

							if (initNode == objCreateExpr)
							{
								dimSize = objCreateExpr->mArguments.size();
								if (!objCreateExpr->mArguments.IsEmpty())
									nextInitNode = objCreateExpr->mArguments[0];
							}
							else if (auto tupleNode = BfNodeDynCast<BfTupleExpression>(initNode))
							{
								dimSize = (int)tupleNode->mCommas.size() + 1;
							}

							if (dimSize >= 0)
							{
								if (dim >= dimLengthVals.size())
									dimLengthVals.Add(mModule->GetConstValue(dimSize));
							}
							else
							{
								sizeFailed = true;
							}
						}
					}

					BfIRValue allocCount;
					if (!sizeFailed)
					{
						if (!dimLengthVals.IsEmpty())
						{
							allocCount = dimLengthVals[0];
							for (int dim = 1; dim < (int)dimLengthVals.size(); dim++)
								allocCount = mModule->mBfIRBuilder->CreateMul(allocCount, dimLengthVals[dim]);
						}
					}

					if (!allocCount)
					{
						mFailed = true;
						if (!mIsFirstConstPass)
						{
							if (!mConstAccum)
								mModule->Fail("Unable to determine append alloc size", objCreateExpr->mNewNode);
							return;
						}
					}

					mModule->PopulateType(resultType);

					if (isRawArrayAlloc)
					{
						curAlign = resultType->mAlign;
						EmitAppendAlign(resultType->mAlign);
						sizeValue = mModule->mBfIRBuilder->CreateMul(mModule->GetConstValue(resultType->GetStride()), allocCount);
					}
					else
					{
						if (arrayType == NULL)
							arrayType = mModule->CreateArrayType(resultType, 1);

						// Array is a special case where the total size isn't aligned with mAlign
						//  since we add arbitrary elements to the end without padding the end to align
						EmitAppendAlign(arrayType->mAlign, resultType->mAlign);
						curAlign = arrayType->mAlign;

						auto firstElementField = &arrayType->mFieldInstances.back();
						int arrayClassSize = arrayType->mInstSize - firstElementField->mDataSize;

						sizeValue = mModule->GetConstValue(arrayClassSize);
						BfIRValue elementDataSize = mModule->mBfIRBuilder->CreateMul(mModule->GetConstValue(resultType->GetStride()), allocCount);
						sizeValue = mModule->mBfIRBuilder->CreateAdd(sizeValue, elementDataSize);
					}
				}
				else
				{
					auto typeInst = resultType->ToTypeInstance();
					if (typeInst != NULL)
					{
						curAlign = typeInst->mInstAlign;
						EmitAppendAlign(typeInst->mInstAlign, typeInst->mInstSize);
						sizeValue = mModule->GetConstValue(typeInst->mInstSize);
					}
					else
					{
						curAlign = resultType->mAlign;
						EmitAppendAlign(resultType->mAlign, resultType->mSize);
						sizeValue = mModule->GetConstValue(resultType->mSize);
					}

					BfTypedValue emtpyThis(mModule->mBfIRBuilder->GetFakeVal(), resultType, (typeInst == NULL) || (typeInst->IsComposite()));
					BfExprEvaluator exprEvaluator(mModule);
					SetAndRestoreValue<bool> prevNoBind(exprEvaluator.mNoBind, exprEvaluator.mNoBind || isGenericParam);
					BfFunctionBindResult bindResult;
					bindResult.mWantsArgs = true;
					exprEvaluator.mFunctionBindResult = &bindResult;
					SizedArray<BfExpression*, 8> copiedArgs;
					for (BfExpression* arg : objCreateExpr->mArguments)
						copiedArgs.push_back(arg);
					BfSizedArray<BfExpression*> sizedArgExprs(copiedArgs);
					BfResolvedArgs argValues(&sizedArgExprs);
					if (typeInst != NULL)
					{
						exprEvaluator.ResolveArgValues(argValues);
						exprEvaluator.MatchConstructor(objCreateExpr->mTypeRef, objCreateExpr, emtpyThis, typeInst, argValues, false, true);
					}
					exprEvaluator.mFunctionBindResult = NULL;

					if (bindResult.mMethodInstance != NULL)
					{
						if (bindResult.mMethodInstance->mMethodDef->mHasAppend)
						{
							auto calcAppendArgs = bindResult.mIRArgs;
							BF_ASSERT(calcAppendArgs[0].IsFake());
							calcAppendArgs.RemoveRange(0, 2); // Remove 'this' and 'appendIdx'

							auto calcAppendMethodModule = mModule->GetMethodInstanceAtIdx(bindResult.mMethodInstance->GetOwner(), bindResult.mMethodInstance->mMethodDef->mIdx + 1, BF_METHODNAME_CALCAPPEND);

							auto subDependSize = mModule->TryConstCalcAppend(calcAppendMethodModule.mMethodInstance, calcAppendArgs);
							if (calcAppendMethodModule.mMethodInstance->mAppendAllocAlign > 0)
							{
								EmitAppendAlign(calcAppendMethodModule.mMethodInstance->mAppendAllocAlign);
								BF_ASSERT(calcAppendMethodModule.mMethodInstance->mEndingAppendAllocAlign > -1);
								mModule->mCurMethodState->mCurAppendAlign = BF_MAX(calcAppendMethodModule.mMethodInstance->mEndingAppendAllocAlign, 0);
							}

							curAlign = std::max(curAlign, (int)calcAppendMethodModule.mMethodInstance->mAppendAllocAlign);
							if ((!subDependSize) && (!mConstAccum))
							{
								BF_ASSERT(calcAppendMethodModule.mFunc);

								subDependSize = exprEvaluator.CreateCall(objCreateExpr, calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, calcAppendArgs);
								BF_ASSERT(subDependSize.mType == mModule->GetPrimitiveType(BfTypeCode_IntPtr));
							}
							if (subDependSize)
								sizeValue = mModule->mBfIRBuilder->CreateAdd(sizeValue, subDependSize.mValue);
							else
								mFailed = true;
						}
					}
				}

				if (sizeValue)
				{
					if (mConstAccum)
					{
						if (!sizeValue.IsConst())
						{
							mFailed = true;
							if (!mIsFirstConstPass)
								return;
						}

						mConstAccum.mValue = mModule->mBfIRBuilder->CreateAdd(sizeValue, mConstAccum.mValue);
					}
					else
					{
						auto prevVal = mModule->mBfIRBuilder->CreateAlignedLoad(accumValuePtr, intPtrType->mAlign);
						auto addedVal = mModule->mBfIRBuilder->CreateAdd(sizeValue, prevVal);
						mModule->mBfIRBuilder->CreateAlignedStore(addedVal, accumValuePtr, intPtrType->mAlign);
					}
				}
			}
		}
	}

	void Visit(BfObjectCreateExpression* objCreateExpr) override
	{
		DoObjectCreate(objCreateExpr, NULL);
	}

	void Visit(BfVariableDeclaration* varDecl) override
	{
		if (mHasAppendAllocVisitor.HasAppendAlloc(varDecl))
		{
			if (auto objectCreateExpr = BfNodeDynCast<BfObjectCreateExpression>(varDecl->mInitializer))
				DoObjectCreate(objectCreateExpr, varDecl);
			else
				VisitChild(varDecl->mInitializer);

			if (varDecl->mNameNode != NULL)
			{
				BfLocalVariable* localDef = new BfLocalVariable();
				localDef->mName = varDecl->mNameNode->ToString();
				localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(varDecl->mNameNode);
				localDef->mResolvedType = mModule->GetPrimitiveType(BfTypeCode_None);
				localDef->mIsReadOnly = true;
				localDef->mParamFailed = true;
				localDef->mReadFromId = 0;
				mModule->AddLocalVariableDef(localDef);
			}
		}
		else
		{
			mModule->Visit(varDecl);
		}
	}

	void Visit(BfExpressionStatement* exprStatement) override
	{
		VisitChild(exprStatement->mExpression);
	}

// 	void Visit(BfIfStatement* ifStmt) override
// 	{
// 		mModule->DoIfStatement(ifStmt,
// 			mHasAppendAllocVisitor.HasAppendAlloc(ifStmt->mTrueStatement),
// 			mHasAppendAllocVisitor.HasAppendAlloc(ifStmt->mFalseStatement));
// 	}

	void Visit(BfBlock* block) override
	{
		if (!mHasAppendAllocVisitor.HasAppendAlloc(block))
			return;

		int appendAllocIdx = -1;
		for (int childIdx = block->mChildArr.mSize - 1; childIdx >= 0; childIdx--)
		{
			auto child = block->mChildArr.mVals[childIdx];
			if (mHasAppendAllocVisitor.HasAppendAlloc(child))
			{
				for (int emitIdx = 0; emitIdx <= childIdx; emitIdx++)
				{
					auto emitChild = block->mChildArr.mVals[emitIdx];

					mModule->UpdateSrcPos(emitChild);
					VisitChild(emitChild);

					if ((mFailed) && (!mIsFirstConstPass))
						break;
				}

				break;
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////

BfModule* gLastCreatedModule = NULL;

BfModule::BfModule(BfContext* context, const StringImpl& moduleName)
{
	BfLogSys(context->mSystem, "BfModule::BFModule %p %s\n", this, moduleName.c_str());

	gLastCreatedModule = this;

	mContext = context;
	mModuleName = moduleName;
	if (!moduleName.empty())
	{
		StringT<256> upperModuleName = moduleName;
		MakeUpper(upperModuleName);
		mContext->mUsedModuleNames.Add(upperModuleName);
	}
	mAddedToCount = false;

	mParentModule = NULL;
	mNextAltModule = NULL;
	mBfIRBuilder = NULL;
	mWantsIRIgnoreWrites = false;
	mModuleOptions = NULL;
	mLastUsedRevision = -1;
	mUsedSlotCount = -1;

	mIsReified = true;
	mGeneratesCode = true;
	mReifyQueued = false;
	mIsSpecialModule = false;
	mIsComptimeModule = false;
	mIsScratchModule = false;
	mIsSpecializedMethodModuleRoot = false; // There may be mNextAltModules extending from this
	mHadBuildError = false;
	mHadBuildWarning = false;
	mIgnoreErrors = false;
	mHadIgnoredError = false;
	mIgnoreWarnings = false;
	mReportErrors = true;
	mIsInsideAutoComplete = false;
	mIsDeleting = false;
	mSkipInnerLookup = false;
	mIsHotModule = false;
	mSetIllegalSrcPosition = false;
	mNoResolveGenericParams = false;
	mWroteToLib = false;
	mContext = context;
	mCompiler = context->mCompiler;
	mSystem = mCompiler->mSystem;
	mProject = NULL;
	mCurMethodState = NULL;
	mAttributeState = NULL;
	mCurLocalMethodId = 0;
	mParentNodeEntry = NULL;

	mRevision = -1;
	mRebuildIdx = 0;
	mLastModuleWrittenRevision = 0;
	mIsModuleMutable = false;
	mExtensionCount = 0;
	mIncompleteMethodCount = 0;
	mOnDemandMethodCount = 0;
	mHasGenericMethods = false;
	mCurMethodInstance = NULL;
	mCurTypeInstance = NULL;
	mAwaitingInitFinish = false;
	mAwaitingFinish = false;
	mHasFullDebugInfo = false;
	mHadHotObjectWrites = false;

	for (int i = 0; i < BfBuiltInFuncType_Count; i++)
		mBuiltInFuncs[i] = BfIRFunction();

	BfLogSysM("Creating module %p: %s Reified: %d ResolveOnly: %d\n", this, mModuleName.c_str(), mIsReified, mCompiler->mIsResolveOnly);
}

void BfReportMemory();

BfModule::~BfModule()
{
	mRevision = -2;

	BfLogSysM("Deleting module %p: %s \n", this, mModuleName.c_str());

	if (!mIsDeleting)
		RemoveModuleData();
}

void BfModule::RemoveModuleData()
{
	BF_ASSERT(!mIsDeleting);

	if (!mModuleName.empty())
	{
		// Note: module names not necessarily unique
		mContext->mUsedModuleNames.Remove(ToUpper(mModuleName));
	}

	CleanupFileInstances();
	delete mBfIRBuilder;
	mBfIRBuilder = NULL;

	//delete mCpu;
	for (auto prevModule : mPrevIRBuilders)
		delete prevModule;
	mPrevIRBuilders.Clear();

	for (auto& pairVal : mDeferredMethodCallData)
		delete pairVal.mValue;
	mDeferredMethodCallData.Clear();
	mDeferredMethodIds.Clear();

	for (auto& specModulePair : mSpecializedMethodModules)
		delete specModulePair.mValue;
	mSpecializedMethodModules.Clear();

	if (mNextAltModule != NULL)
		delete mNextAltModule;
	mNextAltModule = NULL;
	if (mModuleOptions != NULL)
		delete mModuleOptions;
	mModuleOptions = NULL;

	BfReportMemory();
}

void BfModule::Init(bool isFullRebuild)
{
	mContext->mFinishedModuleWorkList.Remove(this);

	if ((mCompiler->mIsResolveOnly) && (this != mContext->mUnreifiedModule))
		BF_ASSERT(mIsReified);

	if (!mIsScratchModule)
	{
		mCompiler->mStats.mModulesStarted++;
		if (mIsReified)
			mCompiler->mStats.mReifiedModuleCount++;
		mAddedToCount = true;
	}

	mIsHotModule = (mProject != NULL) && (mCompiler->mOptions.mHotProject != NULL) && (mCompiler->mOptions.mHotProject->ContainsReference(mProject));

	mFuncReferences.Clear();
	mClassVDataRefs.Clear();
	mClassVDataExtRefs.Clear();
	//mTypeDataRefs.Clear();
	mDbgRawAllocDataRefs.Clear();
	CleanupFileInstances();
	mStaticFieldRefs.Clear();
	//mInterfaceSlotRefs.Clear();

	// If we are just doing an extension then the ownede types aren't rebuilt.
	//  If we set mRevision then QueueMethodSpecializations wouldn't actually queue up required specializations
	//  and we'd end up with link errors if the original module uniquely referred to any generic methods
	if (isFullRebuild)
		mRevision = mCompiler->mRevision;

	BF_ASSERT(mCurTypeInstance == NULL);

	mIsModuleMutable = true;
	BF_ASSERT((mBfIRBuilder == NULL) || (mCompiler->mIsResolveOnly));
	if (!mIsComptimeModule)
	{
#ifdef _DEBUG
		EnsureIRBuilder(mCompiler->mLastAutocompleteModule == this);
#else
		EnsureIRBuilder(false);
#endif
	}

	mCurMethodState = NULL;
	mAwaitingInitFinish = true;
	mOnDemandMethodCount = 0;
	mAwaitingFinish = false;
	mHasForceLinkMarker = false;
	mUsedSlotCount = -1;
}

bool BfModule::WantsFinishModule()
{
	return (mIncompleteMethodCount == 0) && (mOnDemandMethodCount == 0) && (!mHasGenericMethods) && (!mIsScratchModule) && (mExtensionCount == 0) && (mParentModule == NULL);
}

bool BfModule::IsHotCompile()
{
	return mCompiler->IsHotCompile() && !mIsComptimeModule;
}

void BfModule::FinishInit()
{
	BF_ASSERT(mAwaitingInitFinish);

	auto moduleOptions = GetModuleOptions();

	mBfIRBuilder->Start(mModuleName, mCompiler->mSystem->mPtrSize, IsOptimized());

	mBfIRBuilder->Module_SetTargetTriple(mCompiler->mOptions.mTargetTriple, mCompiler->mOptions.mTargetCPU);

	mBfIRBuilder->SetBackend(IsTargetingBeefBackend());

	if (moduleOptions.mOptLevel == BfOptLevel_OgPlus)
	{
		// Og+ requires debug info
		moduleOptions.mEmitDebugInfo = 1;
	}

	mHasFullDebugInfo = moduleOptions.mEmitDebugInfo == 1;

	if (mIsComptimeModule)
		mHasFullDebugInfo = true;

	if (((!mCompiler->mIsResolveOnly) && (!mIsScratchModule) && (moduleOptions.mEmitDebugInfo != 0) && (mIsReified)) ||
		(mIsComptimeModule))
	{
		mBfIRBuilder->DbgInit();
	}
	else
		mHasFullDebugInfo = false;

	if ((mBfIRBuilder->DbgHasInfo()) && (mModuleName != "") &&
		((moduleOptions.mEmitDebugInfo != 0)))
	{
		if (mCompiler->mOptions.IsCodeView())
		{
			mBfIRBuilder->Module_AddModuleFlag("CodeView", 1);
		}
		else
		{
			mBfIRBuilder->Module_AddModuleFlag("Dwarf Version", 4);
		}
		mBfIRBuilder->Module_AddModuleFlag("Debug Info Version", 3);

		mDICompileUnit = mBfIRBuilder->DbgCreateCompileUnit(llvm::dwarf::DW_LANG_C_plus_plus, mModuleName, ".", "Beef Compiler 0.42.3", /*moduleOptions.mOptLevel > 0*/false, "", 0, !mHasFullDebugInfo);
	}

	mAwaitingInitFinish = false;
}

void BfModule::CalcGeneratesCode()
{
	if ((!mIsReified) || (mIsScratchModule))
	{
		mGeneratesCode = false;
		return;
	}

	mGeneratesCode = false;
	for (auto typeInst : mOwnedTypeInstances)
		if (!typeInst->IsInterface())
			mGeneratesCode = true;
}

void BfModule::ReifyModule()
{
	BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));

	BfLogSysM("ReifyModule %@ %s\n", this, mModuleName.c_str());
	BF_ASSERT((this != mContext->mScratchModule) && (this != mContext->mUnreifiedModule));
	mIsReified = true;
	CalcGeneratesCode();
	mReifyQueued = false;
	StartNewRevision(RebuildKind_SkipOnDemandTypes, true);
	mCompiler->mStats.mModulesReified++;
}

void BfModule::UnreifyModule()
{
	BfLogSysM("UnreifyModule %p %s\n", this, mModuleName.c_str());
	BF_ASSERT((this != mContext->mScratchModule) && (this != mContext->mUnreifiedModule));
	mIsReified = false;
	CalcGeneratesCode();
	mReifyQueued = false;
	StartNewRevision(RebuildKind_None, true);
	mCompiler->mStats.mModulesUnreified++;
}

void BfModule::CleanupFileInstances()
{
	for (auto& itr : mNamedFileInstanceMap)
	{
		BfLogSysM("FileInstance deleted: %p\n", itr.mValue);
		delete itr.mValue;
	}
	mCurFilePosition.mFileInstance = NULL;//
	mFileInstanceMap.Clear();
	mNamedFileInstanceMap.Clear();
}

void BfModule::Cleanup()
{
	CleanupFileInstances();
}

void BfModule::PrepareForIRWriting(BfTypeInstance* typeInst)
{
	if (HasCompiledOutput())
	{
		// It's possible that the target's code hasn't changed but we're requesting a new generic method specialization
		if ((!mIsModuleMutable) && (!typeInst->IsUnspecializedType()) && (!typeInst->mResolvingVarField))
		{
			StartExtension();
		}
	}
	else
	{
		EnsureIRBuilder();
	}
}

void BfModule::SetupIRBuilder(bool dbgVerifyCodeGen)
{
	if (mIsScratchModule)
	{
		mBfIRBuilder->mIgnoreWrites = true;
		BF_ASSERT(!dbgVerifyCodeGen);
	}
#ifdef _DEBUG
	if (mCompiler->mIsResolveOnly)
	{
		// For "deep" verification testing
		/*if (!mIsSpecialModule)
			dbgVerifyCodeGen = true;*/

			// We only want to turn this off on smaller builds for testing
		mBfIRBuilder->mIgnoreWrites = true;
		if (dbgVerifyCodeGen)
			mBfIRBuilder->mIgnoreWrites = false;
		if (!mBfIRBuilder->mIgnoreWrites)
		{
			// The only purpose of not ignoring writes is so we can verify the codegen one instruction at a time
			mBfIRBuilder->mDbgVerifyCodeGen = true;
		}
	}
	else if (!mGeneratesCode)
	{
		mBfIRBuilder->mIgnoreWrites = true;
	}
	else
	{
		// We almost always want this to be 'false' unless we need need to be able to inspect the generated LLVM
		//  code as we walk the AST
		//mBfIRBuilder->mDbgVerifyCodeGen = true;
		if (
			(mModuleName == "-")
			//|| (mModuleName == "BeefTest2_ClearColorValue")
			//|| (mModuleName == "Tests_FuncRefs")
			)
			mBfIRBuilder->mDbgVerifyCodeGen = true;

		// Just for testing!
		//mBfIRBuilder->mIgnoreWrites = true;
	}
#else
	if (mCompiler->mIsResolveOnly)
	{
		// Always ignore writes in resolveOnly for release builds
		mBfIRBuilder->mIgnoreWrites = true;
	}
	else
	{
		// Just for memory testing!  This breaks everything.
		//mBfIRBuilder->mIgnoreWrites = true;

		// For "deep" verification testing
		//mBfIRBuilder->mDbgVerifyCodeGen = true;
	}
#endif
	mWantsIRIgnoreWrites = mBfIRBuilder->mIgnoreWrites;
}

void BfModule::EnsureIRBuilder(bool dbgVerifyCodeGen)
{
	BF_ASSERT(!mIsDeleting);

	if (mBfIRBuilder == NULL)
	{
		if ((!mIsScratchModule) && (!mAddedToCount))
		{
			mCompiler->mStats.mModulesStarted++;
			mAddedToCount = true;
		}
		BF_ASSERT(mStaticFieldRefs.GetCount() == 0);

		/*if (mCompiler->mIsResolveOnly)
			BF_ASSERT(mIsResolveOnly);*/
		mBfIRBuilder = new BfIRBuilder(this);

		BfLogSysM("Created mBfIRBuilder %p in %p %s Reified: %d\n", mBfIRBuilder, this, mModuleName.c_str(), mIsReified);
		SetupIRBuilder(dbgVerifyCodeGen);
	}
}

BfIRValue BfModule::CreateForceLinkMarker(BfModule* module, String* outName)
{
	String name = "FORCELINKMOD_" + module->mModuleName;
	if (outName != NULL)
		*outName = name;
	auto markerType = GetPrimitiveType(BfTypeCode_Int8);
	return mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(markerType), true, BfIRLinkageType_External, (module == this) ? mBfIRBuilder->CreateConst(BfTypeCode_Int8, 0) : BfIRValue(), name);
}

void BfModule::StartNewRevision(RebuildKind rebuildKind, bool force)
{
	BP_ZONE("BfModule::StartNewRevision");

	// The project MAY be deleted because disabling a project can cause types to be deleted which
	//  causes other types rebuild BEFORE they get deleted, which is okay (though wasteful)
	//BF_ASSERT((mProject == NULL) || (!mProject->mDisabled));

	if (mCompiler->mCompileState == BfCompiler::CompileState_Cleanup)
	{
		// Cleaning up local methods may cause some necessary NewRevisions
		force = true;
	}

	// Already on new revision?
	if ((mRevision == mCompiler->mRevision) && (!force))
		return;

	mHadBuildError = false;
	mHadBuildWarning = false;
	mExtensionCount = 0;
	mRevision = mCompiler->mRevision;
	mRebuildIdx++;
	ClearModuleData(!force);

	// Clear this here, not in ClearModuleData, so we preserve those references even after writing out module
	if (rebuildKind != BfModule::RebuildKind_None) // Leave string pool refs for when we need to use things like [LinkName("")] methods bofore re-reification
	{
		mStringPoolRefs.Clear();
		mUnreifiedStringPoolRefs.Clear();
	}
	mDllImportEntries.Clear();
	mImportFileNames.Clear();
	for (auto& pairVal : mDeferredMethodCallData)
		delete pairVal.mValue;
	mDeferredMethodCallData.Clear();
	mDeferredMethodIds.Clear();
	mModuleRefs.Clear();
	mOutFileNames.Clear();
	mTypeDataRefs.Clear();
	mInterfaceSlotRefs.Clear();

	if (rebuildKind == BfModule::RebuildKind_None)
	{
		for (auto& specPair : mSpecializedMethodModules)
		{
			auto specModule = specPair.mValue;
			if (specModule->mIsReified != mIsReified)
			{
				if (mIsReified)
					specModule->ReifyModule();
				else
					specModule->UnreifyModule();
			}
		}
	}
	else
	{
		for (auto& specPair : mSpecializedMethodModules)
		{
			auto specModule = specPair.mValue;

			BfLogSysM("Setting module mIsDeleting %p due to parent module starting a new revision\n", specModule);

			// This module is no longer needed
			specModule->RemoveModuleData();
			specModule->mIsDeleting = true;
			mContext->mDeletingModules.Add(specModule);
		}
	}
	mSpecializedMethodModules.Clear();
	delete mNextAltModule;
	mNextAltModule = NULL;
	BF_ASSERT(mModuleOptions == NULL);

	BfLogSysM("Mod:%p StartNewRevision: %s Revision: %d\n", this, mModuleName.c_str(), mRevision);

	bool needsTypePopulated = false;
	int oldOnDemandCount = 0;

	// We don't have to rebuild types in the unspecialized module because there is no
	//  data that's needed -- their method instances are all NULL and the module
	//  doesn't get included in the build
	if (this != mContext->mScratchModule)
	{
		for (int typeIdx = 0; typeIdx < (int)mOwnedTypeInstances.size(); typeIdx++)
		{
			auto typeInst = mOwnedTypeInstances[typeIdx];
			if (!typeInst->IsDeleting())
			{
				typeInst->mIsReified = mIsReified;
				if (rebuildKind != BfModule::RebuildKind_None)
				{
					if (typeInst->IsOnDemand())
					{
						// Changing config should require on demand types to be recreated, but reifying a module shouldn't cause the type to be deleted
						if (rebuildKind == BfModule::RebuildKind_All)
							mContext->DeleteType(typeInst);
						else
						{
							RebuildMethods(typeInst);
						}
					}
					else
						//TODO: Why weren't we placing specialzied in purgatory here originally? This caused failed types to stick around too long
						mContext->RebuildType(typeInst, false, false, true);
						//mContext->RebuildType(typeInst, false, false, false);
				}
				else
				{
					auto _HandleMethod = [&](BfMethodInstance* methodInstance)
					{
						if ((methodInstance != NULL) && (methodInstance->mDeclModule != NULL))
							methodInstance->mDeclModule = this;
					};

					for (auto& methodGroup : typeInst->mMethodInstanceGroups)
					{
						if ((methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) ||
							(methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl) ||
							(methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference) ||
							(methodGroup.mOnDemandKind == BfMethodOnDemandKind_InWorkList))
						{
							oldOnDemandCount++;
						}

						_HandleMethod(methodGroup.mDefault);
						if (methodGroup.mMethodSpecializationMap != NULL)
							for (auto& kv : *methodGroup.mMethodSpecializationMap)
								_HandleMethod(kv.mValue);
					}
				}
				if (typeInst->IsDeleting())
					typeIdx--;
			}
		}
	}

	if (!mIsDeleting)
		Init();
	mOnDemandMethodCount += oldOnDemandCount;
	VerifyOnDemandMethods();
}

void BfModule::StartExtension()
{
	BF_ASSERT(!mIsModuleMutable);

	BfLogSysM("Extension started of module %p\n", this);

	mExtensionCount++;

	if (mBfIRBuilder != NULL)
		mPrevIRBuilders.push_back(mBfIRBuilder);
	mBfIRBuilder = NULL;
	mWantsIRIgnoreWrites = false;

	mFuncReferences.Clear();
	mClassVDataRefs.Clear();
	mClassVDataExtRefs.Clear();
	for (auto& kv : mTypeDataRefs)
		kv.mValue = BfIRValue();
	mDbgRawAllocDataRefs.Clear();
	mStringCharPtrPool.Clear();
	mStringObjectPool.Clear();
	mStaticFieldRefs.Clear();
	for (auto& kv : mInterfaceSlotRefs)
		kv.mValue = BfIRValue();
	for (auto& pairVal : mDeferredMethodCallData)
		delete pairVal.mValue;
	mDeferredMethodCallData.Clear();
	mDeferredMethodIds.Clear();

	DisownMethods();

	BfLogSysM("Mod:%p StartExtension: %s\n", this, mModuleName.c_str());

	bool wasAwaitingInitFinish = mAwaitingInitFinish;
	int prevOnDemandMethodCount = mOnDemandMethodCount;
	Init(false);
	if (!wasAwaitingInitFinish)
		FinishInit();
	mOnDemandMethodCount = prevOnDemandMethodCount;
}

void BfModule::GetConstClassValueParam(BfIRValue classVData, SizedArrayImpl<BfIRValue>& typeValueParams)
{
	auto hasObjectDebugFlags = mContext->mBfObjectType->mFieldInstances[0].mResolvedType->IsInteger();

	BfIRValue vDataValue;
	if (hasObjectDebugFlags)
		vDataValue = mBfIRBuilder->CreatePtrToInt(classVData, BfTypeCode_IntPtr);
	else
		vDataValue = mBfIRBuilder->CreateBitCast(classVData, mBfIRBuilder->MapType(mContext->mBfClassVDataPtrType));
	typeValueParams.push_back(vDataValue);
	if (hasObjectDebugFlags)
	{
		auto primType = GetPrimitiveType(BfTypeCode_IntPtr);
		typeValueParams.push_back(GetDefaultValue(primType));
	}
}

BfIRValue BfModule::GetConstValue(int64 val)
{
	return mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)val);
}

BfIRValue BfModule::GetConstValue(int64 val, BfType* type)
{
	BfType* checkType = type;
	if (type->IsTypedPrimitive())
	{
		checkType = type->GetUnderlyingType();
	}
	if (checkType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)checkType;
		if (checkType->IsFloat())
			return mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, (double)val);
		else
			return mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, (uint64)val);
	}
	return BfIRValue();
}

BfIRValue BfModule::GetConstValue8(int val)
{
	return mBfIRBuilder->CreateConst(BfTypeCode_Int8, (uint64)val);
}

BfIRValue BfModule::GetConstValue32(int32 val)
{
	return mBfIRBuilder->CreateConst(BfTypeCode_Int32, (uint64)val);
}

BfIRValue BfModule::GetConstValue64(int64 val)
{
	return mBfIRBuilder->CreateConst(BfTypeCode_Int64, (uint64)val);
}

BfIRValue BfModule::GetDefaultValue(BfType* type)
{
	PopulateType(type, BfPopulateType_Data);
	mBfIRBuilder->PopulateType(type, BfIRPopulateType_Declaration);

	if (type->IsTypedPrimitive())
	{
		auto underlyingType = type->GetUnderlyingType();
		if (underlyingType == NULL)
			return mBfIRBuilder->CreateConst(BfTypeCode_Int64, 0);
		return GetDefaultValue(type->GetUnderlyingType());
	}

	if (type->IsPointer() || type->IsObjectOrInterface() || type->IsGenericParam() || type->IsVar() || type->IsRef() || type->IsNull() ||
		type->IsModifiedTypeType() || type->IsConcreteInterfaceType())
		return mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(type));
	if ((type->IsIntegral()) || (type->IsBoolean()))
	{
		auto primType = (BfPrimitiveType*)type;
		return mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, (uint64)0);
	}
	if (type->IsFloat())
	{
		auto primType = (BfPrimitiveType*)type;
		return mBfIRBuilder->CreateConst(primType->mTypeDef->mTypeCode, 0.0);
	}
	return mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(type));
}

BfTypedValue BfModule::GetFakeTypedValue(BfType* type)
{
	// This is a conservative "IsValueless", since it's not an error to use a fakeVal even if we don't need one
	if (type->mSize == 0)
		return BfTypedValue(BfIRValue::sValueless, type);
	else if (mBfIRBuilder != NULL)
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), type);
	else
		return BfTypedValue(BfIRValue(BfIRValueFlags_Value, -1), type);
}

BfTypedValue BfModule::GetDefaultTypedValue(BfType* type, bool allowRef, BfDefaultValueKind defaultValueKind)
{
	if (type->IsVar())
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), type);

	PopulateType(type, BfPopulateType_Data);
	mBfIRBuilder->PopulateType(type, type->IsValueType() ? BfIRPopulateType_Full : BfIRPopulateType_Declaration);

	if (defaultValueKind == BfDefaultValueKind_Undef)
	{
		return BfTypedValue(mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(type)), type);
	}

	BfTypedValue typedValue;
	if ((defaultValueKind != BfDefaultValueKind_Const) && (!type->IsValuelessType()))
	{
		if (type->IsRef())
		{
			BfRefType* refType = (BfRefType*)type;
			BfType* underlyingType = refType->GetUnderlyingType();
			typedValue = BfTypedValue(CreateAlloca(underlyingType), underlyingType, BfTypedValueKind_Addr);
		}
		else
		{
			typedValue = BfTypedValue(CreateAlloca(type), type, BfTypedValueKind_Addr);
		}

		if (!mBfIRBuilder->mIgnoreWrites)
		{
			mBfIRBuilder->CreateMemSet(typedValue.mValue, GetConstValue(0, GetPrimitiveType(BfTypeCode_Int8)),
				GetConstValue(type->mSize), type->mAlign);
		}

		if ((defaultValueKind == BfDefaultValueKind_Value) && (!type->IsRef()))
			typedValue = LoadValue(typedValue);

		return typedValue;
	}

	if ((type->IsRef()) && (!allowRef))
	{
		BfRefType* refType = (BfRefType*)type;
		BfType* underlyingType = refType->GetUnderlyingType();
		if (underlyingType->IsValuelessType())
			typedValue = BfTypedValue(mBfIRBuilder->GetFakeVal(), underlyingType, true);
		else
			typedValue = BfTypedValue(mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(type)), underlyingType, true);
	}
	else
	{
		typedValue = BfTypedValue(GetDefaultValue(type), type, (defaultValueKind == BfDefaultValueKind_Addr) ? BfTypedValueKind_Addr : BfTypedValueKind_Value);
	}
	return typedValue;
}

BfIRValue BfModule::CreateStringCharPtr(const StringImpl& str, int stringId, bool define)
{
	String stringDataName = StrFormat("__bfStrData%d", stringId);

	auto charType = GetPrimitiveType(BfTypeCode_Char8);
 	BfIRType irStrCharType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(charType), (int)str.length() + 1);

	BfIRValue strConstant;
	if (define)
	{
		strConstant = mBfIRBuilder->CreateConstString(str);
	}
	BfIRValue gv = mBfIRBuilder->CreateGlobalVariable(irStrCharType,
		true, BfIRLinkageType_External,
		strConstant, stringDataName);

	if (define)
		mBfIRBuilder->GlobalVar_SetUnnamedAddr(gv, true);
	return mBfIRBuilder->CreateInBoundsGEP(gv, 0, 0);
}

void BfModule::FixConstValueParams(BfTypeInstance* typeInst, SizedArrayImpl<BfIRValue>& valueParams, bool fillInPadding)
{
	if ((!typeInst->mTypeDef->mIsCombinedPartial) && (!fillInPadding))
		return;

	int prevDataIdx = -1;
	int usedDataIdx = 0;
	int valueParamIdx = 0;

	if (typeInst->mBaseType != NULL)
	{
		usedDataIdx++;
		valueParamIdx++;
		prevDataIdx++;
	}

	int startingParamsSize = (int)valueParams.mSize;
	for (int fieldIdx = 0; fieldIdx < (int)typeInst->mFieldInstances.size(); fieldIdx++)
	{
		auto fieldInstance = &typeInst->mFieldInstances[fieldIdx];
		if (fieldInstance->mDataIdx < 0)
			continue;

		BF_ASSERT(fieldInstance->mDataIdx > prevDataIdx);
		if (fillInPadding)
		{
			for (int i = prevDataIdx + 1; i < fieldInstance->mDataIdx; i++)
				valueParams.Insert(valueParamIdx++, mBfIRBuilder->CreateConstArrayZero(0));
		}

		valueParamIdx++;
		prevDataIdx = fieldInstance->mDataIdx;

		usedDataIdx++;
		if (usedDataIdx <= valueParams.mSize)
			continue;

		valueParams.Add(GetDefaultValue(fieldInstance->mResolvedType));
	}
}

BfIRValue BfModule::CreateStringObjectValue(const StringImpl& str, int stringId, bool define)
{
	auto stringTypeInst = ResolveTypeDef(mCompiler->mStringTypeDef, define ? BfPopulateType_Data : BfPopulateType_Declaration)->ToTypeInstance();
	mBfIRBuilder->PopulateType(stringTypeInst);

	auto classVDataGlobal = CreateClassVDataGlobal(stringTypeInst);

	String stringObjName = StrFormat("__bfStrObj%d", stringId);

	BfIRValue stringValData;

	if (define)
	{
		BfIRValue stringCharsVal = CreateStringCharPtr(str, stringId, define);
		mStringCharPtrPool[stringId] = stringCharsVal;

		SizedArray<BfIRValue, 8> typeValueParams;
		GetConstClassValueParam(classVDataGlobal, typeValueParams);
		FixConstValueParams(stringTypeInst->mBaseType, typeValueParams);
		auto objData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(stringTypeInst->mBaseType, BfIRPopulateType_Full), typeValueParams);

		auto lenByteCount = stringTypeInst->mFieldInstances[0].mResolvedType->mSize;

		typeValueParams.clear();
		typeValueParams.push_back(objData);

		if (lenByteCount == 4)
		{
			typeValueParams.push_back(GetConstValue32((int)str.length())); // mLength
			typeValueParams.push_back(GetConstValue32((int32)(0x40000000 + str.length() + 1))); // mAllocSizeAndFlags
		}
		else
		{
			typeValueParams.push_back(GetConstValue64(str.length())); // mLength
			typeValueParams.push_back(GetConstValue64(0x4000000000000000LL + str.length() + 1)); // mAllocSizeAndFlags
		}
		typeValueParams.push_back(stringCharsVal); // mPtr
		FixConstValueParams(stringTypeInst, typeValueParams);

		stringValData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(stringTypeInst, BfIRPopulateType_Full), typeValueParams);
	}

	mBfIRBuilder->PopulateType(stringTypeInst);
	auto stringValLiteral = mBfIRBuilder->CreateGlobalVariable(
		mBfIRBuilder->MapTypeInst(stringTypeInst, BfIRPopulateType_Full),
		true,
		BfIRLinkageType_External,
		define ? stringValData : BfIRValue(),
		stringObjName);

	return stringValLiteral;
}

int BfModule::GetStringPoolIdx(BfIRValue constantStr, BfIRConstHolder* constHolder)
{
	if (constHolder == NULL)
		constHolder = mBfIRBuilder;

	auto constant = constHolder->GetConstant(constantStr);
	if (constant == NULL)
		return -1;

	if (constant->mTypeCode == BfTypeCode_StringId)
	{
		return constant->mInt32;
	}

	while (constant->mConstType == BfConstType_BitCast)
	{
		auto constBitCast = (BfConstantBitCast*)constant;
		constant = constHolder->GetConstantById(constBitCast->mTarget);
	}

	if (constant->mConstType == BfConstType_GEP32_2)
	{
		auto constGEP = (BfConstantGEP32_2*)constant;
		constant = constHolder->GetConstantById(constGEP->mTarget);
	}

	if (constant->mConstType == BfConstType_GlobalVar)
	{
		auto constGV = (BfGlobalVar*)constant;
		const char* strDataPrefix = "__bfStrData";
		if (strncmp(constGV->mName, strDataPrefix, strlen(strDataPrefix)) == 0)
			return atoi(constGV->mName + strlen(strDataPrefix));

		const char* strObjPrefix = "__bfStrObj";
		if (strncmp(constGV->mName, strObjPrefix, strlen(strObjPrefix)) == 0)
			return atoi(constGV->mName + strlen(strObjPrefix));
	}

	return -1;
}

String* BfModule::GetStringPoolString(BfIRValue constantStr, BfIRConstHolder * constHolder)
{
	int strId = GetStringPoolIdx(constantStr, constHolder);
	if (strId != -1)
	{
		auto& entry = mContext->mStringObjectIdMap[strId];
		return &entry.mString;
	}
	return NULL;
}

CeDbgState* BfModule::GetCeDbgState()
{
	if (!mIsComptimeModule)
		return NULL;
	if ((mCompiler->mCeMachine != NULL) && (mCompiler->mCeMachine->mDebugger != NULL))
		return mCompiler->mCeMachine->mDebugger->mCurDbgState;
	return NULL;
}

BfIRValue BfModule::GetStringCharPtr(int stringId, bool force)
{
	if ((mBfIRBuilder->mIgnoreWrites) && (!force))
	{
		mUnreifiedStringPoolRefs.Add(stringId);
		return mBfIRBuilder->CreateConst(BfTypeCode_StringId, stringId);
	}

	BfIRValue* irValue = NULL;
	if (!mStringCharPtrPool.TryAdd(stringId, NULL, &irValue))
		return *irValue;

	// If this wasn't in neither dictionary yet, add to mStringPoolRefs
	if (!mStringObjectPool.ContainsKey(stringId))
		mStringPoolRefs.Add(stringId);

	const StringImpl& str = mContext->mStringObjectIdMap[stringId].mString;
	BfIRValue strCharPtrConst = CreateStringCharPtr(str, stringId, false);
	*irValue = strCharPtrConst;
	return strCharPtrConst;
}

BfIRValue BfModule::GetStringCharPtr(BfIRValue strValue, bool force)
{
	if (strValue.IsConst())
	{
		int stringId = GetStringPoolIdx(strValue);
		BF_ASSERT(stringId != -1);
		return GetStringCharPtr(stringId, force);
	}

	BfIRValue charPtrPtr = mBfIRBuilder->CreateInBoundsGEP(strValue, 0, 1);
	BfIRValue charPtr = mBfIRBuilder->CreateLoad(charPtrPtr);
	return charPtr;
}

BfIRValue BfModule::GetStringCharPtr(const StringImpl& str, bool force)
{
	return GetStringCharPtr(GetStringObjectValue(str, force), force);
}

BfIRValue BfModule::GetStringObjectValue(int strId, bool define, bool force)
{
	BfIRValue* objValue;
	if (mStringObjectPool.TryGetValue(strId, &objValue))
		return *objValue;

	auto stringPoolEntry = mContext->mStringObjectIdMap[strId];
	return GetStringObjectValue(stringPoolEntry.mString, define, force);
}

BfIRValue BfModule::GetStringObjectValue(const StringImpl& str, bool define, bool force)
{
	auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef, define ? BfPopulateType_Data : BfPopulateType_Declaration);
	mBfIRBuilder->PopulateType(stringType);

	int strId = mContext->GetStringLiteralId(str);

	if ((mBfIRBuilder->mIgnoreWrites) && (!force))
	{
		auto refModule = this;
		if ((this == mContext->mUnreifiedModule) && (mCurTypeInstance != NULL))
			refModule = mCurTypeInstance->mModule;
		refModule->mUnreifiedStringPoolRefs.Add(strId);
		return mBfIRBuilder->CreateConst(BfTypeCode_StringId, strId);
	}

	BfIRValue* irValuePtr = NULL;
	if (mStringObjectPool.TryGetValue(strId, &irValuePtr))
		return *irValuePtr;

	// If this wasn't in neither dictionary yet, add to mStringPoolRefs
	if (!mStringCharPtrPool.ContainsKey(strId))
		mStringPoolRefs.Add(strId);

	BfIRValue strObject = CreateStringObjectValue(str, strId, define);
	mStringObjectPool[strId] = strObject;

	mStringPoolRefs.Add(strId);

	return strObject;
}

BfIRValue BfModule::CreateGlobalConstValue(const StringImpl& name, BfIRValue constant, BfIRType type, bool external)
{
	auto newConst = mBfIRBuilder->CreateGlobalVariable(
		type,
		true,
		external ? BfIRLinkageType_External : BfIRLinkageType_Internal,
		constant,
		name.c_str());
	return newConst;
}

void BfModule::NewScopeState(bool createLexicalBlock, bool flushValueScope)
{
	auto curScope = mCurMethodState->mCurScope;
	if (curScope->mLabelNode != NULL)
	{
		curScope->mLabelNode->ToString(curScope->mLabel);

		auto rootMethodState = mCurMethodState->GetRootMethodState();
		curScope->mScopeLocalId = rootMethodState->mCurLocalVarId++;
		auto autoComplete = mCompiler->GetAutoComplete();
		if (autoComplete != NULL)
			autoComplete->CheckLabel(curScope->mLabelNode, NULL, curScope);
	}

	if (!mCurMethodState->mCurScope->mLabel.IsEmpty())
	{
		auto checkScope = mCurMethodState->mCurScope->mPrevScope;
		while (checkScope != NULL)
		{
			if (checkScope->mLabel == mCurMethodState->mCurScope->mLabel)
			{
				auto errorNode = Fail("Duplicate scope label", curScope->mLabelNode);
				if (errorNode != NULL)
					mCompiler->mPassInstance->MoreInfo("See previous scope label", checkScope->mLabelNode);
				break;
			}

			checkScope = checkScope->mPrevScope;
		}
	}

	auto prevScope = mCurMethodState->mCurScope->mPrevScope;
	if ((flushValueScope) && (prevScope != NULL) && (prevScope->mValueScopeStart))
	{
		if (prevScope->mHadScopeValueRetain)
			mBfIRBuilder->CreateValueScopeSoftEnd(prevScope->mValueScopeStart);
		else
			mBfIRBuilder->CreateValueScopeHardEnd(prevScope->mValueScopeStart);
	}

	mCurMethodState->mBlockNestLevel++;
	if ((createLexicalBlock) && (mBfIRBuilder->DbgHasInfo()) && (mHasFullDebugInfo))
	{
		// DIScope could be NULL if we're in an unspecialized method (for example)
		if (mCurMethodState->mCurScope->mDIScope)
			mCurMethodState->mCurScope->mDIScope = mBfIRBuilder->DbgCreateLexicalBlock(mCurMethodState->mCurScope->mDIScope, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mCurFilePosition.mCurColumn);
	}
	mCurMethodState->mCurScope->mLocalVarStart = (int)mCurMethodState->mLocals.size();
	mCurMethodState->mCurScope->mBlock = mBfIRBuilder->MaybeChainNewBlock((!mCurMethodState->mCurScope->mLabel.empty()) ? mCurMethodState->mCurScope->mLabel : "newScope");
	mCurMethodState->mCurScope->mMixinState = mCurMethodState->mMixinState;
}

void BfModule::RestoreScoreState_LocalVariables(int localVarStart)
{
	while (localVarStart < (int)mCurMethodState->mLocals.size())
	{
		auto localVar = mCurMethodState->mLocals.back();
		LocalVariableDone(localVar, false);
		mCurMethodState->mLocals.pop_back();

		if (localVar->mShadowedLocal != NULL)
		{
			BfLocalVarEntry* localVarEntryPtr;
			if (mCurMethodState->mLocalVarSet.TryGet(BfLocalVarEntry(localVar), &localVarEntryPtr))
			{
				localVarEntryPtr->mLocalVar = localVar->mShadowedLocal;
			}
		}
		else
		{
			mCurMethodState->mLocalVarSet.Remove(BfLocalVarEntry(localVar));
		}

		if (localVar->mIsBumpAlloc)
			localVar->~BfLocalVariable();
		else
			delete localVar;
	}
}

void BfModule::RestoreScopeState()
{
	BfScopeData* prevScopeData = mCurMethodState->mCurScope->mPrevScope;
	mCurMethodState->mBlockNestLevel--;

	if (!mCurMethodState->mCurScope->mAtEndBlocks.empty())
	{
		BfIRBlock afterEndBlock;
		if (!mCurMethodState->mLeftBlockUncond)
		{
			afterEndBlock = mBfIRBuilder->CreateBlock("scopeAfterEnd");
			mBfIRBuilder->CreateBr(afterEndBlock);
			mBfIRBuilder->ClearDebugLocation_Last();
		}

		for (auto preExitBlock : mCurMethodState->mCurScope->mAtEndBlocks)
			mBfIRBuilder->MoveBlockToEnd(preExitBlock);

		if (afterEndBlock)
		{
			mBfIRBuilder->AddBlock(afterEndBlock);
			mBfIRBuilder->SetInsertPoint(afterEndBlock);
		}
	}

	mCurMethodState->mCurScope->mDone = true;
	if (!mCurMethodState->mLeftBlockUncond)
		EmitDeferredScopeCalls(true, mCurMethodState->mCurScope);

	EmitDeferredCallProcessorInstances(mCurMethodState->mCurScope);

	RestoreScoreState_LocalVariables(mCurMethodState->mCurScope->mLocalVarStart);

	if (mCurMethodState->mCurScope->mValueScopeStart)
		mBfIRBuilder->CreateValueScopeHardEnd(mCurMethodState->mCurScope->mValueScopeStart);

	mCurMethodState->mCurScope = prevScopeData;
	mCurMethodState->mTailScope = mCurMethodState->mCurScope;
}

BfIRValue BfModule::CreateAlloca(BfType* type, bool addLifetime, const char* name, BfIRValue arraySize)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return mBfIRBuilder->GetFakeVal();

	BF_ASSERT((*(int8*)&addLifetime == 1) || (*(int8*)&addLifetime == 0));
	mBfIRBuilder->PopulateType(type);
	auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
	if (!mBfIRBuilder->mIgnoreWrites)
		BF_ASSERT(!prevInsertBlock.IsFake());
	mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
	BfIRValue allocaInst;
	if (arraySize)
		allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(type), arraySize);
	else
		allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(type));
	mBfIRBuilder->SetAllocaAlignment(allocaInst, type->mAlign);
	mBfIRBuilder->ClearDebugLocation(allocaInst);
	if (name != NULL)
		mBfIRBuilder->SetName(allocaInst, name);
	mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	if ((addLifetime) && (WantsLifetimes()))
	{
		auto lifetimeStart = mBfIRBuilder->CreateLifetimeStart(allocaInst);
		mBfIRBuilder->ClearDebugLocation(lifetimeStart);
		mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
	}
	return allocaInst;
}

BfIRValue BfModule::CreateAllocaInst(BfTypeInstance* typeInst, bool addLifetime, const char* name)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return mBfIRBuilder->GetFakeVal();

	BF_ASSERT((*(int8*)&addLifetime == 1) || (*(int8*)&addLifetime == 0));
	mBfIRBuilder->PopulateType(typeInst);
	auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
	mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
	auto allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapTypeInst(typeInst));
	mBfIRBuilder->SetAllocaAlignment(allocaInst, typeInst->mInstAlign);
	mBfIRBuilder->ClearDebugLocation(allocaInst);
	if (name != NULL)
		mBfIRBuilder->SetName(allocaInst, name);
	mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	if ((addLifetime) && (WantsLifetimes()))
	{
		auto lifetimeStart = mBfIRBuilder->CreateLifetimeStart(allocaInst);
		mBfIRBuilder->ClearDebugLocation(lifetimeStart);
		mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
	}
	return allocaInst;
}

BfDeferredCallEntry* BfModule::AddStackAlloc(BfTypedValue val, BfIRValue arraySize, BfAstNode* refNode, BfScopeData* scopeData, bool condAlloca, bool mayEscape, BfIRBlock valBlock)
{
	//This was removed because we want the alloc to be added to the __deferred list if it's actually a "stack"
	// 'stack' in a head scopeData is really the same as 'scopeData', so use the simpler scopeData handling
	/*if (mCurMethodState->mInHeadScope)
		isScopeAlloc = true;*/

	if (scopeData == NULL)
		return NULL;

	auto checkBaseType = val.mType->ToTypeInstance();
	if ((checkBaseType != NULL) && (checkBaseType->IsObject()) && (!arraySize))
	{
		bool hadDtorCall = false;
		while (checkBaseType != NULL)
		{
			BfMethodDef* dtorMethodDef = checkBaseType->mTypeDef->GetMethodByName("~this");
			if (dtorMethodDef != NULL)
			{
				auto dtorMethodInstance = GetMethodInstance(checkBaseType, dtorMethodDef, BfTypeVector());
				if (dtorMethodInstance)
				{
					bool isDynAlloc = (scopeData != NULL) && (mCurMethodState->mCurScope->IsDyn(scopeData));
					BfIRValue useVal = val.mValue;

					BfIRBlock prevBlock = mBfIRBuilder->GetInsertBlock();
					if (valBlock)
						mBfIRBuilder->SetInsertPoint(valBlock);
					useVal = mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapTypeInstPtr(checkBaseType));
					if (!useVal.IsConst())
						mBfIRBuilder->ClearDebugLocation(useVal);
					if (valBlock)
						mBfIRBuilder->SetInsertPoint(prevBlock);

					if (isDynAlloc)
					{
						//
					}
					else if (condAlloca)
					{
						BF_ASSERT(!IsTargetingBeefBackend());
						BF_ASSERT(!isDynAlloc);
						auto valPtr = CreateAlloca(checkBaseType);
						mBfIRBuilder->ClearDebugLocation_Last();
						mBfIRBuilder->CreateAlignedStore(useVal, valPtr, checkBaseType->mAlign);
						mBfIRBuilder->ClearDebugLocation_Last();
						useVal = valPtr;
					}

					SizedArray<BfIRValue, 1> llvmArgs;
					llvmArgs.push_back(useVal);
					auto deferredCall = AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
					if (deferredCall != NULL)
					{
						deferredCall->mCastThis = (val.mType != checkBaseType) && (!isDynAlloc);
						if (condAlloca)
							deferredCall->mArgsNeedLoad = true;
					}
					hadDtorCall = true;

					break;
				}
			}

			checkBaseType = checkBaseType->mBaseType;
		}
		return NULL;
	}

	//TODO: In the future we could be smarter about statically determining that our value hasn't escaped and eliding this
	if (mayEscape)
	{
		if ((!IsOptimized()) && (!mIsComptimeModule) && (!val.mType->IsValuelessType()) && (!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mIsResolveOnly))
		{
			auto nullPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
			bool isDyn = mCurMethodState->mCurScope->IsDyn(scopeData);
			if (!isDyn)
			{
				const char* methodName = arraySize ? "SetDeletedArray" : "SetDeleted";
				BfModuleMethodInstance dtorMethodInstance = GetInternalMethod(methodName);
				BF_ASSERT(dtorMethodInstance.mMethodInstance != NULL);
				if (!dtorMethodInstance.mMethodInstance)
				{
					Fail(StrFormat("Unable to find %s", methodName));
				}
				else
				{
					SizedArray<BfIRValue, 1> llvmArgs;
					if (IsTargetingBeefBackend())
					{
						llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(nullPtrType)));
						//mBfIRBuilder->ClearDebugLocation_Last();
					}
					else
						llvmArgs.push_back(val.mValue);
					llvmArgs.push_back(GetConstValue(val.mType->mSize));
					llvmArgs.push_back(GetConstValue32(val.mType->mAlign));
					if (arraySize)
						llvmArgs.push_back(arraySize);
					return AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
				}
			}
			else
			{
				if ((arraySize) && (!arraySize.IsConst()) && (val.mType->mSize < mSystem->mPtrSize))
				{
					BfIRValue clearSize = arraySize;
					if (val.mType->GetStride() > 1)
						clearSize = mBfIRBuilder->CreateMul(clearSize, GetConstValue(val.mType->GetStride()));

					const char* methodName = "SetDeletedX";
					BfModuleMethodInstance dtorMethodInstance = GetInternalMethod(methodName);
					BF_ASSERT(dtorMethodInstance);
					if (!dtorMethodInstance)
					{
						Fail(StrFormat("Unable to find %s", methodName));
					}
					else
					{
						SizedArray<BfIRValue, 1> llvmArgs;
						llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(nullPtrType)));
						//mBfIRBuilder->ClearDebugLocation_Last();
						llvmArgs.push_back(clearSize);
						return AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
					}
				}
				else
				{
					intptr clearSize = val.mType->mSize;
					auto constant = mBfIRBuilder->GetConstant(arraySize);
					if (constant != NULL)
						clearSize = (intptr)(clearSize * constant->mInt64);

					const char* funcName = "SetDeleted1";
					if (clearSize >= 16)
						funcName = "SetDeleted16";
					else if (clearSize >= 8)
						funcName = "SetDeleted8";
					else if (clearSize >= 4)
						funcName = "SetDeleted4";

					BfModuleMethodInstance dtorMethodInstance = GetInternalMethod(funcName);
					BF_ASSERT(dtorMethodInstance.mMethodInstance != NULL);
					if (!dtorMethodInstance)
					{
						Fail(StrFormat("Unable to find %s", funcName));
					}
					else
					{
						SizedArray<BfIRValue, 1> llvmArgs;
						llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(nullPtrType)));
						//mBfIRBuilder->ClearDebugLocation_Last();
						return AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
					}
				}
			}
		}
	}

	return NULL;
}

bool BfModule::TryLocalVariableInit(BfLocalVariable* localVar)
{
	auto startTypeInstance = localVar->mResolvedType->ToTypeInstance();
	if ((startTypeInstance == NULL) || (!startTypeInstance->IsStruct()))
		return false;
	auto checkTypeInstance = startTypeInstance;
	while (checkTypeInstance != NULL)
	{
		for (auto& fieldInstance : checkTypeInstance->mFieldInstances)
		{
			if (fieldInstance.mMergedDataIdx != -1)
			{
				int64 checkMask = (int64)1 << fieldInstance.mMergedDataIdx;
				if ((localVar->mUnassignedFieldFlags & checkMask) != 0)
				{
					// For fields added in extensions, we automatically initialize those if necessary
					auto fieldDef = fieldInstance.GetFieldDef();
					if (!fieldDef->mDeclaringType->IsExtension())
						return false;

					if ((fieldInstance.mDataIdx != -1) && (!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mIsResolveOnly) && (!mIsComptimeModule))
					{
						auto curInsertBlock = mBfIRBuilder->GetInsertBlock();

						mBfIRBuilder->SaveDebugLocation();
						if (localVar->IsParam())
							mBfIRBuilder->SetInsertPointAtStart(mCurMethodState->mIRInitBlock);
						else
						{
							BF_ASSERT(localVar->mDeclBlock);
							mBfIRBuilder->SetInsertPointAtStart(localVar->mDeclBlock);
						}

						mBfIRBuilder->ClearDebugLocation();

						BfIRValue curVal;
						if (localVar->mIsThis)
							curVal = mBfIRBuilder->GetArgument(0);
						else
							curVal = localVar->mAddr;
						auto subTypeInstance = startTypeInstance;
						while (subTypeInstance != checkTypeInstance)
						{
							curVal = mBfIRBuilder->CreateInBoundsGEP(curVal, 0, 0);
							subTypeInstance = subTypeInstance->mBaseType;
						}

						auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(curVal, 0, fieldInstance.mDataIdx);
						auto defVal = GetDefaultValue(fieldInstance.mResolvedType);
						auto storeInst = mBfIRBuilder->CreateStore(defVal, fieldPtr);

						mBfIRBuilder->SetInsertPoint(curInsertBlock);
						mBfIRBuilder->RestoreDebugLocation();
					}

					localVar->mUnassignedFieldFlags &= ~checkMask;
					if (localVar->mUnassignedFieldFlags == 0)
						localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
				}
			}
		}
		checkTypeInstance = checkTypeInstance->mBaseType;
	}

	return localVar->mAssignedKind != BfLocalVarAssignKind_None;
}

void BfModule::LocalVariableDone(BfLocalVariable* localVar, bool isMethodExit)
{
	BfAstNode* localNameNode = localVar->mNameNode;
	if (localVar->mIsThis)
	{
		localNameNode = mCurMethodInstance->mMethodDef->GetRefNode();
	}

	if (localNameNode != NULL)
	{
		bool isOut = false;
		if (localVar->mResolvedType->IsRef())
		{
			auto refType = (BfRefType*)localVar->mResolvedType;
			isOut = refType->mRefKind == BfRefType::RefKind_Out;
		}

		if ((localVar->mReadFromId == -1) || (isOut) || ((localVar->mIsThis) && (mCurTypeInstance->IsStruct())))
		{
			if ((localVar->mAssignedKind != BfLocalVarAssignKind_Unconditional) & (localVar->IsParam()))
				TryLocalVariableInit(localVar);

			// We may skip processing of local methods, so we won't know if it bind to any of our local variables or not
			bool deferFullAnalysis = mCurMethodState->GetRootMethodState()->mLocalMethodCache.size() != 0;

			// We may have init blocks that we aren't processing here...
			if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsAutocompleteMethod) && (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor))
				deferFullAnalysis = true;

			//bool deferFullAnalysis = true;
			bool deferUsageWarning = deferFullAnalysis && (mCompiler->IsAutocomplete()) &&
				(mCompiler->mResolvePassData->mAutoComplete->mResolveType != BfResolveType_GetFixits);

			if (((localVar->mAssignedKind != BfLocalVarAssignKind_Unconditional) || (localVar->mHadExitBeforeAssign)) &&
				(!localVar->mIsImplicitParam))
			{
				if (deferUsageWarning)
				{
					// Ignore
				}
				else if (localVar->mIsThis)
				{
					bool foundFields = false;

					auto checkTypeInstance = mCurTypeInstance;
					while (checkTypeInstance != NULL)
					{
						for (auto& fieldInstance : checkTypeInstance->mFieldInstances)
						{
							if (fieldInstance.mMergedDataIdx == -1)
								continue;

							int checkMask = 1 << fieldInstance.mMergedDataIdx;
							if ((localVar->mUnassignedFieldFlags & checkMask) != 0)
							{
								auto fieldDef = fieldInstance.GetFieldDef();

								if (mCurMethodInstance->mMethodDef->mDeclaringType->mIsPartial)
								{
									if (mCurMethodInstance->mMethodDef->mDeclaringType != fieldInstance.GetFieldDef()->mDeclaringType)
									{
										// This extension is only responsible for its own fields
										foundFields = true;
										continue;
									}

									if (fieldDef->GetInitializer() != NULL)
									{
										// This initializer was handled in CtorNoBody
										foundFields = true;
										continue;
									}
								}

								if (auto propertyDeclaration = BfNodeDynCast<BfPropertyDeclaration>(fieldDef->mFieldDeclaration))
								{
									String propName;
									if (propertyDeclaration->mNameNode != NULL)
										propertyDeclaration->mNameNode->ToString(propName);

									if (checkTypeInstance == mCurTypeInstance)
									{
										Fail(StrFormat("Auto-implemented property '%s' must be fully assigned before control is returned to the caller",
											propName.c_str()), localNameNode, deferFullAnalysis); // 0171
									}
									else
									{
										Fail(StrFormat("Auto-implemented property '%s.%s' must be fully assigned before control is returned to the caller",
											TypeToString(checkTypeInstance).c_str(),
											propName.c_str()), localNameNode, deferFullAnalysis); // 0171
									}
								}
								else
								{
									if (checkTypeInstance == mCurTypeInstance)
									{
										Fail(StrFormat("Field '%s' must be fully assigned before control is returned to the caller",
											fieldDef->mName.c_str()), localNameNode, deferFullAnalysis); // 0171
									}
									else
									{
										Fail(StrFormat("Field '%s.%s' must be fully assigned before control is returned to the caller",
											TypeToString(checkTypeInstance).c_str(),
											fieldDef->mName.c_str()), localNameNode, deferFullAnalysis); // 0171
									}
								}

								foundFields = true;
							}
						}
						checkTypeInstance = checkTypeInstance->mBaseType;
					}

					if (!foundFields)
					{
						if (mCurTypeInstance->mInstSize > 0)
							Fail(StrFormat("The variable '%s' must be fully assigned to before control leaves the current method", localVar->mName.c_str()), localNameNode); // 0177
					}
				}
				else if (localVar->IsParam())
					Fail(StrFormat("The out parameter '%s' must be assigned to before control leaves the current method", localVar->mName.c_str()), localNameNode, deferFullAnalysis); // 0177
				else if (!deferUsageWarning)
					Warn(BfWarning_CS0168_VariableDeclaredButNeverUsed, StrFormat("The variable '%s' is declared but never used", localVar->mName.c_str()), localNameNode, deferFullAnalysis);
			}
			else if ((localVar->mReadFromId == -1) && (!localVar->IsParam()) && (!deferUsageWarning))
				Warn(BfWarning_CS0168_VariableDeclaredButNeverUsed, StrFormat("The variable '%s' is assigned but its value is never used", localVar->mName.c_str()), localNameNode, deferFullAnalysis);
		}
	}
}

void BfModule::CreateRetValLocal()
{
	if (mCurMethodState->mRetVal)
	{
		BfLocalVariable* localDef = new BfLocalVariable();
		localDef->mName = "return";
		localDef->mResolvedType = mCurMethodState->mRetVal.mType;
		localDef->mAddr = mCurMethodState->mRetVal.mValue;
		localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		AddLocalVariableDef(localDef);
	}
	else if (mCurMethodState->mRetValAddr)
	{
		BfLocalVariable* localDef = new BfLocalVariable();
		localDef->mName = "return";
		localDef->mResolvedType = CreateRefType(mCurMethodInstance->mReturnType);
		localDef->mAddr = mCurMethodState->mRetValAddr;
		localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		AddLocalVariableDef(localDef);
	}
}

void BfModule::MarkDynStack(BfScopeData* scopeData)
{
	auto checkScope = mCurMethodState->mCurScope;
	while (true)
	{
		if (checkScope == scopeData)
			break;

		checkScope->mHadOuterDynStack = true;
		checkScope = checkScope->mPrevScope;
	}
}

void BfModule::SaveStackState(BfScopeData* scopeData)
{
	// If we push a value on the stack that needs to survive until the end of scopeData, we need to make sure that no
	//  scopes within scopeData restore the stack
	auto checkScope = mCurMethodState->mCurScope;
	while (true)
	{
		if (checkScope == scopeData)
		{
			if ((!checkScope->mSavedStack) && (checkScope->mBlock) && (!checkScope->mIsScopeHead) && (!checkScope->mHadOuterDynStack))
			{
				if (mBfIRBuilder->mHasDebugInfo)
					mBfIRBuilder->SaveDebugLocation();
				auto prevPos = mBfIRBuilder->GetInsertBlock();
				mBfIRBuilder->SetInsertPointAtStart(checkScope->mBlock);
				checkScope->mSavedStack = mBfIRBuilder->CreateStackSave();
				mBfIRBuilder->ClearDebugLocation(checkScope->mSavedStack);
				mBfIRBuilder->SetInsertPoint(prevPos);
				if (mBfIRBuilder->mHasDebugInfo)
					mBfIRBuilder->RestoreDebugLocation();
			}
			break;
		}
		if (checkScope->mSavedStack)
		{
			for (auto usage : checkScope->mSavedStackUses)
				mBfIRBuilder->EraseInstFromParent(usage);
			checkScope->mSavedStackUses.Clear();

			mBfIRBuilder->EraseInstFromParent(checkScope->mSavedStack);
			checkScope->mSavedStack = BfIRValue();
		}
		checkScope = checkScope->mPrevScope;
	}
}

BfIRValue BfModule::ValueScopeStart()
{
	if (IsTargetingBeefBackend())
		return mBfIRBuilder->CreateValueScopeStart();
	return BfIRValue();
}

void BfModule::ValueScopeEnd(BfIRValue valueScopeStart)
{
	if (valueScopeStart)
		mBfIRBuilder->CreateValueScopeHardEnd(valueScopeStart);
}

BfProjectSet* BfModule::GetVisibleProjectSet()
{
	if (mCurMethodState == NULL)
		return NULL;

	if (mCurMethodState->mVisibleProjectSet.IsEmpty())
	{
		HashSet<BfType*> seenTypes;

		std::function<void(BfProject* project)> _AddProject = [&](BfProject* project)
		{
			if (mCurMethodState->mVisibleProjectSet.Add(project))
			{
				for (auto dep : project->mDependencies)
					_AddProject(dep);
			}
		};

		std::function<void(BfType* type)> _AddType = [&](BfType* type)
		{
			auto typeInstance = type->ToTypeInstance();
			if (typeInstance == NULL)
			{
				if (type->IsSizedArray())
					_AddType(type->GetUnderlyingType());
				return;
			}
			if (typeInstance->IsTuple())
			{
				for (auto& fieldInst : typeInstance->mFieldInstances)
				{
					if (fieldInst.mDataIdx != -1)
						_AddType(fieldInst.mResolvedType);
				}
			}
			_AddProject(typeInstance->mTypeDef->mProject);
			if (typeInstance->mGenericTypeInfo == NULL)
				return;
			for (auto type : typeInstance->mGenericTypeInfo->mTypeGenericArguments)
			{
				if (seenTypes.Add(type))
					_AddType(type);
			}
		};

		if (mCurTypeInstance != NULL)
			_AddType(mCurTypeInstance);

		auto methodState = mCurMethodState;
		while (methodState != NULL)
		{
			if (methodState->mMethodInstance != NULL)
			{
				_AddProject(methodState->mMethodInstance->mMethodDef->mDeclaringType->mProject);
				if (methodState->mMethodInstance->mMethodInfoEx != NULL)
				{
					for (auto type : methodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments)
						_AddType(type);
				}
			}

			methodState = methodState->mPrevMethodState;
		}
	}

	return &mCurMethodState->mVisibleProjectSet;
}

BfFileInstance* BfModule::GetFileFromNode(BfAstNode* astNode)
{
	auto bfParser = astNode->GetSourceData()->ToParserData();
	if (bfParser == NULL)
		return NULL;

	BfFileInstance** fileInstancePtr = NULL;

	if (!mFileInstanceMap.TryAdd(bfParser, NULL, &fileInstancePtr))
	{
		return *fileInstancePtr;
	}
	else
	{
		// It's possible two parsers have the same file name (ie: mNextRevision)
		BfFileInstance** namedFileInstancePtr = NULL;
		if (!mNamedFileInstanceMap.TryAdd(bfParser->mFileName, NULL, &namedFileInstancePtr))
		{
			auto bfFileInstance = *namedFileInstancePtr;
			*fileInstancePtr = bfFileInstance;
			return bfFileInstance;
		}

		int slashPos = (int)bfParser->mFileName.LastIndexOf(DIR_SEP_CHAR);

		auto bfFileInstance = new BfFileInstance();
		*fileInstancePtr = bfFileInstance;
		*namedFileInstancePtr = bfFileInstance;

		bfFileInstance->mPrevPosition.mFileInstance = bfFileInstance;
		bfFileInstance->mParser = bfParser;

		BfLogSysM("GetFileFromNode new file. Mod: %p FileInstance: %p Parser: %p\n", this, bfFileInstance, bfParser);

		if ((mBfIRBuilder != NULL) && (mBfIRBuilder->DbgHasLineInfo()))
		{
			String fileName = bfParser->mFileName;

			for (int i = 0; i < (int)fileName.length(); i++)
			{
				if (fileName[i] == DIR_SEP_CHAR_ALT)
					fileName[i] = DIR_SEP_CHAR;
			}

			bfFileInstance->mDIFile = mBfIRBuilder->DbgCreateFile(fileName.Substring(slashPos + 1), fileName.Substring(0, BF_MAX(slashPos, 0)), bfParser->mMD5Hash);
		}
		return bfFileInstance;
	}
}

void BfModule::UpdateSrcPos(BfAstNode* astNode, BfSrcPosFlags flags, int debugLocOffset)
{
	// We set force to true when we MUST get the line position set (for navigation purposes, for example)
	if (((mBfIRBuilder->mIgnoreWrites)) && ((flags & BfSrcPosFlag_Force) == 0))
		return;

	BF_ASSERT((!mAwaitingInitFinish) || (mBfIRBuilder->mIgnoreWrites));

	if (astNode == NULL)
		return;

	auto bfParser = astNode->GetSourceData()->ToParserData();
	if (bfParser == NULL)
		return;
	if ((mCurFilePosition.mFileInstance == NULL) || (mCurFilePosition.mFileInstance->mParser != bfParser))
	{
		if (mCurFilePosition.mFileInstance != NULL)
		{
			BF_ASSERT(mFileInstanceMap.GetCount() != 0);
			mCurFilePosition.mFileInstance->mPrevPosition = mCurFilePosition;
		}

		auto bfFileInstance = GetFileFromNode(astNode);
		if (bfFileInstance->mPrevPosition.mFileInstance != NULL)
		{
			mCurFilePosition = bfFileInstance->mPrevPosition;
		}
		else
		{
			mCurFilePosition.mFileInstance = bfFileInstance;
			mCurFilePosition.mCurLine = 0;
			mCurFilePosition.mCurColumn = 0;
			mCurFilePosition.mCurSrcPos = 0;
		}
	}

	int srcPos = astNode->GetSrcStart() + debugLocOffset;

	BF_ASSERT(srcPos < bfParser->mSrcLength);

	auto* jumpEntry = bfParser->mJumpTable + (srcPos / PARSER_JUMPTABLE_DIVIDE);
	if (jumpEntry->mCharIdx > srcPos)
		jumpEntry--;
	mCurFilePosition.mCurLine = jumpEntry->mLineNum;
	mCurFilePosition.mCurSrcPos = jumpEntry->mCharIdx;
	mCurFilePosition.mCurColumn = 0;

	while (mCurFilePosition.mCurSrcPos < srcPos)
	{
		if (bfParser->mSrc[mCurFilePosition.mCurSrcPos] == '\n')
		{
			mCurFilePosition.mCurLine++;
			mCurFilePosition.mCurColumn = 0;
		}
		else
		{
			mCurFilePosition.mCurColumn++;
		}

		mCurFilePosition.mCurSrcPos++;
	}

	//TODO: if we bail on the "mCurMethodState == NULL" case then we don't get it set during type declarations
	if (((flags & BfSrcPosFlag_NoSetDebugLoc) == 0) && (mBfIRBuilder->DbgHasLineInfo()) && (mCurMethodState != NULL))
	{
		int column = mCurFilePosition.mCurColumn + 1;
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend))
		{
			// Set to illegal position
			column = 0;
		}

		if (mSetIllegalSrcPosition)
			column = 0;

		BfIRMDNode useDIScope = mCurMethodState->mCurScope->mDIScope;
		auto wantDIFile = mCurFilePosition.mFileInstance->mDIFile;
		if ((wantDIFile != mCurMethodState->mDIFile) && (mCurMethodState->mDIFile))
		{
			// This is from a different scope...
			if (wantDIFile != mCurMethodState->mCurScope->mAltDIFile)
			{
				mCurMethodState->mCurScope->mAltDIFile = wantDIFile;
				mCurMethodState->mCurScope->mAltDIScope = mBfIRBuilder->DbgCreateLexicalBlock(mCurMethodState->mCurScope->mDIScope, wantDIFile, mCurFilePosition.mCurLine + 1, column);
			}
			if (mCurMethodState->mCurScope->mAltDIScope) // This may not be set if mAltDIFile is set by a mixin
				useDIScope = mCurMethodState->mCurScope->mAltDIScope;
		}
		else
		{
			if (mCurMethodState->mCurScope->mAltDIFile)
			{
				mCurMethodState->mCurScope->mAltDIFile = BfIRMDNode();
				mCurMethodState->mCurScope->mAltDIScope = BfIRMDNode();
			}
		}

		auto inlineAt = mCurMethodState->mCurScope->mDIInlinedAt;
		if (mCurMethodState->mCrossingMixin)
			inlineAt = BfIRMDNode();

		if ((!useDIScope) && (mIsComptimeModule))
			useDIScope = wantDIFile;

		if (!useDIScope)
			mBfIRBuilder->ClearDebugLocation();
		else
			mBfIRBuilder->SetCurrentDebugLocation(mCurFilePosition.mCurLine + 1, column, useDIScope, inlineAt);
		if ((flags & BfSrcPosFlag_Expression) == 0)
			mBfIRBuilder->CreateStatementStart();
	}
}

void BfModule::UpdateExprSrcPos(BfAstNode* astNode, BfSrcPosFlags flags)
{
	// We've turned off expr src pos (for now?)
	//UpdateSrcPos(astNode, (BfSrcPosFlags)(flags | BfSrcPosFlag_Expression));
}

void BfModule::UseDefaultSrcPos(BfSrcPosFlags flags, int debugLocOffset)
{
	if (mCompiler->mBfObjectTypeDef != NULL)
		UpdateSrcPos(mCompiler->mBfObjectTypeDef->mTypeDeclaration, flags, debugLocOffset);
	SetIllegalSrcPos();
}

void BfModule::SetIllegalSrcPos(BfSrcPosFlags flags)
{
	if ((mBfIRBuilder->DbgHasInfo()) && (mCurMethodState != NULL))
	{
		auto curScope = mCurMethodState->mCurScope->mDIScope;
		if (curScope)
		{
			if ((mCurMethodState->mCurScope->mDIInlinedAt) && (mCompiler->mOptions.IsCodeView()))
			{
				// Currently, CodeView does not record column positions so we can't use an illegal column position as an "invalid" marker
				mBfIRBuilder->SetCurrentDebugLocation(0, 0, curScope, mCurMethodState->mCurScope->mDIInlinedAt);
			}
			else
			{
				// Set to whatever it previously was but at column zero, which we will know to be illegal
				mBfIRBuilder->SetCurrentDebugLocation(mCurFilePosition.mCurLine + 1, 0, curScope, mCurMethodState->mCurScope->mDIInlinedAt);
			}
		}

		if ((flags & BfSrcPosFlag_Expression) == 0)
			mBfIRBuilder->CreateStatementStart();
	}
}

void BfModule::SetIllegalExprSrcPos(BfSrcPosFlags flags)
{
	// We've turned off expr src pos (for now?)
	//SetIllegalSrcPos((BfSrcPosFlags)(flags | BfSrcPosFlag_Expression));
}

bool BfModule::CheckProtection(BfProtection protection, BfTypeDef* checkType, bool allowProtected, bool allowPrivate)
{
	if ((protection == BfProtection_Public) ||
		(((protection == BfProtection_Protected) || (protection == BfProtection_ProtectedInternal)) && (allowProtected)) ||
		((protection == BfProtection_Private) && (allowPrivate)))
		return true;
	if ((mAttributeState != NULL) && (mAttributeState->mCustomAttributes != NULL) && (mAttributeState->mCustomAttributes->Contains(mCompiler->mFriendAttributeTypeDef)))
	{
		mAttributeState->mUsed = true;
		return true;
	}
	if (((protection == BfProtection_Internal) || (protection == BfProtection_ProtectedInternal)) && (checkType != NULL))
	{
		if (CheckInternalProtection(checkType))
			return true;
	}
	return false;
}

void BfModule::GetAccessAllowed(BfTypeInstance* checkType, bool &allowProtected, bool &allowPrivate)
{
	allowPrivate = (checkType == mCurTypeInstance) || (IsInnerType(mCurTypeInstance, checkType));
	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		auto mixinOwner = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
		allowPrivate |= (checkType == mixinOwner) || (IsInnerType(mixinOwner, checkType));
	}
	allowProtected = allowPrivate;
}

bool BfModule::CheckProtection(BfProtectionCheckFlags& flags, BfTypeInstance* memberOwner, BfProject* memberProject, BfProtection memberProtection, BfTypeInstance* lookupStartType)
{
	if (memberProtection == BfProtection_Hidden)
		return false;
	if (memberProtection == BfProtection_Public)
		return true;

	if ((flags & BfProtectionCheckFlag_CheckedPrivate) == 0)
	{
		BfTypeInstance* curCheckType = mCurTypeInstance;
		if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
		{
			auto mixinOwner = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
			curCheckType = mixinOwner;
		}
		bool allowPrivate = (curCheckType != NULL) && (memberOwner->IsInstanceOf(curCheckType->mTypeDef));
		if (curCheckType != NULL)
			allowPrivate |= IsInnerType(curCheckType->mTypeDef, memberOwner->mTypeDef);
		if (allowPrivate)
			flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_AllowPrivate | BfProtectionCheckFlag_CheckedPrivate);
		else
			flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_CheckedPrivate);
	}
	if ((flags & BfProtectionCheckFlag_AllowPrivate) != 0)
		return true;
	if ((memberProtection == BfProtection_Protected) || (memberProtection == BfProtection_ProtectedInternal))
	{
		if ((flags & BfProtectionCheckFlag_CheckedProtected) == 0)
		{
			bool allowProtected = false;
			auto memberOwnerTypeDef = memberOwner->mTypeDef;
			auto curCheckType = mCurTypeInstance;
			if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
			{
				auto mixinOwner = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
				curCheckType = mixinOwner;
			}

			if ((flags & BfProtectionCheckFlag_InstanceLookup) != 0)
			{
				auto lookupCheckType = lookupStartType;
				while (lookupCheckType->mInheritDepth >= curCheckType->mInheritDepth)
				{
					if (lookupCheckType == curCheckType)
					{
						allowProtected = true;
						break;
					}
					if (lookupCheckType == memberOwner)
						break;
					lookupCheckType = lookupCheckType->mBaseType;
					if (lookupCheckType == NULL)
						break;
				}
			}
			else
			{
				auto lookupCheckType = curCheckType;
				while (lookupCheckType->mInheritDepth >= memberOwner->mInheritDepth)
				{
					if (lookupCheckType == memberOwner)
					{
						allowProtected = true;
						break;
					}
					lookupCheckType = lookupCheckType->mBaseType;
					if (lookupCheckType == NULL)
						break;
				}
			}

			if (!allowProtected)
			{
				// It's possible our outer type is derived from 'memberOwner'
				while (curCheckType->mTypeDef->mNestDepth > 0)
				{
					curCheckType = GetOuterType(curCheckType);
					if (curCheckType == NULL)
						break;

					auto lookupCheckType = lookupStartType;
					while (lookupCheckType->mInheritDepth >= curCheckType->mInheritDepth)
					{
						if (lookupCheckType == curCheckType)
						{
							allowProtected = true;
							break;
						}
						if (lookupCheckType == memberOwner)
							break;
						lookupCheckType = lookupCheckType->mBaseType;
						if (lookupCheckType == NULL)
							break;
					}
				}
			}

			if (allowProtected)
				flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_CheckedProtected | BfProtectionCheckFlag_AllowProtected);
			else
				flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_CheckedProtected);
		}
		if ((flags & BfProtectionCheckFlag_AllowProtected) != 0)
			return true;
	}

	if ((mAttributeState != NULL) && (mAttributeState->mCustomAttributes != NULL) && (mAttributeState->mCustomAttributes->Contains(mCompiler->mFriendAttributeTypeDef)))
	{
		mAttributeState->mUsed = true;
		return true;
	}

	if (((memberProtection == BfProtection_Internal) || (memberProtection == BfProtection_ProtectedInternal)) && (memberOwner != NULL))
	{
		if (CheckInternalProtection(memberOwner->mTypeDef))
			return true;
	}

	return false;
}

void BfModule::SetElementType(BfAstNode* astNode, BfSourceElementType elementType)
{
	if (mCompiler->mResolvePassData != NULL)
	{
		if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(astNode))
			sourceClassifier->SetElementType(astNode, elementType);
	}
}

bool BfModule::PreFail()
{
	if (!mIgnoreErrors)
		return true;
	SetFail();
	return false;
}

void BfModule::SetFail()
{
	if (mIgnoreErrors)
	{
		if (mAttributeState != NULL)
			mAttributeState->mFlags = (BfAttributeState::Flags)(mAttributeState->mFlags | BfAttributeState::Flag_HadError);
	}
}

void BfModule::VerifyOnDemandMethods()
{
#ifdef _DEBUG
//  	if (mParentModule != NULL)
//  	{
//  		BF_ASSERT(mOnDemandMethodCount == 0);
//  		mParentModule->VerifyOnDemandMethods();
//  		return;
//  	}
//
//  	int onDemandCount = 0;
//  	for (auto type : mOwnedTypeInstances)
//  	{
//  		auto typeInst = type->ToTypeInstance();
//  		for (auto& methodGroup : typeInst->mMethodInstanceGroups)
//  		{
//  			if ((methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) ||
//  				(methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference) ||
//  				(methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl) ||
//  				(methodGroup.mOnDemandKind == BfMethodOnDemandKind_InWorkList))
//  				onDemandCount++;
//  		}
//  	}
//
//  	BF_ASSERT(mOnDemandMethodCount == onDemandCount);
#endif
}

bool BfModule::IsSkippingExtraResolveChecks()
{
	if (mIsComptimeModule)
		return false;
	return mCompiler->IsSkippingExtraResolveChecks();
}

bool BfModule::AddErrorContext(StringImpl& errorString, BfAstNode* refNode, BfWhileSpecializingFlags& isWhileSpecializing, bool isWarning)
{
	bool isWhileSpecializingMethod = false;
	if ((mIsSpecialModule) && (mModuleName == "vdata"))
		errorString += StrFormat("\n  while generating vdata for project '%s'", mProject->mName.c_str());
	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend))
		errorString += StrFormat("\n  while generating append size calculating method");
	else if (refNode == NULL)
	{
		if (mCurTypeInstance != NULL)
			errorString += StrFormat("\n  while compiling '%s'", TypeToString(mCurTypeInstance, BfTypeNameFlags_None).c_str());
		else if (mProject != NULL)
			errorString += StrFormat("\n  while compiling project '%s'", mProject->mName.c_str());
	}

	if (mCurTypeInstance != NULL)
	{
		auto _CheckMethodInstance = [&](BfMethodInstance* methodInstance)
		{
			// Propogate the fail all the way to the main method (assuming we're in a local method or lambda)
			if (isWarning)
				methodInstance->mHasWarning = true;
			else
				methodInstance->mHasFailed = true;

			if (!methodInstance->mHasStartedDeclaration)
				StartMethodDeclaration(methodInstance, NULL);

			bool isSpecializedMethod = ((methodInstance != NULL) && (!methodInstance->mIsUnspecialized) && (methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mMethodGenericArguments.size() != 0));
			if (isSpecializedMethod)
			{
				//auto unspecializedMethod = &mCurMethodInstance->mMethodInstanceGroup->mMethodSpecializationMap.begin()->second;
				auto unspecializedMethod = methodInstance->mMethodInstanceGroup->mDefault;
				if (unspecializedMethod == methodInstance)
				{
					// This is a local method inside a generic method
					BF_ASSERT(methodInstance->mMethodDef->mIsLocalMethod);
				}
				else
				{
					if (isWarning)
					{
						if (unspecializedMethod->mHasWarning)
							return false; // At least SOME error has already been reported
					}
					else
					{
						if (unspecializedMethod->mHasFailed)
							return false; // At least SOME error has already been reported
					}
				}
			}

			if (isSpecializedMethod)
			{
				errorString += StrFormat("\n  while specializing method '%s'", MethodToString(methodInstance).c_str());
				isWhileSpecializing = (BfWhileSpecializingFlags)(isWhileSpecializing | BfWhileSpecializingFlag_Method);
				isWhileSpecializingMethod = true;
			}
			else if ((methodInstance != NULL) && (methodInstance->mIsForeignMethodDef))
			{
				errorString += StrFormat("\n  while implementing default interface method '%s'", MethodToString(methodInstance).c_str());
				isWhileSpecializing = (BfWhileSpecializingFlags)(isWhileSpecializing | BfWhileSpecializingFlag_Method);
				isWhileSpecializingMethod = true;
			}

			if ((mCurTypeInstance->IsGenericTypeInstance()) && (!mCurTypeInstance->IsUnspecializedType()))
			{
				errorString += StrFormat("\n  while specializing type '%s'", TypeToString(mCurTypeInstance).c_str());
				isWhileSpecializing = (BfWhileSpecializingFlags)(isWhileSpecializing | BfWhileSpecializingFlag_Type);
			}

			return true;
		};

		bool hadMethodInstance = false;
		if (mCurMethodState != NULL)
		{
			auto checkMethodState = mCurMethodState;
			while (checkMethodState != NULL)
			{
				auto methodInstance = checkMethodState->mMethodInstance;
				if (methodInstance == NULL)
				{
					checkMethodState = checkMethodState->mPrevMethodState;
					continue;
				}

				hadMethodInstance = true;
				if (!_CheckMethodInstance(methodInstance))
					return false;
				checkMethodState = checkMethodState->mPrevMethodState;
			}
		}

		if ((!hadMethodInstance) && (mCurMethodInstance != NULL))
		{
			if (!_CheckMethodInstance(mCurMethodInstance))
				return false;
		}
	}

	if ((!isWhileSpecializing) && (mCurTypeInstance != NULL) && ((mCurTypeInstance->IsGenericTypeInstance()) && (!mCurTypeInstance->IsUnspecializedType())))
	{
		errorString += StrFormat("\n  while specializing type '%s'", TypeToString(mCurTypeInstance).c_str());
		isWhileSpecializing = (BfWhileSpecializingFlags)(isWhileSpecializing | BfWhileSpecializingFlag_Type);
	}

	return true;
}

BfError* BfModule::Fail(const StringImpl& error, BfAstNode* refNode, bool isPersistent, bool deferError)
{
	BP_ZONE("BfModule::Fail");

 	if (mIgnoreErrors)
	{
		mHadIgnoredError = true;
		if (mAttributeState != NULL)
			mAttributeState->mFlags = (BfAttributeState::Flags)(mAttributeState->mFlags | BfAttributeState::Flag_HadError);
	 	return NULL;
	}

 	if (!mReportErrors)
	{
		mCompiler->mPassInstance->SilentFail();
		return NULL;
	}

 	if (refNode != NULL)
		refNode = BfNodeToNonTemporary(refNode);

	//BF_ASSERT(refNode != NULL);

	if (mIsComptimeModule)
	{
		if (auto ceDbgState = GetCeDbgState())
		{
			// Follow normal fail path
		}
		else
		{
			mHadBuildError = true;

			if ((mCompiler->mCeMachine->mCurContext != NULL) && (mCompiler->mCeMachine->mCurContext->mCurCallSource != NULL) &&
				(mCompiler->mCeMachine->mCurContext->mCurCallSource->mRefNode != NULL))
			{
				BfError* bfError = mCompiler->mPassInstance->Fail("Comptime method generation had errors",
					mCompiler->mCeMachine->mCurContext->mCurCallSource->mRefNode);
				if (bfError != NULL)
					mCompiler->mPassInstance->MoreInfo(error, refNode);
				return bfError;
			}

			return NULL;
		}
	}

	if ((mCurMethodState != NULL) && (mCurMethodState->mConstResolveState != NULL) && (mCurMethodState->mConstResolveState->mInCalcAppend))
	{
		mCurMethodState->mConstResolveState->mFailed = true;
		return NULL;
	}

 	if (mCurMethodInstance != NULL)
		mCurMethodInstance->mHasFailed = true;

	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsUnspecializedTypeVariation()))
		return NULL;

 	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->IsOrInUnspecializedVariation()))
		return NULL; // Ignore errors on unspecialized variations, they are always dups
	if (!mHadBuildError)
		mHadBuildError = true;
	if (mParentModule != NULL)
		mParentModule->mHadBuildError = true;

	if (mCurTypeInstance != NULL)
		AddFailType(mCurTypeInstance);

	BfLogSysM("BfModule::Fail module %p type %p %s\n", this, mCurTypeInstance, error.c_str());

 	String errorString = error;
	BfWhileSpecializingFlags isWhileSpecializing = BfWhileSpecializingFlag_None;
	if (!AddErrorContext(errorString, refNode, isWhileSpecializing, false))
		return NULL;

	BfError* bfError = NULL;
	if (isWhileSpecializing)
		deferError = true;

	if (!mHadBuildError)
		mHadBuildError = true;

	// Check mixins
	{
		auto checkMethodState = mCurMethodState;
		while (checkMethodState != NULL)
		{
			auto rootMixinState = checkMethodState->GetRootMixinState();
			if (rootMixinState != NULL)
			{
				String mixinErr = StrFormat("Failed to inject mixin '%s'", MethodToString(rootMixinState->mMixinMethodInstance).c_str());
				if (deferError)
					bfError = mCompiler->mPassInstance->DeferFail(mixinErr, rootMixinState->mSource);
				else
					bfError = mCompiler->mPassInstance->Fail(mixinErr, rootMixinState->mSource);

				if (bfError == NULL)
					return NULL;

				bfError->mIsWhileSpecializing = isWhileSpecializing;
				mCompiler->mPassInstance->MoreInfo(errorString, refNode);

				auto mixinState = checkMethodState->mMixinState;
				while ((mixinState != NULL) && (mixinState->mPrevMixinState != NULL))
				{
					mCompiler->mPassInstance->MoreInfo(StrFormat("Injected from mixin '%s'", MethodToString(mixinState->mPrevMixinState->mMixinMethodInstance).c_str()), mixinState->mSource);
					mixinState = mixinState->mPrevMixinState;
				}

				return bfError;
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	if (deferError)
		bfError = mCompiler->mPassInstance->Fail(errorString, refNode);
	else if (refNode == NULL)
		bfError = mCompiler->mPassInstance->Fail(errorString);
	else
		bfError = mCompiler->mPassInstance->Fail(errorString, refNode);
	if (bfError != NULL)
	{
		bfError->mIsWhileSpecializing = isWhileSpecializing;
		bfError->mProject = mProject;
		bfError->mIsPersistent = isPersistent;

		if ((mCurMethodState != NULL) && (mCurMethodState->mDeferredCallEmitState != NULL) && (mCurMethodState->mDeferredCallEmitState->mCloseNode != NULL))
			mCompiler->mPassInstance->MoreInfo("Error during deferred statement handling", mCurMethodState->mDeferredCallEmitState->mCloseNode);
		else if ((mCurMethodState != NULL) && (mCurMethodState->mEmitRefNode != NULL))
			mCompiler->mPassInstance->MoreInfo("Error in emitted code", mCurMethodState->mEmitRefNode);
	}

	return bfError;
}

BfError* BfModule::FailInternal(const StringImpl& error, BfAstNode* refNode)
{
	if (mHadBuildError)
		return NULL;

	return Fail(error, refNode);
}

BfError* BfModule::FailAfter(const StringImpl& error, BfAstNode* refNode)
{
	if (mIgnoreErrors)
		return NULL;

	if (refNode != NULL)
		refNode = BfNodeToNonTemporary(refNode);

	mHadBuildError = true;
	BfError* bfError =  mCompiler->mPassInstance->FailAfter(error, refNode);
	if (bfError != NULL)
		bfError->mProject = mProject;
	return bfError;
}

BfError* BfModule::Warn(int warningNum, const StringImpl& warning, BfAstNode* refNode, bool isPersistent, bool showInSpecialized)
{
	if (mIgnoreErrors || mIgnoreWarnings)
		return NULL;

	if (!mReportErrors)
	{
		mCompiler->mPassInstance->SilentFail();
		return NULL;
	}

	BfAstNode* unwarnNode = refNode;
	//
	{
		BfParentNodeEntry* parentNodeEntry = mParentNodeEntry;
		while (parentNodeEntry != NULL)
		{
			if (auto block = BfNodeDynCast<BfBlock>(parentNodeEntry->mNode))
				break;
			unwarnNode = parentNodeEntry->mNode;
			parentNodeEntry = parentNodeEntry->mPrev;
		}
	}
	auto parser = unwarnNode->GetSourceData()->ToParserData();
	if ((parser != NULL) && (parser->IsUnwarnedAt(unwarnNode)))
	{
		return NULL;
	}

	// Right now we're only warning on the unspecialized declarations, we may revisit this
	if (mCurMethodInstance != NULL)
	{
		if (mCurMethodInstance->IsSpecializedGenericMethodOrType())
		{
			if (!showInSpecialized)
				return NULL;
		}
		if (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend)
			return NULL; // No ctorCalcAppend warnings
	}
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsSpecializedType()))
	{
		if (!showInSpecialized)
			return NULL;
	}

	if (refNode != NULL)
		refNode = BfNodeToNonTemporary(refNode);

	String warningString = warning;
	BfWhileSpecializingFlags isWhileSpecializing = BfWhileSpecializingFlag_None;
	if (!AddErrorContext(warningString, refNode, isWhileSpecializing, true))
		return NULL;
	bool deferWarning = isWhileSpecializing != BfWhileSpecializingFlag_None;

	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		// We used to bubble up warnings into the mixin injection site, BUT
		//  we are now assuming any relevant warnings will be given at the declaration site
		return NULL;
	}

	if ((mCurMethodState != NULL) && (mCurMethodState->mEmitRefNode != NULL))
	{
		BfError* bfError = mCompiler->mPassInstance->Warn(warningNum, "Emitted code had errors", mCurMethodState->mEmitRefNode);
		if (bfError != NULL)
			mCompiler->mPassInstance->MoreInfo(warningString, refNode);
		return bfError;
	}

	BfError* bfError;
	if (refNode != NULL)
		bfError = mCompiler->mPassInstance->WarnAt(warningNum, warningString, refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength());
	else
		bfError = mCompiler->mPassInstance->Warn(warningNum, warningString);
	if (bfError != NULL)
	{
		bfError->mIsWhileSpecializing = isWhileSpecializing;
		bfError->mProject = mProject;
		AddFailType(mCurTypeInstance);

		mHadBuildWarning = true;
		bfError->mIsPersistent = isPersistent;

		if ((mCompiler->IsAutocomplete()) && (mCompiler->mResolvePassData->mAutoComplete->CheckFixit((refNode))))
		{
			BfParserData* parser = refNode->GetSourceData()->ToParserData();
			if (parser != NULL)
			{
				int fileLoc = BfFixitFinder::FindLineStartBefore(refNode);
				mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("#unwarn\tunwarn|%s|%d|#unwarn|", parser->mFileName.c_str(), fileLoc).c_str()));

				if (warningNum != 0)
				{
					mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("#pragma warning disable %d\t.pragma|%s|%d||#pragma warning disable %d",
						warningNum, parser->mFileName.c_str(), 0, warningNum).c_str()));
				}
			}
		}
	}
	return bfError;
}

void BfModule::CheckErrorAttributes(BfTypeInstance* typeInstance, BfMethodInstance* methodInstance, BfFieldInstance* fieldInstance, BfCustomAttributes* customAttributes, BfAstNode* targetSrc)
{
	if (customAttributes == NULL)
		return;

	auto _AddDeclarationMoreInfo = [&]()
	{
		if (methodInstance != NULL)
		{
			if (methodInstance->mMethodDef->mMethodDeclaration != NULL)
				mCompiler->mPassInstance->MoreInfo(
					StrFormat("See method declaration '%s'", MethodToString(methodInstance).c_str()),
					methodInstance->mMethodDef->GetRefNode());
		}
		else
		{
			mCompiler->mPassInstance->MoreInfo(
				StrFormat("See type declaration '%s'", TypeToString(typeInstance, BfTypeNameFlag_UseUnspecializedGenericParamNames).c_str()),
				typeInstance->mTypeDef->GetRefNode());
		}
	};

	BfIRConstHolder* constHolder = typeInstance->mConstHolder;
	auto customAttribute = customAttributes->Get(mCompiler->mObsoleteAttributeTypeDef);
	if ((customAttribute != NULL) && (!customAttribute->mCtorArgs.IsEmpty()) && (targetSrc != NULL))
	{
		String err;
		if (fieldInstance != NULL)
			err = StrFormat("'%s' is obsolete", FieldToString(fieldInstance).c_str());
		else if (methodInstance != NULL)
			err = StrFormat("'%s' is obsolete", MethodToString(methodInstance).c_str());
		else
			err = StrFormat("'%s' is obsolete", TypeToString(typeInstance, BfTypeNameFlag_UseUnspecializedGenericParamNames).c_str());

		bool isError = false;

		auto constant = constHolder->GetConstant(customAttribute->mCtorArgs[0]);
		if (constant->mTypeCode == BfTypeCode_Boolean)
		{
			isError = constant->mBool;
		}
		else if (customAttribute->mCtorArgs.size() >= 2)
		{
			String* str = GetStringPoolString(customAttribute->mCtorArgs[0], constHolder);
			if (str != NULL)
			{
				err += ":\n    '";
				err += *str;
				err += "'";
			}

			constant = constHolder->GetConstant(customAttribute->mCtorArgs[1]);
			isError = constant->mBool;
		}

		BfError* error = NULL;
		if (isError)
			error = Fail(err, targetSrc);
		else
			error = Warn(0, err, targetSrc, false, true);
		if (error != NULL)
			_AddDeclarationMoreInfo();
	}

	customAttribute = customAttributes->Get(mCompiler->mErrorAttributeTypeDef);
	if ((customAttribute != NULL) && (!customAttribute->mCtorArgs.IsEmpty()))
	{
		String err;
		if (fieldInstance != NULL)
			err += StrFormat("Method error: '", FieldToString(fieldInstance).c_str());
		else if (methodInstance != NULL)
			err += StrFormat("Method error: '", MethodToString(methodInstance).c_str());
		else
			err += StrFormat("Type error: '", TypeToString(typeInstance, BfTypeNameFlag_UseUnspecializedGenericParamNames).c_str());
		String* str = GetStringPoolString(customAttribute->mCtorArgs[0], constHolder);
		if (str != NULL)
			err += *str;
		err += "'";
		if (Fail(err, targetSrc) != NULL)
			_AddDeclarationMoreInfo();
	}

	customAttribute = customAttributes->Get(mCompiler->mWarnAttributeTypeDef);
	if ((customAttribute != NULL) && (!customAttribute->mCtorArgs.IsEmpty()) && (targetSrc != NULL))
	{
		String err;
		if (fieldInstance != NULL)
			err += StrFormat("Field warning: '", FieldToString(fieldInstance).c_str());
		else if (methodInstance != NULL)
			err += StrFormat("Method warning: '", MethodToString(methodInstance).c_str());
		else
			err += StrFormat("Type warning: '", TypeToString(typeInstance, BfTypeNameFlag_UseUnspecializedGenericParamNames).c_str());
		String* str = GetStringPoolString(customAttribute->mCtorArgs[0], constHolder);
		if (str != NULL)
			err += *str;
		err += "'";
		if (Warn(0, err, targetSrc, false, true) != NULL)
			_AddDeclarationMoreInfo();
	}
}

void BfModule::CheckRangeError(BfType* type, BfAstNode* refNode)
{
	if (mBfIRBuilder->mOpFailed)
		Fail(StrFormat("Result out of range for type '%s'", TypeToString(type).c_str()), refNode);
}

void BfModule::FatalError(const StringImpl& error, const char* file, int line)
{
	static bool sHadFatalError = false;
	static bool sHadReentrancy = false;

	if (sHadFatalError)
	{
		return;
		sHadReentrancy = true;
	}
	sHadFatalError = true;

	String fullError = error;

	if (file != NULL)
		fullError += StrFormat(" at %s:%d", file, line);

	fullError += StrFormat("\nModule: %s", mModuleName.c_str());

	if (mCurTypeInstance != NULL)
		fullError += StrFormat("\nType: %s", TypeToString(mCurTypeInstance).c_str());
	if (mCurMethodInstance != NULL)
		fullError += StrFormat("\nMethod: %s", MethodToString(mCurMethodInstance).c_str());

	if ((mCurFilePosition.mFileInstance != NULL) && (mCurFilePosition.mFileInstance->mParser != NULL))
		fullError += StrFormat("\nSource Location: %s:%d", mCurFilePosition.mFileInstance->mParser->mFileName.c_str(), mCurFilePosition.mCurLine + 1);

	if (sHadReentrancy)
		fullError += "\nError had reentrancy";

	BfpSystem_FatalError(fullError.c_str(), "FATAL MODULE ERROR");
}

void BfModule::NotImpl(BfAstNode* astNode)
{
	Fail("INTERNAL ERROR: Not implemented", astNode);
}

bool BfModule::CheckAccessMemberProtection(BfProtection protection, BfTypeInstance* memberType)
{
	bool allowPrivate = (memberType == mCurTypeInstance) || (IsInnerType(mCurTypeInstance, memberType));
	if (!allowPrivate)
		allowPrivate |= memberType->IsInterface();
	bool allowProtected = allowPrivate;
	//TODO: We had this commented out, but this makes accessing protected properties fail
	if (mCurTypeInstance != NULL)
		allowProtected |= TypeIsSubTypeOf(mCurTypeInstance, memberType->ToTypeInstance());
	if (!CheckProtection(protection, memberType->mTypeDef, allowProtected, allowPrivate))
	{
		return false;
	}
	return true;
}

bool BfModule::CheckDefineMemberProtection(BfProtection protection, BfType* memberType)
{
	// Use 'min' - exporting a 'public' from a 'private' class is really just 'private' still
	protection = std::min(protection, mCurTypeInstance->mTypeDef->mProtection);

	auto memberTypeInstance = memberType->ToTypeInstance();

	if (memberTypeInstance == NULL)
	{
		auto underlyingType = memberType->GetUnderlyingType();
		if (underlyingType != NULL)
			return CheckDefineMemberProtection(protection, underlyingType);
		return true;
	}

	if (memberTypeInstance->mTypeDef->mProtection < protection)
		return false;

	if (memberTypeInstance->IsGenericTypeInstance())
	{
		// When we're a generic struct, our data layout can depend on our generic parameters as well
		auto genericTypeInstance = (BfTypeInstance*) memberTypeInstance;
		for (auto typeGenericArg : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
		{
			if (!CheckDefineMemberProtection(protection, typeGenericArg))
				return false;
		}
	}

	return true;
}

void BfModule::AddDependency(BfType* usedType, BfType* userType, BfDependencyMap::DependencyFlags flags, BfDepContext* depContext)
{
	if (usedType == userType)
		return;

	if (((flags & BfDependencyMap::DependencyFlag_ConstValue) != 0) && (mContext->mCurTypeState != NULL) && (mContext->mCurTypeState->mResolveKind == BfTypeState::ResolveKind_FieldType))
	{
		// This can be an `int32[UsedType.cVal]` type reference
		flags = (BfDependencyMap::DependencyFlags)(flags | BfDependencyMap::DependencyFlag_ValueTypeSizeDep);
	}

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsAutocompleteMethod))
	{
		if (userType->IsMethodRef())
		{
			// We cannot short-circuit dependencies because of method group ref counting
		}
		else
			return;
	}

	if (usedType->IsSpecializedByAutoCompleteMethod())
	{
		if ((flags & (BfDependencyMap::DependencyFlag_TypeGenericArg | BfDependencyMap::DependencyFlag_MethodGenericArg |
			BfDependencyMap::DependencyFlag_OuterType | BfDependencyMap::DependencyFlag_DerivedFrom)) == 0)
			return;
	}

// 	if (usedType->IsBoxed())
// 	{
// 		NOP;
// 		auto underlyingType = usedType->GetUnderlyingType()->ToTypeInstance();
// 		if ((underlyingType != NULL) && (underlyingType->IsInstanceOf(mCompiler->mSizedArrayTypeDef)))
// 		{
// 			BfLogSysM("AddDependency UsedType:%p UserType:%p Method:%p\n", usedType, userType, mCurMethodInstance);
// 		}
// 	}

	// TODO: It seems this was bogus - it kept the root array from being marked as a dependency (for example)
// 	BfType* origUsedType = usedType;
 	bool isDataAccess = ((flags & (BfDependencyMap::DependencyFlag_ReadFields | BfDependencyMap::DependencyFlag_LocalUsage | BfDependencyMap::DependencyFlag_Allocates)) != 0);
// 	if (isDataAccess)
// 	{
// 		while (true)
// 		{
// 			if ((usedType->IsPointer()) || (usedType->IsRef()) || (usedType->IsSizedArray()))
// 				usedType = usedType->GetUnderlyingType();
// 			else if (usedType->IsArray())
// 			{
// 				usedType = ((BfTypeInstance*)usedType)->mGenericTypeInfo->mTypeGenericArguments[0];
// 			}
// 			else
// 				break;
// 		}
// 	}

	if ((mCurMethodState != NULL) && (mCurMethodState->mHotDataReferenceBuilder != NULL) && (usedType != mCurTypeInstance) && (isDataAccess))
	{
		bool addType = true;
		auto checkType = usedType;
		PopulateType(checkType, BfPopulateType_Data);
		if (checkType->IsValuelessType())
			addType = false;
		else if (checkType->IsPrimitiveType())
			addType = false;
		else
		{
			auto checkTypeInst = checkType->ToTypeInstance();
			if (checkTypeInst == NULL)
			{
				addType = false;
			}
			else
			{
				if (TypeIsSubTypeOf(mCurTypeInstance, checkTypeInst))
					addType = false;
			}
		}

		if (addType)
		{
			auto checkTypeInst = checkType->ToTypeInstance();
			if (checkTypeInst->mHotTypeData != NULL)
			{
				auto hotTypeVersion = checkTypeInst->mHotTypeData->GetLatestVersion();
				BF_ASSERT(hotTypeVersion != NULL);
				if (hotTypeVersion != NULL)
				{
					bool isAllocation = ((flags & BfDependencyMap::DependencyFlag_Allocates) != 0);
					if (((flags & BfDependencyMap::DependencyFlag_LocalUsage) != 0) &&
						(checkType->IsComposite()))
						isAllocation = true;

					if (isAllocation)
					{
						mCurMethodState->mHotDataReferenceBuilder->mAllocatedData.Add(hotTypeVersion);
					}
					else
						mCurMethodState->mHotDataReferenceBuilder->mUsedData.Add(hotTypeVersion);
				}
			}
		}
	}

	if ((!mCompiler->mIsResolveOnly) && (mIsReified))
	{
		auto usingModule = userType->GetModule();
		BfModule* usedModule;
		if (usedType->IsFunction())
		{
			if (mCompiler->mFunctionTypeDef != NULL)
			{
				auto functionType = ResolveTypeDef(mCompiler->mFunctionTypeDef)->ToTypeInstance();
				usedModule = functionType->GetModule();
			}
		}
		else
			usedModule = usedType->GetModule();

		if ((usingModule != NULL) && (!usingModule->mIsSpecialModule) &&
			(usedModule != NULL) && (!usedModule->mIsSpecialModule))
		{
			if ((flags & ~(BfDependencyMap::DependencyFlag_OuterType)) == 0)
			{
				// Not a 'use' dependency
			}
			else
			{
				usingModule->mModuleRefs.Add(usedModule);
			}
		}
	}

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL) && (flags != BfDependencyMap::DependencyFlag_MethodGenericArg))
	{
		// When we are specializing a method, usage of that specialized type is already handled with DependencyFlag_MethodGenericArg
		for (auto genericArg : mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments)
			if (genericArg == usedType)
				return;
	}

	auto depFlag = flags;
	if ((flags & (BfDependencyMap::DependencyFlag_MethodGenericArg | BfDependencyMap::DependencyFlag_TypeGenericArg)) != 0)
	{
		if (usedType->IsDependendType())
		{
			// Cause a rebuild but not an outright deletion of the type
			// We can only do this if the 'usedType' can actually hold the dependency which can actually trigger a deletion chain
			depFlag = BfDependencyMap::DependencyFlag_GenericArgRef;
		}
	}

	if (usedType->IsTypeAlias())
	{
		auto underlyingType = usedType->GetUnderlyingType();
		if (underlyingType != NULL)
		{
			BfDepContext newDepContext;
			if (depContext == NULL)
				depContext = &newDepContext;

			if (++depContext->mAliasDepth > 8)
			{
				if (!depContext->mDeepSeenAliases.Add(underlyingType))
					return; // Circular!
			}

			AddDependency(underlyingType, userType, depFlag);
		}
	}
	else if (!usedType->IsGenericTypeInstance())
	{
		auto underlyingType = usedType->GetUnderlyingType();
		if (underlyingType != NULL)
			AddDependency(underlyingType, userType, depFlag);
	}

	BfDependedType* checkDType = usedType->ToDependedType();
	if (checkDType == NULL)
		return;

	if ((usedType->mRebuildFlags & BfTypeRebuildFlag_AwaitingReference) != 0)
		mContext->MarkAsReferenced(checkDType);

#ifdef _DEBUG
	// If a MethodRef depends ON US, that means it's a local method that we own
	if (userType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)userType;
		BF_ASSERT(methodRefType->mOwner == checkDType);
	}
#endif

	if (!checkDType->mDependencyMap.AddUsedBy(userType, flags))
		return;
	if ((checkDType->IsGenericTypeInstance()) && (!userType->IsMethodRef()))
	{
		auto genericTypeInstance = (BfTypeInstance*) checkDType;
		for (auto genericArg : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
		{
			AddDependency(genericArg, userType, depFlag);
		}
	}
	if (checkDType->IsTuple())
	{
		for (auto& field : checkDType->ToTypeInstance()->mFieldInstances)
		{
			if (field.mDataIdx != -1)
				AddDependency(field.mResolvedType, userType, depFlag);
		}
	}
}

void BfModule::AddDependency(BfGenericParamInstance* genericParam, BfTypeInstance* usingType)
{
	if ((genericParam->mExternType != NULL) && (!genericParam->mExternType->IsGenericParam()))
		AddDependency(genericParam->mExternType, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
	for (auto constraintTypeInst : genericParam->mInterfaceConstraints)
		AddDependency(constraintTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
	for (auto& operatorConstraint : genericParam->mOperatorConstraints)
	{
		if (operatorConstraint.mLeftType != NULL)
			AddDependency(operatorConstraint.mLeftType, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
		if (operatorConstraint.mRightType != NULL)
			AddDependency(operatorConstraint.mRightType, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
	}
	if (genericParam->mTypeConstraint != NULL)
		AddDependency(genericParam->mTypeConstraint, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
}

void BfModule::AddCallDependency(BfMethodInstance* methodInstance, bool devirtualized)
{
	if ((mCurMethodState != NULL) && (mCurMethodState->mHotDataReferenceBuilder != NULL))
	{
		if (methodInstance->mHotMethod == NULL)
			CheckHotMethod(methodInstance, "");
		BF_ASSERT(methodInstance->mHotMethod != NULL);
		if (devirtualized)
			mCurMethodState->mHotDataReferenceBuilder->mDevirtualizedCalledMethods.Add(methodInstance->mHotMethod);
		else
			mCurMethodState->mHotDataReferenceBuilder->mCalledMethods.Add(methodInstance->mHotMethod);
	}
}

bool BfModule::IsAttribute(BfTypeInstance* typeInst)
{
	auto checkTypeInst = typeInst;
	while (checkTypeInst != NULL)
	{
		if (checkTypeInst->IsInstanceOf(mCompiler->mAttributeTypeDef))
			return true;

		checkTypeInst = checkTypeInst->mBaseType;
	}
	return false;
}

void BfModule::PopulateGlobalContainersList(const BfGlobalLookup& globalLookup)
{
	if (mContext->mCurTypeState == NULL)
		return;

	BP_ZONE("PopulateGlobalContainersList");

	BfTypeDef* userTypeDef = mContext->mCurTypeState->mCurTypeDef;
	if ((userTypeDef == NULL) && (mCurMethodInstance != NULL))
		userTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;
	if (userTypeDef == NULL)
		userTypeDef = mCurTypeInstance->mTypeDef;

	if (mContext->mCurTypeState->mGlobalContainerCurUserTypeDef != userTypeDef)
	{
		mContext->mCurTypeState->mGlobalContainers.Clear();
		for (int namespaceIdx = -1; namespaceIdx < (int)userTypeDef->mNamespaceSearch.size(); namespaceIdx++)
		{
			BfAtomComposite findStr;
			if (namespaceIdx != -1)
				findStr.Reference(userTypeDef->mNamespaceSearch[namespaceIdx]);
			auto typeDefItr = mSystem->mTypeDefs.TryGet(findStr);
			while (typeDefItr)
			{
				auto typeDef = *typeDefItr;
				if ((typeDef->mFullName == findStr) && (typeDef->mIsCombinedPartial) && (typeDef->IsGlobalsContainer()))
				{
					if (typeDef->mProject->ContainsReference(typeDef->mProject))
					{
						BfGlobalContainerEntry globalEntry;
						globalEntry.mTypeDef = typeDef;
						globalEntry.mTypeInst = NULL;
						typeDef->PopulateMemberSets();
						mContext->mCurTypeState->mGlobalContainers.Add(globalEntry);
					}
				}
				else if (!typeDef->mIsPartial)
				{
					//break;
				}

				typeDefItr.MoveToNextHashMatch();
			}
		}

		mContext->mCurTypeState->mGlobalContainerCurUserTypeDef = userTypeDef;
	}

	// Only create actual typeInst when we have an applicable member.  This helps in keeping unused globals from
	//  reifing types
	for (auto& globalEntry : mContext->mCurTypeState->mGlobalContainers)
	{
		if (globalEntry.mTypeInst == NULL)
		{
			bool include = false;
			if (globalLookup.mKind == BfGlobalLookup::Kind_All)
				include = true;
			else if (globalLookup.mKind == BfGlobalLookup::Kind_Field)
			{
				if (globalEntry.mTypeDef->mFieldSet.ContainsWith(globalLookup.mName))
					include = true;
				if (globalEntry.mTypeDef->mPropertySet.ContainsWith(globalLookup.mName))
					include = true;
			}
			else if (globalLookup.mKind == BfGlobalLookup::Kind_Method)
			{
				if (globalEntry.mTypeDef->mMethodSet.ContainsWith(globalLookup.mName))
					include = true;
			}

			if (include)
			{
				auto type = ResolveTypeDef(globalEntry.mTypeDef);
				if (type != NULL)
					globalEntry.mTypeInst = type->ToTypeInstance();
			}
		}
	}
}

BfStaticSearch* BfModule::GetStaticSearch()
{
	auto activeTypeDef = GetActiveTypeDef();
	BfStaticSearch* staticSearch = NULL;
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->mStaticSearchMap.TryGetValue(activeTypeDef, &staticSearch)))
		return staticSearch;
	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mStaticSearchMap.TryGetValue(activeTypeDef, &staticSearch)))
		return staticSearch;
	return NULL;
}

BfInternalAccessSet* BfModule::GetInternalAccessSet()
{
	auto activeTypeDef = GetActiveTypeDef();
	BfInternalAccessSet* internalAccessSet = NULL;
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->mInternalAccessMap.TryGetValue(activeTypeDef, &internalAccessSet)))
		return internalAccessSet;
	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mInternalAccessMap.TryGetValue(activeTypeDef, &internalAccessSet)))
		return internalAccessSet;
	return NULL;
}

bool BfModule::CheckInternalProtection(BfTypeDef* usingTypeDef)
{
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsSpecializedType()))
		return true;
	if ((mCurMethodInstance != NULL) &&
		((mCurMethodInstance->mIsUnspecializedVariation) || (mCurMethodInstance->IsSpecializedGenericMethod())))
		return true;

	auto internalAccessSet = GetInternalAccessSet();
	if (internalAccessSet == NULL)
		return false;

	for (auto& nameComposite : internalAccessSet->mNamespaces)
	{
		if (usingTypeDef->mNamespace.StartsWith(nameComposite))
			return true;
	}

	for (auto internalType : internalAccessSet->mTypes)
	{
		auto checkTypeDef = usingTypeDef->GetDefinition();
		while (checkTypeDef != NULL)
		{
			if (checkTypeDef == internalType->mTypeDef->GetDefinition())
				return true;
			checkTypeDef = checkTypeDef->mOuterType;
		}
	}
	return false;
}

void PrintUsers(llvm::MDNode* md);

BfModuleOptions BfModule::GetModuleOptions()
{
	if (mIsScratchModule)
		return BfModuleOptions();

	if (mModuleOptions != NULL)
		return *mModuleOptions;
	BfModuleOptions moduleOptions;
	moduleOptions.mEmitDebugInfo = mCompiler->mOptions.mEmitDebugInfo ? 1 : mCompiler->mOptions.mEmitLineInfo ? 2 : 0;
	if (mProject != NULL)
	{
		moduleOptions.mSIMDSetting = mProject->mCodeGenOptions.mSIMDSetting;
		moduleOptions.mOptLevel = mProject->mCodeGenOptions.mOptLevel;
	}

	auto headModule = this;
	while (headModule->mParentModule != NULL)
		headModule = headModule->mParentModule;

	BF_ASSERT((headModule->mOwnedTypeInstances.size() > 0) || (mModuleName == "vdata") || mIsSpecialModule);

	if (headModule->mOwnedTypeInstances.size() > 0)
	{
		auto typeInst = headModule->mOwnedTypeInstances[0];
		if (typeInst->mTypeOptionsIdx == -2)
			PopulateType(typeInst);
		if (typeInst->mTypeOptionsIdx != -1)
		{
			auto typeOptions = mSystem->GetTypeOptions(typeInst->mTypeOptionsIdx);
			moduleOptions.mSIMDSetting = (BfSIMDSetting)BfTypeOptions::Apply((int)moduleOptions.mSIMDSetting, typeOptions->mSIMDSetting);
			moduleOptions.mEmitDebugInfo = BfTypeOptions::Apply(moduleOptions.mEmitDebugInfo, typeOptions->mEmitDebugInfo);
			moduleOptions.mOptLevel = (BfOptLevel)BfTypeOptions::Apply((int)moduleOptions.mOptLevel, (int)typeOptions->mOptimizationLevel);
		}
	}
	return moduleOptions;
}

BfCheckedKind BfModule::GetDefaultCheckedKind()
{
	if ((mCurMethodState != NULL) && (mCurMethodState->mDisableChecks))
		return BfCheckedKind_Unchecked;
	bool runtimeChecks = mCompiler->mOptions.mRuntimeChecks;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		runtimeChecks = typeOptions->Apply(runtimeChecks, BfOptionFlags_RuntimeChecks);
	return runtimeChecks ? BfCheckedKind_Checked : BfCheckedKind_Unchecked;
}

void BfModule::AddFailType(BfTypeInstance* typeInstance)
{
	BF_ASSERT(typeInstance != NULL);
	if ((typeInstance->mRebuildFlags & BfTypeRebuildFlag_InFailTypes) != 0)
		return;
	typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags | BfTypeRebuildFlag_InFailTypes);
	mContext->mFailTypes.TryAdd(typeInstance, BfFailKind_Normal);
}

void BfModule::DeferRebuildType(BfTypeInstance* typeInstance)
{
	if ((typeInstance->mRebuildFlags & BfTypeRebuildFlag_RebuildQueued) != 0)
		return;
	mCompiler->mHasQueuedTypeRebuilds = true;
	typeInstance->mRebuildFlags = (BfTypeRebuildFlags)(typeInstance->mRebuildFlags | BfTypeRebuildFlag_RebuildQueued);
	AddFailType(typeInstance);
}

void BfModule::CheckAddFailType()
{
	//TODO: We removed this line, because failures in resolve-only generic method can occur in non-owning modules
	//  but we still need to add the owning type to the FailTypes list
	//.. OLD:
	// While we're inlining, it's possible that mCurTypeInstance belongs to another module, ignore it
	/*if (mCurTypeInstance->mModule != this)
		return;*/

	// Using mHadBuildWarning to insert into mFailTypes is our core of our strategy for reasserting
	//  warnings on rebuilds where otherwise the warnings wouldn't be generated because no code was
	//  being recompiled.  This forces types with warnings to be recompiled.

	// We had this 'mIsResolveOnly' check for speed in resolveOnly, but we removed it so the IDE can be
	//  constantly warning-aware
	if ((mHadBuildError) ||
		(mHadBuildWarning /*&& !mCompiler->mIsResolveOnly*/))
	{
		//mContext->mFailTypes.Add(mCurTypeInstance);
	}
}

void BfModule::MarkDerivedDirty(BfTypeInstance* typeInst)
{
	if (!mCompiler->IsHotCompile())
		return;

	typeInst->mDirty = true;

	for (auto& dep : typeInst->mDependencyMap)
	{
		auto depType = dep.mKey;
		auto depFlags = dep.mValue.mFlags;

		if ((depFlags & BfDependencyMap::DependencyFlag_DerivedFrom) != 0)
		{
			MarkDerivedDirty(depType->ToTypeInstance());
		}
	}
}

void BfModule::CreateStaticField(BfFieldInstance* fieldInstance, bool isThreadLocal)
{
	auto fieldType = fieldInstance->GetResolvedType();
	auto field = fieldInstance->GetFieldDef();
	if (fieldType->IsVar())
		return;

	BfIRValue initValue;

	if (field->mIsConst)
	{
		if (fieldType->IsPointer())
			fieldType = fieldType->GetUnderlyingType();
	}

	BfIRStorageKind storageKind = BfIRStorageKind_Normal;
	if ((fieldInstance->mCustomAttributes != NULL) && (fieldInstance->mCustomAttributes->Get(mCompiler->mExportAttributeTypeDef)))
		storageKind = BfIRStorageKind_Export;
	else if ((fieldInstance->mCustomAttributes != NULL) && (fieldInstance->mCustomAttributes->Get(mCompiler->mImportAttributeTypeDef)))
		storageKind = BfIRStorageKind_Import;

	if ((!field->mIsExtern) && (storageKind != BfIRStorageKind_Import))
		initValue = GetDefaultValue(fieldType);

	if (fieldInstance->mOwner->IsUnspecializedType())
	{
		// Placeholder
		auto ptrVal = CreatePointerType(fieldType);
		mStaticFieldRefs[fieldInstance] = GetDefaultValue(ptrVal);
	}
	else
	{
		BfLogSysM("Creating static field Module:%p Type:%p\n", this, fieldType);
		StringT<4096> staticVarName;
		BfMangler::Mangle(staticVarName, mCompiler->GetMangleKind(), fieldInstance);
		if ((!fieldType->IsValuelessType()) && (!staticVarName.StartsWith("#")))
		{
			BfIRType irType;

			if (fieldInstance->IsAppendedObject())
			{
				irType = mBfIRBuilder->MapTypeInst(fieldType->ToTypeInstance(), BfIRPopulateType_Eventually_Full);
				initValue = mBfIRBuilder->CreateConstAggZero(irType);
			}
			else
				irType = mBfIRBuilder->MapType(fieldType, BfIRPopulateType_Eventually_Full);

			BfIRValue globalVar = mBfIRBuilder->CreateGlobalVariable(
				irType,
				false,
				BfIRLinkageType_External,
				initValue,
				staticVarName,
				isThreadLocal);
			mBfIRBuilder->GlobalVar_SetAlignment(globalVar, fieldType->mAlign);
			if (storageKind != BfIRStorageKind_Normal)
				mBfIRBuilder->GlobalVar_SetStorageKind(globalVar, storageKind);

			BF_ASSERT(globalVar);
			mStaticFieldRefs[fieldInstance] = globalVar;
		}
	}
}

void BfModule::ResolveConstField(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfFieldDef* fieldDef, bool forceResolve)
{
	bool autoCompleteOnly = mCompiler->IsAutocomplete();

	BfType* fieldType = NULL;
	if (fieldInstance != NULL)
	{
		fieldType = fieldInstance->GetResolvedType();
		if (fieldType == NULL)
			fieldType = ResolveTypeRef(fieldDef->mTypeRef);
	}
	else
	{
		BF_ASSERT(mCompiler->IsAutocomplete());
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
		if ((typeInstance->IsEnum()) && (fieldDef->mTypeRef == NULL))
		{
			fieldType = typeInstance;
		}
		else
		{
			auto isLet = (fieldDef->mTypeRef != NULL) && (fieldDef->mTypeRef->IsExact<BfLetTypeReference>());
			auto isVar = (fieldDef->mTypeRef != NULL) && (fieldDef->mTypeRef->IsExact<BfVarTypeReference>());
			if (isLet || isVar)
				fieldType = GetPrimitiveType(BfTypeCode_Var);
			else
				fieldType = ResolveTypeRef(fieldDef->mTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowInferredSizedArray);
			if (fieldType == NULL)
				fieldType = mContext->mBfObjectType;
		}
	}

	if ((fieldInstance != NULL) && (fieldInstance->mConstIdx != -1) && (!forceResolve))
		return;

	if (mContext->mFieldResolveReentrys.size() > 1)
	{
		if (mContext->mFieldResolveReentrys.IndexOf(fieldInstance, 1) != -1)
		{
			String failStr = "Circular data reference detected between the following fields:";
			for (int i = 1; i < (int)mContext->mFieldResolveReentrys.size(); i++)
			{
				if (i > 1)
					failStr += ",";
				failStr += "\n  '" + TypeToString(typeInstance) + "." + mContext->mFieldResolveReentrys[i]->GetFieldDef()->mName + "'";
			}
			BfError* err = Fail(failStr, fieldDef->mFieldDeclaration);
			if (err)
				err->mIsPersistent = true;
			return;
		}
	}

	mContext->mFieldResolveReentrys.push_back(fieldInstance);
	AutoPopBack<decltype (mContext->mFieldResolveReentrys)> popTypeResolveReentry(&mContext->mFieldResolveReentrys);
	if (fieldInstance == NULL)
		popTypeResolveReentry.Pop();

	auto typeDef = typeInstance->mTypeDef;

	BfIRValue constValue;

	if (fieldDef->mIsExtern)
	{
		if (!fieldDef->mTypeRef->IsA<BfPointerTypeRef>())
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
			Fail("Extern consts must be pointer types", fieldDef->mTypeRef);
		}

		if (fieldDef->GetInitializer() != NULL)
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
			Fail("Extern consts cannot have initializers", fieldDef->GetNameNode());
		}
	}
	else if (fieldDef->GetInitializer() == NULL)
	{
		if (fieldDef->IsEnumCaseEntry())
		{
			if (typeInstance->IsTypedPrimitive())
			{
				BfFieldInstance* prevFieldInstance = NULL;
				for (int i = fieldDef->mIdx - 1; i >= 0; i--)
				{
					BfFieldInstance* checkFieldInst = &typeInstance->mFieldInstances[i];
					if (checkFieldInst->GetFieldDef()->IsEnumCaseEntry())
					{
						prevFieldInstance = checkFieldInst;
						break;
					}
				}

				if (prevFieldInstance == NULL)
					constValue = GetConstValue(0, typeInstance);
				else
				{
					ResolveConstField(typeInstance, prevFieldInstance, prevFieldInstance->GetFieldDef());
					if (prevFieldInstance->mConstIdx == -1)
					{
						if (!mCompiler->IsAutocomplete())
							AssertErrorState();
						constValue = GetConstValue(0, typeInstance);
					}
					else
					{
						auto prevConstant = ConstantToCurrent(typeInstance->mConstHolder->GetConstantById(prevFieldInstance->mConstIdx), typeInstance->mConstHolder, typeInstance);
						constValue = mBfIRBuilder->CreateAdd(prevConstant, GetConstValue(1, typeInstance));
					}
				}
			}
		}
		else
		{
			Fail("Const requires initializer", fieldDef->GetNameNode());
		}
	}
	else if (mBfIRBuilder != NULL)
	{
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);

		SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

		BfMethodState methodState;
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
		methodState.mTempKind = BfMethodState::TempKind_Static;

		auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();

		if ((fieldType->IsVar()) || (fieldType->IsUndefSizedArray()))
		{
			auto initValue = GetFieldInitializerValue(fieldInstance, fieldDef->GetInitializer(), fieldDef, fieldType);
			if (!initValue)
			{
				AssertErrorState();
				initValue = GetDefaultTypedValue(mContext->mBfObjectType);
			}
			if (fieldInstance != NULL)
			{
				if (fieldType->IsUndefSizedArray())
				{
					if ((initValue.mType->IsSizedArray()) && (initValue.mType->GetUnderlyingType() == fieldType->GetUnderlyingType()))
					{
						fieldInstance->mResolvedType = initValue.mType;
					}
				}
				else
					fieldInstance->mResolvedType = initValue.mType;
			}
			constValue = initValue.mValue;
		}
		else
		{
			auto uncastedInitValue = GetFieldInitializerValue(fieldInstance, fieldDef->GetInitializer(), fieldDef, fieldType);
			constValue = uncastedInitValue.mValue;
		}

		if ((mCurTypeInstance->IsPayloadEnum()) && (fieldDef->IsEnumCaseEntry()))
		{
			// Illegal
			constValue = BfIRValue();
		}

		mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	}

	if (fieldInstance != NULL)
	{
		fieldType = fieldInstance->GetResolvedType();
		if ((fieldType == NULL) || (fieldType->IsVar()))
		{
			SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
			AssertErrorState();
			// Default const type is 'int'
			BfTypedValue initValue = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
			if (fieldInstance != NULL)
			{
				fieldInstance->mResolvedType = initValue.mType;
			}
			constValue = initValue.mValue;
		}

		if ((constValue) && (fieldInstance->mConstIdx == -1))
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
			CurrentAddToConstHolder(constValue);
			fieldInstance->mConstIdx = constValue.mId;
		}
	}
}

BfType* BfModule::ResolveVarFieldType(BfTypeInstance* typeInstance, BfFieldInstance* fieldInstance, BfFieldDef* field)
{
	bool isDeclType = (field->mFieldDeclaration != NULL) && BfNodeDynCastExact<BfExprModTypeRef>(field->mTypeRef) != NULL;

	auto fieldType = fieldInstance->GetResolvedType();
	if ((field->mIsConst) && (!isDeclType))
	{
		ResolveConstField(typeInstance, fieldInstance, field);
		return fieldInstance->GetResolvedType();
	}

	bool staticOnly = (field->mIsStatic) && (!isDeclType);

	if (!fieldInstance->mIsInferredType)
		return fieldType;
	if ((!fieldType->IsVar()) && (!fieldType->IsUndefSizedArray()))
		return fieldType;

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);

	BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState, mContext->mCurTypeState->mType != typeInstance);

	auto typeDef = typeInstance->mTypeDef;

	if ((!field->mIsStatic) && (typeDef->mIsStatic))
	{
		AssertErrorState();
		return fieldType;
	}

	bool hadInferenceCycle = false;
	if (mContext->mFieldResolveReentrys.size() > 1)
	{
		if (mContext->mFieldResolveReentrys.IndexOf(fieldInstance, 1) != -1)
		{
			for (int i = 1; i < (int)mContext->mFieldResolveReentrys.size(); i++)
			{
				auto fieldInst = mContext->mFieldResolveReentrys[i];
				auto fieldDef = fieldInst->GetFieldDef();
				auto fieldOwner = fieldInst->mOwner;

				auto fieldModule = fieldOwner->mModule;
				SetAndRestoreValue<bool> prevIgnoreError(fieldModule->mIgnoreErrors, false);
				fieldModule->Fail(StrFormat("Field '%s.%s' creates a type inference cycle", TypeToString(fieldOwner).c_str(), fieldDef->mName.c_str()), fieldDef->mTypeRef, true);
			}

			return fieldType;
		}
	}
	mContext->mFieldResolveReentrys.push_back(fieldInstance);

	AutoPopBack<decltype (mContext->mFieldResolveReentrys)> popTypeResolveReentry(&mContext->mFieldResolveReentrys);
	SetAndRestoreValue<bool> prevResolvingVar(typeInstance->mResolvingVarField, true);
	SetAndRestoreValue<bool> prevCtxResolvingVar(mContext->mResolvingVarField, true);

	if ((field->GetInitializer() == NULL) && (!isDeclType))
	{
		if ((field->mTypeRef->IsA<BfVarTypeReference>()) || (field->mTypeRef->IsA<BfLetTypeReference>()))
			Fail("Implicitly-typed fields must be initialized", field->GetRefNode());
		return fieldType;
	}

	BfType* resolvedType = NULL;
	if (!hadInferenceCycle)
	{
		BfTypeState typeState;
		typeState.mPrevState = mContext->mCurTypeState;
		typeState.mType = typeInstance;
		typeState.mCurFieldDef = field;
		typeState.mResolveKind = BfTypeState::ResolveKind_ResolvingVarType;
		SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

		SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL/*ctorMethod.mMethodInstance*/);

		auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();

		BfMethodState methodState;
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
		methodState.mTempKind = staticOnly ? BfMethodState::TempKind_Static : BfMethodState::TempKind_NonStatic;
		if (!staticOnly)
		{
			//BfLocalVariable localVar;
			//methodState.mLocals.push_back(localVar);
		}

		if (isDeclType)
		{
			auto fieldDef = fieldInstance->GetFieldDef();
			resolvedType = ResolveTypeRef(fieldDef->mTypeRef, BfPopulateType_Identity);
		}
		else
		{
			BfTypedValue valueFromExpr;
			valueFromExpr = GetFieldInitializerValue(fieldInstance);
			FixIntUnknown(valueFromExpr);

			if (fieldType->IsUndefSizedArray())
			{
				if ((valueFromExpr.mType != NULL) && (valueFromExpr.mType->IsSizedArray()) && (valueFromExpr.mType->GetUnderlyingType() == fieldType->GetUnderlyingType()))
					resolvedType = valueFromExpr.mType;
			}
			else
				resolvedType = valueFromExpr.mType;
		}

		mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	}

	if (resolvedType == NULL)
		return fieldType;

	fieldInstance->SetResolvedType(resolvedType);

	if (field->mIsStatic)
	{
	}
	else if (fieldInstance->mDataIdx >= 0)
	{
	}
	else
	{
		BF_ASSERT(typeInstance->IsIncomplete());
	}

	return resolvedType;
}

void BfModule::MarkFieldInitialized(BfFieldInstance* fieldInstance)
{
	BF_ASSERT(fieldInstance->mOwner == mCurTypeInstance);

	auto fieldType = fieldInstance->GetResolvedType();
	if ((fieldInstance->mMergedDataIdx != -1) && (mCurMethodState != NULL))
	{
		int fieldIdx = 0;
		int fieldCount = 0;
		fieldInstance->GetDataRange(fieldIdx, fieldCount);
		fieldIdx--;

		int count = fieldCount;
		if (fieldIdx == -1)
			count = 1;

		//TODO: Under what circumstances could 'thisVariable' be NULL?
		auto thisVariable = GetThisVariable();
		if (thisVariable != NULL)
		{
			for (int i = 0; i < count; i++)
				mCurMethodState->LocalDefined(thisVariable, fieldIdx + i);
		}
	}
}

bool BfModule::IsThreadLocal(BfFieldInstance * fieldInstance)
{
	if (fieldInstance->mCustomAttributes == NULL)
		return false;
	auto fieldDef = fieldInstance->GetFieldDef();
	if (!fieldDef->mIsStatic)
		return false;

	bool isThreadLocal = false;
	if (fieldInstance->mCustomAttributes != NULL)
	{
		for (auto customAttr : fieldInstance->mCustomAttributes->mAttributes)
		{
			if (customAttr.mType->ToTypeInstance()->IsInstanceOf(mCompiler->mThreadStaticAttributeTypeDef))
				return true;
		}
	}
	return false;
}

BfTypedValue BfModule::GetFieldInitializerValue(BfFieldInstance* fieldInstance, BfExpression* initializer, BfFieldDef* fieldDef, BfType* fieldType, bool doStore)
{
	if (fieldDef == NULL)
		fieldDef = fieldInstance->GetFieldDef();
	if (fieldType == NULL)
		fieldType = fieldInstance->GetResolvedType();
	if (initializer == NULL)
	{
		if (fieldDef == NULL)
			return BfTypedValue();
		initializer = fieldDef->GetInitializer();
	}

	BfTypedValue staticVarRef;

	BfTypedValue result;
	if (initializer == NULL)
	{
		result = GetDefaultTypedValue(fieldInstance->mResolvedType);
	}
	else
	{
		if (mCompiler->mIsResolveOnly)
		{
			if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(initializer))
			{
				sourceClassifier->SetElementType(initializer, BfSourceElementType_Normal);
				sourceClassifier->VisitChildNoRef(initializer);
			}
		}

		if ((mCurTypeInstance->IsPayloadEnum()) && (fieldDef->IsEnumCaseEntry()))
		{
			Fail("Enums with payloads cannot have explicitly-defined discriminators", initializer);
			BfConstResolver constResolver(this);
			constResolver.Resolve(initializer);
			return GetDefaultTypedValue(fieldType);
		}
		else if (mCurTypeInstance->IsUnspecializedTypeVariation())
		{
			return GetDefaultTypedValue(fieldType);
		}
		else if (fieldDef->mIsConst)
		{
			int ceExecuteId = -1;
			if (mCompiler->mCeMachine != NULL)
				ceExecuteId = mCompiler->mCeMachine->mExecuteId;

			BfTypeState typeState;
			typeState.mType = mCurTypeInstance;
			typeState.mCurTypeDef = fieldDef->mDeclaringType;
			typeState.mCurFieldDef = fieldDef;
			SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

			BfConstResolver constResolver(this);
			if (fieldType->IsVar())
				return constResolver.Resolve(initializer);
			else
			{
				BfConstResolveFlags resolveFlags = BfConstResolveFlag_None;
				if ((mCompiler->mResolvePassData != NULL) && (mCurTypeInstance->IsEnum()) && (fieldDef->IsEnumCaseEntry()) &&
					(mCompiler->mResolvePassData->mAutoCompleteTempTypes.Contains(fieldDef->mDeclaringType)))
				{
					// We avoid doing the cast here because the value we're typing may not be in the range of the current
					//  auto-created underlying type and it will cause an 'error flash'. We defer errors until the full resolve for that purpose
					resolveFlags = BfConstResolveFlag_NoCast;
				}
				UpdateSrcPos(initializer);
				auto result = constResolver.Resolve(initializer, fieldType, resolveFlags);
				if ((mCompiler->mCeMachine != NULL) && (fieldInstance != NULL))
				{
					if (mCompiler->mCeMachine->mExecuteId != ceExecuteId)
						fieldInstance->mHadConstEval = true;
				}
				return result;
			}
		}

		BfExprEvaluator exprEvaluator(this);
		if (doStore)
		{
			staticVarRef = ReferenceStaticField(fieldInstance);
			exprEvaluator.mReceivingValue = &staticVarRef;
		}
		if (fieldType->IsVar())
			result = CreateValueFromExpression(exprEvaluator, initializer, NULL, (BfEvalExprFlags)(BfEvalExprFlags_NoValueAddr | BfEvalExprFlags_FieldInitializer));
		else
			result = CreateValueFromExpression(exprEvaluator, initializer, fieldType, (BfEvalExprFlags)(BfEvalExprFlags_NoValueAddr | BfEvalExprFlags_FieldInitializer));
		if (doStore)
		{
			if (exprEvaluator.mReceivingValue == NULL)
				doStore = false; // Already stored
		}
	}

	if (fieldInstance != NULL)
		MarkFieldInitialized(fieldInstance);

	if ((doStore) && (result))
	{
		if (fieldInstance->mResolvedType->IsUndefSizedArray())
		{
			if ((!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mFastFinish))
				AssertErrorState();
		}
		else
		{
			result = LoadValue(result);
			if (!result.mType->IsValuelessType())
				mBfIRBuilder->CreateAlignedStore(result.mValue, staticVarRef.mValue, result.mType->mAlign);
		}
	}

	return result;
}

void BfModule::AppendedObjectInit(BfFieldInstance* fieldInst)
{
	BfExprEvaluator exprEvaluator(this);

	bool failed = false;

	auto fieldDef = fieldInst->GetFieldDef();
	auto initializer = fieldDef->GetInitializer();

	BfResolvedArgs resolvedArgs;
	if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(initializer))
	{
		bool isDot = false;

		if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(invocationExpr->mTarget))
			isDot = (memberRefExpr->mTarget == NULL) && (memberRefExpr->mMemberName == NULL);

		if (!isDot)
		{
			auto resolvedType = ResolveTypeRef(invocationExpr->mTarget, {});
			if ((resolvedType == NULL) || (resolvedType != fieldInst->mResolvedType))
				failed = true;
		}

		SetAndRestoreValue<BfType*> prevExpectingType(exprEvaluator.mExpectingType, fieldInst->mResolvedType);

		resolvedArgs.Init(invocationExpr->mOpenParen, &invocationExpr->mArguments, &invocationExpr->mCommas, invocationExpr->mCloseParen);
		exprEvaluator.ResolveArgValues(resolvedArgs, BfResolveArgsFlag_DeferParamEval);
	}
	else if (initializer != NULL)
	{
		GetFieldInitializerValue(fieldInst);
		failed = true;
	}

	if (failed)
		Fail("Append fields can only be initialized with a call to their constructor", initializer);

	auto intType = GetPrimitiveType(BfTypeCode_IntPtr);
	auto int8Type = mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8);
	auto ptrType = mBfIRBuilder->GetPointerTo(int8Type);
	auto ptrPtrType = mBfIRBuilder->GetPointerTo(ptrType);

	auto fieldTypeInst = fieldInst->mResolvedType->ToTypeInstance();

	BfIRValue fieldAddr;
	if (fieldDef->mIsStatic)
	{
		fieldAddr = ReferenceStaticField(fieldInst).mValue;
	}
	else
		fieldAddr = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, fieldInst->mDataIdx);
	auto thisValue = BfTypedValue(mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(fieldInst->mResolvedType)), fieldInst->mResolvedType);

	auto indexVal = BfTypedValue(CreateAlloca(intType), CreateRefType(intType));
	auto intThisVal = mBfIRBuilder->CreatePtrToInt(thisValue.mValue, (intType->mSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64);
	auto curValPtr = mBfIRBuilder->CreateAdd(intThisVal, GetConstValue(fieldTypeInst->mInstSize, intType));
	mBfIRBuilder->CreateStore(curValPtr, indexVal.mValue);

	auto vObjectAddr = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, 0);

	auto vDataRef = CreateClassVDataGlobal(fieldInst->mResolvedType->ToTypeInstance());

	auto destAddr = mBfIRBuilder->CreateBitCast(vObjectAddr, ptrPtrType);
	auto srcVal = mBfIRBuilder->CreateBitCast(vDataRef, ptrType);
	mBfIRBuilder->CreateStore(srcVal, destAddr);

	if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
	{
		auto int8Type = mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8);
		auto ptrType = mBfIRBuilder->GetPointerTo(int8Type);

		auto thisFlagsPtr = mBfIRBuilder->CreateBitCast(thisValue.mValue, ptrType);
		mBfIRBuilder->CreateStore(GetConstValue8(BfObjectFlag_AppendAlloc), thisFlagsPtr);
	}

	exprEvaluator.MatchConstructor(fieldDef->GetNameNode(), NULL, thisValue, fieldInst->mResolvedType->ToTypeInstance(), resolvedArgs, false, true, &indexVal);
}

void BfModule::CheckInterfaceMethod(BfMethodInstance* methodInstance)
{
}

void BfModule::FindSubTypes(BfTypeInstance* classType, SizedArrayImpl<int>* outVals, SizedArrayImpl<BfTypeInstance*>* exChecks, bool isInterfacePass)
{
	PopulateType(classType);

	if (isInterfacePass)
	{
		for (auto ifaceInst : classType->mInterfaces)
		{
			if (exChecks->Contains(ifaceInst.mInterfaceType))
				continue;
			if (outVals->Contains(ifaceInst.mInterfaceType->mTypeId))
				continue;

			if (ifaceInst.mDeclaringType->IsExtension())
			{
				bool needsExCheck = false;
				if (ifaceInst.mDeclaringType->mProject != classType->mTypeDef->mProject)
				{
					PopulateType(ifaceInst.mInterfaceType, BfPopulateType_DataAndMethods);
					if (ifaceInst.mInterfaceType->mVirtualMethodTableSize > 0)
					{
						exChecks->push_back(ifaceInst.mInterfaceType);
						continue;
					}
					else
					{
						// We can only do an 'exCheck' if we're actually going to slot this interface
					}
				}
			}

			outVals->push_back(ifaceInst.mInterfaceType->mTypeId);
		}
	}
	else
	{
		outVals->push_back(classType->mTypeId);
	}

	if (classType->mBaseType != NULL)
	{
		FindSubTypes(classType->mBaseType, outVals, exChecks, isInterfacePass);
	}
}

void BfModule::CreateDynamicCastMethod()
{
	auto objType = mContext->mBfObjectType;

	if ((mCompiler->mIsResolveOnly) || (!mIsReified))
	{
		// The main reason to punt on this method for ResolveOnly is because types can be created
		//  and destroyed quickly during autocomplete and the typeId creep can generate lots of
		//  entries in the LLVM ConstantInt pool, primarily from the FindSubTypes call.  We can
		//  remove this punt when we recycle typeId's
		mBfIRBuilder->CreateRet(mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapTypeInstPtr(objType)));
		mCurMethodState->mHadReturn = true;
		return;
	}

	bool isInterfacePass = mCurMethodInstance->mMethodDef->mName == BF_METHODNAME_DYNAMICCAST_INTERFACE;

	auto func = mCurMethodState->mIRFunction;
	auto thisValue = mBfIRBuilder->GetArgument(0);
	auto typeIdValue = mBfIRBuilder->GetArgument(1);

	auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
	auto int32Type = GetPrimitiveType(BfTypeCode_Int32);
	typeIdValue = CastToValue(NULL, BfTypedValue(typeIdValue, intPtrType), int32Type, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail));

	auto thisObject = mBfIRBuilder->CreateBitCast(thisValue, mBfIRBuilder->MapType(objType));

	auto trueBB = mBfIRBuilder->CreateBlock("check.true");
	//auto falseBB = mBfIRBuilder->CreateBlock("check.false");
	auto exitBB = mBfIRBuilder->CreateBlock("exit");

	SizedArray<int, 8> typeMatches;
	SizedArray<BfTypeInstance*, 8> exChecks;
	FindSubTypes(mCurTypeInstance, &typeMatches, &exChecks, isInterfacePass);

	if ((mCurTypeInstance->IsGenericTypeInstance()) && (!mCurTypeInstance->IsUnspecializedType()))
	{
		// Add 'unbound' type id to cast list so things like "List<int> is List<>" work
		auto genericTypeInst = mCurTypeInstance->mTypeDef;
		BfTypeVector genericArgs;
		for (int i = 0; i < (int) genericTypeInst->mGenericParamDefs.size(); i++)
			genericArgs.push_back(GetGenericParamType(BfGenericParamKind_Type, i));
		auto unboundType = ResolveTypeDef(mCurTypeInstance->mTypeDef->GetDefinition(), genericArgs, BfPopulateType_Declaration);
		typeMatches.push_back(unboundType->mTypeId);
	}

	if (mCurTypeInstance->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)mCurTypeInstance;
		BfTypeInstance* innerType = boxedType->mElementType->ToTypeInstance();

		FindSubTypes(innerType, &typeMatches, &exChecks, isInterfacePass);

		if (innerType->IsTypedPrimitive())
		{
			auto underlyingType = innerType->GetUnderlyingType();
			typeMatches.push_back(underlyingType->mTypeId);
		}

		auto innerTypeInst = innerType->ToTypeInstance();
		if ((innerTypeInst->IsInstanceOf(mCompiler->mSizedArrayTypeDef)) ||
			(innerTypeInst->IsInstanceOf(mCompiler->mPointerTTypeDef)) ||
			(innerTypeInst->IsInstanceOf(mCompiler->mMethodRefTypeDef)))
		{
			PopulateType(innerTypeInst);
			//TODO: What case was this supposed to handle?
			//typeMatches.push_back(innerTypeInst->mFieldInstances[0].mResolvedType->mTypeId);
		}
	}

	auto curBlock = mBfIRBuilder->GetInsertBlock();

	BfIRValue vDataPtr;
	if (!exChecks.empty())
	{
		BfType* intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
		auto ptrPtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(intPtrType)));
		auto vDataPtrPtr = mBfIRBuilder->CreateBitCast(thisValue, ptrPtrType);
		vDataPtr = FixClassVData(mBfIRBuilder->CreateLoad(vDataPtrPtr/*, "vtable"*/));
	}

	auto switchStatement = mBfIRBuilder->CreateSwitch(typeIdValue, exitBB, (int)typeMatches.size() + (int)exChecks.size());
	for (auto typeMatch : typeMatches)
		mBfIRBuilder->AddSwitchCase(switchStatement, GetConstValue32(typeMatch), trueBB);

	Array<BfIRValue> incomingFalses;
	for (auto ifaceTypeInst : exChecks)
	{
		BfIRBlock nextBB = mBfIRBuilder->CreateBlock("exCheck", true);
		mBfIRBuilder->AddSwitchCase(switchStatement, GetConstValue32(ifaceTypeInst->mTypeId), nextBB);
		mBfIRBuilder->SetInsertPoint(nextBB);

		BfIRValue slotOfs = GetInterfaceSlotNum(ifaceTypeInst);

		auto ifacePtrPtr = mBfIRBuilder->CreateInBoundsGEP(vDataPtr, slotOfs/*, "iface"*/);
		auto ifacePtr = mBfIRBuilder->CreateLoad(ifacePtrPtr);

		auto cmpResult = mBfIRBuilder->CreateCmpNE(ifacePtr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0));
		mBfIRBuilder->CreateCondBr(cmpResult, trueBB, exitBB);

		incomingFalses.push_back(nextBB);
	}

	mBfIRBuilder->AddBlock(trueBB);
	mBfIRBuilder->SetInsertPoint(trueBB);
	mBfIRBuilder->CreateBr(exitBB);

	mBfIRBuilder->AddBlock(exitBB);
	mBfIRBuilder->SetInsertPoint(exitBB);
	auto phi = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapTypeInstPtr(objType), 2);
	mBfIRBuilder->AddPhiIncoming(phi, thisObject, trueBB);
	auto nullValue = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapTypeInstPtr(objType));
	for (auto incomingFalseBlock : incomingFalses)
		mBfIRBuilder->AddPhiIncoming(phi, nullValue, incomingFalseBlock);
	mBfIRBuilder->AddPhiIncoming(phi, nullValue, curBlock);
	mBfIRBuilder->CreateRet(phi);
	mCurMethodState->mHadReturn = true;
}

void BfModule::EmitEquals(BfTypedValue leftValue, BfTypedValue rightValue, BfIRBlock exitBB, bool strictEquals)
{
	BfExprEvaluator exprEvaluator(this);
	exprEvaluator.mExpectingType = mCurMethodInstance->mReturnType;

	auto typeInst = rightValue.mType->ToTypeInstance();
	exprEvaluator.PerformBinaryOperation((BfAstNode*)NULL, (BfAstNode*)NULL, strictEquals ? BfBinaryOp_StrictEquality : BfBinaryOp_Equality, NULL, BfBinOpFlag_IgnoreOperatorWithWrongResult, leftValue, rightValue);
	BfTypedValue result = exprEvaluator.GetResult();
	if (result.mType != GetPrimitiveType(BfTypeCode_Boolean))
	{
		// Fail?
	}
	if ((result) && (!result.mType->IsVar()))
	{
		auto nextBB = mBfIRBuilder->CreateBlock("next");
		mBfIRBuilder->CreateCondBr(result.mValue, nextBB, exitBB);
		mBfIRBuilder->AddBlock(nextBB);
		mBfIRBuilder->SetInsertPoint(nextBB);
	}
}

void BfModule::CreateFakeCallerMethod(const String& funcName)
{
	if (mCurMethodInstance->mHasFailed)
		return;
	if (mCurMethodInstance->mMethodDef->mIsSkipCall)
		return;

	BF_ASSERT(mCurMethodInstance->mIRFunction);

	auto voidType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_None));
	SizedArray<BfIRType, 4> paramTypes;
	BfIRFunctionType funcType = mBfIRBuilder->CreateFunctionType(voidType, paramTypes);
	BfIRFunction func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_Internal, "FORCELINK_" + funcName);
	mBfIRBuilder->SetActiveFunction(func);
	auto entryBlock = mBfIRBuilder->CreateBlock("main", true);
	mBfIRBuilder->SetInsertPoint(entryBlock);

	BfMethodState methodState;
	methodState.mIRHeadBlock = entryBlock;
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);

	SizedArray<BfIRValue, 8> args;
	BfExprEvaluator exprEvaluator(this);

	if (mCurMethodInstance->GetStructRetIdx() == 0)
	{
		auto retPtrType = CreatePointerType(mCurMethodInstance->mReturnType);
		exprEvaluator.PushArg(GetDefaultTypedValue(retPtrType, true, BfDefaultValueKind_Const), args);
	}

	if (mCurMethodInstance->HasThis())
	{
		auto thisValue = GetDefaultTypedValue(mCurMethodInstance->GetOwner(), true, mCurTypeInstance->IsComposite() ? BfDefaultValueKind_Addr : BfDefaultValueKind_Const);
		exprEvaluator.PushThis(NULL, thisValue, mCurMethodInstance, args);
	}

	if (mCurMethodInstance->GetStructRetIdx() == 1)
	{
		auto retPtrType = CreatePointerType(mCurMethodInstance->mReturnType);
		exprEvaluator.PushArg(GetDefaultTypedValue(retPtrType, true, BfDefaultValueKind_Const), args);
	}

	for (int paramIdx = 0; paramIdx < mCurMethodInstance->GetParamCount(); paramIdx++)
	{
		auto paramType = mCurMethodInstance->GetParamType(paramIdx);
		if (paramType->IsValuelessType())
			continue;
		exprEvaluator.PushArg(GetDefaultTypedValue(paramType, true, paramType->IsComposite() ? BfDefaultValueKind_Addr : BfDefaultValueKind_Const), args);
	}

	mBfIRBuilder->CreateCall(mCurMethodInstance->mIRFunction, args);
	mBfIRBuilder->CreateRetVoid();
}

void BfModule::CreateDelegateEqualsMethod()
{
	if (mBfIRBuilder->mIgnoreWrites)
		return;

	auto refNode = mCurTypeInstance->mTypeDef->GetRefNode();
	if (refNode == NULL)
		refNode = mCompiler->mValueTypeTypeDef->GetRefNode();
	UpdateSrcPos(refNode);
	SetIllegalSrcPos();

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
	auto resultVal = CreateAlloca(boolType);
	mBfIRBuilder->CreateStore(GetConstValue(0, boolType), resultVal);

	auto exitBB = mBfIRBuilder->CreateBlock("exit");

	auto delegateType = ResolveTypeDef(mCompiler->mDelegateTypeDef)->ToTypeInstance();
	mBfIRBuilder->PopulateType(delegateType);

	BfExprEvaluator exprEvaluator(this);
	BfTypedValue leftTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]);
	BfTypedValue lhsDelegate = BfTypedValue(mBfIRBuilder->CreateBitCast(leftTypedVal.mValue, mBfIRBuilder->MapType(delegateType)), delegateType);

	BfTypedValue rhsDelegate = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);
	rhsDelegate = LoadValue(rhsDelegate);
	BfTypedValue rightTypedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(rhsDelegate.mValue, mBfIRBuilder->MapType(mCurTypeInstance)), mCurTypeInstance);

	auto& targetFieldInstance = delegateType->mFieldInstances[0];

	BfTypedValue leftValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(lhsDelegate.mValue, 0, targetFieldInstance.mDataIdx), targetFieldInstance.mResolvedType, true);
	BfTypedValue rightValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(rhsDelegate.mValue, 0, targetFieldInstance.mDataIdx), targetFieldInstance.mResolvedType, true);
	leftValue = LoadValue(leftValue);
	rightValue = LoadValue(rightValue);
	EmitEquals(leftValue, rightValue, exitBB, false);

	bool hadComparison = false;
	for (auto& fieldRef : mCurTypeInstance->mFieldInstances)
	{
		BfFieldInstance* fieldInstance = &fieldRef;
		if (fieldInstance->mDataOffset == -1)
			continue;

		auto fieldType = fieldInstance->mResolvedType;
		if (fieldType->IsValuelessType())
			continue;
		if (fieldType->IsVar())
			continue;
		if (fieldType->IsMethodRef())
			continue;

		if (fieldType->IsRef())
			fieldType = CreatePointerType(fieldType->GetUnderlyingType());

		BfTypedValue leftValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(leftTypedVal.mValue, 0, fieldInstance->mDataIdx), fieldType, true);
		BfTypedValue rightValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(rightTypedVal.mValue, 0, fieldInstance->mDataIdx), fieldType, true);

		if (!fieldInstance->mResolvedType->IsComposite())
		{
			leftValue = LoadValue(leftValue);
			rightValue = LoadValue(rightValue);
		}

		EmitEquals(leftValue, rightValue, exitBB, false);
	}

	mBfIRBuilder->CreateStore(GetConstValue(1, boolType), resultVal);
	mBfIRBuilder->CreateBr(exitBB);

	mBfIRBuilder->AddBlock(exitBB);
	mBfIRBuilder->SetInsertPoint(exitBB);

	auto loadedResult = mBfIRBuilder->CreateLoad(resultVal);

	ClearLifetimeEnds();

	mBfIRBuilder->CreateRet(loadedResult);

	mCurMethodState->mHadReturn = true;
}

void BfModule::CreateValueTypeEqualsMethod(bool strictEquals)
{
	if (mCurMethodInstance->mIsUnspecialized)
		return;

	if (mBfIRBuilder->mIgnoreWrites)
		return;

	BF_ASSERT(!mCurTypeInstance->IsBoxed());

	auto compareType = mCurMethodInstance->mParams[0].mResolvedType;
	bool isValid = true;

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
	if (compareType->IsValuelessType())
	{
		mBfIRBuilder->CreateRet(GetDefaultValue(boolType));
		return;
	}

	if (compareType->IsTypedPrimitive())
	{
		BfExprEvaluator exprEvaluator(this);
		BfTypedValue leftTypedVal = LoadValue(exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]));
		BfTypedValue rightTypedVal = LoadValue(exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]));
		auto cmpResult = mBfIRBuilder->CreateCmpEQ(leftTypedVal.mValue, rightTypedVal.mValue);
		mBfIRBuilder->CreateRet(cmpResult);
		return;
	}

	auto compareDType = compareType->ToDependedType();
	BfTypeInstance* compareTypeInst = compareType->ToTypeInstance();
	if (compareTypeInst != NULL)
	{
		if (compareType->IsPrimitiveType())
			compareTypeInst = GetWrappedStructType(compareType);
		if ((compareTypeInst == NULL) || (!compareTypeInst->IsValueType()))
		{
			isValid = false;
		}
		mBfIRBuilder->PopulateType(compareTypeInst);
	}

	if (!isValid)
	{
		ClearLifetimeEnds();
		mBfIRBuilder->CreateRet(GetDefaultValue(boolType));
		return;
	}

	AddDependency(compareDType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);

	auto refNode = mCurTypeInstance->mTypeDef->GetRefNode();
	if (refNode == NULL)
		refNode = mCompiler->mValueTypeTypeDef->GetRefNode();
	UpdateSrcPos(refNode);
	SetIllegalSrcPos();

	auto resultVal = CreateAlloca(boolType);
	mBfIRBuilder->CreateStore(GetConstValue(0, boolType), resultVal);

	auto exitBB = mBfIRBuilder->CreateBlock("exit");

	if (compareType->IsValuelessType())
	{
		// Always equal, nothing to do
	}
	else if (compareType->IsSizedArray())
	{
		auto sizedArrayType = (BfSizedArrayType*)compareType;

		auto _SizedIndex = [&](BfIRValue target, BfIRValue index)
		{
			BfTypedValue result;
			auto indexResult = CreateIndexedValue(sizedArrayType->mElementType, target, index, true);
			result = BfTypedValue(indexResult, sizedArrayType->mElementType, BfTypedValueKind_Addr);

			if (!result.mType->IsValueType())
				result = LoadValue(result);

			return result;
		};

		if (sizedArrayType->mElementCount > 6)
		{
			auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
			auto itr = CreateAlloca(intPtrType);
			mBfIRBuilder->CreateStore(GetDefaultValue(intPtrType), itr);

			auto loopBB = mBfIRBuilder->CreateBlock("loop", true);
			auto bodyBB = mBfIRBuilder->CreateBlock("body", true);
			auto doneBB = mBfIRBuilder->CreateBlock("done", true);
			mBfIRBuilder->CreateBr(loopBB);

			mBfIRBuilder->SetInsertPoint(loopBB);
			auto loadedItr = mBfIRBuilder->CreateLoad(itr);
			auto cmpRes = mBfIRBuilder->CreateCmpGTE(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)sizedArrayType->mElementCount), true);
			mBfIRBuilder->CreateCondBr(cmpRes, doneBB, bodyBB);

			mBfIRBuilder->SetInsertPoint(bodyBB);

			BfTypedValue leftValue = _SizedIndex(mCurMethodState->mLocals[0]->mValue, loadedItr);
			BfTypedValue rightValue = _SizedIndex(mCurMethodState->mLocals[1]->mValue, loadedItr);
			EmitEquals(leftValue, rightValue, exitBB, strictEquals);
			auto incValue = mBfIRBuilder->CreateAdd(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
			mBfIRBuilder->CreateStore(incValue, itr);
			mBfIRBuilder->CreateBr(loopBB);

			mBfIRBuilder->SetInsertPoint(doneBB);
		}
		else
		{
			for (int dataIdx = 0; dataIdx < sizedArrayType->mElementCount; dataIdx++)
			{
				BfTypedValue leftValue = _SizedIndex(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, dataIdx));
				BfTypedValue rightValue = _SizedIndex(mCurMethodState->mLocals[1]->mValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, dataIdx));
				EmitEquals(leftValue, rightValue, exitBB, strictEquals);
			}
		}
	}
	else if (compareType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)compareType;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;

		BfExprEvaluator exprEvaluator(this);
		BfTypedValue leftTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]);
		BfTypedValue rightTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);

		// We only need to compare the 'this' capture.  The rationale is that it's impossible to capture any other non-matching
		//  values for the same method reference -- they will always refer back to the same local variables.
		int dataIdx = methodRefType->GetDataIdxFromParamIdx(-1);
		if (dataIdx != -1)
		{
			bool failed = false;
			BfTypedValue leftValue = exprEvaluator.DoImplicitArgCapture(NULL, methodInstance, methodRefType->GetParamIdxFromDataIdx(dataIdx), failed, BfImplicitParamKind_General, leftTypedVal);
			BfTypedValue rightValue = exprEvaluator.DoImplicitArgCapture(NULL, methodInstance, methodRefType->GetParamIdxFromDataIdx(dataIdx), failed, BfImplicitParamKind_General, rightTypedVal);
			BF_ASSERT(!failed);
			EmitEquals(leftValue, rightValue, exitBB, strictEquals);
		}
	}
	else if (compareTypeInst->IsEnum())
	{
		//auto intType = GetPrimitiveType(BfTypeCode_Int32);

		BfExprEvaluator exprEvaluator(this);
		BfTypedValue leftTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]);
		BfTypedValue rightTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);

		auto dscrType = compareTypeInst->GetDiscriminatorType();

		BfTypedValue leftValue = ExtractValue(leftTypedVal, NULL, 2);
		leftValue = LoadValue(leftValue);
		BfTypedValue rightValue = ExtractValue(rightTypedVal, NULL, 2);
		rightValue = LoadValue(rightValue);

		BfTypedValue leftPayload = ExtractValue(leftTypedVal, NULL, 1);
		BfTypedValue rightPayload = ExtractValue(rightTypedVal, NULL, 1);

		EmitEquals(leftValue, rightValue, exitBB, strictEquals);

		int enumCount = 0;
		for (auto& fieldRef : compareTypeInst->mFieldInstances)
		{
			BfFieldInstance* fieldInstance = &fieldRef;
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			if ((fieldDef != NULL) && (fieldDef->IsEnumCaseEntry()))
			{
				enumCount = -fieldInstance->mDataIdx;
			}
		}

		if (enumCount > 0)
		{
			BfIRBlock matchedBlock = mBfIRBuilder->CreateBlock("matched");

			auto switchVal = mBfIRBuilder->CreateSwitch(leftValue.mValue, exitBB, enumCount);
			for (auto& fieldRef : compareTypeInst->mFieldInstances)
			{
				BfFieldInstance* fieldInstance = &fieldRef;
				BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
				if ((fieldDef != NULL) && (fieldDef->IsEnumCaseEntry()))
				{
					int enumIdx = -fieldInstance->mDataIdx - 1;
					if (fieldInstance->mResolvedType->mSize == 0)
					{
						mBfIRBuilder->AddSwitchCase(switchVal, mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, enumIdx), matchedBlock);
					}
					else
					{
						auto caseBlock = mBfIRBuilder->CreateBlock("case", true);
						mBfIRBuilder->SetInsertPoint(caseBlock);

						auto tuplePtr = CreatePointerType(fieldInstance->mResolvedType);
						BfTypedValue leftTuple;
						BfTypedValue rightTuple;

						if (leftPayload.IsAddr())
						{
							leftTuple = BfTypedValue(mBfIRBuilder->CreateBitCast(leftPayload.mValue, mBfIRBuilder->MapType(tuplePtr)), fieldInstance->mResolvedType, true);
							rightTuple = BfTypedValue(mBfIRBuilder->CreateBitCast(rightPayload.mValue, mBfIRBuilder->MapType(tuplePtr)), fieldInstance->mResolvedType, true);
						}
						else
						{
							leftTuple = Cast(NULL, leftPayload, fieldInstance->mResolvedType, BfCastFlags_Force);
							rightTuple = Cast(NULL, rightPayload, fieldInstance->mResolvedType, BfCastFlags_Force);
						}

						EmitEquals(leftTuple, rightTuple, exitBB, strictEquals);
						mBfIRBuilder->CreateBr(matchedBlock);

						mBfIRBuilder->AddSwitchCase(switchVal, mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, enumIdx), caseBlock);
					}
				}
			}

			mBfIRBuilder->AddBlock(matchedBlock);
			mBfIRBuilder->SetInsertPoint(matchedBlock);
		}
	}
	else if (compareTypeInst->IsUnion())
	{
		auto innerType = compareTypeInst->GetUnionInnerType();

		if (!innerType->IsValuelessType())
		{
			BfExprEvaluator exprEvaluator(this);
			BfTypedValue leftTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]);
			leftTypedVal = AggregateSplat(leftTypedVal);
			BfTypedValue leftUnionTypedVal = ExtractValue(leftTypedVal, NULL, 1);

			BfTypedValue rightTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);
			rightTypedVal = AggregateSplat(rightTypedVal);
			BfTypedValue rightUnionTypedVal = ExtractValue(rightTypedVal, NULL, 1);

			EmitEquals(leftUnionTypedVal, rightUnionTypedVal, exitBB, strictEquals);
		}
	}
	else
	{
		BfExprEvaluator exprEvaluator(this);
		BfTypedValue leftTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[0]);
		BfTypedValue rightTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);

		bool hadComparison = false;
		for (auto& fieldRef : compareTypeInst->mFieldInstances)
		{
			BfFieldInstance* fieldInstance = &fieldRef;
			if (fieldInstance->mDataOffset == -1)
				continue;

			if (fieldInstance->mResolvedType->IsValuelessType())
				continue;

			if (fieldInstance->mResolvedType->IsVar())
				continue;

			BfTypedValue leftValue = ExtractValue(leftTypedVal, fieldInstance, fieldInstance->mDataIdx);
			BfTypedValue rightValue = ExtractValue(rightTypedVal, fieldInstance, fieldInstance->mDataIdx);

			if (!fieldInstance->mResolvedType->IsComposite())
			{
				leftValue = LoadValue(leftValue);
				rightValue = LoadValue(rightValue);
			}

			EmitEquals(leftValue, rightValue, exitBB, strictEquals);
		}

		auto baseTypeInst = compareTypeInst->mBaseType;
		if ((baseTypeInst != NULL) && (baseTypeInst->mTypeDef != mCompiler->mValueTypeTypeDef))
		{
			BfTypedValue leftValue = Cast(NULL, leftTypedVal, baseTypeInst);
			BfTypedValue rightValue = Cast(NULL, rightTypedVal, baseTypeInst);
			EmitEquals(leftValue, rightValue, exitBB, strictEquals);
		}
	}

	mBfIRBuilder->CreateStore(GetConstValue(1, boolType), resultVal);
	mBfIRBuilder->CreateBr(exitBB);

	mBfIRBuilder->AddBlock(exitBB);
	mBfIRBuilder->SetInsertPoint(exitBB);

	auto loadedResult = mBfIRBuilder->CreateLoad(resultVal);

	ClearLifetimeEnds();

	mBfIRBuilder->CreateRet(loadedResult);

	mCurMethodState->mHadReturn = true;
}

BfIRValue BfModule::CreateClassVDataGlobal(BfTypeInstance* typeInstance, int* outNumElements, String* outMangledName)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return mBfIRBuilder->GetFakeVal();

	PopulateType(typeInstance, mIsComptimeModule ? BfPopulateType_Data : BfPopulateType_DataAndMethods);

	BfType* classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);

	if (mIsComptimeModule)
	{
		auto idVal = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, typeInstance->mTypeId);
		return mBfIRBuilder->CreateIntToPtr(idVal, mBfIRBuilder->MapType(CreatePointerType(classVDataType)));
	}

	BfIRValue* globalVariablePtr = NULL;
	mClassVDataRefs.TryGetValue(typeInstance, &globalVariablePtr);

	int numElements = 1;

	if ((outNumElements != NULL) || (globalVariablePtr == NULL))
	{
		numElements += mCompiler->mMaxInterfaceSlots;

		auto maxIFaceVirtIdx = 0;

		if (!typeInstance->IsInterface())
		{
			int dynCastDataElems = 1 + mCompiler->mMaxInterfaceSlots;
			numElements += ((dynCastDataElems * 4) + mSystem->mPtrSize - 1) / mSystem->mPtrSize;

			numElements += typeInstance->GetOrigVTableSize();
			int ifaceMethodLen = typeInstance->GetIFaceVMethodSize();
			if (typeInstance->mHotTypeData != NULL)
			{
				if (typeInstance->mHotTypeData->mOrigInterfaceMethodsLength != -1)
					ifaceMethodLen = typeInstance->mHotTypeData->mOrigInterfaceMethodsLength;
			}
			numElements += ifaceMethodLen;
		}

		if (outNumElements != NULL)
			*outNumElements = numElements;
	}

	StringT<128> classVDataName;

	String memberName = "sBfClassVData";
	if ((typeInstance->mHotTypeData != NULL) && ((typeInstance->mHotTypeData->mHadDataChange) || (typeInstance->mHotTypeData->mPendingDataChange)))
	{
		auto curVersion = typeInstance->mHotTypeData->GetLatestVersion();
		memberName += "_";
		memberName += BfTypeUtils::HashEncode64(curVersion->mDataHash.mLow);
		memberName += BfTypeUtils::HashEncode64(curVersion->mDataHash.mHigh);
	}

	BfMangler::MangleStaticFieldName(classVDataName, mCompiler->GetMangleKind(), typeInstance, memberName, classVDataType);
	if (outMangledName != NULL)
		*outMangledName = classVDataName;

	/*if (itr != mClassVDataRefs.end())
	{
		globalVariable = itr->second;
	}*/

	BfIRValue globalVariable;
	if (globalVariablePtr != NULL)
	{
		globalVariable = *globalVariablePtr;
	}
	else
	{
		BfLogSysM("Creating VData %s\n", classVDataName.c_str());
		auto arrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr), numElements);
		globalVariable = mBfIRBuilder->CreateGlobalVariable(
				arrayType,
				true,
				BfIRLinkageType_External,
				BfIRValue(),
				classVDataName);

		mClassVDataRefs[typeInstance] = globalVariable;
	}
	return globalVariable;
}

BfIRValue BfModule::GetClassVDataPtr(BfTypeInstance* typeInstance)
{
	auto classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);
	if (mIsComptimeModule)
		return mBfIRBuilder->Comptime_GetBfType(typeInstance->mTypeId, mBfIRBuilder->MapType(CreatePointerType(classVDataType)));
	return mBfIRBuilder->CreateBitCast(CreateClassVDataGlobal(typeInstance), mBfIRBuilder->MapType(CreatePointerType(classVDataType)));
}

BfIRValue BfModule::CreateClassVDataExtGlobal(BfTypeInstance* declTypeInst, BfTypeInstance* implTypeInst, int startVirtIdx)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return mBfIRBuilder->GetFakeVal();

	int numElements = declTypeInst->GetSelfVTableSize() - declTypeInst->GetOrigSelfVTableSize();
	BF_ASSERT(numElements >= 0);
	if (numElements <= 0)
		return BfIRValue();

	BF_ASSERT(implTypeInst->mVirtualMethodTable[startVirtIdx].mDeclaringMethod.mMethodNum == -1);
	BF_ASSERT(implTypeInst->mVirtualMethodTable[startVirtIdx].mDeclaringMethod.mTypeInstance == declTypeInst);

	if (declTypeInst->mInheritDepth == implTypeInst->mInheritDepth)
		BF_ASSERT(declTypeInst == implTypeInst);
	else
		BF_ASSERT(implTypeInst->mInheritDepth > declTypeInst->mInheritDepth);

	if (declTypeInst != implTypeInst)
	{
		BfTypeInstance* highestImpl = declTypeInst;

		for (int virtIdx = startVirtIdx + 1; virtIdx < (int)declTypeInst->mVirtualMethodTable.size(); virtIdx++)
		{
			if (implTypeInst->mVirtualMethodTable[virtIdx].mDeclaringMethod.mMethodNum == -1)
				break; // Start of an ext entry for another type

			auto checkImplTypeInst = implTypeInst->mVirtualMethodTable[virtIdx].mImplementingMethod.mTypeInstance;
			if ((checkImplTypeInst != NULL) && (checkImplTypeInst->mInheritDepth > highestImpl->mInheritDepth))
				highestImpl = checkImplTypeInst;
		}

		if (highestImpl != implTypeInst)
			return CreateClassVDataExtGlobal(declTypeInst, highestImpl, startVirtIdx);
	}

	BfVDataExtEntry mapEntry;
	mapEntry.mDeclTypeInst = declTypeInst;
	mapEntry.mImplTypeInst = implTypeInst;

	BfIRValue* irValuePtr = NULL;
	if (mClassVDataExtRefs.TryGetValue(mapEntry, &irValuePtr))
		return *irValuePtr;

	PopulateType(declTypeInst, BfPopulateType_DataAndMethods);
	PopulateType(implTypeInst, BfPopulateType_DataAndMethods);
	StringT<512> classVDataName;
	BfMangler::MangleStaticFieldName(classVDataName, mCompiler->GetMangleKind(), implTypeInst, "bf_hs_replace_VDataExt");
	if (declTypeInst != implTypeInst)
	{
		classVDataName += StrFormat("_%d", implTypeInst->mInheritDepth - declTypeInst->mInheritDepth);
	}

	auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
	auto voidPtrIRType = mBfIRBuilder->MapType(voidPtrType);

	SizedArray<BfIRValue, 32> vData;
	auto voidPtrNull = GetDefaultValue(voidPtrType);

	for (int virtIdx = startVirtIdx + declTypeInst->GetOrigSelfVTableSize(); virtIdx < (int)declTypeInst->mVirtualMethodTable.size(); virtIdx++)
	{
		if (implTypeInst->mVirtualMethodTable[virtIdx].mDeclaringMethod.mMethodNum == -1)
			break; // Start of an ext entry for another type

		BfIRValue vValue;
		auto& entry = implTypeInst->mVirtualMethodTable[virtIdx];
		BfMethodInstance* declaringMethodInstance = (BfMethodInstance*)entry.mDeclaringMethod;
		if ((declaringMethodInstance != NULL) && (declaringMethodInstance->mMethodInstanceGroup->IsImplemented()) && (declaringMethodInstance->mIsReified))
		{
			BF_ASSERT(entry.mImplementingMethod.mTypeInstance->mMethodInstanceGroups[entry.mImplementingMethod.mMethodNum].IsImplemented());
			BF_ASSERT(entry.mImplementingMethod.mTypeInstance->mMethodInstanceGroups[entry.mImplementingMethod.mMethodNum].mDefault->mIsReified);
			BfMethodInstance* methodInstance = (BfMethodInstance*)entry.mImplementingMethod;
			if ((methodInstance != NULL) && (!methodInstance->mMethodDef->mIsAbstract))
			{
				BF_ASSERT(implTypeInst->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject));
				auto moduleMethodInst = GetMethodInstanceAtIdx(methodInstance->mMethodInstanceGroup->mOwner, methodInstance->mMethodInstanceGroup->mMethodIdx, NULL, BfGetMethodInstanceFlag_NoInline);
				auto funcPtr = mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, voidPtrIRType);
				vValue = funcPtr;
			}
		}
		if (!vValue)
			vValue = voidPtrNull;
		vData.Add(vValue);
	}

	BF_ASSERT((int)vData.size() == numElements);

	BfLogSysM("Creating VDataExt %s\n", classVDataName.c_str());
	auto arrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr), numElements);

	auto globalVariable = mBfIRBuilder->CreateGlobalVariable(arrayType, true,
			BfIRLinkageType_External, BfIRValue(), classVDataName);

	BfIRType extVTableType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)vData.size());
	BfIRValue extVTableConst = mBfIRBuilder->CreateConstAgg_Value(extVTableType, vData);
	mBfIRBuilder->GlobalVar_SetInitializer(globalVariable, extVTableConst);
	mClassVDataExtRefs[mapEntry] = globalVariable;

	return globalVariable;
}

BfIRValue BfModule::CreateTypeDataRef(BfType* type)
{
	if (mBfIRBuilder->mIgnoreWrites)
	{
		return mBfIRBuilder->CreateTypeOf(type);
	}

	if (mIsComptimeModule)
	{
		auto typeTypeDef = ResolveTypeDef(mCompiler->mTypeTypeDef);
		auto typeTypeInst = typeTypeDef->ToTypeInstance();
		return mBfIRBuilder->Comptime_GetReflectType(type->mTypeId, mBfIRBuilder->MapType(typeTypeInst));
	}

	PopulateType(type);

	BfIRValue globalVariable;

	BfIRValue* globalVariablePtr = NULL;
	if (mTypeDataRefs.TryGetValue(type, &globalVariablePtr))
	{
		if (*globalVariablePtr)
			return mBfIRBuilder->CreateTypeOf(type, *globalVariablePtr);
	}

	auto typeTypeDef = ResolveTypeDef(mCompiler->mTypeTypeDef);
	auto typeTypeInst = typeTypeDef->ToTypeInstance();
	auto typeInstance = type->ToTypeInstance();

	StringT<4096> typeDataName;
	if (typeInstance != NULL)
	{
		BfMangler::MangleStaticFieldName(typeDataName, mCompiler->GetMangleKind(), typeInstance, "sBfTypeData");
	}
	else
	{
		typeDataName += "sBfTypeData.";
		BfMangler::Mangle(typeDataName, mCompiler->GetMangleKind(), type, this);
	}

	BfLogSysM("Creating TypeData %s\n", typeDataName.c_str());

	globalVariable = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(typeTypeInst, BfIRPopulateType_Full), true, BfIRLinkageType_External, BfIRValue(), typeDataName);
	mBfIRBuilder->SetReflectTypeData(mBfIRBuilder->MapType(type), globalVariable);

	mTypeDataRefs[type] = globalVariable;
	return mBfIRBuilder->CreateTypeOf(type, globalVariable);
}

void BfModule::EncodeAttributeData(BfTypeInstance* typeInstance, BfType* argType, BfIRValue arg, SizedArrayImpl<uint8>& data, Dictionary<int, int>& usedStringIdMap)
{
#define PUSH_INT8(val) data.push_back((uint8)val)
#define PUSH_INT16(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); }
#define PUSH_INT32(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); data.push_back((val >> 16) & 0xFF); data.push_back((val >> 24) & 0xFF); }
#define PUSH_INT64(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); data.push_back((val >> 16) & 0xFF); data.push_back((val >> 24) & 0xFF); \
			data.push_back((val >> 32) & 0xFF); data.push_back((val >> 40) & 0xFF); data.push_back((val >> 48) & 0xFF); data.push_back((val >> 56) & 0xFF); }

	auto constant = typeInstance->mConstHolder->GetConstant(arg);
	bool handled = false;

	if (constant == NULL)
	{
		Fail(StrFormat("Unhandled attribute constant data in '%s'", TypeToString(typeInstance).c_str()));
		return;
	}

	if (argType->IsObject())
	{
		if (argType->IsInstanceOf(mCompiler->mStringTypeDef))
		{
			int stringId = constant->mInt32;
			int* orderedIdPtr;
			if (usedStringIdMap.TryAdd(stringId, NULL, &orderedIdPtr))
			{
				*orderedIdPtr = (int)usedStringIdMap.size() - 1;
			}

			GetStringObjectValue(stringId, true, true);
			PUSH_INT8(0xFF); // String
			PUSH_INT32(*orderedIdPtr);
			return;
		}
	}

	if (argType->IsPointer())
	{
		if (argType->GetUnderlyingType() == GetPrimitiveType(BfTypeCode_Char8))
		{
			if (constant->mTypeCode == BfTypeCode_StringId)
			{
				int stringId = constant->mInt32;
				int* orderedIdPtr;
				if (usedStringIdMap.TryAdd(stringId, NULL, &orderedIdPtr))
				{
					*orderedIdPtr = (int)usedStringIdMap.size() - 1;
				}

				GetStringObjectValue(stringId, true, true);
				PUSH_INT8(0xFF); // String
				PUSH_INT32(*orderedIdPtr);
				return;
			}
		}
	}

	PUSH_INT8(constant->mTypeCode);
	if ((constant->mTypeCode == BfTypeCode_Int64) ||
		(constant->mTypeCode == BfTypeCode_UInt64) ||
		(constant->mTypeCode == BfTypeCode_Double))
	{
		PUSH_INT64(constant->mInt64);
	}
	else if ((constant->mTypeCode == BfTypeCode_Int32) ||
		(constant->mTypeCode == BfTypeCode_UInt32) ||
		(constant->mTypeCode == BfTypeCode_Char32))
	{
		PUSH_INT32(constant->mInt32);
	}
	else if (constant->mTypeCode == BfTypeCode_Float)
	{
		float val = (float)constant->mDouble;
		PUSH_INT32(*(int*)&val);
	}
	else if ((constant->mTypeCode == BfTypeCode_Int16) ||
		(constant->mTypeCode == BfTypeCode_UInt16) ||
		(constant->mTypeCode == BfTypeCode_Char16))
	{
		PUSH_INT16(constant->mInt16);
	}
	else if ((constant->mTypeCode == BfTypeCode_Int8) ||
		(constant->mTypeCode == BfTypeCode_UInt8) ||
		(constant->mTypeCode == BfTypeCode_Boolean) ||
		(constant->mTypeCode == BfTypeCode_Char8))
	{
		PUSH_INT8(constant->mInt8);
	}
	else if (constant->mConstType == BfConstType_TypeOf)
	{
		auto typeOf = (BfTypeOf_Const*)constant;
		PUSH_INT32(typeOf->mType->mTypeId);
	}
	else if (constant->mConstType == BfConstType_AggZero)
	{
		for (int i = 0; i < argType->mSize; i++)
			data.Add(0);
	}
// 	else if (constant->mConstType == BfConstType_Agg)
// 	{
// 		BF_ASSERT(argType->IsComposite());
// 		if (argType->IsSizedArray())
// 		{
// 			auto argSizedArrayType = (BfSizedArrayType*)argType;
// 		}
// 		else
// 		{
// 			auto argTypeInstance = argType->ToTypeInstance();
// 		}
// 	}
	else
	{
		Fail(StrFormat("Unhandled attribute constant data in '%s'", TypeToString(typeInstance).c_str()));
	}
}

BfIRValue BfModule::CreateFieldData(BfFieldInstance* fieldInstance, int customAttrIdx)
{
	bool isComptimeArg = mBfIRBuilder->mIgnoreWrites;
	BfFieldDef* fieldDef = fieldInstance->GetFieldDef();

	auto typeInstance = fieldInstance->mOwner;

	BfType* intType = GetPrimitiveType(BfTypeCode_Int32);
	BfType* intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
	BfType* shortType = GetPrimitiveType(BfTypeCode_Int16);
	BfType* typeIdType = intType;

	BfTypeInstance* reflectFieldDataType = ResolveTypeDef(mCompiler->mReflectFieldDataDef)->ToTypeInstance();
	BfIRValue emptyValueType = mBfIRBuilder->mIgnoreWrites ?
		mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapTypeInst(reflectFieldDataType->mBaseType), SizedArray<BfIRValue, 1>()) :
		mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType->mBaseType), SizedArray<BfIRValue, 1>());
	BfIRValue fieldNameConst = GetStringObjectValue(fieldDef->mName, !mIsComptimeModule);
	bool is32Bit = mCompiler->mSystem->mPtrSize == 4;

	int typeId = 0;
	auto fieldType = fieldInstance->GetResolvedType();
	if (fieldType->IsGenericParam())
	{
		//TODO:
	}
	else
		typeId = fieldType->mTypeId;

	BfFieldFlags fieldFlags = (BfFieldFlags)0;

	if (fieldDef->mProtection == BfProtection_Protected)
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_Protected);
	if (fieldDef->mProtection == BfProtection_Public)
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_Public);
	if (fieldDef->mIsStatic)
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_Static);
	if (fieldDef->mIsConst)
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_Const);
	if (fieldDef->IsEnumCaseEntry())
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_EnumCase);
	if (fieldDef->mIsReadOnly)
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_ReadOnly);
	if (fieldInstance->IsAppendedObject())
		fieldFlags = (BfFieldFlags)(fieldFlags | BfFieldFlags_Appended);

	BfIRValue constValue;
	BfIRValue constValue2;
	if (fieldInstance->GetFieldDef()->mIsConst)
	{
		if (fieldInstance->mConstIdx != -1)
		{
			auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
			uint64 val = constant->mUInt64;

			if (constant->mTypeCode == BfTypeCode_Float)
			{
				float f = (float)*(double*)&val;
				val = *(uint32*)&f;
			}

			constValue = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, val);
			if (is32Bit)
				constValue2 = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, val >> 32);
		}
	}
	else if (fieldInstance->GetFieldDef()->mIsStatic)
	{
		BfTypedValue refVal;
		if (!mIsComptimeModule) // This can create circular reference issues for a `Self` static
			refVal = ReferenceStaticField(fieldInstance);
		if (refVal.mValue.IsConst())
		{
			auto constant = mBfIRBuilder->GetConstant(refVal.mValue);
			if (constant->mConstType == BfConstType_GlobalVar)
			{
				auto globalVar = (BfGlobalVar*)constant;
				if (globalVar->mName[0] == '#')
					refVal = BfTypedValue();
			}
		}

		if (!isComptimeArg)
		{
			if (refVal.IsAddr())
				constValue = mBfIRBuilder->CreatePtrToInt(refVal.mValue, BfTypeCode_IntPtr);
			else if ((fieldInstance->IsAppendedObject()) && (refVal))
				constValue = mBfIRBuilder->CreatePtrToInt(refVal.mValue, BfTypeCode_IntPtr);
		}
	}

	if (!constValue)
		constValue = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, fieldInstance->mDataOffset);

	BfIRValue result;
	if (is32Bit)
	{
		if (!constValue2)
			constValue2 = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0);

		SizedArray<BfIRValue, 8> fieldVals =
		{
			emptyValueType,
			fieldNameConst, // mName
			GetConstValue(typeId, typeIdType), // mFieldTypeId
			constValue, // mData
			constValue2, // mDataHi
			GetConstValue(fieldFlags, shortType), // mFlags
			GetConstValue((isComptimeArg || mIsComptimeModule) ? fieldInstance->mFieldIdx : customAttrIdx, intType), // mCustomAttributesIdx
		};
		FixConstValueParams(reflectFieldDataType, fieldVals, isComptimeArg);
		result = isComptimeArg ?
			mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapTypeInst(reflectFieldDataType, BfIRPopulateType_Full), fieldVals) :
			mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType, BfIRPopulateType_Full), fieldVals);
	}
	else
	{
		SizedArray<BfIRValue, 8> fieldVals =
		{
			emptyValueType,
			fieldNameConst, // mName
			GetConstValue(typeId, typeIdType), // mFieldTypeId
			constValue, // mData
			GetConstValue(fieldFlags, shortType), // mFlags
			GetConstValue((isComptimeArg || mIsComptimeModule) ? fieldInstance->mFieldIdx : customAttrIdx, intType), // mCustomAttributesIdx
		};
		FixConstValueParams(reflectFieldDataType, fieldVals, isComptimeArg);
		result = isComptimeArg ?
			mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapTypeInst(reflectFieldDataType, BfIRPopulateType_Full), fieldVals) :
			mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType, BfIRPopulateType_Full), fieldVals);
	}

	return result;
}

BfIRValue BfModule::CreateTypeData(BfType* type, Dictionary<int, int>& usedStringIdMap, bool forceReflectFields, bool needsTypeData, bool needsTypeNames, bool needsVData)
{
	if ((IsHotCompile()) && (!type->mDirty))
		return BfIRValue();

	BfIRValue* irValuePtr = NULL;
	if (mTypeDataRefs.TryGetValue(type, &irValuePtr))
	{
		return *irValuePtr;
	}

	BfTypeInstance* typeInstance = type->ToTypeInstance();
	BfType* typeInstanceType = ResolveTypeDef(mCompiler->mReflectTypeInstanceTypeDef);
	mBfIRBuilder->PopulateType(typeInstanceType, BfIRPopulateType_Full_ForceDefinition);

	if (typeInstanceType == NULL)
	{
		AssertErrorState();
		return BfIRValue();
	}

	BfIRValue typeTypeData;
	int typeFlags = 0;
	if (needsTypeData)
	{
		BfTypeInstance* typeInstanceTypeInstance = typeInstanceType->ToTypeInstance();

		BfTypeInstance* typeDataSource = NULL;

		if (typeInstance != NULL)
		{
			if (typeInstance->IsUnspecializedType())
			{
				typeDataSource = ResolveTypeDef(mCompiler->mReflectUnspecializedGenericType)->ToTypeInstance();
				typeFlags |= BfTypeFlags_UnspecializedGeneric;
			}
			else if (typeInstance->IsArray())
			{
				typeDataSource = ResolveTypeDef(mCompiler->mReflectArrayType)->ToTypeInstance();
				typeFlags |= BfTypeFlags_Array;
			}
			else if (typeInstance->IsGenericTypeInstance())
			{
				typeDataSource = ResolveTypeDef(mCompiler->mReflectSpecializedGenericType)->ToTypeInstance();
				typeFlags |= BfTypeFlags_SpecializedGeneric;
			}
			else
				typeDataSource = typeInstanceTypeInstance;
		}
		else if (type->IsPointer())
			typeDataSource = ResolveTypeDef(mCompiler->mReflectPointerType)->ToTypeInstance();
		else if (type->IsRef())
			typeDataSource = ResolveTypeDef(mCompiler->mReflectRefType)->ToTypeInstance();
		else if (type->IsSizedArray())
			typeDataSource = ResolveTypeDef(mCompiler->mReflectSizedArrayType)->ToTypeInstance();
		else if (type->IsConstExprValue())
			typeDataSource = ResolveTypeDef(mCompiler->mReflectConstExprType)->ToTypeInstance();
		else if (type->IsGenericParam())
		{
			typeFlags |= BfTypeFlags_GenericParam;
			typeDataSource = ResolveTypeDef(mCompiler->mReflectGenericParamType)->ToTypeInstance();
		}
		else
			typeDataSource = mContext->mBfTypeType;

		if ((!mTypeDataRefs.ContainsKey(typeDataSource)) && (typeDataSource != type) && (!mIsComptimeModule))
		{
			CreateTypeData(typeDataSource, usedStringIdMap, false, true, needsTypeNames, true);
		}

		typeTypeData = CreateClassVDataGlobal(typeDataSource);
	}
	else
		typeTypeData = CreateClassVDataGlobal(typeInstanceType->ToTypeInstance());

	BfType* longType = GetPrimitiveType(BfTypeCode_Int64);
	BfType* intType = GetPrimitiveType(BfTypeCode_Int32);
	BfType* intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
	BfType* shortType = GetPrimitiveType(BfTypeCode_Int16);
	BfType* byteType = GetPrimitiveType(BfTypeCode_Int8);

	BfType* typeIdType = intType;

	auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
	auto voidPtrIRType = mBfIRBuilder->MapType(voidPtrType);
	auto voidPtrPtrIRType = mBfIRBuilder->GetPointerTo(voidPtrIRType);
	auto voidPtrNull = GetDefaultValue(voidPtrType);

	SizedArray<BfIRValue, 4> typeValueParams;
	GetConstClassValueParam(typeTypeData, typeValueParams);

	FixConstValueParams(mContext->mBfObjectType, typeValueParams);
	BfIRValue objectData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(mContext->mBfObjectType, BfIRPopulateType_Full), typeValueParams);

	StringT<512> typeDataName;
	if ((typeInstance != NULL) && (!typeInstance->IsTypeAlias()))
	{
		BfMangler::MangleStaticFieldName(typeDataName, mCompiler->GetMangleKind(), typeInstance, "sBfTypeData");
		if (typeInstance->mTypeDef->IsGlobalsContainer())
			typeDataName += "`G`" + typeInstance->mTypeDef->mProject->mName;
	}
	else
	{
		typeDataName += "sBfTypeData.";
		BfMangler::Mangle(typeDataName, mCompiler->GetMangleKind(), type, mContext->mScratchModule);
	}

	int typeCode = BfTypeCode_None;

	if (typeInstance != NULL)
	{
		BF_ASSERT((type->mDefineState >= BfTypeDefineState_DefinedAndMethodsSlotted) || mIsComptimeModule);
		typeCode = typeInstance->mTypeDef->mTypeCode;
	}
	else if (type->IsPrimitiveType())
	{
		BfPrimitiveType* primType = (BfPrimitiveType*)type;
		typeCode = primType->mTypeDef->mTypeCode;
	}
	else if (type->IsPointer())
	{
		typeCode = BfTypeCode_Pointer;
		typeFlags |= BfTypeFlags_Pointer;
	}
	else if (type->IsRef())
	{
		typeCode = BfTypeCode_Pointer;
		typeFlags |= BfTypeFlags_Pointer;
	}

	if (type->IsObject())
	{
		typeFlags |= BfTypeFlags_Object;
		if (typeInstance->mDefineState >= BfTypeDefineState_DefinedAndMethodsSlotted)
		{
			BfMethodInstance* methodInstance = typeInstance->mVirtualMethodTable[mCompiler->GetVTableMethodOffset() + 0].mImplementingMethod;
			if (methodInstance->GetOwner() != mContext->mBfObjectType)
				typeFlags |= BfTypeFlags_HasDestructor;
		}
	}
	if (type->IsStruct())
		typeFlags |= BfTypeFlags_Struct;
	if (type->IsInterface())
		typeFlags |= BfTypeFlags_Interface;
	if (type->IsBoxed())
	{
		typeFlags |= BfTypeFlags_Boxed;
		if (((BfBoxedType*)type)->IsBoxedStructPtr())
			typeFlags |= BfTypeFlags_Pointer;
	}
	if (type->IsPrimitiveType())
		typeFlags |= BfTypeFlags_Primitive;
	if (type->IsTypedPrimitive())
		typeFlags |= BfTypeFlags_TypedPrimitive;
	if (type->IsTuple())
		typeFlags |= BfTypeFlags_Tuple;
	if (type->IsNullable())
		typeFlags |= BfTypeFlags_Nullable;
	if (type->IsSizedArray())
		typeFlags |= BfTypeFlags_SizedArray;
	if (type->IsConstExprValue())
		typeFlags |= BfTypeFlags_ConstExpr;
	if (type->IsSplattable())
		typeFlags |= BfTypeFlags_Splattable;
	if (type->IsUnion())
		typeFlags |= BfTypeFlags_Union;
	if (type->IsDelegate())
		typeFlags |= BfTypeFlags_Delegate;
	if (type->IsFunction())
		typeFlags |= BfTypeFlags_Function;
	if ((type->mDefineState != BfTypeDefineState_CETypeInit) && (type->WantsGCMarking()))
		typeFlags |= BfTypeFlags_WantsMarking;

	int virtSlotIdx = -1;
	if ((typeInstance != NULL) && (typeInstance->mSlotNum >= 0))
		virtSlotIdx = typeInstance->mSlotNum + 1 + mCompiler->GetDynCastVDataCount();
	int memberDataOffset = 0;
	if (type->IsInterface())
		memberDataOffset = virtSlotIdx * mSystem->mPtrSize;
	else if (typeInstance != NULL)
	{
		for (auto& fieldInstance : typeInstance->mFieldInstances)
		{
			if (fieldInstance.mDataOffset != -1)
			{
				memberDataOffset = fieldInstance.mDataOffset;
				break;
			}
		}
	}

	int boxedTypeId = 0;
	if ((type->IsValueType()) || (type->IsWrappableType()))
	{
		auto boxedType = CreateBoxedType(type, false);
		if ((boxedType != NULL) && (boxedType->mIsReified))
			boxedTypeId = boxedType->mTypeId;
	}

	int stackCount = 0;
	if ((typeInstance != NULL) && (typeInstance->mTypeOptionsIdx != -1))
	{
		auto typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);
		if (typeOptions->mAllocStackTraceDepth != -1)
			stackCount = BF_MIN(0xFF, BF_MAX(0x01, typeOptions->mAllocStackTraceDepth));
	}

	SizedArray<BfIRValue, 9> typeDataParams =
	{
		objectData,
		GetConstValue(type->mSize, intType), // mSize
		GetConstValue(type->mTypeId, typeIdType), // mTypeId
		GetConstValue(boxedTypeId, typeIdType), // mBoxedType
		GetConstValue(typeFlags, intType), // mTypeFlags
		GetConstValue(memberDataOffset, intType), // mMemberDataOffset
		GetConstValue(typeCode, byteType), // mTypeCode
		GetConstValue(type->mAlign, byteType), // mAlign
		GetConstValue(stackCount, byteType), // mAllocStackCountOverride
	};
	FixConstValueParams(mContext->mBfTypeType, typeDataParams);
	auto typeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(mContext->mBfTypeType, BfIRPopulateType_Full), typeDataParams);

	if (typeInstance == NULL)
	{
		BfIRValue typeDataVar;

		if (needsTypeData)
		{
			if (type->IsPointer())
			{
				auto pointerType = (BfPointerType*)type;
				SizedArray<BfIRValue, 3> pointerTypeDataParms =
				{
					typeData,
					GetConstValue(pointerType->mElementType->mTypeId, typeIdType),
				};

				auto reflectPointerType = ResolveTypeDef(mCompiler->mReflectPointerType)->ToTypeInstance();
				FixConstValueParams(reflectPointerType, pointerTypeDataParms);
				auto pointerTypeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectPointerType, BfIRPopulateType_Full), pointerTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectPointerType), true,
					BfIRLinkageType_External, pointerTypeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else if (type->IsRef())
			{
				auto refType = (BfRefType*)type;
				SizedArray<BfIRValue, 3> refTypeDataParms =
				{
					typeData,
					GetConstValue(refType->mElementType->mTypeId, typeIdType),
					GetConstValue((int8)refType->mRefKind, byteType),
				};

				auto reflectRefType = ResolveTypeDef(mCompiler->mReflectRefType)->ToTypeInstance();
				FixConstValueParams(reflectRefType, refTypeDataParms);
				auto refTypeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectRefType, BfIRPopulateType_Full), refTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectRefType), true,
					BfIRLinkageType_External, refTypeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else if (type->IsSizedArray())
			{
				auto sizedArrayType = (BfSizedArrayType*)type;
				SizedArray<BfIRValue, 3> sizedArrayTypeDataParms =
				{
					typeData,
					GetConstValue(sizedArrayType->mElementType->mTypeId, typeIdType),
					GetConstValue(sizedArrayType->mElementCount, intType)
				};

				auto reflectSizedArrayType = ResolveTypeDef(mCompiler->mReflectSizedArrayType)->ToTypeInstance();
				FixConstValueParams(reflectSizedArrayType, sizedArrayTypeDataParms);
				auto sizedArrayTypeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectSizedArrayType, BfIRPopulateType_Full), sizedArrayTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectSizedArrayType), true,
					BfIRLinkageType_External, sizedArrayTypeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else if (type->IsConstExprValue())
			{
				auto constExprType = (BfConstExprValueType*)type;
				SizedArray<BfIRValue, 3> constExprTypeDataParms =
				{
					typeData,
					GetConstValue(constExprType->mType->mTypeId, typeIdType),
					GetConstValue(constExprType->mValue.mInt64, longType)
				};

				auto reflectConstExprType = ResolveTypeDef(mCompiler->mReflectConstExprType)->ToTypeInstance();
				FixConstValueParams(reflectConstExprType, constExprTypeDataParms);
				auto ConstExprTypeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectConstExprType, BfIRPopulateType_Full), constExprTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectConstExprType), true,
					BfIRLinkageType_External, ConstExprTypeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else if (type->IsGenericParam())
			{
				auto genericParamType = (BfGenericParamType*)type;
				SizedArray<BfIRValue, 3> genericParamTypeDataParms =
				{
					typeData
				};

				auto reflectGenericParamType = ResolveTypeDef(mCompiler->mReflectGenericParamType)->ToTypeInstance();
				FixConstValueParams(reflectGenericParamType, genericParamTypeDataParms);
				auto genericParamTypeData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectGenericParamType, BfIRPopulateType_Full), genericParamTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectGenericParamType), true,
					BfIRLinkageType_External, genericParamTypeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else
			{
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(mContext->mBfTypeType), true,
					BfIRLinkageType_External, typeData, typeDataName);
				mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);
			}
		}
		else
			typeDataVar = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(mContext->mBfTypeType));

		mTypeDataRefs[type] = typeDataVar;
		return typeDataVar;
	}

	// Reserve position
	mTypeDataRefs[typeInstance] = BfIRValue();

	if ((typeInstance->IsReified()) && (!mIsComptimeModule))
		PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	BfTypeDef* typeDef = typeInstance->mTypeDef;
	StringT<512> mangledName;
	BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), typeInstance, typeInstance->mModule);

	if (!mIsComptimeModule)
	{
		for (int methodIdx = 0; methodIdx < (int)typeDef->mMethods.size(); methodIdx++)
		{
			auto methodDef = typeDef->mMethods[methodIdx];
			auto methodInstance = typeInstance->mMethodInstanceGroups[methodIdx].mDefault;
			if (methodInstance == NULL)
				continue;
			if (typeInstance->IsUnspecializedType())
				continue;
			if (!typeInstance->IsTypeMemberAccessible(methodDef->mDeclaringType, mProject))
			{
				if (methodInstance->mChainType == BfMethodChainType_ChainMember)
				{
					BF_ASSERT(!methodInstance->GetOwner()->IsUnspecializedType());

					// We need to create an empty thunk for this chained method
					BfIRFunction func = CreateFunctionFrom(methodInstance, false, methodInstance->mAlwaysInline);
					mBfIRBuilder->SetActiveFunction(func);
					auto block = mBfIRBuilder->CreateBlock("entry", true);
					mBfIRBuilder->SetInsertPoint(block);
					mBfIRBuilder->CreateRetVoid();
					mBfIRBuilder->SetActiveFunction(BfIRFunction());
				}
			}
		}
	}

	SizedArray<BfIRValue, 32> vData;
	BfIRValue classVDataVar;
	String classVDataName;
	if (typeInstance->mSlotNum >= 0)
	{
		// For interfaces we ONLY emit the slot num
		StringT<512> slotVarName;
		BfMangler::MangleStaticFieldName(slotVarName, mCompiler->GetMangleKind(), typeInstance, "sBfSlotOfs");
		auto intType = GetPrimitiveType(BfTypeCode_Int32);
		auto slotNumVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(intType), true, BfIRLinkageType_External,
			GetConstValue32(virtSlotIdx), slotVarName);
	}
	else if ((typeInstance->IsObject()) && (!typeInstance->IsUnspecializedType()) && (needsVData))
	{
		int dynCastDataElems = 0;
		int numElements = 1;
		int vDataOfs = 1; // The number of intptrs before the iface slot map
		numElements += mCompiler->mMaxInterfaceSlots;
		if (!typeInstance->IsInterface())
		{
			dynCastDataElems = 1 + mCompiler->mMaxInterfaceSlots;
			numElements += mCompiler->GetDynCastVDataCount();
			numElements += typeInstance->mVirtualMethodTableSize;
		}

		int expectNumElements = 0;
		if (!typeDef->mIsStatic)
		{
			classVDataVar = CreateClassVDataGlobal(typeInstance, &expectNumElements, &classVDataName);
		}

		vData.push_back(BfIRValue()); // Type*

		SizedArray<BfIRValue, 1> extVTableData;

		SizedArrayImpl<BfIRValue>* vFuncDataExt = &extVTableData;

		if (!typeInstance->IsInterface())
		{
			Array<int32> dynCastData;
			if (typeInstance->IsBoxed())
			{
				auto underlyingType = typeInstance->GetUnderlyingType()->ToTypeInstance();
				dynCastData.Add(underlyingType->mInheritanceId);
			}
			else
				dynCastData.Add(typeInstance->mInheritanceId);
			for (int i = 0; i < mCompiler->mMaxInterfaceSlots; i++)
				dynCastData.Add(0);
			dynCastData.Add(0);

			auto checkTypeInst = typeInstance;
			while (checkTypeInst != NULL)
			{
				for (auto&& interfaceEntry : checkTypeInst->mInterfaces)
				{
					if (interfaceEntry.mInterfaceType->mSlotNum >= 0)
					{
						if (dynCastData[interfaceEntry.mInterfaceType->mSlotNum + 1] == 0)
							dynCastData[interfaceEntry.mInterfaceType->mSlotNum + 1] = interfaceEntry.mInterfaceType->mTypeId;
					}
				}
				checkTypeInst = checkTypeInst->GetImplBaseType();
			}

			if (mSystem->mPtrSize == 8)
			{
				int intPtrCount = (dynCastDataElems + 1) / 2;
				vDataOfs += intPtrCount;
				uint64* intPtrData = (uint64*)&dynCastData[0];
				for (int i = 0; i < intPtrCount; i++)
				{
					uint64 val = intPtrData[i];
					if (val == 0)
					{
						vData.push_back(voidPtrNull);
					}
					else
					{
						auto intPtrVal = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, val);
						vData.push_back(mBfIRBuilder->CreateBitCast(intPtrVal, voidPtrIRType));
					}
				}
			}
			else
			{
				int intPtrCount = dynCastDataElems;
				vDataOfs += intPtrCount;
				uint32* intPtrData = (uint32*)&dynCastData[0];
				for (int i = 0; i < intPtrCount; i++)
				{
					uint32 val = intPtrData[i];
					if (val == 0)
					{
						vData.push_back(voidPtrNull);
					}
					else
					{
						auto intPtrVal = mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (int)val);
						vData.push_back(mBfIRBuilder->CreateBitCast(intPtrVal, voidPtrIRType));
					}
				}
			}
		}

		for (int iSlotIdx = 0; iSlotIdx < mCompiler->mMaxInterfaceSlots; iSlotIdx++)
			vData.push_back(voidPtrNull);

		struct _InterfaceMatchEntry
		{
			BfTypeInterfaceEntry* mEntry;
			BfTypeInstance* mEntrySource;
			Array<BfTypeInterfaceEntry*> mAmbiguousEntries;
		};

		int highestIFaceVirtIdx = 0;
		Dictionary<BfTypeInstance*, _InterfaceMatchEntry> interfaceMap;
		auto checkTypeInst = typeInstance;

		bool forceInterfaceSet = false;
		while (checkTypeInst != NULL)
		{
			for (auto&& interfaceEntry : checkTypeInst->mInterfaces)
			{
				highestIFaceVirtIdx = BF_MAX(highestIFaceVirtIdx, interfaceEntry.mStartVirtualIdx + interfaceEntry.mInterfaceType->mVirtualMethodTableSize);

				if ((!mIsComptimeModule) && (!typeInstance->IsTypeMemberAccessible(interfaceEntry.mDeclaringType, mProject)))
					continue;

				_InterfaceMatchEntry* matchEntry = NULL;
				if (interfaceMap.TryGetValue(interfaceEntry.mInterfaceType, &matchEntry))
				{
					BfTypeInstance* compareCheckTypeInst = checkTypeInst;
					if (compareCheckTypeInst->IsBoxed())
						compareCheckTypeInst = compareCheckTypeInst->GetUnderlyingType()->ToTypeInstance();
					BfTypeInstance* compareEntrySource = matchEntry->mEntrySource;
					if (compareEntrySource->IsBoxed())
						compareEntrySource = compareEntrySource->GetUnderlyingType()->ToTypeInstance();

					auto prevEntry = matchEntry->mEntry;
					bool isBetter = TypeIsSubTypeOf(compareCheckTypeInst, compareEntrySource);
					bool isWorse = TypeIsSubTypeOf(compareEntrySource, compareCheckTypeInst);

					if ((!isBetter) && (!isWorse))
					{
						// Unboxed comparison to Object fix
						if (compareCheckTypeInst == mContext->mBfObjectType)
							isWorse = true;
						else if (compareEntrySource == mContext->mBfObjectType)
							isBetter = true;
					}

					if (forceInterfaceSet)
					{
						isBetter = true;
						isWorse = false;
					}
					if (isBetter == isWorse)
						CompareDeclTypes(checkTypeInst, interfaceEntry.mDeclaringType, prevEntry->mDeclaringType, isBetter, isWorse);
					if (isBetter == isWorse)
					{
						if (matchEntry->mAmbiguousEntries.empty())
							matchEntry->mAmbiguousEntries.push_back(prevEntry);
						matchEntry->mAmbiguousEntries.push_back(&interfaceEntry);
						continue;
					}
					else if (isBetter)
					{
						matchEntry->mEntry = &interfaceEntry;
						matchEntry->mEntrySource = checkTypeInst;
						matchEntry->mAmbiguousEntries.Clear();
					}
					else
						continue;
				}
				else
				{
					_InterfaceMatchEntry matchEntry;
					matchEntry.mEntry = &interfaceEntry;
					matchEntry.mEntrySource = checkTypeInst;
					interfaceMap[interfaceEntry.mInterfaceType] = matchEntry;
				}
			}

			checkTypeInst = checkTypeInst->GetImplBaseType();
		}

		for (auto interfacePair : interfaceMap)
		{
			auto& interfaceMatchEntry = interfacePair.mValue;
			if (!interfaceMatchEntry.mAmbiguousEntries.empty())
			{
				auto error = Fail(StrFormat("Type '%s' has an ambiguous declaration for interface '%s'", TypeToString(typeInstance).c_str(), TypeToString(interfaceMatchEntry.mEntry->mInterfaceType).c_str()), interfaceMatchEntry.mEntry->mDeclaringType->GetRefNode());
				if (error != NULL)
				{
					for (auto ambiguiousEntry : interfaceMatchEntry.mAmbiguousEntries)
						mCompiler->mPassInstance->MoreInfo("See other declaration", ambiguiousEntry->mDeclaringType->GetRefNode());
				}
			}
		}

		if ((!mIsComptimeModule) && (!typeInstance->IsInterface()) && (typeInstance->mVirtualMethodTableSize > 0) && (needsVData))
		{
			int startTabIdx = (int)vData.size();

			SizedArray<BfTypeInstance*, 8> origVirtTypeStack;
			BfTypeInstance* checkTypeInst = typeInstance;
			while (checkTypeInst != NULL)
			{
				origVirtTypeStack.push_back(checkTypeInst);
				checkTypeInst = checkTypeInst->GetImplBaseType();
			}

			Array<BfVirtualMethodEntry> origVTable;

			int origVirtIdx = 0;
			if (typeInstance->mTypeDef->mIsCombinedPartial)
			{
				HashSet<String> reslotNames;
				for (int virtIdx = 0; virtIdx < (int)typeInstance->mVirtualMethodTable.size(); virtIdx++)
				{
					auto& entry = typeInstance->mVirtualMethodTable[virtIdx];
					if (entry.mDeclaringMethod.mMethodNum == -1)
						continue;
					BfMethodInstance* methodInstance = (BfMethodInstance*)entry.mImplementingMethod;
					if ((methodInstance == NULL) ||
						((!mIsComptimeModule) && (!typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject))))
					{
						if (origVTable.empty())
							origVTable = typeInstance->mVirtualMethodTable;

						BfMethodInstance* declMethodInstance = entry.mDeclaringMethod;
						if ((mIsComptimeModule) || (typeInstance->IsTypeMemberAccessible(declMethodInstance->mMethodDef->mDeclaringType, mProject)))
						{
							// Prepare to reslot...
							entry.mImplementingMethod = entry.mDeclaringMethod;
							reslotNames.Add(declMethodInstance->mMethodDef->mName);
						}
						else
						{
							// Decl isn't accessible, null out entry
							entry.mImplementingMethod = BfMethodRef();
						}
					}
				}

				if (!reslotNames.IsEmpty())
				{
					BfAmbiguityContext ambiguityContext;
					ambiguityContext.mTypeInstance = typeInstance;
					ambiguityContext.mModule = this;
					ambiguityContext.mIsProjectSpecific = true;
					ambiguityContext.mIsReslotting = true;

					SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
					for (auto& methodGroup : typeInstance->mMethodInstanceGroups)
					{
						auto methodInstance = methodGroup.mDefault;
						if ((methodInstance == NULL) || (!methodInstance->mMethodDef->mIsOverride))
							continue;

						if (!reslotNames.Contains(methodInstance->mMethodDef->mName))
							continue;

						if ((!mIsComptimeModule) && (!typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject)))
							continue;
						if ((methodInstance->mChainType != BfMethodChainType_None) && (methodInstance->mChainType != BfMethodChainType_ChainHead))
							continue;
						SlotVirtualMethod(methodInstance, &ambiguityContext);
					}

					ambiguityContext.Finish();
				}
			}

			bool isInExts = false;
			for (int virtIdx = 0; virtIdx < (int)typeInstance->mVirtualMethodTable.size(); virtIdx++)
			{
				BfIRValue vValue = voidPtrNull;

				auto& entry = typeInstance->mVirtualMethodTable[virtIdx];
				BfModuleMethodInstance moduleMethodInst;
				if (entry.mDeclaringMethod.mMethodNum == -1)
				{
					// mMethodNum of -1 indicates a vExt slot rather than an actual method reference
					isInExts = true;
					BfIRValue vExt = CreateClassVDataExtGlobal(entry.mDeclaringMethod.mTypeInstance, typeInstance, virtIdx);
					if (vExt)
						vValue = mBfIRBuilder->CreateBitCast(vExt, voidPtrIRType);
				}
				else
				{
					BfMethodInstance* declaringMethodInstance = (BfMethodInstance*)entry.mDeclaringMethod;
					if ((declaringMethodInstance != NULL) && (declaringMethodInstance->mMethodInstanceGroup->IsImplemented()) && (declaringMethodInstance->mIsReified))
					{
 						BF_ASSERT(entry.mImplementingMethod.mTypeInstance->mMethodInstanceGroups[entry.mImplementingMethod.mMethodNum].IsImplemented());
 						BF_ASSERT(entry.mImplementingMethod.mTypeInstance->mMethodInstanceGroups[entry.mImplementingMethod.mMethodNum].mDefault->mIsReified);
						BfMethodInstance* methodInstance = (BfMethodInstance*)entry.mImplementingMethod;
						if ((methodInstance != NULL) && (!methodInstance->mMethodDef->mIsAbstract))
						{
							BF_ASSERT((mIsComptimeModule) || typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject));
							moduleMethodInst = GetMethodInstanceAtIdx(methodInstance->mMethodInstanceGroup->mOwner, methodInstance->mMethodInstanceGroup->mMethodIdx, NULL, BfGetMethodInstanceFlag_NoInline);
							auto funcPtr = mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, voidPtrIRType);
							vValue = funcPtr;
						}
					}
				}

				while (origVirtTypeStack.size() != 0)
				{
					BfTypeInstance* useTypeInst = origVirtTypeStack[origVirtTypeStack.size() - 1];
					if (virtIdx >= useTypeInst->mVirtualMethodTableSize)
					{
						// Moved past current base vtable size
						origVirtTypeStack.pop_back();
						continue;
					}

					if (origVirtIdx < useTypeInst->GetOrigVTableSize())
					{
						// We are within the original vtable size
						int idx = startTabIdx + origVirtIdx;
						origVirtIdx++;
						vData.push_back(vValue);
					}
					else
					{
						// This is a new entry, it only gets added to the ext
						BF_ASSERT(isInExts);
					}
					break;
				}
			}

			if (!origVTable.empty())
				typeInstance->mVirtualMethodTable = origVTable;
		}

		int ifaceMethodExtStart = (int)typeInstance->GetIFaceVMethodSize();
		if (typeInstance->mHotTypeData != NULL)
		{
			if (typeInstance->mHotTypeData->mOrigInterfaceMethodsLength != -1)
				ifaceMethodExtStart = typeInstance->mHotTypeData->mOrigInterfaceMethodsLength;
		}

		SizedArray<BfIRValue, 32> ifaceMethodExtData;

		int iFaceMethodStartIdx = (int)vData.size();
		vData.resize(iFaceMethodStartIdx + ifaceMethodExtStart);
		for (int i = iFaceMethodStartIdx; i < iFaceMethodStartIdx + ifaceMethodExtStart; i++)
			vData[i] = voidPtrNull;

		if (expectNumElements != 0)
			BF_ASSERT(expectNumElements == vData.size());

		//BF_ASSERT(vData.size() == expectNumElements);

// 		for (int i = 0; i < (int)typeInstance->mInterfaceMethodTable.size() - ifaceMethodExtStart; i++)
// 			ifaceMethodExtData.Add(voidPtrNull);

		int ifaceEntryIdx = iFaceMethodStartIdx;
		for (auto& interfacePair : interfaceMap)
		{
			auto interfaceEntry = interfacePair.mValue.mEntry;

			if (typeInstance->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted)
			{
				BF_ASSERT(mIsComptimeModule);
				break;
			}

			bool makeEmpty = false;
			if ((!mIsComptimeModule) && (!typeInstance->IsTypeMemberAccessible(interfaceEntry->mDeclaringType, mProject)))
				makeEmpty = true;

			int endVirtualIdx = interfaceEntry->mStartVirtualIdx + interfaceEntry->mInterfaceType->mVirtualMethodTableSize;
			bool useExt = endVirtualIdx > ifaceMethodExtStart;

			for (int methodIdx = 0; methodIdx < (int)interfaceEntry->mInterfaceType->mMethodInstanceGroups.size(); methodIdx++)
			{
				BfIRValue pushValue;

				BfMethodInstance* ifaceMethodInstance = interfaceEntry->mInterfaceType->mMethodInstanceGroups[methodIdx].mDefault;
				if ((ifaceMethodInstance == NULL) || (ifaceMethodInstance->mVirtualTableIdx == -1))
					continue;

				if ((!ifaceMethodInstance->mIsReified) || (!ifaceMethodInstance->mMethodInstanceGroup->IsImplemented()))
					continue;

				if (makeEmpty)
				{
					pushValue = voidPtrNull;
				}
				else
				{
					auto& methodRef = typeInstance->mInterfaceMethodTable[interfaceEntry->mStartInterfaceTableIdx + methodIdx].mMethodRef;
					auto methodInstance = (BfMethodInstance*)methodRef;
					if ((methodInstance != NULL) && (!methodInstance->mMethodDef->mIsAbstract))
					{
						BF_ASSERT(methodInstance->mIsReified);

						// This doesn't work because we may have FOREIGN methods from implicit interface methods
						//auto moduleMethodInst = GetMethodInstanceAtIdx(methodRef.mTypeInstance, methodRef.mMethodNum);
						auto moduleMethodInst = ReferenceExternalMethodInstance(methodInstance, BfGetMethodInstanceFlag_NoInline);
						auto funcPtr = mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, voidPtrIRType);
						pushValue = funcPtr;
					}
					else
					{
						pushValue = voidPtrNull;
					}
				}

				int idx = interfaceEntry->mStartVirtualIdx + ifaceMethodInstance->mVirtualTableIdx;
				if (idx < ifaceMethodExtStart)
				{
					BF_ASSERT(iFaceMethodStartIdx + idx < (int)vData.size());
					vData[iFaceMethodStartIdx + idx] = pushValue;
				}
				else
				{
					BF_ASSERT(useExt);
					int extIdx = idx - ifaceMethodExtStart;
					while (extIdx >= ifaceMethodExtData.size())
						ifaceMethodExtData.Add(voidPtrNull);
					ifaceMethodExtData[extIdx] = pushValue;
				}
			}
		}

		if (typeInstance->IsBoxed())
		{
			BfBoxedType* boxedType = (BfBoxedType*)typeInstance;
			if (boxedType->IsBoxedStructPtr())
			{
				// Force override of GetHashCode so we use the pointer address as the hash code
				checkTypeInst = CreateBoxedType(ResolveTypeDef(mCompiler->mPointerTypeDef));

				// Force override of GetHashCode so we use the pointer address as the hash code
				for (auto& checkIFace : checkTypeInst->mInterfaces)
				{
					for (int methodIdx = 0; methodIdx < (int)checkIFace.mInterfaceType->mMethodInstanceGroups.size(); methodIdx++)
					{
						BfIRValue pushValue;

						BfMethodInstance* ifaceMethodInstance = checkIFace.mInterfaceType->mMethodInstanceGroups[methodIdx].mDefault;
						if ((ifaceMethodInstance == NULL) || (ifaceMethodInstance->mVirtualTableIdx == -1))
							continue;

						if ((!ifaceMethodInstance->mIsReified) || (!ifaceMethodInstance->mMethodInstanceGroup->IsImplemented()))
							continue;

						auto& methodRef = checkTypeInst->mInterfaceMethodTable[checkIFace.mStartInterfaceTableIdx + methodIdx].mMethodRef;

						auto methodInstance = (BfMethodInstance*)methodRef;
						BF_ASSERT(methodInstance->mIsReified);
						// This doesn't work because we may have FOREIGN methods from implicit interface methods
						//auto moduleMethodInst = GetMethodInstanceAtIdx(methodRef.mTypeInstance, methodRef.mMethodNum);
						auto moduleMethodInst = ReferenceExternalMethodInstance(methodInstance, BfGetMethodInstanceFlag_NoInline);
						auto funcPtr = mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, voidPtrIRType);

						int idx = checkIFace.mStartVirtualIdx + ifaceMethodInstance->mVirtualTableIdx;
						vData[iFaceMethodStartIdx + idx] = funcPtr;
					}
				}
			}
		}

		if ((needsVData) && (!typeInstance->mTypeDef->mIsStatic) && (!mIsComptimeModule))
		{
			BfIRValue ifaceMethodExtVar;
			if ((!ifaceMethodExtData.IsEmpty()) && (!mIsComptimeModule))
			{
				StringT<512> classVDataName;
				BfMangler::MangleStaticFieldName(classVDataName, mCompiler->GetMangleKind(), typeInstance, "bf_hs_replace_IFaceExt");
				auto arrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr), (int)ifaceMethodExtData.size());
				ifaceMethodExtVar = mBfIRBuilder->CreateGlobalVariable(arrayType, true,
					BfIRLinkageType_External, BfIRValue(), classVDataName);

				BfIRType extVTableType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)ifaceMethodExtData.size());
				BfIRValue extVTableConst = mBfIRBuilder->CreateConstAgg_Value(extVTableType, ifaceMethodExtData);
				mBfIRBuilder->GlobalVar_SetInitializer(ifaceMethodExtVar, extVTableConst);
			}

			for (auto& ifaceInstPair : interfaceMap)
			{
				auto interfaceEntry = ifaceInstPair.mValue.mEntry;
				int slotNum = interfaceEntry->mInterfaceType->mSlotNum;
				if (slotNum >= 0)
				{
					BfIRValue vtablePtr;
					int idx = interfaceEntry->mStartVirtualIdx;
					int endVirtualIdx = interfaceEntry->mStartVirtualIdx + interfaceEntry->mInterfaceType->mVirtualMethodTableSize;

					if ((endVirtualIdx > ifaceMethodExtStart) && (ifaceMethodExtVar))
						vtablePtr = mBfIRBuilder->CreateInBoundsGEP(ifaceMethodExtVar, 0, interfaceEntry->mStartVirtualIdx - ifaceMethodExtStart);
					else
						vtablePtr = mBfIRBuilder->CreateInBoundsGEP(classVDataVar, 0, iFaceMethodStartIdx + interfaceEntry->mStartVirtualIdx);

					int slotVirtIdx = slotNum + vDataOfs;
					if (vData[slotVirtIdx] == voidPtrNull)
					{
						vData[slotVirtIdx] = mBfIRBuilder->CreateBitCast(vtablePtr, voidPtrIRType);
					}
					else
					{
						if (mCompiler->IsHotCompile())
							Fail("Interface slot collision error. Restart program or undo interface changes.", typeDef->GetRefNode());
						else
							Fail("Interface slot collision error", typeDef->GetRefNode());
					}
				}
			}
		}
	}

	BfIRValue typeNameConst;
	BfIRValue namespaceConst;

	if (needsTypeNames)
	{
		typeNameConst = GetStringObjectValue(typeDef->mName->mString, !mIsComptimeModule);
		namespaceConst = GetStringObjectValue(typeDef->mNamespace.ToString(), !mIsComptimeModule);
	}
	else
	{
		auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef);
		typeNameConst = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(stringType));
		namespaceConst = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(stringType));
	}

	int baseTypeId = 0;
	if (typeInstance->mBaseType != NULL)
	{
		baseTypeId = typeInstance->mBaseType->mTypeId;
	}

	BfTypeOptions* typeOptions = NULL;
	if (typeInstance->mTypeOptionsIdx >= 0)
		typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);

	SizedArray<BfIRValue, 16> customAttrs;

	BfTypeInstance* attributeType = mContext->mUnreifiedModule->ResolveTypeDef(mCompiler->mAttributeTypeDef)->ToTypeInstance();

	BfIRValue castedClassVData;
	if (classVDataVar)
		castedClassVData = mBfIRBuilder->CreateBitCast(classVDataVar, mBfIRBuilder->MapType(mContext->mBfClassVDataPtrType));
	else
		castedClassVData = GetDefaultValue(mContext->mBfClassVDataPtrType);

	bool freflectIncludeTypeData = false;
	bool reflectIncludeAllFields = false;
	bool reflectIncludeAllMethods = false;

	if (TypeIsSubTypeOf(typeInstance, attributeType))
	{
		reflectIncludeAllFields = true;
		reflectIncludeAllMethods = true;
	}

	BfReflectKind reflectKind = BfReflectKind_Type;

	auto _GetReflectUserFromDirective = [&](BfAttributeDirective* attributesDirective, BfAttributeTargets attrTarget)
	{
		BfReflectKind reflectKind = BfReflectKind_None;
		auto customAttrs = GetCustomAttributes(attributesDirective, attrTarget);
		if (customAttrs != NULL)
		{
			for (auto& attr : customAttrs->mAttributes)
			{
				reflectKind = (BfReflectKind)(reflectKind | GetUserReflectKind(attr.mType));
			}
			delete customAttrs;
		}
		return reflectKind;
	};

	reflectKind = GetReflectKind(reflectKind, typeInstance);

	// Fields
	BfType* reflectFieldDataType = ResolveTypeDef(mCompiler->mReflectFieldDataDef);
	BfIRValue emptyValueType = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance()->mBaseType), SizedArray<BfIRValue, 1>());

	auto _HandleCustomAttrs = [&](BfCustomAttributes* customAttributes)
	{
		if (customAttributes == NULL)
			return -1;

		Array<BfCustomAttribute*> reflectAttributes;
		for (auto& attr : customAttributes->mAttributes)
		{
			bool wantAttribute = false;

			if (attr.mType->mCustomAttributes != NULL)
			{
				auto usageAttribute = attr.mType->mCustomAttributes->Get(mCompiler->mAttributeUsageAttributeTypeDef);
				if (usageAttribute != NULL)
				{
					// Check for Flags arg
					if (usageAttribute->mCtorArgs.size() < 2)
						continue;
					auto constant = attr.mType->mConstHolder->GetConstant(usageAttribute->mCtorArgs[1]);
					if (constant == NULL)
						continue;
					if (constant->mTypeCode == BfTypeCode_Boolean)
						continue;
					if ((constant->mInt8 & BfCustomAttributeFlags_ReflectAttribute) != 0)
						wantAttribute = true;
				}
			}

			if ((wantAttribute) && (attr.mType->mIsReified))
			{
				reflectAttributes.Add(&attr);
			}
		}
		if (reflectAttributes.size() == 0)
			return -1;

#define PUSH_INT8(val) data.push_back((uint8)val)
#define PUSH_INT16(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); }
#define PUSH_INT32(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); data.push_back((val >> 16) & 0xFF); data.push_back((val >> 24) & 0xFF); }
		SizedArray<uint8, 16> data;

		int customAttrIdx = (int)customAttrs.size();

		data.push_back((uint8)reflectAttributes.size());

		for (auto attr : reflectAttributes)
		{
			// Size prefix
			int sizeIdx = (int)data.size();
			PUSH_INT16(0); // mSize

			PUSH_INT32(attr->mType->mTypeId); // mType

			int ctorIdx = -1;
			int ctorCount = 0;

			attr->mType->mTypeDef->PopulateMemberSets();
			BfMemberSetEntry* entry;
			if (attr->mType->mTypeDef->mMethodSet.TryGetWith(String("__BfCtor"), &entry))
			{
				BfMethodDef* nextMethodDef = (BfMethodDef*)entry->mMemberDef;
				while (nextMethodDef != NULL)
				{
					if (nextMethodDef == attr->mCtor)
						ctorIdx = ctorCount;
					nextMethodDef = nextMethodDef->mNextWithSameName;
					ctorCount++;
				}
			}

			BF_ASSERT(ctorIdx != -1);
			if (ctorIdx != -1)
				ctorIdx = (ctorCount - 1) - ctorIdx;
			PUSH_INT16(ctorIdx);

			auto ctorMethodInstance = GetRawMethodInstanceAtIdx(attr->mType, attr->mCtor->mIdx);
			int argIdx = 0;

			for (auto arg : attr->mCtorArgs)
			{
				auto argType = ctorMethodInstance->GetParamType(argIdx);
				EncodeAttributeData(typeInstance, argType, arg, data, usedStringIdMap);
				argIdx++;
			}

			int size = (int)data.size() - sizeIdx;
			*((uint16*)&data[sizeIdx]) = size;
		}

#undef PUSH_INT8
#undef PUSH_INT16
#undef PUSH_INT32

		SizedArray<BfIRValue, 16> dataValues;
		for (uint8 val : data)
			dataValues.push_back(GetConstValue8(val));
		auto dataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(byteType), (int)data.size());
		auto customAttrConst = mBfIRBuilder->CreateConstAgg_Value(dataArrayType, dataValues);

		BfIRValue customAttrArray = mBfIRBuilder->CreateGlobalVariable(dataArrayType, true, BfIRLinkageType_Internal,
			customAttrConst, typeDataName + StrFormat(".customAttr%d", customAttrIdx));
		BfIRValue customAttrArrayPtr = mBfIRBuilder->CreateBitCast(customAttrArray, voidPtrIRType);
		customAttrs.push_back(customAttrArrayPtr);
		return customAttrIdx;
	};

	int typeCustomAttrIdx = _HandleCustomAttrs(typeInstance->mCustomAttributes);

	SizedArray<int, 16> reflectFieldIndices;
	for (int pass = 0; pass < 2; pass++)
	{
		bool skippedField = false;

		reflectFieldIndices.clear();
		for (int fieldIdx = 0; fieldIdx < (int)typeInstance->mFieldInstances.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = &typeInstance->mFieldInstances[fieldIdx];
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			if (fieldDef == NULL)
				continue;

			auto fieldReflectKind = (BfReflectKind)(reflectKind & ~BfReflectKind_User);

			bool includeField = reflectIncludeAllFields;

			if (fieldInstance->mCustomAttributes != NULL)
			{
				for (auto& customAttr : fieldInstance->mCustomAttributes->mAttributes)
				{
					if (customAttr.mType->mTypeDef->mName->ToString() == "ReflectAttribute")
					{
						includeField = true;
					}
					else
					{
						auto userReflectKind = GetUserReflectKind(customAttr.mType);
						fieldReflectKind = (BfReflectKind)(fieldReflectKind | userReflectKind);
					}
				}
			}

			if ((fieldReflectKind & BfReflectKind_User) != 0)
				includeField = true;
			if ((!fieldDef->mIsStatic) && ((fieldReflectKind & BfReflectKind_NonStaticFields) != 0))
				includeField = true;
			if ((fieldDef->mIsStatic) && ((fieldReflectKind & BfReflectKind_StaticFields) != 0))
				includeField = true;

			if ((!fieldDef->mIsStatic) && (typeOptions != NULL))
				includeField = typeOptions->Apply(includeField, BfOptionFlags_ReflectNonStaticFields);
			if ((fieldDef->mIsStatic) && (typeOptions != NULL))
				includeField = typeOptions->Apply(includeField, BfOptionFlags_ReflectStaticFields);

			includeField |= forceReflectFields;

			if (!includeField)
			{
				skippedField = true;
				continue;
			}

			reflectFieldIndices.Add(fieldIdx);
		}

		// For a splattable type, we need to either include all fields or zero fields. This is to allow us to correctly splat
		//  params for reflected method calls even if we don't have reflection field info for the splatted type. This is
		//  possible because we write out a SplatData structure containing just TypeIds in that case
		if (pass == 0)
		{
			if (!typeInstance->IsSplattable())
				break;
			if (!skippedField)
				break;
			if (reflectFieldIndices.size() == 0)
				break;
		}
	}

	SizedArray<BfIRValue, 16> fieldTypes;

	bool is32Bit = mCompiler->mSystem->mPtrSize == 4;

	if ((typeInstance->IsPayloadEnum()) && (!typeInstance->IsBoxed()))
	{
		BfType* payloadType = typeInstance->GetUnionInnerType();
		if (!payloadType->IsValuelessType())
		{
			BfIRValue payloadNameConst = GetStringObjectValue("$payload", !mIsComptimeModule);
			SizedArray<BfIRValue, 8> payloadFieldVals;
			if (is32Bit)
			{
				payloadFieldVals =
				{
					emptyValueType,
					payloadNameConst, // mName
					GetConstValue(payloadType->mTypeId, typeIdType), // mFieldTypeId
					GetConstValue(0, intPtrType), // mData
					GetConstValue(0, intPtrType), // mDataHi
					GetConstValue(BfFieldFlags_SpecialName | BfFieldFlags_EnumPayload, shortType), // mFlags
					GetConstValue(-1, intType), // mCustomAttributesIdx
				};
			}
			else
			{
				payloadFieldVals =
				{
					emptyValueType,
					payloadNameConst, // mName
					GetConstValue(payloadType->mTypeId, typeIdType), // mFieldTypeId
					GetConstValue(0, intPtrType), // mData
					GetConstValue(BfFieldFlags_SpecialName | BfFieldFlags_EnumPayload, shortType), // mFlags
					GetConstValue(-1, intType), // mCustomAttributesIdx
				};
			}
			FixConstValueParams(reflectFieldDataType->ToTypeInstance(), payloadFieldVals);
			auto payloadFieldData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance(), BfIRPopulateType_Full), payloadFieldVals);
			fieldTypes.push_back(payloadFieldData);
		}

		BfType* dscrType = typeInstance->GetDiscriminatorType();
		BfIRValue dscrNameConst = GetStringObjectValue("$discriminator", !mIsComptimeModule);
		SizedArray<BfIRValue, 8> dscrFieldVals;
		if (is32Bit)
		{
			dscrFieldVals =
			{
				emptyValueType,
				dscrNameConst, // mName
				GetConstValue(dscrType->mTypeId, typeIdType), // mFieldTypeId
				GetConstValue(BF_ALIGN(payloadType->mSize, dscrType->mAlign), intPtrType), // mData
				GetConstValue(0, intPtrType), // mDataHi
				GetConstValue(BfFieldFlags_SpecialName | BfFieldFlags_EnumDiscriminator, shortType), // mFlags
				GetConstValue(-1, intType), // mCustomAttributesIdx
			};
		}
		else
		{
			dscrFieldVals =
			{
				emptyValueType,
				dscrNameConst, // mName
				GetConstValue(dscrType->mTypeId, typeIdType), // mFieldTypeId
				GetConstValue(BF_ALIGN(payloadType->mSize, dscrType->mAlign), intPtrType), // mData
				GetConstValue(BfFieldFlags_SpecialName | BfFieldFlags_EnumDiscriminator, shortType), // mFlags
				GetConstValue(-1, intType), // mCustomAttributesIdx
			};
		}
		auto dscrFieldData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance(), BfIRPopulateType_Full), dscrFieldVals);
		fieldTypes.push_back(dscrFieldData);
	}

	for (auto fieldIdx : reflectFieldIndices)
	{
		if (!needsTypeData)
			break;

		BfFieldInstance* fieldInstance = &typeInstance->mFieldInstances[fieldIdx];
		fieldTypes.push_back(CreateFieldData(fieldInstance, _HandleCustomAttrs(fieldInstance->mCustomAttributes)));
	}

	auto reflectFieldDataIRType = mBfIRBuilder->MapType(reflectFieldDataType);
	BfIRValue fieldDataPtr;
	BfIRType fieldDataPtrType = mBfIRBuilder->GetPointerTo(reflectFieldDataIRType);
	if (fieldTypes.size() == 0)
	{
		if ((type->IsSplattable()) && (type->IsStruct()) && (!type->IsValuelessType()) && (!typeInstance->mIsCRepr))
		{
			BfTypeInstance* reflectFieldSplatDataType = ResolveTypeDef(mCompiler->mReflectFieldSplatDataDef)->ToTypeInstance();

			SizedArray<BfIRValue, 3> splatTypes;
			SizedArray<BfIRValue, 3> splatOffsets;

			std::function<void(BfType*, int)> _CheckSplat = [&](BfType* checkType, int offset)
			{
				auto checkTypeInstance = checkType->ToTypeInstance();

				if ((checkTypeInstance != NULL) && (checkTypeInstance->IsValueType()) && (checkTypeInstance->IsDataIncomplete()))
					PopulateType(checkTypeInstance);

				if (checkType->IsStruct())
				{
					int dataEnd = offset;

					if (checkTypeInstance->mBaseType != NULL)
						_CheckSplat(checkTypeInstance->mBaseType, offset);

					if (checkTypeInstance->mIsUnion)
					{
						BfType* unionInnerType = checkTypeInstance->GetUnionInnerType();
						_CheckSplat(unionInnerType, offset);
					}
					else
					{
						for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
							if (fieldInstance->mDataIdx >= 0)
							{
								_CheckSplat(fieldInstance->GetResolvedType(), offset + fieldInstance->mDataOffset);
								dataEnd = offset + fieldInstance->mDataOffset + fieldInstance->mResolvedType->mSize;
							}
						}
					}

					if (checkTypeInstance->IsEnum())
					{
						// Add discriminator
						auto dscrType = checkTypeInstance->GetDiscriminatorType();
						if (checkTypeInstance->mPacking > 0)
							dataEnd = BF_ALIGN(dataEnd, dscrType->mAlign);
						_CheckSplat(dscrType, dataEnd);
					}
				}
				else if (checkType->IsMethodRef())
				{
// 					auto methodRefType = (BfMethodRefType*)checkType;
// 					for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
// 					{
// 						if (methodRefType->WantsDataPassedAsSplat(dataIdx))
// 							_CheckSplat(methodRefType->GetCaptureType(dataIdx), offsetof);
// 						else
// 							dataLambda(methodRefType->GetCaptureType(dataIdx));
// 					}
				}
				else if (!checkType->IsValuelessType())
				{
					splatTypes.Add(GetConstValue(checkType->mTypeId, typeIdType));
					splatOffsets.Add(GetConstValue(offset, intType));
				}
			};
			_CheckSplat(type, 0);

			while (splatTypes.size() < 3)
			{
				splatTypes.Add(GetConstValue(0, typeIdType));
				splatOffsets.Add(GetConstValue(0, intType));
			}

			BfIRValue splatTypesConst = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapType(reflectFieldSplatDataType->mFieldInstances[0].mResolvedType), splatTypes);
			BfIRValue splatOffsetsConst = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapType(reflectFieldSplatDataType->mFieldInstances[1].mResolvedType), splatOffsets);
			SizedArray<BfIRValue, 8> splatVals =
			{
				emptyValueType,
				splatTypesConst,
				splatOffsetsConst
			};
			auto fieldSplatData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectFieldSplatDataType->ToTypeInstance(), BfIRPopulateType_Full), splatVals);
			BfIRValue fieldDataArray = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(reflectFieldSplatDataType), true, BfIRLinkageType_Internal,
				fieldSplatData, typeDataName + ".splats");

			fieldDataPtr = mBfIRBuilder->CreateBitCast(fieldDataArray, fieldDataPtrType);
		}
		else
			fieldDataPtr = mBfIRBuilder->CreateConstNull(fieldDataPtrType);
	}
	else
	{
		BfIRType fieldDataConstType = mBfIRBuilder->GetSizedArrayType(reflectFieldDataIRType, (int)fieldTypes.size());
		BfIRValue fieldDataConst = mBfIRBuilder->CreateConstAgg_Value(fieldDataConstType, fieldTypes);
		BfIRValue fieldDataArray = mBfIRBuilder->CreateGlobalVariable(fieldDataConstType, true, BfIRLinkageType_Internal,
			fieldDataConst, "fields." + typeDataName);
		fieldDataPtr = mBfIRBuilder->CreateBitCast(fieldDataArray, fieldDataPtrType);
	}

	/// Methods

	BfType* reflectMethodDataType = ResolveTypeDef(mCompiler->mReflectMethodDataDef);
	BfType* reflectParamDataType = ResolveTypeDef(mCompiler->mReflectParamDataDef);
	BfType* reflectParamDataPtrType = CreatePointerType(reflectParamDataType);

	struct _SortedMethodInfo
	{
		BfMethodDef* mMethodDef;
		BfMethodCustomAttributes* mMethodCustomAttributes;
	};

	Array<_SortedMethodInfo> sortedMethodList;

	SizedArray<BfIRValue, 16> methodTypes;
	for (int methodIdx = 0; methodIdx < (int)typeDef->mMethods.size(); methodIdx++)
	{
		if (!needsTypeData)
			break;

		// Disable const method reflection info for now
		if (mIsComptimeModule)
			break;

		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodIdx];
		if (!methodInstanceGroup->IsImplemented())
			continue;
		auto methodDef = typeDef->mMethods[methodIdx];
		if (methodDef->mIsNoReflect)
			continue;
		if (methodDef->mHasComptime)
			continue;

		auto defaultMethod = methodInstanceGroup->mDefault;
		if (defaultMethod == NULL)
			continue;
		if (defaultMethod->mIsUnspecialized)
			continue;
		if (!defaultMethod->mIsReified)
			continue;

		if ((defaultMethod->mChainType == BfMethodChainType_ChainMember) || (defaultMethod->mChainType == BfMethodChainType_ChainSkip))
			continue;
		if (defaultMethod->mMethodDef->mMethodType == BfMethodType_CtorNoBody)
			continue;

		auto methodReflectKind = (BfReflectKind)(reflectKind & ~BfReflectKind_User);

		bool includeMethod = reflectIncludeAllMethods;

		BfMethodCustomAttributes* methodCustomAttributes = NULL;
		if ((defaultMethod->mMethodInfoEx != NULL) && (defaultMethod->mMethodInfoEx->mMethodCustomAttributes != NULL) && (defaultMethod->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes != NULL))
		{
			methodCustomAttributes = defaultMethod->mMethodInfoEx->mMethodCustomAttributes;
			for (auto& customAttr : defaultMethod->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes->mAttributes)
			{
				if (customAttr.mType->mTypeDef->mName->ToString() == "ReflectAttribute")
				{
					includeMethod = true;
				}
				else
				{
					auto userReflectKind = GetUserReflectKind(customAttr.mType);
					methodReflectKind = (BfReflectKind)(methodReflectKind | userReflectKind);
				}
			}
		}

		if ((!mIsComptimeModule) && (!typeInstance->IsTypeMemberAccessible(methodDef->mDeclaringType, mProject)))
			continue;

		//
		{
			SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
			if (auto methodDeclaration = methodDef->GetMethodDeclaration())
			{
				for (BfParameterDeclaration* paramDecl : methodDeclaration->mParams)
				{
					if (paramDecl->mAttributes != NULL)
						methodReflectKind = (BfReflectKind)(methodReflectKind | _GetReflectUserFromDirective(paramDecl->mAttributes, BfAttributeTargets_Parameter));
				}
			}
		}

		if ((methodReflectKind & (BfReflectKind_Methods | BfReflectKind_User)) != 0)
			includeMethod = true;
		if ((methodDef->mIsStatic) && ((methodReflectKind & BfReflectKind_StaticMethods) != 0))
			includeMethod = true;

		if ((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend))
		{
			if ((methodReflectKind & BfReflectKind_Constructors) != 0)
				includeMethod = true;
			if ((methodDef->IsDefaultCtor()) && ((methodReflectKind & BfReflectKind_DefaultConstructor) != 0))
				includeMethod = true;
		}

		if ((!includeMethod) && (typeOptions != NULL))
			includeMethod = ApplyTypeOptionMethodFilters(includeMethod, methodDef, typeOptions);

		if (!includeMethod)
			continue;

		sortedMethodList.Add({ methodDef, methodCustomAttributes });
	}

	auto _GetMethodKind = [](BfMethodDef* methodDef)
	{
		if (methodDef->mMethodType == BfMethodType_Ctor)
			return 0;
		return 1;
	};

	std::sort(sortedMethodList.begin(), sortedMethodList.end(), [_GetMethodKind](const _SortedMethodInfo& lhs, const _SortedMethodInfo& rhs)
		{
			int lhsKind = _GetMethodKind(lhs.mMethodDef);
			int rhsKind = _GetMethodKind(rhs.mMethodDef);

			if (lhsKind != rhsKind)
				return lhsKind < rhsKind;
			if (lhs.mMethodDef->mName != rhs.mMethodDef->mName)
				return lhs.mMethodDef->mName < rhs.mMethodDef->mName;
			return lhs.mMethodDef->mIdx < rhs.mMethodDef->mIdx;
		});

	for (auto& methodInfo : sortedMethodList)
	{
		auto methodDef = methodInfo.mMethodDef;
		int methodIdx = methodDef->mIdx;
		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodIdx];
		auto defaultMethod = methodInstanceGroup->mDefault;

		BfModuleMethodInstance moduleMethodInstance;
		BfIRValue funcVal = voidPtrNull;

		if (((!typeInstance->IsBoxed()) || (!methodDef->mIsStatic)) &&
			(!typeInstance->IsUnspecializedType()) &&
			(methodDef->mMethodType != BfMethodType_Ignore) &&
			(methodDef->mMethodType != BfMethodType_Mixin) &&
			(!methodDef->mIsAbstract) &&
			(methodDef->mGenericParams.size() == 0))
		{
			moduleMethodInstance = GetMethodInstanceAtIdx(typeInstance, methodIdx, NULL, BfGetMethodInstanceFlag_NoInline);
			if (moduleMethodInstance.mFunc)
				funcVal = mBfIRBuilder->CreateBitCast(moduleMethodInstance.mFunc, voidPtrIRType);
		}

		BfIRValue methodNameConst = GetStringObjectValue(methodDef->mName, !mIsComptimeModule);

		BfMethodFlags methodFlags = defaultMethod->GetMethodFlags();

		int customAttrIdx = -1;
		int returnCustomAttrIdx = -1;
		if (methodInfo.mMethodCustomAttributes != NULL)
		{
			customAttrIdx = _HandleCustomAttrs(methodInfo.mMethodCustomAttributes->mCustomAttributes);
			returnCustomAttrIdx = _HandleCustomAttrs(methodInfo.mMethodCustomAttributes->mReturnCustomAttributes);
		}

		enum ParamFlags
		{
			ParamFlag_None = 0,
			ParamFlag_Splat = 1,
			ParamFlag_Implicit = 2,
			ParamFlag_AppendIdx = 4,
			ParamFlag_Params = 8
		};

		SizedArray<BfIRValue, 8> paramVals;
		for (int paramIdx = 0; paramIdx < defaultMethod->GetParamCount(); paramIdx++)
		{
			String paramName = defaultMethod->GetParamName(paramIdx);
			BfType* paramType = defaultMethod->GetParamType(paramIdx);

			ParamFlags paramFlags = ParamFlag_None;
			if (defaultMethod->GetParamIsSplat(paramIdx))
				paramFlags = (ParamFlags)(paramFlags | ParamFlag_Splat);
			if (defaultMethod->GetParamKind(paramIdx) == BfParamKind_AppendIdx)
				paramFlags = (ParamFlags)(paramFlags | ParamFlag_Implicit | ParamFlag_AppendIdx);
			if (defaultMethod->GetParamKind(paramIdx) == BfParamKind_Params)
				paramFlags = (ParamFlags)(paramFlags | ParamFlag_Params);

			BfIRValue paramNameConst = GetStringObjectValue(paramName, !mIsComptimeModule);

			int paramCustomAttrIdx = -1;
			if ((methodInfo.mMethodCustomAttributes != NULL) && (paramIdx < (int)methodInfo.mMethodCustomAttributes->mParamCustomAttributes.mSize))
				paramCustomAttrIdx = _HandleCustomAttrs(methodInfo.mMethodCustomAttributes->mParamCustomAttributes[paramIdx]);

			SizedArray<BfIRValue, 8> paramDataVals =
				{
					emptyValueType,
					paramNameConst,
					GetConstValue(paramType->mTypeId, typeIdType),
					GetConstValue((int32)paramFlags, shortType),
					GetConstValue(-1, intType), // defaultIdx
					GetConstValue(paramCustomAttrIdx, intType) // customAttrIdx
				};
			auto paramData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapType(reflectParamDataType, BfIRPopulateType_Full), paramDataVals);
			paramVals.Add(paramData);
		}

		BfIRValue paramsVal;
		if (paramVals.size() > 0)
		{
			BfIRType paramDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(reflectParamDataType, BfIRPopulateType_Full), (int)paramVals.size());
			BfIRValue paramDataConst = mBfIRBuilder->CreateConstAgg_Value(paramDataArrayType, paramVals);

			BfIRValue paramDataArray = mBfIRBuilder->CreateGlobalVariable(paramDataArrayType, true, BfIRLinkageType_Internal,
				paramDataConst, typeDataName + StrFormat(".params%d", methodIdx));
			paramsVal = mBfIRBuilder->CreateBitCast(paramDataArray, mBfIRBuilder->MapType(reflectParamDataPtrType));
		}
		else
			paramsVal = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(reflectParamDataPtrType));

		int vDataVal = -1;
		if (defaultMethod->mVirtualTableIdx != -1)
		{
			int vDataIdx = -1;

			if (type->IsInterface())
			{
				vDataIdx = defaultMethod->mVirtualTableIdx;
			}
			else
			{
				vDataIdx = 1 + mCompiler->GetDynCastVDataCount() + mCompiler->mMaxInterfaceSlots;
				if ((mCompiler->mOptions.mHasVDataExtender) && (mCompiler->IsHotCompile()))
				{
					auto typeInst = defaultMethod->mMethodInstanceGroup->mOwner;

					int extMethodIdx = (defaultMethod->mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) - typeInst->GetOrigSelfVTableSize();
					if (extMethodIdx >= 0)
					{
						// Extension?
						int vExtOfs = typeInst->GetOrigImplBaseVTableSize();
						vDataVal = ((vDataIdx + vExtOfs + 1) << 20) | (extMethodIdx);
					}
					else
					{
						// Map this new virtual index back to the original index
						vDataIdx += (defaultMethod->mVirtualTableIdx - typeInst->GetImplBaseVTableSize()) + typeInst->GetOrigImplBaseVTableSize();
					}
				}
				else
				{
					vDataIdx += defaultMethod->mVirtualTableIdx;
				}
			}
			if (vDataVal == -1)
				vDataVal = vDataIdx * mSystem->mPtrSize;
		}

		SizedArray<BfIRValue, 8> methodDataVals =
			{
				emptyValueType,
				methodNameConst, // mName
				funcVal, // mFuncPtr
				paramsVal,
				GetConstValue(defaultMethod->mReturnType->mTypeId, typeIdType),
				GetConstValue((int)paramVals.size(), shortType),
				GetConstValue(methodFlags, shortType),
				GetConstValue(methodIdx, intType),
				GetConstValue(vDataVal, intType),
				GetConstValue(customAttrIdx, intType),
				GetConstValue(returnCustomAttrIdx, intType),
			};
		auto methodData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectMethodDataType->ToTypeInstance(), BfIRPopulateType_Full), methodDataVals);
		methodTypes.push_back(methodData);
	}

	BfIRValue methodDataPtr;
	BfIRType methodDataPtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(reflectMethodDataType));
	if (methodTypes.size() == 0)
		methodDataPtr = mBfIRBuilder->CreateConstNull(methodDataPtrType);
	else
	{
		BfIRType methodDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(reflectMethodDataType, BfIRPopulateType_Full), (int)methodTypes.size());
		BfIRValue methodDataConst = mBfIRBuilder->CreateConstAgg_Value(methodDataArrayType, methodTypes);
		BfIRValue methodDataArray = mBfIRBuilder->CreateGlobalVariable(methodDataArrayType, true, BfIRLinkageType_Internal,
			methodDataConst, "methods." + typeDataName);
		methodDataPtr = mBfIRBuilder->CreateBitCast(methodDataArray, methodDataPtrType);
	}

	/////

	int interfaceCount = 0;
	BfIRValue interfaceDataPtr;
	BfType* reflectInterfaceDataType = ResolveTypeDef(mCompiler->mReflectInterfaceDataDef);
	BfIRType interfaceDataPtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(reflectInterfaceDataType));
	Array<bool> wantsIfaceMethod;
	bool wantsIfaceMethods = false;
	if (typeInstance->mInterfaces.IsEmpty())
		interfaceDataPtr = mBfIRBuilder->CreateConstNull(interfaceDataPtrType);
	else
	{
		SizedArray<BfIRValue, 16> interfaces;
		for (auto& interface : typeInstance->mInterfaces)
		{
			SizedArray<BfIRValue, 8> interfaceDataVals =
			{
				emptyValueType,
				GetConstValue(interface.mInterfaceType->mTypeId, typeIdType),
				GetConstValue(interface.mStartInterfaceTableIdx, intType),
				GetConstValue(interface.mStartVirtualIdx, intType),
			};

			auto interfaceData = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapTypeInst(reflectInterfaceDataType->ToTypeInstance(), BfIRPopulateType_Full), interfaceDataVals);
			interfaces.push_back(interfaceData);

			if (!mIsComptimeModule)
			{
				for (int methodIdx = 0; methodIdx < (int)interface.mInterfaceType->mMethodInstanceGroups.size(); methodIdx++)
				{
					auto ifaceMethodGroup = &interface.mInterfaceType->mMethodInstanceGroups[methodIdx];
					if (ifaceMethodGroup->mExplicitlyReflected)
					{
						wantsIfaceMethods = true;
						int tableIdx = interface.mStartInterfaceTableIdx + methodIdx;
						while (tableIdx >= wantsIfaceMethod.size())
							wantsIfaceMethod.Add(false);
						wantsIfaceMethod[tableIdx] = true;
					}
				}
			}
		}

		BfIRType interfaceDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(reflectInterfaceDataType, BfIRPopulateType_Full), (int)interfaces.size());
		BfIRValue interfaceDataConst = mBfIRBuilder->CreateConstAgg_Value(interfaceDataArrayType, interfaces);
		BfIRValue interfaceDataArray = mBfIRBuilder->CreateGlobalVariable(interfaceDataArrayType, true, BfIRLinkageType_Internal,
			interfaceDataConst, "interfaces." + typeDataName);
		interfaceDataPtr = mBfIRBuilder->CreateBitCast(interfaceDataArray, interfaceDataPtrType);
		interfaceCount = (int)interfaces.size();
	}

	BfIRValue interfaceMethodTable;
	int ifaceMethodTableSize = 0;
	if ((wantsIfaceMethods) && (!typeInstance->IsInterface()) && (typeInstance->mIsReified) && (!typeInstance->IsUnspecializedType())
		&& (!typeInstance->mInterfaceMethodTable.IsEmpty()))
	{
		SizedArray<BfIRValue, 16> methods;
		for (int tableIdx = 0; tableIdx < (int)typeInstance->mInterfaceMethodTable.size(); tableIdx++)
		{
			BfIRValue funcVal = voidPtrNull;
			if ((tableIdx < (int)wantsIfaceMethod.size()) && (wantsIfaceMethod[tableIdx]))
			{
				auto methodEntry = typeInstance->mInterfaceMethodTable[tableIdx];
				if (!methodEntry.mMethodRef.IsNull())
				{
					BfMethodInstance* methodInstance = methodEntry.mMethodRef;
					if ((methodInstance->mIsReified) && (methodInstance->mMethodInstanceGroup->IsImplemented()) && (!methodInstance->mIsUnspecialized) &&
						(!methodInstance->mMethodDef->mIsAbstract))
					{
						auto moduleMethodInstance = ReferenceExternalMethodInstance(methodInstance);
						if (moduleMethodInstance.mFunc)
							funcVal = mBfIRBuilder->CreateBitCast(moduleMethodInstance.mFunc, voidPtrIRType);
					}
				}
			}
			methods.Add(funcVal);
		}

 		while ((!methods.IsEmpty()) && (methods.back() == voidPtrNull))
 			methods.pop_back();

		if (!methods.IsEmpty())
		{
			BfIRType methodDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(voidPtrType, BfIRPopulateType_Full), (int)methods.size());
			BfIRValue methodDataConst = mBfIRBuilder->CreateConstAgg_Value(methodDataArrayType, methods);
			BfIRValue methodDataArray = mBfIRBuilder->CreateGlobalVariable(methodDataArrayType, true, BfIRLinkageType_Internal,
				methodDataConst, "imethods." + typeDataName);
			interfaceMethodTable = mBfIRBuilder->CreateBitCast(methodDataArray, voidPtrPtrIRType);
			ifaceMethodTableSize = (int)methods.size();
		}
	}
	if (!interfaceMethodTable)
		interfaceMethodTable = mBfIRBuilder->CreateConstNull(voidPtrPtrIRType);

	/////

	int underlyingType = 0;
	if ((typeInstance->IsTypedPrimitive()) || (typeInstance->IsBoxed()))
	{
		underlyingType = typeInstance->GetUnderlyingType()->mTypeId;
	}

	int outerTypeId = 0;
	auto outerType = mContext->mUnreifiedModule->GetOuterType(typeInstance);
	if (outerType != NULL)
	{
		outerTypeId = outerType->mTypeId;
	}

	//

	BfIRValue customAttrDataPtr;
	if (customAttrs.size() > 0)
	{
		BfIRType customAttrsArrayType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)customAttrs.size());
		BfIRValue customAttrsConst =  mBfIRBuilder->CreateConstAgg_Value(customAttrsArrayType, customAttrs);
		BfIRValue customAttrsArray = mBfIRBuilder->CreateGlobalVariable(customAttrsArrayType, true, BfIRLinkageType_Internal,
			customAttrsConst, "customAttrs." + typeDataName);
		customAttrDataPtr = mBfIRBuilder->CreateBitCast(customAttrsArray, voidPtrPtrIRType);
	}
	else
	{
		customAttrDataPtr = mBfIRBuilder->CreateConstNull(voidPtrPtrIRType);
	}

	SizedArray<BfIRValue, 32> typeDataVals =
		{
			typeData,

			castedClassVData, // mTypeClassVData
			typeNameConst, // mName
			namespaceConst, // mNamespace
			GetConstValue(typeInstance->mInstSize, intType), // mInstSize
			GetConstValue(typeInstance->mInstAlign, intType), // mInstAlign
			GetConstValue(typeCustomAttrIdx, intType), // mCustomAttributes
			GetConstValue(baseTypeId, typeIdType), // mBaseType
			GetConstValue(underlyingType, typeIdType), // mUnderlyingType
			GetConstValue(outerTypeId, typeIdType), // mOuterType
			GetConstValue(typeInstance->mInheritanceId, intType), // mInheritanceId
			GetConstValue(typeInstance->mInheritanceCount, intType), // mInheritanceCount

			GetConstValue(typeInstance->mSlotNum, byteType), // mInterfaceSlot
			GetConstValue(interfaceCount, byteType), // mInterfaceCount
			GetConstValue(ifaceMethodTableSize, shortType), // mInterfaceMethodCount
			GetConstValue((int)methodTypes.size(), shortType), // mMethodDataCount
			GetConstValue(0, shortType), // mPropertyDataCount
			GetConstValue((int)fieldTypes.size(), shortType), // mFieldDataCount

			interfaceDataPtr, // mInterfaceDataPtr
			interfaceMethodTable, // mInterfaceMethodTable
			methodDataPtr, // mMethodDataPtr
			voidPtrNull, // mPropertyDataPtr
			fieldDataPtr, // mFieldDataPtr

			customAttrDataPtr, // mCustomAttrDataPtr
		};

	BfIRType typeInstanceDataType = mBfIRBuilder->MapTypeInst(typeInstanceType->ToTypeInstance(), BfIRPopulateType_Full);
	FixConstValueParams(typeInstanceType->ToTypeInstance(), typeDataVals);
	auto typeInstanceData = mBfIRBuilder->CreateConstAgg_Value(typeInstanceDataType, typeDataVals);

	if (!needsTypeData)
	{
		// No need for anything beyond typeInstanceData
	}
	else if ((typeInstance->IsGenericTypeInstance()) && (typeInstance->IsUnspecializedType()))
	{
		auto genericTypeInstance = typeInstance->ToGenericTypeInstance();

		SizedArray<BfIRValue, 4> unspecializedData =
		{
			typeInstanceData,
			GetConstValue((int)genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments.size(), byteType), // mGenericParamCount
		};
		auto reflectUnspecializedGenericType = ResolveTypeDef(mCompiler->mReflectUnspecializedGenericType);
		typeInstanceDataType = mBfIRBuilder->MapTypeInst(reflectUnspecializedGenericType->ToTypeInstance(), BfIRPopulateType_Full);
		typeInstanceData = mBfIRBuilder->CreateConstAgg_Value(typeInstanceDataType, unspecializedData);
	}
	else if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInstance = typeInstance->ToGenericTypeInstance();
		auto reflectSpecializedGenericType = ResolveTypeDef(mCompiler->mReflectSpecializedGenericType);
		auto unspecializedType = ResolveTypeDef(typeInstance->mTypeDef->GetDefinition());

		SizedArray<BfIRValue, 4> resolvedTypes;
		for (auto typeGenericArg : genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments)
			resolvedTypes.push_back(GetConstValue(typeGenericArg->mTypeId, typeIdType));

		auto typeIRType = mBfIRBuilder->MapType(typeIdType);
		auto typePtrIRType = mBfIRBuilder->GetPointerTo(typeIRType);
		auto genericArrayType = mBfIRBuilder->GetSizedArrayType(typeIRType, (int)resolvedTypes.size());
		BfIRValue resolvedTypesConst = mBfIRBuilder->CreateConstAgg_Value(genericArrayType, resolvedTypes);
		BfIRValue resolvedTypesArray = mBfIRBuilder->CreateGlobalVariable(genericArrayType, true, BfIRLinkageType_Internal,
			resolvedTypesConst, "resolvedTypes." + typeDataName);
		BfIRValue resovledTypesPtr = mBfIRBuilder->CreateBitCast(resolvedTypesArray, typePtrIRType);

		SizedArray<BfIRValue, 3> specGenericData  =
			{
				typeInstanceData,
				GetConstValue(unspecializedType->mTypeId, typeIdType), // mUnspecialziedType
				resovledTypesPtr, // mFieldDataPtr
			};

		typeInstanceDataType = mBfIRBuilder->MapTypeInst(reflectSpecializedGenericType->ToTypeInstance(), BfIRPopulateType_Full);
		typeInstanceData = mBfIRBuilder->CreateConstAgg_Value(typeInstanceDataType, specGenericData);

		if (typeInstance->IsArray())
		{
			auto arrayType = (BfArrayType*)typeInstance;

			BfType* elementType = genericTypeInstance->mGenericTypeInfo->mTypeGenericArguments[0];
			BfFieldInstance* elementFieldInstance = &genericTypeInstance->mFieldInstances[0];

			SizedArray<BfIRValue, 4> arrayData  =
			{
				typeInstanceData,
				GetConstValue(elementType->mSize, intType), // mElementSize
				GetConstValue(1, byteType), // mRank
				GetConstValue(elementFieldInstance->mDataOffset, byteType), // mElementsDataOffset
			};
			auto reflectArrayType = ResolveTypeDef(mCompiler->mReflectArrayType);
			typeInstanceDataType = mBfIRBuilder->MapTypeInst(reflectArrayType->ToTypeInstance(), BfIRPopulateType_Full);
			typeInstanceData = mBfIRBuilder->CreateConstAgg_Value(typeInstanceDataType, arrayData);
		}
	}

	BfIRValue typeDataVar;
	if (needsTypeData)
	{
		typeDataVar = mBfIRBuilder->CreateGlobalVariable(typeInstanceDataType, true,
			BfIRLinkageType_External, typeInstanceData, typeDataName);
		mBfIRBuilder->GlobalVar_SetAlignment(typeDataVar, mSystem->mPtrSize);

		if (mBfIRBuilder->DbgHasInfo())
		{
			mBfIRBuilder->DbgCreateGlobalVariable(mDICompileUnit, typeDataName, typeDataName, BfIRMDNode(), 0, mBfIRBuilder->DbgGetTypeInst(typeInstanceType->ToTypeInstance()), false, typeDataVar);
		}
	}
	else
	{
		typeDataVar = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(mContext->mBfTypeType));
	}
	typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));

	mTypeDataRefs[typeInstance] = typeDataVar;

	if ((!mIsComptimeModule) && (classVDataVar))
	{
		BF_ASSERT(!classVDataName.IsEmpty());

		vData[0] = mBfIRBuilder->CreateBitCast(typeDataVar, voidPtrIRType);
		auto classVDataConstDataType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)vData.size());
		auto classVDataConstData = mBfIRBuilder->CreateConstAgg_Value(classVDataConstDataType, vData);

		mBfIRBuilder->GlobalVar_SetInitializer(classVDataVar, classVDataConstData);
		if (mCompiler->mOptions.mObjectHasDebugFlags)
			mBfIRBuilder->GlobalVar_SetAlignment(classVDataVar, 256);
		else
			mBfIRBuilder->GlobalVar_SetAlignment(classVDataVar, mContext->mBfClassVDataPtrType->mAlign);

		if (mBfIRBuilder->DbgHasInfo())
		{
			BfType* voidType = GetPrimitiveType(BfTypeCode_None);
			BfType* voidPtrType = CreatePointerType(voidType);

			BfType* classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);
			BfIRMDNode arrayType = mBfIRBuilder->DbgCreateArrayType(vData.size() * mSystem->mPtrSize * 8, mSystem->mPtrSize * 8, mBfIRBuilder->DbgGetType(voidPtrType), vData.size());
			mBfIRBuilder->DbgCreateGlobalVariable(mDICompileUnit, classVDataName, classVDataName, BfIRMDNode(), 0, arrayType, false, classVDataVar);
		}
    }

	return typeDataVar;
}

BfIRValue BfModule::FixClassVData(BfIRValue value)
{
	if ((!mCompiler->mOptions.mObjectHasDebugFlags) || (mIsComptimeModule))
		return value;
	auto intptrValue = mBfIRBuilder->CreatePtrToInt(value, BfTypeCode_IntPtr);
	auto maskedValue = mBfIRBuilder->CreateAnd(intptrValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)~0xFFULL));
	return mBfIRBuilder->CreateIntToPtr(maskedValue, mBfIRBuilder->GetType(value));
}

void BfModule::CheckStaticAccess(BfTypeInstance* typeInstance)
{
	// Note: this is not just for perf, it fixes a field var-type resolution issue
	if (mBfIRBuilder->mIgnoreWrites)
		return;
	if (mIsComptimeModule)
		return;

	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	//TODO: Create a hashset of these, we don't need to repeatedly call static ctors for a given type

	// When we access classes with static constructors FROM a static constructor,
	//  we call that other class's static constructor first.  Recursion cannot occur, since
	//  we simply ignore subsequent attempts to re-enter the constructor
	if (!typeInstance->mHasStaticInitMethod)
		return;
	if ((mCurMethodInstance == NULL) || ((mCurMethodInstance->mMethodDef->mMethodType != BfMethodType_Ctor) || (!mCurMethodInstance->mMethodDef->mIsStatic)))
		return;
	if (mCurMethodInstance->mMethodInstanceGroup->mOwner == typeInstance)
		return;

	auto checkTypeDef = typeInstance->mTypeDef;
	checkTypeDef->PopulateMemberSets();
	BfMethodDef* nextMethodDef = NULL;
	BfMemberSetEntry* entry;
	if (checkTypeDef->mMethodSet.TryGetWith(String("__BfStaticCtor"), &entry))
		nextMethodDef = (BfMethodDef*)entry->mMemberDef;
	while (nextMethodDef != NULL)
	{
		auto checkMethod = nextMethodDef;
		nextMethodDef = nextMethodDef->mNextWithSameName;
		auto methodModule = GetMethodInstance(typeInstance, checkMethod, BfTypeVector());
		if (methodModule)
		{
			auto methodInstance = methodModule.mMethodInstance;
			if ((methodInstance != NULL) &&
				(methodInstance->mMethodDef->mIsStatic) &&
				(methodInstance->mMethodDef->mMethodType == BfMethodType_Ctor) &&
				((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
			{
				mBfIRBuilder->CreateCall(methodModule.mFunc, SizedArray<BfIRValue, 0>());
				break;
			}
		}
	}
}

BfIRFunction BfModule::GetIntrinsic(BfMethodInstance* methodInstance, bool reportFailure)
{
	auto methodOwner = methodInstance->GetOwner();
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();

	if (!methodDef->mIsExtern)
		return BfIRFunction();

	if (methodInstance->GetCustomAttributes() == NULL)
		return BfIRFunction();

	for (auto& customAttribute : methodInstance->GetCustomAttributes()->mAttributes)
	{
		String typeName = TypeToString(customAttribute.mType);
		if ((typeName == "System.IntrinsicAttribute") && (customAttribute.mCtorArgs.size() > 0))
		{
			methodInstance->mIsIntrinsic = true;

			auto constant = methodOwner->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);
			String error;

			if ((constant != NULL) && (constant->mTypeCode == BfTypeCode_StringId))
			{
				int stringId = constant->mInt32;
				auto entry = mContext->mStringObjectIdMap[stringId];
				String intrinName = entry.mString;

// 				if (intrinName.StartsWith(":"))
// 				{
// 					SizedArray<BfIRType, 2> paramTypes;
// 					for (auto& param : methodInstance->mParams)
// 						paramTypes.push_back(mBfIRBuilder->MapType(param.mResolvedType));
// 					return mBfIRBuilder->GetIntrinsic(intrinName.Substring(1), mBfIRBuilder->MapType(methodInstance->mReturnType), paramTypes);
// 				}
// 				else
				{
					int intrinId = BfIRCodeGen::GetIntrinsicId(intrinName);
					if (intrinId != -1)
					{
						if (intrinId == BfIRIntrinsic_Malloc)
						{
							return GetBuiltInFunc(BfBuiltInFuncType_Malloc);
						}
						else if (intrinId == BfIRIntrinsic_Free)
						{
							return GetBuiltInFunc(BfBuiltInFuncType_Free);
						}

						SizedArray<BfIRType, 2> paramTypes;
						for (auto& param : methodInstance->mParams)
							paramTypes.push_back(mBfIRBuilder->MapType(param.mResolvedType));
						return mBfIRBuilder->GetIntrinsic(intrinName, intrinId, mBfIRBuilder->MapType(methodInstance->mReturnType), paramTypes);
					}
					else if (reportFailure)
						error = StrFormat("Unable to find intrinsic '%s'", entry.mString.c_str());
				}
			}
			else if (reportFailure)
				error = "Intrinsic name must be a constant string";

			if (reportFailure)
			{
				Fail(error, customAttribute.mRef);
			}
		}
	}

	return BfIRFunction();
}

BfIRFunction BfModule::GetBuiltInFunc(BfBuiltInFuncType funcTypeId)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return mBfIRBuilder->GetFakeFunction();

	if (!mBuiltInFuncs[(int)funcTypeId])
	{
		SizedArray<BfIRType, 4> paramTypes;
		auto nullPtrType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr));
		auto intType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_IntPtr));
		auto int32Type = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_Int32));
		auto intPtrType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_IntPtr));
		auto byteType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_Int8));
		auto voidType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_None));
		auto objType = mBfIRBuilder->MapType(mContext->mBfObjectType);

		BfIRFunctionType funcType;
		BfIRFunction func;

		switch (funcTypeId)
		{
		case BfBuiltInFuncType_PrintF:
			paramTypes.Add(nullPtrType);
			funcType = mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, true);
			func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, "PrintF");
			break;
		case BfBuiltInFuncType_Malloc:
			{
				if ((mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
				{
					func = GetInternalMethod("Dbg_RawAlloc", 1).mFunc;
				}
				else
				{
					String funcName = mCompiler->mOptions.mMallocLinkName;
					if ((funcName.IsEmpty()) || (mIsComptimeModule))
						funcName = "malloc";
					func = mBfIRBuilder->GetFunction(funcName);
					if (!func)
					{
						paramTypes.clear();
						paramTypes.push_back(intType);
						funcType = mBfIRBuilder->CreateFunctionType(nullPtrType, paramTypes);
						func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, funcName);
						mBfIRBuilder->Func_SetParamName(func, 1, "size");
					}
				}
			}
			break;
		case BfBuiltInFuncType_Free:
		{
				if ((mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
				{
					func = GetInternalMethod("Dbg_RawFree").mFunc;
				}
				else
				{
					String funcName = mCompiler->mOptions.mFreeLinkName;
					if ((funcName.IsEmpty()) || (mIsComptimeModule))
						funcName = "free";
					func = mBfIRBuilder->GetFunction(funcName);
					if (!func)
					{
						paramTypes.clear();
						paramTypes.push_back(nullPtrType);
						funcType = mBfIRBuilder->CreateFunctionType(voidType, paramTypes);
						func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, funcName);
						mBfIRBuilder->Func_SetParamName(func, 1, "ptr");
					}
				}
			}
			break;
		case BfBuiltInFuncType_LoadSharedLibraries:
			paramTypes.clear();
			funcType = mBfIRBuilder->CreateFunctionType(voidType, paramTypes);
			func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, "BfLoadSharedLibraries");
			break;
		default: break;
		}

		mBuiltInFuncs[(int)funcTypeId] = func;
		return func;
	}

	return mBuiltInFuncs[(int)funcTypeId];
}

void BfModule::ResolveGenericParamConstraints(BfGenericParamInstance* genericParamInstance, bool isUnspecialized, Array<BfTypeReference*>* deferredResolveTypes)
{
	BfGenericParamDef* genericParamDef = genericParamInstance->GetGenericParamDef();
	BfExternalConstraintDef* externConstraintDef = genericParamInstance->GetExternConstraintDef();
	BfConstraintDef* constraintDef = genericParamInstance->GetConstraintDef();

	BfType* startingTypeConstraint = genericParamInstance->mTypeConstraint;

	BfAutoComplete* bfAutocomplete = NULL;
	if ((mCompiler->mResolvePassData != NULL) && (isUnspecialized))
		bfAutocomplete = mCompiler->mResolvePassData->mAutoComplete;

	if ((bfAutocomplete != NULL) && (genericParamDef != NULL))
	{
		for (int nameIdx = 0; nameIdx < (int)genericParamDef->mNameNodes.size(); nameIdx++)
		{
			auto nameNode = genericParamDef->mNameNodes[nameIdx];
			if (bfAutocomplete->IsAutocompleteNode(nameNode))
			{
				String filter = nameNode->ToString();
				bfAutocomplete->mInsertStartIdx = nameNode->GetSrcStart();
				bfAutocomplete->mInsertEndIdx = nameNode->GetSrcEnd();

				if (nameIdx != 0)
				{
					bfAutocomplete->AddEntry(AutoCompleteEntry("generic", nameNode->ToString().c_str()), filter);
				}
			}
		}
	}

	for (auto constraint : constraintDef->mConstraints)
	{
		if (auto opConstraint = BfNodeDynCast<BfGenericOperatorConstraint>(constraint))
		{
			BfGenericOperatorConstraintInstance opConstraintInstance;

			if (opConstraint->mLeftType != NULL)
			{
				if (bfAutocomplete != NULL)
					bfAutocomplete->CheckTypeRef(opConstraint->mLeftType, false);
				opConstraintInstance.mLeftType = ResolveTypeRef(opConstraint->mLeftType, BfPopulateType_Interfaces_All);
				if (opConstraintInstance.mLeftType == NULL)
					continue;
			}

			if (opConstraint->mRightType == NULL)
			{
				// We had a failure in parsing
				continue;
			}

			if (opConstraint->mRightType != NULL)
			{
				if (bfAutocomplete != NULL)
					bfAutocomplete->CheckTypeRef(opConstraint->mRightType, false);
				opConstraintInstance.mRightType = ResolveTypeRef(opConstraint->mRightType, BfPopulateType_Interfaces_All);
				if (opConstraintInstance.mRightType == NULL)
					continue;
			}

			if (opConstraint->mOpToken == NULL)
			{
				FailAfter("Missing operator", (opConstraint->mLeftType != NULL) ? (BfAstNode*)opConstraint->mLeftType : (BfAstNode*)opConstraint->mOperatorToken);
				continue;
			}

			if (opConstraint->mLeftType != NULL)
			{
				if (opConstraint->mRightType == NULL)
				{
					// Parse should have failed
					continue;
				}

				opConstraintInstance.mBinaryOp = BfTokenToBinaryOp(opConstraint->mOpToken->mToken);
				if (opConstraintInstance.mBinaryOp == BfBinaryOp_None)
				{
					Fail("Invalid binary operator", opConstraint->mOpToken);
					continue;
				}
			}
			else if ((opConstraint->mOpToken->mToken == BfToken_Implicit) || (opConstraint->mOpToken->mToken == BfToken_Explicit))
			{
				opConstraintInstance.mCastToken = opConstraint->mOpToken->mToken;
			}
			else
			{
				opConstraintInstance.mUnaryOp = BfTokenToUnaryOp(opConstraint->mOpToken->mToken);
				if (opConstraintInstance.mUnaryOp == BfUnaryOp_None)
				{
					Fail("Invalid unary operator", opConstraint->mOpToken);
					continue;
				}
			}

			genericParamInstance->mOperatorConstraints.Add(opConstraintInstance);

			continue;
		}

		auto constraintTypeRef = BfNodeDynCast<BfTypeReference>(constraint);

		if (bfAutocomplete != NULL)
			bfAutocomplete->CheckTypeRef(constraintTypeRef, true);
		//TODO: Constraints may refer to other generic params (of either type or method)
		//  TO allow resolution, perhaps move this generic param initialization into GetMethodInstance (passing a genericPass bool)

		BfResolveTypeRefFlags resolveFlags = BfResolveTypeRefFlag_AllowGenericMethodParamConstValue;
		if (isUnspecialized)
			resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_DisallowComptime);
		// We we have a deferredResolveTypes then we defer the generic validation, because we may have a case like
		//  `where T : Dictionay<TElem, int> and TElem : IHashable` and we don't want to throw the error on `T` before we build `TElem`
		auto constraintType = ResolveTypeRef(constraintTypeRef, (deferredResolveTypes != NULL) ? BfPopulateType_Identity : BfPopulateType_Declaration, resolveFlags);
		if (constraintType != NULL)
		{
			if (deferredResolveTypes != NULL)
			{
				PopulateType(constraintType, BfPopulateType_Declaration);
				if (constraintType->IsUnspecializedTypeVariation())
					deferredResolveTypes->Add(constraintTypeRef);
			}

			if ((constraintDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
			{
				bool isValidTypeCode = false;
				BfTypeCode typeCode = BfTypeCode_None;

				if (constraintType->IsTypedPrimitive())
				{
					auto underlyingType = constraintType->GetUnderlyingType();
					if (underlyingType->IsPrimitiveType())
						typeCode = ((BfPrimitiveType*)underlyingType)->mTypeDef->mTypeCode;
				}
				if (constraintType->IsPrimitiveType())
					typeCode = ((BfPrimitiveType*)constraintType)->mTypeDef->mTypeCode;

				if (constraintType->IsInstanceOf(mCompiler->mStringTypeDef))
					isValidTypeCode = true;

				switch (typeCode)
				{
				case BfTypeCode_StringId:
				case BfTypeCode_Boolean:
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
				case BfTypeCode_Float:
				case BfTypeCode_Double:
				case BfTypeCode_Char8:
				case BfTypeCode_Char16:
				case BfTypeCode_Char32:
					isValidTypeCode = true;
					break;
				default: break;
				}

				if (isValidTypeCode)
				{
					genericParamInstance->mTypeConstraint = constraintType;
				}
				else
				{
					Fail("Const constraint must be a primitive type", constraintTypeRef);
				}
			}
			else
			{
				bool checkEquality = false;

				if (constraintType->IsVar())
				{
					// From a `comptype` generic undef resolution. Ignore.
					genericParamInstance->mGenericParamFlags = (BfGenericParamFlags)(genericParamInstance->mGenericParamFlags | BfGenericParamFlag_ComptypeExpr);
					genericParamInstance->mComptypeConstraint.Add(constraintTypeRef);
					continue;
				}
				else if (constraintType->IsPrimitiveType())
				{
					if (isUnspecialized)
					{
						Fail("Primitive constraints are not allowed unless preceded with 'const'", constraintTypeRef);
						continue;
					}
					checkEquality = true;
				}

				if (constraintType->IsArray())
				{
					if (isUnspecialized)
					{
						Fail("Array constraints are not allowed.  If a constant-sized array was intended, an type parameterized by a const generic param can be used (ie: where T : int[T2] where T2 : const int)", constraintTypeRef);
						continue;
					}
					checkEquality = true;
				}

				if (constraintType->IsGenericParam())
				{
					checkEquality = true;
				}
				else if ((!constraintType->IsTypeInstance()) && (!constraintType->IsSizedArray()))
				{
					if (isUnspecialized)
					{
						Fail(StrFormat("Type '%s' is not allowed as a generic constraint", TypeToString(constraintType).c_str()), constraintTypeRef);
						continue;
					}
					checkEquality = true;
				}

				if (checkEquality)
				{
					genericParamInstance->mTypeConstraint = constraintType;
				}
				else if (constraintType->IsInterface())
				{
					genericParamInstance->mInterfaceConstraints.push_back(constraintType->ToTypeInstance());
				}
				else
				{
					auto constraintTypeInst = constraintType->ToTypeInstance();
					if (genericParamInstance->mTypeConstraint != NULL)
					{
						if ((constraintTypeInst != NULL) && (TypeIsSubTypeOf(constraintTypeInst, genericParamInstance->mTypeConstraint->ToTypeInstance(), false)))
						{
							// Allow more specific type
							genericParamInstance->mTypeConstraint = constraintTypeInst;
						}
						else if ((constraintTypeInst != NULL) && (TypeIsSubTypeOf(genericParamInstance->mTypeConstraint->ToTypeInstance(), constraintTypeInst, false)))
						{
							// Silently ignore less-specific type
							if ((startingTypeConstraint != NULL) && (startingTypeConstraint->IsDelegate()))
							{
								// 'System.Delegate' means that we are expecting an actual delegate instance.  Simulate this by wanting a class.
								genericParamInstance->mGenericParamFlags = (BfGenericParamFlags)(genericParamInstance->mGenericParamFlags | BfGenericParamFlag_Class);
							}
						}
						else
						{
							Fail("Only one concrete type constraint may be specified", constraintTypeRef);
							return;
						}
					}
					else
						genericParamInstance->mTypeConstraint = constraintType;
				}
			}
		}
	}

	if (((constraintDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0) &&
		(genericParamInstance->mTypeConstraint == NULL))
		genericParamInstance->mTypeConstraint = GetPrimitiveType(BfTypeCode_IntPtr);
}

String BfModule::GenericParamSourceToString(const BfGenericParamSource & genericParamSource)
{
	if (genericParamSource.mMethodInstance != NULL)
	{
		//auto methodInst = GetUnspecializedMethodInstance(genericParamSource.mMethodInstance, false);
		//SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mCurMethodInstance, methodInst);
		return MethodToString(genericParamSource.mMethodInstance);
	}
	else
	{
		auto typeInst = GetUnspecializedTypeInstance(genericParamSource.mTypeInstance);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mCurMethodInstance, NULL);
		SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mCurTypeInstance, typeInst);
		return TypeToString(typeInst);
	}
}

bool BfModule::CheckGenericConstraints(const BfGenericParamSource& genericParamSource, BfType* checkArgType, BfAstNode* checkArgTypeRef, BfGenericParamInstance* genericParamInst, BfTypeVector* methodGenericArgs, BfError** errorOut)
{
	Array<String> methodParamNameOverrides;
	auto _TypeToString = [&](BfType* type)
	{
		if (genericParamSource.mMethodInstance != NULL)
		{
			if (methodParamNameOverrides.IsEmpty())
			{
				for (auto genericParam : genericParamSource.mMethodInstance->mMethodDef->mGenericParams)
					methodParamNameOverrides.Add(genericParam->mName);
			}
			return TypeToString(type, &methodParamNameOverrides);
		}
		return TypeToString(type);
	};

	bool ignoreErrors = (errorOut == NULL) ||
		((genericParamSource.mMethodInstance == NULL) && (genericParamSource.mTypeInstance == NULL));

	BfType* origCheckArgType = checkArgType;

	if (origCheckArgType->IsRef())
		origCheckArgType = origCheckArgType->GetUnderlyingType();

	bool argIsReferenceType = false;

	int checkGenericParamFlags = 0;
	if (checkArgType->IsGenericParam())
	{
		BfGenericParamInstance* checkGenericParamInst = GetGenericParamInstance((BfGenericParamType*)checkArgType);
		checkGenericParamFlags = checkGenericParamInst->mGenericParamFlags;
		if (checkGenericParamInst->mTypeConstraint != NULL)
			checkArgType = checkGenericParamInst->mTypeConstraint;

// 		if ((checkGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr)) != 0)
// 		{
// 			argMayBeReferenceType = false;
// 		}
// 		else
// 		{
// 			argMayBeReferenceType = true;
// 		}
	}

	if (checkArgType->IsObjectOrInterface())
		argIsReferenceType = true;

	BfTypeInstance* typeConstraintInst = NULL;
	if (genericParamInst->mTypeConstraint != NULL)
		typeConstraintInst = genericParamInst->mTypeConstraint->ToTypeInstance();

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Struct) &&
		((checkGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_Enum | BfGenericParamFlag_Var)) == 0) && (!checkArgType->IsValueType()))
	{
		if ((!ignoreErrors) && (PreFail()))
			*errorOut = Fail(StrFormat("The type '%s' must be a value type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_StructPtr) &&
		((checkGenericParamFlags & (BfGenericParamFlag_StructPtr | BfGenericParamFlag_Var)) == 0) && (!checkArgType->IsPointer()))
	{
		if ((!ignoreErrors) && (PreFail()))
			*errorOut = Fail(StrFormat("The type '%s' must be a pointer type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Class) &&
		((checkGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_Var)) == 0) && (!argIsReferenceType))
	{
		if ((!ignoreErrors) && (PreFail()))
			*errorOut = Fail(StrFormat("The type '%s' must be a reference type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if (genericParamInst->mGenericParamFlags & BfGenericParamFlag_Enum)
	{
		bool isEnum = checkArgType->IsEnum();
		if ((origCheckArgType->IsGenericParam()) && (checkArgType->IsInstanceOf(mCompiler->mEnumTypeDef)))
			isEnum = true;
		if (((checkGenericParamFlags & (BfGenericParamFlag_Enum | BfGenericParamFlag_Var)) == 0) && (!isEnum))
		{
			if ((!ignoreErrors) && (PreFail()))
				*errorOut = Fail(StrFormat("The type '%s' must be an enum type in order to use it as parameter '%s' for '%s'",
					TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Concrete) &&
		((checkGenericParamFlags & (BfGenericParamFlag_Interface | BfGenericParamFlag_Var)) == 0) && (checkArgType->IsInterface()))
	{
		if ((!ignoreErrors) && (PreFail()))
			*errorOut = Fail(StrFormat("The type '%s' must be an concrete type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Interface) &&
		((checkGenericParamFlags & (BfGenericParamFlag_Interface | BfGenericParamFlag_Var)) == 0) && (!checkArgType->IsInterface()))
	{
		if ((!ignoreErrors) && (PreFail()))
			*errorOut = Fail(StrFormat("The type '%s' must be an interface type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
	{
		if (((checkGenericParamFlags & BfGenericParamFlag_Const) == 0) && (!checkArgType->IsConstExprValue()))
		{
			if ((!ignoreErrors) && (PreFail()))
				*errorOut = Fail(StrFormat("The type '%s' must be a const value in order to use it as parameter '%s' for '%s'",
					TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}
	}
	else
	{
		if (checkArgType->IsConstExprValue())
		{
			if ((!ignoreErrors) && (PreFail()))
				*errorOut = Fail(StrFormat("The value '%s' cannot be used for generic type parameter '%s' for '%s'",
					TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Delete) != 0)
	{
		bool canDelete = false;
		if (checkArgType->IsPointer())
			canDelete = true;
		else if (checkArgType->IsObjectOrInterface())
			canDelete = true;
		else if ((checkGenericParamFlags & (BfGenericParamFlag_Delete | BfGenericParamFlag_Var)) != 0)
			canDelete = true;

		if (!canDelete)
		{
			if ((!ignoreErrors) && (PreFail()))
			{
				if (checkArgType->IsGenericParam())
					*errorOut = Fail(StrFormat("Must add 'where %s : delete' constraint in order to use type as parameter '%s' for '%s'",
						TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
				else
					*errorOut = Fail(StrFormat("The type '%s' must be a deletable type in order to use it as parameter '%s' for '%s'",
						TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			}
			return false;
		}
	}

	if (checkArgType->IsPointer())
	{
		auto ptrType = (BfPointerType*)checkArgType;
		checkArgType = ptrType->mElementType;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_New) != 0)
	{
		bool canAlloc = false;

		if (auto checkTypeInst = checkArgType->ToTypeInstance())
		{
			if (checkTypeInst->IsObjectOrStruct())
			{
				checkTypeInst->mTypeDef->PopulateMemberSets();
				BfMemberSetEntry* entry = NULL;
				BfMethodDef* checkMethodDef = NULL;
				checkTypeInst->mTypeDef->mMethodSet.TryGetWith(String("__BfCtor"), &entry);
				if (entry != NULL)
					checkMethodDef = (BfMethodDef*)entry->mMemberDef;
				bool hadProtected = false;
				while (checkMethodDef != NULL)
				{
					if (!checkMethodDef->mParams.IsEmpty())
					{
						auto firstParam = checkMethodDef->mParams[0];
						if (((firstParam->mParamKind == BfParamKind_Params) || (firstParam->mParamKind == BfParamKind_AppendIdx) || (firstParam->mParamKind == BfParamKind_VarArgs)) ||
							((firstParam->mParamDeclaration != NULL) && (firstParam->mParamDeclaration->mInitializer != NULL)))
						{
							// Allow all-default params
						}
						else
						{
							checkMethodDef = checkMethodDef->mNextWithSameName;
							continue;
						}
					}
					if (checkMethodDef->mProtection == BfProtection_Public)
					{
						canAlloc = true;
						break;
					}
					if ((checkMethodDef->mProtection == BfProtection_Protected) || (checkMethodDef->mProtection == BfProtection_ProtectedInternal))
						hadProtected = true;
					checkMethodDef = checkMethodDef->mNextWithSameName;
				}

				if ((!canAlloc) && (hadProtected) && (mCurTypeInstance != NULL))
					canAlloc = TypeIsSubTypeOf(mCurTypeInstance, checkTypeInst, false);
			}
		}
		else if (checkArgType->IsGenericParam())
		{
			canAlloc = (checkGenericParamFlags & (BfGenericParamFlag_New | BfGenericParamFlag_Var)) != 0;
		}
		else if (checkArgType->IsPrimitiveType())
		{
			// Any primitive types and stuff can be allocated
			canAlloc = true;
		}

		if (!canAlloc)
		{
			if ((!ignoreErrors) && (PreFail()))
			{
				if (checkArgType->IsGenericParam())
					*errorOut = Fail(StrFormat("Must add 'where %s : new' constraint in order to use type as parameter '%s' for '%s'",
						TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
				else
					*errorOut = Fail(StrFormat("The type '%s' must have an accessible default constructor in order to use it as parameter '%s' for '%s'",
						TypeToString(origCheckArgType).c_str(), genericParamInst->GetName().c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			}
			return false;
		}
	}

	if ((genericParamInst->mInterfaceConstraints.IsEmpty()) && (genericParamInst->mOperatorConstraints.IsEmpty()) && (genericParamInst->mTypeConstraint == NULL))
		return true;

	if (genericParamInst->mTypeConstraint != NULL)
	{
		bool constraintMatched = false;

		if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
		{
			if (checkArgType->IsConstExprValue())
			{
				auto constExprValueType = (BfConstExprValueType*)checkArgType;

				if (genericParamInst->mTypeConstraint->IsPrimitiveType())
				{
					auto primType = (BfPrimitiveType*)genericParamInst->mTypeConstraint;

					// Let either an exact typematch or an undef pass through.  Eventually instead of undefs we may want to do
					//  actual expression comparisons, but we are overly permissive now and then we may fail on specialization
					if ((constExprValueType->mValue.mTypeCode != primType->mTypeDef->mTypeCode) &&
						(constExprValueType->mValue.mTypeCode != BfTypeCode_Let) &&
						(primType->mTypeDef->mTypeCode != BfTypeCode_Let))
					{
						bool doError = true;

						if (BfIRConstHolder::IsInt(constExprValueType->mValue.mTypeCode))
						{
							if (BfIRConstHolder::IsInt(primType->mTypeDef->mTypeCode))
							{
								if (mCompiler->mSystem->DoesLiteralFit(primType->mTypeDef->mTypeCode, constExprValueType->mValue))
									doError = false;
							}
						}
						else if ((primType->mTypeDef->mTypeCode == BfTypeCode_Float) && ((constExprValueType->mValue.mTypeCode == BfTypeCode_Double)))
						{
							doError = false;
						}

						if (doError)
						{
							if ((!ignoreErrors) && (PreFail()))
								*errorOut = Fail(StrFormat("Const generic argument '%s', declared with '%s', is not compatible with const constraint '%s' for '%s'", genericParamInst->GetName().c_str(),
									_TypeToString(constExprValueType).c_str(), _TypeToString(genericParamInst->mTypeConstraint).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
							return false;
						}
					}
				}
				else if (genericParamInst->mTypeConstraint != constExprValueType->mType)
				{
					if ((!ignoreErrors) && (PreFail()))
						*errorOut = Fail(StrFormat("Const generic argument '%s', declared as '%s', is not compatible with const constraint '%s' for '%s'", genericParamInst->GetName().c_str(),
							_TypeToString(constExprValueType).c_str(), _TypeToString(genericParamInst->mTypeConstraint).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
					return false;
				}
			}
		}
		else
		{
			BfType* convCheckConstraint = genericParamInst->mTypeConstraint;
			if ((convCheckConstraint->IsUnspecializedType()) && (methodGenericArgs != NULL))
				convCheckConstraint = ResolveGenericType(convCheckConstraint, NULL, methodGenericArgs, mCurTypeInstance);
			if (convCheckConstraint == NULL)
				return false;
			if ((checkArgType->IsMethodRef()) || (checkArgType->IsFunction()))
			{
				if (convCheckConstraint->IsDelegate())
				{
					BfMethodInstance* checkMethodInstance;
					if (checkArgType->IsMethodRef())
					{
						auto methodRefType = (BfMethodRefType*)checkArgType;
						checkMethodInstance = methodRefType->mMethodRef;
					}
					else
					{
						checkMethodInstance = GetRawMethodInstanceAtIdx(checkArgType->ToTypeInstance(), 0, "Invoke");
					}

					auto invokeMethod = GetRawMethodInstanceAtIdx(convCheckConstraint->ToTypeInstance(), 0, "Invoke");

					if (checkMethodInstance->HasExplicitThis() != 0)
					{
						// Don't allow functions with explicit 'this'
					}
					else
					{
						BfExprEvaluator exprEvaluator(this);
						if (exprEvaluator.IsExactMethodMatch(checkMethodInstance, invokeMethod))
							constraintMatched = true;
					}
				}
				else if (convCheckConstraint->IsInstanceOf(mCompiler->mDelegateTypeDef))
					constraintMatched = true;
			}
			else if (CanCast(GetFakeTypedValue(checkArgType), convCheckConstraint))
			{
				constraintMatched = true;
			}
			else if (origCheckArgType->IsWrappableType())
			{
				if (origCheckArgType->IsSizedArray())
				{
					auto sizedArrayType = (BfSizedArrayType*)origCheckArgType;
					if (convCheckConstraint->IsGenericTypeInstance())
					{
						auto convCheckConstraintInst = (BfTypeInstance*)convCheckConstraint;
						if (convCheckConstraintInst->IsInstanceOf(mCompiler->mSizedArrayTypeDef))
						{
							if (convCheckConstraintInst->mGenericTypeInfo->mTypeGenericArguments[0] == sizedArrayType->mElementType)
							{
								auto constExprValueType = (BfConstExprValueType*)convCheckConstraintInst->mGenericTypeInfo->mTypeGenericArguments[1];
								if (sizedArrayType->mElementCount == constExprValueType->mValue.mInt64)
									constraintMatched = true;
							}
						}
					}
				}

				if (!constraintMatched)
				{
					BfType* wrappedStructType = GetWrappedStructType(origCheckArgType, false);
					if (CanCast(GetFakeTypedValue(wrappedStructType), convCheckConstraint))
						constraintMatched = true;
				}
			}

			if (!constraintMatched)
			{
				if ((!ignoreErrors) && (PreFail()))
					*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must derive from '%s'", genericParamInst->GetName().c_str(),
						TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(), TypeToString(convCheckConstraint).c_str(),
						_TypeToString(genericParamInst->mTypeConstraint).c_str()), checkArgTypeRef);
				return false;
			}
		}
	}

	for (auto checkConstraint : genericParamInst->mInterfaceConstraints)
	{
		BfType* convCheckConstraint = checkConstraint;
		if (convCheckConstraint->IsUnspecializedType())
			convCheckConstraint = ResolveGenericType(convCheckConstraint, NULL, methodGenericArgs, mCurTypeInstance);
		if (convCheckConstraint == NULL)
			return false;

		BfTypeInstance* typeConstraintInst = convCheckConstraint->ToTypeInstance();

		bool implementsInterface = false;
		if (origCheckArgType != checkArgType)
		{
			implementsInterface = CanCast(BfTypedValue(BfIRValue::sValueless, origCheckArgType), convCheckConstraint);
		}

		if (!implementsInterface)
			implementsInterface = CanCast(BfTypedValue(BfIRValue::sValueless, checkArgType), convCheckConstraint);

		if ((!implementsInterface) && (origCheckArgType->IsWrappableType()))
		{
			BfTypeInstance* wrappedStructType = GetWrappedStructType(origCheckArgType, false);
			if (TypeIsSubTypeOf(wrappedStructType, typeConstraintInst))
				implementsInterface = true;
		}

		if ((!implementsInterface) && (origCheckArgType->IsTypedPrimitive()))
		{
			auto underlyingType = origCheckArgType->GetUnderlyingType();
			BfTypeInstance* wrappedStructType = GetWrappedStructType(underlyingType, false);
			if (TypeIsSubTypeOf(wrappedStructType, typeConstraintInst))
				implementsInterface = true;
		}

		if (!implementsInterface)
		{
			if ((!ignoreErrors) && (PreFail()))
				*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must implement '%s'", genericParamInst->GetName().c_str(),
					TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(), TypeToString(checkConstraint).c_str()), checkArgTypeRef);
			return false;
		}
	}

	for (auto& checkOpConstraint : genericParamInst->mOperatorConstraints)
	{
		auto leftType = checkOpConstraint.mLeftType;
		if ((leftType != NULL) && (leftType->IsUnspecializedType()))
			leftType = ResolveGenericType(leftType, NULL, methodGenericArgs, mCurTypeInstance);
		if (leftType != NULL)
			leftType = FixIntUnknown(leftType);

		auto rightType = checkOpConstraint.mRightType;
		if ((rightType != NULL) && (rightType->IsUnspecializedType()))
			rightType = ResolveGenericType(rightType, NULL, methodGenericArgs, mCurTypeInstance);
		if (rightType != NULL)
			rightType = FixIntUnknown(rightType);

		BfConstraintState constraintSet;
		constraintSet.mPrevState = mContext->mCurConstraintState;
		constraintSet.mGenericParamInstance = genericParamInst;
		constraintSet.mLeftType = leftType;
		constraintSet.mRightType = rightType;
		SetAndRestoreValue<BfConstraintState*> prevConstraintSet(mContext->mCurConstraintState, &constraintSet);
		if (!CheckConstraintState(NULL))
			return false;

		if (checkOpConstraint.mBinaryOp != BfBinaryOp_None)
		{
			BfExprEvaluator exprEvaluator(this);

			BfTypedValue leftValue(mBfIRBuilder->GetFakeVal(), leftType);
			BfTypedValue rightValue(mBfIRBuilder->GetFakeVal(), rightType);

			//
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
				SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
				exprEvaluator.PerformBinaryOperation(NULL, NULL, checkOpConstraint.mBinaryOp, NULL, (BfBinOpFlags)(BfBinOpFlag_NoClassify | BfBinOpFlag_IsConstraintCheck), leftValue, rightValue);
			}

			if ((!exprEvaluator.mResult) ||
				(!CanCast(exprEvaluator.mResult, origCheckArgType, BfCastFlags_NoConversionOperator)))
			{
				if ((!ignoreErrors) && (PreFail()))
				{
					if (genericParamInst->mExternType != NULL)
						*errorOut = Fail(StrFormat("Binary operation for '%s' must result in '%s' from binary operation '%s %s %s'",
							GenericParamSourceToString(genericParamSource).c_str(), TypeToString(origCheckArgType).c_str(),
							TypeToString(leftType).c_str(), BfGetOpName(checkOpConstraint.mBinaryOp), TypeToString(rightType).c_str()
						), checkArgTypeRef);
					else
						*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must result from binary operation '%s %s %s'", genericParamInst->GetName().c_str(),
							TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(),
							TypeToString(leftType).c_str(), BfGetOpName(checkOpConstraint.mBinaryOp), TypeToString(rightType).c_str()
						), checkArgTypeRef);
				}
				return false;
			}
		}
		else
		{
			BfTypedValue rightValue(mBfIRBuilder->GetFakeVal(), rightType);

			StringT<128> failedOpName;

			if (checkOpConstraint.mCastToken == BfToken_Implicit)
			{
				if (!CanCast(rightValue, origCheckArgType, (BfCastFlags)(BfCastFlags_SilentFail | BfCastFlags_IsConstraintCheck)))
					failedOpName = "implicit conversion from '";
			}
			else
			{
				SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
				SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);

				if (checkOpConstraint.mCastToken == BfToken_Explicit)
				{
					if (!CastToValue(NULL, rightValue, origCheckArgType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail | BfCastFlags_IsConstraintCheck)))
						failedOpName = "explicit conversion from '";
				}
				else
				{
					BfExprEvaluator exprEvaluator(this);
					exprEvaluator.mResult = rightValue;
					exprEvaluator.PerformUnaryOperation(NULL, checkOpConstraint.mUnaryOp, NULL, BfUnaryOpFlag_IsConstraintCheck);

					if ((!exprEvaluator.mResult) ||
						(!CanCast(exprEvaluator.mResult, origCheckArgType)))
					{
						failedOpName += "unary operation '";
						failedOpName += BfGetOpName(checkOpConstraint.mUnaryOp);
					}
				}
			}

			if (!failedOpName.IsEmpty())
			{
				if ((!ignoreErrors) && (PreFail()))
					*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must result from %s%s'", genericParamInst->GetName().c_str(),
						TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(),
						failedOpName.c_str(), TypeToString(rightType).c_str()
					), checkArgTypeRef);
				return false;
			}
		}
	}

	return true;
}

BfGenericParamType* BfModule::GetGenericParamType(BfGenericParamKind paramKind, int paramIdx)
{
	if (paramIdx < (int)mContext->mGenericParamTypes[paramKind].size())
	{
		auto genericParamType = mContext->mGenericParamTypes[paramKind][paramIdx];
		return genericParamType;
	}

	auto genericParamType = new BfGenericParamType();

	genericParamType->mContext = mContext;
	genericParamType->mGenericParamKind = paramKind;
	genericParamType->mGenericParamIdx = paramIdx;

	PopulateType(genericParamType);

	BF_ASSERT(paramIdx == (int)mContext->mGenericParamTypes[paramKind].size());
	mContext->mGenericParamTypes[paramKind].push_back(genericParamType);

	BfResolvedTypeSet::LookupContext lookupCtx;
	lookupCtx.mModule = this;
	BfResolvedTypeSet::EntryRef typeEntry;
	auto inserted = mContext->mResolvedTypes.Insert(genericParamType, &lookupCtx, &typeEntry);
	BF_ASSERT(inserted);
	typeEntry->mValue = genericParamType;

	return genericParamType;
}

static int sValueFromExprIdx = 0;

BfTypedValue BfModule::FlushNullConditional(BfTypedValue result, bool ignoreNullable)
{
	auto pendingNullCond = mCurMethodState->mPendingNullConditional;

	if ((result) && (!ignoreNullable))
	{
		if (result.mType->IsVar())
			return result;

		auto notNullBB = mBfIRBuilder->GetInsertBlock();

		//TODO: Make this work, needed for 'void' and such
		BfType* nullableType = NULL;
		if ((result.mType->IsValueType()) && (!result.mType->IsNullable()))
		{
			BfTypeVector typeVec;
			typeVec.push_back(result.mType);
			nullableType = ResolveTypeDef(mCompiler->mNullableTypeDef, typeVec);
			BF_ASSERT(nullableType != NULL);
		}

		// Go back to start and do any setup we need
		mBfIRBuilder->SetInsertPoint(pendingNullCond->mPrevBB);
		BfTypedValue nullableTypedValue;
		if (nullableType != NULL)
		{
			nullableTypedValue = BfTypedValue(CreateAlloca(nullableType, true, "nullCond.nullable"), nullableType, true);
			mBfIRBuilder->CreateMemSet(nullableTypedValue.mValue, GetConstValue(0, GetPrimitiveType(BfTypeCode_Int8)),
				GetConstValue(nullableType->mSize), nullableType->mAlign);
		}
		mBfIRBuilder->CreateBr(pendingNullCond->mCheckBB);

		// Now that we have the nullableTypedValue we can set that, or just jump if we didn't need it
		mBfIRBuilder->SetInsertPoint(notNullBB);
		result = LoadValue(result);
		if (nullableTypedValue)
		{
			auto elementType = nullableType->GetUnderlyingType();
			if (elementType->IsVar())
			{
				// Do nothing
			}
			else if (elementType->IsValuelessType())
			{
				BfIRValue ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 1); // mHasValue
				mBfIRBuilder->CreateStore(GetConstValue(1, GetPrimitiveType(BfTypeCode_Boolean)), ptrValue);
			}
			else
			{
				BfIRValue ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 1); // mValue
				mBfIRBuilder->CreateAlignedStore(result.mValue, ptrValue, result.mType->mAlign);
				ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 2); // mHasValue
				mBfIRBuilder->CreateAlignedStore(GetConstValue(1, GetPrimitiveType(BfTypeCode_Boolean)), ptrValue, 1);
			}
			result = nullableTypedValue;
		}
		mBfIRBuilder->CreateBr(pendingNullCond->mDoneBB);

		AddBasicBlock(pendingNullCond->mDoneBB);
		if (nullableType == NULL)
		{
			auto phi = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(result.mType), 1 + (int)pendingNullCond->mNotNullBBs.size());
			mBfIRBuilder->AddPhiIncoming(phi, result.mValue, notNullBB);

			mBfIRBuilder->AddPhiIncoming(phi, GetDefaultValue(result.mType), pendingNullCond->mCheckBB);
			for (int notNullIdx = 0; notNullIdx < (int)pendingNullCond->mNotNullBBs.size() - 1; notNullIdx++)
			{
				auto prevNotNullBB = pendingNullCond->mNotNullBBs[notNullIdx];
				mBfIRBuilder->AddPhiIncoming(phi, GetDefaultValue(result.mType), prevNotNullBB);
			}

			result.mValue = phi;
		}
	}
	else
	{
		mBfIRBuilder->CreateBr(pendingNullCond->mDoneBB);

		mBfIRBuilder->SetInsertPoint(pendingNullCond->mPrevBB);
		mBfIRBuilder->CreateBr(pendingNullCond->mCheckBB);

		AddBasicBlock(pendingNullCond->mDoneBB);
	}

	delete mCurMethodState->mPendingNullConditional;
	mCurMethodState->mPendingNullConditional = NULL;

	return result;
}

BF_NOINLINE void BfModule::EvaluateWithNewConditionalScope(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfEvalExprFlags flags)
{
	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData);
	if (mCurMethodState != NULL)
	{
		deferredLocalAssignData.mIsIfCondition = true;
		deferredLocalAssignData.mIfMayBeSkipped = true;
		deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
		deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
		mCurMethodState->mDeferredLocalAssignData = &deferredLocalAssignData;
	}

	BfScopeData newScope;
	newScope.mOuterIsConditional = true;
	newScope.mAllowTargeting = false;
	if (mCurMethodState != NULL)
	{
		mCurMethodState->AddScope(&newScope);
		NewScopeState(true, false);
	}

	exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | flags);
	exprEvaluator.Evaluate(expr, (flags & BfEvalExprFlags_PropogateNullConditional) != 0, (flags & BfEvalExprFlags_IgnoreNullConditional) != 0, (flags & BfEvalExprFlags_AllowSplat) != 0);

	if (mCurMethodState != NULL)
		RestoreScopeState();
}

BfTypedValue BfModule::CreateValueFromExpression(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfType* wantTypeRef, BfEvalExprFlags flags, BfType** outOrigType)
{
	//
	{
		BP_ZONE("CreateValueFromExpression:CheckStack");

		StackHelper stackHelper;
		if (!stackHelper.CanStackExpand(64 * 1024))
		{
			BfTypedValue result;
			if (!stackHelper.Execute([&]()
			{
				result = CreateValueFromExpression(exprEvaluator, expr, wantTypeRef, flags, outOrigType);
			}))
			{
				Fail("Expression too complex to compile", expr);
			}
			return result;
		}
	}

	BP_ZONE("BfModule::CreateValueFromExpression");

	if (outOrigType != NULL)
		*outOrigType = NULL;

	exprEvaluator.mExpectingType = wantTypeRef;
	exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | flags);
	exprEvaluator.mExplicitCast = (flags & BfEvalExprFlags_ExplicitCast) != 0;

	if ((flags & BfEvalExprFlags_CreateConditionalScope) != 0)
	{
		if ((mCurMethodState == NULL) || (mCurMethodState->mCurScope->mScopeKind != BfScopeKind_StatementTarget_Conditional))
		{
			EvaluateWithNewConditionalScope(exprEvaluator, expr, flags);
		}
		else
		{
			SetAndRestoreValue<bool> prevIsConditional(mCurMethodState->mCurScope->mIsConditional, true);
			exprEvaluator.Evaluate(expr, (flags & BfEvalExprFlags_PropogateNullConditional) != 0, (flags & BfEvalExprFlags_IgnoreNullConditional) != 0, true);
		}
	}
	else
		exprEvaluator.Evaluate(expr, (flags & BfEvalExprFlags_PropogateNullConditional) != 0, (flags & BfEvalExprFlags_IgnoreNullConditional) != 0, true);

	if (!exprEvaluator.mResult)
	{
		if ((flags & BfEvalExprFlags_InferReturnType) != 0)
			return exprEvaluator.mResult;
		if (!mCompiler->mPassInstance->HasFailed())
		{
			if (PreFail())
				Fail("INTERNAL ERROR: No expression result returned but no error caught in expression evaluator", expr);
		}
		return BfTypedValue();
	}
	auto typedVal = exprEvaluator.mResult;
	if (typedVal.mKind == BfTypedValueKind_GenericConstValue)
	{
		BF_ASSERT(typedVal.mType->IsGenericParam());
		auto genericParamDef = GetGenericParamInstance((BfGenericParamType*)typedVal.mType);

		bool handled = false;

		if ((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
		{
			auto genericTypeConstraint = genericParamDef->mTypeConstraint;
			if (genericTypeConstraint != NULL)
			{
				auto underlyingConstraint = genericTypeConstraint;
				if ((underlyingConstraint != NULL) && (underlyingConstraint->IsBoxed()))
					underlyingConstraint = underlyingConstraint->GetUnderlyingType();
				if (underlyingConstraint != NULL)
				{
					BfTypedValue result;
					result.mKind = BfTypedValueKind_Value;
					result.mType = genericTypeConstraint;
					result.mValue = mBfIRBuilder->GetUndefConstValue(mBfIRBuilder->MapType(underlyingConstraint));
					typedVal = result;
					handled = true;
				}
			}
		}

		if (!handled)
		{
			//Fail("Only const generic parameters can be used as values", expr);
			AssertErrorState();
			return BfTypedValue();
		}
	}

	if ((flags & BfEvalExprFlags_AllowIntUnknown) == 0)
		FixIntUnknown(typedVal);
	if (!mBfIRBuilder->mIgnoreWrites)
		FixValueActualization(typedVal);
	exprEvaluator.CheckResultForReading(typedVal);

	if ((wantTypeRef == NULL) || (!wantTypeRef->IsRef()))
	{
		if ((flags & BfEvalExprFlags_NoCast) == 0)
		{
			// Only allow a 'ref' type if we have an explicit 'ref' operator
			bool allowRef = false;
			BfAstNode* checkExpr = expr;

			while (checkExpr != NULL)
			{
				if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(checkExpr))
					checkExpr = parenExpr->mExpression;
				else if (auto unaryOp = BfNodeDynCast<BfUnaryOperatorExpression>(checkExpr))
				{
					if ((unaryOp->mOp == BfUnaryOp_Ref) || (unaryOp->mOp == BfUnaryOp_Mut))
						allowRef = true;
					break;
				}
				if (auto block = BfNodeDynCast<BfBlock>(checkExpr))
				{
					if (block->mChildArr.mSize == 0)
						break;
					checkExpr = block->mChildArr.back();
				}
				else
					break;
			}
			if (!allowRef)
				typedVal = RemoveRef(typedVal);
		}
	}

	if ((!typedVal.mType->IsComposite()) && (!typedVal.mType->IsGenericParam())) // Load non-structs by default
	{
		if ((!mBfIRBuilder->mIgnoreWrites) && (!typedVal.mType->IsDataIncomplete()) && (!typedVal.mType->IsValuelessType()) && (!typedVal.mType->IsVar()))
		{
			BF_ASSERT(!typedVal.mValue.IsFake());
		}

		typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);
	}

	if (wantTypeRef != NULL)
	{
		if (outOrigType != NULL)
			*outOrigType = typedVal.mType;

		if (((flags & BfEvalExprFlags_NoCast) == 0) && (!wantTypeRef->IsVar()))
		{
			BfCastFlags castFlags = ((flags & BfEvalExprFlags_ExplicitCast) != 0) ? BfCastFlags_Explicit : BfCastFlags_None;
			if ((flags & BfEvalExprFlags_FieldInitializer) != 0)
				castFlags = (BfCastFlags)(castFlags | BfCastFlags_WarnOnBox);
			typedVal = Cast(expr, typedVal, wantTypeRef, castFlags);
			if (!typedVal)
				return typedVal;
		}

		if (exprEvaluator.mIsVolatileReference)
			typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);
	}

	if ((typedVal.mType->IsValueType()) && ((flags & BfEvalExprFlags_NoValueAddr) != 0))
		typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);

	return typedVal;
}

BfTypedValue BfModule::CreateValueFromExpression(BfExpression* expr, BfType* wantTypeRef, BfEvalExprFlags flags, BfType** outOrigType)
{
	BfExprEvaluator exprEvaluator(this);
	return CreateValueFromExpression(exprEvaluator, expr, wantTypeRef, flags, outOrigType);
}

BfTypedValue BfModule::GetOrCreateVarAddr(BfExpression* expr)
{
	BfExprEvaluator exprEvaluator(this);
	exprEvaluator.Evaluate(expr);
	if (!exprEvaluator.mResult)
	{
		Fail("Invalid expression type", expr);
		return BfTypedValue();
	}
	if (!exprEvaluator.mResult.IsAddr())
	{
		Fail("Cannot assign to value", expr);
		return BfTypedValue();
	}
	return exprEvaluator.mResult;
}

// Clear memory, set classVData, call init. Actual ctor is called elsewhere.
void BfModule::InitTypeInst(BfTypedValue typedValue, BfScopeData* scopeData, bool zeroMemory, BfIRValue sizeValue)
{
	auto typeInstance = typedValue.mType->ToTypeInstance();
	if (zeroMemory)
	{
		int zeroAlign = typedValue.mType->mAlign;
		if (typeInstance != NULL)
			zeroAlign = typeInstance->mInstAlign;
		mBfIRBuilder->CreateMemSet(typedValue.mValue, GetConstValue8(0), sizeValue, zeroAlign);
	}

	if (!typedValue.mType->IsObject())
	{
		return;
	}

	if ((scopeData == NULL) && (mCurMethodState != NULL))
		return; // Handled in heap alloc funcs

	auto typeDef = typeInstance->mTypeDef;

	mBfIRBuilder->PopulateType(typedValue.mType);
	auto vObjectAddr = mBfIRBuilder->CreateInBoundsGEP(typedValue.mValue, 0, 0);
	bool isAutocomplete = mCompiler->IsAutocomplete();

	BfIRValue vDataRef;
	if (!isAutocomplete)
	{
		vDataRef = GetClassVDataPtr(typeInstance);
	}

	auto i8Type = GetPrimitiveType(BfTypeCode_Int8);
	auto ptrType = CreatePointerType(i8Type);
	auto ptrPtrType = CreatePointerType(ptrType);

	PopulateType(ptrPtrType, BfPopulateType_Declaration);
	PopulateType(ptrType, BfPopulateType_Declaration);
	auto destAddr = mBfIRBuilder->CreateBitCast(vObjectAddr, mBfIRBuilder->MapType(ptrPtrType));
	if (!isAutocomplete)
	{
		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
		{
			auto objectPtr = mBfIRBuilder->CreateBitCast(destAddr, mBfIRBuilder->MapType(mContext->mBfObjectType));

			SizedArray<BfIRValue, 4> llvmArgs;
			llvmArgs.push_back(objectPtr);
			llvmArgs.push_back(vDataRef);

			auto objectStackInitMethod = GetInternalMethod("Dbg_ObjectStackInit");
			if (objectStackInitMethod)
				mBfIRBuilder->CreateCall(objectStackInitMethod.mFunc, llvmArgs);
		}
		else
		{
			auto srcVal = mBfIRBuilder->CreateBitCast(vDataRef, mBfIRBuilder->MapType(ptrType));
			auto objectPtr = mBfIRBuilder->CreateBitCast(destAddr, mBfIRBuilder->MapType(ptrType));
			mBfIRBuilder->CreateStore(srcVal, destAddr);
		}
	}
}

bool BfModule::IsAllocatorAligned()
{
	if (mCompiler->mOptions.mMallocLinkName == "StompAlloc")
		return false;
	return true;
}

BfIRValue BfModule::AllocBytes(BfAstNode* refNode, const BfAllocTarget& allocTarget, BfType* type, BfIRValue sizeValue, BfIRValue alignValue, BfAllocFlags allocFlags/*, bool zeroMemory, bool defaultToMalloc*/)
{
	BfIRValue result;

	BfType* ptrType;
	if ((type->IsObject()) && ((allocFlags & BfAllocFlags_RawArray) == 0))
		ptrType = type;
	else
		ptrType = CreatePointerType(type);

	if ((allocTarget.mScopedInvocationTarget != NULL) || (allocTarget.mCustomAllocator))
	{
		auto intType = GetPrimitiveType(BfTypeCode_IntPtr);
		SizedArray<BfExpression*, 2> argExprs;

		BfTypedValueExpression typedValueExpr;
		typedValueExpr.Init(BfTypedValue(sizeValue, intType));
		typedValueExpr.mRefNode = refNode;
		argExprs.push_back(&typedValueExpr);

		BfTypedValueExpression appendSizeValueExpr;
		appendSizeValueExpr.Init(BfTypedValue(alignValue, intType));
		appendSizeValueExpr.mRefNode = refNode;
		argExprs.push_back(&appendSizeValueExpr);

		BfExprEvaluator exprEvaluator(this);
		BfTypedValue allocResult;

		if (allocTarget.mScopedInvocationTarget != NULL)
		{
			SizedArray<BfTypeReference*, 2> genericArgs;
			exprEvaluator.DoInvocation(allocTarget.mScopedInvocationTarget, NULL, argExprs, BfMethodGenericArguments());
			allocResult = LoadValue(exprEvaluator.mResult);
		}
		else if (allocTarget.mCustomAllocator)
		{
			auto customTypeInst = allocTarget.mCustomAllocator.mType->ToTypeInstance();
			if (customTypeInst == NULL)
			{
				if (allocTarget.mCustomAllocator.mType->IsStructPtr())
					customTypeInst = allocTarget.mCustomAllocator.mType->GetUnderlyingType()->ToTypeInstance();
			}

			if (customTypeInst == NULL)
			{
				Fail(StrFormat("Type '%s' cannot be used as a custom allocator", TypeToString(allocTarget.mCustomAllocator.mType).c_str()), refNode);
			}
			else
			{
				BfTypedValueExpression typeValueExpr;
				String allocMethodName = "Alloc";
				if (GetRawMethodByName(customTypeInst, "AllocTyped", -1, true, true))
				{
					allocMethodName = "AllocTyped";
					auto typeType = ResolveTypeDef(mCompiler->mTypeTypeDef);
					auto typeRefVal = CreateTypeDataRef(type);

					typeValueExpr.Init(BfTypedValue(typeRefVal, typeType));
					typeValueExpr.mRefNode = refNode;
					argExprs.Insert(0, &typeValueExpr);
				}

				if (HasMixin(customTypeInst, allocMethodName, 2))
				{
					BfSizedArray<BfExpression*> argExprArr;
					argExprArr.mSize = (int)argExprs.size();
					argExprArr.mVals = &argExprs[0];

					exprEvaluator.InjectMixin(refNode, allocTarget.mCustomAllocator, false, allocMethodName, argExprs, {});
					allocResult = exprEvaluator.GetResult();
				}
				else
				{
					BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
					BfResolvedArgs argValues(&sizedArgExprs);
					exprEvaluator.ResolveArgValues(argValues);
					SetAndRestoreValue<bool> prevNoBind(mCurMethodState->mNoBind, true);
					allocResult = exprEvaluator.MatchMethod(refNode, NULL, allocTarget.mCustomAllocator, false, false, allocMethodName, argValues, BfMethodGenericArguments());
				}
			}
		}

		if (allocResult)
		{
			if (allocResult.mType->IsVoidPtr())
				result = mBfIRBuilder->CreateBitCast(allocResult.mValue, mBfIRBuilder->MapType(ptrType));
			else
				result = CastToValue(refNode, allocResult, ptrType, BfCastFlags_Explicit);
			return result;
		}
	}

	if ((allocFlags & BfAllocFlags_NoDefaultToMalloc) != 0)
		return result;

	if ((mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
	{
		BfIRValue allocData = GetDbgRawAllocData(type);
		BfModuleMethodInstance allocMethod = GetInternalMethod("Dbg_RawAlloc", 2);

		SizedArray<BfIRValue, 2> llvmArgs;
		llvmArgs.push_back(sizeValue);
		llvmArgs.push_back(allocData);
		BfIRValue bitData = mBfIRBuilder->CreateCall(allocMethod.mFunc, llvmArgs);
		return mBfIRBuilder->CreateBitCast(bitData, mBfIRBuilder->MapType(ptrType));
	}

	BfModuleMethodInstance moduleMethodInstance;
	moduleMethodInstance = GetInternalMethod("Malloc");

	SizedArray<BfIRValue, 1> llvmArgs;
	llvmArgs.push_back(sizeValue);

	auto func = moduleMethodInstance.mFunc;

	if ((!func) || (func.IsFake()))
	{
		BF_ASSERT((mCompiler->mIsResolveOnly) || (mBfIRBuilder->mIgnoreWrites));
		return mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(ptrType));
	}
	BfIRValue bitData = mBfIRBuilder->CreateCall(func, llvmArgs);
	if ((allocFlags & BfAllocFlags_ZeroMemory) != 0)
		mBfIRBuilder->CreateMemSet(bitData, GetConstValue8(0), sizeValue, alignValue);
	result = mBfIRBuilder->CreateBitCast(bitData, mBfIRBuilder->MapType(ptrType));
	return result;
}

BfIRValue BfModule::GetMarkFuncPtr(BfType* type)
{
	if (type->IsStruct())
	{
		auto typeInstance = type->ToTypeInstance();

		BfModuleMethodInstance moduleMethodInst = GetMethodByName(typeInstance, BF_METHODNAME_MARKMEMBERS, 0, true);
		return mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr)));
	}
	else if (type->IsObjectOrInterface())
	{
		auto gcType = ResolveTypeDef(mCompiler->mGCTypeDef)->ToTypeInstance();
		BfModuleMethodInstance moduleMethodInst = GetMethodByName(gcType, "MarkDerefedObject");
		BF_ASSERT(moduleMethodInst.mFunc);
		return mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr)));
	}
	else
	{
		auto gcType = ResolveTypeDef(mCompiler->mGCTypeDef)->ToTypeInstance();

		BfExprEvaluator exprEvaluator(this);
		SizedArray<BfResolvedArg, 1> resolvedArgs;
		BfResolvedArg resolvedArg;
		resolvedArg.mTypedValue = BfTypedValue(mBfIRBuilder->GetFakeVal(), type, type->IsComposite());
		resolvedArgs.Add(resolvedArg);
		BfMethodMatcher methodMatcher(NULL, this, "Mark", resolvedArgs, BfMethodGenericArguments());
		methodMatcher.CheckType(gcType, BfTypedValue(), false);

		BfModuleMethodInstance moduleMethodInst = exprEvaluator.GetSelectedMethod(NULL, methodMatcher.mBestMethodTypeInstance, methodMatcher.mBestMethodDef, methodMatcher);
		BF_ASSERT(moduleMethodInst.mFunc);
		return mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr)));
	}

	return BfIRValue();
}

BfIRValue BfModule::GetDbgRawAllocData(BfType* type)
{
	BfIRValue allocDataValue;
	if (mDbgRawAllocDataRefs.TryGetValue(type, &allocDataValue))
		return allocDataValue;

	BfIRValue markFuncPtr;
	if (type->WantsGCMarking())
		markFuncPtr = GetMarkFuncPtr(type);
	else
		markFuncPtr = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr)));

	BfIRValue typeDataRef = CreateTypeDataRef(type);

	auto dbgRawAllocDataType = ResolveTypeDef(mCompiler->mDbgRawAllocDataTypeDef)->ToTypeInstance();

	BfTypeInstance* typeInstance = type->ToTypeInstance();
	if (typeInstance == NULL)
	{
		typeInstance = GetWrappedStructType(type);
		if (typeInstance != NULL)
			AddDependency(typeInstance, mCurTypeInstance, BfDependencyMap::DependencyFlag_Allocates);
	}
	if (typeInstance != NULL)
		PopulateType(typeInstance);
	int stackCount = mCompiler->mOptions.mAllocStackCount;
	if ((typeInstance != NULL) && (typeInstance->mTypeOptionsIdx != -1))
	{
		auto typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);
		stackCount = BfTypeOptions::Apply(stackCount, typeOptions->mAllocStackTraceDepth);
	}

	SizedArray<BfIRValue, 2> dataValues;
	dataValues.Add(mBfIRBuilder->CreateConstAggZero(mBfIRBuilder->MapType(dbgRawAllocDataType->mBaseType, BfIRPopulateType_Full)));
	dataValues.Add(typeDataRef);
	dataValues.Add(markFuncPtr);
	dataValues.Add(mBfIRBuilder->CreateConst(BfTypeCode_Int32, stackCount));
	BfIRValue dataStruct = mBfIRBuilder->CreateConstAgg_Value(mBfIRBuilder->MapType(dbgRawAllocDataType, BfIRPopulateType_Full), dataValues);
	allocDataValue = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(dbgRawAllocDataType), true, BfIRLinkageType_Internal, dataStruct, "__allocData_" + BfSafeMangler::Mangle(type));

	mDbgRawAllocDataRefs.TryAdd(type, allocDataValue);
	return allocDataValue;
}

BfIRValue BfModule::AllocFromType(BfType* type, const BfAllocTarget& allocTarget, BfIRValue appendSizeValue, BfIRValue arraySize, int arrayDim, BfAllocFlags allocFlags, int alignOverride)
{
	BP_ZONE("AllocFromType");

	BfScopeData* scopeData = allocTarget.mScopeData;

	BF_ASSERT(!type->IsVar());

	auto typeInstance = type->ToTypeInstance();
	if ((typeInstance == NULL) && (type->IsGenericParam()))
		typeInstance = mContext->mBfObjectType;

	bool isHeapAlloc = scopeData == NULL;
	bool isScopeAlloc = scopeData != NULL;
	bool isLoopedAlloc = (scopeData != NULL) && (mCurMethodState->mCurScope->IsLooped(scopeData));
	bool isDynAlloc = (scopeData != NULL) && (mCurMethodState->mCurScope->IsDyn(scopeData));

	bool isRawArrayAlloc = (allocFlags & BfAllocFlags_RawArray) != 0;
	bool zeroMemory = (allocFlags & BfAllocFlags_ZeroMemory) != 0;
	bool noDtorCall = (allocFlags & BfAllocFlags_NoDtorCall) != 0;

	if ((type->IsValuelessType()) && ((!arraySize) || (isRawArrayAlloc)))
	{
		BfPointerType* ptrType = CreatePointerType(type);
		auto val = mBfIRBuilder->CreateIntToPtr(mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1), mBfIRBuilder->MapType(ptrType));
		return val;
	}

	if (typeInstance != NULL)
		mBfIRBuilder->PopulateType(typeInstance);

	bool hasCustomAllocator = (allocTarget.mCustomAllocator) || (allocTarget.mScopedInvocationTarget != NULL);

	if ((!hasCustomAllocator) && (mBfIRBuilder->mIgnoreWrites))
	{
		return mBfIRBuilder->GetFakeVal();
	}

	AddDependency(type, mCurTypeInstance, BfDependencyMap::DependencyFlag_Allocates);

	BfIRValue sizeValue;
	BfIRType allocType = mBfIRBuilder->MapType(type);
	int allocSize = type->mSize;
	int allocAlign = type->mAlign;
	if (typeInstance != NULL)
	{
		if ((!mBfIRBuilder->mIgnoreWrites) && (!mIsComptimeModule))
			typeInstance->mHasBeenInstantiated = true;
		allocSize = typeInstance->mInstSize;
		allocAlign = typeInstance->mInstAlign;

		//if (typeInstance->IsEnum())
			//allocType = typeInstance->mIRType;
		allocType = mBfIRBuilder->MapTypeInst(typeInstance);
	}

	if (alignOverride != -1)
		allocAlign = alignOverride;

	// The "dynSize" cases:
	// If we are not dyn, but we are a variable size and could be looped over multiple times then we attempt to reuse
	//  the last stack space.  We won't use StackSave and StackRestore because a scoped alloc that occurs after this but
	//  needs to be retained after the current scopeData (ie: scopeData::), then it would cause these allocs to accumulate even
	//  though we'd expect their stack space to be released
	auto _CreateDynAlloc = [&](const BfIRValue& sizeValue, int align)
	{
		MarkDynStack(scopeData);

		BfType* intType = GetPrimitiveType(BfTypeCode_IntPtr);
		// For dynamically sized scoped allocs, we need to check the previous size to see if we need to realloc
		BfType* voidType = GetPrimitiveType(BfTypeCode_None);
		BfType* voidPtrType = CreatePointerType(voidType);

		if (!mCurMethodState->mDynStackRevIdx)
		{
			mCurMethodState->mDynStackRevIdx = CreateAlloca(intType, false, "curDynStackRevIdx");

			auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
			mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
			auto storeInst = mBfIRBuilder->CreateStore(GetConstValue(0), mCurMethodState->mDynStackRevIdx);
			mBfIRBuilder->ClearDebugLocation(storeInst);
			mBfIRBuilder->SetInsertPoint(prevInsertBlock);
		}

		auto byteType = GetPrimitiveType(BfTypeCode_Int8);
		auto bytePtrType = CreatePointerType(byteType);

		auto dynSizeAllocaInst = CreateAlloca(intType, false, "dynSize");
		auto dynStackRevIdx = CreateAlloca(intType, false, "dynStackRevIdx");

		auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
		mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
		auto storeInst = mBfIRBuilder->CreateStore(GetConstValue(0), dynSizeAllocaInst);
		mBfIRBuilder->ClearDebugLocation(storeInst);
		storeInst = mBfIRBuilder->CreateStore(GetConstValue(0), dynStackRevIdx);
		mBfIRBuilder->ClearDebugLocation(storeInst);
		mBfIRBuilder->SetInsertPoint(prevInsertBlock);

		auto dynPtrAllocaInst = CreateAlloca(bytePtrType, false, "dynPtr");
		if (IsTargetingBeefBackend())
		{
			// Since we don't allocate a debug variable for this, with LifetimeExtends, the easiest way
			//  to ensure there isn't any register collision during looping is just to not allow it to
			//  bind to a register at all
			mBfIRBuilder->SetAllocaForceMem(dynSizeAllocaInst);
			mBfIRBuilder->SetAllocaForceMem(dynPtrAllocaInst);
		}

		auto resizeBB = mBfIRBuilder->CreateBlock("dynAlloc.resize");
		auto endBB = mBfIRBuilder->CreateBlock("dynAlloc.end");
		auto verCheckBB = mBfIRBuilder->CreateBlock("dynAlloc.verCheck");

		auto curDynStackRevIdx = mBfIRBuilder->CreateLoad(mCurMethodState->mDynStackRevIdx);

		auto prevSize = mBfIRBuilder->CreateLoad(dynSizeAllocaInst);
		auto isGreater = mBfIRBuilder->CreateCmpGT(sizeValue, prevSize, true);
		mBfIRBuilder->CreateCondBr(isGreater, resizeBB, verCheckBB);

		mBfIRBuilder->AddBlock(verCheckBB);
		mBfIRBuilder->SetInsertPoint(verCheckBB);
		auto prevDynStackRevIdx = mBfIRBuilder->CreateLoad(dynStackRevIdx);
		auto dynStackChanged = mBfIRBuilder->CreateCmpNE(prevDynStackRevIdx, curDynStackRevIdx);
		mBfIRBuilder->CreateCondBr(dynStackChanged, resizeBB, endBB);

		mBfIRBuilder->AddBlock(resizeBB);
		mBfIRBuilder->SetInsertPoint(resizeBB);
		auto allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(byteType), sizeValue);
		mBfIRBuilder->SetAllocaAlignment(allocaInst, align);
		mBfIRBuilder->CreateStore(allocaInst, dynPtrAllocaInst);
		mBfIRBuilder->CreateStore(sizeValue, dynSizeAllocaInst);
		mBfIRBuilder->CreateStore(curDynStackRevIdx, dynStackRevIdx);
		mBfIRBuilder->CreateBr(endBB);

		mBfIRBuilder->AddBlock(endBB);
		mBfIRBuilder->SetInsertPoint(endBB);
		return mBfIRBuilder->CreateLoad(dynPtrAllocaInst);
	};

	if (isScopeAlloc)
	{
		if (arraySize)
		{
			bool isConstantArraySize = arraySize.IsConst();
			if (isRawArrayAlloc)
			{
				BfPointerType* ptrType = CreatePointerType(type);
				if ((!isDynAlloc) && (!isConstantArraySize) && (mCurMethodState->mCurScope->IsLooped(NULL)))
				{
					BfIRValue sizeValue = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
					auto loadedPtr = _CreateDynAlloc(sizeValue, type->mAlign);
					InitTypeInst(BfTypedValue(loadedPtr, ptrType), scopeData, zeroMemory, sizeValue);
					return mBfIRBuilder->CreateBitCast(loadedPtr, mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(type)));
				}
				else
				{
					auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
					isDynAlloc = (isDynAlloc) || (!arraySize.IsConst());
					if (!isDynAlloc)
						mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
					else
					{
						MarkDynStack(scopeData);
						SaveStackState(scopeData);
					}

					int typeSize = type->GetStride();

					BfIRValue allocaInst;
					BfIRValue result;
					if ((typeInstance == NULL) || (typeInstance->mIsCRepr))
					{
						allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(type), arraySize);
						result = allocaInst;
					}
					else
					{
						if (typeInstance->IsStruct())
							typeSize = typeInstance->GetInstStride();
						auto byteType = GetPrimitiveStructType(BfTypeCode_Int8);
						sizeValue = mBfIRBuilder->CreateMul(GetConstValue(typeSize), arraySize);
						allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(byteType), sizeValue);
						auto ptrType = CreatePointerType(typeInstance);
						result = mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapType(ptrType));
						if (!isDynAlloc)
							mBfIRBuilder->ClearDebugLocation(result);
					}
					if (!isDynAlloc)
						mBfIRBuilder->ClearDebugLocation(allocaInst);
					mBfIRBuilder->SetAllocaAlignment(allocaInst, allocAlign);
					if (!isDynAlloc)
						mBfIRBuilder->SetInsertPoint(prevInsertBlock);
					auto typedVal = BfTypedValue(result, type, BfTypedValueKind_Addr);

					if (arraySize.IsConst())
					{
						auto constant = mBfIRBuilder->GetConstant(arraySize);
						if (constant->mInt64 == 0)
							noDtorCall = true;
					}

					if (!noDtorCall)
					{
						bool isConstSize = arraySize.IsConst();
						BfIRBlock clearBlock;
						BfIRBlock contBlock;

						bool wantsDeinit = ((!IsOptimized()) && (!mIsComptimeModule) && (!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mIsResolveOnly));

						if (wantsDeinit)
						{
							BfIRBlock prevBlock;

							if (!isConstSize)
							{
								clearBlock = mBfIRBuilder->CreateBlock("clear");
								contBlock = mBfIRBuilder->CreateBlock("clearCont");

								prevBlock = mBfIRBuilder->GetInsertBlock();

								mBfIRBuilder->AddBlock(clearBlock);
								mBfIRBuilder->SetInsertPoint(clearBlock);
							}

							auto deferredCall = AddStackAlloc(typedVal, arraySize, NULL, scopeData, false, true);

							if (!isConstSize)
							{
								mBfIRBuilder->CreateBr(contBlock);

								mBfIRBuilder->SetInsertPoint(prevBlock);
								if (deferredCall != NULL)
								{
									// Zero out size args
									for (int i = 1; i < deferredCall->mModuleMethodInstance.mMethodInstance->GetParamCount(); i++)
									{
										auto scopedArg = deferredCall->mScopeArgs[i];
										if (scopedArg != deferredCall->mOrigScopeArgs[i])
											mBfIRBuilder->CreateStore(GetDefaultValue(deferredCall->mModuleMethodInstance.mMethodInstance->GetParamType(i)), deferredCall->mScopeArgs[i]);
									}
								}
								mBfIRBuilder->CreateCondBr(mBfIRBuilder->CreateCmpNE(arraySize, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0)), clearBlock, contBlock);

								mBfIRBuilder->AddBlock(contBlock);
								mBfIRBuilder->SetInsertPoint(contBlock);
							}
						}
					}

					return result;
				}
			}
			else
			{
				if ((!isDynAlloc) && (!arraySize.IsConst()) && (mCurMethodState->mCurScope->IsLooped(NULL)))
				{
					// Generate and check new size
					BfArrayType* arrayType = CreateArrayType(type, arrayDim);
					auto firstElementField = &arrayType->mFieldInstances.back();
					int arrayClassSize = firstElementField->mDataOffset;
					sizeValue = GetConstValue(arrayClassSize);
					BfIRValue elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
					sizeValue = mBfIRBuilder->CreateAdd(sizeValue, elementDataSize);

					auto loadedPtr = _CreateDynAlloc(sizeValue, arrayType->mAlign);
					auto typedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(loadedPtr, mBfIRBuilder->MapType(arrayType)), arrayType);
					if (!noDtorCall)
						AddStackAlloc(typedVal, BfIRValue(), NULL, scopeData, false, true);
					InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);
					return typedVal.mValue;
				}
				else
				{
					BfArrayType* arrayType = CreateArrayType(type, arrayDim);
					auto firstElementField = &arrayType->mFieldInstances.back();

					if (!type->IsValuelessType())
					{
						int arrayClassSize = firstElementField->mDataOffset;
						sizeValue = GetConstValue(arrayClassSize);
						BfIRValue elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
						sizeValue = mBfIRBuilder->CreateAdd(sizeValue, elementDataSize);
					}
					else
						sizeValue = GetConstValue(arrayType->mInstSize);

					auto prevBlock = mBfIRBuilder->GetInsertBlock();
					isDynAlloc = (isDynAlloc) || (!sizeValue.IsConst());
					if (!isDynAlloc)
						mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
					else
					{
						MarkDynStack(scopeData);
						SaveStackState(scopeData);
					}

					BfType* byteType = GetPrimitiveType(BfTypeCode_Int8);
					auto allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(byteType), sizeValue);
					if (!isDynAlloc)
						mBfIRBuilder->ClearDebugLocation(allocaInst);
					auto allocaBlock = mBfIRBuilder->GetInsertBlock();
					mBfIRBuilder->SetAllocaAlignment(allocaInst, allocAlign);

					BfTypedValue typedVal;
					if (!isDynAlloc)
					{
						mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
						typedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapType(arrayType)), arrayType);
						mBfIRBuilder->ClearDebugLocation_Last();
						mBfIRBuilder->SetInsertPoint(prevBlock);
						allocaBlock = mCurMethodState->mIRInitBlock;
					}
					else
						typedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapType(arrayType)), arrayType);

					if (!noDtorCall)
						AddStackAlloc(typedVal, BfIRValue(), NULL, scopeData, false, true, allocaBlock);
					InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);
					return typedVal.mValue;
				}
			}
		}
		else if (appendSizeValue)
		{
			// It may seem that we should check IsLooped against only the requested scope, BUT- if later on we perform
			//  a scoped alloc that crosses our boundary then we won't be able to reclaim the stack space, so we always
			//  use the "dynSize" version conservatively
			if ((!isDynAlloc) && (!appendSizeValue.IsConst()) && (mCurMethodState->mCurScope->IsLooped(NULL)))
			{
				// Generate and check new size
				sizeValue = GetConstValue(typeInstance->mInstSize);
				sizeValue = mBfIRBuilder->CreateAdd(sizeValue, appendSizeValue);

				auto loadedPtr = _CreateDynAlloc(sizeValue, typeInstance->mAlign);
				auto typedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(loadedPtr, mBfIRBuilder->MapTypeInstPtr(typeInstance)), typeInstance);
				if (!noDtorCall)
					AddStackAlloc(typedVal, arraySize, NULL, scopeData, mCurMethodState->mInConditionalBlock, true);
				InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);
				return typedVal.mValue;
			}
			else // stack alloc, unlooped, with append
			{
				sizeValue = GetConstValue(typeInstance->mInstSize);
				sizeValue = mBfIRBuilder->CreateAdd(sizeValue, appendSizeValue);
				auto prevBlock = mBfIRBuilder->GetInsertBlock();
				bool wasDynAlloc = isDynAlloc;
				isDynAlloc = (isDynAlloc) || (!sizeValue.IsConst());
				if (!isDynAlloc)
					mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
				else
				{
					MarkDynStack(scopeData);
					SaveStackState(scopeData);
				}
				auto sbyteType = GetPrimitiveType(BfTypeCode_Int8);
				auto allocaInst = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(sbyteType), sizeValue);
				if (!isDynAlloc)
				{
					mBfIRBuilder->ClearDebugLocation(allocaInst);
				}
				mBfIRBuilder->SetAllocaAlignment(allocaInst, allocAlign);

				bool mayBeLarge = false;
				if (sizeValue.IsConst())
				{
					auto constantInt = mBfIRBuilder->GetConstant(sizeValue);
					if (constantInt->mInt64 >= 4096)
						mayBeLarge = true;
				}
				else
					mayBeLarge = true;

				if (((type->IsObject() && (!mayBeLarge))) ||
					// If we create a memset that can fill in backwards (or at least 4k chunked backward) then we can remove the '!mayBeLarge' cond
					((zeroMemory) && (!mayBeLarge)))
				{
					mBfIRBuilder->SetAllocaNoChkStkHint(allocaInst);
				}
				BfIRValue castedVal;

				if (!isDynAlloc)
				{
					mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
					castedVal = mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapTypeInstPtr(typeInstance));
					mBfIRBuilder->ClearDebugLocation(castedVal);
					mBfIRBuilder->SetInsertPoint(prevBlock);
					if (WantsLifetimes())
					{
						mBfIRBuilder->CreateLifetimeStart(allocaInst);
						scopeData->mDeferredLifetimeEnds.push_back(allocaInst);
					}
				}
				else
				{
					castedVal = mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapTypeInstPtr(typeInstance));
				}

				auto typedVal = BfTypedValue(castedVal, typeInstance);
				if (!noDtorCall)
				{
					bool doCondAlloca = false;
					if (!IsTargetingBeefBackend())
						doCondAlloca = !wasDynAlloc && isDynAlloc && mCurMethodState->mInConditionalBlock;
					AddStackAlloc(typedVal, arraySize, NULL, scopeData, doCondAlloca, true);
				}
				InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);

				return typedVal.mValue;
			}
		}
		else // "Normal" stack alloc
		{
			BF_ASSERT(!sizeValue);
			//TODO: If sizeValue is a constant then do this in the head IR builder
			auto prevBlock = mBfIRBuilder->GetInsertBlock();
			if (!isLoopedAlloc)
				mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
			else
			{
				MarkDynStack(scopeData);
				SaveStackState(scopeData);
			}
			auto allocaInst = mBfIRBuilder->CreateAlloca(allocType);
			if (!isLoopedAlloc)
				mBfIRBuilder->ClearDebugLocation(allocaInst);
			mBfIRBuilder->SetAllocaAlignment(allocaInst, allocAlign);
			bool mayBeLarge = allocSize >= 4096;
			if (((typeInstance != NULL) && (typeInstance->IsObject()) && (!mayBeLarge)) ||
				// If we create a memset that can fill in backwards (or at least 4k chunked backward) then we can remove the '!mayBeLarge' cond
				((zeroMemory) && (!mayBeLarge)))
			{
				// With an object, we know we will at least set the vdata pointer which will probe the start of the new address,
				//  and memset is always safe
				mBfIRBuilder->SetAllocaNoChkStkHint(allocaInst);
			}
			auto typedVal = BfTypedValue(allocaInst, type);
			if (!isLoopedAlloc)
			{
				mBfIRBuilder->ClearDebugLocation(allocaInst);
				mBfIRBuilder->SetInsertPoint(prevBlock);
				if (WantsLifetimes())
				{
					mBfIRBuilder->CreateLifetimeStart(allocaInst);
					scopeData->mDeferredLifetimeEnds.push_back(allocaInst);
				}
			}
			if (!noDtorCall)
				AddStackAlloc(typedVal, BfIRValue(), NULL, scopeData, false, true);
			if (typeInstance != NULL)
			{
				InitTypeInst(typedVal, scopeData, zeroMemory, GetConstValue(typeInstance->mInstSize));
			}
			else
			{
				mBfIRBuilder->CreateMemSet(allocaInst, GetConstValue8(0), GetConstValue(allocSize), allocAlign);
			}
			return allocaInst;
		}
	}
	else if (arraySize)
	{
		if (isRawArrayAlloc)
		{
			sizeValue = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
			return AllocBytes(allocTarget.mRefNode, allocTarget, type, sizeValue, GetConstValue(allocAlign), allocFlags);
		}
		else
		{
			BfArrayType* arrayType = CreateArrayType(type, arrayDim);
			typeInstance = arrayType;
			BfIRValue arraySizeMinusOne = mBfIRBuilder->CreateSub(arraySize, GetConstValue(1));
			BfIRValue elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySizeMinusOne);
			appendSizeValue = elementDataSize;

			if (!type->IsSizeAligned())
			{
				appendSizeValue = mBfIRBuilder->CreateAdd(appendSizeValue, GetConstValue(type->GetStride() - type->mSize));
			}
		}
	}

	if ((typeInstance != NULL) && (typeInstance->IsObject()))
	{
		auto vDataRef = GetClassVDataPtr(typeInstance);
		BfIRValue result;
		bool isResultInitialized = false;

		int stackCount = mCompiler->mOptions.mAllocStackCount;
		if (typeInstance->mTypeOptionsIdx != -1)
		{
			auto typeOptions = mSystem->GetTypeOptions(typeInstance->mTypeOptionsIdx);
			stackCount = BfTypeOptions::Apply(stackCount, typeOptions->mAllocStackTraceDepth);
		}
		if (mIsComptimeModule)
			stackCount = 0;

		if (!sizeValue)
			sizeValue = GetConstValue(typeInstance->mInstSize);
		if (appendSizeValue)
			sizeValue = mBfIRBuilder->CreateAdd(sizeValue, appendSizeValue);
		BfIRValue origSizeValue = sizeValue;

		bool isAllocEx = hasCustomAllocator && (stackCount > 1);

		if (isAllocEx)
		{
			auto prepareStackTraceMethod = GetInternalMethod("Dbg_PrepareStackTrace");
			if (prepareStackTraceMethod)
			{
				SizedArray<BfIRValue, 2> irArgs;
				irArgs.Add(sizeValue);
				irArgs.Add(GetConstValue(stackCount));
				auto addlBytes = mBfIRBuilder->CreateCall(prepareStackTraceMethod.mFunc, irArgs);
				sizeValue = mBfIRBuilder->CreateAdd(sizeValue, addlBytes);
			}
		}

		if (allocTarget.mCustomAllocator)
		{
			auto customAlloc = allocTarget.mCustomAllocator.mType->ToTypeInstance();
			// Use AllocObject if we have it, otherwise we just use AllocBytes further down
			if ((customAlloc != NULL) && (GetRawMethodByName(customAlloc, "AllocObject", -1, true, true) != NULL))
			{
				auto classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);
				mBfIRBuilder->PopulateType(classVDataType);
				auto typeInstType = ResolveTypeDef(mCompiler->mReflectTypeInstanceTypeDef)->ToTypeInstance();
				mBfIRBuilder->PopulateType(typeInstType);
				auto typePtrPtr = mBfIRBuilder->CreateInBoundsGEP(vDataRef, 0, 1); // mType
				auto typePtr = mBfIRBuilder->CreateLoad(typePtrPtr);
				auto typeInstPtr = mBfIRBuilder->CreateBitCast(typePtr, mBfIRBuilder->MapTypeInstPtr(typeInstType));

				BfTypedValueExpression typedValueExpr;
				typedValueExpr.Init(BfTypedValue(typeInstPtr, typeInstType));
				typedValueExpr.mRefNode = allocTarget.mRefNode;

				BfExprEvaluator exprEvaluator(this);
				SizedArray<BfExpression*, 2> argExprs;
				argExprs.push_back(&typedValueExpr);

				BfTypedValueExpression sizeValueExpr;
				sizeValueExpr.Init(BfTypedValue(sizeValue, GetPrimitiveType(BfTypeCode_IntPtr)));
 				sizeValueExpr.mRefNode = allocTarget.mRefNode;
 				argExprs.push_back(&sizeValueExpr);

				BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
				BfResolvedArgs argValues(&sizedArgExprs);
				exprEvaluator.ResolveArgValues(argValues);
				exprEvaluator.mNoBind = true;
				BfTypedValue allocResult = exprEvaluator.MatchMethod(allocTarget.mRefNode, NULL, allocTarget.mCustomAllocator, false, false, "AllocObject", argValues, BfMethodGenericArguments());
				if (allocResult)
				{
					if ((allocResult.mType->IsVoidPtr()) || (allocResult.mType == mContext->mBfObjectType))
						result = mBfIRBuilder->CreateBitCast(allocResult.mValue, mBfIRBuilder->MapType(typeInstance));
					else
						result = CastToValue(allocTarget.mRefNode, allocResult, typeInstance, BfCastFlags_Explicit);
					isResultInitialized = true;
				}
			}
		}

		bool wasAllocated = false;
		if (!result)
		{
			if (hasCustomAllocator)
				result = AllocBytes(allocTarget.mRefNode, allocTarget, typeInstance, sizeValue, GetConstValue(typeInstance->mInstAlign), (BfAllocFlags)(BfAllocFlags_ZeroMemory | BfAllocFlags_NoDefaultToMalloc));
			else if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
			{
				SizedArray<BfIRValue, 4> llvmArgs;
				llvmArgs.push_back(sizeValue);
				auto irFunc = GetBuiltInFunc(BfBuiltInFuncType_Malloc);
				result = mBfIRBuilder->CreateCall(irFunc, llvmArgs);
				result = mBfIRBuilder->CreateBitCast(result, mBfIRBuilder->MapTypeInstPtr(typeInstance));
				wasAllocated = true;
			}
		}

		if (result)
		{
			if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
			{
				auto objectPtr = mBfIRBuilder->CreateBitCast(result, mBfIRBuilder->MapTypeInstPtr(mContext->mBfObjectType));
				SizedArray<BfIRValue, 4> llvmArgs;
				llvmArgs.push_back(objectPtr);
				llvmArgs.push_back(origSizeValue);
				llvmArgs.push_back(vDataRef);
				auto objectCreatedMethod = GetInternalMethod(isAllocEx ?
					(isResultInitialized ? "Dbg_ObjectCreatedEx" : "Dbg_ObjectAllocatedEx") :
					(isResultInitialized ? "Dbg_ObjectCreated" : "Dbg_ObjectAllocated"));
				mBfIRBuilder->CreateCall(objectCreatedMethod.mFunc, llvmArgs);

				if (wasAllocated)
				{
					auto byteType = GetPrimitiveType(BfTypeCode_UInt8);
					auto bytePtrType = CreatePointerType(byteType);
					auto flagsPtr = mBfIRBuilder->CreateBitCast(result, mBfIRBuilder->MapType(bytePtrType));
					auto flagsVal = mBfIRBuilder->CreateLoad(flagsPtr);
					auto modifiedFlagsVal = mBfIRBuilder->CreateOr(flagsVal, mBfIRBuilder->CreateConst(BfTypeCode_UInt8, /*BF_OBJECTFLAG_ALLOCATED*/4));
					mBfIRBuilder->CreateStore(modifiedFlagsVal, flagsPtr);
				}
			}
			else
			{
				auto ptrType = mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr);
				auto vDataPtr = mBfIRBuilder->CreateBitCast(vDataRef, ptrType);
				auto vDataMemberPtr = mBfIRBuilder->CreateBitCast(result, mBfIRBuilder->GetPointerTo(ptrType));
				mBfIRBuilder->CreateStore(vDataPtr, vDataMemberPtr);
			}
		}
		else
		{
			if ((mBfIRBuilder->mIgnoreWrites) ||
				((mCompiler->mIsResolveOnly) && (!mIsComptimeModule)))
				return GetDefaultValue(typeInstance);

			auto classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);
			auto vData = mBfIRBuilder->CreateBitCast(vDataRef, mBfIRBuilder->MapTypeInstPtr(classVDataType->ToTypeInstance()));

			if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
			{
				SizedArray<BfIRValue, 4> llvmArgs;
				llvmArgs.push_back(vData);
				llvmArgs.push_back(sizeValue);
				llvmArgs.push_back(GetConstValue(typeInstance->mAlign));
				llvmArgs.push_back(GetConstValue(stackCount));
				auto moduleMethodInstance = GetInternalMethod("Dbg_ObjectAlloc", 4);
				BfIRValue objectVal = mBfIRBuilder->CreateCall(moduleMethodInstance.mFunc, llvmArgs);
				result = mBfIRBuilder->CreateBitCast(objectVal, mBfIRBuilder->MapType(typeInstance));
			}
			else
			{
				SizedArray<BfIRValue, 4> llvmArgs;
				llvmArgs.push_back(sizeValue);
				BfIRFunction irFunc;
				if ((mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
				{
					auto moduleMethodInstance = GetInternalMethod("Dbg_RawObjectAlloc", 1);
					irFunc = moduleMethodInstance.mFunc;
				}
				if (!irFunc)
					irFunc = GetBuiltInFunc(BfBuiltInFuncType_Malloc);
				BfIRValue objectVal = mBfIRBuilder->CreateCall(irFunc, llvmArgs);
				auto objResult = mBfIRBuilder->CreateBitCast(objectVal, mBfIRBuilder->MapType(mContext->mBfObjectType, BfIRPopulateType_Full));
				auto vdataPtr = mBfIRBuilder->CreateInBoundsGEP(objResult, 0, 0);

				if (mIsComptimeModule)
				{
					vdataPtr = mBfIRBuilder->CreateBitCast(vdataPtr, mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapTypeInstPtr(classVDataType->ToTypeInstance())));
				}

				mBfIRBuilder->CreateStore(vData, vdataPtr);
				result = mBfIRBuilder->CreateBitCast(objectVal, mBfIRBuilder->MapType(typeInstance));
			}
		}

		if ((zeroMemory) && (arraySize))
		{
			BfArrayType* arrayType = CreateArrayType(type, arrayDim);
			typeInstance = arrayType;
			auto firstElementField = &arrayType->mFieldInstances.back();
			int arrayClassSize = firstElementField->mDataOffset;

			BfIRValue elementDataSize;
			bool skipZero = false;
			if (arraySize.IsConst())
			{
				BfIRValue arraySizeMinusOne = mBfIRBuilder->CreateSub(arraySize, GetConstValue(1));
				elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySizeMinusOne);
				elementDataSize = mBfIRBuilder->CreateAdd(elementDataSize, GetConstValue(type->mSize));

				auto constVal = mBfIRBuilder->GetConstant(elementDataSize);
				if (constVal->mInt64 <= 0)
					skipZero = true;
			}
			else
			{
				// For non const, don't use an unpadded last element - saves us checking against negative numbers going into memset for the zero-length case
				elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
			}

			if (!skipZero)
			{
				auto i8Type = GetPrimitiveType(BfTypeCode_Int8);
				auto ptrType = CreatePointerType(i8Type);
				auto ptrVal = mBfIRBuilder->CreateBitCast(result, mBfIRBuilder->MapType(ptrType));
				ptrVal = mBfIRBuilder->CreateInBoundsGEP(ptrVal, arrayClassSize);
				mBfIRBuilder->CreateMemSet(ptrVal, GetConstValue8(0), elementDataSize, type->mAlign);
				appendSizeValue = elementDataSize;
			}
		}

		return result;
	}
	else
	{
		if (!sizeValue)
			sizeValue = GetConstValue(allocSize);
		return AllocBytes(allocTarget.mRefNode, allocTarget, type, sizeValue, GetConstValue(allocAlign), zeroMemory ? BfAllocFlags_ZeroMemory : BfAllocFlags_None);
	}
}

void BfModule::ValidateAllocation(BfType* type, BfAstNode* refNode)
{
	if ((type == NULL) || (refNode == NULL))
		return;
	auto typeInstance = type->ToTypeInstance();
	if ((typeInstance != NULL) && (typeInstance->mTypeDef->mIsOpaque))
		Fail(StrFormat("Unable to allocate opaque type '%s'", TypeToString(type).c_str()), refNode);
}

void BfModule::EmitAppendAlign(int align, int sizeMultiple)
{
	if (align <= 1)
		return;

	if (sizeMultiple == 0)
		sizeMultiple = align;
	if ((mCurMethodState->mCurAppendAlign != 0) && (mCurMethodState->mCurAppendAlign % align != 0))
	{
		if (!IsAllocatorAligned())
		{
			// Don't align
		}
		else if (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)
		{
			auto localVar = mCurMethodState->GetRootMethodState()->mLocals[1];
			BF_ASSERT(localVar->mName == "appendIdx");
			auto appendIdxVal = BfTypedValue(localVar->mValue, localVar->mResolvedType, true);
			BfIRValue appendCurIdx = mBfIRBuilder->CreateLoad(appendIdxVal.mValue);
			if (align > 1)
			{
				appendCurIdx = mBfIRBuilder->CreateAdd(appendCurIdx, GetConstValue(align - 1));
				appendCurIdx = mBfIRBuilder->CreateAnd(appendCurIdx, GetConstValue(~(align - 1)));
			}
			mBfIRBuilder->CreateStore(appendCurIdx, appendIdxVal.mValue);
		}
		else
		{
			BF_ASSERT(mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend);
			BfIRValue appendCurIdxPtr = mCurMethodState->mRetVal.mValue;
			BfIRValue appendCurIdx = mBfIRBuilder->CreateLoad(appendCurIdxPtr);
			appendCurIdx = mBfIRBuilder->CreateAdd(appendCurIdx, GetConstValue(align - 1));
			appendCurIdx = mBfIRBuilder->CreateAnd(appendCurIdx, GetConstValue(~(align - 1)));
			mBfIRBuilder->CreateStore(appendCurIdx, appendCurIdxPtr);
		}
	}

	if (mCurMethodState->mCurAppendAlign == 0)
		mCurMethodState->mCurAppendAlign = sizeMultiple;
	else
	{
		if (sizeMultiple % align == 0)
			mCurMethodState->mCurAppendAlign = align;
		else
			mCurMethodState->mCurAppendAlign = sizeMultiple % align;
	}
}

BfIRValue BfModule::AppendAllocFromType(BfType* type, BfIRValue appendSizeValue, int appendAllocAlign, BfIRValue arraySize, int arrayDim, bool isRawArrayAlloc, bool zeroMemory)
{
	auto localVar = mCurMethodState->GetRootMethodState()->mLocals[1];
	BF_ASSERT(localVar->mName == "appendIdx");
	BfTypedValue appendIdxVal(localVar->mValue, localVar->mResolvedType, true);

	BfIRValue retValue;
	BfTypeInstance* retTypeInstance = NULL;
	auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
	auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);

	if (arraySize)
	{
		if (isRawArrayAlloc)
		{
			EmitAppendAlign(type->mAlign);
			BfPointerType* ptrType = CreatePointerType(type);
			BfIRValue sizeValue = mBfIRBuilder->CreateMul(GetConstValue(type->mSize), arraySize);
			auto curIdxVal = mBfIRBuilder->CreateLoad(appendIdxVal.mValue);
			if (mSystem->mPtrSize != 4)
				sizeValue = mBfIRBuilder->CreateNumericCast(sizeValue, true, (intPtrType->mSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64);
			auto newIdxVal = mBfIRBuilder->CreateAdd(curIdxVal, sizeValue);
			mBfIRBuilder->CreateStore(newIdxVal, appendIdxVal.mValue);

			if (zeroMemory)
			{
				auto ptr = mBfIRBuilder->CreateIntToPtr(curIdxVal, mBfIRBuilder->MapType(voidPtrType));
				mBfIRBuilder->CreateMemSet(ptr, GetConstValue8(0), sizeValue, type->mAlign);
			}

			return mBfIRBuilder->CreateIntToPtr(curIdxVal, mBfIRBuilder->MapType(ptrType));
		}
		else
		{
			BfArrayType* arrayType = CreateArrayType(type, arrayDim);
			EmitAppendAlign(arrayType->mAlign, type->mSize);

			auto firstElementField = &arrayType->mFieldInstances.back();
			int arrayClassSize = arrayType->mInstSize - firstElementField->mDataSize;

			BfIRValue sizeValue = GetConstValue(arrayClassSize);
			BfIRValue elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->mSize), arraySize);
			sizeValue = mBfIRBuilder->CreateAdd(sizeValue, elementDataSize);

			auto curIdxVal = mBfIRBuilder->CreateLoad(appendIdxVal.mValue);
			if (mSystem->mPtrSize != 4)
				sizeValue = mBfIRBuilder->CreateNumericCast(sizeValue, true, (intPtrType->mSize == 4) ? BfTypeCode_Int32 : BfTypeCode_Int64);
			auto newIdxVal = mBfIRBuilder->CreateAdd(curIdxVal, sizeValue);
			mBfIRBuilder->CreateStore(newIdxVal, appendIdxVal.mValue);

			if (zeroMemory)
			{
				// Only zero out the actual elements
				int dataOffset = arrayType->mFieldInstances.back().mDataOffset;
				auto newOffset = mBfIRBuilder->CreateAdd(curIdxVal, GetConstValue(dataOffset));
				auto newSize = mBfIRBuilder->CreateSub(sizeValue, GetConstValue(dataOffset));
				auto ptr = mBfIRBuilder->CreateIntToPtr(newOffset, mBfIRBuilder->MapType(voidPtrType));
				mBfIRBuilder->CreateMemSet(ptr, GetConstValue8(0), newSize, type->mAlign);
			}

			retTypeInstance = arrayType;
			retValue = mBfIRBuilder->CreateIntToPtr(curIdxVal, mBfIRBuilder->MapType(arrayType));
		}
	}
	else
	{
		auto typeInst = type->ToTypeInstance();
		BfIRValue sizeValue;
		BfIRType toType;
		if (typeInst != NULL)
		{
			EmitAppendAlign(typeInst->mInstAlign, typeInst->mInstSize);
			sizeValue = GetConstValue(typeInst->mInstSize);
			toType = mBfIRBuilder->MapTypeInstPtr(typeInst);
		}
		else
		{
			EmitAppendAlign(type->mAlign, type->mSize);
			sizeValue = GetConstValue(type->mSize);
			auto toPtrType = CreatePointerType(type);
			toType = mBfIRBuilder->MapType(toPtrType);
		}

		auto curIdxVal = mBfIRBuilder->CreateLoad(appendIdxVal.mValue);
		auto newIdxVal = mBfIRBuilder->CreateAdd(curIdxVal, sizeValue);
		mBfIRBuilder->CreateStore(newIdxVal, appendIdxVal.mValue);

		retTypeInstance = typeInst;
		retValue = mBfIRBuilder->CreateIntToPtr(curIdxVal, toType);
	}

	if ((retTypeInstance != NULL) && (retTypeInstance->IsObject()))
	{
		mBfIRBuilder->PopulateType(retTypeInstance);
		auto int8Type = mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8);
		auto ptrType = mBfIRBuilder->GetPointerTo(int8Type);
		auto ptrPtrType = mBfIRBuilder->GetPointerTo(ptrType);

		auto curThis = GetThis();

		BfIRValue newFlags;
		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
		{
			auto thisFlagsPtr = mBfIRBuilder->CreateBitCast(curThis.mValue, ptrType);
			auto thisFlags = mBfIRBuilder->CreateLoad(thisFlagsPtr);
			auto maskedThisFlags = mBfIRBuilder->CreateAnd(thisFlags, GetConstValue8(BfObjectFlag_StackAlloc)); // Take 'stack' flag from 'this'
			newFlags = mBfIRBuilder->CreateOr(maskedThisFlags, GetConstValue8(BfObjectFlag_AppendAlloc));
		}

		auto vObjectAddr = mBfIRBuilder->CreateInBoundsGEP(retValue, 0, 0);

		auto vDataRef = CreateClassVDataGlobal(retTypeInstance);

		auto destAddr = mBfIRBuilder->CreateBitCast(vObjectAddr, ptrPtrType);
		auto srcVal = mBfIRBuilder->CreateBitCast(vDataRef, ptrType);
		mBfIRBuilder->CreateStore(srcVal, destAddr);

		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
		{
			auto flagsPtr = mBfIRBuilder->CreateBitCast(destAddr, ptrType);
			mBfIRBuilder->CreateStore(newFlags, flagsPtr);
		}
	}

	if (appendSizeValue)
	{
		EmitAppendAlign(appendAllocAlign);
		auto curIdxVal = mBfIRBuilder->CreateLoad(appendIdxVal.mValue);
		auto newIdxVal = mBfIRBuilder->CreateAdd(curIdxVal, appendSizeValue);
		mBfIRBuilder->CreateStore(newIdxVal, appendIdxVal.mValue);
	}

	return retValue;
}

bool BfModule::IsOptimized()
{
	if (mProject == NULL)
		return false;
	if (mIsComptimeModule)
		return false;
	int optLevel = GetModuleOptions().mOptLevel;
	return (optLevel >= BfOptLevel_O1) && (optLevel <= BfOptLevel_O3);
}

bool BfModule::IsTargetingBeefBackend()
{
	if (mProject == NULL)
		return true;
	return GetModuleOptions().mOptLevel == BfOptLevel_OgPlus;
}

bool BfModule::WantsLifetimes()
{
	if (mBfIRBuilder->mIgnoreWrites)
		return false;

	if ((mIsComptimeModule) && (mBfIRBuilder->HasDebugLocation()))
		return true;
	else if (mProject == NULL)
		return false;

	return GetModuleOptions().mOptLevel == BfOptLevel_OgPlus;
}

bool BfModule::HasCompiledOutput()
{
	return (!mSystem->mIsResolveOnly) && (mGeneratesCode) && (!mIsComptimeModule);
}

bool BfModule::HasExecutedOutput()
{
	return ((!mSystem->mIsResolveOnly) && (mGeneratesCode)) || (mIsComptimeModule);
}

// We will skip the object access check for any occurrences of this value
void BfModule::SkipObjectAccessCheck(BfTypedValue typedVal)
{
	if ((mBfIRBuilder->mIgnoreWrites) || (!typedVal.mType->IsObjectOrInterface()) || (mCurMethodState == NULL) || (mCurMethodState->mIgnoreObjectAccessCheck))
		return;

	if ((!mCompiler->mOptions.mObjectHasDebugFlags) || (mIsComptimeModule))
		return;

	if ((typedVal.mValue.mFlags & BfIRValueFlags_Value) == 0)
		return;

	mCurMethodState->mSkipObjectAccessChecks.Add(typedVal.mValue.mId);
}

void BfModule::EmitObjectAccessCheck(BfTypedValue typedVal)
{
	if ((mBfIRBuilder->mIgnoreWrites) || (!typedVal.mType->IsObjectOrInterface()) || (mCurMethodState == NULL) || (mCurMethodState->mIgnoreObjectAccessCheck))
		return;

	if ((!mCompiler->mOptions.mObjectHasDebugFlags) || (mIsComptimeModule))
		return;

	if (typedVal.mValue.IsConst())
	{
		int stringIdx = GetStringPoolIdx(typedVal.mValue, mBfIRBuilder);
		if (stringIdx != -1)
			return;
		auto constant = mBfIRBuilder->GetConstant(typedVal.mValue);
		if (constant->mTypeCode == BfTypeCode_NullPtr)
			return;
		if (constant->mConstType == BfConstType_BitCastNull)
			return;
	}

	bool emitObjectAccessCheck = mCompiler->mOptions.mEmitObjectAccessCheck;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		emitObjectAccessCheck = typeOptions->Apply(emitObjectAccessCheck, BfOptionFlags_EmitObjectAccessCheck);
	if (!emitObjectAccessCheck)
		return;

	if ((typedVal.mValue.mFlags & BfIRValueFlags_Value) != 0)
	{
		if (mCurMethodState->mSkipObjectAccessChecks.Contains(typedVal.mValue.mId))
			return;
	}

	if (typedVal.IsAddr())
		typedVal = LoadValue(typedVal);

	mBfIRBuilder->CreateObjectAccessCheck(typedVal.mValue, !IsOptimized());
}

void BfModule::EmitEnsureInstructionAt()
{
	if (mBfIRBuilder->mIgnoreWrites)
		return;

	if (mIsComptimeModule)
	{
		// Always add
	}
	else if ((mProject == NULL) || (!mHasFullDebugInfo) || (IsOptimized()) || (mCompiler->mOptions.mOmitDebugHelpers))
		return;

	mBfIRBuilder->CreateEnsureInstructionAt();
}

void BfModule::EmitDynamicCastCheck(const BfTypedValue& targetValue, BfType* targetType, BfIRBlock trueBlock, BfIRBlock falseBlock, bool nullSucceeds)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return; // Nothing needed here

	auto irb = mBfIRBuilder;

	auto checkBB = irb->CreateBlock("as.check");
	auto isNull = irb->CreateIsNull(targetValue.mValue);
	mBfIRBuilder->CreateCondBr(isNull, nullSucceeds ? trueBlock : falseBlock, checkBB);

	if (mIsComptimeModule)
	{
		AddBasicBlock(checkBB);
		auto callResult = mBfIRBuilder->Comptime_DynamicCastCheck(targetValue.mValue, targetType->mTypeId, mBfIRBuilder->MapType(mContext->mBfObjectType));
		auto cmpResult = mBfIRBuilder->CreateCmpNE(callResult, GetDefaultValue(mContext->mBfObjectType));
		irb->CreateCondBr(cmpResult, trueBlock, falseBlock);
		return;
	}

	auto intType = GetPrimitiveType(BfTypeCode_IntPtr);
	auto intPtrType = CreatePointerType(intType);
	auto intPtrPtrType = CreatePointerType(intPtrType);
	auto int32Type = GetPrimitiveType(BfTypeCode_Int32);
	auto int32PtrType = CreatePointerType(int32Type);

	auto typeTypeInstance = ResolveTypeDef(mCompiler->mReflectTypeInstanceTypeDef)->ToTypeInstance();

	if (mCompiler->mOptions.mAllowHotSwapping)
	{
		BfExprEvaluator exprEvaluator(this);

		AddBasicBlock(checkBB);
		auto objectParam = mBfIRBuilder->CreateBitCast(targetValue.mValue, mBfIRBuilder->MapType(mContext->mBfObjectType));
		auto moduleMethodInstance = GetMethodByName(mContext->mBfObjectType, targetType->IsInterface() ? "DynamicCastToInterface" : "DynamicCastToTypeId");
		SizedArray<BfIRValue, 4> irArgs;
		irArgs.push_back(objectParam);
		irArgs.push_back(GetConstValue32(targetType->mTypeId));
		auto callResult = exprEvaluator.CreateCall(NULL, moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, irArgs);
		auto cmpResult = mBfIRBuilder->CreateCmpNE(callResult.mValue, GetDefaultValue(callResult.mType));
		irb->CreateCondBr(cmpResult, trueBlock, falseBlock);
	}
	else
	{
		AddBasicBlock(checkBB);
		BfIRValue vDataPtr = irb->CreateBitCast(targetValue.mValue, irb->MapType(intPtrType));
		vDataPtr = irb->CreateLoad(vDataPtr);
		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
			vDataPtr = irb->CreateAnd(vDataPtr, irb->CreateConst(BfTypeCode_IntPtr, (uint64)~0xFFULL));

		if (targetType->IsInterface())
		{
			auto targetTypeInst = targetType->ToTypeInstance();
			AddDependency(targetType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);

			// Skip past mType, but since a 'GetDynCastVDataCount()' data is included in the sSlotOfs, we need to remove that
			//int inheritanceIdOfs = mSystem->mPtrSize - (mCompiler->GetDynCastVDataCount())*4;
			//vDataPtr = irb->CreateAdd(vDataPtr, irb->CreateConst(BfTypeCode_IntPtr, inheritanceIdOfs));

			vDataPtr = irb->CreateAdd(vDataPtr, irb->GetConfigConst(BfIRConfigConst_DynSlotOfs, BfTypeCode_IntPtr));
			BfIRValue slotOfs = GetInterfaceSlotNum(targetType->ToTypeInstance());
			BfIRValue slotByteOfs = irb->CreateMul(slotOfs, irb->CreateConst(BfTypeCode_Int32, 4));
			slotByteOfs = irb->CreateNumericCast(slotByteOfs, false, BfTypeCode_IntPtr);
			vDataPtr = irb->CreateAdd(vDataPtr, slotByteOfs);
			vDataPtr = irb->CreateIntToPtr(vDataPtr, irb->MapType(int32PtrType));
			BfIRValue typeId = irb->CreateLoad(vDataPtr);
			BfIRValue cmpResult = irb->CreateCmpEQ(typeId, irb->CreateConst(BfTypeCode_Int32, targetType->mTypeId));
			irb->CreateCondBr(cmpResult, trueBlock, falseBlock);
		}
		else
		{
			if (!targetType->IsTypeInstance())
				targetType = GetWrappedStructType(targetType);

			AddDependency(targetType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
			int inheritanceIdOfs = mSystem->mPtrSize;
			vDataPtr = irb->CreateAdd(vDataPtr, irb->CreateConst(BfTypeCode_IntPtr, inheritanceIdOfs));
			vDataPtr = irb->CreateIntToPtr(vDataPtr, irb->MapType(int32PtrType));
			BfIRValue objInheritanceId = irb->CreateLoad(vDataPtr);
			BfIRValue typeDataRef = CreateTypeDataRef(targetType);
			BfIRValue typeInstDataRef = irb->CreateBitCast(typeDataRef, irb->MapType(typeTypeInstance, BfIRPopulateType_Full));
			auto fieldInst = &typeTypeInstance->mFieldInstances[9];
			BF_ASSERT(fieldInst->GetFieldDef()->mName == "mInheritanceId");
			BfIRValue gepValue = irb->CreateInBoundsGEP(typeInstDataRef, 0, fieldInst->mDataIdx);
			BfIRValue wantInheritanceId = irb->CreateLoad(gepValue);
			fieldInst = &typeTypeInstance->mFieldInstances[10];
			BF_ASSERT(fieldInst->GetFieldDef()->mName == "mInheritanceCount");
			gepValue = irb->CreateInBoundsGEP(typeInstDataRef, 0, fieldInst->mDataIdx);
			BfIRValue inheritanceCount = irb->CreateLoad(gepValue);
			BfIRValue idDiff = irb->CreateSub(objInheritanceId, wantInheritanceId);
			BfIRValue cmpResult = irb->CreateCmpLTE(idDiff, inheritanceCount, false);
			irb->CreateCondBr(cmpResult, trueBlock, falseBlock);
		}
	}
}

void BfModule::EmitDynamicCastCheck(BfTypedValue typedVal, BfType* type, bool allowNull)
{
	if (mBfIRBuilder->mIgnoreWrites)
		return;

	bool emitDynamicCastCheck = mCompiler->mOptions.mEmitDynamicCastCheck;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		emitDynamicCastCheck = typeOptions->Apply(emitDynamicCastCheck, BfOptionFlags_EmitDynamicCastCheck);

	if (emitDynamicCastCheck)
	{
		int wantTypeId = 0;
		if (!type->IsGenericParam())
			wantTypeId = type->mTypeId;

		BfIRBlock failBlock = mBfIRBuilder->CreateBlock("dynFail");
		BfIRBlock endBlock = mBfIRBuilder->CreateBlock("dynEnd");

		EmitDynamicCastCheck(typedVal, type, endBlock, failBlock, allowNull ? true : false);

		AddBasicBlock(failBlock);

		if (mIsComptimeModule)
		{
			mBfIRBuilder->Comptime_Error(CeErrorKind_ObjectDynCheckFailed);
		}
		else
		{
			SizedArray<BfIRValue, 8> llvmArgs;
			auto bitAddr = mBfIRBuilder->CreateBitCast(typedVal.mValue, mBfIRBuilder->MapType(mContext->mBfObjectType));
			llvmArgs.push_back(bitAddr);
			llvmArgs.push_back(GetConstValue32(wantTypeId));
			auto objDynCheck = GetInternalMethod("ObjectDynCheckFailed");
			BF_ASSERT(objDynCheck);
			if (objDynCheck)
			{
				auto callInst = mBfIRBuilder->CreateCall(objDynCheck.mFunc, llvmArgs);
			}
		}

		mBfIRBuilder->CreateBr(endBlock);

		AddBasicBlock(endBlock);
	}
}

BfTypedValue BfModule::BoxValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType, const BfAllocTarget& allocTarget, BfCastFlags castFlags)
{
	bool callDtor = (castFlags & BfCastFlags_NoBoxDtor) == 0;
	bool wantConst = ((castFlags & BfCastFlags_WantsConst) != 0) && (typedVal.mValue.IsConst());

	if ((mBfIRBuilder->mIgnoreWrites) && (!wantConst))
	{
		if (toType == mContext->mBfObjectType)
			return BfTypedValue(mBfIRBuilder->GetFakeVal(), toType);
		if (toType->IsBoxed())
		{
			BfBoxedType* boxedType = (BfBoxedType*)toType;
			if (boxedType->mElementType->IsGenericParam())
			{
				if (typedVal.mType == boxedType->mElementType)
					return BfTypedValue(mBfIRBuilder->GetFakeVal(), toType);
				else
					return BfTypedValue();
			}
		}
	}

	BP_ZONE("BoxValue");

	BfTypeInstance* fromStructTypeInstance = typedVal.mType->ToTypeInstance();
	if (typedVal.mType->IsNullable())
	{
		typedVal = MakeAddressable(typedVal);

		auto innerType = typedVal.mType->GetUnderlyingType();
		if (!innerType->IsValueType())
		{
			if (!mIgnoreErrors)
				Fail("Only value types can be boxed", srcNode);
			return BfTypedValue();
		}

		auto boxedType = CreateBoxedType(innerType);
		auto resultType = toType;
		if (resultType == NULL)
			resultType = boxedType;

		if (mBfIRBuilder->mIgnoreWrites)
			return BfTypedValue(mBfIRBuilder->GetFakeVal(), resultType);

		auto prevBB = mBfIRBuilder->GetInsertBlock();
		auto boxBB = mBfIRBuilder->CreateBlock("boxedN.notNull");
		auto endBB = mBfIRBuilder->CreateBlock("boxedN.end");

		mBfIRBuilder->PopulateType(typedVal.mType);
		auto hasValueAddr = mBfIRBuilder->CreateInBoundsGEP(typedVal.mValue, 0, innerType->IsValuelessType() ? 1 : 2); // has_value
		auto hasValue = mBfIRBuilder->CreateLoad(hasValueAddr);

		mBfIRBuilder->CreateCondBr(hasValue, boxBB, endBB);

		AddDependency(boxedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);

		mBfIRBuilder->AddBlock(boxBB);
		mBfIRBuilder->SetInsertPoint(boxBB);
		BfScopeData newScope;
		newScope.mOuterIsConditional = true;
		mCurMethodState->AddScope(&newScope);
		NewScopeState();

		BfIRValue nullableValueAddr;
		BfIRValue loadedVal;
		if (innerType->IsValuelessType())
		{
			loadedVal = mBfIRBuilder->GetFakeVal();
		}
		else
		{
			nullableValueAddr = mBfIRBuilder->CreateInBoundsGEP(typedVal.mValue, 0, 1); // value
			loadedVal = mBfIRBuilder->CreateLoad(nullableValueAddr);
		}

		auto boxedVal = BoxValue(srcNode, BfTypedValue(loadedVal, fromStructTypeInstance->GetUnderlyingType()), resultType, allocTarget, callDtor ? BfCastFlags_None : BfCastFlags_NoBoxDtor);
		RestoreScopeState();
		if (!boxedVal)
			return BfTypedValue();
		mBfIRBuilder->CreateBr(endBB);
		auto boxBBEnd = mBfIRBuilder->GetInsertBlock();

		mBfIRBuilder->AddBlock(endBB);
		mBfIRBuilder->SetInsertPoint(endBB);
		auto phi = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(resultType), 2);
		mBfIRBuilder->AddPhiIncoming(phi, boxedVal.mValue, boxBBEnd);
		mBfIRBuilder->AddPhiIncoming(phi, GetDefaultValue(resultType), prevBB);

		return BfTypedValue(phi, resultType);
	}

	bool alreadyCheckedCast = false;

	BfTypeInstance* toTypeInstance = NULL;
	if (toType != NULL)
		toTypeInstance = toType->ToTypeInstance();

	bool isStructPtr = typedVal.mType->IsStructPtr();
	if (fromStructTypeInstance == NULL)
	{
		auto primType = (BfPrimitiveType*)typedVal.mType;

		if ((typedVal.mType->IsPointer()) && (toTypeInstance->IsInstanceOf(mCompiler->mIHashableTypeDef)))
		{
			// Can always do IHashable
			alreadyCheckedCast = true;
		}

		if ((!typedVal.mType->IsPointer()) || (toTypeInstance == mContext->mBfObjectType))
			fromStructTypeInstance = GetWrappedStructType(typedVal.mType);
		else
		{
			auto checkStructTypeInstance = GetWrappedStructType(typedVal.mType);
			if (checkStructTypeInstance == toType->GetUnderlyingType())
				fromStructTypeInstance = checkStructTypeInstance;
		}

		if (isStructPtr)
		{
			if ((toTypeInstance != NULL) && (fromStructTypeInstance != NULL) && (TypeIsSubTypeOf(fromStructTypeInstance, toTypeInstance)))
				alreadyCheckedCast = true;

			fromStructTypeInstance = typedVal.mType->GetUnderlyingType()->ToTypeInstance();
		}

		if ((fromStructTypeInstance == NULL) && (alreadyCheckedCast))
			fromStructTypeInstance = GetWrappedStructType(typedVal.mType);
	}
	if (fromStructTypeInstance == NULL)
		return BfTypedValue();

	// Need to box it
	bool isBoxedType = (fromStructTypeInstance != NULL) && (toType->IsBoxed());

	if ((toType == NULL) || (toType == mContext->mBfObjectType) || (isBoxedType) || (alreadyCheckedCast) ||  (TypeIsSubTypeOf(fromStructTypeInstance, toTypeInstance)))
	{
		if ((mBfIRBuilder->mIgnoreWrites) && (!wantConst))
			return BfTypedValue(mBfIRBuilder->GetFakeVal(), (toType != NULL) ? toType : CreateBoxedType(typedVal.mType));

		auto boxedType = CreateBoxedType(typedVal.mType);
		mBfIRBuilder->PopulateType(boxedType);

		if (wantConst)
			return BfTypedValue(mBfIRBuilder->CreateConstBox(typedVal.mValue, mBfIRBuilder->MapType(boxedType)), boxedType);

		AddDependency(boxedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
		auto allocaInst = AllocFromType(boxedType, allocTarget, BfIRValue(), BfIRValue(), 0, callDtor ? BfAllocFlags_None : BfAllocFlags_NoDtorCall);

		BfTypedValue boxedTypedValue(allocaInst, boxedType);
		mBfIRBuilder->SetName(allocaInst, "boxed." + fromStructTypeInstance->mTypeDef->mName->ToString());

		if (boxedType->IsUnspecializedType())
		{
			BF_ASSERT((srcNode == NULL) || (mCurMethodInstance->mIsUnspecialized));
		}
		else
		{
			PopulateType(fromStructTypeInstance);
			if (!fromStructTypeInstance->IsValuelessType())
			{
				typedVal = LoadValue(typedVal);
				auto valPtr = mBfIRBuilder->CreateInBoundsGEP(allocaInst, 0, 1);

				if ((typedVal.mType != fromStructTypeInstance) && (!isStructPtr))
				{
					auto ptrType = CreatePointerType(typedVal.mType);
					valPtr = mBfIRBuilder->CreateBitCast(valPtr, mBfIRBuilder->MapType(ptrType));
				}

				if (typedVal.IsSplat())
				{
					AggregateSplatIntoAddr(typedVal, valPtr);
				}
				else
					mBfIRBuilder->CreateStore(typedVal.mValue, valPtr, typedVal.mType->mAlign);
			}
		}

		if (toType == NULL)
		{
			return BfTypedValue(allocaInst, boxedType);
		}
		else
		{
			auto castedValue = mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapType(toType));
			return BfTypedValue(castedValue, toType);
		}
	}

	return BfTypedValue();
}

bool BfModule::GetBasePropertyDef(BfPropertyDef*& propDef, BfTypeInstance*& typeInst)
{
	BfTypeInstance* checkTypeInst = typeInst;
	while (checkTypeInst != NULL)
	{
		for (auto checkProp : checkTypeInst->mTypeDef->mProperties)
		{
			if (checkProp->mName == propDef->mName)
			{
				auto checkPropDeclaration = BfNodeDynCast<BfPropertyDeclaration>(checkProp->mFieldDeclaration);
				if ((checkPropDeclaration == NULL) || (checkPropDeclaration->mVirtualSpecifier == NULL) || (checkPropDeclaration->mVirtualSpecifier->GetToken() == BfToken_Virtual))
				{
					propDef = checkProp;
					typeInst = checkTypeInst;
					return true;
				}
			}
		}

		checkTypeInst = checkTypeInst->mBaseType;
	}
	return false;
}

BfMethodInstance* BfModule::GetRawMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName)
{
	if (!typeInstance->mResolvingVarField)
	{
		if (!typeInstance->DefineStateAllowsStaticMethods())
			PopulateType(typeInstance, BfPopulateType_AllowStaticMethods);
	}
	else
	{
		if (methodIdx >= (int)typeInstance->mMethodInstanceGroups.size())
		{
			AssertErrorState();
			return NULL;
		}
	}

	if (assertName != NULL)
	{
		BF_ASSERT(typeInstance->mTypeDef->mMethods[methodIdx]->mName == assertName);
	}

	if (methodIdx >= typeInstance->mMethodInstanceGroups.mSize)
	{
		if (mCompiler->EnsureCeUnpaused(typeInstance))
		{
			BF_FATAL("OOB in GetRawMethodInstanceAtIdx");
		}
		return NULL;
	}

	auto& methodGroup = typeInstance->mMethodInstanceGroups[methodIdx];
	if (methodGroup.mDefault == NULL)
	{
		if (!mCompiler->mIsResolveOnly)
		{
			// Get it from the owning module so we don't create a reference prematurely...
			auto declModule = typeInstance->mModule;

			if ((typeInstance->mGenericTypeInfo != NULL) && (typeInstance->mGenericTypeInfo->mFinishedGenericParams) && (methodGroup.mOnDemandKind == BfMethodOnDemandKind_NotSet))
			{
				methodGroup.mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
				if (!declModule->mIsScratchModule)
					declModule->mOnDemandMethodCount++;
			}

			BF_ASSERT((methodGroup.mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude) || (methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) || (methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl) ||
				(typeInstance->mTypeFailed) || (typeInstance->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted));
			if ((methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) || (methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl))
				methodGroup.mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;

			BfGetMethodInstanceFlags useFlags = (BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_MethodInstanceOnly | BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_Unreified);
			return declModule->GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector(), useFlags).mMethodInstance;
		}
		else
		{
			auto declModule = typeInstance->mModule;
			BfGetMethodInstanceFlags useFlags = (BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_MethodInstanceOnly | BfGetMethodInstanceFlag_UnspecializedPass);
			return declModule->GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector(), useFlags).mMethodInstance;
		}
	}
	auto methodInstance = typeInstance->mMethodInstanceGroups[methodIdx].mDefault;

	//TODO: Why did we have this adding methods to the work list?  This should only happen if we actually attempt to USE the method, which should
	//  be from a call to the NON-raw version
// 	if (!methodInstance->mMethodInstanceGroup->IsImplemented())
// 	{
// 		methodInstance->mIsReified = mIsReified;
// 		if (methodInstance->mMethodProcessRequest == NULL)
// 			typeInstance->mModule->AddMethodToWorkList(methodInstance);
// 	}
	return methodInstance;
}

BfMethodInstance* BfModule::GetRawMethodInstance(BfTypeInstance* typeInstance, BfMethodDef* methodDef)
{
	if (methodDef->mIsLocalMethod)
	{
		return GetMethodInstance(typeInstance, methodDef, BfTypeVector()).mMethodInstance;
	}

	return GetRawMethodInstanceAtIdx(typeInstance, methodDef->mIdx, NULL);
}

BfMethodInstance* BfModule::GetRawMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount, bool checkBase, bool allowMixin)
{
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	while (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(methodName, &entry))
			methodDef = (BfMethodDef*)entry->mMemberDef;

		while (methodDef != NULL)
		{
			if (((allowMixin) || (methodDef->mMethodType != BfMethodType_Mixin)) &&
				(methodDef->mGenericParams.size() == 0) &&
				((paramCount == -1) || (paramCount == (int)methodDef->mParams.size())))
				return GetRawMethodInstance(typeInstance, methodDef);
			methodDef = methodDef->mNextWithSameName;
		}

		if (!checkBase)
			break;
		typeInstance = typeInstance->mBaseType;
	}
	return NULL;
}

BfMethodInstance* BfModule::GetUnspecializedMethodInstance(BfMethodInstance* methodInstance, bool useUnspecializedType)
{
	if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mMethodGenericArguments.size() != 0))
		methodInstance = methodInstance->mMethodInstanceGroup->mDefault;

	auto owner = methodInstance->mMethodInstanceGroup->mOwner;

	if (!useUnspecializedType)
		return methodInstance;

	if (!owner->IsGenericTypeInstance())
		return methodInstance;

	if ((owner->IsDelegateFromTypeRef()) ||
		(owner->IsFunctionFromTypeRef()) ||
		(owner->IsTuple()))
	{
		return methodInstance;
	}

	auto genericType = (BfTypeInstance*)owner;
	if ((genericType->IsUnspecializedType()) && (!genericType->IsUnspecializedTypeVariation()))
		return methodInstance;

	if (methodInstance->mMethodDef->mIsLocalMethod)
		return methodInstance;
	if (methodInstance->mMethodDef->mDeclaringType->IsEmitted())
		return methodInstance;

	auto unspecializedType = ResolveTypeDef(genericType->mTypeDef->GetDefinition());
	if (unspecializedType == NULL)
	{
		AssertErrorState();
		return methodInstance;
	}
	if (unspecializedType == NULL)
		return methodInstance;
	auto unspecializedTypeInst = unspecializedType->ToTypeInstance();
	return GetRawMethodInstanceAtIdx(unspecializedTypeInst, methodInstance->mMethodDef->mIdx);
}

int BfModule::GetGenericParamAndReturnCount(BfMethodInstance* methodInstance)
{
	int genericCount = 0;
	auto unspecializedMethodInstance = GetUnspecializedMethodInstance(methodInstance);
	for (int paramIdx = 0; paramIdx < unspecializedMethodInstance->GetParamCount(); paramIdx++)
	{
		auto param = unspecializedMethodInstance->GetParamType(paramIdx);
		if (param->IsGenericParam())
			genericCount++;
	}
	if (unspecializedMethodInstance->mReturnType->IsGenericParam())
		genericCount++;
	return genericCount;
}

BfModule* BfModule::GetSpecializedMethodModule(const SizedArrayImpl<BfProject*>& projectList)
{
	BF_ASSERT(!mIsScratchModule);
	BF_ASSERT(mIsReified);

	BfModule* mainModule = this;
	if (mParentModule != NULL)
		mainModule = mParentModule;

	BfModule* specModule = NULL;
	BfModule** specModulePtr = NULL;
	if (mainModule->mSpecializedMethodModules.TryGetValueWith(projectList, &specModulePtr))
	{
		return *specModulePtr;
	}
	else
	{
		String specModuleName = mModuleName;
		for (auto bfProject : projectList)
		{
			specModuleName += StrFormat("@%s", bfProject->mSafeName.c_str());
		}
		specModule = new BfModule(mContext, specModuleName);
		specModule->mProject = mainModule->mProject;
		specModule->mParentModule = mainModule;
		specModule->mIsSpecializedMethodModuleRoot = true;
		specModule->Init();

		Array<BfProject*> projList;
		for (auto project : projectList)
			projList.Add(project);
		mainModule->mSpecializedMethodModules[projList] = specModule;
	}
	return specModule;
}

BfIRValue BfModule::CreateFunctionFrom(BfMethodInstance* methodInstance, bool tryExisting, bool isInlined)
{
	if (IsSkippingExtraResolveChecks())
		return BfIRValue();

	if (methodInstance->mMethodInstanceGroup->mOwner->IsInterface())
	{
		//BF_ASSERT(!methodInstance->mIRFunction);
		return BfIRValue();
	}

	auto methodDef = methodInstance->mMethodDef;
	StringT<4096> methodName;
	BfMangler::Mangle(methodName, mCompiler->GetMangleKind(), methodInstance);
	if (isInlined != methodInstance->mAlwaysInline)
	{
		if (isInlined)
			methodName += "__INLINE";
		else
			methodName += "__NOINLINE";
	}

	if (tryExisting)
	{
		auto func = mBfIRBuilder->GetFunction(methodName);
		if (func)
			return func;
	}

	auto intrinsic = GetIntrinsic(methodInstance);
	if (intrinsic)
		return intrinsic;

	if (methodInstance->GetImportCallKind() != BfImportCallKind_None)
	{
		return CreateDllImportGlobalVar(methodInstance, false);
	}

	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	methodInstance->GetIRFunctionInfo(this, returnType, paramTypes);
	auto funcType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes, methodInstance->IsVarArgs());

	auto func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, methodName);
	auto callingConv = GetIRCallingConvention(methodInstance);
	if (callingConv != BfIRCallingConv_CDecl)
		mBfIRBuilder->SetFuncCallingConv(func, callingConv);
	SetupIRMethod(methodInstance, func, isInlined);

// 	auto srcModule = methodInstance->GetOwner()->GetModule();
// 	if ((srcModule != NULL) && (srcModule->mProject != mProject))
// 	{
// 		if (srcModule->mProject->mTargetType == BfTargetType_BeefDynLib)
// 		{
// 			mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_DllImport);
// 		}
// 	}

	return func;
}

BfModuleMethodInstance BfModule::GetMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName, BfGetMethodInstanceFlags flags)
{
	if (assertName != NULL)
	{
		BF_ASSERT(typeInstance->mTypeDef->mMethods[methodIdx]->mName == assertName);
	}

	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	auto methodInstance = typeInstance->mMethodInstanceGroups[methodIdx].mDefault;

	BfMethodDef* methodDef = NULL;
	BfTypeInstance* foreignType = NULL;
	if (methodInstance != NULL)
	{
		methodDef = methodInstance->mMethodDef;
		if (methodInstance->mMethodInfoEx != NULL)
			foreignType = methodInstance->mMethodInfoEx->mForeignType;
	}

	if ((methodInstance != NULL) && (mIsReified) && (!methodInstance->mIsReified))
	{
		// Can't use it, not reified
		methodInstance = NULL;
	}

	if ((methodInstance != NULL) && (mBfIRBuilder != NULL) && (mBfIRBuilder->mIgnoreWrites) &&
		(methodInstance->mMethodInstanceGroup->IsImplemented()))
	{
		BfIRFunction func(mBfIRBuilder->GetFakeVal());
		return BfModuleMethodInstance(methodInstance, func);
	}

	if (foreignType != NULL)
		return GetMethodInstance(typeInstance, methodDef, BfTypeVector(), (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForeignMethodDef), foreignType);
	return GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector(), flags);
}

BfModuleMethodInstance BfModule::GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount, bool checkBase)
{
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	while (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(methodName, &entry))
			methodDef = (BfMethodDef*)entry->mMemberDef;

		while (methodDef != NULL)
		{
			if ((methodDef->mMethodType != BfMethodType_Mixin) &&
				(methodDef->mGenericParams.size() == 0) &&
				((paramCount == -1) || (paramCount == (int)methodDef->mParams.size())))
			{
				auto moduleMethodInstance = GetMethodInstanceAtIdx(typeInstance, methodDef->mIdx);
				if (moduleMethodInstance)
					return moduleMethodInstance;
			}
			methodDef = methodDef->mNextWithSameName;
 		}

		if (!checkBase)
			break;
		typeInstance = typeInstance->mBaseType;
	}
	return BfModuleMethodInstance();
}

BfModuleMethodInstance BfModule::GetMethodByName(BfTypeInstance* typeInstance, const StringImpl& methodName, const Array<BfType*>& paramTypes, bool checkBase)
{
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	while (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(methodName, &entry))
			methodDef = (BfMethodDef*)entry->mMemberDef;

		while (methodDef != NULL)
		{
			if ((methodDef->mMethodType != BfMethodType_Mixin) &&
				(methodDef->mGenericParams.size() == 0) &&
				(paramTypes.size() == methodDef->mParams.size()))
			{
				auto moduleMethodInstance = GetMethodInstanceAtIdx(typeInstance, methodDef->mIdx);
				if (moduleMethodInstance.mMethodInstance != NULL)
				{
					bool matches = true;
					for (int paramIdx = 0; paramIdx < (int)paramTypes.size(); paramIdx++)
					{
						if (moduleMethodInstance.mMethodInstance->GetParamType(paramIdx) != paramTypes[paramIdx])
							matches = false;
					}
					if (matches)
						return moduleMethodInstance;
				}
			}

			methodDef = methodDef->mNextWithSameName;
		}

		if (!checkBase)
			break;
		typeInstance = typeInstance->mBaseType;
	}
	return BfModuleMethodInstance();
}

BfModuleMethodInstance BfModule::GetInternalMethod(const StringImpl& methodName, int paramCount)
{
	auto internalType = ResolveTypeDef(mCompiler->mInternalTypeDef);
	PopulateType(internalType);
	auto moduleMethodInstance = GetMethodByName(internalType->ToTypeInstance(), methodName, paramCount);
	if (!moduleMethodInstance)
	{
		Fail(StrFormat("Failed to find System.Internal method '%s'", methodName.c_str()));
	}
	return moduleMethodInstance;
}

BfOperatorInfo* BfModule::GetOperatorInfo(BfTypeInstance* typeInstance, BfOperatorDef* operatorDef)
{
	while (operatorDef->mIdx >= typeInstance->mOperatorInfo.size())
		typeInstance->mOperatorInfo.Add(NULL);

	if (typeInstance->mOperatorInfo[operatorDef->mIdx] == NULL)
	{
		SetAndRestoreValue<bool> ignoreErrors(mIgnoreErrors, true);
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);

		BfTypeState typeState;
		typeState.mType = typeInstance;
		typeState.mCurTypeDef = operatorDef->mDeclaringType;
		SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

		BfOperatorInfo* operatorInfo = new BfOperatorInfo();
		if (operatorDef->mReturnTypeRef != NULL)
			operatorInfo->mReturnType = ResolveTypeRef(operatorDef->mReturnTypeRef, BfPopulateType_Identity);
		if (operatorDef->mParams.size() >= 1)
			operatorInfo->mLHSType = ResolveTypeRef(operatorDef->mParams[0]->mTypeRef, BfPopulateType_Identity);
		if (operatorDef->mParams.size() >= 2)
			operatorInfo->mRHSType = ResolveTypeRef(operatorDef->mParams[1]->mTypeRef, BfPopulateType_Identity);
		typeInstance->mOperatorInfo[operatorDef->mIdx] = operatorInfo;
	}

	return typeInstance->mOperatorInfo[operatorDef->mIdx];
}

BfType* BfModule::CheckOperator(BfTypeInstance* typeInstance, BfOperatorDef* operatorDef, const BfTypedValue& lhs, const BfTypedValue& rhs)
{
	auto operatorInfo = GetOperatorInfo(typeInstance, operatorDef);
	if (operatorInfo == NULL)
		return NULL;
	if (operatorInfo->mReturnType == NULL)
		return NULL;

	auto castFlags = BfCastFlags_IsConstraintCheck;
	if (operatorDef->mOperatorDeclaration->mIsConvOperator)
		castFlags = (BfCastFlags)(castFlags | BfCastFlags_NoConversionOperator);

	if (lhs)
	{
		if (operatorInfo->mLHSType == NULL)
			return NULL;
		if (!CanCast(lhs, operatorInfo->mLHSType, castFlags))
			return NULL;
	}
	if (rhs)
	{
		if (operatorInfo->mRHSType == NULL)
			return NULL;
		if (!CanCast(rhs, operatorInfo->mRHSType, castFlags))
			return NULL;
	}
	return operatorInfo->mReturnType;
}

bool BfModule::IsMethodImplementedAndReified(BfTypeInstance*  typeInstance, const StringImpl& methodName, int paramCount, bool checkBase)
{
	while (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(methodName, &entry))
			methodDef = (BfMethodDef*)entry->mMemberDef;

		while (methodDef != NULL)
		{
			if ((methodDef->mMethodType != BfMethodType_Mixin) &&
				(methodDef->mGenericParams.size() == 0) &&
				((paramCount == -1) || (paramCount == (int)methodDef->mParams.size())))
			{
				auto& methodInstanceGroup = typeInstance->mMethodInstanceGroups[methodDef->mIdx];
				if (!methodInstanceGroup.IsImplemented())
					return false;
				if (methodInstanceGroup.mDefault == NULL)
					return false;
				return methodInstanceGroup.mDefault->mIsReified;
			}

			methodDef = methodDef->mNextWithSameName;
		}

		if (!checkBase)
			break;
		typeInstance = typeInstance->mBaseType;
	}
	return false;
}

bool BfModule::HasMixin(BfTypeInstance* typeInstance, const StringImpl& methodName, int paramCount, bool checkBase)
{
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	while (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(methodName, &entry))
			methodDef = (BfMethodDef*)entry->mMemberDef;

		while (methodDef != NULL)
		{
			if ((methodDef->mMethodType == BfMethodType_Mixin) &&
				((paramCount == -1) || (paramCount == (int)methodDef->mParams.size())))
				return true;

			methodDef = methodDef->mNextWithSameName;
		}

		if (!checkBase)
			break;
		typeInstance = typeInstance->mBaseType;
	}
	return BfModuleMethodInstance();
}

String BfModule::FieldToString(BfFieldInstance* fieldInstance)
{
	auto result = TypeToString(fieldInstance->mOwner);
	result += ".";
	auto fieldDef = fieldInstance->GetFieldDef();
	if (fieldDef != NULL)
		result += fieldDef->mName;
	else
		result += "???";
	return result;
}

StringT<128> BfModule::MethodToString(BfMethodInstance* methodInst, BfMethodNameFlags methodNameFlags, BfTypeVector* typeGenericArgs, BfTypeVector* methodGenericArgs)
{
	auto methodDef = methodInst->mMethodDef;
	bool allowResolveGenericParamNames = ((methodNameFlags & BfMethodNameFlag_ResolveGenericParamNames) != 0);

	BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None;

	bool hasGenericArgs = (typeGenericArgs != NULL) || (methodGenericArgs != NULL);

	BfType* type = methodInst->mMethodInstanceGroup->mOwner;
	if ((hasGenericArgs) && (type->IsUnspecializedType()))
		type = ResolveGenericType(type, typeGenericArgs, methodGenericArgs, mCurTypeInstance);
	if ((type == NULL) || (!type->IsUnspecializedTypeVariation()))
		typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
	if (allowResolveGenericParamNames)
		typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;

	StringT<128> methodName;

	auto _AddTypeName = [&](BfType* type)
	{
		auto typeNameFlags = BfTypeNameFlags_None;
		if (allowResolveGenericParamNames)
			typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
		if ((hasGenericArgs) && (type->IsUnspecializedType()))
			type = ResolveGenericType(type, typeGenericArgs, methodGenericArgs, mCurTypeInstance);
		methodName += TypeToString(type, typeNameFlags);
	};

	if ((methodNameFlags & BfMethodNameFlag_IncludeReturnType) != 0)
	{
		_AddTypeName(methodInst->mReturnType);
		methodName += " ";
	}

	if ((methodNameFlags & BfMethodNameFlag_OmitTypeName) == 0)
	{
		methodName += TypeToString(type, typeNameFlags);
		if (methodName == "$")
			methodName = "";
		else if (!methodName.IsEmpty())
			methodName += ".";
	}
	String accessorString;
	StringT<64> methodDefName = methodInst->mMethodDef->mName;

	if (methodInst->mMethodDef->mIsLocalMethod)
	{
		int atPos = (int)methodDefName.IndexOf('$');
		methodDefName.RemoveToEnd(atPos);
		methodDefName.Replace("@", ".");
	}
	else
	{
		int atPos = (int)methodDefName.IndexOf('$');
		if (atPos != -1)
		{
			accessorString = methodDefName.Substring(0, atPos);
			if ((accessorString == "get") || (accessorString == "set"))
			{
				methodDefName = methodDefName.Substring(atPos + 1);
			}
			else
				accessorString = "";
		}
	}

	if ((methodInst->mMethodInfoEx != NULL) && (methodInst->mMethodInfoEx->mForeignType != NULL))
	{
		BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None;
		if (!methodInst->mIsUnspecializedVariation && allowResolveGenericParamNames)
			typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
		methodName += TypeToString(methodInst->mMethodInfoEx->mForeignType, typeNameFlags);
		methodName += ".";
	}

	if ((methodInst->mMethodInfoEx != NULL) && (methodInst->mMethodInfoEx->mExplicitInterface != NULL))
	{
		BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None;
		if (!methodInst->mIsUnspecializedVariation && allowResolveGenericParamNames)
			typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
		methodName += "[";
		methodName += TypeToString(methodInst->mMethodInfoEx->mExplicitInterface, typeNameFlags);
		methodName += "].";
	}

	if (methodDef->mMethodType == BfMethodType_Operator)
	{
		BfOperatorDef* operatorDef = (BfOperatorDef*)methodDef;
		if (operatorDef->mOperatorDeclaration->mIsConvOperator)
		{
			// Don't add explicit/implicit part, since that's not available in GCC mangling
			/*if (operatorDef->mOperatorDeclaration->mExplicitToken != NULL)
			{
				if (operatorDef->mOperatorDeclaration->mExplicitToken->GetToken() == BfToken_Explicit)
					methodName += "explicit ";
				else if (operatorDef->mOperatorDeclaration->mExplicitToken->GetToken() == BfToken_Explicit)
					methodName += "explicit ";
			}*/
			methodName += "operator ";
			if (methodInst->mReturnType != NULL)
				methodName += TypeToString(methodInst->mReturnType);
		}
		else
		{
			methodName += "operator";
			if (operatorDef->mOperatorDeclaration->mOpTypeToken != NULL)
				methodName += BfTokenToString(operatorDef->mOperatorDeclaration->mOpTypeToken->GetToken());
		}
	}
	else if (methodDef->mMethodType == BfMethodType_Ctor)
	{
		if (methodDef->mIsStatic)
			methodName += "__BfStaticCtor";
		else
			methodName += "this";
		accessorString = "";
	}
	else if (methodDef->mMethodType == BfMethodType_PropertyGetter)
	{
		auto propertyDecl = methodDef->GetPropertyDeclaration();
		if (auto indexerProperty = BfNodeDynCast<BfIndexerDeclaration>(propertyDecl))
		{
			methodName += "get indexer";
		}
		else
		{
			if ((propertyDecl != NULL) && (propertyDecl->mNameNode != NULL))
				propertyDecl->mNameNode->ToString(methodName);
			methodName += " get accessor";
			return methodName;
		}
	}
	else if (methodDef->mMethodType == BfMethodType_PropertySetter)
	{
		auto propertyDecl = methodDef->GetPropertyDeclaration();
		if (auto indexerProperty = BfNodeDynCast<BfIndexerDeclaration>(propertyDecl))
		{
			methodName += "set indexer";
		}
		else
		{
			if ((propertyDecl != NULL) && (propertyDecl->mNameNode != NULL))
				propertyDecl->mNameNode->ToString(methodName);
			methodName += " set accessor";
			return methodName;
		}
	}
	else
		methodName += methodDefName;

	if (methodDef->mMethodType == BfMethodType_Mixin)
		methodName += "!";

	BfTypeVector newMethodGenericArgs;
	if ((methodInst->mMethodInfoEx != NULL) && (methodInst->mMethodInfoEx->mMethodGenericArguments.size() != 0))
	{
		methodName += "<";
		for (int i = 0; i < (int)methodInst->mMethodInfoEx->mMethodGenericArguments.size(); i++)
		{
			if (i > 0)
				methodName += ", ";
			BfTypeNameFlags typeNameFlags = BfTypeNameFlag_ShortConst;
			//Why did we have this methodInst->mIsUnspecializedVariation check?  Sometimes we do need to show errors calling methods that refer back to our types
			//if (!methodInst->mIsUnspecializedVariation && allowResolveGenericParamNames)
			if (allowResolveGenericParamNames)
				typeNameFlags = (BfTypeNameFlags)(typeNameFlags | BfTypeNameFlag_ResolveGenericParamNames);
			BfType* type = methodInst->mMethodInfoEx->mMethodGenericArguments[i];
			if ((methodGenericArgs != NULL) && (type->IsUnspecializedType()))
			{
				bool hasEmpty = false;
				for (auto arg : *methodGenericArgs)
				{
					if (arg == NULL)
						hasEmpty = true;
				}
				if (hasEmpty)
				{
					for (int genericIdx = 0; genericIdx < (int)methodGenericArgs->size(); genericIdx++)
					{
						auto arg = (*methodGenericArgs)[genericIdx];
						if (arg != NULL)
							newMethodGenericArgs.push_back(arg);
						else
						{
							auto genericParamInst = methodInst->mMethodInfoEx->mGenericParams[genericIdx];
							if (genericParamInst->mTypeConstraint != NULL)
								newMethodGenericArgs.push_back(genericParamInst->mTypeConstraint);
							else
								newMethodGenericArgs.push_back(mContext->mBfObjectType); // Default
						}
					}
					methodGenericArgs = &newMethodGenericArgs;
				}

				if (type->IsUnspecializedType())
					type = ResolveGenericType(type, NULL, methodGenericArgs, mCurTypeInstance);
			}

			if ((methodGenericArgs == NULL) && (mCurMethodInstance == NULL) && (mCurTypeInstance == NULL))
			{
				SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, methodInst->GetOwner());
				SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInst);
				methodName += TypeToString(type, typeNameFlags);
			}
			else
				methodName += TypeToString(type, typeNameFlags);
		}
		methodName += ">";
	}
	if (accessorString.length() == 0)
	{
		int dispParamIdx = 0;
		methodName += "(";
		for (int paramIdx = 0; paramIdx < (int)methodInst->GetParamCount(); paramIdx++)
		{
			int paramKind = methodInst->GetParamKind(paramIdx);
			if ((paramKind == BfParamKind_ImplicitCapture) || (paramKind == BfParamKind_AppendIdx))
				continue;

			if (dispParamIdx > 0)
				methodName += ", ";

			if (paramKind == BfParamKind_Params)
				methodName += "params ";

			BfType* type = methodInst->GetParamType(paramIdx);
			_AddTypeName(type);

			methodName += " ";
			methodName += methodInst->GetParamName(paramIdx);

			auto paramInitializer = methodInst->GetParamInitializer(paramIdx);
			if ((paramInitializer != NULL) && ((methodNameFlags & BfMethodNameFlag_NoAst) == 0))
			{
				methodName += " = ";
				methodName += paramInitializer->ToString();
			}

			dispParamIdx++;
		}
		methodName += ")";
	}

	if (accessorString.length() != 0)
	{
		methodName += " ";
		methodName += accessorString;
	}

	if ((methodNameFlags & BfMethodNameFlag_IncludeMut) != 0)
	{
		if ((methodDef->mIsMutating) && (methodInst->GetOwner()->IsValueType()))
			methodName += " mut";
	}

	return methodName;
}

void BfModule::pt(BfType* type)
{
	OutputDebugStrF("%s\n", TypeToString(type).c_str());
}

void BfModule::pm(BfMethodInstance* type)
{
	OutputDebugStrF("%s\n", MethodToString(type).c_str());
}

static void AddAttributeTargetName(BfAttributeTargets& flagsLeft, BfAttributeTargets checkFlag, String& str, String addString)
{
	if ((flagsLeft & checkFlag) == 0)
		return;

	if (!str.empty())
	{
		if (flagsLeft == checkFlag)
		{
			if ((int)str.IndexOf(',') != -1)
				str += ", and ";
			else
				str += " and ";
		}
		else
			str += ", ";
	}
	str += addString;
	flagsLeft = (BfAttributeTargets)(flagsLeft & ~checkFlag);
}

static String GetAttributesTargetListString(BfAttributeTargets attrTarget)
{
	String resultStr;
	auto flagsLeft = attrTarget;
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Assembly, resultStr, "assembly declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Module, resultStr, "module declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Class, resultStr, "class declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Struct, resultStr, "struct declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Enum, resultStr, "enum declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Constructor, resultStr, "constructor declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Method, resultStr, "method declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Property, resultStr, "property declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Field, resultStr, "field declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_StaticField, resultStr, "static field declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Interface, resultStr, "interface declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Parameter, resultStr, "parameter declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Delegate, resultStr, "delegate declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Function, resultStr, "function declarations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_ReturnValue, resultStr, "return value");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_GenericParameter, resultStr, "generic parameters");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Invocation, resultStr, "invocations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_MemberAccess, resultStr, "member access");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Alloc, resultStr, "allocations");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Alias, resultStr, "aliases");
	AddAttributeTargetName(flagsLeft, BfAttributeTargets_Block, resultStr, "blocks");
	if (resultStr.IsEmpty())
		return "<nothing>";
	return resultStr;
}

BfIRType BfModule::CurrentAddToConstHolder(BfIRType irType)
{
	if (irType.mKind == BfIRTypeData::TypeKind_SizedArray)
	{
		auto sizedArrayType = (BfConstantSizedArrayType*)mBfIRBuilder->GetConstantById(irType.mId);
		return mCurTypeInstance->GetOrCreateConstHolder()->GetSizedArrayType(CurrentAddToConstHolder(sizedArrayType->mType), (int)sizedArrayType->mLength);
	}

	return irType;
}

void BfModule::CurrentAddToConstHolder(BfIRValue& irVal)
{
	auto constant = mBfIRBuilder->GetConstant(irVal);

	int stringPoolIdx = GetStringPoolIdx(irVal, mBfIRBuilder);
	if (stringPoolIdx != -1)
	{
		irVal = mCurTypeInstance->GetOrCreateConstHolder()->CreateConst(BfTypeCode_StringId, stringPoolIdx);
		return;
	}

	if (constant->mConstType == BfConstType_Agg)
	{
		auto constArray = (BfConstantAgg*)constant;

		SizedArray<BfIRValue, 8> newVals;
		for (auto val : constArray->mValues)
		{
			auto newVal = val;
			CurrentAddToConstHolder(newVal);
			newVals.push_back(newVal);
		}

		irVal = mCurTypeInstance->GetOrCreateConstHolder()->CreateConstAgg(CurrentAddToConstHolder(constArray->mType), newVals);
		return;
	}

	auto origConst = irVal;
	if ((constant->mConstType == BfConstType_BitCast) || (constant->mConstType == BfConstType_BitCastNull))
	{
		auto bitcast = (BfConstantBitCast*)constant;
		BfIRValue newVal;
		if (bitcast->mTarget)
		{
			newVal = BfIRValue(BfIRValueFlags_Const, bitcast->mTarget);
			CurrentAddToConstHolder(newVal);
		}
		else
			newVal = mCurTypeInstance->GetOrCreateConstHolder()->CreateConstNull();
		irVal = mCurTypeInstance->GetOrCreateConstHolder()->CreateConstBitCast(newVal, CurrentAddToConstHolder(bitcast->mToType));
		return;
	}

	irVal = mCurTypeInstance->CreateConst(constant, mBfIRBuilder);
}

void BfModule::ClearConstData()
{
	mBfIRBuilder->ClearConstData();
	mStringObjectPool.Clear();
	mStringCharPtrPool.Clear();
	mStringPoolRefs.Clear();
	mUnreifiedStringPoolRefs.Clear();
	mStaticFieldRefs.Clear();
}

BfTypedValue BfModule::GetTypedValueFromConstant(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType)
{
	switch (constant->mTypeCode)
	{
	case BfTypeCode_StringId:
	case BfTypeCode_Boolean:
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
	case BfTypeCode_Char8:
	case BfTypeCode_Char16:
	case BfTypeCode_Char32:
	case BfTypeCode_Float:
	case BfTypeCode_Double:
		{
			auto constVal = mBfIRBuilder->CreateConst(constant, constHolder);
			BfTypedValue typedValue;

			bool allowUnactualized = mBfIRBuilder->mIgnoreWrites;
			if (constant->mTypeCode == BfTypeCode_StringId)
			{
				if ((wantType->IsInstanceOf(mCompiler->mStringTypeDef)) ||
					((wantType->IsPointer()) && (wantType->GetUnderlyingType() == GetPrimitiveType(BfTypeCode_Char8))))
				{
					typedValue = BfTypedValue(ConstantToCurrent(constant, constHolder, wantType, allowUnactualized), wantType);
					return typedValue;
				}

				auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef);
				typedValue = BfTypedValue(ConstantToCurrent(constant, constHolder, stringType, allowUnactualized), stringType);
			}

			if (!typedValue)
			{
				auto constVal = mBfIRBuilder->CreateConst(constant, constHolder);
				typedValue = BfTypedValue(constVal, GetPrimitiveType(constant->mTypeCode));
			}

			if (typedValue.mType == wantType)
				return typedValue;
			if (wantType->IsTypedPrimitive())
			{
				if (typedValue.mType == wantType->GetUnderlyingType())
				{
					typedValue.mType = wantType;
					return typedValue;
				}
			}
			auto castedTypedValue = Cast(NULL, typedValue, wantType, (BfCastFlags)(BfCastFlags_SilentFail | BfCastFlags_Explicit));
			if (!castedTypedValue)
				return BfTypedValue();
			return castedTypedValue;
		}
		break;
	default: break;
	}
	BfIRValue irValue = ConstantToCurrent(constant, constHolder, wantType);
	BF_ASSERT(irValue);
	if (!irValue)
		return BfTypedValue();

	if (constant->mConstType == BfConstType_GlobalVar)
	{
		mBfIRBuilder->PopulateType(wantType);
		auto result = BfTypedValue(irValue, wantType, true);
		if (!wantType->IsComposite())
			result = LoadValue(result);
		return result;
	}

	return BfTypedValue(irValue, wantType, false);
}

bool BfModule::HasUnactializedConstant(BfConstant* constant, BfIRConstHolder* constHolder)
{
	if ((constant->mConstType == BfConstType_TypeOf) || (constant->mConstType == BfConstType_TypeOf_WithData))
		return true;
	if (constant->mTypeCode == BfTypeCode_StringId)
		return true;

	if (constant->mConstType == BfConstType_Agg)
	{
		auto constArray = (BfConstantAgg*)constant;
		for (auto val : constArray->mValues)
		{
			if (HasUnactializedConstant(constHolder->GetConstant(val), constHolder))
				return true;
		}
	}

	return false;
}

BfIRValue BfModule::ConstantToCurrent(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType, bool allowUnactualized)
{
	if (constant->mTypeCode == BfTypeCode_NullPtr)
	{
		if ((wantType == NULL) && (constant->mIRType.mKind == BfIRTypeData::TypeKind_TypeId))
			wantType = mContext->mTypes[constant->mIRType.mId];

		if (wantType == NULL)
			return mBfIRBuilder->CreateConstNull();

		return GetDefaultValue(wantType);
	}

	if (constant->mTypeCode == BfTypeCode_StringId)
	{
		if (!allowUnactualized)
		{
			if ((wantType->IsInstanceOf(mCompiler->mStringTypeDef)) ||
				((wantType->IsPointer()) && (wantType->GetUnderlyingType() == GetPrimitiveType(BfTypeCode_Char8))))
			{
				const StringImpl& str = mContext->mStringObjectIdMap[constant->mInt32].mString;
				BfIRValue stringObjConst = GetStringObjectValue(str, false, true);
				if (wantType->IsPointer())
					return GetStringCharPtr(stringObjConst, true);
				return stringObjConst;
			}
		}
	}

	if (constant->mConstType == Beefy::BfConstType_TypeOf)
	{
		auto constTypeOf = (BfTypeOf_Const*)constant;
		if (mCurTypeInstance != NULL)
			AddDependency(constTypeOf->mType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
		return CreateTypeDataRef(constTypeOf->mType);
	}

	if (constant->mConstType == BfConstType_PtrToInt)
	{
		auto fromPtrToInt = (BfConstantPtrToInt*)constant;
		auto fromTarget = constHolder->GetConstantById(fromPtrToInt->mTarget);
		return mBfIRBuilder->CreatePtrToInt(ConstantToCurrent(fromTarget, constHolder, NULL), fromPtrToInt->mToTypeCode);
	}

 	if (constant->mConstType == BfConstType_IntToPtr)
	{
		auto fromPtrToInt = (BfConstantIntToPtr*)constant;
		auto fromTarget = constHolder->GetConstantById(fromPtrToInt->mTarget);
		BfIRType toIRType = fromPtrToInt->mToType;
		if (toIRType.mKind == BfIRTypeData::TypeKind_TypeId)
		{
			auto toType = mContext->mTypes[toIRType.mId];
			toIRType = mBfIRBuilder->MapType(toType);
		}
		return mBfIRBuilder->CreateIntToPtr(ConstantToCurrent(fromTarget, constHolder, NULL), toIRType);
	}

	if ((constant->mConstType == BfConstType_BitCast) || (constant->mConstType == BfConstType_BitCastNull))
	{
		auto bitcast = (BfConstantBitCast*)constant;
		auto fromTarget = constHolder->GetConstantById(bitcast->mTarget);
		BfIRType toIRType = bitcast->mToType;
		if (toIRType.mKind == BfIRTypeData::TypeKind_TypeId)
		{
			auto toType = mContext->mTypes[toIRType.mId];
			toIRType = mBfIRBuilder->MapType(toType);
		}
		return mBfIRBuilder->CreateBitCast(ConstantToCurrent(fromTarget, constHolder, NULL), toIRType);
	}

	if (constant->mConstType == BfConstType_Agg)
	{
		auto constArray = (BfConstantAgg*)constant;

		if ((wantType == NULL) && (constArray->mType.mKind == BfIRTypeData::TypeKind_TypeId))
			wantType = mContext->mTypes[constArray->mType.mId];

		if (wantType->IsArray())
			wantType = CreateSizedArrayType(wantType->GetUnderlyingType(), (int)constArray->mValues.mSize);

		bool handleAsArray = false;

		SizedArray<BfIRValue, 8> newVals;
		if (wantType->IsSizedArray())
		{
			handleAsArray = true;
		}
		else if (wantType->IsInstanceOf(mCompiler->mSpanTypeDef))
		{
			auto valueType = ResolveTypeDef(mCompiler->mValueTypeTypeDef);
			handleAsArray = true;
			if (!constArray->mValues.IsEmpty())
			{
				auto firstConstant = constHolder->GetConstant(constArray->mValues[0]);
				if ((firstConstant->mConstType == BfConstType_AggZero) && (firstConstant->mIRType.mKind == BfIRType::TypeKind_TypeId) &&
					(firstConstant->mIRType.mId == valueType->mTypeId))
					handleAsArray = false;

				if (firstConstant->mConstType == BfConstType_Agg)
				{
					auto firstConstArray = (BfConstantAgg*)firstConstant;
					if ((firstConstArray->mType.mKind == BfIRType::TypeKind_TypeId) &&
						(firstConstArray->mType.mId == valueType->mTypeId))
						handleAsArray = false;
				}
			}
		}

		if (handleAsArray)
		{
			auto elementType = wantType->GetUnderlyingType();
			for (auto val : constArray->mValues)
			{
				newVals.Add(ConstantToCurrent(constHolder->GetConstant(val), constHolder, elementType));
			}
		}
		else
		{
			auto wantTypeInst = wantType->ToTypeInstance();
			if (wantTypeInst->mBaseType != NULL)
			{
				auto baseVal = ConstantToCurrent(constHolder->GetConstant(constArray->mValues[0]), constHolder, wantTypeInst->mBaseType);
				newVals.Add(baseVal);
			}

			if (wantType->IsUnion())
			{
				auto innerType = wantType->ToTypeInstance()->GetUnionInnerType();
				if (!innerType->IsValuelessType())
				{
					auto val = ConstantToCurrent(constHolder->GetConstant(constArray->mValues[1]), constHolder, innerType);
					newVals.Add(val);
				}
			}

			if ((!wantType->IsUnion()) || (wantType->IsPayloadEnum()))
			{
				for (auto& fieldInstance : wantTypeInst->mFieldInstances)
				{
					if (fieldInstance.mDataIdx < 0)
						continue;
					auto val = constArray->mValues[fieldInstance.mDataIdx];
					BfIRValue memberVal = ConstantToCurrent(constHolder->GetConstant(val), constHolder, fieldInstance.mResolvedType);
					if (fieldInstance.mDataIdx == newVals.mSize)
						newVals.Add(memberVal);
					else
					{
						while (fieldInstance.mDataIdx >= newVals.mSize)
							newVals.Add(BfIRValue());
						newVals[fieldInstance.mDataIdx] = memberVal;
					}
				}
			}

			for (auto& val : newVals)
			{
				if (!val)
					val = mBfIRBuilder->CreateConstArrayZero(0);
			}
		}

		return mBfIRBuilder->CreateConstAgg(mBfIRBuilder->MapType(wantType, BfIRPopulateType_Identity), newVals);
	}

	return mBfIRBuilder->CreateConst(constant, constHolder);
}

void BfModule::ValidateCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeTargets attrTarget)
{
	if (attrTarget == BfAttributeTargets_SkipValidate)
		return;

	for (auto& customAttribute : customAttributes->mAttributes)
	{
		if (!customAttribute.mAwaitingValidation)
			continue;

		if ((customAttribute.mType->mAttributeData->mAttributeTargets & attrTarget) == 0)
		{
			Fail(StrFormat("Attribute '%s' is not valid on this declaration type. It is only valid on %s.",
				customAttribute.GetRefNode()->ToString().c_str(), GetAttributesTargetListString(customAttribute.mType->mAttributeData->mAttributeTargets).c_str()), customAttribute.mRef->mAttributeTypeRef);	// CS0592
		}

		customAttribute.mAwaitingValidation = false;
	}
}

void BfModule::GetCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeDirective* attributesDirective, BfAttributeTargets attrTarget, BfGetCustomAttributesFlags flags, BfCaptureInfo* captureInfo)
{
	bool allowNonConstArgs = (flags & BfGetCustomAttributesFlags_AllowNonConstArgs) != 0;
	bool keepConstsInModule = (flags & BfGetCustomAttributesFlags_KeepConstsInModule) != 0;

	if (!mCompiler->mHasRequiredTypes)
		return;

	if ((attributesDirective != NULL) && (mCompiler->mResolvePassData != NULL))
	{
		if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(attributesDirective))
			sourceClassifier->VisitChild(attributesDirective);
	}

	SetAndRestoreValue<bool> prevIsCapturingMethodMatchInfo;
	if (mCompiler->IsAutocomplete())
		prevIsCapturingMethodMatchInfo.Init(mCompiler->mResolvePassData->mAutoComplete->mIsCapturingMethodMatchInfo, false);

	BfTypeInstance* baseAttrTypeInst = mContext->mUnreifiedModule->ResolveTypeDef(mCompiler->mAttributeTypeDef)->ToTypeInstance();
	BfAttributeTargets targetOverride = (BfAttributeTargets)0;

	BfTypeDef* activeTypeDef = GetActiveTypeDef();
	BfAutoComplete* autoComplete = NULL;
	if (mCompiler->mResolvePassData != NULL)
		autoComplete = mCompiler->mResolvePassData->mAutoComplete;

	for (; attributesDirective != NULL; attributesDirective = attributesDirective->mNextAttribute)
	{
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(attributesDirective->mAttributeTargetSpecifier))
		{
			if (captureInfo == NULL)
			{
				Fail("Capture specifiers can only be used in lambda allocations", tokenNode);
				continue;
			}

			BfCaptureInfo::Entry captureEntry;
			captureEntry.mCaptureType = (tokenNode->mToken == BfToken_Ampersand) ? BfCaptureType_Reference : BfCaptureType_Copy;
			if (!attributesDirective->mArguments.IsEmpty())
			{
				captureEntry.mNameNode = BfNodeDynCast<BfIdentifierNode>(attributesDirective->mArguments[0]);
				if ((captureEntry.mNameNode != NULL) && (autoComplete != NULL))
					autoComplete->CheckIdentifier(captureEntry.mNameNode);
			}
			captureInfo->mCaptures.Add(captureEntry);
			continue;
		}

		BfAutoParentNodeEntry autoParentNodeEntry(this, attributesDirective);

		BfCustomAttribute customAttribute;
		customAttribute.mAwaitingValidation = true;
		customAttribute.mDeclaringType = activeTypeDef;
		customAttribute.mRef = attributesDirective;

		if (attributesDirective->mAttrOpenToken != NULL)
			targetOverride = (BfAttributeTargets)0;

		if (attributesDirective->mAttributeTypeRef == NULL)
		{
			AssertErrorState();
			continue;
		}

		BfType* attrType;
		if (mContext->mCurTypeState != NULL)
		{
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurAttributeTypeRef, attributesDirective->mAttributeTypeRef);
			attrType = ResolveTypeRef(attributesDirective->mAttributeTypeRef, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_Attribute | BfResolveTypeRefFlag_NoReify));
		}
		else
		{
			attrType = ResolveTypeRef(attributesDirective->mAttributeTypeRef, BfPopulateType_Identity, (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_Attribute | BfResolveTypeRefFlag_NoReify));
		}

		BfTypeDef* attrTypeDef = NULL;
		if ((attrType != NULL) && (attrType->IsTypeInstance()))
			attrTypeDef = attrType->ToTypeInstance()->mTypeDef;

		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		{
			mCompiler->mResolvePassData->mAutoComplete->CheckAttributeTypeRef(attributesDirective->mAttributeTypeRef);
			if (attrTypeDef != NULL)
				mCompiler->mResolvePassData->HandleTypeReference(attributesDirective->mAttributeTypeRef, attrTypeDef);
		}

		bool isBypassedAttr = false;

		if (attrTypeDef != NULL)
		{
			// 'Object' has some dependencies on some attributes, but those attributes are classes so we have a circular dependency issue
			//  We solve it by having a 'bypass' for known attributes that Object depends on
			if ((attributesDirective->mArguments.empty()) && (autoComplete == NULL) && (attrType != NULL) && (attrType->IsTypeInstance()))
			{
				if (attrType->IsInstanceOf(mCompiler->mCReprAttributeTypeDef))
				{
					for (auto methodDef : attrTypeDef->mMethods)
					{
						if ((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mProtection == BfProtection_Public))
						{
							customAttribute.mType = attrType->ToTypeInstance();
							customAttribute.mCtor = methodDef;
							isBypassedAttr = true;
							break;
						}
					}
				}
			}

			if (isBypassedAttr)
			{
				customAttribute.mAwaitingValidation = false;
				customAttributes->mAttributes.push_back(customAttribute);
				continue;
			}

			SetAndRestoreValue<BfTypeInstance*> prevCurTypeInst(mContext->mUnreifiedModule->mCurTypeInstance, mCurTypeInstance);
			SetAndRestoreValue<BfMethodInstance*> prevCurMethodInst(mContext->mUnreifiedModule->mCurMethodInstance, mCurMethodInstance);
			if (mContext->mCurTypeState != NULL)
			{
				SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurAttributeTypeRef, attributesDirective->mAttributeTypeRef);
				mContext->mUnreifiedModule->ResolveTypeResult(attributesDirective->mAttributeTypeRef, attrType, BfPopulateType_BaseType, (BfResolveTypeRefFlags)0);
			}
			else
			{
				mContext->mUnreifiedModule->ResolveTypeResult(attributesDirective->mAttributeTypeRef, attrType, BfPopulateType_BaseType, (BfResolveTypeRefFlags)0);
			}
		}

		BfTypeInstance* attrTypeInst = NULL;
		if (attrType == NULL)
			continue;
		attrTypeInst = attrType->ToTypeInstance();
		if ((attrTypeInst != NULL) && (attrTypeInst->mDefineState != BfTypeDefineState_DefinedAndMethodsSlotting))
			mContext->mUnreifiedModule->PopulateType(attrType, BfPopulateType_DataAndMethods);

		if ((attrTypeInst == NULL) || (!TypeIsSubTypeOf(attrTypeInst, baseAttrTypeInst)) || (attrTypeInst->mAttributeData == NULL))
		{
			Fail(StrFormat("'%s' is not an attribute class", TypeToString(attrType).c_str()), attributesDirective->mAttributeTypeRef); //CS0616
			continue;
		}

		if ((mIsReified) && (attrTypeInst->mAttributeData != NULL) && ((attrTypeInst->mAttributeData->mFlags & BfAttributeFlag_ReflectAttribute) != 0))
		{
			// Reify attribute
			PopulateType(attrTypeInst);
		}

		if (mCurTypeInstance != NULL)
			AddDependency(attrTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_CustomAttribute);

		customAttribute.mType = attrTypeInst;

		bool allocatedMethodState = NULL;
		defer(
			{
				if (allocatedMethodState)
				{
					delete mCurMethodState;
					mCurMethodState = NULL;
				}
			});

		if (mCurMethodState == NULL)
		{
			allocatedMethodState = true;
			mCurMethodState = new BfMethodState();
			mCurMethodState->mTempKind = BfMethodState::TempKind_Static;
		}

		BfConstResolver constResolver(this);
		if (allowNonConstArgs)
			constResolver.mBfEvalExprFlags = (BfEvalExprFlags)(constResolver.mBfEvalExprFlags | BfEvalExprFlags_AllowNonConst);

		bool inPropSet = false;
		SizedArray<BfResolvedArg, 2> argValues;
		for (BfExpression* arg : attributesDirective->mArguments)
		{
			if (arg == NULL)
			{
				continue;
			}

			if (autoComplete != NULL)
				autoComplete->mShowAttributeProperties = attrTypeInst;

			if (auto assignExpr = BfNodeDynCast<BfAssignmentExpression>(arg))
			{
				inPropSet = true;
				if (autoComplete != NULL)
					autoComplete->CheckNode(assignExpr->mLeft, true);

				String findName = assignExpr->mLeft->ToString();
				BfPropertyDef* bestProp = NULL;
				BfTypeInstance* bestPropTypeInst = NULL;
				BfFieldDef* bestField = NULL;
				BfTypeInstance* bestFieldTypeInst = NULL;
				auto checkTypeInst = attrTypeInst;
				while (checkTypeInst != NULL)
				{
					mContext->mUnreifiedModule->PopulateType(checkTypeInst, BfPopulateType_Data);

					for (auto prop : checkTypeInst->mTypeDef->mProperties)
					{
						if (prop->mName == findName)
						{
							if ((bestProp == NULL) || (bestProp->mProtection != BfProtection_Public))
							{
								bestProp = prop;
								bestPropTypeInst = checkTypeInst;
							}
						}
					}

					for (auto field : checkTypeInst->mTypeDef->mFields)
					{
						if (field->mName == findName)
						{
							if ((bestField == NULL) || (bestField->mProtection != BfProtection_Public))
							{
								bestField = field;
								bestFieldTypeInst = checkTypeInst;
							}
						}
					}

					if ((bestProp != NULL) || (bestField != NULL))
						break;

					checkTypeInst = checkTypeInst->mBaseType;
				}

				bool handledExpr = false;

				if (bestField != NULL)
				{
					handledExpr = true;

					if (mCompiler->mResolvePassData != NULL)
					{
						mCompiler->mResolvePassData->HandleFieldReference(assignExpr->mLeft, attrTypeDef, bestField);
						if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(assignExpr->mLeft)))
						{
							autoComplete->mDefField = bestField;
							autoComplete->mDefType = attrTypeDef;
						}
					}

					if (bestField->mProtection != BfProtection_Public)
						Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(bestFieldTypeInst).c_str(), findName.c_str()), assignExpr->mLeft); // CS0122

					AddDependency(bestFieldTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_CustomAttribute);

					BfCustomAttributeSetField setField;
					setField.mFieldRef = BfFieldRef(bestFieldTypeInst, bestField);

					auto& fieldTypeInst = checkTypeInst->mFieldInstances[bestField->mIdx];
					if (assignExpr->mRight != NULL)
					{
						BfTypedValue result = constResolver.Resolve(assignExpr->mRight, fieldTypeInst.mResolvedType, BfConstResolveFlag_NoActualizeValues);
						if (result)
						{
							if (!keepConstsInModule)
								CurrentAddToConstHolder(result.mValue);
							setField.mParam = result;
							customAttribute.mSetField.push_back(setField);
						}
					}
				}
				else if (bestProp == NULL)
				{
					Fail(StrFormat("'%s' does not contain a field or property named '%s'", TypeToString(attrTypeInst).c_str(), findName.c_str()), assignExpr->mLeft);
				}
				else
				{
					BfMethodDef* setMethod = NULL;
					for (auto methodDef : bestProp->mMethods)
					{
						if (methodDef->mMethodType == BfMethodType_PropertySetter)
						{
							setMethod = methodDef;
							break;
						}
					}
					if (setMethod == NULL)
					{
						Fail("Property has no setter", assignExpr->mLeft);
					}
					else
					{
						if (mCompiler->mResolvePassData != NULL)
						{
							mCompiler->mResolvePassData->HandlePropertyReference(assignExpr->mLeft, attrTypeDef, bestProp);
							if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(assignExpr->mLeft)))
							{
								autoComplete->mDefProp = bestProp;
								autoComplete->mDefType = attrTypeDef;
							}
						}

						handledExpr = true;

						if (bestProp->mProtection != BfProtection_Public)
							Fail(StrFormat("'%s.%s' is inaccessible due to its protection level", TypeToString(bestPropTypeInst).c_str(), findName.c_str()), assignExpr->mLeft); // CS0122

						BfResolvedArg resolvedArg;

						AddDependency(bestPropTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_CustomAttribute);

						BfCustomAttributeSetProperty setProperty;
						setProperty.mPropertyRef = BfPropertyRef(bestPropTypeInst, bestProp);

						// We don't actually need the mFunc, so get the ModuleMethodInstance from the native module
						auto methodInstance = bestPropTypeInst->mModule->GetMethodInstance(bestPropTypeInst, setMethod, BfTypeVector());
						if (methodInstance.mMethodInstance != NULL)
						{
							auto propType = methodInstance.mMethodInstance->GetParamType(0);
							if (assignExpr->mRight != NULL)
							{
								BfTypedValue result = constResolver.Resolve(assignExpr->mRight, propType, BfConstResolveFlag_NoActualizeValues);
								if ((result) && (!result.mType->IsVar()))
								{
									if (!result.mValue.IsConst())
										result = GetDefaultTypedValue(result.mType);
									BF_ASSERT(result.mType == propType);
									if (!keepConstsInModule)
										CurrentAddToConstHolder(result.mValue);
									setProperty.mParam = result;
									customAttribute.mSetProperties.push_back(setProperty);
								}
							}
						}
					}
				}

				if ((!handledExpr) && (assignExpr->mRight != NULL))
					constResolver.Resolve(assignExpr->mRight, NULL, BfConstResolveFlag_NoActualizeValues);
			}
			else
			{
				if (inPropSet)
				{
					Fail("Named attribute argument expected", arg); // CS1016
				}

				BfDeferEvalChecker deferEvalChecker;
				arg->Accept(&deferEvalChecker);
				bool deferParamEval = deferEvalChecker.mNeedsDeferEval;

				BfResolvedArg resolvedArg;
				resolvedArg.mExpression = arg;
				if (deferParamEval)
				{
					resolvedArg.mArgFlags = BfArgFlag_DeferredEval;
				}
				else
					resolvedArg.mTypedValue = constResolver.Resolve(arg, NULL, BfConstResolveFlag_NoActualizeValues);

				if (!inPropSet)
				{
					argValues.push_back(resolvedArg);
				}
			}

			if (autoComplete != NULL)
				autoComplete->mShowAttributeProperties = NULL;
		}

		auto wasCapturingMethodInfo = false;
		if (autoComplete != NULL)
		{
			wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;
			if (attributesDirective->mCtorOpenParen != NULL)
				autoComplete->CheckInvocation(attributesDirective, attributesDirective->mCtorOpenParen, attributesDirective->mCtorCloseParen, attributesDirective->mCommas);
		}

		BfMethodMatcher methodMatcher(attributesDirective, this, "", argValues, BfMethodGenericArguments());
		methodMatcher.mBfEvalExprFlags = constResolver.mBfEvalExprFlags;
		attrTypeDef = attrTypeInst->mTypeDef;

		bool success = true;

		bool isFailurePass = false;
		for (int pass = 0; pass < 2; pass++)
		{
			bool isFailurePass = pass == 1;

			for (auto checkMethod : attrTypeDef->mMethods)
			{
				if ((isFailurePass) && (checkMethod->mMethodDeclaration == NULL))
					continue; // Don't match private default ctor if there's a user-defined one

				if ((checkMethod->mIsStatic) || (checkMethod->mMethodType != BfMethodType_Ctor))
					continue;

				if ((!isFailurePass) && (!CheckProtection(checkMethod->mProtection, attrTypeInst->mTypeDef, false, false)))
					continue;

				methodMatcher.CheckMethod(NULL, attrTypeInst, checkMethod, isFailurePass);
			}

			if ((methodMatcher.mBestMethodDef != NULL) || (methodMatcher.mBackupMethodDef != NULL))
				break;
		}

		if (autoComplete != NULL)
		{
			if ((wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo))
			{
				autoComplete->mIsCapturingMethodMatchInfo = true;
				BF_ASSERT(autoComplete->mMethodMatchInfo != NULL);
			}
			else
				autoComplete->mIsCapturingMethodMatchInfo = false;
		}

		if (methodMatcher.mBestMethodDef == NULL)
			methodMatcher.mBestMethodDef = methodMatcher.mBackupMethodDef;

		if (methodMatcher.mBestMethodDef == NULL)
		{
			AssertErrorState();
			continue;
		}

		BF_ASSERT(methodMatcher.mBestMethodDef != NULL);
		customAttribute.mCtor = methodMatcher.mBestMethodDef;

		if (methodMatcher.mBestMethodTypeInstance == mCurTypeInstance)
		{
			Fail("Custom attribute circular reference", attributesDirective);
		}

		if (mCurTypeInstance != NULL)
			AddDependency(methodMatcher.mBestMethodTypeInstance, mCurTypeInstance, BfDependencyMap::DependencyFlag_CustomAttribute);

		if (!constResolver.PrepareMethodArguments(attributesDirective->mAttributeTypeRef, &methodMatcher, customAttribute.mCtorArgs))
			success = false;

		for (auto& arg : argValues)
		{
			if ((arg.mArgFlags & BfArgFlag_DeferredEval) != 0)
			{
				if (auto expr = BfNodeDynCast<BfExpression>(arg.mExpression))
					constResolver.Resolve(expr, NULL, BfConstResolveFlag_NoActualizeValues);
			}
		}

		// Move all those to the constHolder
		if (!keepConstsInModule)
		{
			for (auto& ctorArg : customAttribute.mCtorArgs)
			{
				if (ctorArg.IsConst())
					CurrentAddToConstHolder(ctorArg);
			}
		}

		if (attributesDirective->mAttributeTargetSpecifier != NULL)
		{
			targetOverride = BfAttributeTargets_ReturnValue;
			if (attrTarget != BfAttributeTargets_Method)
			{
				Fail(StrFormat("'%s' is not a valid attribute location for this declaration. Valid attribute locations for this declaration are '%s'. All attributes in this block will be ignored.",
					GetAttributesTargetListString(targetOverride).c_str(), GetAttributesTargetListString(attrTarget).c_str()), attributesDirective->mAttributeTargetSpecifier); // CS0657
				success = false;
			}
		}

		if ((success) && (targetOverride != (BfAttributeTargets)0))
		{
			if ((mCurMethodInstance != NULL) && (targetOverride == BfAttributeTargets_ReturnValue) && (attrTarget == BfAttributeTargets_Method))
			{
				auto methodInfoEx = mCurMethodInstance->GetMethodInfoEx();

				if (methodInfoEx->mMethodCustomAttributes == NULL)
					methodInfoEx->mMethodCustomAttributes = new BfMethodCustomAttributes();
				if (success)
				{
					if (methodInfoEx->mMethodCustomAttributes->mReturnCustomAttributes == NULL)
						methodInfoEx->mMethodCustomAttributes->mReturnCustomAttributes = new BfCustomAttributes();
					methodInfoEx->mMethodCustomAttributes->mReturnCustomAttributes->mAttributes.push_back(customAttribute);
				}
			}

			// Mark as failed since we don't actually want to add this to the custom attributes set
			success = false;
		}

		if (success)
		{
			if ((attrTypeInst->mAttributeData->mFlags & BfAttributeFlag_DisallowAllowMultiple) != 0)
			{
				for (auto& prevCustomAttribute : customAttributes->mAttributes)
				{
					if (prevCustomAttribute.mType == attrTypeInst)
					{
						Fail(StrFormat("Duplicate '%s' attribute", attributesDirective->mAttributeTypeRef->ToCleanAttributeString().c_str()), attributesDirective->mAttributeTypeRef); // CS0579
					}
				}
			}
		}

		if (success)
		{
			customAttributes->mAttributes.push_back(customAttribute);
		}
	}

	ValidateCustomAttributes(customAttributes, attrTarget);
}

BfCustomAttributes* BfModule::GetCustomAttributes(BfAttributeDirective* attributesDirective, BfAttributeTargets attrType, BfGetCustomAttributesFlags flags, BfCaptureInfo* captureInfo)
{
	BfCustomAttributes* customAttributes = new BfCustomAttributes();
	GetCustomAttributes(customAttributes, attributesDirective, attrType, flags, captureInfo);
	return customAttributes;
}

BfCustomAttributes* BfModule::GetCustomAttributes(BfTypeDef* typeDef)
{
	BF_ASSERT(mCompiler->IsAutocomplete());

	BfAttributeTargets attrTarget;
	if (typeDef->mIsDelegate)
		attrTarget = BfAttributeTargets_Delegate;
	else if (typeDef->mIsFunction)
		attrTarget = BfAttributeTargets_Function;
	else if (typeDef->mTypeCode == BfTypeCode_Enum)
		attrTarget = BfAttributeTargets_Enum;
	else if (typeDef->mTypeCode == BfTypeCode_Interface)
		attrTarget = BfAttributeTargets_Interface;
	else if (typeDef->mTypeCode == BfTypeCode_Struct)
		attrTarget = BfAttributeTargets_Struct;
	else
		attrTarget = BfAttributeTargets_Class;
	return GetCustomAttributes(typeDef->mTypeDeclaration->mAttributes, attrTarget);
}

void BfModule::FinishAttributeState(BfAttributeState* attributeState)
{
	if ((!attributeState->mUsed) && (attributeState->mSrc != NULL))
		Warn(0, "Unused attributes", attributeState->mSrc);
}

void BfModule::ProcessTypeInstCustomAttributes(int& packing, bool& isUnion, bool& isCRepr, bool& isOrdered, int& alignOverride, BfType*& underlyingArrayType, int& underlyingArraySize)
{
	if (mCurTypeInstance->mTypeDef->mIsAlwaysInclude)
		mCurTypeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(mCurTypeInstance->mAlwaysIncludeFlags | BfAlwaysIncludeFlag_Type);
	if (mCurTypeInstance->mCustomAttributes != NULL)
	{
		for (auto& customAttribute : mCurTypeInstance->mCustomAttributes->mAttributes)
		{
			String typeName = TypeToString(customAttribute.mType);
			if (typeName == "System.PackedAttribute")
			{
				packing = 1;
				if (customAttribute.mCtorArgs.size() >= 1)
				{
					auto alignConstant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);

					int checkPacking = alignConstant->mInt32;
					if (((checkPacking & (checkPacking - 1)) == 0) && (packing > 0) && (packing < 256))
						packing = checkPacking;
					else
						Fail("Packing must be a power of 2", customAttribute.GetRefNode());
				}
			}
			else if (typeName == "System.UnionAttribute")
			{
				isUnion = true;
			}
			else if (typeName == "System.CReprAttribute")
			{
				isCRepr = true;
				isOrdered = true;
			}
			else if (typeName == "System.OrderedAttribute")
			{
				isOrdered = true;
			}
			else if (typeName == "System.AlwaysIncludeAttribute")
			{
				mCurTypeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(mCurTypeInstance->mAlwaysIncludeFlags | BfAlwaysIncludeFlag_Type);

				for (auto setProp : customAttribute.mSetProperties)
				{
					BfPropertyDef* propertyDef = setProp.mPropertyRef;
					if (propertyDef->mName == "AssumeInstantiated")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if ((constant != NULL) && (constant->mBool))
							mCurTypeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(mCurTypeInstance->mAlwaysIncludeFlags | BfAlwaysIncludeFlag_AssumeInstantiated);
					}
					else if (propertyDef->mName == "IncludeAllMethods")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if ((constant != NULL) && (constant->mBool))
							mCurTypeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(mCurTypeInstance->mAlwaysIncludeFlags | BfAlwaysIncludeFlag_IncludeAllMethods);
					}
				}
			}
			else if (typeName == "System.AlignAttribute")
			{
				if (customAttribute.mCtorArgs.size() >= 1)
				{
					auto alignConstant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);

					int checkAlign = alignConstant->mInt32;
					if ((checkAlign & (checkAlign - 1)) == 0)
						alignOverride = checkAlign;
					else
						Fail("Alignment must be a power of 2", customAttribute.GetRefNode());
				}
			}
			else if (typeName == "System.UnderlyingArrayAttribute")
			{
				if (customAttribute.mCtorArgs.size() >= 2)
				{
					auto typeConstant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);
					auto sizeConstant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[1]);
					if ((typeConstant != NULL) && (sizeConstant != NULL) && (typeConstant->mConstType == BfConstType_TypeOf))
					{
						underlyingArrayType = (BfType*)(intptr)typeConstant->mInt64;
						underlyingArraySize = sizeConstant->mInt32;
					}
				}
			}

			if (customAttribute.mType->mAttributeData == NULL)
				PopulateType(customAttribute.mType);
			BF_ASSERT(customAttribute.mType->mAttributeData != NULL);
			if ((customAttribute.mType->mAttributeData != NULL) && ((customAttribute.mType->mAttributeData->mAlwaysIncludeUser & BfAlwaysIncludeFlag_AssumeInstantiated) != 0))
				mCurTypeInstance->mAlwaysIncludeFlags = (BfAlwaysIncludeFlags)(mCurTypeInstance->mAlwaysIncludeFlags | customAttribute.mType->mAttributeData->mAlwaysIncludeUser);
		}
	}
}

// Checking to see if we're an attribute or not
void BfModule::ProcessCustomAttributeData()
{
	if (mCurTypeInstance->mAttributeData != NULL)
		return;

	bool isAttribute = false;
	auto checkTypeInst = mCurTypeInstance->mBaseType;
	while (checkTypeInst != NULL)
	{
		if (checkTypeInst->IsInstanceOf(mCompiler->mAttributeTypeDef))
			isAttribute = true;
		checkTypeInst = checkTypeInst->mBaseType;
	}
	if (!isAttribute)
		return;

	auto attributeData = new BfAttributeData();
	bool hasCustomAttribute = false;

	if (mCurTypeInstance->mCustomAttributes != NULL)
	{
		for (auto& customAttribute : mCurTypeInstance->mCustomAttributes->mAttributes)
		{
			if (customAttribute.mType->IsInstanceOf(mCompiler->mAttributeUsageAttributeTypeDef))
			{
				if (customAttribute.mCtorArgs.size() > 0)
				{
					auto constant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);
					if ((constant != NULL) && (mBfIRBuilder->IsInt(constant->mTypeCode)))
						attributeData->mAttributeTargets = (BfAttributeTargets)constant->mInt32;
				}

				if (customAttribute.mCtorArgs.size() == 2)
				{
					auto constant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);
					if ((constant != NULL) && (mBfIRBuilder->IsInt(constant->mTypeCode)))
					{
						attributeData->mFlags = (BfAttributeFlags)constant->mInt32;
					}
				}

				for (auto& setProp : customAttribute.mSetProperties)
				{
					BfPropertyDef* propDef = setProp.mPropertyRef;

					if (propDef->mName == "AllowMultiple")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if ((constant != NULL) && (constant->mBool))
							attributeData->mFlags = (BfAttributeFlags)(attributeData->mFlags & ~BfAttributeFlag_DisallowAllowMultiple);
						else
							attributeData->mFlags = (BfAttributeFlags)(attributeData->mFlags | BfAttributeFlag_DisallowAllowMultiple);
					}
					else if (propDef->mName == "Inherited")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if ((constant != NULL) && (constant->mBool))
							attributeData->mFlags = (BfAttributeFlags)(attributeData->mFlags & ~BfAttributeFlag_NotInherited);
						else
							attributeData->mFlags = (BfAttributeFlags)(attributeData->mFlags | BfAttributeFlag_NotInherited);
					}
					else if (propDef->mName == "ValidOn")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if (constant != NULL)
							attributeData->mAttributeTargets = (BfAttributeTargets)constant->mInt32;
					}
					else if (propDef->mName == "AlwaysIncludeUser")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if (constant != NULL)
							attributeData->mAlwaysIncludeUser = (BfAlwaysIncludeFlags)constant->mInt32;
					}
				}

				hasCustomAttribute = true;
			}
		}
	}

	if ((!hasCustomAttribute) && (mCurTypeInstance->mBaseType->mAttributeData != NULL))
	{
		attributeData->mAttributeTargets = mCurTypeInstance->mBaseType->mAttributeData->mAttributeTargets;
		attributeData->mFlags = mCurTypeInstance->mBaseType->mAttributeData->mFlags;
		attributeData->mAlwaysIncludeUser = mCurTypeInstance->mBaseType->mAttributeData->mAlwaysIncludeUser;
	}

	mCurTypeInstance->mAttributeData = attributeData;
}

bool BfModule::TryGetConstString(BfIRConstHolder* constHolder, BfIRValue irValue, StringImpl& str)
{
	auto constant = constHolder->GetConstant(irValue);
	if (constant == NULL)
		return false;
	if (constant->mTypeCode != BfTypeCode_StringId)
		return false;
	int stringId = constant->mInt32;
	BfStringPoolEntry* entry = NULL;
	if (mContext->mStringObjectIdMap.TryGetValue(stringId, &entry))
	{
		str += entry->mString;
	}
	else
	{
		BF_DBG_FATAL("Failed to find string by id");
	}
	return true;
}

BfVariant BfModule::TypedValueToVariant(BfAstNode* refNode, const BfTypedValue& value, bool allowUndef)
{
	BfVariant variant;
	variant.mTypeCode = BfTypeCode_None;
	BfType* primType = NULL;
	if (value.mType->IsPrimitiveType())
		primType = (BfPrimitiveType*)value.mType;
	else if (value.mType->IsTypedPrimitive())
		primType = value.mType->GetUnderlyingType();
	else if (value.mType->IsInstanceOf(mCompiler->mStringTypeDef))
		primType = value.mType;

	if (primType)
	{
		auto constant = mBfIRBuilder->GetConstant(value.mValue);
		if (constant != NULL)
		{
			if ((allowUndef) && (constant->mConstType == BfConstType_Undef))
			{
				variant.mUInt64 = 0;
				variant.mTypeCode = BfTypeCode_Let;
				return variant;
			}

			if (constant->mConstType == BfConstType_GlobalVar)
			{
				int stringIdx = GetStringPoolIdx(value.mValue, mBfIRBuilder);
				if (stringIdx != -1)
				{
					variant.mTypeCode = BfTypeCode_StringId;
					variant.mInt64 = stringIdx;
					return variant;
				}
			}

			switch (constant->mTypeCode)
			{
			case BfTypeCode_Boolean:
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
			case BfTypeCode_Double:
			case BfTypeCode_Char8:
			case BfTypeCode_Char16:
			case BfTypeCode_Char32:
			case BfTypeCode_StringId:
				variant.mTypeCode = constant->mTypeCode;
				variant.mInt64 = constant->mInt64;
				break;
			case BfTypeCode_Float:
				variant.mTypeCode = constant->mTypeCode;
				variant.mSingle = (float)constant->mDouble;
				break;
			default:
				if (refNode != NULL)
					Fail("Invalid const expression type", refNode);
				break;
			}
		}
	}

	return variant;
}

BfTypedValue BfModule::RemoveRef(BfTypedValue typedValue)
{
	if ((typedValue.mType != NULL) && (typedValue.mType->IsRef()))
	{
		auto refType = (BfRefType*)typedValue.mType;

		auto elementType = typedValue.mType->GetUnderlyingType();
		if (typedValue.IsAddr())
		{
			if (elementType->IsValuelessType())
			{
				BF_ASSERT(!typedValue.mValue);
				typedValue = BfTypedValue(typedValue.mValue, elementType, true);
			}
			else
				typedValue = BfTypedValue(mBfIRBuilder->CreateAlignedLoad(typedValue.mValue, elementType->mAlign), elementType, true);
		}
		else
			typedValue = BfTypedValue(typedValue.mValue, elementType, true);

		if (typedValue.mType->IsValuelessType())
		{
			BF_ASSERT(typedValue.mValue.IsFake());
		}

		if (refType->mRefKind == BfRefType::RefKind_In)
		{
			if (typedValue.mKind == BfTypedValueKind_Addr)
				typedValue.mKind = BfTypedValueKind_ReadOnlyAddr;
		}
	}
	return typedValue;
}

BfTypedValue BfModule::SanitizeAddr(BfTypedValue typedValue)
{
	if (!typedValue)
		return typedValue;

	if (typedValue.mType->IsRef())
	{
		typedValue = LoadValue(typedValue);

		auto copiedVal = BfTypedValue(CreateAlloca(typedValue.mType), typedValue.mType, true);
		mBfIRBuilder->CreateStore(typedValue.mValue, copiedVal.mValue);
		return copiedVal;
	}

	return typedValue;
}

BfTypedValue BfModule::ToRef(BfTypedValue typedValue, BfRefType* refType)
{
	if (refType == NULL)
		refType = CreateRefType(typedValue.mType);

	if ((refType->mRefKind == BfRefType::RefKind_Mut) && (typedValue.mType->IsObjectOrInterface()))
	{
		return LoadValue(typedValue);
	}

	if (refType->mRefKind == BfRefType::RefKind_Mut)
		refType = CreateRefType(typedValue.mType);

	if (!typedValue.mType->IsValuelessType())
		typedValue = MakeAddressable(typedValue, false, true);
	return BfTypedValue(typedValue.mValue, refType);
}

BfTypedValue BfModule::LoadValue(BfTypedValue typedValue, BfAstNode* refNode, bool isVolatile)
{
	if (!typedValue.IsAddr())
		return typedValue;

	PopulateType(typedValue.mType);
	if ((typedValue.mType->IsValuelessType()) || (typedValue.mType->IsVar()))
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), typedValue.mType, false);

	if (typedValue.mValue.IsConst())
	{
		auto constantValue = mBfIRBuilder->GetConstant(typedValue.mValue);
		if (constantValue != NULL)
		{
			if (constantValue->mConstType == BfConstType_GlobalVar)
			{
				auto globalVar = (BfGlobalVar*)constantValue;
				if (globalVar->mName[0] == '#')
				{
					BfTypedValue result = GetCompilerFieldValue(globalVar->mName);
					if (result)
					{
						// We want to avoid 'unreachable code' issues from values that
						//  are technically constant but change depending on compilation context
						if (mCurMethodState != NULL)
						{
							auto checkScope = mCurMethodState->mCurScope;
							while (checkScope != NULL)
							{
								checkScope->mSupressNextUnreachable = true;
								checkScope = checkScope->mPrevScope;
							}
						}
						return result;
					}

					if (!mIsComptimeModule)
						return GetDefaultTypedValue(typedValue.mType);
				}
			}

			if ((mIsComptimeModule) && (mCompiler->mCeMachine->mDebugger != NULL) && (mCompiler->mCeMachine->mDebugger->mCurDbgState != NULL))
			{
				auto ceDebugger = mCompiler->mCeMachine->mDebugger;
				auto ceContext = ceDebugger->mCurDbgState->mCeContext;
				auto activeFrame = ceDebugger->mCurDbgState->mActiveFrame;

				auto ceTypedValue = ceDebugger->GetAddr(constantValue);
				if (ceTypedValue)
				{
					if ((typedValue.mType->IsObjectOrInterface()) || (typedValue.mType->IsPointer()) || (typedValue.mType->IsRef()))
					{
						void* data = ceContext->GetMemoryPtr((addr_ce)ceTypedValue.mAddr, sizeof(addr_ce));
						if (data == NULL)
						{
							Fail("Invalid address", refNode);
							return GetDefaultTypedValue(typedValue.mType);
						}

						uint64 dataAddr;
						if (mSystem->mPtrSize == 4)
							dataAddr = *(uint32*)data;
						else
							dataAddr = *(uint64*)data;
						return BfTypedValue(mBfIRBuilder->CreateIntToPtr(
							mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, dataAddr), mBfIRBuilder->MapType(typedValue.mType)), typedValue.mType);
					}

					auto constVal = ceContext->CreateConstant(this, ceContext->mMemory.mVals + ceTypedValue.mAddr, typedValue.mType);
					if (!constVal)
					{
						Fail("Failed to create const", refNode);
						return GetDefaultTypedValue(typedValue.mType);
					}
					return BfTypedValue(constVal, typedValue.mType);
				}
			}
		}
	}

	BfIRValue loadedVal = typedValue.mValue;
	if (loadedVal)
	{
		if (typedValue.mType->IsVar())
		{
			return BfTypedValue(loadedVal, typedValue.mType, false);
		}

		PopulateType(typedValue.mType, BfPopulateType_Data);
		loadedVal = mBfIRBuilder->CreateAlignedLoad(loadedVal, std::max(1, (int)typedValue.mType->mAlign), isVolatile || typedValue.IsVolatile());
	}
	return BfTypedValue(loadedVal, typedValue.mType, false);
}

BfTypedValue BfModule::PrepareConst(BfTypedValue& typedValue)
{
	if (!typedValue.mValue.IsConst())
		return typedValue;

	auto constant = mBfIRBuilder->GetConstant(typedValue.mValue);
	if (constant->mTypeCode == BfTypeCode_StringId)
		return GetTypedValueFromConstant(constant, mBfIRBuilder, typedValue.mType);
	return typedValue;
}

BfTypedValue BfModule::LoadOrAggregateValue(BfTypedValue typedValue)
{
	if (typedValue.IsSplat())
		return AggregateSplat(typedValue);
	if (typedValue.IsAddr())
		return LoadValue(typedValue);
	return typedValue;
}

BfTypedValue BfModule::AggregateSplat(BfTypedValue typedValue, BfIRValue* valueArrPtr)
{
	if (!typedValue.IsSplat())
		return typedValue;
	BF_ASSERT(!mIsComptimeModule);
	if (typedValue.mType->IsValuelessType())
		return typedValue;

	int elementIdx = 0;

	auto _ExtractValue = [&](BfType* checkType)
	{
		if (valueArrPtr != NULL)
			return valueArrPtr[elementIdx++];
		return ExtractSplatValue(typedValue, elementIdx++, checkType);
	};

	std::function<BfIRValue(BfType*)> checkTypeLambda = [&](BfType* checkType)
	{
		if (checkType->IsStruct())
		{
			BfIRValue curValue = mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(checkType, BfIRPopulateType_Full));

			auto checkTypeInstance = checkType->ToTypeInstance();
			if (checkTypeInstance->mBaseType != NULL)
			{
				mBfIRBuilder->PopulateType(checkTypeInstance->mBaseType, BfIRPopulateType_Full);
				BfIRValue baseValue = checkTypeLambda(checkTypeInstance->mBaseType);
				curValue = mBfIRBuilder->CreateInsertValue(curValue, baseValue, 0);
			}

			if (checkTypeInstance->mIsUnion)
			{
				auto unionInnerType = checkTypeInstance->GetUnionInnerType();
				if (!unionInnerType->IsValuelessType())
				{
					BfIRValue fieldValue = checkTypeLambda(unionInnerType);
					curValue = mBfIRBuilder->CreateInsertValue(curValue, fieldValue, 1);
				}

				if (checkTypeInstance->IsEnum())
				{
					auto dscrType = checkTypeInstance->GetDiscriminatorType();
					BfIRValue fieldValue = checkTypeLambda(dscrType);
					curValue = mBfIRBuilder->CreateInsertValue(curValue, fieldValue, 2);
				}
			}
			else
			{
				for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
				{
					auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
					if (fieldInstance->mDataIdx >= 0)
					{
						BfIRValue fieldValue = checkTypeLambda(fieldInstance->GetResolvedType());
						curValue = mBfIRBuilder->CreateInsertValue(curValue, fieldValue, fieldInstance->mDataIdx);
					}
				}
			}
			return curValue;
		}
		else if (checkType->IsMethodRef())
		{
			BfMethodRefType* methodRefType = (BfMethodRefType*)checkType;
			BfIRValue curValue = mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(checkType, BfIRPopulateType_Full));

			for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
			{
				BfIRValue fieldValue;
				auto checkType = methodRefType->GetCaptureType(dataIdx);
				if (methodRefType->WantsDataPassedAsSplat(dataIdx))
				{
					fieldValue = checkTypeLambda(checkType);
				}
				else
				{
					fieldValue = _ExtractValue(checkType);
				}
				curValue = mBfIRBuilder->CreateInsertValue(curValue, fieldValue, dataIdx);
			}
			return curValue;
		}
		else if (!checkType->IsValuelessType())
		{
			return _ExtractValue(checkType);
		}

		return BfIRValue();
	};

	BfIRValue value = checkTypeLambda(typedValue.mType);
	return BfTypedValue(value, typedValue.mType, typedValue.IsThis() ? BfTypedValueKind_ThisValue : BfTypedValueKind_Value);
}

void BfModule::AggregateSplatIntoAddr(BfTypedValue typedValue, BfIRValue addrVal)
{
	if (!typedValue.IsSplat())
		return;
	if (typedValue.mType->IsValuelessType())
		return;

	BF_ASSERT(!mIsComptimeModule);

	/*static int sCallIdx = 0;
	if (!mCompiler->mIsResolveOnly)
		sCallIdx++;
	int callIdx = sCallIdx;*/

	int elementIdx = 0;

	std::function<void(BfType*, BfIRValue)> checkTypeLambda = [&](BfType* checkType, BfIRValue curAddrVal)
	{
		if (checkType->IsStruct())
		{
			auto checkTypeInstance = checkType->ToTypeInstance();
			if (checkTypeInstance->mBaseType != NULL)
			{
				mBfIRBuilder->PopulateType(checkTypeInstance->mBaseType);
				auto baseAddrVal = mBfIRBuilder->CreateInBoundsGEP(curAddrVal, 0, 0);
				checkTypeLambda(checkTypeInstance->mBaseType, baseAddrVal);
			}

			if (checkTypeInstance->mIsUnion)
			{
				auto unionInnerType = checkTypeInstance->GetUnionInnerType();
				if (!unionInnerType->IsValuelessType())
				{
					auto fieldAddrVal = mBfIRBuilder->CreateInBoundsGEP(curAddrVal, 0, 1);
					checkTypeLambda(unionInnerType, fieldAddrVal);
				}

				if (checkTypeInstance->IsEnum())
				{
					auto dscrType = checkTypeInstance->GetDiscriminatorType();
					auto fieldAddrVal = mBfIRBuilder->CreateInBoundsGEP(curAddrVal, 0, 2);
					checkTypeLambda(dscrType, fieldAddrVal);
				}
			}
			else
			{
				for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
				{
					auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
					if (fieldInstance->mDataIdx >= 0)
					{
						auto fieldAddrVal = mBfIRBuilder->CreateInBoundsGEP(curAddrVal, 0, fieldInstance->mDataIdx);
						checkTypeLambda(fieldInstance->mResolvedType, fieldAddrVal);
					}
				}
			}
		}
		else if (checkType->IsMethodRef())
		{
			BfMethodRefType* methodRefType = (BfMethodRefType*)checkType;
			for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
			{
				auto dataType = methodRefType->GetCaptureType(dataIdx);

				auto fieldAddrVal = mBfIRBuilder->CreateInBoundsGEP(curAddrVal, 0, dataIdx);
				if (methodRefType->WantsDataPassedAsSplat(dataIdx))
				{
					checkTypeLambda(methodRefType->GetCaptureType(dataIdx), fieldAddrVal);
				}
				else
				{
// 					static int sItrCount = 0;
// 					sItrCount++;
// 					int curItr = sItrCount;
// 					if (curItr == 1)
// 					{
// 						NOP;
// 					}

					auto val = ExtractSplatValue(typedValue, elementIdx++, dataType);
					mBfIRBuilder->CreateStore(val, fieldAddrVal);
				}
			}
		}
		else if (!checkType->IsValuelessType())
		{
			auto val = ExtractSplatValue(typedValue, elementIdx++, checkType);
			mBfIRBuilder->CreateStore(val, curAddrVal);
		}
	};

	checkTypeLambda(typedValue.mType, addrVal);
}

BfTypedValue BfModule::MakeAddressable(BfTypedValue typedVal, bool forceMutable, bool forceAddressable)
{
	bool wasReadOnly = typedVal.IsReadOnly();

 	if ((forceAddressable) ||
		((typedVal.mType->IsValueType()) &&
 		(!typedVal.mType->IsValuelessType())))
	{
		wasReadOnly = true; // Any non-addr is implicitly read-only

		//static int gCallIdx = 0;
		FixValueActualization(typedVal);
		if (typedVal.IsAddr())
			return typedVal;
		BfType* type = typedVal.mType;
		PopulateType(type);
		BfIRValue tempVar;
		if (typedVal.mValue.IsFake())
			tempVar = mBfIRBuilder->GetFakeVal();
		else
		{
			tempVar = CreateAlloca(type);
			if (typedVal.IsSplat())
				AggregateSplatIntoAddr(typedVal, tempVar);
			else
				mBfIRBuilder->CreateAlignedStore(typedVal.mValue, tempVar, type->mAlign);
		}

		if (forceMutable)
			wasReadOnly = false;

		return BfTypedValue(tempVar, type,
			typedVal.IsThis() ?
			(wasReadOnly ? BfTypedValueKind_ReadOnlyThisAddr : BfTypedValueKind_ThisAddr) :
			(wasReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr));
	}
	return LoadValue(typedVal);
}

BfTypedValue BfModule::RemoveReadOnly(BfTypedValue typedValue)
{
	if (typedValue.mKind == BfTypedValueKind_ReadOnlyAddr)
		typedValue.mKind = BfTypedValueKind_Addr;
	return typedValue;
}

BfTypedValue BfModule::CopyValue(const BfTypedValue& typedValue)
{
	return MakeAddressable(LoadValue(typedValue), true);
}

BfIRValue BfModule::ExtractSplatValue(BfTypedValue typedValue, int componentIdx, BfType* wantType, bool* isAddr)
{
	BF_ASSERT(!mIsComptimeModule);

	BfIRValue val;
	if (typedValue.mValue.IsArg())
	{
		val = mBfIRBuilder->GetArgument(typedValue.mValue.mId + componentIdx);
		if (isAddr != NULL)
		{
			if (wantType != NULL)
				*isAddr = wantType->IsComposite();
			else
				*isAddr = false;
		}
		else if (wantType->IsComposite())
			val = mBfIRBuilder->CreateLoad(val);
	}

	if (!val)
	{
		auto checkMethodState = mCurMethodState;
		while (checkMethodState != NULL)
		{
			for (int idx = 0; idx < (int)checkMethodState->mSplatDecompAddrs.size(); idx++)
			{
				auto decompAddr = checkMethodState->mSplatDecompAddrs[idx];
				if (decompAddr == typedValue.mValue)
				{
					val = checkMethodState->mSplatDecompAddrs[idx + componentIdx];

					if ((wantType->IsComposite()) && (typedValue.mType->IsMethodRef()))
					{
						// This is really backing for a POINTER to a composite inside a methodRef
						val = mBfIRBuilder->CreateLoad(val);
					}

					if ((isAddr != NULL) && (typedValue.mKind != BfTypedValueKind_SplatHead_NeedsCasting))
					{
						*isAddr = true;
						return val;
					}
					else
					{
						val = mBfIRBuilder->CreateAlignedLoad(val, wantType->mAlign);
						break;
					}
				}
			}

			if (val)
				break;

			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mCapturing))
			{
				BF_ASSERT(mBfIRBuilder->mIgnoreWrites);
				return mBfIRBuilder->GetFakeVal();
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	if (val)
	{
		if (typedValue.mKind == BfTypedValueKind_SplatHead_NeedsCasting)
		{
			BF_ASSERT(wantType != NULL);
			if (wantType != NULL)
				return mBfIRBuilder->CreateBitCast(val, mBfIRBuilder->MapType(wantType));
		}

		return val;
	}

	BFMODULE_FATAL(this, "Splat not found");
	return BfIRValue();
}

BfTypedValue BfModule::ExtractValue(BfTypedValue typedValue, BfFieldInstance* fieldInstance, int fieldIdx)
{
	int useFieldIdx = fieldIdx;

	BfType* fieldType = NULL;
	if (fieldInstance != NULL)
	{
		fieldType = fieldInstance->mResolvedType;
		if (typedValue.mType->IsUnion())
		{
			if (fieldIdx == 1)
			{
				if (typedValue.IsSplat())
				{
					bool isAddr = false;
					BfIRValue irVal = ExtractSplatValue(typedValue, 0, fieldType, &isAddr);
					return BfTypedValue(irVal, fieldType, typedValue.mValue.IsArg() ? BfTypedValueKind_SplatHead : BfTypedValueKind_Addr);
				}
			}
		}
	}
	else
	{
		if (typedValue.mType->IsPayloadEnum())
		{
			auto typeInst = typedValue.mType->ToTypeInstance();
			if (fieldIdx == 1)
				fieldType = typeInst->GetUnionInnerType();
			else if (fieldIdx == 2)
			{
				fieldType = typeInst->GetDiscriminatorType(&useFieldIdx);
			}
		}
		else if (typedValue.mType->IsUnion())
		{
			if (fieldIdx == 1)
			{
				fieldType = typedValue.mType->ToTypeInstance()->GetUnionInnerType();

				if (typedValue.IsSplat())
				{
					bool isAddr = false;
					BfIRValue irVal = ExtractSplatValue(typedValue, 0, fieldType, &isAddr);
					return BfTypedValue(irVal, fieldType, typedValue.mValue.IsArg() ? BfTypedValueKind_SplatHead : BfTypedValueKind_Addr);
				}
			}
		}
	}

	BF_ASSERT(typedValue.mType->IsStruct());
	if (typedValue.IsSplat())
	{
		if (typedValue.mType->IsPayloadEnum())
		{
			if (fieldIdx == 1)
			{
				// Payload
				auto typeInst = typedValue.mType->ToTypeInstance();
				auto unionInnerType = typeInst->GetUnionInnerType();
				bool isAddr = false;
				BfIRValue irVal = ExtractSplatValue(typedValue, 0, unionInnerType, &isAddr);
				return BfTypedValue(irVal, unionInnerType, BfTypedValueKind_SplatHead);
			}

			if (fieldIdx == 2)
			{
				// Discriminator
				auto typeInst = typedValue.mType->ToTypeInstance();
				auto unionInnerType = typeInst->GetUnionInnerType();
				auto dscrType = typeInst->GetDiscriminatorType();

				int argIdx = typedValue.mValue.mId;

				int componentIdx = 0;
				BfTypeUtils::SplatIterate([&](BfType* checkType) { componentIdx++; }, unionInnerType);

				bool isAddr;
				BfIRValue irVal = ExtractSplatValue(typedValue, componentIdx, dscrType, &isAddr);
				return BfTypedValue(irVal, dscrType, isAddr);
			}
		}

		int componentIdx = 0;

		BfTypedValue retVal;
		BfFieldInstance* wantFieldInstance = fieldInstance;
		std::function<void(BfType*)> checkTypeLambda = [&](BfType* checkType)
		{
			if (checkType->IsStruct())
			{
				auto checkTypeInstance = checkType->ToTypeInstance();
				if ((checkType->IsUnion()) || (checkType->IsEnum()))
				{
					//TODO: Why did we have this removed?  It messed up an extraction from a nullable splattable
					fieldType = checkTypeInstance->GetUnionInnerType();
					checkTypeLambda(fieldType);

					if (checkType->IsEnum())
					{
						// Past discriminator...
						componentIdx++;
					}
					return;
				}

				if (checkTypeInstance->mBaseType != NULL)
					checkTypeLambda(checkTypeInstance->mBaseType);
				for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
				{
					auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];

					if (fieldInstance == wantFieldInstance)
					{
						if (fieldInstance->mResolvedType->IsValuelessType())
						{
							retVal = GetDefaultTypedValue(fieldInstance->mResolvedType);
							break;
						}

						bool isAddr = false;
						BfIRValue val = ExtractSplatValue(typedValue, componentIdx, fieldInstance->mResolvedType, &isAddr);
						retVal = BfTypedValue(val, fieldInstance->mResolvedType,
							fieldInstance->mResolvedType->IsComposite() ? BfTypedValueKind_SplatHead :
							isAddr ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Value);

						if ((retVal.IsAddr()) && (!fieldInstance->mResolvedType->IsValueType()))
						{
							retVal = LoadValue(retVal);
						}

						break;
					}

					if (fieldInstance->mDataIdx >= 0)
						checkTypeLambda(fieldInstance->GetResolvedType());
				}
			}
			else if (!checkType->IsValuelessType())
			{
				componentIdx++;
				//argIdx++;
			}
		};

		checkTypeLambda(typedValue.mType);

		BF_ASSERT(retVal);
		return retVal;
	}
	if (typedValue.IsAddr())
	{
		BF_ASSERT(fieldType != NULL);
		auto valRef = mBfIRBuilder->CreateInBoundsGEP(typedValue.mValue, 0, useFieldIdx);
		return BfTypedValue(valRef, fieldType, BfTypedValueKind_ReadOnlyAddr);
	}
	else
	{
		BF_ASSERT(fieldType != NULL);
		return BfTypedValue(mBfIRBuilder->CreateExtractValue(typedValue.mValue, useFieldIdx), fieldType, BfTypedValueKind_Value);
	}
}

BfIRValue BfModule::ExtractValue(BfTypedValue typedValue, int dataIdx)
{
	if (typedValue.IsAddr())
	{
		auto addrVal = mBfIRBuilder->CreateInBoundsGEP(typedValue.mValue, 0, dataIdx);
		return mBfIRBuilder->CreateAlignedLoad(addrVal, typedValue.mType->mAlign);
	}
	return mBfIRBuilder->CreateExtractValue(typedValue.mValue, dataIdx);
}

BfIRValue BfModule::CreateIndexedValue(BfType* elementType, BfIRValue value, BfIRValue indexValue, bool isElementIndex)
{
	if (isElementIndex)
		return mBfIRBuilder->CreateInBoundsGEP(value, GetConstValue(0), indexValue);
	else
		return mBfIRBuilder->CreateInBoundsGEP(value, indexValue);

// 	if (elementType->IsSizeAligned())
// 	{
// 		if (isElementIndex)
// 			return mBfIRBuilder->CreateInBoundsGEP(value, GetConstValue(0), indexValue);
// 		else
// 			return mBfIRBuilder->CreateInBoundsGEP(value, indexValue);
// 	}
//
// 	auto ptrType = CreatePointerType(elementType);
// 	auto ofsVal = mBfIRBuilder->CreateNumericCast(indexValue, true, BfTypeCode_IntPtr);
// 	auto ofsScaledVal = mBfIRBuilder->CreateMul(ofsVal, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, elementType->GetStride()));
// 	auto i8PtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->GetPrimitiveType(BfTypeCode_Int8));
// 	BfIRValue origPtrValue = mBfIRBuilder->CreateBitCast(value, i8PtrType);
// 	BfIRValue newPtrValue = mBfIRBuilder->CreateInBoundsGEP(origPtrValue, ofsScaledVal);
// 	return mBfIRBuilder->CreateBitCast(newPtrValue, mBfIRBuilder->MapType(ptrType));
}

BfIRValue BfModule::CreateIndexedValue(BfType* elementType, BfIRValue value, int indexValue, bool isElementIndex)
{
	if (elementType->IsSizeAligned())
	{
		if (isElementIndex)
			return mBfIRBuilder->CreateInBoundsGEP(value, 0, indexValue);
		else
			return mBfIRBuilder->CreateInBoundsGEP(value, indexValue);
	}
	return CreateIndexedValue(elementType, value, GetConstValue(indexValue), isElementIndex);
}

bool BfModule::CheckModifyValue(BfTypedValue& typedValue, BfAstNode* refNode, const char* modifyType)
{
	if (modifyType == NULL)
		modifyType = "modify";

	if (typedValue.mType->IsVar())
		return true;

	if (typedValue.IsReadOnly())
	{
		Fail(StrFormat("Cannot %s read-only variable", modifyType), refNode);
		return false;
	}

	if ((!typedValue.IsAddr()) && (!typedValue.mType->IsValuelessType()))
	{
		Fail(StrFormat("Cannot %s value", modifyType), refNode);
		return false;
	}

	if (typedValue.mKind == BfTypedValueKind_RestrictedTempAddr)
	{
		Fail(StrFormat("Cannot %s temporary value", modifyType), refNode);
		return false;
	}

	return true;
}

bool BfModule::CompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB)
{
	// If one is an interface and the other is an impl, B is the impl
	auto implOwner = methodB->GetOwner();

	if (methodA->mMethodDef->mIsLocalMethod)
	{
		int sepPosA = (int)BF_MIN(methodA->mMethodDef->mName.IndexOf('@'), methodA->mMethodDef->mName.length());
		int sepPosB = (int)BF_MIN(methodB->mMethodDef->mName.IndexOf('@'), methodB->mMethodDef->mName.length());
		if (sepPosA != sepPosB)
			return false;
		if (strncmp(methodA->mMethodDef->mName.c_str(), methodB->mMethodDef->mName.c_str(), sepPosA) != 0)
			return false;
	}
	else if (methodA->mMethodDef->mName != methodB->mMethodDef->mName)
		return false;
	if (methodA->mMethodDef->mCheckedKind != methodB->mMethodDef->mCheckedKind)
		return false;
	if (methodA->mMethodDef->mHasComptime != methodB->mMethodDef->mHasComptime)
		return false;
	if ((methodA->mMethodDef->mMethodType == BfMethodType_Mixin) != (methodB->mMethodDef->mMethodType == BfMethodType_Mixin))
		return false;

	if (methodA->mMethodDef->mMethodType == BfMethodType_Ctor)
	{
		if (methodA->mMethodDef->mIsStatic != methodB->mMethodDef->mIsStatic)
			return false;
	}

	if (methodA->mMethodDef->mMethodType == BfMethodType_Operator)
	{
		if (methodB->mMethodDef->mMethodType != BfMethodType_Operator)
			return false;
		auto operatorA = (BfOperatorDef*)methodA->mMethodDef;
		auto operatorB = (BfOperatorDef*)methodB->mMethodDef;
		if (operatorA->mOperatorDeclaration->mUnaryOp != operatorB->mOperatorDeclaration->mUnaryOp)
			return false;
		if (operatorA->mOperatorDeclaration->mBinOp != operatorB->mOperatorDeclaration->mBinOp)
			return false;
		if (operatorA->mOperatorDeclaration->mAssignOp != operatorB->mOperatorDeclaration->mAssignOp)
			return false;
		if (operatorA->mOperatorDeclaration->mIsConvOperator)
		{
			if (!BfTypeUtils::TypeEquals(methodA->mReturnType, methodB->mReturnType, implOwner))
				return false;
		}
	}

	int implicitParamCountA = methodA->GetImplicitParamCount();
	int implicitParamCountB = methodB->GetImplicitParamCount();

	if (methodA->GetParamCount() - implicitParamCountA != methodB->GetParamCount() - implicitParamCountB)
		return false;

	if (methodA->mHadGenericDelegateParams != methodB->mHadGenericDelegateParams)
		return false;

	for (int paramIdx = 0; paramIdx < (int)methodA->GetParamCount() - implicitParamCountA; paramIdx++)
	{
		if ((!BfTypeUtils::TypeEquals(methodA->GetParamType(paramIdx + implicitParamCountA), methodB->GetParamType(paramIdx + implicitParamCountB), implOwner)) ||
			(methodA->GetParamKind(paramIdx + implicitParamCountA) != methodB->GetParamKind(paramIdx + implicitParamCountB)))
			return false;
	}

	// Compare generic params.  Generic params are part of the method signature here
	if (methodA->GetNumGenericParams() != methodB->GetNumGenericParams())
		return false;
	for (int genericParamIdx = 0; genericParamIdx < (int)methodA->GetNumGenericParams(); genericParamIdx++)
	{
		auto genericParamA = methodA->mMethodInfoEx->mGenericParams[genericParamIdx];
		auto genericParamB = methodB->mMethodInfoEx->mGenericParams[genericParamIdx];

		if (genericParamA->mGenericParamFlags != genericParamB->mGenericParamFlags)
			return false;
		if (genericParamA->mTypeConstraint != genericParamB->mTypeConstraint)
			return false;
		if (genericParamA->mInterfaceConstraints.size() != genericParamB->mInterfaceConstraints.size())
			return false;
		for (int interfaceIdx = 0; interfaceIdx < (int)genericParamA->mInterfaceConstraints.size(); interfaceIdx++)
			if (genericParamA->mInterfaceConstraints[interfaceIdx] != genericParamB->mInterfaceConstraints[interfaceIdx])
				return false;
	}

	return true;
}

bool BfModule::StrictCompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB)
{
	if (!CompareMethodSignatures(methodA, methodB))
		return false;
	if (methodA->mReturnType != methodB->mReturnType)
		return false;
	if (methodA->mMethodDef->mIsStatic != methodB->mMethodDef->mIsStatic)
		return false;
	return true;
}

bool BfModule::IsCompatibleInterfaceMethod(BfMethodInstance* iMethodInst, BfMethodInstance* methodInstance)
{
	if ((iMethodInst->GetNumGenericParams() != 0) || (methodInstance->GetNumGenericParams() != 0))
		return false;

	if (iMethodInst->mMethodDef->mName != methodInstance->mMethodDef->mName)
		return false;

	if (iMethodInst->mMethodDef->mIsOperator != methodInstance->mMethodDef->mIsOperator)
		return false;

	if (iMethodInst->mMethodDef->mIsOperator)
	{
		auto iOperatorDef = (BfOperatorDef*)iMethodInst->mMethodDef;
		auto operatorDef = (BfOperatorDef*)methodInstance->mMethodDef;

		if (iOperatorDef->mOperatorDeclaration->mUnaryOp != operatorDef->mOperatorDeclaration->mUnaryOp)
			return false;
		if (iOperatorDef->mOperatorDeclaration->mBinOp != operatorDef->mOperatorDeclaration->mBinOp)
			return false;
	}

	if (iMethodInst->GetParamCount() != methodInstance->GetParamCount())
		return false;

	auto selfType = methodInstance->GetOwner();
	for (int paramIdx = 0; paramIdx < (int)iMethodInst->GetParamCount(); paramIdx++)
	{
		if (iMethodInst->GetParamKind(paramIdx) != methodInstance->GetParamKind(paramIdx))
			return false;

		BfType* iParamType = iMethodInst->GetParamType(paramIdx);
		BfType* methodParamType = methodInstance->GetParamType(paramIdx);

		iParamType = ResolveSelfType(iParamType, selfType);
		methodParamType = ResolveSelfType(methodParamType, selfType);

		if (!iParamType->IsGenericParam())
		{
			if (methodParamType != iParamType)
				return false;
		}
		else
		{
			if (!methodParamType->IsGenericParam())
				return false;

			auto genericParamType = (BfGenericParamType*)methodParamType;
			if (genericParamType->mGenericParamKind == BfGenericParamKind_Type)
				return false;

			auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
			bool hadInterface = false;
			for (auto interfaceConstraint : genericParam->mInterfaceConstraints)
				if (interfaceConstraint == iParamType)
					hadInterface = true;
			if (!hadInterface)
				return false;
		}
	}

	return true;
}

void BfModule::AddMethodReference(const BfMethodRef& methodRef, BfGetMethodInstanceFlags flags)
{
	BfMethodInstance* methodInstance = methodRef;

	if ((mCurTypeInstance != NULL) && (!mCompiler->IsAutocomplete()))
	{
		auto methodInstanceGroup = methodInstance->mMethodInstanceGroup;
		if ((methodInstance->IsSpecializedGenericMethod()) || (methodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude))
		{
			BF_ASSERT(!methodRef.mTypeInstance->IsFunction());

			// This ensures we rebuild - there are some cases where we get a method reference but never call it, so this is required here
			AddDependency(methodInstance->GetOwner(), mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);

			BfMethodRef methodRef = methodInstance;
			BfSpecializedMethodRefInfo* specializedMethodRefInfo = NULL;
			bool isNew = mCurTypeInstance->mSpecializedMethodReferences.TryAdd(methodRef, NULL, &specializedMethodRefInfo);

			if (((flags & BfGetMethodInstanceFlag_Unreified) == 0) &&
				((flags & BfGetMethodInstanceFlag_NoForceReification) == 0) &&
				(mIsReified))
			{
				specializedMethodRefInfo->mHasReifiedRef = true;
			}
		}
	}
}

BfModuleMethodInstance BfModule::ReferenceExternalMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags)
{
	if ((flags & BfGetMethodInstanceFlag_ResultNotUsed) != 0)
		return BfModuleMethodInstance(methodInstance, BfIRValue());

	if (mBfIRBuilder == NULL)
		return BfModuleMethodInstance(methodInstance, BfIRValue());

	if (methodInstance->GetOwner()->IsFunction())
	{
		// No actual ref needed- not an actual generated function
		return BfModuleMethodInstance(methodInstance, BfIRValue());
	}

	bool isGenFunction = methodInstance->mMethodInstanceGroup->mMethodIdx < 0;
	if (!isGenFunction)
		AddMethodReference(methodInstance, flags);

	if ((mBfIRBuilder->mIgnoreWrites) || ((flags & BfGetMethodInstanceFlag_Unreified) != 0))
		return BfModuleMethodInstance(methodInstance, mBfIRBuilder->GetFakeVal());

	if (IsSkippingExtraResolveChecks())
		return BfModuleMethodInstance(methodInstance, BfIRFunction());

	if (methodInstance->mMethodDef->mMethodType == BfMethodType_Mixin)
	{
		return BfModuleMethodInstance(methodInstance, BfIRFunction());
	}

	bool isInlined = (methodInstance->mAlwaysInline) || ((flags & BfGetMethodInstanceFlag_ForceInline) != 0);
	if ((methodInstance->mIsIntrinsic) || (methodInstance->mMethodDef->mIsExtern))
		isInlined = false;

	BfMethodRef methodRef = methodInstance;
	if (isInlined)
		methodRef.mMethodRefFlags = (BfMethodRefFlags)(methodRef.mMethodRefFlags | BfMethodRefFlag_AlwaysInclude);
	else
		methodRef.mMethodRefFlags = BfMethodRefFlag_None;

	if (!isGenFunction)
	{
		BfIRValue* irFuncPtr = NULL;
		if (mFuncReferences.TryGetValue(methodRef, &irFuncPtr))
			return BfModuleMethodInstance(methodInstance, *irFuncPtr);
	}

	if (mAwaitingInitFinish)
		FinishInit();

	BfIRValue func = CreateFunctionFrom(methodInstance, isGenFunction, isInlined);

	if (!isGenFunction)
	{
		BF_ASSERT(func || methodInstance->mMethodInstanceGroup->mOwner->IsInterface());
		mFuncReferences[methodRef] = func;
	}
	BfLogSysM("Adding func reference. Module:%p MethodInst:%p NewLLVMFunc:%d OldLLVMFunc:%d\n", this, methodInstance, func.mId, methodInstance->mIRFunction.mId);

	if ((isInlined) && (!mIsScratchModule) && ((flags & BfGetMethodInstanceFlag_NoInline) == 0))
	{
		// We can't just add a dependency to mCurTypeInstance because we may have nested inlined functions, and
		//   mCurTypeInstance will just reflect the owner of the method currently being inlined, not the top-level
		//   type instance
		// Be smarter about this if we ever insert a lot of type instances into a single module - track in a field
		BF_ASSERT(mOwnedTypeInstances.size() <= 1);
		for (auto ownedTypeInst : mOwnedTypeInstances)
			AddDependency(methodInstance->GetOwner(), ownedTypeInst, BfDependencyMap::DependencyFlag_InlinedCall);

		if ((!mCompiler->mIsResolveOnly) && (mIsReified) && (!methodInstance->mIsUnspecialized))
		{
			mIncompleteMethodCount++;
			BfInlineMethodRequest* inlineMethodRequest = mContext->mInlineMethodWorkList.Alloc();
			inlineMethodRequest->mType = methodInstance->GetOwner();
			inlineMethodRequest->mFromModule = this;
			inlineMethodRequest->mFunc = func;
			inlineMethodRequest->mFromModuleRevision = mRevision;
			inlineMethodRequest->mFromModuleRebuildIdx = mRebuildIdx;
			inlineMethodRequest->mMethodInstance = methodInstance;
			BF_ASSERT(mIsModuleMutable);

			BfLogSysM("mInlineMethodWorkList %p for method %p in module %p in ReferenceExternalMethodInstance\n", inlineMethodRequest, methodInstance, this);
		}
	}

	return BfModuleMethodInstance(methodInstance, func);
}

BfModule* BfModule::GetOrCreateMethodModule(BfMethodInstance* methodInstance)
{
	BfTypeInstance* typeInst = methodInstance->mMethodInstanceGroup->mOwner;

	if (mBfIRBuilder == NULL)
	{
		// To allow for custom attribute data
		PrepareForIRWriting(typeInst);
	}

	BfModule* declareModule = this;
	// Get to the optionless branch head -- but this could still be a specialized method module, not the true root
	while ((declareModule->mParentModule != NULL) && (!declareModule->mIsSpecializedMethodModuleRoot))
		declareModule = declareModule->mParentModule;

	// Check for attributes
	if (typeInst == mContext->mBfObjectType)
	{
		auto methodDecl = methodInstance->mMethodDef->GetMethodDeclaration();
		if (methodDecl != NULL)
		{
			auto attributesDirective = methodDecl->mAttributes;
			if ((attributesDirective != NULL) && (mCompiler->mResolvePassData != NULL))
			{
				if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(attributesDirective))
					sourceClassifier->VisitChild(attributesDirective);
			}
		}
	}
	else
	{
		// Only allow attributes on System.Object methods that can be handled inside the DefBuilder
		GetMethodCustomAttributes(methodInstance);
	}
	BF_ASSERT(mModuleOptions == NULL);
	if (methodInstance->GetCustomAttributes() != NULL)
	{
		auto project = typeInst->mTypeDef->mProject;
		BfModuleOptions moduleOptions = declareModule->GetModuleOptions();

		BfModuleOptions wantOptions = moduleOptions;
		auto typeOptions = mSystem->GetTypeOptions(typeInst->mTypeOptionsIdx);
		if (typeOptions != NULL)
		{
			wantOptions.mOptLevel = (BfOptLevel)BfTypeOptions::Apply((int)wantOptions.mOptLevel, (int)typeOptions->mOptimizationLevel);
			wantOptions.mSIMDSetting = (BfSIMDSetting)BfTypeOptions::Apply((int)wantOptions.mSIMDSetting, typeOptions->mSIMDSetting);
			wantOptions.mEmitDebugInfo = (BfSIMDSetting)BfTypeOptions::Apply((int)wantOptions.mEmitDebugInfo, typeOptions->mEmitDebugInfo);
		}

		if (!mCompiler->mAttributeTypeOptionMap.IsEmpty())
		{
			StringT<128> attrName;
			for (auto& customAttrs : methodInstance->GetCustomAttributes()->mAttributes)
			{
				attrName.Clear();
				customAttrs.mType->mTypeDef->mFullName.ToString(attrName);
				Array<int>* arrPtr;
				if (mCompiler->mAttributeTypeOptionMap.TryGetValue(attrName, &arrPtr))
				{
					for (auto optionsIdx : *arrPtr)
					{
						auto& typeOptions = mCompiler->mSystem->mTypeOptions[optionsIdx];
						wantOptions.mOptLevel = (BfOptLevel)BfTypeOptions::Apply((int)wantOptions.mOptLevel, (int)typeOptions.mOptimizationLevel);
						wantOptions.mSIMDSetting = (BfSIMDSetting)BfTypeOptions::Apply((int)wantOptions.mSIMDSetting, typeOptions.mSIMDSetting);
						wantOptions.mEmitDebugInfo = (BfSIMDSetting)BfTypeOptions::Apply((int)wantOptions.mEmitDebugInfo, typeOptions.mEmitDebugInfo);
					}
				}
			}
		}

		if ((HasCompiledOutput()) && (wantOptions != moduleOptions) && (!mIsScratchModule))
		{
			auto lastCheckModule = this;
			auto checkModule = this->mNextAltModule;
			while (checkModule != NULL)
			{
				if (*checkModule->mModuleOptions == wantOptions)
				{
					declareModule = checkModule;
					break;
				}

				lastCheckModule = checkModule;
				checkModule = checkModule->mNextAltModule;
			}

			// Not found?
			if (declareModule == this)
			{
				String specModuleName = mModuleName + "@@";
				if (wantOptions.mOptLevel != moduleOptions.mOptLevel)
					specModuleName += StrFormat("O%d", wantOptions.mOptLevel);
				if (wantOptions.mSIMDSetting != moduleOptions.mSIMDSetting)
					specModuleName += StrFormat("SIMD%d", wantOptions.mSIMDSetting);

				declareModule = new BfModule(mContext, specModuleName);
				declareModule->mProject = project;
				declareModule->mModuleOptions = new BfModuleOptions();
				*declareModule->mModuleOptions = wantOptions;
				declareModule->mParentModule = lastCheckModule;
				declareModule->Init();
				lastCheckModule->mNextAltModule = declareModule;
			}
		}
	}

	declareModule->PrepareForIRWriting(typeInst);
	return declareModule;
}

BfModuleMethodInstance BfModule::GetMethodInstance(BfTypeInstance* typeInst, BfMethodDef* methodDef, const BfTypeVector& methodGenericArguments, BfGetMethodInstanceFlags flags, BfTypeInstance* foreignType)
{
	if (methodDef->mMethodType == BfMethodType_Init)
		return BfModuleMethodInstance();

	if (((flags & BfGetMethodInstanceFlag_ForceInline) != 0) && (mCompiler->mIsResolveOnly))
	{
		// Don't bother inlining for resolve-only
		flags = (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline);
	}

	if (methodDef->mIsExtern)
		flags = (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline);

	bool processNow = false;
	bool keepInCurrentModule = false;

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecialized))
		flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_NoForceReification);

	if (!typeInst->IsTypeMemberIncluded(methodDef->mDeclaringType))
	{
		return BfModuleMethodInstance();
	}

	if (methodDef->mIsLocalMethod)
	{
		auto rootMethodState = mCurMethodState->GetRootMethodState();
		BfLocalMethod* localMethod;
		if (rootMethodState->mLocalMethodCache.TryGetValue(methodDef->mName, &localMethod))
		{
			// Handle the def in the correct method state
			return GetLocalMethodInstance(localMethod, methodGenericArguments);
		}

		BFMODULE_FATAL(this, "Cannot find local method");
 		return BfModuleMethodInstance();
	}

#ifdef _DEBUG
	for (auto arg : methodGenericArguments)
		BF_ASSERT(!arg->IsVar());
#endif

	BF_ASSERT(methodDef->mMethodType != BfMethodType_Ignore);

	// We need to do the 'mNeedsMethodProcessing' check because we want to do a proper initial "awaiting reference" population
	//  on the methods before we handle an on-demand situation.  This also ensures that our type options are set before doing
	//  a FinishInit
	if ((typeInst->IsIncomplete()) &&
		(!typeInst->mResolvingVarField) && (!typeInst->mTypeFailed))
	{
		// For autocomplete, we still may not actually generate methods. This shouldn't matter, and on-demand works differently
		//  for resolve-only because we don't differentiate between reified/unreified there
		if ((methodDef->mIsStatic) /*&& (mIsComptimeModule)*/)
		{
			if (!typeInst->DefineStateAllowsStaticMethods())
				PopulateType(typeInst, BfPopulateType_AllowStaticMethods);
		}
		else
			PopulateType(typeInst, BfPopulateType_Full);

		if (typeInst->mDefineState < BfTypeDefineState_Defined)
		{
			if (!mCompiler->EnsureCeUnpaused(typeInst))
			{
				BfLogSysM("GetMethodInstance (DefineState < BfTypeDefineState_Defined)) bailing due to IsCePaused\n");
				return BfModuleMethodInstance();
			}
		}
	}

	bool tryModuleMethodLookup = false;
	BfModuleMethodInstance moduleMethodInst;

	BfModule* instModule = typeInst->mModule;

	if (keepInCurrentModule)
	{
		// Stay here
		instModule = this;
	}

	if (this == mContext->mUnreifiedModule)
	{
		// Stay in this 'resolve only' module here
	}
	else if (flags & BfGetMethodInstanceFlag_ExplicitResolveOnlyPass)
	{
		BF_ASSERT(this == mContext->mUnreifiedModule);
	}
	else if (flags & BfGetMethodInstanceFlag_ExplicitSpecializedModule)
	{
		BF_ASSERT(instModule == mParentModule);
	}
	else if (instModule != this)
	{
		if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mMinDependDepth >= 32))
			flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_DepthExceeded);

		if ((!mIsComptimeModule) && (!mIsReified) && (instModule->mIsReified))
		{
			BF_ASSERT(!mCompiler->mIsResolveOnly);
			// A resolve-only module is specializing a method from a type in a reified module,
			//  we need to take care that this doesn't cause anything new to become reified
			BfModuleMethodInstance moduleMethodInstance = mContext->mUnreifiedModule->GetMethodInstance(typeInst, methodDef, methodGenericArguments, (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ExplicitResolveOnlyPass), foreignType);
			if (!moduleMethodInstance)
				return moduleMethodInstance;
			SetMethodDependency(moduleMethodInstance.mMethodInstance);
			return moduleMethodInstance;
		}
		else
		{
			if ((!mIsComptimeModule) && (mIsReified) && (!instModule->mIsReified))
			{
				if (!typeInst->IsUnspecializedType())
				{
					if (!instModule->mReifyQueued)
					{
						BF_ASSERT((instModule != mContext->mUnreifiedModule) && (instModule != mContext->mScratchModule));
						BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));
						BfLogSysM("Queueing ReifyModule: %p\n", instModule);
						mContext->mReifyModuleWorkList.Add(instModule);
						instModule->mReifyQueued = true;
					}

					// This ensures that the method will actually be created when it gets reified
					BfMethodSpecializationRequest* specializationRequest = mContext->mMethodSpecializationWorkList.Alloc();
					specializationRequest->mFromModule = typeInst->mModule;
					specializationRequest->mFromModuleRevision = typeInst->mModule->mRevision;
					specializationRequest->Init(typeInst, foreignType, methodDef);
					//specializationRequest->mMethodDef = methodDef;
					specializationRequest->mMethodGenericArguments = methodGenericArguments;
					specializationRequest->mType = typeInst;
					specializationRequest->mFlags = flags;
				}
			}

			auto defFlags = (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline);

			defFlags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_NoReference);

			if (mIsComptimeModule)
			{
				defFlags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_MethodInstanceOnly);
				if (!mCompiler->mIsResolveOnly)
					defFlags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_NoForceReification | BfGetMethodInstanceFlag_Unreified | BfGetMethodInstanceFlag_MethodInstanceOnly);
			}

			// Not extern
			// Create the instance in the proper module and then create a reference in this one
			moduleMethodInst = instModule->GetMethodInstance(typeInst, methodDef, methodGenericArguments, defFlags, foreignType);
			if (!moduleMethodInst)
				return moduleMethodInst;
			tryModuleMethodLookup = true;

			if ((mIsReified) && (!moduleMethodInst.mMethodInstance->mIsReified))
			{
				CheckHotMethod(moduleMethodInst.mMethodInstance, "");
			}
		}
	}

	if (tryModuleMethodLookup)
	{
		SetMethodDependency(moduleMethodInst.mMethodInstance);
		return ReferenceExternalMethodInstance(moduleMethodInst.mMethodInstance, flags);
	}

	if (((flags & BfGetMethodInstanceFlag_ForceInline) != 0) && (!methodDef->mAlwaysInline))
	{
		auto moduleMethodInstance = GetMethodInstance(typeInst, methodDef, methodGenericArguments, (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline), foreignType);
		if (moduleMethodInstance)
			return ReferenceExternalMethodInstance(moduleMethodInstance.mMethodInstance, flags);
		return moduleMethodInst;
	}

	bool isReified = mIsReified /* || ((flags & BfGetMethodInstanceFlag_ExplicitResolveOnlyPass) != 0)*/;
	bool hadConcreteInterfaceGenericArgument = false;

	if ((flags & BfGetMethodInstanceFlag_Unreified) != 0)
		isReified = false;

	if ((flags & BfGetMethodInstanceFlag_ExplicitResolveOnlyPass) != 0)
	{
		isReified = false;
	}

	BfTypeVector sanitizedMethodGenericArguments;
	SizedArray<BfProject*, 4> projectList;

	bool isUnspecializedPass = (flags & BfGetMethodInstanceFlag_UnspecializedPass) != 0;
	if ((isUnspecializedPass) && (methodDef->mGenericParams.size() == 0))
		isUnspecializedPass = false;

	// Check for whether we are an extension method from a project that does not hold the root type, AND that isn't already the project for this type
	bool isExternalExtensionMethod = false;
	if ((!typeInst->IsUnspecializedType()) && (!isUnspecializedPass))
	{
		if (((flags & BfGetMethodInstanceFlag_ForeignMethodDef) == 0) &&
			(methodDef->mDeclaringType != NULL) &&
			(methodDef->mDeclaringType->mProject != typeInst->mTypeDef->mProject))
		{
			auto specProject = methodDef->mDeclaringType->mProject;

			isExternalExtensionMethod = true;
			if (typeInst->IsGenericTypeInstance())
			{
				auto genericTypeInst = (BfTypeInstance*)typeInst;
				if (genericTypeInst->mGenericTypeInfo->mProjectsReferenced.empty())
					genericTypeInst->GenerateProjectsReferenced();
				if (genericTypeInst->mGenericTypeInfo->mProjectsReferenced.Contains(specProject))
				{
					// This is a generic type where a generic param is already confined to the project in question
					isExternalExtensionMethod = false;
				}
			}

			if (isExternalExtensionMethod)
			{
				BF_ASSERT(typeInst->mTypeDef->mIsCombinedPartial);
				projectList.push_back(methodDef->mDeclaringType->mProject);
			}
		}
	}

	if ((!isUnspecializedPass) && (methodGenericArguments.size() != 0))
	{
		int typeProjectsCounts = 0;
		if (typeInst->IsGenericTypeInstance())
		{
			auto genericTypeInst = (BfTypeInstance*)typeInst;
			if (genericTypeInst->mGenericTypeInfo->mProjectsReferenced.empty())
				genericTypeInst->GenerateProjectsReferenced();
			typeProjectsCounts = (int)genericTypeInst->mGenericTypeInfo->mProjectsReferenced.size();
			projectList.Insert(0, &genericTypeInst->mGenericTypeInfo->mProjectsReferenced[0], genericTypeInst->mGenericTypeInfo->mProjectsReferenced.size());
		}
		else
		{
			typeProjectsCounts = 1;
			projectList.Insert(0, typeInst->mTypeDef->mProject);
		}

		isUnspecializedPass = true;

		for (int genericArgIdx = 0; genericArgIdx < (int) methodGenericArguments.size(); genericArgIdx++)
		{
			auto genericArgType = methodGenericArguments[genericArgIdx];
			//BF_ASSERT(!genericArgType->IsRef());

			if (genericArgType->IsConcreteInterfaceType())
			{
				hadConcreteInterfaceGenericArgument = true;
				auto concreteInterfaceType = (BfConcreteInterfaceType*)genericArgType;
				genericArgType = concreteInterfaceType->mInterface;
			}

			if (genericArgType->IsGenericParam())
			{
				auto genericParam = (BfGenericParamType*) genericArgType;
				if ((genericParam->mGenericParamKind != BfGenericParamKind_Method) || (genericParam->mGenericParamIdx != genericArgIdx))
					isUnspecializedPass = false;
			}
			else
			{
				BfTypeUtils::GetProjectList(genericArgType, &projectList, typeProjectsCounts);
				isUnspecializedPass = false;
			}

			sanitizedMethodGenericArguments.push_back(genericArgType);
		}

		if ((int)projectList.size() > typeProjectsCounts)
		{
			// Just leave the new items
			projectList.RemoveRange(0, typeProjectsCounts);
			std::sort(projectList.begin(), projectList.end(), [](BfProject* lhs, BfProject*rhs)
				{
					return lhs->mName < rhs->mName;
				});
		}
		else
		{
			projectList.Clear();
		}
	}

	const BfTypeVector& lookupMethodGenericArguments = isUnspecializedPass ? BfTypeVector() : sanitizedMethodGenericArguments;
	BfMethodInstanceGroup* methodInstGroup = NULL;
	if ((flags & BfGetMethodInstanceFlag_ForeignMethodDef) != 0)
	{
		BF_ASSERT(!typeInst->IsInterface());

		//for (auto& checkGroup : typeInst->mMethodInstanceGroups)
		for (int methodIdx = (int)typeInst->mTypeDef->mMethods.size(); methodIdx < (int)typeInst->mMethodInstanceGroups.size(); methodIdx++)
		{
 			auto& checkGroup = typeInst->mMethodInstanceGroups[methodIdx];
// 			if (checkGroup.mDefault == NULL)
// 			{
// 				// This should only be for when we explicitly add a spot so we can set mOnDemandKind
// 				BF_ASSERT(methodIdx == typeInst->mMethodInstanceGroups.size() - 1);
// 				methodInstGroup = &checkGroup;
// 				break;
// 			}

			if ((checkGroup.mDefault != NULL) && (checkGroup.mDefault->mMethodDef == methodDef))
			{
				methodInstGroup = &checkGroup;
				break;
			}
		}

		if (methodInstGroup == NULL)
		{
			if (lookupMethodGenericArguments.size() != 0)
			{
				BF_ASSERT((flags & BfGetMethodInstanceFlag_ForeignMethodDef) != 0);
				auto defaultMethodInstance = GetMethodInstance(typeInst, methodDef, BfTypeVector(),
					(BfGetMethodInstanceFlags)((flags & (BfGetMethodInstanceFlag_ForeignMethodDef)) | BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_MethodInstanceOnly), foreignType);
				methodInstGroup = defaultMethodInstance.mMethodInstance->mMethodInstanceGroup;
			}
			else
			{
				// Allocate a new entry
				typeInst->mMethodInstanceGroups.Resize(typeInst->mMethodInstanceGroups.size() + 1);
				methodInstGroup = &typeInst->mMethodInstanceGroups.back();
				methodInstGroup->mMethodIdx = (int)typeInst->mMethodInstanceGroups.size() - 1;
				methodInstGroup->mOwner = typeInst;
				// 			if (methodInstGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
				// 				methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
				if (mCompiler->mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
					methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
				else
				{
					methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
					if (!mIsScratchModule)
						mOnDemandMethodCount++;
				}
			}
		}
	}
	else
	{
		methodInstGroup = &typeInst->mMethodInstanceGroups[methodDef->mIdx];
	}

	if (methodInstGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
	{
		if (typeInst->mDefineState > BfTypeDefineState_DefinedAndMethodsSlotted)
		{
			BfLogSysM("Forcing BfMethodOnDemandKind_NotSet to BfMethodOnDemandKind_AlwaysInclude for Method:%s in Type:%p\n", methodDef->mName.c_str(), typeInst);
			methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
		}
	}

	BfIRFunction prevIRFunc;

	bool doingRedeclare = false;
	BfMethodInstance* methodInstance = NULL;

	auto _SetReified = [&]()
	{
		if (!mCompiler->mIsResolveOnly)
			BF_ASSERT(mCompiler->mCompileState <= BfCompiler::CompileState_Normal);
		methodInstance->mIsReified = true;
	};

	if (lookupMethodGenericArguments.size() == 0)
	{
		methodInstance = methodInstGroup->mDefault;

		if ((methodInstance != NULL) && ((flags & BfGetMethodInstanceFlag_MethodInstanceOnly) != 0))
			return methodInstance;

		if ((methodInstance != NULL) && (isReified) && (!methodInstance->mIsReified))
		{
			MarkDerivedDirty(typeInst);

			if (!HasCompiledOutput())
			{
				BfLogSysM("Marking non-compiled-output module method instance as reified: %p\n", methodInstance);
				_SetReified();
				CheckHotMethod(methodInstance, "");
			}
			else if ((flags & BfGetMethodInstanceFlag_NoForceReification) != 0)
			{
				// We don't need to force reification if we're in an unspecialized generic- we can wait for
				//  the specializations to handle that for us
				return BfModuleMethodInstance(methodInstance);
			}
			else if (methodInstGroup->mOnDemandKind == BfMethodOnDemandKind_Referenced)
			{
				if (methodDef->mIsAbstract)
				{
					BfLogSysM("Reifying abstract unreified method instance: %p IRFunction:%d\n", methodInstance, methodInstance->mIRFunction.mId);
					_SetReified();
				}
				else
				{
					BfLogSysM("Re-declaring processed but unreified method instance: %p IRFunction:%d\n", methodInstance, methodInstance->mIRFunction.mId);
					methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_NoDecl_AwaitingReference;
					methodInstance->UndoDeclaration(!methodInstance->mIRFunction.IsFake());
					doingRedeclare = true;
					mOnDemandMethodCount++;
					VerifyOnDemandMethods();
				}
			}
			else
			{
				BfLogSysM("Marking unprocessed method instance as reified: %p\n", methodInstance);

				// We haven't processed it yet
				_SetReified();
				CheckHotMethod(methodInstance, "");

				if (methodInstance->mDeclModule == this)
				{
					if (methodInstance->mMethodProcessRequest != NULL)
					{
						// Disconnect method process request
						BF_ASSERT(methodInstance->mMethodProcessRequest->mFromModule == this);
					}
				}
				else
				{
					if (methodInstance->mMethodProcessRequest != NULL)
					{
						// Disconnect method process request
						methodInstance->mMethodProcessRequest->mMethodInstance = NULL;
						methodInstance->mMethodProcessRequest = NULL;
					}

					if (methodInstance->mDeclModule == mContext->mUnreifiedModule)
					{
						// Add new request in proper module
						methodInstance->mDeclModule = this;
					}
					else
					{
						if (methodInstance->mDeclModule == NULL)
						{
							//
						}
						else if (methodInstance->mDeclModule != this)
						{
							// Is from specialized module?
							BF_ASSERT(methodInstance->mDeclModule->mParentModule == this);
						}
					}

					if ((!methodInstance->mIRFunction) || (methodInstance->mIRFunction.IsFake()))
					{
						methodInstance->mIRFunction = BfIRFunction();
						if (!mIsModuleMutable)
							StartExtension();

						if ((!mBfIRBuilder->mIgnoreWrites) && (methodInstance->mDeclModule != NULL))
						{
							StringT<512> mangledName;
							BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), methodInstance);
							bool isIntrinsic = false;
							SetupIRFunction(methodInstance, mangledName, false, &isIntrinsic);

							BfLogSysM("Creating new IRFunction %d\n", methodInstance->mIRFunction.mId);
						}
					}

					AddMethodToWorkList(methodInstance);
				}
			}
		}
	}
	else
	{
		if (methodInstGroup->mDefault == NULL)
		{
			auto defaultMethodInstance = GetMethodInstance(typeInst, methodDef, BfTypeVector(),
				(BfGetMethodInstanceFlags)((flags & (BfGetMethodInstanceFlag_ForeignMethodDef)) | BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_MethodInstanceOnly), foreignType);
			methodInstGroup = defaultMethodInstance.mMethodInstance->mMethodInstanceGroup;
		}

		BF_ASSERT(lookupMethodGenericArguments.size() != 0);
		if (methodInstGroup->mMethodSpecializationMap == NULL)
			methodInstGroup->mMethodSpecializationMap = new BfMethodInstanceGroup::MapType();

		BfMethodInstance** methodInstancePtr = NULL;
		if (methodInstGroup->mMethodSpecializationMap->TryGetValue(lookupMethodGenericArguments, &methodInstancePtr))
		{
			methodInstance = *methodInstancePtr;

			if ((flags & BfGetMethodInstanceFlag_MethodInstanceOnly) != 0)
				return methodInstance;

			if ((methodInstance->mRequestedByAutocomplete) && (!mCompiler->IsAutocomplete()))
			{
				BfLogSysM("Setting mRequestedByAutocomplete=false for method instance %p\n", methodInstance);
				// We didn't want to process this message yet if it was autocomplete-specific, but now we will
				if (methodInstance->mMethodProcessRequest == NULL)
					AddMethodToWorkList(methodInstance);
				methodInstance->mRequestedByAutocomplete = false;
			}

			if ((isReified) && (!methodInstance->mIsReified))
			{
				MarkDerivedDirty(typeInst);

				if (methodInstance->mHasBeenProcessed)
				{
					BfLogSysM("Deleting processed but unreified specialized method instance %p\n", methodInstance);

					delete methodInstance;
					methodInstGroup->mMethodSpecializationMap->Remove(lookupMethodGenericArguments);
					methodInstance = NULL;
				}
				else if ((!methodInstance->mIRFunction) || (methodInstance->mIRFunction.IsFake()))
				{
					BfLogSysM("Deleting declared but uncreated specialized method instance %p\n", methodInstance);

					delete methodInstance;
					methodInstGroup->mMethodSpecializationMap->Remove(lookupMethodGenericArguments);
					methodInstance = NULL;
				}
				else
				{
					BfLogSysM("Reifying declared but unreified method instance %p\n", methodInstance);

					// We haven't processed it yet
					_SetReified();
					CheckHotMethod(methodInstance, "");

					if (methodInstance->mMethodProcessRequest == NULL)
						AddMethodToWorkList(methodInstance);
				}
			}
		}
	}

	if ((methodInstance != NULL) && (!doingRedeclare))
	{
		SetMethodDependency(methodInstance);

		if (methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference)
		{
			/*if ((!mCompiler->mIsResolveOnly) && (!isReified))
				BF_ASSERT(!methodInstance->mIsReified);*/

			if (methodInstance->mIsReified != isReified)
				BfLogSysM("GetMethodInstance %p Decl_AwaitingReference setting reified to %d\n", methodInstance, isReified);

			if ((!isReified) &&
				((methodInstance->mDeclModule == NULL) || (!methodInstance->mDeclModule->mIsModuleMutable)))
			{
				methodInstance->mDeclModule = mContext->mUnreifiedModule;
				methodInstance->mIRFunction = BfIRFunction();
			}
			methodInstance->mIsReified = isReified;

			if ((methodInstance->mHotMethod != NULL) && (!mCompiler->IsHotCompile()) && (isReified))
			{
				methodInstance->mHotMethod->mFlags = (BfHotDepDataFlags)(methodInstance->mHotMethod->mFlags | BfHotDepDataFlag_IsOriginalBuild);
			}

			if (!methodInstance->mMethodDef->mIsAbstract)
			{
				if ((methodInstance->mMethodDef->mMethodType == BfMethodType_Mixin) && (!methodInstance->mDeclModule->mIsModuleMutable))
				{
					// Wait until unreified
				}
				else
					AddMethodToWorkList(methodInstance);
			}
			else
			{
				methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Referenced;
				auto owningModule = methodInstance->GetOwner()->mModule;
				if (!owningModule->mIsScratchModule)
				{
					owningModule->mOnDemandMethodCount--;
					owningModule->VerifyOnDemandMethods();
				}
			}
		}

		if (mExtensionCount > 0)
		{
			if ((mIsModuleMutable) && (!methodInstance->mIRFunction) &&
				((projectList.IsEmpty() || ((flags & BfGetMethodInstanceFlag_ExplicitSpecializedModule) != 0))))
			{
				SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, false);

				if (mAwaitingInitFinish)
					FinishInit();

				// We need to refer to a function that was defined in a prior module
				bool isInlined = (methodInstance->mAlwaysInline) || ((flags & BfGetMethodInstanceFlag_ForceInline) != 0);
				if (methodInstance->mIsIntrinsic)
					isInlined = false;
				methodInstance->mIRFunction = CreateFunctionFrom(methodInstance, false, isInlined);
				BF_ASSERT((methodInstance->mDeclModule == this) || (methodInstance->mDeclModule == mContext->mUnreifiedModule) || (methodInstance->mDeclModule == NULL));
				methodInstance->mDeclModule = this;

				// Add this inlined def to ourselves
				if ((methodInstance->mAlwaysInline) && (HasCompiledOutput()) && (!methodInstance->mIsUnspecialized) && ((flags & BfGetMethodInstanceFlag_NoInline) == 0))
				{
					mIncompleteMethodCount++;
					BfInlineMethodRequest* inlineMethodRequest = mContext->mInlineMethodWorkList.Alloc();
					inlineMethodRequest->mType = typeInst;
					inlineMethodRequest->mFromModule = this;
					inlineMethodRequest->mFunc = methodInstance->mIRFunction;
					inlineMethodRequest->mFromModuleRevision = mRevision;
					inlineMethodRequest->mFromModuleRebuildIdx = mRebuildIdx;
					inlineMethodRequest->mMethodInstance = methodInstance;

					BfLogSysM("mInlineMethodWorkList %p for method %p in module %p in GetMethodInstance\n", inlineMethodRequest, methodInstance, this);
					BF_ASSERT(mIsModuleMutable);
				}
			}
		}

		if (IsSkippingExtraResolveChecks())
			return BfModuleMethodInstance(methodInstance, BfIRFunction());
		else
		{
			if ((flags & (BfGetMethodInstanceFlag_MethodInstanceOnly | BfGetMethodInstanceFlag_NoReference)) != 0)
				return methodInstance;

			if (methodInstance->mDeclModule != this)
				return ReferenceExternalMethodInstance(methodInstance, flags);

			if ((!methodInstance->mIRFunction) && (mIsModuleMutable) && (!mBfIRBuilder->mIgnoreWrites))
			{
				auto importKind = methodInstance->GetImportCallKind();
				if (importKind != BfImportCallKind_None)
				{
					BfLogSysM("DllImportGlobalVar creating %p\n", methodInstance);
					methodInstance->mIRFunction = mBfIRBuilder->GetFakeVal();
					auto func = CreateDllImportGlobalVar(methodInstance, true);
					BF_ASSERT(func);
					mFuncReferences[methodInstance] = func;
				}
			}

			return BfModuleMethodInstance(methodInstance);
		}
	}

	if (!doingRedeclare)
	{
		BfModule* specModule = NULL;
		if ((!keepInCurrentModule) && (projectList.size() > 0) && (!isUnspecializedPass) && (HasCompiledOutput()) && (!mIsScratchModule))
		{
			specModule = GetSpecializedMethodModule(projectList);
		}

		if ((specModule != NULL) && (specModule != this))
		{
			auto specMethodInstance = specModule->GetMethodInstance(typeInst, methodDef, methodGenericArguments, (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ExplicitSpecializedModule));
			if (mAwaitingInitFinish)
				return BfModuleMethodInstance(specMethodInstance.mMethodInstance, BfIRFunction());
			if ((flags & (BfGetMethodInstanceFlag_MethodInstanceOnly | BfGetMethodInstanceFlag_NoReference)) != 0)
				return specMethodInstance.mMethodInstance;
			return ReferenceExternalMethodInstance(specMethodInstance.mMethodInstance, flags);
		}

		if ((!isUnspecializedPass) && (methodGenericArguments.size() != 0) && (methodInstGroup->mDefault == NULL))
		{
			// We are attempting to specialize but we don't have the unspecialized method yet.  Generate that first.
			GetMethodInstance(typeInst, methodDef, BfTypeVector(), BfGetMethodInstanceFlag_UnspecializedPass);
		}
	}

	if (methodInstance == NULL)
	{
		if (!mCompiler->EnsureCeUnpaused(typeInst))
		{
			BfLogSysM("GetMethodInstance (methodInstance == NULL) bailing due to IsCePaused\n");
			return BfModuleMethodInstance();
		}

		if (lookupMethodGenericArguments.size() == 0)
		{
			BF_ASSERT(methodInstGroup->mDefault == NULL);
			methodInstance = new BfMethodInstance();
			methodInstGroup->mDefault = methodInstance;

			BF_ASSERT(typeInst->mDefineState > BfTypeDefineState_Declared);

			BfLogSysM("Created Default MethodInst: %p TypeInst: %p Group: %p\n", methodInstance, typeInst, methodInstGroup);
		}
		else
		{
			bool depthExceeded = ((flags & BfGetMethodInstanceFlag_DepthExceeded) != 0);

			if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mMinDependDepth >= 32))
				depthExceeded = true;

			if (depthExceeded)
			{
				Fail("Generic method dependency depth exceeded", methodDef->GetRefNode());
				return BfModuleMethodInstance();
			}

			BfMethodInstance** methodInstancePtr = NULL;
			bool added = methodInstGroup->mMethodSpecializationMap->TryAdd(lookupMethodGenericArguments, NULL, &methodInstancePtr);
			BF_ASSERT(added);
			methodInstance = new BfMethodInstance();
			*methodInstancePtr = methodInstance;

			if (mCompiler->IsAutocomplete())
				methodInstance->mRequestedByAutocomplete = true;

			BfLogSysM("Created Specialized MethodInst: %p TypeInst: %p\n", methodInstance, typeInst);
		}

		if ((prevIRFunc) && (!prevIRFunc.IsFake()))
			methodInstance->mIRFunction = prevIRFunc; // Take it over
	}

	methodInstance->mMethodDef = methodDef;
	methodInstance->mAlwaysInline = methodDef->mAlwaysInline;
	methodInstance->mMethodInstanceGroup = methodInstGroup;
	methodInstance->mIsReified = isReified;

	SetupMethodIdHash(methodInstance);

	if (hadConcreteInterfaceGenericArgument)
		methodInstance->mIgnoreBody = true;
	if ((flags & BfGetMethodInstanceFlag_ForeignMethodDef) != 0)
	{
		methodInstance->mIsForeignMethodDef = true;
		BF_ASSERT(foreignType != NULL);
		methodInstance->GetMethodInfoEx()->mForeignType = foreignType;
	}

	if ((typeInst->IsInstanceOf(mCompiler->mValueTypeTypeDef)) && (methodDef->mName == BF_METHODNAME_EQUALS))
	{
		if (!lookupMethodGenericArguments.empty())
		{
			auto compareType = lookupMethodGenericArguments[0];
			PopulateType(compareType, BfPopulateType_Data);
			if (compareType->IsSplattable())
				methodInstance->mAlwaysInline = true;
		}
	}

	if (methodDef->mMethodType == BfMethodType_Init)
	{
		methodInstance->mMangleWithIdx = true;
	}

	BF_ASSERT(typeInst == methodInstance->GetOwner());

	auto methodDeclaration = methodDef->GetMethodDeclaration();
	if (isUnspecializedPass)
	{
		for (int genericParamIdx = 0; genericParamIdx < (int) methodDef->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParamType = GetGenericParamType(BfGenericParamKind_Method, genericParamIdx);
			methodInstance->GetMethodInfoEx()->mMethodGenericArguments.Add(genericParamType);
		}
	}
	else if ((methodDeclaration != NULL) && (methodDeclaration->mGenericParams != NULL))
	{
		if (!sanitizedMethodGenericArguments.IsEmpty())
			methodInstance->GetMethodInfoEx()->mMethodGenericArguments = sanitizedMethodGenericArguments;
		if (methodDef->mGenericParams.size() != sanitizedMethodGenericArguments.size())
		{
			Fail("Internal error");
			BFMODULE_FATAL(this, "Generic method argument counts mismatch");
			return NULL;
		}

		for (auto genericArg : sanitizedMethodGenericArguments)
		{
			if (genericArg->IsPrimitiveType())
				genericArg = GetWrappedStructType(genericArg);
			if (genericArg != NULL)
				AddDependency(genericArg, typeInst, BfDependencyMap::DependencyFlag_MethodGenericArg);
		}
	}

	// Generic constraints
	if (!methodDef->mGenericParams.IsEmpty())
	{
		BF_ASSERT(methodInstance->GetMethodInfoEx()->mGenericParams.IsEmpty());
		for (int genericParamIdx = 0; genericParamIdx < (int)methodDef->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, genericParamIdx);
			methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
		}
	}

	for (int externConstraintIdx = 0; externConstraintIdx < (int)methodDef->mExternalConstraints.size(); externConstraintIdx++)
	{
		auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, externConstraintIdx + (int)methodDef->mGenericParams.size());
		methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
	}

	bool addToWorkList = !processNow;
	if (mCompiler->GetAutoComplete() != NULL)
	{
		if (typeInst->IsSpecializedByAutoCompleteMethod())
			addToWorkList = false;
		if (methodInstance->mRequestedByAutocomplete)
			addToWorkList = false;
	}

	BfModule* declareModule;
	//
	{
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);
		declareModule = GetOrCreateMethodModule(methodInstance);
	}
	if ((doingRedeclare) && (methodInstance->mDeclModule != mContext->mUnreifiedModule))
		declareModule = methodInstance->mDeclModule;

	BF_ASSERT(declareModule != NULL);
	methodInstance->mDeclModule = declareModule;

	if ((!methodInstance->mIsReified) && (mCompiler->mCompileState != BfCompiler::CompileState_Normal))
	{
		// We can be sure this method won't become reified later. Normally we can "go either way" with an unreified method
		//  of a reified module -
		// If we declare it in the reified module then we can switch it to "reified" before actual processing without any extra work,
		//  BUT if we don't reify it then we have to remove the body after processing.
		// But if we declare it in the unreified module module and then end up needing to reify it then we need to re-declare it in
		//  the proper reified module.
		declareModule = mContext->mUnreifiedModule;
	}
	else if ((!declareModule->mIsModuleMutable) && (!mCompiler->mIsResolveOnly))
	{
		if (!methodInstance->mIsReified)
		{
			declareModule = mContext->mUnreifiedModule;
		}
		else
		{
			declareModule->PrepareForIRWriting(methodInstance->GetOwner());
		}
	}

	SetMethodDependency(methodInstance);

	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(declareModule->mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(declareModule->mCurTypeInstance, typeInst);
	SetAndRestoreValue<BfFilePosition> prevFilePos(declareModule->mCurFilePosition);

	if ((methodDef->mMethodType == BfMethodType_Mixin) && (methodDef->mGenericParams.size() != 0) && (!isUnspecializedPass))
	{
		// For mixins we only process the unspecialized version
		addToWorkList = false;
	}

	if (((flags & BfGetMethodInstanceFlag_MethodInstanceOnly) != 0) && (methodInstGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude))
	{
		addToWorkList = false;
	}

// 	if ((flags & BfGetMethodInstanceFlag_NoReference) != 0)
// 		addToWorkList = false;

	declareModule->DoMethodDeclaration(methodDef->GetMethodDeclaration(), false, addToWorkList);

	if (processNow)
	{
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);
		ProcessMethod(methodInstance);
	}

	if (IsSkippingExtraResolveChecks())
		return BfModuleMethodInstance(methodInstance, BfIRFunction());

	if (methodInstance->mDeclModule != this)
		return ReferenceExternalMethodInstance(methodInstance, flags);

	return BfModuleMethodInstance(methodInstance);
}

BfModuleMethodInstance BfModule::GetMethodInstance(BfMethodInstance* methodInstance, BfGetMethodInstanceFlags flags)
{
	BF_ASSERT((methodInstance->mMethodInfoEx == NULL) || (methodInstance->mMethodInfoEx->mGenericParams.IsEmpty()));
	if (methodInstance->mIsForeignMethodDef)
		flags = (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ForeignMethodDef);
	return GetMethodInstance(methodInstance->GetOwner(), methodInstance->mMethodDef, BfTypeVector(), flags);
}

BfMethodInstance* BfModule::GetOuterMethodInstance(BfMethodInstance* methodInstance)
{
	if (!methodInstance->mMethodDef->mIsLocalMethod)
		return NULL;
	auto outerLocal = methodInstance->mMethodInfoEx->mClosureInstanceInfo->mLocalMethod->mOuterLocalMethod;
	if (outerLocal == NULL)
		return NULL;

	BfTypeVector genericArgs;
	for (int i = 0; i < (int)outerLocal->mMethodDef->mGenericParams.size(); i++)
		genericArgs.Add(methodInstance->mMethodInfoEx->mMethodGenericArguments[i]);
	return GetLocalMethodInstance(outerLocal, genericArgs).mMethodInstance;
}

void BfModule::SetupMethodIdHash(BfMethodInstance* methodInstance)
{
	HashContext hashCtx;

	std::function<void(BfMethodInstance*)> _MixinMethodInstance = [&](BfMethodInstance* methodInstance)
	{
		hashCtx.Mixin(methodInstance->GetOwner()->mTypeId);
		hashCtx.Mixin(methodInstance->mMethodDef->mIdx);

		if (methodInstance->GetNumGenericArguments() != 0)
		{
			hashCtx.Mixin((int32)methodInstance->mMethodInfoEx->mMethodGenericArguments.size());
			for (auto methodGenericArg : methodInstance->mMethodInfoEx->mMethodGenericArguments)
				hashCtx.Mixin(methodGenericArg->mTypeId);
		}
	};

	_MixinMethodInstance(methodInstance);

	if (methodInstance->mMethodDef->mIsLocalMethod)
	{
		auto outmostMethodInstance = mCurMethodState->GetRootMethodState()->mMethodInstance;
		if (outmostMethodInstance != NULL)
		{
			BF_ASSERT((outmostMethodInstance->mIdHash != 0) || (outmostMethodInstance->mIsAutocompleteMethod));
			hashCtx.Mixin(outmostMethodInstance->mIdHash);
		}
	}

	methodInstance->mIdHash = (int64)hashCtx.Finish64();
}

bool BfModule::CheckUseMethodInstance(BfMethodInstance* methodInstance, BfAstNode* refNode)
{
	if (methodInstance->mHasBeenDeclared)
		return true;
	Fail(StrFormat("Circular reference in method instance '%s'", MethodToString(methodInstance).c_str()), refNode);
	return false;
}

BfIRValue BfModule::GetInterfaceSlotNum(BfTypeInstance* ifaceType)
{
	BfIRValue globalValue;

	BfIRValue* globalValuePtr = NULL;
	if (mInterfaceSlotRefs.TryGetValue(ifaceType, &globalValuePtr))
	{
		globalValue = *globalValuePtr;
	}

	if (!globalValue)
	{
		// This is necessary to reify the interface type
		PopulateType(ifaceType);

		StringT<512> slotVarName;
		BfMangler::MangleStaticFieldName(slotVarName, mCompiler->GetMangleKind(), ifaceType, "sBfSlotOfs");
		BfType* intType = GetPrimitiveType(BfTypeCode_Int32);
		BfIRValue value;

		if ((mCompiler->mHotState != NULL) && (ifaceType->mSlotNum >= 0))
		{
			/*// If this interface already existed before this hot compile but we never indirectly called any of its methods
			//  then the sBfSlotOfs could have gotten its symbol stripped, so actually define it here.
			int virtSlotIdx = ifaceType->mSlotNum + 1;
			value = GetConstValue32(virtSlotIdx);*/
			mCompiler->mHotState->mSlotDefineTypeIds.Add(ifaceType->mTypeId);
			ifaceType->mDirty = true; // Makes sure we create interface in vdata
		}

		globalValue = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(intType), true, BfIRLinkageType_External, value, slotVarName);
		mInterfaceSlotRefs[ifaceType] = globalValue;
	}

	return mBfIRBuilder->CreateAlignedLoad(globalValue/*, "slotOfs"*/, 4);
}

void BfModule::HadSlotCountDependency()
{
	if (mCompiler->mIsResolveOnly)
		return;
	BF_ASSERT(!mBfIRBuilder->mIgnoreWrites);
	BF_ASSERT((mUsedSlotCount == BF_MAX(mCompiler->mMaxInterfaceSlots, 0)) || (mUsedSlotCount == -1));
	mUsedSlotCount = BF_MAX(mCompiler->mMaxInterfaceSlots, 0);
}

BfTypedValue BfModule::GetCompilerFieldValue(const StringImpl& str)
{
	if (str == "#IsComptime")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, mIsComptimeModule ? 1 : 0), GetPrimitiveType(BfTypeCode_Boolean));
	}
	if (str == "#IsBuilding")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, (!mCompiler->mIsResolveOnly) ? 1 : 0), GetPrimitiveType(BfTypeCode_Boolean));
	}
	if (str == "#IsReified")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, mIsReified ? 1 : 0), GetPrimitiveType(BfTypeCode_Boolean));
	}
	if (str == "#CompileRev")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Int32, mCompiler->mRevision), GetPrimitiveType(BfTypeCode_Int32));
	}
	if (str == "#NextId")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Int64, (uint64)++mCompiler->mUniqueId), GetPrimitiveType(BfTypeCode_Int32));
	}
	if (str == "#ModuleName")
	{
		return BfTypedValue(GetStringObjectValue(mModuleName), ResolveTypeDef(mCompiler->mStringTypeDef));
	}
	if (str == "#ProjectName")
	{
		if (mProject != NULL)
			return BfTypedValue(GetStringObjectValue(mProject->mName), ResolveTypeDef(mCompiler->mStringTypeDef));
	}
	if (str == "#AllocStackCount")
	{
		return BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Int32, mCompiler->mOptions.mAllocStackCount), GetPrimitiveType(BfTypeCode_Int32));
	}

	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		if (str == "#CallerLineNum")
		{
			return BfTypedValue(GetConstValue(mCurMethodState->mMixinState->mInjectFilePosition.mCurLine + 1), GetPrimitiveType(BfTypeCode_Int32));
		}
		else if (str == "#CallerFilePath")
		{
			String filePath = "";
			if (mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance != NULL)
				filePath = mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance->mParser->mFileName;
			return BfTypedValue(GetStringObjectValue(filePath), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
		else if (str == "#CallerFileName")
		{
			String filePath = "";
			if (mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance != NULL)
				filePath = mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance->mParser->mFileName;
			return BfTypedValue(GetStringObjectValue(GetFileName(filePath)), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
		else if (str == "#CallerFileDir")
		{
			String filePath = "";
			if (mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance != NULL)
				filePath = mCurMethodState->mMixinState->mInjectFilePosition.mFileInstance->mParser->mFileName;
			return BfTypedValue(GetStringObjectValue(GetFileDir(filePath)), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
		else if (str == "#CallerTypeName")
		{
			String typeName = "";
			if (mCurMethodState->mMixinState->mMixinMethodInstance)
				typeName = TypeToString(mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner());
			return BfTypedValue(GetStringObjectValue(typeName), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
		else if (str == "#CallerType")
		{
			auto typeType = ResolveTypeDef(mCompiler->mTypeTypeDef);
			BfType* type = NULL;
			if (mCurMethodState->mMixinState->mMixinMethodInstance)
				type = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
			if (type != NULL)
			{
				AddDependency(type, mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);
				return BfTypedValue(CreateTypeDataRef(type), typeType);
			}
		}
		else if (str == "#CallerMemberName")
		{
 			String memberName = "";
 			if (mCurMethodState->mMixinState->mMixinMethodInstance)
 				memberName = MethodToString(mCurMethodState->mMixinState->mMixinMethodInstance);
			return BfTypedValue(GetStringObjectValue(memberName), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
		else if (str == "#CallerProject")
		{
			BfProject* project = NULL;
			project = mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType->mProject;
			if (project != NULL)
				return BfTypedValue(GetStringObjectValue(mProject->mName), ResolveTypeDef(mCompiler->mStringTypeDef));
		}
	}

	if (str == "#TimeLocal")
	{
		time_t rawtime;
		time(&rawtime);
		tm* timeinfo = localtime(&rawtime);
		char result[32];
		sprintf(result, "%d/%.2d/%.2d %.2d:%.2d:%.2d",
			1900 + timeinfo->tm_year,
			timeinfo->tm_mon,
			timeinfo->tm_mday,
			timeinfo->tm_hour,
			timeinfo->tm_min,
			timeinfo->tm_sec);
		return BfTypedValue(GetStringObjectValue(result), ResolveTypeDef(mCompiler->mStringTypeDef));
	}
	return BfTypedValue();
}

BfTypedValue BfModule::GetCompilerFieldValue(BfTypedValue typedValue)
{
	if (!typedValue.IsAddr())
		return BfTypedValue();

	if (typedValue.mValue.IsConst())
	{
		auto constantValue = mBfIRBuilder->GetConstant(typedValue.mValue);
		if (constantValue != NULL)
		{
			if (constantValue->mConstType == BfConstType_GlobalVar)
			{
				auto globalVar = (BfGlobalVar*)constantValue;
				if (globalVar->mName[0] == '#')
				{
					BfTypedValue result = GetCompilerFieldValue(globalVar->mName);
					return result;
				}
			}
		}
	}

	return BfTypedValue();
}

BfTypedValue BfModule::ReferenceStaticField(BfFieldInstance* fieldInstance)
{
	BfIRValue globalValue;

	auto fieldDef = fieldInstance->GetFieldDef();

	if ((fieldDef->mIsConst) && (!fieldDef->mIsExtern))
	{
		if (fieldInstance->mConstIdx != -1)
		{
			auto constant = fieldInstance->mOwner->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
			return BfTypedValue(ConstantToCurrent(constant, fieldInstance->mOwner->mConstHolder, fieldInstance->GetResolvedType()), fieldInstance->GetResolvedType());
		}
		else
		{
			return GetDefaultTypedValue(fieldInstance->GetResolvedType());
		}
	}

	if ((mIsScratchModule) && (mCompiler->mIsResolveOnly) && (!fieldInstance->mOwner->IsInstanceOf(mCompiler->mCompilerTypeDef)))
	{
		// Just fake it for the extern and unspecialized modules
		// We can't do this for compilation because unreified methods with default params need to get actual global variable refs
		return BfTypedValue(mBfIRBuilder->CreateConstNull(), fieldInstance->GetResolvedType(), true);
	}

	BfIRValue* globalValuePtr = NULL;
	if (mStaticFieldRefs.TryGetValue(fieldInstance, &globalValuePtr))
	{
		globalValue = *globalValuePtr;
		BF_ASSERT(globalValue);

		auto globalVar = (BfGlobalVar*)mBfIRBuilder->GetConstant(globalValue);
		if ((globalVar->mStreamId == -1) && (!mBfIRBuilder->mIgnoreWrites))
		{
			mBfIRBuilder->MapType(fieldInstance->mResolvedType);
			mBfIRBuilder->CreateGlobalVariable(globalValue);
		}
	}
	else
	{
		StringT<512> staticVarName;
		BfMangler::Mangle(staticVarName, mCompiler->GetMangleKind(), fieldInstance);

		auto typeType = fieldInstance->GetResolvedType();
		if ((fieldDef->mIsExtern) && (fieldDef->mIsConst) && (typeType->IsPointer()))
		{
			typeType = typeType->GetUnderlyingType();
		}

		if (mIsComptimeModule)
		{
			mCompiler->mCeMachine->QueueStaticField(fieldInstance, staticVarName);
		}

		PopulateType(typeType);
		if ((typeType != NULL) && (!typeType->IsValuelessType()))
		{
			BfIRType irType = mBfIRBuilder->MapType(typeType);

			if (fieldInstance->IsAppendedObject())
				irType = mBfIRBuilder->MapTypeInst(typeType->ToTypeInstance());

			SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mBfIRBuilder->mIgnoreWrites || staticVarName.StartsWith('#'));

			globalValue = mBfIRBuilder->CreateGlobalVariable(
				irType,
				false,
				BfIRLinkageType_External,
				BfIRValue(),
				staticVarName,
				IsThreadLocal(fieldInstance));

			BF_ASSERT(globalValue);
			mStaticFieldRefs[fieldInstance] = globalValue;

			BfLogSysM("Mod:%p Type:%p ReferenceStaticField %p -> %p\n", this, fieldInstance->mOwner, fieldInstance, globalValue);
		}
	}

	auto type = fieldInstance->GetResolvedType();
	if (type->IsValuelessType())
		return BfTypedValue(globalValue, type);

	if (fieldDef->mIsVolatile)
		return BfTypedValue(globalValue, type, BfTypedValueKind_VolatileAddr);
	return BfTypedValue(globalValue, type, !fieldDef->mIsConst && !fieldInstance->IsAppendedObject());
}

BfFieldInstance* BfModule::GetFieldInstance(BfTypeInstance* typeInst, int fieldIdx, const char* fieldName)
{
	if (typeInst->IsDataIncomplete())
		PopulateType(typeInst);
	if (fieldIdx >= typeInst->mFieldInstances.mSize)
	{
		Fail(StrFormat("Invalid field data in type '%s'", TypeToString(typeInst).c_str()));
		return 0;
	}
	return &typeInst->mFieldInstances[fieldIdx];
}

void BfModule::MarkUsingThis()
{
	auto useMethodState = mCurMethodState;
	while ((useMethodState != NULL) && (useMethodState->mClosureState != NULL) && (useMethodState->mClosureState->mCapturing))
	{
		useMethodState = useMethodState->mPrevMethodState;
	}

	if ((useMethodState != NULL) && (!useMethodState->mLocals.IsEmpty()))
	{
		auto localVar = useMethodState->mLocals[0];
		if (localVar->mIsThis)
			localVar->mReadFromId = useMethodState->GetRootMethodState()->mCurAccessId++;
	}
}

BfTypedValue BfModule::GetThis(bool markUsing)
{
	if ((mIsComptimeModule) && (mCompiler->mCeMachine->mDebugger != NULL) && (mCompiler->mCeMachine->mDebugger->mCurDbgState != NULL))
	{
		if (mCompiler->mCeMachine->mDebugger->mCurDbgState->mExplicitThis)
			return mCompiler->mCeMachine->mDebugger->mCurDbgState->mExplicitThis;

		auto ceDebugger = mCompiler->mCeMachine->mDebugger;
		auto activeFrame = ceDebugger->mCurDbgState->mActiveFrame;
		if ((activeFrame != NULL) && (activeFrame->mFunction->mDbgInfo != NULL))
		{
			for (auto& dbgVar : activeFrame->mFunction->mDbgInfo->mVariables)
			{
				if (dbgVar.mName == "this")
					return BfTypedValue(mBfIRBuilder->CreateConstAggCE(mBfIRBuilder->MapType(dbgVar.mType), activeFrame->mFrameAddr + dbgVar.mValue.mFrameOfs), dbgVar.mType,
						dbgVar.mIsConst ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr);
			}
		}

		return BfTypedValue();
	}

	auto useMethodState = mCurMethodState;
	while ((useMethodState != NULL) && (useMethodState->mClosureState != NULL) && (useMethodState->mClosureState->mCapturing))
	{
		useMethodState = useMethodState->mPrevMethodState;
	}

	if (useMethodState != NULL)
	{
		// Fake a 'this' for var field resolution
		if (useMethodState->mTempKind == BfMethodState::TempKind_NonStatic)
		{
			auto thisType = mCurTypeInstance;
			if (thisType->IsValueType())
				return BfTypedValue(mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapTypeInstPtr(thisType)), thisType, BfTypedValueKind_ThisAddr);
			else
				return BfTypedValue(mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapTypeInst(thisType)), thisType, BfTypedValueKind_ThisValue);
		}
		else if (useMethodState->mTempKind == BfMethodState::TempKind_Static)
		{
			return BfTypedValue();
		}
	}
	else
	{
		//TODO: Do we allow useMethodState to be NULL anymore?
		return BfTypedValue();
	}

	// Check mixin state for 'this'
	{
		auto checkMethodState = mCurMethodState;
		while (checkMethodState != NULL)
		{
			if (checkMethodState->mMixinState != NULL)
			{
				BfTypedValue thisValue = checkMethodState->mMixinState->mTarget;
				if (thisValue.HasType())
				{
					checkMethodState->mMixinState->mLastTargetAccessId = useMethodState->GetRootMethodState()->mCurAccessId++;
					if (!thisValue.mType->IsValueType())
						thisValue = LoadValue(thisValue);
					return thisValue;
				}
			}
			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	auto curMethodInstance = mCurMethodInstance;
	if (useMethodState->mMethodInstance != NULL)
		curMethodInstance = useMethodState->mMethodInstance;

	if ((curMethodInstance == NULL) /*|| (!mCurMethodInstance->mIRFunction)*/ || (curMethodInstance->mMethodDef->mIsStatic))
	{
		if ((useMethodState->mClosureState != NULL) && (!useMethodState->mClosureState->mConstLocals.empty()))
		{
			auto& constLocal = useMethodState->mClosureState->mConstLocals[0];
			if (constLocal.mIsThis)
			{
				BF_ASSERT(constLocal.mResolvedType->IsValuelessType());
				return BfTypedValue(mBfIRBuilder->GetFakeVal(), constLocal.mResolvedType);
			}
		}

		return BfTypedValue();
	}

	if (useMethodState->mLocals.IsEmpty())
	{
		// This can happen in rare non-capture cases, such as when we need to do a const expression resolve for a sized-array return type on a local method
		BF_ASSERT(useMethodState->mPrevMethodState != NULL);
		return BfTypedValue();
	}

	auto thisLocal = useMethodState->mLocals[0];
	BF_ASSERT(thisLocal->mIsThis);

	bool preferValue = !IsTargetingBeefBackend();
	if (!thisLocal->mAddr)
		preferValue = true;

	bool usedVal = false;
	BfIRValue thisValue;
	if ((preferValue) && (!thisLocal->mIsLowered))
	{
		thisValue = thisLocal->mValue;
		usedVal = true;
	}
	else if ((thisLocal->mIsSplat) || (thisLocal->mIsLowered))
		thisValue = thisLocal->mAddr;
	else
		thisValue = mBfIRBuilder->CreateAlignedLoad(thisLocal->mAddr, thisLocal->mResolvedType->mAlign);
	if (markUsing)
		useMethodState->mLocals[0]->mReadFromId = useMethodState->GetRootMethodState()->mCurAccessId++;

	if (useMethodState->mClosureState != NULL)
	{
		auto closureTypeInst = useMethodState->mClosureState->mClosureType;
		if (closureTypeInst != NULL)
		{
			if (closureTypeInst->mFieldInstances.size() > 0)
			{
				auto& field = closureTypeInst->mFieldInstances[0];
				auto fieldDef = field.GetFieldDef();
				if (fieldDef->mName == "__this")
				{
					if (mCurMethodState->mClosureState->mCapturing)
						mCurMethodState->mClosureState->mReferencedOuterClosureMembers.Add(&field);

					// This is a captured 'this'
					BfTypedValue result = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(thisValue, 0, field.mDataIdx), field.mResolvedType, true);
					if (field.mResolvedType->IsRef())
					{
						auto refType = (BfRefType*)field.mResolvedType;
						auto underlyingType = refType->GetUnderlyingType();
						result = BfTypedValue(mBfIRBuilder->CreateLoad(result.mValue), underlyingType, true);
					}
					if (field.mResolvedType->IsObject())
					{
						result = LoadValue(result);
						result.mKind = BfTypedValueKind_ThisValue;
					}
					else
					{
						if (fieldDef->mIsReadOnly)
							result.mKind = BfTypedValueKind_ReadOnlyThisAddr;
						else
							result.mKind = BfTypedValueKind_ThisAddr;
					}
					return result;
				}
			}

			return BfTypedValue();
		}

		if (useMethodState->mClosureState->mCapturing)
		{
			auto thisType = useMethodState->mLocals[0]->mResolvedType;
			if (thisType->IsValueType())
				return BfTypedValue(CreateAlloca(thisType), thisType, BfTypedValueKind_ThisAddr);
			else
				return BfTypedValue(GetDefaultValue(thisType), thisType, BfTypedValueKind_ThisValue);
		}
	}

	if (mCurMethodInstance == NULL)
		return BfTypedValue();

	auto localDef = useMethodState->mLocals[0];
	auto curMethodOwner = mCurMethodInstance->mMethodInstanceGroup->mOwner;
	if ((curMethodOwner->IsStruct()) || (curMethodOwner->IsTypedPrimitive()))
	{
		if ((localDef->mResolvedType->IsTypedPrimitive()) && (!mCurMethodInstance->mMethodDef->mIsMutating))
		{
			return BfTypedValue(thisValue, useMethodState->mLocals[0]->mResolvedType, BfTypedValueKind_ReadOnlyThisValue);
		}
		if (localDef->mIsSplat)
		{
			return BfTypedValue(thisValue, useMethodState->mLocals[0]->mResolvedType, BfTypedValueKind_ThisSplatHead);
		}
		return BfTypedValue(thisValue, useMethodState->mLocals[0]->mResolvedType, localDef->mIsReadOnly ? BfTypedValueKind_ReadOnlyThisAddr : BfTypedValueKind_ThisAddr);
	}
	return BfTypedValue(thisValue, mCurMethodInstance->mMethodInstanceGroup->mOwner, BfTypedValueKind_ThisValue);
}

BfLocalVariable* BfModule::GetThisVariable()
{
	if (mCurMethodState == NULL)
		return NULL;
	auto methodState = mCurMethodState->GetNonCaptureState();
	if ((methodState->mLocals.size() > 0) && (methodState->mLocals[0]->mIsThis))
		return methodState->mLocals[0];
	return NULL;
}

bool BfModule::IsInGeneric()
{
	return ((mCurMethodInstance != NULL) && (mCurMethodInstance->GetNumGenericArguments() != 0)) || (mCurTypeInstance->IsGenericTypeInstance());
}

bool BfModule::InDefinitionSection()
{
	if (mCurTypeInstance != NULL)
	{
		if (mCurTypeInstance->IsUnspecializedTypeVariation())
			return false;
	}
	return !IsInSpecializedSection();
}

bool BfModule::IsInSpecializedGeneric()
{
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsSpecializedType()))
		return true;
	if ((mCurMethodInstance == NULL) || (mCurMethodInstance->mIsUnspecialized))
		return false;
	return (mCurMethodInstance->GetNumGenericArguments() != 0);
}

bool BfModule::IsInSpecializedSection()
{
	return IsInSpecializedGeneric() ||
		((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL));
}

bool BfModule::IsInUnspecializedGeneric()
{
	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecialized))
		return true;
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsUnspecializedType()))
		return true;
	return false;
}

//////////////////////////////////////////////////////////////////////////

BfIRValue BfModule::AllocLocalVariable(BfType* type, const StringImpl& name, bool doLifetimeEnd)
{
	//if ((type->IsValuelessType()) || (type->IsMethodRef()))
	if (type->IsValuelessType())
		return mBfIRBuilder->GetFakeVal();

	auto allocaInst = CreateAlloca(type, doLifetimeEnd, name.c_str());
	if ((!doLifetimeEnd) && (WantsLifetimes()))
	{
		auto lifetimeStart = mBfIRBuilder->CreateLifetimeStart(allocaInst);
		mBfIRBuilder->ClearDebugLocation(lifetimeStart);
	}

	bool initLocalVariables = mCompiler->mOptions.mInitLocalVariables;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		initLocalVariables = typeOptions->Apply(initLocalVariables, BfOptionFlags_InitLocalVariables);
	// Local variable inits are implicitly handled in the Beef Backend
	if ((initLocalVariables) && (!IsTargetingBeefBackend()))
	{
		auto prevBlock = mBfIRBuilder->GetInsertBlock();
		mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
		auto storeInst = mBfIRBuilder->CreateAlignedStore(GetDefaultValue(type), allocaInst, type->mAlign);
		mBfIRBuilder->ClearDebugLocation(storeInst);
		mBfIRBuilder->SetInsertPoint(prevBlock);
	}
	return allocaInst;
}

void BfModule::DoAddLocalVariable(BfLocalVariable* localVar)
{
	while (localVar->mName.StartsWith('@'))
	{
		localVar->mNamePrefixCount++;
		localVar->mName.Remove(0);
	}

	if (mCurMethodState->mLocals.mAllocSize == 0)
	{
		mCurMethodState->mLocals.Reserve(16);
		mCurMethodState->mLocalVarSet.Reserve(16);
	}

	localVar->mLocalVarIdx = (int)mCurMethodState->mLocals.size();
	mCurMethodState->mLocals.push_back(localVar);

	BfLocalVarEntry* localVarEntryPtr;
	if (!mCurMethodState->mLocalVarSet.TryAdd(BfLocalVarEntry(localVar), &localVarEntryPtr))
	{
		localVar->mShadowedLocal = localVarEntryPtr->mLocalVar;
		localVarEntryPtr->mLocalVar = localVar;
	}
}

void BfModule::DoLocalVariableDebugInfo(BfLocalVariable* localVarDef, bool doAliasValue, BfIRValue declareBefore, BfIRInitType initType)
{
	if (localVarDef->mResolvedType->IsVar())
		return;

	if ((mBfIRBuilder->DbgHasInfo()) &&
		((mCurMethodInstance == NULL) || (!mCurMethodInstance->mIsUnspecialized)) &&
		(mHasFullDebugInfo) &&
		((mCurMethodState->mClosureState == NULL) || (!mCurMethodState->mClosureState->mCapturing)))
	{
		auto varType = localVarDef->mResolvedType;

		if (localVarDef->mResolvedType->IsValuelessType())
		{
		}
		else
		{
			bool isByAddr = false;
			auto diValue = localVarDef->mValue;
			if (localVarDef->mConstValue)
				diValue = localVarDef->mConstValue;
			else if (localVarDef->mAddr)
			{
				diValue = localVarDef->mAddr;
				isByAddr = true;
			}

			if (diValue.IsConst())
			{
				auto constant = mBfIRBuilder->GetConstant(diValue);
				if ((constant->mConstType == BfConstType_TypeOf) || (constant->mConstType == BfConstType_TypeOf_WithData))
				{
					// Ignore for now
					return;
				}
			}

			auto diType = mBfIRBuilder->DbgGetType(localVarDef->mResolvedType);
			bool didConstToMem = false;

			bool isConstant = false;

			if ((localVarDef->mIsReadOnly) && (localVarDef->mAddr))
				diType = mBfIRBuilder->DbgCreateConstType(diType);
			else if (localVarDef->mConstValue)
			{
				auto constant = mBfIRBuilder->GetConstant(localVarDef->mConstValue);
				isConstant =
					(constant->mConstType < BfConstType_GlobalVar) ||
					(constant->mConstType == BfConstType_AggZero) ||
					(constant->mConstType == BfConstType_Agg);
				if (isConstant)
				{
					if (localVarDef->mResolvedType->IsComposite())
					{
						mBfIRBuilder->PopulateType(localVarDef->mResolvedType);
						auto constMem = mBfIRBuilder->ConstToMemory(localVarDef->mConstValue);

// 						if (IsTargetingBeefBackend())
// 						{
// 							diValue = mBfIRBuilder->CreateAliasValue(constMem);
// 							didConstToMem = true;
//
// 							diType = mBfIRBuilder->DbgCreateReferenceType(diType);
// 						}
						//else
						{
							isByAddr = true;
							mBfIRBuilder->SaveDebugLocation();
							auto prevBlock = mBfIRBuilder->GetInsertBlock();
							mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);
							mBfIRBuilder->ClearDebugLocation();

							auto ptrType = CreatePointerType(localVarDef->mResolvedType);
							auto addrVal = CreateAlloca(ptrType);
							mBfIRBuilder->CreateStore(constMem, addrVal);

							diType = mBfIRBuilder->DbgCreateReferenceType(diType);
							diValue = addrVal;
							didConstToMem = true;

							mBfIRBuilder->SetInsertPoint(prevBlock);
							mBfIRBuilder->RestoreDebugLocation();
						}
					}

					if (mCompiler->mOptions.IsCodeView())
						diType = mBfIRBuilder->DbgCreateConstType(diType);
				}
			}

			if (!mBfIRBuilder->mIgnoreWrites)
			{
				if ((localVarDef->mIsStatic) && (localVarDef->mAddr) && (!localVarDef->mResolvedType->IsValuelessType()))
				{
					auto refType = CreateRefType(localVarDef->mResolvedType);
					diType = mBfIRBuilder->DbgGetType(refType);

					auto refAlloca = CreateAlloca(refType);
					mBfIRBuilder->CreateStore(localVarDef->mAddr, refAlloca);
					diValue = refAlloca;
				}

				auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
					localVarDef->mName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType, initType);
				localVarDef->mDbgVarInst = diVariable;

				if (mBfIRBuilder->HasDebugLocation())
				{
					if ((isConstant) && (!didConstToMem))
					{
						BfTypedValue result(localVarDef->mConstValue, localVarDef->mResolvedType);
						FixValueActualization(result);
						localVarDef->mDbgDeclareInst = mBfIRBuilder->DbgInsertValueIntrinsic(result.mValue, diVariable);
					}
					else
					{
						if ((IsTargetingBeefBackend()) && (doAliasValue) && (!mIsComptimeModule))
						{
							diValue = mBfIRBuilder->CreateAliasValue(diValue);
							mCurMethodState->mCurScope->mDeferredLifetimeEnds.Add(diValue);
						}

						if (isByAddr)
							localVarDef->mDbgDeclareInst = mBfIRBuilder->DbgInsertDeclare(diValue, diVariable, declareBefore);
						else if (diValue)
						{
							localVarDef->mDbgDeclareInst = mBfIRBuilder->DbgInsertValueIntrinsic(diValue, diVariable);
						}
						else if (mCompiler->mOptions.mToolsetType != BfToolsetType_GNU) // DWARF chokes on this:
							localVarDef->mDbgDeclareInst = mBfIRBuilder->DbgInsertValueIntrinsic(BfIRValue(), diVariable);
					}
				}
			}
		}
	}
}

BfLocalVariable* BfModule::AddLocalVariableDef(BfLocalVariable* localVarDef, bool addDebugInfo, bool doAliasValue, BfIRValue declareBefore, BfIRInitType initType)
{
	if ((localVarDef->mValue) && (!localVarDef->mAddr) && (IsTargetingBeefBackend()) && (!localVarDef->mResolvedType->IsValuelessType()))
	{
		if ((!localVarDef->mValue.IsConst()) &&
			(!localVarDef->mValue.IsArg()) && (!localVarDef->mValue.IsFake()))
		{
			mBfIRBuilder->CreateValueScopeRetain(localVarDef->mValue);
			mCurMethodState->mCurScope->mHadScopeValueRetain = true;
		}
	}

	if (addDebugInfo)
		DoLocalVariableDebugInfo(localVarDef, doAliasValue, declareBefore, initType);

	localVarDef->mDeclBlock = mBfIRBuilder->GetInsertBlock();
	DoAddLocalVariable(localVarDef);

	auto rootMethodState = mCurMethodState->GetRootMethodState();

	if (localVarDef->mLocalVarId == -1)
	{
		BF_ASSERT(rootMethodState->mCurLocalVarId >= 0);
		localVarDef->mLocalVarId = rootMethodState->mCurLocalVarId++;
	}

	bool checkLocal = true;
	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor))
	{
		if (auto autoCtorDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(mCurMethodInstance->mMethodDef->mMethodDeclaration))
			checkLocal = false;
	}

	if ((localVarDef->mNameNode != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) && (!mIsComptimeModule) && (checkLocal))
		mCompiler->mResolvePassData->mAutoComplete->CheckLocalDef(localVarDef->mNameNode, localVarDef);

	if (((localVarDef->mNameNode != NULL) && (mCurMethodInstance != NULL)) && (checkLocal))
	{
		bool isClosureProcessing = (mCurMethodState->mClosureState != NULL) && (!mCurMethodState->mClosureState->mCapturing);
		if ((!isClosureProcessing) && (mCompiler->mResolvePassData != NULL) && (localVarDef->mNameNode != NULL) && (rootMethodState->mMethodInstance != NULL) && (!mIsComptimeModule))
			mCompiler->mResolvePassData->HandleLocalReference(localVarDef->mNameNode, rootMethodState->mMethodInstance->GetOwner()->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, localVarDef->mLocalVarId);
	}

	return localVarDef;
}

void BfModule::CreateDIRetVal()
{
	if (!WantsDebugInfo())
		return;

	/*if (!mCurMethodState->mRetVal)
	{
		if (mCurMethodInstance->mMethodDef->mName != "Ziggle")
			return;
	}*/

	if ((mCurMethodState->mRetVal) || (mCurMethodState->mRetValAddr))
	{
		BfType* dbgType = mCurMethodInstance->mReturnType;
		BfIRValue dbgValue = mCurMethodState->mRetVal.mValue;
		if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
		{
			BF_ASSERT(mCurMethodState->mRetValAddr);
			dbgType = CreatePointerType(dbgType);
			dbgValue = mCurMethodState->mRetValAddr;
		}
		else
		{
			BF_ASSERT(!mCurMethodState->mRetValAddr);
		}

		mCurMethodState->mDIRetVal = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
			"@return", mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(dbgType));
		auto declareCall = mBfIRBuilder->DbgInsertDeclare(dbgValue, mCurMethodState->mDIRetVal);
	}
}

BfTypedValue BfModule::CreateTuple(const Array<BfTypedValue>& values, const Array<String>& fieldNames)
{
	BfTypeVector fieldTypes;
	for (auto arg : values)
		fieldTypes.Add(arg.mType);

	auto tupleType = CreateTupleType(fieldTypes, fieldNames);

	auto tupleTypedValue = BfTypedValue(CreateAlloca(tupleType), tupleType, true);
	for (int fieldIdx = 0; fieldIdx < tupleType->mFieldInstances.size(); fieldIdx++)
	{
		auto& fieldInstance = tupleType->mFieldInstances[fieldIdx];
		if (fieldInstance.mDataIdx <= 0)
			continue;

		auto typedVal = values[fieldIdx];
		typedVal = LoadOrAggregateValue(typedVal);
		mBfIRBuilder->CreateAlignedStore(typedVal.mValue, mBfIRBuilder->CreateInBoundsGEP(tupleTypedValue.mValue, 0, fieldInstance.mDataIdx), typedVal.mType->mAlign);
	}

	return tupleTypedValue;
}

void BfModule::CheckVariableDef(BfLocalVariable* variableDef)
{
	if (variableDef->mName.IsEmpty())
		return;

	BfLocalVarEntry* localVarEntryPtr = NULL;
	if ((mCurMethodState != NULL) && (mCurMethodState->mLocalVarSet.TryGet(BfLocalVarEntry(variableDef), &localVarEntryPtr)))
	{
		auto checkLocal = localVarEntryPtr->mLocalVar;
		if ((checkLocal->mLocalVarIdx >= mCurMethodState->GetLocalStartIdx()) && (!checkLocal->mIsShadow))
		{
			BfError* error;
			if (checkLocal->mIsImplicitParam)
				return; // Ignore 'redefinition'
			if (checkLocal->IsParam())
			{
				if (variableDef->IsParam())
					error = Fail(StrFormat("A parameter named '%s' has already been declared", variableDef->mName.c_str()), variableDef->mNameNode);
				else
					error = Fail(StrFormat("The name '%s' is already used by a parameter. Consider declaring 'var %s;' if you wish to make a mutable copy of that parameter.", variableDef->mName.c_str(), variableDef->mName.c_str()), variableDef->mNameNode);
			}
			else if (checkLocal->mLocalVarIdx < mCurMethodState->mCurScope->mLocalVarStart)
				error = Fail(StrFormat("A variable named '%s' has already been declared in this parent's scope", variableDef->mName.c_str()), variableDef->mNameNode);
			else
				error = Fail(StrFormat("A variable named '%s' has already been declared in this scope", variableDef->mName.c_str()), variableDef->mNameNode);
			if ((checkLocal->mNameNode != NULL) && (error != NULL))
				mCompiler->mPassInstance->MoreInfo("Previous declaration", checkLocal->mNameNode);
			return;
		}
	}
}

BfScopeData* BfModule::FindScope(BfAstNode* scopeName, BfMixinState* fromMixinState, bool allowAcrossDeferredBlock)
{
	bool inMixinDecl = (mCurMethodInstance != NULL) && (mCurMethodInstance->IsMixin());

	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(scopeName))
	{
		if (tokenNode->GetToken() == BfToken_Colon)
		{
			if ((!allowAcrossDeferredBlock) && (mCurMethodState->mInDeferredBlock))
			{
				Fail("Cannot access method scope across deferred block boundary", scopeName);
				return NULL;
			}

			return &mCurMethodState->mHeadScope;
		}
		else if (tokenNode->GetToken() == BfToken_Mixin)
		{
			if (fromMixinState == NULL)
			{
				if (mCurMethodInstance->mMethodDef->mMethodType != BfMethodType_Mixin)
					Fail("'mixin' scope specifier can only be used within a mixin declaration", scopeName);
				return mCurMethodState->mCurScope;
			}

			fromMixinState->mUsedInvocationScope = true;
			return fromMixinState->mTargetScope;
		}
		else if (tokenNode->GetToken() == BfToken_New)
			return NULL;
	}

	if (auto identifier = BfNodeDynCast<BfIdentifierNode>(scopeName))
	{
		auto findLabel = scopeName->ToString();

		bool crossedDeferredBlock = false;

		auto checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			if (checkScope->mIsDeferredBlock)
				crossedDeferredBlock = true;

			if (checkScope->mLabel == findLabel)
			{
				if ((crossedDeferredBlock) && (!allowAcrossDeferredBlock))
				{
					Fail(StrFormat("Cannot access scope '%s' across deferred block boundary", findLabel.c_str()), scopeName);
					return NULL;
				}
				return checkScope;
			}
			checkScope = checkScope->mPrevScope;
		}

		if (!inMixinDecl)
			Fail(StrFormat("Unable to locate label '%s'", findLabel.c_str()), scopeName);
	}

	if (auto scopeNode = BfNodeDynCast<BfScopeNode>(scopeName))
	{
		return FindScope(scopeNode->mTargetNode, allowAcrossDeferredBlock);
	}

	return mCurMethodState->mCurScope;
}

BfScopeData* BfModule::FindScope(BfAstNode* scopeName, bool allowAcrossDeferredBlock)
{
	return FindScope(scopeName, mCurMethodState->mMixinState, allowAcrossDeferredBlock);
}

BfBreakData* BfModule::FindBreakData(BfAstNode* scopeName)
{
	auto scopeData = FindScope(scopeName, false);
	if (scopeData == NULL)
		return NULL;
	int scopeDepth = scopeData->GetDepth();

	// Find the first break data that is at or below the depth of our requested scope
	auto breakData = mCurMethodState->mBreakData;
	while ((breakData != NULL) && (scopeDepth < breakData->mScope->GetDepth()))
	{
		if (breakData->mScope->mIsConditional)
			return NULL;
		breakData = breakData->mPrevBreakData;
	}
	return breakData;
}

void BfModule::EmitLifetimeEnds(BfScopeData* scopeData)
{
	// LLVM is stricter about the placement of these (ie: they can't occur after a 'ret')
	if (!IsTargetingBeefBackend())
		return;
	for (auto lifetimeEnd : scopeData->mDeferredLifetimeEnds)
	{
		mBfIRBuilder->CreateLifetimeEnd(lifetimeEnd);
	}
}

void BfModule::ClearLifetimeEnds()
{
	auto scopeData = mCurMethodState->mCurScope;
	while (scopeData != NULL)
	{
		scopeData->mDeferredLifetimeEnds.Clear();
		scopeData = scopeData->mPrevScope;
	}
}

bool BfModule::WantsDebugInfo()
{
	if ((mCurMethodInstance != NULL) &&
		((mCurMethodInstance->mIsUnspecialized) || (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Mixin)))
		return false;

	return (mBfIRBuilder->DbgHasInfo()) && (mHasFullDebugInfo) &&
		((mCurMethodState == NULL) || (mCurMethodState->mClosureState == NULL) || (!mCurMethodState->mClosureState->mCapturing));
}

BfTypeOptions* BfModule::GetTypeOptions()
{
	if ((mCurMethodState != NULL) && (mCurMethodState->mMethodTypeOptions != NULL))
		return mCurMethodState->mMethodTypeOptions;
	if (mCurMethodInstance == NULL)
		return NULL;
	return mSystem->GetTypeOptions(mCurTypeInstance->mTypeOptionsIdx);
}

BfReflectKind BfModule::GetUserReflectKind(BfTypeInstance* attrType)
{
	BfReflectKind reflectKind = BfReflectKind_None;

	if (attrType->mCustomAttributes != NULL)
	{
		for (auto& customAttr : attrType->mCustomAttributes->mAttributes)
		{
			if (customAttr.mType->mTypeDef->mName->ToString() == "AttributeUsageAttribute")
			{
				for (auto& prop : customAttr.mSetProperties)
				{
					auto propDef = prop.mPropertyRef.mTypeInstance->mTypeDef->mProperties[prop.mPropertyRef.mPropIdx];
					if (propDef->mName == "ReflectUser")
					{
						if (prop.mParam.mValue.IsConst())
						{
							auto constant = attrType->mConstHolder->GetConstant(prop.mParam.mValue);
							reflectKind = (BfReflectKind)(reflectKind | (BfReflectKind)constant->mInt32);
						}
					}
				}

				// Check for Flags arg
				if (customAttr.mCtorArgs.size() < 2)
					continue;
				auto constant = attrType->mConstHolder->GetConstant(customAttr.mCtorArgs[1]);
				if (constant == NULL)
					continue;
				if (constant->mTypeCode == BfTypeCode_Boolean)
					continue;
				if ((constant->mInt8 & BfCustomAttributeFlags_ReflectAttribute) != 0)
					reflectKind = (BfReflectKind)(reflectKind | BfReflectKind_User);
			}
		}
	}

	return reflectKind;
}

BfReflectKind BfModule::GetReflectKind(BfReflectKind reflectKind, BfTypeInstance* typeInstance)
{
	auto checkTypeInstance = typeInstance;
	while (checkTypeInstance != NULL)
	{
		if (checkTypeInstance->mCustomAttributes != NULL)
		{
			auto checkReflectKind = BfReflectKind_None;

			for (auto& customAttr : checkTypeInstance->mCustomAttributes->mAttributes)
			{
				if (customAttr.mType->mTypeDef->mName->ToString() == "ReflectAttribute")
				{
					if (customAttr.mCtorArgs.size() > 0)
					{
						auto constant = checkTypeInstance->mConstHolder->GetConstant(customAttr.mCtorArgs[0]);
						checkReflectKind = (BfReflectKind)((int)checkReflectKind | constant->mInt32);
					}
					else
					{
						checkReflectKind = BfReflectKind_All;
					}
				}
				else
				{
					auto userReflectKind = GetUserReflectKind(customAttr.mType);
					checkReflectKind = (BfReflectKind)(checkReflectKind | userReflectKind);
				}
			}

			if ((checkTypeInstance == typeInstance) ||
				((checkReflectKind & BfReflectKind_ApplyToInnerTypes) != 0))
			{
				reflectKind = (BfReflectKind)(reflectKind | checkReflectKind);
			}
		}

		for (auto ifaceEntry : typeInstance->mInterfaces)
		{
			if (ifaceEntry.mInterfaceType->mCustomAttributes != NULL)
			{
				auto iface = ifaceEntry.mInterfaceType;
				auto customAttr = iface->mCustomAttributes->Get(mCompiler->mReflectAttributeTypeDef);
				if (customAttr != NULL)
				{
					for (auto& prop : customAttr->mSetProperties)
					{
						auto propDef = prop.mPropertyRef.mTypeInstance->mTypeDef->mProperties[prop.mPropertyRef.mPropIdx];
						if (propDef->mName == "ReflectImplementer")
						{
							if (prop.mParam.mValue.IsConst())
							{
								auto constant = iface->mConstHolder->GetConstant(prop.mParam.mValue);
								reflectKind = (BfReflectKind)(reflectKind | (BfReflectKind)constant->mInt32);
							}
						}
					}
				}
			}
		}

		checkTypeInstance = mContext->mUnreifiedModule->GetOuterType(checkTypeInstance);
	}

	return reflectKind;
}

bool BfModule::HasDeferredScopeCalls(BfScopeData* scope)
{
	BfScopeData* checkScope = mCurMethodState->mCurScope;
	while (checkScope != NULL)
	{
		if ((!checkScope->mDeferredCallEntries.IsEmpty()) || (!checkScope->mDeferredLifetimeEnds.empty()))
			return true;
		if (checkScope == scope)
			break;
		checkScope = checkScope->mPrevScope;
	}

	return false;
}

void BfModule::EmitDeferredScopeCalls(bool useSrcPositions, BfScopeData* scopeData, BfIRBlock doneBlock)
{
	// Is there anything we need to do here?
	if (mBfIRBuilder->mIgnoreWrites)
	{
		// Just visit deferred blocks
		BfScopeData* checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			BfDeferredCallEntry* deferredCallEntry = checkScope->mDeferredCallEntries.mHead;
			while (deferredCallEntry != NULL)
			{
				if (deferredCallEntry->mDeferredBlock != NULL)
				{
					SetAndRestoreValue<BfMixinState*> prevMixinState(mCurMethodState->mMixinState, checkScope->mMixinState);

					if (checkScope == &mCurMethodState->mHeadScope)
						CreateRetValLocal();

					SetAndRestoreValue<BfAstNode*> prevCustomAttribute(mCurMethodState->mEmitRefNode, deferredCallEntry->mEmitRefNode);
					VisitEmbeddedStatement(deferredCallEntry->mDeferredBlock, NULL, BfEmbeddedStatementFlags_IsDeferredBlock);
				}
				deferredCallEntry = deferredCallEntry->mNext;
			}

			if (checkScope == scopeData)
				break;
			checkScope = checkScope->mPrevScope;
		}

		return;
	}

	// Why did we want to do SetIllegalSrcPos here?
	//  Don't we always want to step onto these instances?
	//  Find a case where we don't and perhaps only do it there.
	//  The downside was that 'EmitEnsureInstructionAt' on the end of block statements causes
	//  a (seemingly) unneeded NOP when we do SetIllegalSrcPos
	//SetIllegalSrcPos();

	bool setSrcPos = false;

	bool wantsNop = true;

	BfAstNode* deferCloseNode = NULL;

	if (mCurMethodState->mInPostReturn)
	{
		// These won't get used, they are post-return
		return;
	}

	SizedArray<BfIRValue, 8> deferredLifetimeEnds;

	BfScopeData* scopeJumpBlock = NULL;

	bool hadExtraDeferredLifetimeEnds = false;

	auto _FlushLifetimeEnds = [&]()
	{
		for (auto lifetimeEnd : deferredLifetimeEnds)
		{
			mBfIRBuilder->CreateLifetimeEnd(lifetimeEnd);
			mBfIRBuilder->ClearDebugLocation_Last();
		}
		deferredLifetimeEnds.clear();
	};

	BfScopeData* checkScope = mCurMethodState->mCurScope;
	while (checkScope != NULL)
	{
		if (checkScope->mCloseNode != NULL)
			deferCloseNode = checkScope->mCloseNode;

		if (doneBlock)
		{
			// Try to find a match where we've already emitted these calls and then jumped to the correct block
			for (auto& checkHandler : checkScope->mDeferredHandlers)
			{
				if (checkHandler.mDoneBlock == doneBlock)
				{
					scopeJumpBlock = checkScope;
					mBfIRBuilder->CreateBr(checkHandler.mHandlerBlock);
					mBfIRBuilder->ClearDebugLocation_Last();
					_FlushLifetimeEnds();
					break;
				}
			}

			if (scopeJumpBlock != NULL)
				break;
		}

		bool hasWork = (checkScope->mSavedStack) || (checkScope->mDeferredCallEntries.mHead != NULL);

		if (checkScope != scopeData) // Only emit a block for deferred lifetimes if we're going back beyond this entry
			hasWork |= (!deferredLifetimeEnds.IsEmpty());

		if (hasWork)
		{
			SetAndRestoreValue<BfScopeData*> prevScope(mCurMethodState->mCurScope, checkScope);

			if (deferCloseNode != NULL)
			{
				UpdateSrcPos(deferCloseNode);
			}

			if (checkScope == &mCurMethodState->mHeadScope)
				CreateRetValLocal();

			if (doneBlock)
			{
				bool crossingMixin = mCurMethodState->mCurScope->mMixinDepth != checkScope->mMixinDepth;

				String blockName = "deferredCalls";
				//blockName += StrFormat("_%d", mBfIRBuilder->mBlockCount);

				BfDeferredHandler deferredHandler;
				if ((!crossingMixin) && (deferredLifetimeEnds.IsEmpty()))
				{
					mBfIRBuilder->SaveDebugLocation();
					mBfIRBuilder->ClearDebugLocation();
					deferredHandler.mHandlerBlock = mBfIRBuilder->MaybeChainNewBlock(blockName);
					mBfIRBuilder->RestoreDebugLocation();
				}
				else
				{
					// Definitely chain
					deferredHandler.mHandlerBlock = mBfIRBuilder->CreateBlock(blockName);
					mBfIRBuilder->CreateBr(deferredHandler.mHandlerBlock);
					mBfIRBuilder->ClearDebugLocation_Last();
					_FlushLifetimeEnds();
					mBfIRBuilder->AddBlock(deferredHandler.mHandlerBlock);
					mBfIRBuilder->SetInsertPoint(deferredHandler.mHandlerBlock);
				}

				deferredHandler.mDoneBlock = doneBlock;

				if (!mBfIRBuilder->mIgnoreWrites)
					checkScope->mDeferredHandlers.push_back(deferredHandler);

				if (checkScope == &mCurMethodState->mHeadScope)
				{
					if (!mCurMethodState->mDIRetVal)
					{
						// Weird case- if we have a return from a mixin, we need the DbgLoc to be for the mixin but we need the DIRetVal to
						//  be scoped to the physical method

						/*if (deferCloseNode != NULL)
						{
							UpdateSrcPos(deferCloseNode);
						}*/
						CreateDIRetVal();
					}
				}

				if (checkScope != mCurMethodState->mTailScope)
				{
					if (deferredHandler.mHandlerBlock.IsFake())
					{
						BF_ASSERT(mBfIRBuilder->mIgnoreWrites);
					}

					if (!mBfIRBuilder->mIgnoreWrites)
						checkScope->mAtEndBlocks.push_back(deferredHandler.mHandlerBlock);
				}
			}

			HashSet<BfDeferredCallEntry*> handledSet;
			BfDeferredCallEntry* deferredCallEntry = checkScope->mDeferredCallEntries.mHead;
			while (deferredCallEntry != NULL)
			{
				BfDeferredCallEmitState deferredCallEmitState;
				deferredCallEmitState.mCloseNode = deferCloseNode;
				SetAndRestoreValue<BfDeferredCallEmitState*> prevDeferredCallEmitState(mCurMethodState->mDeferredCallEmitState, &deferredCallEmitState);
				SetAndRestoreValue<bool> prevIgnoredWrites(mBfIRBuilder->mIgnoreWrites, deferredCallEntry->mIgnored);
				SetAndRestoreValue<BfMixinState*> prevMixinState(mCurMethodState->mMixinState, checkScope->mMixinState);

				if (deferCloseNode != NULL)
				{
					UpdateSrcPos(deferCloseNode);
				}
				if (wantsNop)
					EmitEnsureInstructionAt();
				wantsNop = false;

				if (deferredCallEntry->mDeferredBlock != NULL)
				{
					BfDeferredCallEntry** entryPtr = NULL;
					if (!handledSet.TryAdd(deferredCallEntry, &entryPtr))
					{
						// Already handled, can happen if we defer again within the block
						deferredCallEntry = deferredCallEntry->mNext;
						continue;
					}

					auto prevHead = checkScope->mDeferredCallEntries.mHead;
					EmitDeferredCall(checkScope, *deferredCallEntry, true);
					if (prevHead != checkScope->mDeferredCallEntries.mHead)
					{
						// The list changed, start over and ignore anything we've already handled
						deferredCallEntry = checkScope->mDeferredCallEntries.mHead;
					}
					else
						deferredCallEntry = deferredCallEntry->mNext;
				}
				else
				{
					EmitDeferredCall(checkScope, *deferredCallEntry, true);
					deferredCallEntry = deferredCallEntry->mNext;
				}
			}

			if (checkScope->mSavedStack)
			{
				checkScope->mSavedStackUses.Add(mBfIRBuilder->CreateStackRestore(checkScope->mSavedStack));

				if (mCurMethodState->mDynStackRevIdx)
				{
					auto curValue = mBfIRBuilder->CreateLoad(mCurMethodState->mDynStackRevIdx);
					auto newValue = mBfIRBuilder->CreateAdd(curValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
					mBfIRBuilder->CreateStore(newValue, mCurMethodState->mDynStackRevIdx);
				}
			}
		}

		if (!checkScope->mIsScopeHead)
		{
			// We manually emit function-level lifetime ends after the 'ret' in ProcessMethod
			if (!IsTargetingBeefBackend())
			{
				for (auto lifetimeEnd : checkScope->mDeferredLifetimeEnds)
				{
					mBfIRBuilder->CreateLifetimeEnd(lifetimeEnd);
				}
			}
			else
			{
				for (auto lifetimeEnd : checkScope->mDeferredLifetimeEnds)
				{
					deferredLifetimeEnds.Add(lifetimeEnd);
				}
			}
		}

		if (checkScope == scopeData)
			break;
		checkScope = checkScope->mPrevScope;
	}

	if ((doneBlock) && (scopeJumpBlock == NULL))
	{
		mBfIRBuilder->CreateBr(doneBlock);
		mBfIRBuilder->ClearDebugLocation_Last();
	}

	// Keeping CreateLifetimeEnds until the end messes up when we chain to old mDeferredHandlers that
	//  may have been created when other scope lifetimes were available

	_FlushLifetimeEnds();

	// Emit lifetimeEnds after the branch for Beef
	/*if (IsTargetingBeefBackend())
	{
		bool needsEnsureInst = (!doneBlock) && (scopeJumpBlock == NULL);

		BfScopeData* checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			if (checkScope == scopeJumpBlock)
				break;

			if (!checkScope->mIsScopeHead)
			{
				for (auto lifetimeEnd : checkScope->mDeferredLifetimeEnds)
				{
					if (needsEnsureInst)
					{
						needsEnsureInst = false;
					}

					mBfIRBuilder->CreateLifetimeEnd(lifetimeEnd);
				}
			}

			if (checkScope == scopeData)
				break;
			checkScope = checkScope->mPrevScope;
		}
	}*/
}

void BfModule::MarkScopeLeft(BfScopeData* scopeData, bool isNoReturn)
{
	if ((mCurMethodState->mDeferredLocalAssignData != NULL) && (!isNoReturn))
	{
		auto deferredLocalAssignData = mCurMethodState->mDeferredLocalAssignData;

		while (deferredLocalAssignData != NULL)
		{
			if ((deferredLocalAssignData->mScopeData == NULL) || (deferredLocalAssignData->mScopeData->mScopeDepth < scopeData->mScopeDepth))
				break;
			if ((deferredLocalAssignData->mScopeData != NULL) && (deferredLocalAssignData->mScopeData->mScopeDepth == scopeData->mScopeDepth))
				deferredLocalAssignData->mIsUnconditional = false;
			deferredLocalAssignData = deferredLocalAssignData->mChainedAssignData;
		}
	}

	// When we leave a scope, mark those as assigned for deferred assignment purposes
	for (int localIdx = scopeData->mLocalVarStart; localIdx < (int)mCurMethodState->mLocals.size(); localIdx++)
	{
		auto localDef = mCurMethodState->mLocals[localIdx];
		if (localDef->mAssignedKind == BfLocalVarAssignKind_None)
		{
			bool hadAssignment = false;
			if (mCurMethodState->mDeferredLocalAssignData != NULL)
			{
				for (auto& entry : mCurMethodState->mDeferredLocalAssignData->mAssignedLocals)
					if (entry.mLocalVar == localDef)
						hadAssignment = true;
			}
			if (!hadAssignment)
			{
				if (!isNoReturn)
					localDef->mHadExitBeforeAssign = true;
				mCurMethodState->LocalDefined(localDef);
			}
		}
	}
}

void BfModule::CreateReturn(BfIRValue val)
{
	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
	{
		// Store to sret
		BF_ASSERT(val);
		mBfIRBuilder->CreateStore(val, mBfIRBuilder->GetArgument(mCurMethodInstance->GetStructRetIdx()));
		mBfIRBuilder->CreateRetVoid();
		return;
	}

	if (mCurMethodInstance->mReturnType->IsVar())
		return;

	if (mCurMethodInstance->mReturnType->IsValuelessType())
	{
		mBfIRBuilder->CreateRetVoid();
		return;
	}

	if (mCurMethodInstance->mReturnType->IsStruct())
	{
		BfTypeCode loweredReturnType = BfTypeCode_None;
		BfTypeCode loweredReturnType2 = BfTypeCode_None;
		if (!mIsComptimeModule)
			mCurMethodInstance->GetLoweredReturnType(&loweredReturnType, &loweredReturnType2);

		if (loweredReturnType != BfTypeCode_None)
		{
			auto retVal = CreateAlloca(mCurMethodInstance->mReturnType);
			mBfIRBuilder->CreateStore(val, retVal);

			auto irRetType = GetIRLoweredType(loweredReturnType, loweredReturnType2);
			irRetType = mBfIRBuilder->GetPointerTo(irRetType);
			auto ptrReturnValue = mBfIRBuilder->CreateBitCast(retVal, irRetType);
			auto loadedReturnValue = mBfIRBuilder->CreateLoad(ptrReturnValue);
			mBfIRBuilder->CreateRet(loadedReturnValue);
			return;
		}
	}

	BF_ASSERT(val);
	mBfIRBuilder->CreateRet(val);
}

void BfModule::EmitReturn(const BfTypedValue& val)
{
	if (mCurMethodState->mIRExitBlock)
	{
		if (!mCurMethodInstance->mReturnType->IsValuelessType())
		{
			if (val) // We allow for val to be empty if we know we've already written the value to mRetVal
			{
				if ((mCurMethodState->mRetValAddr) || (mCurMethodState->mRetVal))
				{
					BfIRValue retVal = mCurMethodState->mRetVal.mValue;
					if (!mCurMethodState->mRetVal)
						retVal = mBfIRBuilder->CreateAlignedLoad(mCurMethodState->mRetValAddr, mCurMethodInstance->mReturnType->mAlign);

					mBfIRBuilder->CreateAlignedStore(val.mValue, retVal, mCurMethodInstance->mReturnType->mAlign);
				}
				else if (mIsComptimeModule)
				{
					if (!val.mType->IsValuelessType())
						mBfIRBuilder->CreateSetRet(val.mValue, val.mType->mTypeId);
					else
						mBfIRBuilder->CreateSetRet(BfIRValue(), val.mType->mTypeId);
				}
				else
				{
					// Just ignore
					BF_ASSERT(mCurMethodInstance->mReturnType->IsVar());
				}
			}
		}
		EmitDeferredScopeCalls(true, NULL, mCurMethodState->mIRExitBlock);
	}
	else
	{
		EmitDeferredScopeCalls(false, NULL);
		if (val)
		{
			BF_ASSERT(mBfIRBuilder->mIgnoreWrites);
		}
	}

	mCurMethodState->SetHadReturn(true);
	mCurMethodState->mLeftBlockUncond = true;
}

void BfModule::EmitDefaultReturn()
{
	if (mCurMethodState->mInDeferredBlock)
		return;

	if (mCurMethodState->mIRExitBlock)
	{
		EmitDeferredScopeCalls(true, NULL, mCurMethodState->mIRExitBlock);
	}
	else
	{
		if ((mCurMethodInstance == NULL) || (mCurMethodInstance->mReturnType->IsVar()))
		{
			// Ignore
		}
		else if (mCurMethodInstance->mReturnType->IsVoid())
			mBfIRBuilder->CreateRetVoid();
		else if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() == -1))
			mBfIRBuilder->CreateRet(GetDefaultValue(mCurMethodInstance->mReturnType));
	}

	mCurMethodState->SetHadReturn(true);
	mCurMethodState->mLeftBlockUncond = true;
}

void BfModule::AssertErrorState()
{
	if (mIgnoreErrors)
		return;
	if (mHadBuildError)
		return;

	if (mCurTypeInstance != NULL)
	{
		if (mCurTypeInstance->IsUnspecializedTypeVariation())
			return;
	}

	if (mCurMethodInstance != NULL)
	{
		if (mCurMethodInstance->IsOrInUnspecializedVariation())
			return;
		if (mCurMethodInstance->mHasFailed)
			return;
	}

	// We want the module to be marked as failed even if it's just an error in the parser
	if (mCurMethodInstance != NULL)
		mCurMethodInstance->mHasFailed = true;
	mHadBuildError = true;

	// We check for either the parsing to have failed (possibly on a previous run), or for the current instance to have failed
	if (mCurTypeInstance != NULL)
	{
		if (mCurTypeInstance->mTypeFailed)
			return;
		if ((mCurTypeInstance->mTypeDef->GetDefinition()->mSource != NULL) && (mCurTypeInstance->mTypeDef->GetDefinition()->mSource->mParsingFailed))
			return;
	}
	if (mCurMethodInstance != NULL)
	{
		if ((mCurMethodInstance->mMethodDef->mDeclaringType != NULL) &&
			(mCurMethodInstance->mMethodDef->mDeclaringType->mSource != NULL) &&
			(mCurMethodInstance->mMethodDef->mDeclaringType->mSource->mParsingFailed))
			return;
		if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL) &&
			(mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType->mSource != NULL) &&
			(mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType->mSource->mParsingFailed))
			return;
	}

	if (mCompiler->IsAutocomplete())
		return;

	BF_ASSERT(mCompiler->mPassInstance->HasFailed());
}

void BfModule::AssertParseErrorState()
{
	if (mCompiler->mRevision == 1)
		AssertErrorState();
}

BfType* BfModule::GetDelegateReturnType(BfType* delegateType)
{
	BF_ASSERT(delegateType->IsDelegate());
	auto typeInst = delegateType->ToTypeInstance();
	PopulateType(typeInst, BfPopulateType_DataAndMethods);
	BfMethodInstance* invokeMethodInstance = GetRawMethodInstanceAtIdx(typeInst->ToTypeInstance(), 0, "Invoke");
	if (invokeMethodInstance == NULL)
		return GetPrimitiveType(BfTypeCode_Var);
	return invokeMethodInstance->mReturnType;
}

BfMethodInstance* BfModule::GetDelegateInvokeMethod(BfTypeInstance* typeInstance)
{
	return GetRawMethodInstanceAtIdx(typeInstance, 0, "Invoke");
}

void BfModule::CreateDelegateInvokeMethod()
{
	// Clear out debug loc - otherwise we'll single step onto the delegate type declaration
	//mBfIRBuilder->ClearDebugLocation();

	SetIllegalSrcPos();

	auto typeInstance = mCurTypeInstance;
	auto memberFuncType = mBfIRBuilder->MapMethod(mCurMethodInstance);
	SizedArray<BfIRType, 4> staticParamTypes;
	SizedArray<BfIRValue, 4> staticFuncArgs;
	SizedArray<BfIRValue, 4> memberFuncArgs;

	auto multicastDelegateType = typeInstance->mBaseType;
	if (multicastDelegateType->mFieldInstances.size() != 2)
	{
		AssertErrorState();
		return;
	}

	auto multicastDelegate = mBfIRBuilder->CreateBitCast(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->MapType(multicastDelegateType));
	auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(multicastDelegate, 0, 2); // Load 'delegate.mTarget'
	auto fieldVal = mBfIRBuilder->CreateAlignedLoad(fieldPtr, mSystem->mPtrSize);

	BfExprEvaluator exprEvaluator(this);

	SizedArray<BfIRType, 8> origParamTypes;
	BfIRType origReturnType;
	BfIRType staticReturnType;
	mCurMethodInstance->GetIRFunctionInfo(this, origReturnType, origParamTypes);

	if (mCurMethodInstance->mReturnType->IsValueType())
		mBfIRBuilder->PopulateType(mCurMethodInstance->mReturnType, BfIRPopulateType_Full);

	if ((mIsComptimeModule) || (mCurMethodInstance->GetStructRetIdx() != 0))
		memberFuncArgs.push_back(BfIRValue()); // Push 'target'

	int thisIdx = 0;
	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
	{
		thisIdx = mCurMethodInstance->GetStructRetIdx() ^ 1;
		memberFuncArgs.push_back(mBfIRBuilder->GetArgument(mCurMethodInstance->GetStructRetIdx()));
	}

	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx(true) != -1))
		staticFuncArgs.push_back(mBfIRBuilder->GetArgument(mCurMethodInstance->GetStructRetIdx()));

	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() == 0))
		memberFuncArgs.push_back(BfIRValue()); // Push 'target'

	mCurMethodInstance->GetIRFunctionInfo(this, staticReturnType, staticParamTypes, true);

	for (int i = 1; i < (int)mCurMethodState->mLocals.size(); i++)
	{
		BfTypedValue localVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[i], true);
		exprEvaluator.PushArg(localVal, staticFuncArgs);
		exprEvaluator.PushArg(localVal, memberFuncArgs);
	}

	auto staticFunc = mBfIRBuilder->CreateFunctionType(staticReturnType, staticParamTypes, false);
	auto staticFuncPtr = mBfIRBuilder->GetPointerTo(staticFunc);
	auto staticFuncPtrPtr = mBfIRBuilder->GetPointerTo(staticFuncPtr);

	auto trueBB = mBfIRBuilder->CreateBlock("if.then", true);
	auto falseBB = mBfIRBuilder->CreateBlock("if.else");
	auto doneBB = mBfIRBuilder->CreateBlock("done");

	auto checkTargetNull = mBfIRBuilder->CreateIsNotNull(fieldVal);
	mBfIRBuilder->CreateCondBr(checkTargetNull, trueBB, falseBB);

	BfIRValue nonStaticResult;
	BfIRValue staticResult;

	auto callingConv = GetIRCallingConvention(mCurMethodInstance);

	/// Non-static invocation
	{
		auto memberFuncPtr = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapMethod(mCurMethodInstance));
		auto memberFuncPtrPtr = mBfIRBuilder->GetPointerTo(memberFuncPtr);

		mBfIRBuilder->SetInsertPoint(trueBB);
		memberFuncArgs[thisIdx] = mBfIRBuilder->CreateBitCast(fieldVal, mBfIRBuilder->MapType(mCurTypeInstance));
		auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(multicastDelegate, 0, 1); // Load 'delegate.mFuncPtr'
		auto funcPtrPtr = mBfIRBuilder->CreateBitCast(fieldPtr, memberFuncPtrPtr);
		auto funcPtr = mBfIRBuilder->CreateAlignedLoad(funcPtrPtr, mSystem->mPtrSize);
		nonStaticResult = mBfIRBuilder->CreateCall(funcPtr, memberFuncArgs);
		if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
			mBfIRBuilder->Call_AddAttribute(nonStaticResult, mCurMethodInstance->GetStructRetIdx() + 1, BfIRAttribute_StructRet);
		if (callingConv != BfIRCallingConv_CDecl)
			mBfIRBuilder->SetCallCallingConv(nonStaticResult, callingConv);
		mCurMethodState->SetHadReturn(false);
		mCurMethodState->mLeftBlockUncond = false;
		mCurMethodState->mLeftBlockCond = false;
		mBfIRBuilder->CreateBr(doneBB);
	}

	// Static invocation
	{
		mBfIRBuilder->AddBlock(falseBB);
		mBfIRBuilder->SetInsertPoint(falseBB);
		auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(multicastDelegate, 0, 1); // Load 'delegate.mFuncPtr'
		auto funcPtrPtr = mBfIRBuilder->CreateBitCast(fieldPtr, staticFuncPtrPtr);
		auto funcPtr = mBfIRBuilder->CreateAlignedLoad(funcPtrPtr, mSystem->mPtrSize);
		staticResult = mBfIRBuilder->CreateCall(funcPtr, staticFuncArgs);
		if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx(true) != -1))
		{
			// Note: since this is a forced static invocation, we know the sret will be the first parameter
			mBfIRBuilder->Call_AddAttribute(staticResult, 0 + 1, BfIRAttribute_StructRet);
		}

		// We had a sret for the non-static but no sret for the static (because we have a lowered return type there)
		if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1) && (mCurMethodInstance->GetStructRetIdx(true) == -1))
		{
			auto sretToType = mBfIRBuilder->GetPointerTo(staticReturnType);
			auto sretCastedPtr = mBfIRBuilder->CreateBitCast(mBfIRBuilder->GetArgument(mCurMethodInstance->GetStructRetIdx()), sretToType);
			mBfIRBuilder->CreateStore(staticResult, sretCastedPtr);
		}

		if (callingConv == BfIRCallingConv_ThisCall)
			callingConv = BfIRCallingConv_CDecl;
		if (callingConv != BfIRCallingConv_CDecl)
			mBfIRBuilder->SetCallCallingConv(staticResult, callingConv);
		mCurMethodState->SetHadReturn(false);
		mCurMethodState->mLeftBlockUncond = false;
		mCurMethodState->mLeftBlockCond = false;
		mBfIRBuilder->CreateBr(doneBB);
	}

	mBfIRBuilder->AddBlock(doneBB);
	mBfIRBuilder->SetInsertPoint(doneBB);
	if (mCurMethodInstance->mReturnType->IsVar())
	{
		// Do nothing
	}
	else if ((mCurMethodInstance->mReturnType->IsValuelessType()) ||
		((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1)))
	{
		mBfIRBuilder->CreateRetVoid();
	}
	else
	{
		BfIRType loweredIRReturnType;
		BfTypeCode loweredTypeCode = BfTypeCode_None;
		BfTypeCode loweredTypeCode2 = BfTypeCode_None;
		if ((!mIsComptimeModule) && (mCurMethodInstance->GetLoweredReturnType(&loweredTypeCode, &loweredTypeCode2)))
			loweredIRReturnType = GetIRLoweredType(loweredTypeCode, loweredTypeCode2);
		else
			loweredIRReturnType = mBfIRBuilder->MapType(mCurMethodInstance->mReturnType);
		auto phi = mBfIRBuilder->CreatePhi(loweredIRReturnType, 2);
		mBfIRBuilder->AddPhiIncoming(phi, nonStaticResult, trueBB);
		mBfIRBuilder->AddPhiIncoming(phi, staticResult, falseBB);
		mBfIRBuilder->CreateRet(phi);
	}
}

// "Interested" here means every method for a normal compile, and just the method the cursor is on for autocompletion
bool BfModule::IsInterestedInMethod(BfTypeInstance* typeInstance, BfMethodDef* methodDef)
{
	auto typeDef = typeInstance->mTypeDef;
	auto methodDeclaration = methodDef->mMethodDeclaration;

	if (!mCompiler->mIsResolveOnly)
		return true;

	if (typeInstance->IsGenericTypeInstance())
	{
		// We only really want to process the unspecialized type for autocompletion
		if ((!typeInstance->IsUnspecializedType()) || (typeInstance->IsUnspecializedTypeVariation()))
			return false;
	}

	BfAstNode* checkNode = methodDeclaration;
	if (methodDeclaration == NULL)
		checkNode = methodDef->mBody;

	if ((!mCompiler->mResolvePassData->mParsers.IsEmpty()) && (typeDef->mTypeDeclaration->IsFromParser(mCompiler->mResolvePassData->mParsers[0])))
	{
		if (mCompiler->mResolvePassData->mAutoComplete == NULL)
			return true;
	}
	return false;
}

void BfModule::CalcAppendAlign(BfMethodInstance* methodInst)
{
	methodInst->mAppendAllocAlign = 1;
}

BfTypedValue BfModule::TryConstCalcAppend(BfMethodInstance* methodInst, SizedArrayImpl<BfIRValue>& args, bool force)
{
	BP_ZONE("BfModule::TryConstCalcAppend");

	BF_ASSERT(methodInst->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend);

	if ((mCompiler->mIsResolveOnly) && (!mIsComptimeModule) && (!force))
		return BfTypedValue();

	// We want to regenerate all ctor calls when the method internals change
	{
		auto checkTypeInst = methodInst->GetOwner();
		while (checkTypeInst->mTypeDef->mHasAppendCtor)
		{
			AddDependency(checkTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_InlinedCall);
			checkTypeInst = GetBaseType(checkTypeInst);
		}
	}

	if (!methodInst->mMayBeConst)
		return BfTypedValue();

	// Do we need to iterate in order to resolve mAppendAllocAlign?
	bool isFirstRun = methodInst->mEndingAppendAllocAlign < 0;
	bool wasAllConst = true;

	int argIdx = 0;
	int paramIdx = 0;
	while (true)
	{
		if (argIdx >= (int)args.size())
			break;
		auto paramType = methodInst->GetParamType(paramIdx);
		PopulateType(paramType);
		int argCount = 0;
		if (!paramType->IsValuelessType())
		{
			if ((!mIsComptimeModule) && (methodInst->GetParamIsSplat(paramIdx)))
				argCount = paramType->GetSplatCount();
			else
				argCount = 1;

			for (int argOfs = 0; argOfs < argCount; argOfs++)
			{
				auto arg = args[argIdx + argOfs];
				if (!arg.IsConst())
				{
					wasAllConst = false;

					if (!isFirstRun)
					{
						auto& param = methodInst->mParams[argIdx];
						if (param.mReferencedInConstPass)
						{
							// If this param is required as part of the const calculation then
							//  we require that it be const...
							return BfTypedValue();
						}
					}
				}
			}
		}

		paramIdx++;
		argIdx += argCount;
	}

	auto methodDef = methodInst->mMethodDef;
	auto methodDecl = methodDef->GetMethodDeclaration();
	auto methodDeclBlock = BfNodeDynCast<BfBlock>(methodDecl->mBody);
	if (methodDeclBlock == NULL)
		return GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr)); // Must not have an append alloc at all!

	BfTypedValue constValue;

	auto prevBlock = mBfIRBuilder->GetInsertBlock();

	auto checkState = mCurMethodState->mConstResolveState;
	while (checkState != NULL)
	{
		if (checkState->mMethodInstance == methodInst)
		{
			return BfTypedValue();
		}

		checkState = checkState->mPrevConstResolveState;
	}

	//auto accumValue = GetConstValue(0);
	bool failed = false;
	//
	{
		BfConstResolveState constResolveState;
		constResolveState.mMethodInstance = methodInst;
		constResolveState.mPrevConstResolveState = mCurMethodState->mConstResolveState;
		constResolveState.mInCalcAppend = true;

		SetAndRestoreValue<bool> ignoreWrites(mBfIRBuilder->mIgnoreWrites, true);
		BfMethodState methodState;
		SetAndRestoreValue<BfTypeInstance*> prevTypeInst(mCurTypeInstance, methodInst->GetOwner());
		SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mCurMethodInstance, methodInst);
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
		methodState.mTempKind = BfMethodState::TempKind_NonStatic;
		methodState.mConstResolveState = &constResolveState;

		int argIdx = 0;
		for (int paramIdx = 0; paramIdx < methodInst->GetParamCount(); paramIdx++)
		{
			if (methodInst->GetParamKind(paramIdx) == BfParamKind_AppendIdx)
				continue;
			auto paramType = methodInst->GetParamType(paramIdx);
			// Fix this after we allow structs to be consts
			//BF_ASSERT(!paramType->IsSplattable());

			BfLocalVariable* localVar = new BfLocalVariable();
			localVar->mName = methodInst->GetParamName(paramIdx);
			localVar->mResolvedType = paramType;
			localVar->mConstValue = args[argIdx];
			localVar->mParamIdx = paramIdx;
			AddLocalVariableDef(localVar);
			argIdx++;
		}

		AppendAllocVisitor appendAllocVisitor;
		appendAllocVisitor.mConstAccum = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
		appendAllocVisitor.mIsFirstConstPass = isFirstRun;

		BfTypedValue baseCtorAppendValue = CallBaseCtorCalc(true);
		if (baseCtorAppendValue)
		{
			if (baseCtorAppendValue.mValue.IsFake())
				appendAllocVisitor.mFailed = true;
			else
				appendAllocVisitor.mConstAccum = baseCtorAppendValue;
		}

		appendAllocVisitor.mModule = this;

		appendAllocVisitor.VisitChild(methodDecl->mBody);
		if (constResolveState.mFailed)
			appendAllocVisitor.mFailed = true;
		if (!appendAllocVisitor.mFailed)
			constValue = appendAllocVisitor.mConstAccum;
		if (isFirstRun)
		{
			mCurMethodInstance->mEndingAppendAllocAlign = appendAllocVisitor.mCurAppendAlign;
			if (mCurMethodInstance->mAppendAllocAlign <= 0)
				mCurMethodInstance->mAppendAllocAlign = 1;
		}

		if (isFirstRun)
		{
			for (int paramIdx = 0; paramIdx < methodInst->GetParamCount(); paramIdx++)
			{
				auto localVar = mCurMethodState->mLocals[paramIdx];
				if (localVar->mReadFromId != -1)
				{
					auto& param = methodInst->mParams[paramIdx];
					param.mReferencedInConstPass = true;
				}
			}
		}
	}

	mBfIRBuilder->SetInsertPoint(prevBlock);

	if (!constValue)
 	{
		// If we did a 'force' then some params may not have been const -- only clear mMayBeConst if we had
		//  all-const args
		if (wasAllConst)
		{
			methodInst->mMayBeConst = false;
		}
 	}

	return constValue;
}

BfTypedValue BfModule::CallBaseCtorCalc(bool constOnly)
{
	// Any errors should only be shown in the actual CTOR call
	SetAndRestoreValue<bool> prevIgnoreWrites(mIgnoreErrors, true);

	auto methodDef = mCurMethodInstance->mMethodDef;
	BF_ASSERT((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend));
	auto ctorDeclaration = (BfConstructorDeclaration*)methodDef->mMethodDeclaration;

	BfCustomAttributes* customAttributes = NULL;
	defer(delete customAttributes);
	BfInvocationExpression* ctorInvocation = NULL;
	if (ctorDeclaration != NULL)
	{
		ctorInvocation = BfNodeDynCast<BfInvocationExpression>(ctorDeclaration->mInitializer);
		if (auto attributedExpr = BfNodeDynCast<BfAttributedExpression>(ctorDeclaration->mInitializer))
		{
			ctorInvocation = BfNodeDynCast<BfInvocationExpression>(attributedExpr->mExpression);
			customAttributes = GetCustomAttributes(attributedExpr->mAttributes, BfAttributeTargets_MemberAccess);
		}
	}

	BfTypeInstance* targetType = NULL;
	if (ctorInvocation != NULL)
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorInvocation->mTarget);
		targetType = (targetToken->GetToken() == BfToken_This) ? mCurTypeInstance : mCurTypeInstance->mBaseType;
	}
	else
		targetType = mCurTypeInstance->mBaseType;
	if ((targetType == NULL) || (targetType == mContext->mBfObjectType))
		return NULL;

	auto targetRefNode = methodDef->GetRefNode();

	BfType* targetThisType = targetType;
	BfTypedValue target(mBfIRBuilder->GetFakeVal(), targetThisType);

	BfExprEvaluator exprEvaluator(this);
	BfResolvedArgs argValues;
	if ((ctorDeclaration != NULL) && (ctorInvocation != NULL))
	{
		argValues.Init(&ctorInvocation->mArguments);
	}

	//
	{
	}
	BfFunctionBindResult bindResult;
	bindResult.mSkipThis = true;
	bindResult.mWantsArgs = true;
	{
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);
		SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(exprEvaluator.mFunctionBindResult, &bindResult);
		exprEvaluator.MatchConstructor(targetRefNode, NULL, target, targetType, argValues, true, true);
	}

	if (bindResult.mMethodInstance == NULL)
	{
		AssertErrorState();
		return BfTypedValue();
	}

	if (!bindResult.mMethodInstance->mMethodDef->mHasAppend)
	{
		return BfTypedValue();
	}

	BF_ASSERT(bindResult.mIRArgs[0].IsFake());
	bindResult.mIRArgs.RemoveAt(0);
	auto calcAppendMethodModule = GetMethodInstanceAtIdx(bindResult.mMethodInstance->GetOwner(), bindResult.mMethodInstance->mMethodDef->mIdx + 1, BF_METHODNAME_CALCAPPEND);
	BfTypedValue appendSizeTypedValue = TryConstCalcAppend(calcAppendMethodModule.mMethodInstance, bindResult.mIRArgs, true);
	BF_ASSERT(calcAppendMethodModule.mMethodInstance->mAppendAllocAlign >= 0);
	mCurMethodInstance->mAppendAllocAlign = BF_MAX((int)mCurMethodInstance->mAppendAllocAlign, calcAppendMethodModule.mMethodInstance->mAppendAllocAlign);
	BF_ASSERT(calcAppendMethodModule.mMethodInstance->mEndingAppendAllocAlign > -1);
	mCurMethodState->mCurAppendAlign = BF_MAX(calcAppendMethodModule.mMethodInstance->mEndingAppendAllocAlign, 0);

	if (appendSizeTypedValue)
		return appendSizeTypedValue;

	if (constOnly)
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), GetPrimitiveType(BfTypeCode_IntPtr));

	bool needsRecalc = false;
	for (auto irArg : bindResult.mIRArgs)
	{
		if (irArg.IsFake())
			needsRecalc = true;
	}
	auto calcAppendArgs = bindResult.mIRArgs;
	if (needsRecalc)
	{
		// Do it again, but without mIgnoreWrites set
		BfResolvedArgs argValues;
		argValues.Init(&ctorInvocation->mArguments);
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);

		BfFunctionBindResult bindResult;
		bindResult.mSkipThis = true;
		bindResult.mWantsArgs = true;
		SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(exprEvaluator.mFunctionBindResult, &bindResult);
		exprEvaluator.MatchConstructor(targetRefNode, NULL, target, targetType, argValues, true, true);
		BF_ASSERT(bindResult.mIRArgs[0].IsFake());
		bindResult.mIRArgs.RemoveAt(0);
		calcAppendArgs = bindResult.mIRArgs;
	}

	if (mBfIRBuilder->mIgnoreWrites)
	{
		appendSizeTypedValue = GetFakeTypedValue(GetPrimitiveType(BfTypeCode_IntPtr));
	}
	else
	{
		BF_ASSERT(calcAppendMethodModule.mFunc);
		appendSizeTypedValue = exprEvaluator.CreateCall(NULL, calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, calcAppendArgs);
	}

	BF_ASSERT(appendSizeTypedValue.mType == GetPrimitiveType(BfTypeCode_IntPtr));
	return appendSizeTypedValue;
}

// This method never throws errors - it relies on the proper ctor actually throwing the errors
void BfModule::EmitCtorCalcAppend()
{
	if ((mCompiler->mIsResolveOnly) && (!mIsComptimeModule))
		return;

	auto methodDef = mCurMethodInstance->mMethodDef;
	auto methodDecl = methodDef->GetMethodDeclaration();

	Array<BfAstNode*> deferredNodeList;

	auto methodDeclBlock = BfNodeDynCast<BfBlock>(methodDecl->mBody);
	if (methodDeclBlock == NULL)
		return;

	AppendAllocVisitor appendAllocVisitor;
	auto baseCalcAppend = CallBaseCtorCalc(false);
	if (baseCalcAppend)
	{
		mBfIRBuilder->CreateStore(baseCalcAppend.mValue, mCurMethodState->mRetVal.mValue);
	}
	appendAllocVisitor.mModule = this;
	appendAllocVisitor.VisitChild(methodDecl->mBody);
}

void BfModule::CreateStaticCtor()
{
	auto typeDef = mCurTypeInstance->mTypeDef;
	auto methodDef = mCurMethodInstance->mMethodDef;

	BfIRBlock exitBB;
	if ((HasCompiledOutput()) && (!mCurMethodInstance->mIsUnspecialized) && (mCurMethodInstance->mChainType != BfMethodChainType_ChainMember))
	{
		auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
		auto didStaticInitVarAddr = mBfIRBuilder->CreateGlobalVariable(
			mBfIRBuilder->MapType(boolType),
			false,
			BfIRLinkageType_Internal,
			GetDefaultValue(boolType),
			"didStaticInit");

		auto initBB = mBfIRBuilder->CreateBlock("init", true);
		mCurMethodState->mIRExitBlock = mBfIRBuilder->CreateBlock("exit", true);

		auto didStaticInitVar = mBfIRBuilder->CreateLoad(didStaticInitVarAddr);
		mBfIRBuilder->CreateCondBr(didStaticInitVar, mCurMethodState->mIRExitBlock, initBB);

		mBfIRBuilder->SetInsertPoint(initBB);
		mBfIRBuilder->CreateStore(GetConstValue(1, boolType), didStaticInitVarAddr);
	}

	// Do DLL imports
	if (!mDllImportEntries.empty())
	{
		auto internalType = ResolveTypeDef(mCompiler->mInternalTypeDef);
		PopulateType(internalType);
		auto getSharedProcAddressInstance = GetMethodByName(internalType->ToTypeInstance(), "GetSharedProcAddress");
		if (!getSharedProcAddressInstance)
		{
			if (!mCompiler->mPassInstance->HasFailed())
				Fail("Internal error: System.Internal doesn't contain LoadSharedLibrary method");
		}
	}

	// Fill in initializer values
	if ((!mCompiler->mIsResolveOnly) || (mCompiler->mResolvePassData->mAutoComplete == NULL))
	{
		for (auto fieldDef : typeDef->mFields)
		{
			if ((!fieldDef->mIsConst) && (fieldDef->mIsStatic))
			{
				// For extensions, only handle these fields in the appropriate extension
				if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
					continue;

				auto initializer = fieldDef->GetInitializer();
				auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldDef->mIdx];
				if (!fieldInst->mFieldIncluded)
					continue;
				if (fieldInst->mResolvedType->IsVar())
				{
					continue;
				}

				if (fieldInst->IsAppendedObject())
				{
					AppendedObjectInit(fieldInst);
				}
				else if (initializer != NULL)
				{
					UpdateSrcPos(initializer);
					GetFieldInitializerValue(fieldInst, NULL, NULL, NULL, true);
				}
			}
		}

		auto _CheckInitBlock = [&](BfAstNode* node)
		{
		};

		EmitInitBlocks(_CheckInitBlock);
	}
	else
	{
		for (auto tempTypeDef : mCompiler->mResolvePassData->mAutoCompleteTempTypes)
		{
			if (tempTypeDef->mFullName == typeDef->mFullName)
			{
				for (auto fieldDef : tempTypeDef->mFields)
				{
					if ((fieldDef->mIsStatic) && (!fieldDef->mIsConst))
					{
						if (fieldDef->GetInitializer() != NULL)
						{
							if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(fieldDef->GetInitializer()))
							{
								sourceClassifier->SetElementType(fieldDef->GetInitializer(), BfSourceElementType_Normal);
								sourceClassifier->VisitChildNoRef(fieldDef->GetInitializer());
							}
							BfType* wantType = NULL;
							if ((!BfNodeIsA<BfVarTypeReference>(fieldDef->mTypeRef)) && (!BfNodeIsA<BfLetTypeReference>(fieldDef->mTypeRef)))
							{
								wantType = ResolveTypeRef(fieldDef->mTypeRef, BfPopulateType_Identity, BfResolveTypeRefFlag_AllowInferredSizedArray);
							}

							BfEvalExprFlags exprFlags = BfEvalExprFlags_FieldInitializer;
							if (fieldDef->mIsAppend)
								exprFlags = (BfEvalExprFlags)(exprFlags | BfEvalExprFlags_AppendFieldInitializer);

							CreateValueFromExpression(fieldDef->GetInitializer(), wantType, exprFlags);
						}
					}
				}
			}
		}
	}

	if (mCurMethodInstance->mChainType == BfMethodChainType_ChainHead)
		CallChainedMethods(mCurMethodInstance, false);
}

void BfModule::EmitDtorBody()
{
	if (!mCurMethodState->mIRExitBlock)
		mCurMethodState->mIRExitBlock = mBfIRBuilder->CreateBlock("exit", true);

	if (mCurTypeInstance->IsClosure())
	{
		BfFieldInstance* fieldInstance = &mCurTypeInstance->mFieldInstances.back();
		BF_ASSERT(fieldInstance->GetFieldDef()->mName == "__dtorThunk");
		auto thisVal = GetThis();
		auto dtorThunkPtr = mBfIRBuilder->CreateInBoundsGEP(thisVal.mValue, 0, fieldInstance->mDataIdx);
		auto dtorThunk = mBfIRBuilder->CreateLoad(dtorThunkPtr);

		auto funcPtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapMethod(mCurMethodInstance));
		auto dtorPtr = mBfIRBuilder->CreateBitCast(dtorThunk, funcPtrType);

		SizedArray<BfIRValue, 1> args;
		args.push_back(thisVal.mValue);
		auto result = mBfIRBuilder->CreateCall(dtorPtr, args);
		mBfIRBuilder->SetCallCallingConv(result, BfIRCallingConv_CDecl);

		// Fall through to Object::~this call

		auto dtorFunc = GetMethodByName(mContext->mBfObjectType, "~this");
		if (mIsComptimeModule)
			mCompiler->mCeMachine->QueueMethod(dtorFunc.mMethodInstance, dtorFunc.mFunc);
		auto basePtr = mBfIRBuilder->CreateBitCast(thisVal.mValue, mBfIRBuilder->MapTypeInstPtr(mContext->mBfObjectType));
		SizedArray<BfIRValue, 1> vals = { basePtr };
		result = mBfIRBuilder->CreateCall(dtorFunc.mFunc, vals);
		mBfIRBuilder->SetCallCallingConv(result, GetIRCallingConvention(dtorFunc.mMethodInstance));
		mBfIRBuilder->SetTailCall(result);

		return;
	}

	auto typeDef = mCurTypeInstance->mTypeDef;
	auto methodDef = mCurMethodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();

	if (mCurMethodInstance->mChainType == BfMethodChainType_ChainHead)
		CallChainedMethods(mCurMethodInstance, true);

	if (auto bodyBlock = BfNodeDynCast<BfBlock>(methodDef->mBody))
	{
		VisitEmbeddedStatement(bodyBlock);
		if (bodyBlock->mCloseBrace != NULL)
		{
			UpdateSrcPos(bodyBlock->mCloseBrace);
		}
	}
	else
	{
		if (methodDeclaration != NULL)
			UpdateSrcPos(methodDeclaration);
		else if ((methodDef->mDeclaringType != NULL) && (methodDef->mDeclaringType->GetRefNode() != NULL))
			UpdateSrcPos(methodDef->mDeclaringType->GetRefNode());
		else if (typeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(typeDef->mTypeDeclaration);
		if ((methodDeclaration != NULL) && (methodDeclaration->mFatArrowToken != NULL))
		{
			Fail("Destructors cannot have expression bodies", methodDeclaration->mFatArrowToken, true);
		}
	}

	if ((!mCompiler->mIsResolveOnly) || (mCompiler->mResolvePassData->mAutoComplete == NULL))
	{
		for (int fieldIdx = (int)mCurTypeInstance->mFieldInstances.size() - 1; fieldIdx >= 0; fieldIdx--)
		{
			auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldIdx];
			auto fieldDef = fieldInst->GetFieldDef();

			BfFieldDeclaration* fieldDecl = NULL;
			if (fieldDef != NULL)
				fieldDecl = fieldDef->GetFieldDeclaration();

			if ((fieldDef != NULL) && (fieldDef->mIsStatic == methodDef->mIsStatic) && (fieldDecl != NULL) && (fieldDecl->mFieldDtor != NULL))
			{
				if (fieldDef->mDeclaringType != mCurMethodInstance->mMethodDef->mDeclaringType)
				{
					BF_ASSERT(mCurTypeInstance->mTypeDef->mIsCombinedPartial);
					continue;
				}

				if ((!methodDef->mIsStatic) && (mCurTypeInstance->IsValueType()))
				{
					Fail("Structs cannot have field destructors", fieldDecl->mFieldDtor->mTildeToken, true);
				}

				SetAndRestoreValue<BfFilePosition> prevFilePos(mCurFilePosition);

				auto fieldDtor = fieldDecl->mFieldDtor;

				if ((fieldDef->mIsStatic) != (methodDef->mIsStatic))
					continue;

				UpdateSrcPos(fieldDtor);

				BfScopeData scopeData;
				mCurMethodState->AddScope(&scopeData);
				NewScopeState();
				UpdateSrcPos(fieldDtor);

				bool hasDbgInfo = (!fieldDef->mIsConst);

				if (hasDbgInfo)
				{
					mBfIRBuilder->SaveDebugLocation();
					mBfIRBuilder->ClearDebugLocation();
				}

				BfIRValue value;
				if (fieldDef->mIsStatic)
				{
					value = ReferenceStaticField(fieldInst).mValue;
				}
				else
				{
					PopulateType(fieldInst->mResolvedType);
					if (fieldInst->mResolvedType->IsValuelessType())
					{
						value = mBfIRBuilder->GetFakeVal();
					}
					else if (!mCurTypeInstance->IsValueType())
					{
						auto thisValue = GetThis();
						value = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInst->mDataIdx);
					}
					else
					{
						AssertErrorState();
						value = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(fieldInst->mResolvedType));
					}
				}

				BfIRValue staticVal;

				if (hasDbgInfo)
				{
					BfIRValue dbgShowValue = value;

					BfLocalVariable* localDef = new BfLocalVariable();
					localDef->mName = "_";
					localDef->mResolvedType = fieldInst->mResolvedType;

					if (fieldInst->IsAppendedObject())
					{
						localDef->mValue = mBfIRBuilder->CreateBitCast(value, mBfIRBuilder->MapType(fieldInst->mResolvedType));
					}
					else
					{
						localDef->mAddr = value;
						if ((mBfIRBuilder->DbgHasInfo()) && (!IsTargetingBeefBackend()))
						{
							// Create another pointer indirection, a ref to the gep
							auto refFieldType = CreateRefType(fieldInst->mResolvedType);

							auto allocaInst = CreateAlloca(refFieldType);

							auto storeResult = mBfIRBuilder->CreateStore(value, allocaInst);

							localDef->mResolvedType = refFieldType;
							localDef->mAddr = allocaInst;
						}
					}

					mBfIRBuilder->RestoreDebugLocation();

					auto defLocalVar = AddLocalVariableDef(localDef, true);

					if (!fieldInst->IsAppendedObject())
					{
						// Put back so we actually modify the correct value*/
						defLocalVar->mResolvedType = fieldInst->mResolvedType;
						defLocalVar->mAddr = value;
					}
				}

				while (fieldDtor != NULL)
				{
					if (mCompiler->mResolvePassData != NULL)
					{
						if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(fieldDtor))
						{
							sourceClassifier->SetElementType(fieldDtor, BfSourceElementType_Normal);
							sourceClassifier->VisitChild(fieldDtor);
						}
					}

					UpdateSrcPos(fieldDtor);
					VisitEmbeddedStatement(fieldDtor->mBody);
					fieldDtor = fieldDtor->mNextFieldDtor;
				}

				RestoreScopeState();
			}

			if ((fieldDef != NULL) && (fieldDef->mIsStatic == methodDef->mIsStatic) && (fieldInst->IsAppendedObject()))
			{
				if (fieldDef->mDeclaringType != mCurMethodInstance->mMethodDef->mDeclaringType)
				{
					BF_ASSERT(mCurTypeInstance->mTypeDef->mIsCombinedPartial);
					continue;
				}

				auto refNode = fieldDef->GetRefNode();
				UpdateSrcPos(refNode);

				auto objectType = mContext->mBfObjectType;
				BfTypeInstance* checkTypeInst = mCurTypeInstance->ToTypeInstance();

				BfTypedValue val;
				if (fieldDef->mIsStatic)
					val = ReferenceStaticField(fieldInst);
				else
				{
					auto fieldAddr = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, fieldInst->mDataIdx);
					val = BfTypedValue(mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(fieldInst->mResolvedType)), fieldInst->mResolvedType);
				}

				bool allowPrivate = checkTypeInst == mCurTypeInstance;
				bool allowProtected = allowPrivate || TypeIsSubTypeOf(mCurTypeInstance, checkTypeInst);
				while (checkTypeInst != NULL)
				{
					auto dtorMethodDef = checkTypeInst->mTypeDef->GetMethodByName("~this");
					if (dtorMethodDef)
					{
						if (!CheckProtection(dtorMethodDef->mProtection, checkTypeInst->mTypeDef, allowProtected, allowPrivate))
						{
							auto error = Fail(StrFormat("'%s.~this()' is inaccessible due to its protection level", TypeToString(checkTypeInst).c_str()), refNode); // CS0122
						}
					}
					checkTypeInst = checkTypeInst->mBaseType;
					allowPrivate = false;
				}

				// call dtor
				BfExprEvaluator expressionEvaluator(this);
				PopulateType(val.mType);
				PopulateType(objectType, BfPopulateType_DataAndMethods);

				if (objectType->mVirtualMethodTable.size() == 0)
				{
					if (!mCompiler->IsAutocomplete())
						AssertErrorState();
				}
				else if (!IsSkippingExtraResolveChecks())
				{
					BfMethodInstance* methodInstance = objectType->mVirtualMethodTable[mCompiler->GetVTableMethodOffset() + 0].mImplementingMethod;
					BF_ASSERT(methodInstance->mMethodDef->mName == "~this");
					SizedArray<BfIRValue, 4> llvmArgs;
					llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(objectType)));
					expressionEvaluator.CreateCall(refNode, methodInstance, mBfIRBuilder->GetFakeVal(), false, llvmArgs);
				}

				if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
				{
					auto int8PtrType = CreatePointerType(GetPrimitiveType(BfTypeCode_Int8));
					auto int8PtrVal = mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(int8PtrType));
					mBfIRBuilder->CreateStore(GetConstValue8(BfObjectFlag_Deleted), int8PtrVal);
				}
			}
		}

		if ((!methodDef->mIsStatic) && (mCurMethodInstance->mChainType != BfMethodChainType_ChainMember))
		{
			auto checkBaseType = mCurTypeInstance->mBaseType;
			while (checkBaseType != NULL)
			{
				if (auto bodyBlock = BfNodeDynCast<BfBlock>(methodDef->mBody))
				{
					if (bodyBlock->mCloseBrace != NULL)
						UpdateSrcPos(bodyBlock->mCloseBrace);
				}
				else
				{
					UpdateSrcPos(typeDef->mTypeDeclaration->mNameNode);
				}

				BfMethodDef* dtorMethodDef = checkBaseType->mTypeDef->GetMethodByName("~this");
				if (dtorMethodDef != NULL)
				{
					auto dtorMethodInstance = GetMethodInstance(checkBaseType, dtorMethodDef, BfTypeVector());
					if (IsSkippingExtraResolveChecks())
					{
						// Nothing
					}
					else if (dtorMethodInstance.mMethodInstance->GetParamCount() == 0)
					{
						if ((!IsSkippingExtraResolveChecks()) && (!checkBaseType->IsUnspecializedType()))
						{
							auto basePtr = mBfIRBuilder->CreateBitCast(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->MapTypeInstPtr(checkBaseType));
							SizedArray<BfIRValue, 1> vals = { basePtr };
							auto callInst = mBfIRBuilder->CreateCall(dtorMethodInstance.mFunc, vals);
							if (mIsComptimeModule)
								mCompiler->mCeMachine->QueueMethod(dtorMethodInstance.mMethodInstance, dtorMethodInstance.mFunc);
							mBfIRBuilder->SetCallCallingConv(callInst, GetIRCallingConvention(dtorMethodInstance.mMethodInstance));
						}
					}
					else
					{
						// Invalid base dtor declaration
						AssertErrorState();
					}
					break;
				}
				checkBaseType = checkBaseType->mBaseType;
			}
		}

		EmitLifetimeEnds(&mCurMethodState->mHeadScope);
	}
	else
	{
		// The reason we can't just do the 'normal' path for this is that the BfTypeInstance here is NOT the
		//  autocomplete type instance, so FieldInstance initializer values contain expressions from the full
		//  resolve pass, NOT the autocomplete expression
		for (auto tempTypeDef : mCompiler->mResolvePassData->mAutoCompleteTempTypes)
		{
			if (tempTypeDef->mFullName == typeDef->mFullName)
			{
				for (auto fieldDef : tempTypeDef->mFields)
				{
					auto fieldDecl = fieldDef->GetFieldDeclaration();

					if ((fieldDef->mIsStatic == methodDef->mIsStatic) && (fieldDef->mFieldDeclaration != NULL) &&
						(fieldDecl->mFieldDtor != NULL) && (mCompiler->mResolvePassData->mIsClassifying))
					{
						BfType* fieldType = NULL;

						for (int curFieldIdx = 0; curFieldIdx < (int)mCurTypeInstance->mFieldInstances.size(); curFieldIdx++)
						{
							auto curFieldInstance = &mCurTypeInstance->mFieldInstances[curFieldIdx];
							auto curFieldDef = curFieldInstance->GetFieldDef();
							if ((curFieldDef != NULL) && (fieldDef->mName == curFieldDef->mName))
								fieldType = curFieldInstance->GetResolvedType();
						}
						if (fieldType == NULL)
							fieldType = GetPrimitiveType(BfTypeCode_Var);

						auto fieldDtor = fieldDecl->mFieldDtor;

						BfScopeData scopeData;
						mCurMethodState->AddScope(&scopeData);
						NewScopeState();

						// This is just for autocomplete, it doesn't matter that mAddr is incorrect
						if (fieldType != NULL)
						{
							BfLocalVariable* localDef = new BfLocalVariable();
							localDef->mName = "_";
							localDef->mResolvedType = fieldType;
							localDef->mAddr = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(fieldType));
							localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
							AddLocalVariableDef(localDef);
						}

						while (fieldDtor != NULL)
						{
							if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(fieldDtor))
							{
								sourceClassifier->SetElementType(fieldDtor, BfSourceElementType_Normal);
								sourceClassifier->VisitChild(fieldDtor);
							}

							UpdateSrcPos(fieldDtor);
							VisitEmbeddedStatement(fieldDtor->mBody);
							fieldDtor = fieldDtor->mNextFieldDtor;
						}

						RestoreScopeState();
					}
				}
			}
		}
	}
}

BfIRValue BfModule::CreateDllImportGlobalVar(BfMethodInstance* methodInstance, bool define)
{
	BF_ASSERT(methodInstance->mIsReified);

	auto typeInstance = methodInstance->GetOwner();

	bool foundDllImportAttr = false;
	BfCallingConvention callingConvention = methodInstance->mCallingConvention;
	for (auto customAttr : methodInstance->GetCustomAttributes()->mAttributes)
	{
		if (customAttr.mType->mTypeDef->mFullName.ToString() == "System.ImportAttribute")
		{
			foundDllImportAttr = true;
		}
	}
	if (!foundDllImportAttr)
	{
		AssertErrorState();
		return BfIRValue();
	}

	StringT<512> name = "bf_hs_preserve@";
	BfMangler::Mangle(name, mCompiler->GetMangleKind(), methodInstance);
	name += "__imp";

	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	methodInstance->GetIRFunctionInfo(this, returnType, paramTypes);

	BfIRFunctionType externFunctionType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes, methodInstance->IsVarArgs());
	auto ptrType = mBfIRBuilder->GetPointerTo(externFunctionType);

	BfIRValue initVal;
	if (define)
	{
		if (methodInstance->GetImportCallKind() != BfImportCallKind_None)
			initVal = mBfIRBuilder->CreateConstNull(ptrType);
	}

	auto globalVar = mBfIRBuilder->CreateGlobalVariable(ptrType, false, BfIRLinkageType_External, initVal, name);

	if ((define) && (mBfIRBuilder->DbgHasInfo()))
	{
		auto voidType = GetPrimitiveType(BfTypeCode_None);
		auto ptrType = CreatePointerType(voidType);
		mBfIRBuilder->DbgCreateGlobalVariable(mDICompileUnit, name, name, BfIRMDNode(), 0, mBfIRBuilder->DbgGetType(ptrType), false, globalVar);
	}

	return globalVar;
}

void BfModule::CreateDllImportMethod()
{
	if (mBfIRBuilder->mIgnoreWrites)
		return;

	auto globalVar = mFuncReferences[mCurMethodInstance];
	if (!globalVar)
		return;

	bool allowTailCall = true;

	mBfIRBuilder->ClearDebugLocation();

	bool isHotCompile = mCompiler->IsHotCompile();

	// If we are hot swapping, we need to have this stub because we may need to call the LoadSharedLibraries on demand
	if (isHotCompile)
	{
		auto loadSharedLibsFunc = GetBuiltInFunc(BfBuiltInFuncType_LoadSharedLibraries);
		mBfIRBuilder->CreateCall(loadSharedLibsFunc, SizedArray<BfIRValue, 0>());
	}

	auto callingConvention = GetIRCallingConvention(mCurMethodInstance);

	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	mCurMethodInstance->GetIRFunctionInfo(this, returnType, paramTypes, mCurMethodInstance->IsVarArgs());

	SizedArray<BfIRValue, 8> args;

	for (int i = 0; i < (int)paramTypes.size(); i++)
		args.push_back(mBfIRBuilder->GetArgument(i));

	BfIRFunctionType externFunctionType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes, mCurMethodInstance->IsVarArgs());

	if (isHotCompile)
	{
		BfIRValue funcVal = mBfIRBuilder->CreateLoad(globalVar);
		auto result = mBfIRBuilder->CreateCall(funcVal, args);
		if (callingConvention == BfIRCallingConv_StdCall)
			mBfIRBuilder->SetCallCallingConv(result, BfIRCallingConv_StdCall);
		else if (callingConvention == BfIRCallingConv_FastCall)
			mBfIRBuilder->SetCallCallingConv(result, BfIRCallingConv_FastCall);
		else
			mBfIRBuilder->SetCallCallingConv(result, BfIRCallingConv_CDecl);
		if (allowTailCall)
			mBfIRBuilder->SetTailCall(result);
		CreateReturn(result);
		mCurMethodState->mHadReturn = true;
	}

	if (HasCompiledOutput())
	{
		BfDllImportEntry dllImportEntry;
		dllImportEntry.mFuncVar = globalVar;
		dllImportEntry.mMethodInstance = mCurMethodInstance;
		mDllImportEntries.push_back(dllImportEntry);
	}
}

BfIRCallingConv BfModule::GetIRCallingConvention(BfMethodInstance* methodInstance)
{
	auto methodDef = methodInstance->mMethodDef;
	BfTypeInstance* owner = NULL;
	if (!methodDef->mIsStatic)
		owner = methodInstance->GetThisType()->ToTypeInstance();
	if (owner == NULL)
		owner = methodInstance->GetOwner();

	if ((mCompiler->mOptions.mMachineType != BfMachineType_x86) || (mCompiler->mOptions.mPlatformType != BfPlatformType_Windows))
		return BfIRCallingConv_CDecl;
	if (methodInstance->mCallingConvention == BfCallingConvention_Stdcall)
		return BfIRCallingConv_StdCall;
	if (methodInstance->mCallingConvention == BfCallingConvention_Fastcall)
		return BfIRCallingConv_FastCall;
	if (!methodDef->mIsStatic)
	{
		if (owner->mIsCRepr)
			return BfIRCallingConv_ThisCall;
		if ((!owner->IsValuelessType()) &&
			((!owner->IsSplattable()) || (methodDef->HasNoThisSplat())))
			return BfIRCallingConv_ThisCall;
	}
	return BfIRCallingConv_CDecl;

	//return GetIRCallingConvention(owner, methodInstance->mMethodDef);
}

void BfModule::SetupIRMethod(BfMethodInstance* methodInstance, BfIRFunction func, bool isInlined)
{
	BfMethodDef* methodDef = NULL;
	if (methodInstance != NULL)
		methodDef = methodInstance->mMethodDef;

	if (!func)
		return;

	if (mCompiler->mOptions.mNoFramePointerElim)
		mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_NoFramePointerElim);
	mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_NoUnwind);
	if (mSystem->mPtrSize == 8) // We need unwind info for debugging
		mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_UWTable);

	if (methodInstance == NULL)
		return;

	if (methodInstance->mReturnType->IsVar())
		mBfIRBuilder->Func_AddAttribute(func, -1, BfIRAttribute_VarRet);
	if (methodDef->mImportKind == BfImportKind_Export)
	{
		if (methodDef->mDeclaringType->mProject->mTargetType != BfTargetType_BeefLib_StaticLib)
			mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_DllExport);
	}
	if (methodDef->mIsNoReturn)
		mBfIRBuilder->Func_AddAttribute(func, -1, BfIRAttribute_NoReturn);
	auto callingConv = GetIRCallingConvention(methodInstance);
	if (callingConv != BfIRCallingConv_CDecl)
		mBfIRBuilder->SetFuncCallingConv(func, callingConv);

	if (isInlined)
	{
		mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_AlwaysInline);
	}

	int argIdx = 0;
	int paramIdx = 0;

	if ((methodInstance->HasThis()) && (!methodDef->mHasExplicitThis))
		paramIdx = -1;

	int argCount = methodInstance->GetIRFunctionParamCount(this);

	while (argIdx < argCount)
	{
		if ((!mIsComptimeModule) && (argIdx == methodInstance->GetStructRetIdx()))
		{
			mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_NoAlias);
			mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_StructRet);
			argIdx++;
			continue;
		}

		bool isThis = (paramIdx == -1) || ((methodDef->mHasExplicitThis) && (paramIdx == 0));
		while ((!isThis) && (methodInstance->IsParamSkipped(paramIdx)))
			paramIdx++;

		BfType* resolvedTypeRef = NULL;
		BfType* resolvedTypeRef2 = NULL;
		String paramName;
		bool isSplattable = false;
		bool tryLowering = !mIsComptimeModule;
		if (isThis)
		{
			paramName = "this";
			if (methodInstance->mIsClosure)
				resolvedTypeRef = mCurMethodState->mClosureState->mClosureType;
			else
				resolvedTypeRef = methodInstance->GetThisType();
			isSplattable = (!mIsComptimeModule) && (resolvedTypeRef->IsSplattable()) && (methodInstance->AllowsSplatting(-1));
			tryLowering = (!mIsComptimeModule) && (methodInstance->AllowsSplatting(-1));
        }
		else
		{
			paramName = methodInstance->GetParamName(paramIdx);
        	resolvedTypeRef = methodInstance->GetParamType(paramIdx);
			if (resolvedTypeRef->IsMethodRef())
				isSplattable = true;
			else if ((!mIsComptimeModule) && (resolvedTypeRef->IsSplattable()) && (methodInstance->AllowsSplatting(paramIdx)))
			{
				auto resolvedTypeInst = resolvedTypeRef->ToTypeInstance();
				if ((resolvedTypeInst != NULL) && (resolvedTypeInst->mIsCRepr))
				{
					// crepr splat is always splattable
					isSplattable = true;
				}
				else
				{
					auto resolvedTypeInst = resolvedTypeRef->ToTypeInstance();
					if ((resolvedTypeInst != NULL) && (resolvedTypeInst->mIsCRepr))
						isSplattable = true;
					else if (resolvedTypeRef->GetSplatCount() + argIdx <= mCompiler->mOptions.mMaxSplatRegs)
						isSplattable = true;
				}
			}
		}

		if (tryLowering)
		{
			BfTypeCode loweredTypeCode = BfTypeCode_None;
			BfTypeCode loweredTypeCode2 = BfTypeCode_None;
			if (resolvedTypeRef->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
			{
				mBfIRBuilder->Func_SetParamName(func, argIdx + 1, paramName + "__1");
				argIdx++;

				if (loweredTypeCode2 != BfTypeCode_None)
				{
					mBfIRBuilder->Func_SetParamName(func, argIdx + 1, paramName + "__2");
					argIdx++;
				}

				paramIdx++;
				continue;
			}
		}

		auto _SetupParam = [&](const StringImpl& paramName, BfType* resolvedTypeRef)
		{
			mBfIRBuilder->Func_SetParamName(func, argIdx + 1, paramName);

			int addDeref = -1;
			if (resolvedTypeRef->IsRef())
			{
				auto refType = (BfRefType*)resolvedTypeRef;
				auto elementType = refType->mElementType;
				PopulateType(elementType, BfPopulateType_Data);
				addDeref = elementType->mSize;
				if ((addDeref <= 0) && (!elementType->IsValuelessType()) && (!elementType->IsOpaque()))
					AssertErrorState();
			}
			if ((resolvedTypeRef->IsComposite()) && (!resolvedTypeRef->IsTypedPrimitive()))
			{
				if (isThis)
				{
					mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_NoCapture);
					PopulateType(resolvedTypeRef, BfPopulateType_Data);
					addDeref = resolvedTypeRef->mSize;
				}
				else if (methodInstance->WantsStructsAttribByVal(resolvedTypeRef))
				{
					mBfIRBuilder->PopulateType(resolvedTypeRef, BfIRPopulateType_Full);
					BF_ASSERT(resolvedTypeRef->mAlign > 0);
					mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_ByVal, mSystem->mPtrSize);
				}
			}
			else if (resolvedTypeRef->IsPrimitiveType())
			{
				auto primType = (BfPrimitiveType*)resolvedTypeRef;
				if (primType->mTypeDef->mTypeCode == BfTypeCode_Boolean)
					mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_ZExt);
			}

			if (addDeref >= 0)
				mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_Dereferencable, addDeref);

			argIdx++;
		};

		if (isSplattable)
		{
			std::function<void(BfType*, const StringImpl&)> checkTypeLambda = [&](BfType* checkType, const StringImpl& curName)
			{
				if (checkType->IsStruct())
				{
					auto checkTypeInstance = checkType->ToTypeInstance();
					if (checkTypeInstance->mBaseType != NULL)
						checkTypeLambda(checkTypeInstance->mBaseType, curName);

					if (checkTypeInstance->mIsUnion)
					{
						BfType* unionInnerType = checkTypeInstance->GetUnionInnerType();
						checkTypeLambda(unionInnerType, curName + "_data");
					}
					else
					{
						for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
							auto fieldDef = fieldInstance->GetFieldDef();

							if (fieldInstance->mDataIdx >= 0)
							{
								checkTypeLambda(fieldInstance->GetResolvedType(), curName + "_" + fieldInstance->GetFieldDef()->mName);
							}
						}
					}

					if (checkTypeInstance->IsEnum())
					{
						mBfIRBuilder->Func_SetParamName(func, argIdx + 1, curName + "_dscr");
						argIdx++;
					}
				}
				else if (checkType->IsMethodRef())
				{
					auto methodRefType = (BfMethodRefType*)checkType;

					BfMethodInstance* methodRefMethodInst = methodRefType->mMethodRef;
					for (int dataIdx = 0; dataIdx < (int)methodRefType->GetCaptureDataCount(); dataIdx++)
					{
						int methodRefParamIdx = methodRefType->GetParamIdxFromDataIdx(dataIdx);
						if (methodRefType->WantsDataPassedAsSplat(dataIdx))
						{
							checkTypeLambda(methodRefType->GetCaptureType(dataIdx), curName + "_" + methodRefMethodInst->GetParamName(methodRefParamIdx));
						}
						else
						{
							_SetupParam(curName + "_" + methodRefMethodInst->GetParamName(methodRefParamIdx), methodRefType->GetCaptureType(dataIdx));
						}
					}
				}
				else if (!checkType->IsValuelessType())
				{
					_SetupParam(curName, checkType);
				}
			};

			checkTypeLambda(resolvedTypeRef, paramName);
			paramIdx++;
			continue;
		}

		_SetupParam(paramName, resolvedTypeRef);
		if (resolvedTypeRef2 != NULL)
			_SetupParam(paramName, resolvedTypeRef2);

		paramIdx++;
	}
}

void BfModule::EmitInitBlocks(const std::function<void(BfAstNode*)>& initBlockCallback)
{
	auto methodDef = mCurMethodInstance->mMethodDef;

	mCurTypeInstance->mTypeDef->PopulateMemberSets();
	BfMemberSetEntry* entry = NULL;
	BfMethodDef* initMethodDef = NULL;
	mCurTypeInstance->mTypeDef->mMethodSet.TryGetWith(String("__BfInit"), &entry);
	if (entry != NULL)
		initMethodDef = (BfMethodDef*)entry->mMemberDef;

	SizedArray<BfAstNode*, 8> initBodies;

	for (; initMethodDef != NULL; initMethodDef = initMethodDef->mNextWithSameName)
	{
		if (initMethodDef->mDeclaringType->GetDefinition() != methodDef->mDeclaringType->GetDefinition())
			continue;
		if (initMethodDef->mMethodType != BfMethodType_Init)
			continue;
		if (initMethodDef->mIsStatic != methodDef->mIsStatic)
			continue;
		initBodies.Insert(0, initMethodDef->mBody);
	}

	for (auto body : initBodies)
	{
		initBlockCallback(body);
		VisitEmbeddedStatement(body);
	}
}

void BfModule::EmitCtorBody(bool& skipBody)
{
	auto methodInstance = mCurMethodInstance;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->mMethodDeclaration;
	auto ctorDeclaration = (BfConstructorDeclaration*)methodDef->mMethodDeclaration;
	auto typeDef = mCurTypeInstance->mTypeDef;

	BfCustomAttributes* customAttributes = NULL;
	defer(delete customAttributes);
	BfInvocationExpression* ctorInvocation = NULL;

	BfAttributeState attributeState;
	attributeState.mTarget = BfAttributeTargets_MemberAccess;
	if (ctorDeclaration != NULL)
	{
		ctorInvocation = BfNodeDynCast<BfInvocationExpression>(ctorDeclaration->mInitializer);
		if (auto attributedExpr = BfNodeDynCast<BfAttributedExpression>(ctorDeclaration->mInitializer))
		{
			ctorInvocation = BfNodeDynCast<BfInvocationExpression>(attributedExpr->mExpression);
			attributeState.mCustomAttributes = GetCustomAttributes(attributedExpr->mAttributes, attributeState.mTarget);
		}
	}

	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mAttributeState, &attributeState);

	// Prologue
	mBfIRBuilder->ClearDebugLocation();

	bool hadThisInitializer = false;
	if ((ctorDeclaration != NULL) && (ctorInvocation != NULL))
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorInvocation->mTarget);
		auto targetType = (targetToken->GetToken() == BfToken_This) ? mCurTypeInstance : mCurTypeInstance->mBaseType;
		if (targetToken->GetToken() == BfToken_This)
		{
			hadThisInitializer = true;
			// Since we call a 'this', it's that other ctor's responsibility to fully assign the fields
			mCurMethodState->LocalDefined(GetThisVariable());
		}
	}

	// Zero out memory for default ctor
	if ((methodDeclaration == NULL) && (mCurTypeInstance->IsStruct()) && (methodInstance->mChainType != BfMethodChainType_ChainMember))
	{
		if (mCurTypeInstance->IsTypedPrimitive())
		{
			auto thisRef = mCurMethodState->mLocals[0]->mValue;
			mBfIRBuilder->CreateStore(GetDefaultValue(mCurTypeInstance), thisRef);
		}
		else if (mCurTypeInstance->mInstSize > 0)
		{
			BfIRValue fillValue = GetConstValue8(0);
			BfIRValue sizeValue = GetConstValue(mCurTypeInstance->mInstSize);
			auto thisRef = mCurMethodState->mLocals[0]->mValue;
			BfIRValue destBits = mBfIRBuilder->CreateBitCast(thisRef, mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
			mBfIRBuilder->CreateMemSet(destBits, fillValue, sizeValue, mCurTypeInstance->mAlign);
		}
	}

	BfAstNode* baseCtorNode = NULL;
	if ((ctorDeclaration != NULL) && (ctorInvocation != NULL) && (ctorInvocation->mTarget != NULL))
		baseCtorNode = ctorInvocation->mTarget;
	else if (methodDef->mBody != NULL)
		baseCtorNode = methodDef->mBody;
	else if (ctorDeclaration != NULL)
		baseCtorNode = ctorDeclaration;
	else if ((methodDef->mDeclaringType != NULL) && (methodDef->mDeclaringType->GetRefNode() != NULL))
		baseCtorNode = methodDef->mDeclaringType->GetRefNode();
	else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
		baseCtorNode = mCurTypeInstance->mTypeDef->mTypeDeclaration->mNameNode;
	else if ((mCurTypeInstance->mBaseType != NULL) && (mCurTypeInstance->mBaseType->mTypeDef->mTypeDeclaration != NULL))
		baseCtorNode = mCurTypeInstance->mBaseType->mTypeDef->mTypeDeclaration;
	else
		baseCtorNode = mContext->mBfObjectType->mTypeDef->mTypeDeclaration;

	bool calledCtorNoBody = false;

	if ((mCurTypeInstance->IsTypedPrimitive()) && (!mCurTypeInstance->IsValuelessType()))
	{
		// Zero out typed primitives in ctor
		mBfIRBuilder->CreateAlignedStore(GetDefaultValue(mCurTypeInstance->GetUnderlyingType()), mBfIRBuilder->GetArgument(0), mCurTypeInstance->mAlign);
	}

	if ((!mCurTypeInstance->IsBoxed()) && (methodDef->mMethodType == BfMethodType_Ctor) && (!hadThisInitializer))
	{
		// Call the root type's default ctor (with no body) to initialize its fields and call the chained ctors
		if (mCurTypeInstance->mTypeDef->mHasCtorNoBody)
		{
			BfMethodDef* defaultCtor = NULL;

		 	for (auto methodDef : mCurTypeInstance->mTypeDef->mMethods)
		 	{
		 		if ((methodDef->mMethodType == BfMethodType_CtorNoBody)	&& (methodDef->mDeclaringType->mTypeCode != BfTypeCode_Extension))
		 		{
					defaultCtor = methodDef;
		 		}
		 	}

			if (defaultCtor != NULL)
			{
				UpdateSrcPos(mCurTypeInstance->mTypeDef->GetRefNode());
				SetIllegalSrcPos();

				auto moduleMethodInstance = GetMethodInstance(mCurTypeInstance, defaultCtor, BfTypeVector());

				BfExprEvaluator exprEvaluator(this);

				SizedArray<BfIRValue, 1> irArgs;
				exprEvaluator.PushThis(NULL, GetThis(), moduleMethodInstance.mMethodInstance, irArgs);
				exprEvaluator.CreateCall(NULL, moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, irArgs);
				if (mIsComptimeModule)
					mCompiler->mCeMachine->QueueMethod(moduleMethodInstance);

				calledCtorNoBody = true;
			}
		}
	}

	// Initialize fields (if applicable)
	if (mCurTypeInstance->IsBoxed())
	{
		skipBody = true;
	}
	else if ((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mDeclaringType->mTypeCode == BfTypeCode_Extension))
	{
		// Field initializers occur in CtorNoBody methods for extensions
	}
	else if (!hadThisInitializer)
	{
		// If we had a 'this' initializer, that other ctor will have initialized our fields

		//auto

		if ((!mCompiler->mIsResolveOnly) ||
			(mCompiler->mResolvePassData->mAutoComplete == NULL) ||
			(mCompiler->mResolvePassData->mAutoComplete->mResolveType == BfResolveType_ShowFileSymbolReferences))
		{
			// If we calledCtorNoBody then we did the field initializer code, but we still need to run though it here
			//  to properly set the assigned flags
			SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mBfIRBuilder->mIgnoreWrites || calledCtorNoBody);

			bool hadInlineInitBlock = false;
			BfScopeData scopeData;
			scopeData.mInInitBlock = true;

			auto _CheckInitBlock = [&](BfAstNode* node)
			{
				if (!hadInlineInitBlock)
				{
					if ((mBfIRBuilder->DbgHasInfo()) && (!mCurTypeInstance->IsUnspecializedType()) && (mHasFullDebugInfo))
					{
						UpdateSrcPos(baseCtorNode);

						if (methodDef->mBody == NULL)
							SetIllegalSrcPos();

						// NOP so we step onto the open paren of the method so we can choose to set into the field initializers or step over them
						EmitEnsureInstructionAt();

						BfType* thisType = mCurTypeInstance;
						if (thisType->IsValueType())
							thisType = CreateRefType(thisType);

						SizedArray<BfIRMDNode, 1> diParamTypes;
						BfIRMDNode diFuncType = mBfIRBuilder->DbgCreateSubroutineType(diParamTypes);

						mCurMethodState->AddScope(&scopeData);
						NewScopeState();

						int flags = 0;
						mCurMethodState->mCurScope->mDIInlinedAt = mBfIRBuilder->DbgGetCurrentLocation();
						BF_ASSERT(mCurMethodState->mCurScope->mDIInlinedAt);
						// mCurMethodState->mCurScope->mDIInlinedAt may still be null ifwe don't have an explicit ctor

						String linkageName;
						if ((mIsComptimeModule) && (mCompiler->mCeMachine->mCurBuilder != NULL))
							linkageName = StrFormat("%d", mCompiler->mCeMachine->mCurBuilder->DbgCreateMethodRef(mCurMethodInstance, "$initFields"));
						mCurMethodState->mCurScope->mDIScope = mBfIRBuilder->DbgCreateFunction(mBfIRBuilder->DbgGetTypeInst(mCurTypeInstance), "this$initFields", linkageName, mCurFilePosition.mFileInstance->mDIFile,
							mCurFilePosition.mCurLine + 1, diFuncType, false, true, mCurFilePosition.mCurLine + 1, flags, false, BfIRValue());

						UpdateSrcPos(node);

						auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
							"this", mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(thisType));

						//
						{
							auto loadedThis = GetThis();

							// LLVM can't handle two variables pointing to the same value
							BfIRValue copiedThisPtr;
							copiedThisPtr = CreateAlloca(thisType);
							auto storeInst = mBfIRBuilder->CreateStore(loadedThis.mValue, copiedThisPtr);
							mBfIRBuilder->ClearDebugLocation(storeInst);
							mBfIRBuilder->DbgInsertDeclare(copiedThisPtr, diVariable);
						}

						hadInlineInitBlock = true;
					}
					else
					{
						// Just do a simple one
						mCurMethodState->AddScope(&scopeData);
						NewScopeState();
						hadInlineInitBlock = true;
					}
				}

				UpdateSrcPos(node);
			};

			for (auto fieldDef : typeDef->mFields)
			{
				// For extensions, only handle these fields in the appropriate extension
				if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
					continue;

				if ((!fieldDef->mIsConst) && (!fieldDef->mIsStatic))
				{
					auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldDef->mIdx];
					if (!fieldInst->mFieldIncluded)
						continue;
					if (fieldInst->mDataIdx < 0)
						continue;
					auto initializer = fieldDef->GetInitializer();

					if (fieldInst->IsAppendedObject())
					{
						UpdateSrcPos(fieldDef->GetNameNode());
						AppendedObjectInit(fieldInst);
						continue;
					}

					if (initializer == NULL)
					{
						continue;

//  					if (fieldDef->mProtection != BfProtection_Hidden)
//  						continue;
// 						if (mCurTypeInstance->IsObject()) // Already zeroed out
// 							continue;
					}

					if (fieldInst->mResolvedType == NULL)
					{
						BF_ASSERT(mHadBuildError);
						continue;
					}

					if (initializer != NULL)
					{
						_CheckInitBlock(initializer);
					}

					BfIRValue fieldAddr;
					if ((!mCurTypeInstance->IsTypedPrimitive()) && (!fieldInst->mResolvedType->IsVar()))
					{
						fieldAddr = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, fieldInst->mDataIdx /*, fieldDef->mName*/);
					}
					else
					{
						// Failed
					}
					auto assignValue = GetFieldInitializerValue(fieldInst);

					if (mCurTypeInstance->IsUnion())
					{
						auto fieldPtrType = CreatePointerType(fieldInst->mResolvedType);
						fieldAddr = mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(fieldPtrType));
					}

					if ((fieldAddr) && (assignValue))
						mBfIRBuilder->CreateAlignedStore(assignValue.mValue, fieldAddr, fieldInst->mResolvedType->mAlign);
				}
			}

			EmitInitBlocks(_CheckInitBlock);

			if (hadInlineInitBlock)
			{
				RestoreScopeState();
				// This gets set to false because of AddScope but we actually are still in the head block
				mCurMethodState->mInHeadScope = true;

				// Make sure we emit a return even if we had a NoReturn call or return inside ourselves
				mCurMethodState->mHadReturn = false;
				mCurMethodState->mLeftBlockUncond = false;
			}
		}
		else // Autocomplete case
		{
			BfScopeData scopeData;
			scopeData.mInInitBlock = true;
			mCurMethodState->AddScope(&scopeData);
			NewScopeState();

			// The reason we can't just do the 'normal' path for this is that the BfTypeInstance here is NOT the
			//  autocomplete type instance, so FieldInstance initializer values contain expressions from the full
			//  resolve pass, NOT the autocomplete expression
			for (auto tempTypeDef : mCompiler->mResolvePassData->mAutoCompleteTempTypes)
			{
				if (tempTypeDef->mFullName == typeDef->mFullName)
				{
					for (auto fieldDef : tempTypeDef->mFields)
					{
						auto initializer = fieldDef->GetInitializer();

						if ((!fieldDef->mIsStatic) && (initializer != NULL) && (mCompiler->mResolvePassData->mIsClassifying))
						{
							if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(initializer))
							{
								sourceClassifier->SetElementType(initializer, BfSourceElementType_Normal);
								sourceClassifier->VisitChild(initializer);
							}

							BfType* wantType = NULL;
							if ((!BfNodeIsA<BfVarTypeReference>(fieldDef->mTypeRef)) &&
								(!BfNodeIsA<BfLetTypeReference>(fieldDef->mTypeRef)))
								wantType = ResolveTypeRef(fieldDef->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowInferredSizedArray);
							if ((wantType != NULL) &&
								((wantType->IsVar()) || (wantType->IsLet()) || (wantType->IsRef())))
								wantType = NULL;

							BfEvalExprFlags exprFlags = BfEvalExprFlags_FieldInitializer;
							if (fieldDef->mIsAppend)
								exprFlags = (BfEvalExprFlags)(exprFlags | BfEvalExprFlags_AppendFieldInitializer);

							CreateValueFromExpression(initializer, wantType, exprFlags);
						}
					}

					tempTypeDef->PopulateMemberSets();
					BfMemberSetEntry* entry = NULL;
					BfMethodDef* initMethodDef = NULL;
					tempTypeDef->mMethodSet.TryGetWith(String("__BfInit"), &entry);
					if (entry != NULL)
						initMethodDef = (BfMethodDef*)entry->mMemberDef;
					SizedArray<BfAstNode*, 8> initBodies;
					for (; initMethodDef != NULL; initMethodDef = initMethodDef->mNextWithSameName)
					{
						if (initMethodDef->mMethodType != BfMethodType_Init)
							continue;
						initBodies.Insert(0, initMethodDef->mBody);
					}
					for (auto body : initBodies)
						VisitEmbeddedStatement(body);
				}
			}

			// Mark fields from full type with initializers as initialized, to give proper initialization errors within ctor
			for (auto fieldDef : typeDef->mFields)
			{
				if ((!fieldDef->mIsConst) && (!fieldDef->mIsStatic) && (fieldDef->GetInitializer() != NULL))
				{
					auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldDef->mIdx];
					if (fieldDef->GetInitializer() != NULL)
						MarkFieldInitialized(fieldInst);
				}
			}

			RestoreScopeState();
		}
	}

	if (!methodInstance->mIsAutocompleteMethod)
	{
		if (auto autoDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(methodDeclaration))
		{
			BfExprEvaluator exprEvaluator(this);
			for (int paramIdx = 0; paramIdx < methodDef->mParams.mSize; paramIdx++)
			{
				auto paramDef = methodDef->mParams[paramIdx];
				auto& fieldInstance = mCurTypeInstance->mFieldInstances[paramIdx];
				BF_ASSERT(paramDef->mName == fieldInstance.GetFieldDef()->mName);
				if (fieldInstance.mDataIdx < 0)
					continue;
				if (paramDef->mParamKind != BfParamKind_Normal)
					continue;

				auto localVar = mCurMethodState->mLocals[paramIdx + 1];
				BF_ASSERT(localVar->mName == paramDef->mName);
				auto localVal = exprEvaluator.LoadLocal(localVar);
				localVal = LoadOrAggregateValue(localVal);

				if (!localVal.mType->IsVar())
				{
					auto thisVal = GetThis();
					auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(thisVal.mValue, 0, fieldInstance.mDataIdx);
					if (mCurTypeInstance->IsUnion())
						fieldPtr = mBfIRBuilder->CreateBitCast(fieldPtr, mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(fieldInstance.mResolvedType)));
					mBfIRBuilder->CreateAlignedStore(localVal.mValue, fieldPtr, localVar->mResolvedType->mAlign);
				}
				MarkFieldInitialized(&fieldInstance);
			}
		}
	}

	// Call base ctor (if applicable)
	BfTypeInstance* targetType = NULL;
	BfAstNode* targetRefNode = NULL;
	if (ctorDeclaration != NULL)
    	targetRefNode = ctorDeclaration->mInitializer;
	if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(targetRefNode))
		targetRefNode = invocationExpr->mTarget;

	if (baseCtorNode != NULL)
	{
		UpdateSrcPos(baseCtorNode);
		if (methodDef->mBody == NULL)
			SetIllegalSrcPos();
	}
	if ((ctorDeclaration != NULL) && (ctorInvocation != NULL))
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorInvocation->mTarget);
		targetType = (targetToken->GetToken() == BfToken_This) ? mCurTypeInstance : mCurTypeInstance->mBaseType;
	}
	else if ((mCurTypeInstance->mBaseType != NULL) && (!mCurTypeInstance->IsUnspecializedType()) && (methodDef->mMethodType != BfMethodType_CtorNoBody))
	{
		auto baseType = mCurTypeInstance->mBaseType;
		if ((!mCurTypeInstance->IsTypedPrimitive()) &&
			((baseType->mTypeDef != mCompiler->mValueTypeTypeDef) || (!mContext->mCanSkipValueTypeCtor)) &&
			((baseType->mTypeDef != mCompiler->mBfObjectTypeDef) || (!mContext->mCanSkipObjectCtor)))
		{
			// Try to find a ctor without any params first
			BfMethodDef* matchedMethod = NULL;
			bool hadCtorWithAllDefaults = false;

			bool isHiddenGenerated = (methodDeclaration == NULL) && (methodDef->mProtection == BfProtection_Hidden);

			for (int pass = 0; pass < 2; pass++)
			{
				baseType->mTypeDef->PopulateMemberSets();
				BfMemberSetEntry* entry = NULL;
				BfMethodDef* checkMethodDef = NULL;
				baseType->mTypeDef->mMethodSet.TryGetWith(String("__BfCtor"), &entry);
				if (entry != NULL)
					checkMethodDef = (BfMethodDef*)entry->mMemberDef;

				while (checkMethodDef != NULL)
				{
					bool allowMethod = checkMethodDef->mProtection > BfProtection_Private;

					if (isHiddenGenerated)
					{
						// Allow calling of the default base ctor if it is implicitly defined
						if ((checkMethodDef->mMethodDeclaration == NULL) && (pass == 1) && (!hadCtorWithAllDefaults))
							allowMethod = true;
					}

					if ((checkMethodDef->mMethodType == BfMethodType_Ctor) && (!checkMethodDef->mIsStatic) && (allowMethod))
					{
						if (checkMethodDef->mParams.size() == 0)
						{
							if (matchedMethod != NULL)
							{
								// Has multiple matched methods - can happen from extensions
								matchedMethod = NULL;
								break;
							}
							matchedMethod = checkMethodDef;
						}
						else if (isHiddenGenerated)
						{
							if ((checkMethodDef->mParams[0]->mParamDeclaration != NULL) && (checkMethodDef->mParams[0]->mParamDeclaration->mInitializer != NULL))
								hadCtorWithAllDefaults = true;
						}
					}

					checkMethodDef = checkMethodDef->mNextWithSameName;
				}

				if (matchedMethod != NULL)
					break;
			}

			if ((HasExecutedOutput()) && (matchedMethod != NULL))
			{
				SizedArray<BfIRValue, 1> args;
				auto ctorBodyMethodInstance = GetMethodInstance(mCurTypeInstance->mBaseType, matchedMethod, BfTypeVector());
				if (!mCurTypeInstance->mBaseType->IsValuelessType())
				{
					auto basePtr = mBfIRBuilder->CreateBitCast(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->MapTypeInstPtr(mCurTypeInstance->mBaseType));
					args.push_back(basePtr);
				}
				AddCallDependency(ctorBodyMethodInstance.mMethodInstance, true);
				auto callInst = mBfIRBuilder->CreateCall(ctorBodyMethodInstance.mFunc, args);
				auto callingConv = GetIRCallingConvention(ctorBodyMethodInstance.mMethodInstance);
				if (callingConv != BfIRCallingConv_CDecl)
					mBfIRBuilder->SetCallCallingConv(callInst, callingConv);
				if (mIsComptimeModule)
					mCompiler->mCeMachine->QueueMethod(ctorBodyMethodInstance);
			}

			if (matchedMethod == NULL)
			{
				targetType = mCurTypeInstance->mBaseType;
				if (ctorDeclaration != NULL)
					targetRefNode = ctorDeclaration->mThisToken;
				else if (typeDef->mTypeDeclaration != NULL)
					targetRefNode = typeDef->mTypeDeclaration->mNameNode;
			}
		}
	}

	if (methodDef->mHasAppend)
	{
		mCurMethodState->mCurAppendAlign = methodInstance->mAppendAllocAlign;
	}

	if ((ctorDeclaration != NULL) && (ctorInvocation != NULL) && (ctorInvocation->mArguments.size() == 1) && (targetType != NULL))
	{
		if (auto tokenNode = BfNodeDynCast<BfUninitializedExpression>(ctorInvocation->mArguments[0]))
		{
			if (targetType == mCurTypeInstance)
			{
				auto thisVariable = GetThisVariable();
				if (thisVariable != NULL)
				{
					thisVariable->mUnassignedFieldFlags = 0;
					thisVariable->mAssignedKind = BfLocalVarAssignKind_Unconditional;
				}
			}

			targetType = NULL;
		}
	}

	auto autoComplete = mCompiler->GetAutoComplete();
	if (targetType != NULL)
	{
		BfAstNode* refNode = methodDeclaration;
		if (refNode == NULL)
			refNode = typeDef->mTypeDeclaration;

		BfAutoParentNodeEntry autoParentNodeEntry(this, refNode);

		auto wasCapturingMethodInfo = (autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo);
		if ((autoComplete != NULL) && (ctorDeclaration != NULL) && (ctorInvocation != NULL))
		{
			auto invocationExpr = ctorInvocation;
			autoComplete->CheckInvocation(invocationExpr, invocationExpr->mOpenParen, invocationExpr->mCloseParen, invocationExpr->mCommas);
		}

		BfType* targetThisType = targetType;
		auto target = Cast(targetRefNode, GetThis(), targetThisType);
		BfExprEvaluator exprEvaluator(this);
		BfResolvedArgs argValues;
		if ((ctorDeclaration != NULL) && (ctorInvocation != NULL))
		{
			argValues.Init(&ctorInvocation->mArguments);
			if (gDebugStuff)
			{
				OutputDebugStrF("Expr: %@  %d\n", argValues.mArguments->mVals, argValues.mArguments->mSize);
			}
		}
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgsFlag_DeferParamEval);

		BfTypedValue appendIdxVal;
		if (methodDef->mHasAppend)
		{
			auto localVar = mCurMethodState->GetRootMethodState()->mLocals[1];
			BF_ASSERT(localVar->mName == "appendIdx");
			auto intRefType = localVar->mResolvedType;
			appendIdxVal = BfTypedValue(localVar->mValue, intRefType);
			mCurMethodState->mCurAppendAlign = 1; // Don't make any assumptions about how the base leaves the alignment
		}
        exprEvaluator.MatchConstructor(targetRefNode, NULL, target, targetType, argValues, true, methodDef->mHasAppend, &appendIdxVal);

		if (autoComplete != NULL)
		{
			if ((wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo))
			{
				if (autoComplete->mMethodMatchInfo != NULL)
					autoComplete->mIsCapturingMethodMatchInfo = true;
				else
					autoComplete->mIsCapturingMethodMatchInfo = false;
			}
			else
				autoComplete->mIsCapturingMethodMatchInfo = false;
		}
	}

	auto autoCtorDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(methodDeclaration);
	if ((autoComplete != NULL) && (autoComplete->CheckFixit(methodDeclaration)) && (methodDeclaration != NULL) && (autoCtorDecl != NULL))
	{
		auto typeDecl = methodDef->mDeclaringType->mTypeDeclaration;
		BfParserData* parser = typeDecl->GetSourceData()->ToParserData();
		if (parser != NULL)
		{
			String fixitStr = "Expand auto constructor\t";
			int insertPos = typeDecl->mSrcStart;

			bool needsBlock = false;

			if (auto defBlock = BfNodeDynCast<BfBlock>(typeDecl->mDefineNode))
			{
				insertPos = defBlock->mOpenBrace->mSrcStart + 1;
			}
			else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(typeDecl->mDefineNode))
			{
				insertPos = tokenNode->mSrcStart;
				fixitStr += StrFormat("delete|%s-%d|\x01",
					autoComplete->FixitGetLocation(parser, tokenNode->mSrcStart).c_str(), tokenNode->mSrcEnd - tokenNode->mSrcStart);
				needsBlock = true;
			}

			int srcStart = methodDeclaration->mSrcStart;
			if ((autoCtorDecl->mPrefix == NULL) && (typeDecl->mColonToken != NULL))
				srcStart = typeDecl->mColonToken->mSrcStart;

			while ((srcStart > 0) && (::isspace((uint8)parser->mSrc[srcStart - 1])))
				srcStart--;

			fixitStr += StrFormat("expand|%s|%d|",
				parser->mFileName.c_str(), insertPos);

			if (needsBlock)
				fixitStr += "\t";
			else
				fixitStr += "\f";

			for (int paramIdx = 0; paramIdx < autoCtorDecl->mParams.mSize; paramIdx++)
			{
				String paramStr = autoCtorDecl->mParams[paramIdx]->ToString();
				paramStr.Replace('\n', '\r');

				fixitStr += "public ";
				fixitStr += paramStr;
				fixitStr += ";\r";
			}

			fixitStr += "\rpublic this(";
			for (int paramIdx = 0; paramIdx < autoCtorDecl->mParams.mSize; paramIdx++)
			{
				if (paramIdx > 0)
					fixitStr += ", ";
				String paramStr = autoCtorDecl->mParams[paramIdx]->ToString();
				paramStr.Replace('\n', '\r');
				fixitStr += paramStr;
			}
			fixitStr += ")\t";
			for (int paramIdx = 0; paramIdx < autoCtorDecl->mParams.mSize; paramIdx++)
			{
				if (paramIdx > 0)
					fixitStr += "\r";
				auto nameNode = autoCtorDecl->mParams[paramIdx]->mNameNode;
				if (nameNode == NULL)
					continue;
				String nameStr = nameNode->ToString();
				fixitStr += "this.";
				fixitStr += nameStr;
				fixitStr += " = ";
				fixitStr += nameStr;
				fixitStr += ";";
			}
			fixitStr += "\b";

			if (needsBlock)
				fixitStr += "\b";

			fixitStr += StrFormat("\x01""delete|%s-%d|",
				autoComplete->FixitGetLocation(parser, srcStart).c_str(), autoCtorDecl->mSrcEnd - srcStart);

			mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", fixitStr.c_str()));
		}
	}
}

void BfModule::EmitEnumToStringBody()
{
	auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef);
	auto strVal = CreateAlloca(stringType);

	auto int64StructType = GetPrimitiveStructType(BfTypeCode_Int64);

	BfExprEvaluator exprEvaluator(this);
	auto stringDestAddr = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);

	BfIRBlock appendBlock = mBfIRBuilder->CreateBlock("append");
	BfIRBlock noMatchBlock = mBfIRBuilder->CreateBlock("noMatch");
	BfIRBlock endBlock = mBfIRBuilder->CreateBlock("end");

	BfType* discriminatorType = NULL;

	BfTypedValue rawPayload;
	BfIRValue enumVal;
	if (mCurTypeInstance->IsPayloadEnum())
	{
		discriminatorType = mCurTypeInstance->GetDiscriminatorType();
		auto enumTypedValue = ExtractValue(GetThis(), NULL, 2);
		enumTypedValue = LoadValue(enumTypedValue);
		enumVal = enumTypedValue.mValue;
		rawPayload = ExtractValue(GetThis(), NULL, 1);
	}
	else
		enumVal = mBfIRBuilder->GetArgument(0);

	Array<BfType*> paramTypes;
	paramTypes.Add(stringType);
	auto appendModuleMethodInstance = GetMethodByName(stringType->ToTypeInstance(), "Append", paramTypes);

	auto switchVal = mBfIRBuilder->CreateSwitch(enumVal, noMatchBlock, (int)mCurTypeInstance->mFieldInstances.size());

	HashSet<int64> handledCases;
	for (auto& fieldInstance : mCurTypeInstance->mFieldInstances)
	{
		if (fieldInstance.mIsEnumPayloadCase)
		{
			int tagId = -fieldInstance.mDataIdx - 1;
			BfIRBlock caseBlock = mBfIRBuilder->CreateBlock("case");
			mBfIRBuilder->AddBlock(caseBlock);
			mBfIRBuilder->SetInsertPoint(caseBlock);

			BF_ASSERT(discriminatorType->IsPrimitiveType());
			auto constVal = mBfIRBuilder->CreateConst(((BfPrimitiveType*)discriminatorType)->mTypeDef->mTypeCode, tagId);
			mBfIRBuilder->AddSwitchCase(switchVal, constVal, caseBlock);

			auto caseStr = GetStringObjectValue(fieldInstance.GetFieldDef()->mName);

			auto stringDestVal = LoadValue(stringDestAddr);
			SizedArray<BfIRValue, 2> args;
			args.Add(stringDestVal.mValue);
			args.Add(caseStr);
			exprEvaluator.CreateCall(NULL, appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);

			auto payloadType = fieldInstance.mResolvedType->ToTypeInstance();
			BF_ASSERT(payloadType->IsTuple());

			if (payloadType->mFieldInstances.size() != 0)
			{
				auto payload = rawPayload;
				if (payload.mType != payloadType)
				{
					payload = Cast(NULL, payload, payloadType, BfCastFlags_Force);
				}

				auto toStringMethod = GetMethodByName(payloadType->ToTypeInstance(), "ToString");

				SizedArray<BfIRValue, 2> irArgs;
				exprEvaluator.PushThis(NULL, payload, toStringMethod.mMethodInstance, irArgs);
				stringDestVal = LoadValue(stringDestAddr);
				irArgs.Add(stringDestVal.mValue);
				exprEvaluator.CreateCall(NULL, toStringMethod.mMethodInstance, toStringMethod.mFunc, true, irArgs);
			}

			mBfIRBuilder->CreateBr(endBlock);
			continue;
		}

		if (fieldInstance.mConstIdx == -1)
			continue;

		// Only allow compact 'ValA, ValB' enum declaration fields through
		auto fieldDef = fieldInstance.GetFieldDef();
		auto fieldDecl = fieldDef->mFieldDeclaration;
		if ((fieldDecl == NULL) || (fieldDef->mTypeRef != NULL))
			continue;

		auto constant = mCurTypeInstance->mConstHolder->GetConstantById(fieldInstance.mConstIdx);
		if (!handledCases.TryAdd(constant->mInt64, NULL))
		{
			// Duplicate
			continue;
		}

		BfIRBlock caseBlock = mBfIRBuilder->CreateBlock("case");
		mBfIRBuilder->AddBlock(caseBlock);
		mBfIRBuilder->SetInsertPoint(caseBlock);

		BfIRValue constVal = ConstantToCurrent(constant, mCurTypeInstance->mConstHolder, mCurTypeInstance);
		mBfIRBuilder->AddSwitchCase(switchVal, constVal, caseBlock);

		auto caseStr = GetStringObjectValue(fieldInstance.GetFieldDef()->mName);
		mBfIRBuilder->CreateStore(caseStr, strVal);
		mBfIRBuilder->CreateBr(appendBlock);
	}

	mBfIRBuilder->AddBlock(appendBlock);
	mBfIRBuilder->SetInsertPoint(appendBlock);

	SizedArray<BfIRValue, 2> args;
	auto stringDestVal = LoadValue(stringDestAddr);
	args.Add(stringDestVal.mValue);
	args.Add(mBfIRBuilder->CreateLoad(strVal));
	exprEvaluator.CreateCall(NULL, appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
	mBfIRBuilder->CreateBr(endBlock);

	mBfIRBuilder->AddBlock(noMatchBlock);
	mBfIRBuilder->SetInsertPoint(noMatchBlock);
	auto int64Val = mBfIRBuilder->CreateNumericCast(enumVal, false, BfTypeCode_Int64);
	auto toStringModuleMethodInstance = GetMethodByName(int64StructType, "ToString", 1);
	args.clear();
	args.Add(int64Val);
	stringDestVal = LoadValue(stringDestAddr);
	args.Add(stringDestVal.mValue);
	exprEvaluator.CreateCall(NULL, toStringModuleMethodInstance.mMethodInstance, toStringModuleMethodInstance.mFunc, false, args);
	mBfIRBuilder->CreateBr(endBlock);

	mBfIRBuilder->AddBlock(endBlock);
	mBfIRBuilder->SetInsertPoint(endBlock);
}

void BfModule::EmitTupleToStringBody()
{
	auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef);

	BfExprEvaluator exprEvaluator(this);

	auto stringDestRef = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);

	Array<BfType*> paramTypes;
	paramTypes.Add(GetPrimitiveType(BfTypeCode_Char8));
	auto appendCharModuleMethodInstance = GetMethodByName(stringType->ToTypeInstance(), "Append", paramTypes);

	paramTypes.Clear();
	paramTypes.Add(stringType);
	auto appendModuleMethodInstance = GetMethodByName(stringType->ToTypeInstance(), "Append", paramTypes);

	auto _AppendChar = [&](char c)
	{
		SizedArray<BfIRValue, 2> args;
		auto stringDestVal = LoadValue(stringDestRef);
		args.Add(stringDestVal.mValue);
		args.Add(mBfIRBuilder->CreateConst(BfTypeCode_Char8, (int)c));
		exprEvaluator.CreateCall(NULL, appendCharModuleMethodInstance.mMethodInstance, appendCharModuleMethodInstance.mFunc, false, args);
	};

	int fieldIdx = 0;

	auto thisValue = GetThis();

	auto toStringModuleMethodInstance = GetMethodByName(mContext->mBfObjectType, "ToString", 1);
	auto toStringSafeModuleMethodInstance = GetMethodByName(mContext->mBfObjectType, "ToString", 2);
	BfIRValue commaStr;

	auto iPrintableType = ResolveTypeDef(mCompiler->mIPrintableTypeDef)->ToTypeInstance();
	auto printModuleMethodInstance = GetMethodByName(iPrintableType, "Print");

	_AppendChar('(');
	for (auto& fieldInstance : mCurTypeInstance->mFieldInstances)
	{
		if (fieldInstance.mDataIdx == -1)
			continue;

		if (fieldIdx > 0)
		{
			if (!commaStr)
				commaStr = GetStringObjectValue(", ");
			SizedArray<BfIRValue, 2> args;
			auto stringDestVal = LoadValue(stringDestRef);
			args.Add(stringDestVal.mValue);
			args.Add(commaStr);
			exprEvaluator.CreateCall(NULL, appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
		}
		fieldIdx++;

		if (fieldInstance.mResolvedType->IsValuelessType())
			continue;

		BfTypedValue fieldValue = ExtractValue(thisValue, &fieldInstance, fieldInstance.mDataIdx);

		if (fieldValue.mType->IsPrimitiveType())
		{
			fieldValue.mType = GetPrimitiveStructType(((BfPrimitiveType*)fieldValue.mType)->mTypeDef->mTypeCode);
		}

		auto typeInstance = fieldValue.mType->ToTypeInstance();

		if ((typeInstance != NULL) && (TypeIsSubTypeOf(typeInstance, iPrintableType, false)))
		{
			BfExprEvaluator exprEvaluator(this);
			SizedArray<BfResolvedArg, 0> resolvedArgs;
			BfMethodMatcher methodMatcher(NULL, this, printModuleMethodInstance.mMethodInstance, resolvedArgs, BfMethodGenericArguments());
			methodMatcher.mBestMethodDef = printModuleMethodInstance.mMethodInstance->mMethodDef;
			methodMatcher.mBestMethodTypeInstance = iPrintableType;
			methodMatcher.TryDevirtualizeCall(fieldValue);

			BfTypedValue callVal = Cast(NULL, fieldValue, methodMatcher.mBestMethodTypeInstance);

			BfResolvedArg resolvedArg;
			auto stringDestVal = LoadValue(stringDestRef);
			resolvedArg.mTypedValue = stringDestVal;
			resolvedArg.mResolvedType = stringDestVal.mType;
			resolvedArgs.Add(resolvedArg);

			exprEvaluator.CreateCall(&methodMatcher, callVal);
			continue;
		}

		if (fieldValue.mType->IsObjectOrInterface())
		{
			fieldValue = LoadValue(fieldValue);
			BF_ASSERT(!fieldValue.IsAddr());
			SizedArray<BfIRValue, 2> args;
			args.Add(mBfIRBuilder->CreateBitCast(fieldValue.mValue, mBfIRBuilder->MapType(mContext->mBfObjectType)));
			auto stringDestVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);
			stringDestVal = LoadValue(stringDestVal);
			args.Add(stringDestVal.mValue);
			exprEvaluator.CreateCall(NULL, toStringSafeModuleMethodInstance.mMethodInstance, toStringSafeModuleMethodInstance.mFunc, false, args);
			continue;
		}

		BfExprEvaluator exprEvaluator(this);
		SizedArray<BfResolvedArg, 0> resolvedArgs;
		BfMethodMatcher methodMatcher(NULL, this, toStringModuleMethodInstance.mMethodInstance, resolvedArgs, BfMethodGenericArguments());
		methodMatcher.mBestMethodDef = toStringModuleMethodInstance.mMethodInstance->mMethodDef;
		methodMatcher.mBestMethodTypeInstance = mContext->mBfObjectType;
		methodMatcher.TryDevirtualizeCall(fieldValue);

		if (methodMatcher.mBestMethodTypeInstance == mContext->mBfObjectType)
		{
			AddCallDependency(appendModuleMethodInstance.mMethodInstance);
			AddDependency(fieldValue.mType, mCurTypeInstance, BfDependencyMap::DependencyFlag_Calls);

			// Just append the type name
			String typeName = TypeToString(fieldInstance.mResolvedType);
			auto strVal = GetStringObjectValue(typeName);

			SizedArray<BfIRValue, 2> args;
			auto stringDestVal = LoadValue(stringDestRef);
			args.Add(stringDestVal.mValue);
			args.Add(strVal);
			exprEvaluator.CreateCall(NULL, appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
			continue;
		}

		BfTypedValue callVal = Cast(NULL, fieldValue, methodMatcher.mBestMethodTypeInstance);

		BfResolvedArg resolvedArg;
		auto stringDestVal = LoadValue(stringDestRef);
		resolvedArg.mTypedValue = stringDestVal;
		resolvedArg.mResolvedType = stringDestVal.mType;
		resolvedArgs.Add(resolvedArg);

		exprEvaluator.CreateCall(&methodMatcher, callVal);
	}
	_AppendChar(')');
}

void BfModule::EmitIteratorBlock(bool& skipBody)
{
	auto methodInstance = mCurMethodInstance;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();
	auto typeDef = mCurTypeInstance->mTypeDef;

	BfType* innerRetType = NULL;
	BfTypeInstance* usingInterface = NULL;

	auto retTypeInst = mCurMethodInstance->mReturnType->ToGenericTypeInstance();
	if (retTypeInst != NULL)
	{
		if ((retTypeInst->IsInstanceOf(mCompiler->mGenericIEnumerableTypeDef)) ||
			(retTypeInst->IsInstanceOf(mCompiler->mGenericIEnumeratorTypeDef)))
		{
			innerRetType = retTypeInst->mGenericTypeInfo->mTypeGenericArguments[0];
		}
	}

	if (innerRetType == NULL)
	{
		Fail("Methods with yields must return either System.IEnumerable<T> or System.IEnumerator<T>", methodDeclaration->mReturnType);
		return;
	}

	auto blockBody = BfNodeDynCast<BfBlock>(methodDeclaration->mBody);
	if (blockBody == NULL)
		return;
}

void BfModule::EmitGCMarkAppended(BfTypedValue markVal)
{
	auto gcType = ResolveTypeDef(mCompiler->mGCTypeDef, BfPopulateType_DataAndMethods);
	if (gcType == NULL)
		return;
	BfModuleMethodInstance markFromGCThreadMethodInstance = GetMethodByName(gcType->ToTypeInstance(), "MarkAppendedObject", 1);
	if (!markFromGCThreadMethodInstance)
		return;

	SizedArray<BfIRValue, 1> args;
	args.push_back(mBfIRBuilder->CreateBitCast(markVal.mValue, mBfIRBuilder->MapType(mContext->mBfObjectType)));
	BfExprEvaluator exprEvaluator(this);
	exprEvaluator.CreateCall(NULL, markFromGCThreadMethodInstance.mMethodInstance, markFromGCThreadMethodInstance.mFunc, false, args);
}

void BfModule::EmitGCMarkValue(BfTypedValue markVal, BfModuleMethodInstance markFromGCThreadMethodInstance)
{
	auto fieldType = markVal.mType;
	auto fieldTypeInst = fieldType->ToTypeInstance();
	if (fieldType == NULL)
		return;

	if (!markVal.mType->IsComposite())
		markVal = LoadValue(markVal);

	BfExprEvaluator exprEvaluator(this);
	PopulateType(markVal.mType, BfPopulateType_Data);
	if (markVal.mType->IsObjectOrInterface())
	{
		BfIRValue val = markVal.mValue;
		val = mBfIRBuilder->CreateBitCast(val, mBfIRBuilder->MapType(mContext->mBfObjectType));
		SizedArray<BfIRValue, 1> args;
		args.push_back(val);
		exprEvaluator.CreateCall(NULL, markFromGCThreadMethodInstance.mMethodInstance, markFromGCThreadMethodInstance.mFunc, false, args);
	}
	else if (fieldType->IsSizedArray())
	{
		BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)fieldType;
		if (sizedArrayType->mElementType->WantsGCMarking())
		{
			BfTypedValue arrayValue = markVal;
			auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
			auto itr = CreateAlloca(intPtrType);
			mBfIRBuilder->CreateStore(GetDefaultValue(intPtrType), itr);

			auto loopBB = mBfIRBuilder->CreateBlock("loop", true);
			auto bodyBB = mBfIRBuilder->CreateBlock("body", true);
			auto doneBB = mBfIRBuilder->CreateBlock("done", true);
			mBfIRBuilder->CreateBr(loopBB);

			mBfIRBuilder->SetInsertPoint(loopBB);
			auto loadedItr = mBfIRBuilder->CreateLoad(itr);
			auto cmpRes = mBfIRBuilder->CreateCmpGTE(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)sizedArrayType->mElementCount), true);
			mBfIRBuilder->CreateCondBr(cmpRes, doneBB, bodyBB);

			mBfIRBuilder->SetInsertPoint(bodyBB);

			auto ptrType = CreatePointerType(sizedArrayType->mElementType);
			auto ptrValue = mBfIRBuilder->CreateBitCast(arrayValue.mValue, mBfIRBuilder->MapType(ptrType));
			auto gepResult = mBfIRBuilder->CreateInBoundsGEP(ptrValue, loadedItr);
			auto value = BfTypedValue(gepResult, sizedArrayType->mElementType, BfTypedValueKind_Addr);

			EmitGCMarkValue(value, markFromGCThreadMethodInstance);

			auto incValue = mBfIRBuilder->CreateAdd(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
			mBfIRBuilder->CreateStore(incValue, itr);
			mBfIRBuilder->CreateBr(loopBB);

			mBfIRBuilder->SetInsertPoint(doneBB);
		}
	}
	else if ((fieldType->IsComposite()) && (!fieldType->IsTypedPrimitive()) && (fieldTypeInst != NULL))
	{
		auto markMemberMethodInstance = GetMethodByName(fieldTypeInst, BF_METHODNAME_MARKMEMBERS, 0, true);
		if (markMemberMethodInstance)
		{
			SizedArray<BfIRValue, 1> args;

			auto methodOwner = markMemberMethodInstance.mMethodInstance->GetOwner();
			if (markVal.mType != methodOwner)
				markVal = Cast(NULL, markVal, methodOwner);

			exprEvaluator.PushThis(NULL, markVal, markMemberMethodInstance.mMethodInstance, args);
			exprEvaluator.CreateCall(NULL, markMemberMethodInstance.mMethodInstance, markMemberMethodInstance.mFunc, false, args);
		}
	}
}

void BfModule::CallChainedMethods(BfMethodInstance* methodInstance, bool reverse)
{
	Array<BfMethodInstance*> methodInstances;

	for (int methodIdx = 0; methodIdx < (int)mCurTypeInstance->mMethodInstanceGroups.size(); methodIdx++)
	{
		auto& methodInstGroup = mCurTypeInstance->mMethodInstanceGroups[methodIdx];
		auto chainedMethodInst = methodInstGroup.mDefault;
		if ((chainedMethodInst != NULL) && (chainedMethodInst->mChainType == BfMethodChainType_ChainMember))
		{
			if ((chainedMethodInst->mMethodDef->mIsStatic == methodInstance->mMethodDef->mIsStatic) &&
				(CompareMethodSignatures(methodInstance, chainedMethodInst)))
			{
				methodInstances.push_back(chainedMethodInst);
			}
		}
	}

	std::stable_sort(methodInstances.begin(), methodInstances.end(),
		[&](BfMethodInstance* lhs, BfMethodInstance* rhs)
		{
			bool isBetter;
			bool isWorse;
			CompareDeclTypes(mCurTypeInstance, lhs->mMethodDef->mDeclaringType, rhs->mMethodDef->mDeclaringType, isBetter, isWorse);
			if (isBetter == isWorse)
			{
				return false;
			}
			return isWorse;
		});

	if (reverse)
		std::reverse(methodInstances.begin(), methodInstances.end());

	for (auto chainedMethodInst : methodInstances)
	{
		auto declModule = this;
		BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None;

		auto moduleMethodInst = declModule->GetMethodInstance(mCurTypeInstance, chainedMethodInst->mMethodDef, BfTypeVector(), flags);

		BfExprEvaluator exprEvaluator(this);
		SizedArray<BfIRValue, 1> args;
		exprEvaluator.PushThis(chainedMethodInst->mMethodDef->GetRefNode(), GetThis(), chainedMethodInst, args);
		exprEvaluator.CreateCall(NULL, moduleMethodInst.mMethodInstance, moduleMethodInst.mFunc, true, args);
	}
}

void BfModule::AddHotDataReferences(BfHotDataReferenceBuilder* builder)
{
	BF_ASSERT(mCurMethodInstance->mIsReified);

	if (mCurTypeInstance->mHotTypeData == NULL)
	{
		mCurTypeInstance->mHotTypeData = new BfHotTypeData();
		BfLogSysM("Created HotTypeData %p created for type %p in AddHotDataReferences\n", mCurTypeInstance->mHotTypeData, mCurTypeInstance);
	}

	auto hotMethod = mCurMethodInstance->mHotMethod;
	for (auto depData : hotMethod->mReferences)
	{
		// Only virtual decls are allowed to already be there
		BF_ASSERT((depData->mDataKind == BfHotDepDataKind_VirtualDecl) || (depData->mDataKind == BfHotDepDataKind_DupMethod));
	}

	BF_ASSERT(hotMethod->mSrcTypeVersion != NULL);

	int prevSize = (int)hotMethod->mReferences.size();
	int refCount = (int)(prevSize + builder->mUsedData.size() + builder->mCalledMethods.size() + builder->mDevirtualizedCalledMethods.size());
	if (!mCurMethodInstance->mMethodDef->mIsStatic)
		refCount++;

	hotMethod->mReferences.Reserve(refCount);
	if (!mCurMethodInstance->mMethodDef->mIsStatic)
	{
		auto hotThis = mCompiler->mHotData->GetThisType(mCurMethodInstance->GetOwner()->mHotTypeData->GetLatestVersion());
		hotThis->mRefCount++;
		hotMethod->mReferences.Insert(0, hotThis);
		prevSize++;
	}
 	for (auto val : builder->mAllocatedData)
 	{
 		auto hotAllocation = mCompiler->mHotData->GetAllocation(val);
 		hotMethod->mReferences.Add(hotAllocation);
 	}
	for (auto val : builder->mUsedData)
		hotMethod->mReferences.Add(val);
	for (auto val : builder->mCalledMethods)
		hotMethod->mReferences.Add(val);
	for (auto val : builder->mDevirtualizedCalledMethods)
	{
		auto devirtualizedMethod = mCompiler->mHotData->GetDevirtualizedMethod(val);
		hotMethod->mReferences.Add(devirtualizedMethod);
	}
	for (auto val : builder->mFunctionPtrs)
	{
		auto funcRef = mCompiler->mHotData->GetFunctionReference(val);
		hotMethod->mReferences.Add(funcRef);
	}
	for (auto val : builder->mInnerMethods)
	{
		auto innerMethod = mCompiler->mHotData->GetInnerMethod(val);
		hotMethod->mReferences.Add(innerMethod);
	}

	for (int refIdx = prevSize; refIdx < (int)hotMethod->mReferences.size(); refIdx++)
	{
		auto depData = hotMethod->mReferences[refIdx];
		BF_ASSERT(depData != NULL);
		depData->mRefCount++;
	}
}

void BfModule::ProcessMethod_SetupParams(BfMethodInstance* methodInstance, BfType* thisType, bool wantsDIData, SizedArrayImpl<BfIRMDNode>* diParams)
{
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->mMethodDeclaration;

	bool isThisStruct = false;
	if (thisType != NULL)
		isThisStruct = thisType->IsStruct() && !thisType->IsTypedPrimitive();

	int argIdx = 0;

	if ((!mIsComptimeModule) && (argIdx == methodInstance->GetStructRetIdx()))
		argIdx++;

	auto rootMethodState = mCurMethodState->GetRootMethodState();

	if (!methodDef->mIsStatic)
	{
		BfTypeCode loweredTypeCode = BfTypeCode_None;
		BfTypeCode loweredTypeCode2 = BfTypeCode_None;

		BfLocalVariable* paramVar = rootMethodState->mBumpAlloc.Alloc<BfLocalVariable>();
		paramVar->mIsBumpAlloc = true;
		paramVar->mResolvedType = thisType;
		paramVar->mName.Reference("this");
		if (!thisType->IsValuelessType())
			paramVar->mValue = mBfIRBuilder->GetArgument(argIdx);
		else
			paramVar->mValue = mBfIRBuilder->GetFakeVal();

		if ((!mIsComptimeModule) && (thisType->IsSplattable()) && (methodInstance->AllowsSplatting(-1)))
		{
			if (!thisType->IsTypedPrimitive())
				paramVar->mIsSplat = true;
		}
		else if ((!mIsComptimeModule) && (!methodDef->mIsMutating) && (methodInstance->mCallingConvention == BfCallingConvention_Unspecified))
			paramVar->mIsLowered = thisType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2) != BfTypeCode_None;

		auto thisTypeInst = thisType->ToTypeInstance();
		paramVar->mIsStruct = isThisStruct;
		paramVar->mAddr = BfIRValue();
		paramVar->mIsThis = true;
		paramVar->mParamIdx = -1;
		if ((methodDef->mMethodType == BfMethodType_Ctor) && (mCurTypeInstance->IsStruct()))
		{
			paramVar->mAssignedKind = BfLocalVarAssignKind_None;
		}
		else
		{
			paramVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		}

		paramVar->mReadFromId = -1;
		paramVar->Init();
		if (methodDeclaration == NULL)
			UseDefaultSrcPos();
		AddLocalVariableDef(paramVar);
		if (!thisType->IsValuelessType())
		{
			if (wantsDIData)
			{
				BfType* thisPtrType = thisType;
				auto diType = mBfIRBuilder->DbgGetType(thisPtrType);
				if (!thisType->IsValueType())
					diType = mBfIRBuilder->DbgCreateArtificialType(diType);
				else if (!paramVar->mIsSplat)
					diType = mBfIRBuilder->DbgCreatePointerType(diType);

				diParams->push_back(diType);
			}

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
			}
			else
			{
				argIdx++;
				if (loweredTypeCode2 != BfTypeCode_None)
					argIdx++;
			}
		}
	}

	if ((!mIsComptimeModule) && (argIdx == methodInstance->GetStructRetIdx()))
		argIdx++;

	bool hadParams = false;
	bool hasDefault = false;

	int compositeVariableIdx = -1;
	int paramIdx = 0;
	for (paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
	{
		// We already issues a type error for this param if we had one in declaration processing
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
		BfLocalVariable* paramVar = rootMethodState->mBumpAlloc.Alloc<BfLocalVariable>();
		paramVar->mIsBumpAlloc = true;

		BfTypeCode loweredTypeCode = BfTypeCode_None;
		BfTypeCode loweredTypeCode2 = BfTypeCode_None;

		bool isParamSkipped = methodInstance->IsParamSkipped(paramIdx);

		auto resolvedType = methodInstance->GetParamType(paramIdx);
		if (resolvedType == NULL)
		{
			AssertErrorState();
			paramVar->mParamFailed = true;
		}
		prevIgnoreErrors.Restore();
		PopulateType(resolvedType, BfPopulateType_Declaration);
		paramVar->mResolvedType = resolvedType;
		int namePrefixCount = 0;
		methodInstance->GetParamName(paramIdx, paramVar->mName, namePrefixCount);

		paramVar->mNamePrefixCount = (uint8)namePrefixCount;
		paramVar->mNameNode = methodInstance->GetParamNameNode(paramIdx);
		if (!isParamSkipped)
		{
			paramVar->mValue = mBfIRBuilder->GetArgument(argIdx);

			if (resolvedType->IsMethodRef())
			{
				paramVar->mIsSplat = true;
			}
			else if ((!mIsComptimeModule) && (resolvedType->IsComposite()) && (resolvedType->IsSplattable()))
			{
				if (methodInstance->AllowsSplatting(paramIdx))
				{
					auto resolveTypeInst = resolvedType->ToTypeInstance();
					if ((resolveTypeInst != NULL) && (resolveTypeInst->mIsCRepr))
						paramVar->mIsSplat = true;
					else
					{
						int splatCount = resolvedType->GetSplatCount();
						if (argIdx + splatCount <= mCompiler->mOptions.mMaxSplatRegs)
							paramVar->mIsSplat = true;
					}
				}
			}
		}
		else
		{
			paramVar->mIsSplat = true; // Treat skipped (valueless) as a splat
			paramVar->mValue = mBfIRBuilder->GetFakeVal();
		}

		paramVar->mIsLowered = (!mIsComptimeModule) && resolvedType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2) != BfTypeCode_None;
		paramVar->mIsStruct = resolvedType->IsComposite() && !resolvedType->IsTypedPrimitive();
		paramVar->mParamIdx = paramIdx;
		paramVar->mIsImplicitParam = methodInstance->IsImplicitCapture(paramIdx);
		paramVar->mReadFromId = 0;
		if (resolvedType->IsRef())
		{
			auto refType = (BfRefType*)resolvedType;
			paramVar->mAssignedKind = (refType->mRefKind != BfRefType::RefKind_Out) ? BfLocalVarAssignKind_Unconditional : BfLocalVarAssignKind_None;
			if (refType->mRefKind == BfRefType::RefKind_In)
			{
				paramVar->mIsReadOnly = true;
			}
		}
		else
		{
			paramVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
			if (methodDef->mMethodType != BfMethodType_Mixin)
				paramVar->mIsReadOnly = true;
		}

		if (paramVar->mResolvedType->IsGenericParam())
		{
			auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)paramVar->mResolvedType);
			if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
			{
				auto typedVal = GetDefaultTypedValue(genericParamInst->mTypeConstraint, false, BfDefaultValueKind_Undef);
				paramVar->mResolvedType = typedVal.mType;
				paramVar->mConstValue = typedVal.mValue;
			}
		}

		if (paramVar->mResolvedType->IsConstExprValue())
		{
			BfConstExprValueType* constExprValueType = (BfConstExprValueType*)paramVar->mResolvedType;

			auto unspecializedMethodInstance = GetUnspecializedMethodInstance(methodInstance);
			auto unspecParamType = unspecializedMethodInstance->GetParamType(paramIdx);
			BF_ASSERT(unspecParamType->IsGenericParam());
			if (unspecParamType->IsGenericParam())
			{
				BfGenericParamType* genericParamType = (BfGenericParamType*)unspecParamType;
				if (genericParamType->mGenericParamKind == BfGenericParamKind_Method)
				{
					auto genericParamInst = unspecializedMethodInstance->mMethodInfoEx->mGenericParams[genericParamType->mGenericParamIdx];
					if (genericParamInst->mTypeConstraint != NULL)
					{
						BfExprEvaluator exprEvaluator(this);
						exprEvaluator.mExpectingType = genericParamInst->mTypeConstraint;
						exprEvaluator.GetLiteral(NULL, constExprValueType->mValue);

						auto checkType = genericParamInst->mTypeConstraint;
						if (checkType->IsTypedPrimitive())
							checkType = checkType->GetUnderlyingType();

						auto typedVal = exprEvaluator.mResult;
						if ((typedVal) && (typedVal.mType != checkType))
							typedVal = Cast(NULL, typedVal, checkType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail));

						if ((typedVal.mType == checkType) && (typedVal.mValue.IsConst()))
						{
							paramVar->mResolvedType = genericParamInst->mTypeConstraint;
							paramVar->mConstValue = typedVal.mValue;
							BF_ASSERT(paramVar->mConstValue.IsConst());
							paramVar->mIsConst = true;
						}
						else
						{
							AssertErrorState();
							paramVar->mResolvedType = genericParamInst->mTypeConstraint;
							paramVar->mConstValue = GetDefaultValue(genericParamInst->mTypeConstraint);
							paramVar->mIsConst = true;
						}

						if (paramVar->mResolvedType->IsObject())
						{
							BfTypedValue typedVal(paramVar->mConstValue, paramVar->mResolvedType);
							FixValueActualization(typedVal);
							if (typedVal.mValue.IsConst())
								paramVar->mConstValue = typedVal.mValue;
						}
					}
				}
			}

			//BF_ASSERT(!paramVar->mResolvedType->IsConstExprValue());
			if (paramVar->mResolvedType->IsConstExprValue())
			{
				Fail("Invalid use of constant expression", methodInstance->GetParamTypeRef(paramIdx));
				//AssertErrorState();
				paramVar->mResolvedType = GetPrimitiveType(BfTypeCode_IntPtr);
				paramVar->mConstValue = GetDefaultValue(paramVar->mResolvedType);
				paramVar->mIsConst = true;
			}
		}

		if (methodInstance->GetParamKind(paramIdx) == BfParamKind_DelegateParam)
		{
			if (compositeVariableIdx == -1)
			{
				compositeVariableIdx = (int)mCurMethodState->mLocals.size();

				BfLocalVariable* localVar = new BfLocalVariable();
				auto paramInst = &methodInstance->mParams[paramIdx];
				auto paramDef = methodDef->mParams[paramInst->mParamDefIdx];
				localVar->mName = paramDef->mName;
				localVar->mNamePrefixCount = paramDef->mNamePrefixCount;
				localVar->mResolvedType = ResolveTypeRef(paramDef->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);
				localVar->mCompositeCount = 0;
				DoAddLocalVariable(localVar);
			}
			mCurMethodState->mLocals[compositeVariableIdx]->mCompositeCount++;
			paramVar->mIsImplicitParam = true;
		}

		if (!mCurTypeInstance->IsDelegateOrFunction())
			CheckVariableDef(paramVar);
		paramVar->Init();
		AddLocalVariableDef(paramVar);
		if (!isParamSkipped)
		{
			if (wantsDIData)
				diParams->push_back(mBfIRBuilder->DbgGetType(resolvedType));

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
			}
			else
			{
				argIdx++;
			}

			if (loweredTypeCode2 != BfTypeCode_None)
				argIdx++;
		}
	}

	// Try to find an ending delegate params composite
	if ((compositeVariableIdx == -1) && (methodDef->mParams.size() > 0))
	{
		auto paramDef = methodDef->mParams.back();
		if (paramDef->mParamKind == BfParamKind_Params)
		{
			auto paramsType = ResolveTypeRef(paramDef->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);
			if (paramsType == NULL)
			{
				// Had error or 'var'
			}
			else if (paramsType->IsGenericParam())
			{
				auto genericParamInstance = GetGenericParamInstance((BfGenericParamType*)paramsType);

				if (genericParamInstance->mTypeConstraint != NULL)
				{
					auto typeInstConstraint = genericParamInstance->mTypeConstraint->ToTypeInstance();
					if ((genericParamInstance->mTypeConstraint->IsDelegate()) || (genericParamInstance->mTypeConstraint->IsFunction()) ||
						((typeInstConstraint != NULL) &&
						 ((typeInstConstraint->IsInstanceOf(mCompiler->mDelegateTypeDef)) || (typeInstConstraint->IsInstanceOf(mCompiler->mFunctionTypeDef)))))
					{
						BfLocalVariable* localVar = new BfLocalVariable();
						localVar->mName = paramDef->mName;
						localVar->mResolvedType = paramsType;
						localVar->mCompositeCount = 0;
						DoAddLocalVariable(localVar);
					}
				}
			}
			else if (paramsType->IsDelegate())
			{
				BfMethodInstance* invokeMethodInstance = GetRawMethodInstanceAtIdx(paramsType->ToTypeInstance(), 0, "Invoke");
				if (invokeMethodInstance != NULL)
				{
					// This is only for the case where the delegate didn't actually have any params so the composite is zero-sized
					//  and wasn't cleared by the preceding loop
					BF_ASSERT(invokeMethodInstance->GetParamCount() == 0);

					BfLocalVariable* localVar = new BfLocalVariable();
					localVar->mName = paramDef->mName;
					localVar->mResolvedType = paramsType;
					localVar->mCompositeCount = 0;
					DoAddLocalVariable(localVar);
				}
			}
		}
	}
}

void BfModule::ProcessMethod_ProcessDeferredLocals(int startIdx)
{
// 	if (mCurMethodState->mPrevMethodState != NULL) // Re-entry
// 		return;

	auto startMethodState = mCurMethodState;

	auto _ClearState = [&]()
	{
		mCurMethodState->mLocalMethods.Clear();

		for (auto& local : mCurMethodState->mLocals)
		{
			if (local->mIsBumpAlloc)
				local->~BfLocalVariable();
			else
				delete local;
		}
		mCurMethodState->mLocals.Clear();
		mCurMethodState->mLocalVarSet.Clear();
	};

	while (true)
	{
		bool didWork = false;
		// Don't process local methods if we had a build error - this isn't just an optimization, it keeps us from showing the same error twice since
		//  we show errors in the capture phase.  If we somehow pass the capture phase without error then this method WILL show any errors here,
		//  however (compiler bug), so this is the safest way
		if (!mCurMethodState->mDeferredLocalMethods.IsEmpty())
		{
			for (int deferredLocalMethodIdx = 0; deferredLocalMethodIdx < (int)mCurMethodState->mDeferredLocalMethods.size(); deferredLocalMethodIdx++)
			{
				auto deferredLocalMethod = mCurMethodState->mDeferredLocalMethods[deferredLocalMethodIdx];

				BfLogSysM("Processing deferred local method %p\n", deferredLocalMethod);

				if (!mHadBuildError)
				{
					// Process as a closure - that allows us to look back and see the const locals and stuff
					BfClosureState closureState;
					mCurMethodState->mClosureState = &closureState;
					closureState.mConstLocals = deferredLocalMethod->mConstLocals;
					closureState.mReturnType = deferredLocalMethod->mMethodInstance->mReturnType;
					closureState.mActiveDeferredLocalMethod = deferredLocalMethod;
					closureState.mLocalMethod = deferredLocalMethod->mLocalMethod;
					if (deferredLocalMethod->mMethodInstance->mMethodInfoEx != NULL)
						closureState.mClosureInstanceInfo = deferredLocalMethod->mMethodInstance->mMethodInfoEx->mClosureInstanceInfo;
					mCurMethodState->mMixinState = deferredLocalMethod->mLocalMethod->mDeclMixinState;

					_ClearState();

					for (auto& constLocal : deferredLocalMethod->mConstLocals)
					{
						auto localVar = new BfLocalVariable();
						*localVar = constLocal;
						DoAddLocalVariable(localVar);
					}

					mCurMethodState->mLocalMethods = deferredLocalMethod->mLocalMethods;

					bool doProcess = !mCompiler->mCanceling;
					if (doProcess)
					{
						BP_ZONE_F("ProcessMethod local %s", deferredLocalMethod->mMethodInstance->mMethodDef->mName.c_str());
						ProcessMethod(deferredLocalMethod->mMethodInstance);
					}
				}
				delete deferredLocalMethod;

				mContext->CheckLockYield();

				didWork = true;
			}
			mCurMethodState->mDeferredLocalMethods.Clear();
		}

		for (auto& kv : mCurMethodState->mLocalMethodCache)
		{
			BF_ASSERT((kv.mValue->mMethodDef == NULL) || (kv.mKey == kv.mValue->mMethodDef->mName));
		}

		if ((!mCurMethodState->mDeferredLambdaInstances.IsEmpty()) && (!mHadBuildError))
		{
			for (int deferredLambdaIdx = 0; deferredLambdaIdx < (int)mCurMethodState->mDeferredLambdaInstances.size(); deferredLambdaIdx++)
			{
				auto lambdaInstance = mCurMethodState->mDeferredLambdaInstances[deferredLambdaIdx];
				BfLogSysM("Processing deferred lambdaInstance %p\n", lambdaInstance);

				BfClosureState closureState;
				mCurMethodState->mClosureState = &closureState;
				//closureState.mConstLocals = deferredLocalMethod->mConstLocals;
				closureState.mReturnType = lambdaInstance->mMethodInstance->mReturnType;
				if (lambdaInstance->mClosureTypeInstance != NULL)
					closureState.mClosureType = lambdaInstance->mClosureTypeInstance;
				else
					closureState.mClosureType = lambdaInstance->mDelegateTypeInstance;
				closureState.mClosureInstanceInfo = lambdaInstance->mMethodInstance->mMethodInfoEx->mClosureInstanceInfo;
				closureState.mDeclaringMethodIsMutating = lambdaInstance->mDeclaringMethodIsMutating;
				mCurMethodState->mMixinState = lambdaInstance->mDeclMixinState;

				_ClearState();
				for (auto& constLocal : lambdaInstance->mConstLocals)
				{
					//if (constLocal.mIsThis)
					{
						// Valueless 'this'
						closureState.mConstLocals.Add(constLocal);
					}
					/*else
					{
						auto localVar = new BfLocalVariable();
						*localVar = constLocal;
						DoAddLocalVariable(localVar);
					}*/
				}

				BfMethodInstanceGroup methodInstanceGroup;
				methodInstanceGroup.mOwner = mCurTypeInstance;
				methodInstanceGroup.mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;

				bool doProcess = !mCompiler->mCanceling;
				if (doProcess)
				{
					BP_ZONE_F("ProcessMethod lambdaInstance %s", lambdaInstance->mMethodInstance->mMethodDef->mName.c_str());
					lambdaInstance->mMethodInstance->mMethodInstanceGroup = &methodInstanceGroup;
					ProcessMethod(lambdaInstance->mMethodInstance);
					lambdaInstance->mMethodInstance->mMethodInstanceGroup = NULL;

					if (lambdaInstance->mDtorMethodInstance != NULL)
					{
						lambdaInstance->mDtorMethodInstance->mMethodInstanceGroup = &methodInstanceGroup;

						auto startMethodState2 = mCurMethodState;

						ProcessMethod(lambdaInstance->mDtorMethodInstance);
						lambdaInstance->mDtorMethodInstance->mMethodInstanceGroup = NULL;
					}
				}

				didWork = true;
			}

			mContext->CheckLockYield();
			mCurMethodState->mDeferredLambdaInstances.Clear();
		}

		if (!didWork)
			break;
	}
}

void BfModule::EmitGCMarkValue(BfTypedValue& thisValue, BfType* checkType, int memberDepth, int curOffset, HashSet<int>& objectOffsets, BfModuleMethodInstance markFromGCThreadMethodInstance, bool isAppendObject)
{
	if (checkType->IsComposite())
		PopulateType(checkType, BfPopulateType_Data);

	if (!checkType->WantsGCMarking())
		return;

	auto typeInstance = checkType->ToTypeInstance();
	bool callMarkMethod = false;

	// Call the actual marking method
	if (memberDepth == 0)
	{
		// Don't call marking method on self
	}
	else if (checkType->IsObjectOrInterface())
	{
		if (!objectOffsets.Add(curOffset))
		{
			return;
		}
		callMarkMethod = true;
	}
	else if (checkType->IsSizedArray())
	{
		BfSizedArrayType* sizedArrayType = (BfSizedArrayType*)checkType;
		if (sizedArrayType->mElementType->WantsGCMarking())
		{
			BfTypedValue arrayValue = thisValue;
			if (thisValue.mType != checkType)
			{
				BfIRValue srcValue = thisValue.mValue;

				if (curOffset != 0)
				{
					if (!thisValue.mType->IsPointer())
					{
						auto int8Type = GetPrimitiveType(BfTypeCode_UInt8);
						auto int8PtrType = CreatePointerType(int8Type);
						srcValue = mBfIRBuilder->CreateBitCast(thisValue.mValue, mBfIRBuilder->MapType(int8PtrType));
					}

					srcValue = mBfIRBuilder->CreateInBoundsGEP(srcValue, curOffset);
				}

				arrayValue = BfTypedValue(mBfIRBuilder->CreateBitCast(srcValue, mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(checkType))), checkType, BfTypedValueKind_Addr);
			}

			if (sizedArrayType->mElementCount > 6)
			{
				auto intPtrType = GetPrimitiveType(BfTypeCode_IntPtr);
				auto itr = CreateAlloca(intPtrType);
				mBfIRBuilder->CreateStore(GetDefaultValue(intPtrType), itr);

				auto loopBB = mBfIRBuilder->CreateBlock("loop", true);
				auto bodyBB = mBfIRBuilder->CreateBlock("body", true);
				auto doneBB = mBfIRBuilder->CreateBlock("done", true);
				mBfIRBuilder->CreateBr(loopBB);

				mBfIRBuilder->SetInsertPoint(loopBB);
				auto loadedItr = mBfIRBuilder->CreateLoad(itr);
				auto cmpRes = mBfIRBuilder->CreateCmpGTE(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)sizedArrayType->mElementCount), true);
				mBfIRBuilder->CreateCondBr(cmpRes, doneBB, bodyBB);

				mBfIRBuilder->SetInsertPoint(bodyBB);

				auto ptrType = CreatePointerType(sizedArrayType->mElementType);
				auto ptrValue = mBfIRBuilder->CreateBitCast(arrayValue.mValue, mBfIRBuilder->MapType(ptrType));
				auto gepResult = mBfIRBuilder->CreateInBoundsGEP(ptrValue, loadedItr);
				auto value = BfTypedValue(gepResult, sizedArrayType->mElementType, BfTypedValueKind_Addr);

				EmitGCMarkValue(value, markFromGCThreadMethodInstance);

				auto incValue = mBfIRBuilder->CreateAdd(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
				mBfIRBuilder->CreateStore(incValue, itr);
				mBfIRBuilder->CreateBr(loopBB);

				mBfIRBuilder->SetInsertPoint(doneBB);
			}
			else
			{
				for (int dataIdx = 0; dataIdx < sizedArrayType->mElementCount; dataIdx++)
				{
					auto ptrType = CreatePointerType(sizedArrayType->mElementType);
					auto ptrValue = mBfIRBuilder->CreateBitCast(arrayValue.mValue, mBfIRBuilder->MapType(ptrType));
					auto gepResult = mBfIRBuilder->CreateInBoundsGEP(ptrValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, dataIdx));
					auto value = BfTypedValue(gepResult, sizedArrayType->mElementType, BfTypedValueKind_Addr);

					HashSet<int> objectOffsets;
					EmitGCMarkValue(value, markFromGCThreadMethodInstance);
				}
			}
		}
	}
	else if (typeInstance != NULL)
	{
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMemberSetEntry* entry = NULL;
		BfMethodDef* methodDef = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGetWith(String(BF_METHODNAME_MARKMEMBERS), &entry))
		{
			methodDef = (BfMethodDef*)entry->mMemberDef;
			if (methodDef->HasBody())
			{
				callMarkMethod = true;
			}
		}
	}

	if (callMarkMethod)
	{
		BfTypedValue markValue;
		auto memberType = checkType;
		if (memberType->IsObjectOrInterface())
			memberType = mContext->mBfObjectType; // Just to avoid that extra cast for the call itself
		auto memberPtrType = CreatePointerType(memberType);

		if (curOffset == 0)
		{
			markValue = BfTypedValue(mBfIRBuilder->CreateBitCast(thisValue.mValue, mBfIRBuilder->MapType(memberPtrType)), memberType, true);
		}
		else
		{
			if (!thisValue.mType->IsPointer())
			{
				auto int8Type = GetPrimitiveType(BfTypeCode_UInt8);
				auto int8PtrType = CreatePointerType(int8Type);

				thisValue = BfTypedValue(mBfIRBuilder->CreateBitCast(thisValue.mValue, mBfIRBuilder->MapType(int8PtrType)), int8PtrType);
			}

			auto offsetValue = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, curOffset);
			markValue = BfTypedValue(mBfIRBuilder->CreateBitCast(offsetValue, mBfIRBuilder->MapType(memberPtrType)), memberType, true);
		}

		if (isAppendObject)
			EmitGCMarkAppended(markValue);
		else
			EmitGCMarkValue(markValue, markFromGCThreadMethodInstance);
		return;
	}

	auto methodDef = mCurMethodInstance->mMethodDef;

	if (checkType->IsPayloadEnum())
	{
		for (auto& fieldInst : typeInstance->mFieldInstances)
		{
			if (!fieldInst.mIsEnumPayloadCase)
				continue;
			auto fieldDef = fieldInst.GetFieldDef();
			EmitGCMarkValue(thisValue, fieldInst.mResolvedType, memberDepth + 1, curOffset, objectOffsets, markFromGCThreadMethodInstance);
		}
		return;
	}

	if (typeInstance == NULL)
		return;

	for (auto& fieldInst : typeInstance->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if (fieldDef == NULL)
			continue;
		if (fieldDef->mIsStatic)
			continue;
		if (typeInstance == mCurTypeInstance)
		{
			// Note: we don't do method chaining when we are marking members of members. Theoretically this means
			//  we may be marking member values that can never contain a value anyway, but this is benign
			if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
				continue;
		}

		EmitGCMarkValue(thisValue, fieldInst.mResolvedType, memberDepth + 1, curOffset + fieldInst.mDataOffset, objectOffsets, markFromGCThreadMethodInstance, fieldInst.IsAppendedObject());
	}

	if ((typeInstance->mBaseType != NULL) && (typeInstance->mBaseType != mContext->mBfObjectType))
	{
		EmitGCMarkValue(thisValue, typeInstance->mBaseType, memberDepth, curOffset, objectOffsets, markFromGCThreadMethodInstance);
	}
}

void BfModule::EmitGCMarkMembers()
{
	auto methodDef = mCurMethodInstance->mMethodDef;

	auto gcType = ResolveTypeDef(mCompiler->mGCTypeDef, BfPopulateType_DataAndMethods);
	BfModuleMethodInstance markFromGCThreadMethodInstance;
	if (gcType != NULL)
		markFromGCThreadMethodInstance = GetMethodByName(gcType->ToTypeInstance(), "Mark", 1);

	if ((markFromGCThreadMethodInstance.mMethodInstance != NULL) && (!mCurMethodInstance->mIsUnspecialized))
	{
		if (mCurTypeInstance->IsBoxed())
		{
			// This already gets filled in by the normal boxed forwarding
		}
		else
		{
			auto thisValue = GetThis();
			if (thisValue.IsSplat())
			{
				BfIRFunction func = mCurMethodInstance->mIRFunction;
				int argIdx = 0;

				std::function<void(BfType*)> checkTypeLambda = [&](BfType* checkType)
				{
					if (checkType->IsStruct())
					{
						auto checkTypeInstance = checkType->ToTypeInstance();
						if (checkTypeInstance->mBaseType != NULL)
							checkTypeLambda(checkTypeInstance->mBaseType);
						for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
						{
							auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
							if (fieldInstance->mDataIdx >= 0)
								checkTypeLambda(fieldInstance->GetResolvedType());
						}
					}
					else if (checkType->IsMethodRef())
					{
						BFMODULE_FATAL(this, "Not handled");
					}
					else if (checkType->WantsGCMarking())
					{
						BfTypedValue markValue(mBfIRBuilder->GetArgument(argIdx), checkType);
						EmitGCMarkValue(markValue, markFromGCThreadMethodInstance);
						argIdx++;
					}
				};

				checkTypeLambda(thisValue.mType);
			}
			else if (!methodDef->mIsStatic)
			{
				if ((mCurTypeInstance->mBaseType != NULL) && (!mCurTypeInstance->IsTypedPrimitive()) && (mCurTypeInstance->mBaseType->WantsGCMarking()))
				{
					BfModuleMethodInstance moduleMethodInst = GetMethodByName(mCurTypeInstance->mBaseType, BF_METHODNAME_MARKMEMBERS, 0, true);
					if (moduleMethodInst)
					{
						auto methodBaseType = moduleMethodInst.mMethodInstance->GetOwner();
						bool wantsBaseMarking = true;
						if (methodBaseType == mContext->mBfObjectType)
						{
							wantsBaseMarking = false;
							PopulateType(methodBaseType);
							for (auto& fieldInstance : mContext->mBfObjectType->mFieldInstances)
							{
								if ((fieldInstance.GetFieldDef()->mDeclaringType->IsExtension()) &&
									(fieldInstance.mResolvedType->WantsGCMarking()))
									wantsBaseMarking = true;
							}
						}

						if (wantsBaseMarking)
						{
							auto thisValue = GetThis();
							auto baseValue = Cast(NULL, thisValue, methodBaseType, BfCastFlags_Explicit);

							SizedArray<BfIRValue, 1> args;
							if ((!mIsComptimeModule) && (moduleMethodInst.mMethodInstance->GetParamIsSplat(-1)))
							{
								BfExprEvaluator exprEvaluator(this);
								exprEvaluator.SplatArgs(baseValue, args);
							}
							else
							{
								args.push_back(baseValue.mValue);
							}

							mBfIRBuilder->CreateCall(moduleMethodInst.mFunc, args);
						}
					}
				}
			}

			if (!methodDef->mIsStatic)
			{
				HashSet<int> objectOffsets;
				EmitGCMarkValue(thisValue, mCurTypeInstance, 0, 0, objectOffsets, markFromGCThreadMethodInstance);
			}
			else
			{
				for (auto& fieldInst : mCurTypeInstance->mFieldInstances)
				{
					if (thisValue.IsSplat())
						break;

					auto fieldDef = fieldInst.GetFieldDef();
					if (fieldDef == NULL)
						continue;
					if (fieldInst.mIsThreadLocal)
						continue;

					if (fieldDef->mIsStatic == methodDef->mIsStatic)
					{
						// For extensions, only handle these fields in the appropriate extension
						if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
							continue;
						if (!fieldInst.mFieldIncluded)
							continue;

						auto fieldType = fieldInst.mResolvedType;
						auto fieldDef = fieldInst.GetFieldDef();
						BfTypedValue markVal;

						if (fieldDef->mIsConst)
							continue;

						if ((!fieldType->IsObjectOrInterface()) && (!fieldType->IsComposite()))
							continue;

						mBfIRBuilder->PopulateType(fieldType);

						if (fieldType->IsValuelessType())
							continue;

						if ((fieldDef->mIsStatic) && (!fieldDef->mIsConst))
						{
							StringT<512> staticVarName;
							BfMangler::Mangle(staticVarName, mCompiler->GetMangleKind(), &fieldInst);
							if (staticVarName.StartsWith('#'))
								continue;
							markVal = ReferenceStaticField(&fieldInst);
						}
						else if (!fieldDef->mIsStatic)
						{
							if (fieldInst.IsAppendedObject())
							{
								auto fieldAddr = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, fieldInst.mDataIdx);
								auto val = mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(mContext->mBfObjectType));
								markVal = BfTypedValue(mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(fieldInst.mResolvedType)), fieldInst.mResolvedType);
							}
							else
							{
								markVal = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInst.mDataIdx/*, fieldDef->mName*/), fieldInst.mResolvedType, true);
							}
						}

						if (markVal)
						{
							if (fieldInst.IsAppendedObject())
								EmitGCMarkAppended(markVal);
							else
								EmitGCMarkValue(markVal, markFromGCThreadMethodInstance);
						}
					}
				}
			}

			if (mCurMethodInstance->mChainType == BfMethodChainType_ChainHead)
				CallChainedMethods(mCurMethodInstance, false);
		}
	}
}

void BfModule::EmitGCFindTLSMembers()
{
	auto methodDef = mCurMethodInstance->mMethodDef;

	auto gcTypeInst = ResolveTypeDef(mCompiler->mGCTypeDef)->ToTypeInstance();

	auto reportTLSMark = GetMethodByName(gcTypeInst, "ReportTLSMember", 2);
	BF_ASSERT(reportTLSMark);

	for (auto& fieldInst : mCurTypeInstance->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if (fieldDef == NULL)
			continue;
		if (!fieldInst.mIsThreadLocal)
			continue;

		// For extensions, only handle these fields in the appropriate extension
		if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
			continue;
		if (!fieldInst.mFieldIncluded)
			continue;

		if (fieldDef->mIsConst)
			continue;

		auto fieldType = fieldInst.mResolvedType;

		if ((!fieldType->IsObjectOrInterface()) && (!fieldType->IsComposite()))
			continue;

		if (fieldType->IsValuelessType())
			continue;

		BfTypedValue markVal = ReferenceStaticField(&fieldInst);
		if (markVal)
		{
			BfIRValue fieldRefPtr = mBfIRBuilder->CreateBitCast(markVal.mValue, mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_NullPtr)));
			BfIRValue markFuncPtr = GetMarkFuncPtr(fieldType);

			SizedArray<BfIRValue, 2> llvmArgs;
			llvmArgs.push_back(fieldRefPtr);
			llvmArgs.push_back(markFuncPtr);
			mBfIRBuilder->CreateCall(reportTLSMark.mFunc, llvmArgs);
		}
	}

	if (mCurMethodInstance->mChainType == BfMethodChainType_ChainHead)
		CallChainedMethods(mCurMethodInstance, false);
}

void BfModule::ProcessMethod(BfMethodInstance* methodInstance, bool isInlineDup, bool forceIRWrites)
{
	BP_ZONE_F("BfModule::ProcessMethod %s", BP_DYN_STR(methodInstance->mMethodDef->mName.c_str()));

	if (mIsComptimeModule)
	{
		BF_ASSERT(!mCompiler->IsAutocomplete());
	}

	if (mAwaitingInitFinish)
		FinishInit();

	if (!methodInstance->mIsReified)
		BF_ASSERT(!mIsReified);

	BF_ASSERT((!methodInstance->GetOwner()->IsUnspecializedTypeVariation()) || (mIsComptimeModule));

	if (methodInstance->mMethodInfoEx != NULL)
	{
		for (auto methodGenericArg : methodInstance->mMethodInfoEx->mMethodGenericArguments)
		{
			if (methodGenericArg->IsMethodRef())
			{
				auto methodRefType = (BfMethodRefType*)methodGenericArg;
				BF_ASSERT(methodRefType->mOwnerRevision == methodRefType->mOwner->mRevision);
			}
		}
	}

	if (mBfIRBuilder == NULL)
	{
		BfLogSysM("ProcessMethod %p calling EnsureIRBuilder\n", methodInstance);
		EnsureIRBuilder();
	}

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, (mWantsIRIgnoreWrites || methodInstance->mIsUnspecialized) && (!forceIRWrites));

	if ((HasCompiledOutput()) && (!mBfIRBuilder->mIgnoreWrites))
	{
		BF_ASSERT(!methodInstance->mIRFunction.IsFake() || (methodInstance->GetImportCallKind() != BfImportCallKind_None));
	}

	SetAndRestoreValue<bool> prevIsClassifying;
	if (((methodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend) || (methodInstance->mIsForeignMethodDef) || (methodInstance->IsSpecializedGenericMethod())) &&
		(mCompiler->mResolvePassData != NULL))
	{
		// Don't classify on the CtorCalcAppend, just on the actual Ctor
		prevIsClassifying.Init(mCompiler->mResolvePassData->mIsClassifying, false);
	}

	if (methodInstance->mHasBeenProcessed)
	{
		BfLogSysM("ProcessMethod %p HasBeenProcessed\n", methodInstance);
		return;
	}

	if (methodInstance->mFailedConstraints)
	{
		// The only way this should be able to happen is if we fail our generic param check
		BF_ASSERT(methodInstance->IsSpecializedGenericMethod());
		BfLogSysM("Method instance marked as 'failed' at start of ProcessMethod %p\n", methodInstance);
		methodInstance->mHasBeenProcessed = true;
		return;
	}

	if (HasExecutedOutput())
	{
		BF_ASSERT(mIsModuleMutable);
	}

	BfMethodInstance* defaultMethodInstance = methodInstance->mMethodInstanceGroup->mDefault;

	if (!mIsComptimeModule)
	{
		BF_ASSERT(methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_NotSet);

		if ((methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude) &&
			(methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Referenced))
		{
			auto owningModule = methodInstance->GetOwner()->mModule;

			if (owningModule->mIsScratchModule)
			{
				BF_ASSERT(owningModule->mOnDemandMethodCount == 0);
			}
			else
			{
				BF_ASSERT((owningModule->mOnDemandMethodCount > 0) || (!HasCompiledOutput()) || (owningModule->mExtensionCount > 0));
				if (owningModule->mOnDemandMethodCount > 0)
					owningModule->mOnDemandMethodCount--;
			}

			methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Referenced;
			VerifyOnDemandMethods();
		}
	}

	// We set mHasBeenProcessed to true immediately -- this helps avoid stack overflow during recursion for things like
	//  self-referencing append allocations in ctor@calcAppend
	methodInstance->mHasBeenProcessed = true;
	mIncompleteMethodCount--;
	BF_ASSERT((mIsSpecialModule) || (mIncompleteMethodCount >= 0));

	auto typeDef = methodInstance->mMethodInstanceGroup->mOwner->mTypeDef;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();

	if ((methodDef->mHasComptime) && (!mIsComptimeModule))
		mBfIRBuilder->mIgnoreWrites = true;

	if ((methodInstance->mIsReified) && (methodInstance->mVirtualTableIdx != -1))
	{
		// If we reify a virtual method in a HotCompile then we need to make sure the type data gets written out
		//  so we get the new vdata for it
		methodInstance->GetOwner()->mDirty = true;
	}

	int dependentGenericStartIdx = 0;
	if ((methodDef->mIsLocalMethod) && (mCurMethodState != NULL)) // See DoMethodDeclaration for an explaination of dependentGenericStartIdx
		dependentGenericStartIdx = (int)mCurMethodState->GetRootMethodState()->mMethodInstance->GetNumGenericArguments();

	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, methodInstance->mMethodInstanceGroup->mOwner);
	SetAndRestoreValue<BfFilePosition> prevFilePos(mCurFilePosition);
	SetAndRestoreValue<bool> prevHadBuildError(mHadBuildError, false);
	SetAndRestoreValue<bool> prevHadWarning(mHadBuildWarning, false);
	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors);
	SetAndRestoreValue<bool> prevIgnoreWarnings(mIgnoreWarnings, mIsComptimeModule);

	if ((!mIsComptimeModule) && (methodInstance->mIsReified) &&
		((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorNoBody)))
	{
		mCurTypeInstance->mHasBeenInstantiated = true;
	}

	// Warnings are shown in the capture phase
	if ((methodDef->mIsLocalMethod) || (mCurTypeInstance->IsClosure()))
		mIgnoreWarnings = true;

	if ((methodDeclaration == NULL) && (mCurMethodState == NULL))
		UseDefaultSrcPos();

	if ((mIsComptimeModule) && (methodDef->mIsStatic))
	{
		PopulateType(mCurTypeInstance, BfPopulateType_AllowStaticMethods);
	}
	else
	{
		// We may not actually be populated in relatively rare autocompelte cases
		PopulateType(mCurTypeInstance, BfPopulateType_DataAndMethods);
		mBfIRBuilder->PopulateType(mCurTypeInstance, BfIRPopulateType_Full);
	}

	BfAstNode* nameNode = NULL;
	if (methodDeclaration != NULL)
	{
		nameNode = methodDeclaration->mNameNode;
		if (nameNode == NULL)
		{
			if (auto ctorDeclaration = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
				nameNode = ctorDeclaration->mThisToken;
		}
	}
	if ((mCompiler->mResolvePassData != NULL) && (methodDeclaration != NULL) && (nameNode != NULL) &&
		(mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Method) &&
		(methodDef->mIdx >= 0) && (!methodInstance->mIsForeignMethodDef))
	{
		if (methodInstance->GetExplicitInterface() == NULL)
			mCompiler->mResolvePassData->HandleMethodReference(nameNode, typeDef, methodDef);
		else
		{
			for (auto& ifaceEntry : mCurTypeInstance->mInterfaces)
			{
				auto ifaceInst = ifaceEntry.mInterfaceType;
				int startIdx = ifaceEntry.mStartInterfaceTableIdx;
				int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();

				if (methodInstance->GetExplicitInterface() != ifaceInst)
					continue;

				ifaceInst->mTypeDef->PopulateMemberSets();
				BfMethodDef* nextMethod = NULL;
				BfMemberSetEntry* entry = NULL;
				if (ifaceInst->mTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
					nextMethod = (BfMethodDef*)entry->mMemberDef;

				while (nextMethod != NULL)
				{
					auto checkMethod = nextMethod;
					nextMethod = nextMethod->mNextWithSameName;

					BfMethodInstance* implMethodInst = mCurTypeInstance->mInterfaceMethodTable[startIdx + checkMethod->mIdx].mMethodRef;
					if (implMethodInst == methodInstance)
					{
						auto ifaceMethodDef = ifaceInst->mTypeDef->mMethods[checkMethod->mIdx];
						mCompiler->mResolvePassData->HandleMethodReference(nameNode, ifaceInst->mTypeDef, ifaceMethodDef);
					}
				}
			}
		}

		if (methodDef->mIsOverride)
		{
			for (int virtIdx = 0; virtIdx < (int)mCurTypeInstance->mVirtualMethodTable.size(); virtIdx++)
			{
				auto& ventry = mCurTypeInstance->mVirtualMethodTable[virtIdx];
				if (ventry.mDeclaringMethod.mMethodNum == -1)
					continue;
				BfMethodInstance* virtMethod = ventry.mImplementingMethod;
				if (virtMethod != NULL)
				{
					if (virtMethod == methodInstance)
					{
						// Is matching method
						if (virtIdx < (int)mCurTypeInstance->mBaseType->mVirtualMethodTable.size())
						{
							auto baseType = mCurTypeInstance->mBaseType;
							if (baseType != NULL)
							{
								while ((baseType->mBaseType != NULL) && (virtIdx < baseType->mBaseType->mVirtualMethodTableSize))
									baseType = baseType->mBaseType;
								BfMethodInstance* baseVirtMethod = baseType->mVirtualMethodTable[virtIdx].mImplementingMethod;
								mCompiler->mResolvePassData->HandleMethodReference(nameNode, baseType->mTypeDef, baseVirtMethod->mMethodDef);
							}
						}

						break;
					}
				}
			}
		}
	}

	BfLogSysM("ProcessMethod %p Unspecialized: %d Module: %p IRFunction: %d Reified: %d Incomplete:%d\n", methodInstance, mCurTypeInstance->IsUnspecializedType(), this, methodInstance->mIRFunction.mId, methodInstance->mIsReified, mIncompleteMethodCount);

	int importStrNum = -1;
	auto importKind = methodInstance->GetImportKind();
	if ((!mCompiler->mIsResolveOnly) &&
		((importKind == BfImportKind_Import_Static) || (importKind == BfImportKind_Import_Dynamic)))
	{
		if (auto customAttr = methodInstance->GetCustomAttributes()->Get(mCompiler->mImportAttributeTypeDef))
		{
			if (customAttr->mCtorArgs.size() == 1)
			{
				auto fileNameArg = customAttr->mCtorArgs[0];
				auto constant = mCurTypeInstance->mConstHolder->GetConstant(fileNameArg);
				if (constant != NULL)
				{
					if (!constant->IsNull())
						importStrNum = constant->mInt32;
				}
				else
				{
					importStrNum = GetStringPoolIdx(fileNameArg, mCurTypeInstance->mConstHolder);
				}
				if (importStrNum != -1)
				{
					if (!mStringPoolRefs.Contains(importStrNum))
						mStringPoolRefs.Add(importStrNum);
				}
			}
		}
	}

	if (methodInstance->GetImportCallKind() != BfImportCallKind_None)
	{
		if (mBfIRBuilder->mIgnoreWrites)
			return;

		BfLogSysM("DllImportGlobalVar processing %p\n", methodInstance);

		if (!methodInstance->mIRFunction)
		{
			BfIRValue dllImportGlobalVar = CreateDllImportGlobalVar(methodInstance, true);
			methodInstance->mIRFunction = mBfIRBuilder->GetFakeVal();
			BF_ASSERT(dllImportGlobalVar || methodInstance->mHasFailed);
			mFuncReferences[mCurMethodInstance] = dllImportGlobalVar;
		}

		if (HasCompiledOutput())
		{
			BfDllImportEntry dllImportEntry;
			dllImportEntry.mFuncVar = methodInstance->mIRFunction;
			dllImportEntry.mMethodInstance = mCurMethodInstance;
			mDllImportEntries.push_back(dllImportEntry);
		}
		return;
	}

	StringT<512> mangledName;
	BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), mCurMethodInstance);
	if (!methodInstance->mIRFunction)
	{
		bool isIntrinsic = false;
		SetupIRFunction(methodInstance, mangledName, false, &isIntrinsic);
	}
	if (methodInstance->mIsIntrinsic)
		return;
	if (mCurTypeInstance->IsFunction())
		return;

	auto prevActiveFunction = mBfIRBuilder->GetActiveFunction();
	mBfIRBuilder->SetActiveFunction(mCurMethodInstance->mIRFunction);

	if (methodDef->mBody != NULL)
		UpdateSrcPos(methodDef->mBody, BfSrcPosFlag_NoSetDebugLoc);
	else if (methodDeclaration != NULL)
		UpdateSrcPos(methodDeclaration, BfSrcPosFlag_NoSetDebugLoc);
	else if ((methodDef->mDeclaringType != NULL) && (methodDef->mDeclaringType->GetRefNode() != NULL))
		UpdateSrcPos(methodDef->mDeclaringType->GetRefNode(), BfSrcPosFlag_NoSetDebugLoc);
	else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
		UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration, BfSrcPosFlag_NoSetDebugLoc);

	if ((mCurMethodState == NULL) && (!IsInSpecializedSection())) // Only do initial classify for the 'outer' method state, not any local methods or lambdas
	{
		if ((mCompiler->mIsResolveOnly) && (!mIsComptimeModule) && (methodDef->mBody != NULL) && (!mCurTypeInstance->IsBoxed()))
		{
			if (auto sourceClassifier = mCompiler->mResolvePassData->GetSourceClassifier(methodDef->mBody))
				sourceClassifier->VisitChildNoRef(methodDef->mBody);
		}
	}

	BfHotDataReferenceBuilder hotDataReferenceBuilder;
	BfMethodState methodState;
	if (mCurMethodState != NULL)
		methodState.mClosureState = mCurMethodState->mClosureState;

	if ((mCompiler->mOptions.mAllowHotSwapping) && (methodInstance->mIsReified) && (!methodInstance->mIsUnspecialized) && (!isInlineDup))
	{
		//BF_ASSERT(methodInstance->mHotMethod != NULL);
		if (methodInstance->mHotMethod == NULL)
			CheckHotMethod(methodInstance, mangledName);
		methodState.mHotDataReferenceBuilder = &hotDataReferenceBuilder;
	}

	methodState.mPrevMethodState = mCurMethodState;
	methodState.mMethodInstance = methodInstance;
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);

	if (methodInstance->GetCustomAttributes() != NULL)
	{
		int typeOptionsIdx = GenerateTypeOptions(methodInstance->GetCustomAttributes(), mCurTypeInstance, false);
		if (typeOptionsIdx != -1)
			methodState.mMethodTypeOptions = mSystem->GetTypeOptions(typeOptionsIdx);
	}

	BfTypeState typeState(mCurTypeInstance);
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	bool isGenericVariation = (methodInstance->mIsUnspecializedVariation) || (mCurTypeInstance->IsUnspecializedTypeVariation());

	BfMethodInstance* unspecializedMethodInstance = NULL;
	if ((prevMethodState.mPrevVal != NULL) &&
		((methodState.mClosureState == NULL) || (methodState.mClosureState->mActiveDeferredLocalMethod == NULL)))
	{
		// This is 'inner' (probably a closure) - use binding from outer function
		methodState.mGenericTypeBindings = prevMethodState.mPrevVal->mGenericTypeBindings;
	}
	else if ((methodInstance->mIsUnspecialized) ||
		((mCurTypeInstance->IsUnspecializedType()) && (!isGenericVariation)))
	{
		methodState.mGenericTypeBindings = &methodInstance->GetMethodInfoEx()->mGenericTypeBindings;
	}
	else if ((((methodInstance->mMethodInfoEx != NULL) && ((int)methodInstance->mMethodInfoEx->mMethodGenericArguments.size() > dependentGenericStartIdx)) ||
		((mCurTypeInstance->IsGenericTypeInstance()) && (!isGenericVariation || mIsComptimeModule) && (!methodInstance->mMethodDef->mIsLocalMethod) && (!methodInstance->mMethodDef->mDeclaringType->IsEmitted()))))
	{
		unspecializedMethodInstance = GetUnspecializedMethodInstance(methodInstance, !methodInstance->mMethodDef->mIsLocalMethod);

		if ((mIsComptimeModule) && (!unspecializedMethodInstance->mHasBeenProcessed))
		{
			// This will have already been populated by CeMachine
			methodState.mGenericTypeBindings = &methodInstance->GetMethodInfoEx()->mGenericTypeBindings;
		}
		else
		{
			BF_ASSERT(unspecializedMethodInstance != methodInstance);
			if (!unspecializedMethodInstance->mHasBeenProcessed)
			{
				// Make sure the unspecialized method is processed so we can take its bindings
				// Clear mCurMethodState so we don't think we're in a local method
				SetAndRestoreValue<BfMethodState*> prevMethodState_Unspec(mCurMethodState, prevMethodState.mPrevVal);
				if (unspecializedMethodInstance->mMethodProcessRequest == NULL)
					unspecializedMethodInstance->mDeclModule->mIncompleteMethodCount++;
				mContext->ProcessMethod(unspecializedMethodInstance);
			}
			methodState.mGenericTypeBindings = &unspecializedMethodInstance->GetMethodInfoEx()->mGenericTypeBindings;
		}
	}

	if (methodDef->mMethodType == BfMethodType_Operator)
	{
		auto operatorDef = (BfOperatorDef*) methodDef;
		BfAstNode* refNode = operatorDef->mOperatorDeclaration->mOperatorToken;

		auto paramErrorRefNode = refNode;
		if (operatorDef->mOperatorDeclaration->mOpenParen != NULL)
			paramErrorRefNode = operatorDef->mOperatorDeclaration->mOpenParen;
		if ((mCurMethodInstance->GetParamCount() > 0) && (mCurMethodInstance->GetParamTypeRef(0) != NULL))
			paramErrorRefNode = mCurMethodInstance->GetParamTypeRef(0);

		if (operatorDef->mOperatorDeclaration->mBinOp != BfBinaryOp_None)
		{
			if (methodDef->mParams.size() != 2)
			{
				Fail("Binary operators must declare two parameters", paramErrorRefNode);
			}
			else
			{
				auto checkParam0 = mCurMethodInstance->GetParamType(0);
				if ((checkParam0->IsRef()) && (!checkParam0->IsOut()))
					checkParam0 = checkParam0->GetUnderlyingType();
				auto checkParam1 = mCurMethodInstance->GetParamType(1);
				if ((checkParam1->IsRef()) && (!checkParam1->IsOut()))
					checkParam1 = checkParam1->GetUnderlyingType();

				if ((checkParam0 != mCurTypeInstance) && (!checkParam0->IsSelf()) &&
					(checkParam1 != mCurTypeInstance) && (!checkParam1->IsSelf()))
				{
					Fail("At least one of the parameters of a binary operator must be the containing type", paramErrorRefNode);
				}
			}

			BfType* wantType = NULL;
			if (operatorDef->mOperatorDeclaration->mBinOp == BfBinaryOp_Compare)
			{
				wantType = GetPrimitiveType(BfTypeCode_IntPtr);
			}

			if ((wantType != NULL) && (methodInstance->mReturnType != wantType))
				Fail(StrFormat("The return type for the '%s' operator must be '%s'", BfGetOpName(operatorDef->mOperatorDeclaration->mBinOp),
					TypeToString(wantType).c_str()), operatorDef->mOperatorDeclaration->mReturnType);
		}
		else if (operatorDef->mOperatorDeclaration->mUnaryOp != BfUnaryOp_None)
		{
			if (methodDef->mIsStatic)
			{
				if (methodDef->mParams.size() != 1)
				{
					Fail("Unary operators must declare one parameter", paramErrorRefNode);
				}
				else
				{
					auto checkParam0 = mCurMethodInstance->GetParamType(0);
					if ((checkParam0->IsRef()) && (!checkParam0->IsOut()))
						checkParam0 = checkParam0->GetUnderlyingType();

					if ((checkParam0 != mCurTypeInstance) && (!checkParam0->IsSelf()))
					{
						Fail("The parameter of a unary operator must be the containing type", paramErrorRefNode);
					}
				}

				if (((operatorDef->mOperatorDeclaration->mUnaryOp == BfUnaryOp_Increment) ||
					(operatorDef->mOperatorDeclaration->mUnaryOp == BfUnaryOp_Decrement)) &&
					(!TypeIsSubTypeOf(mCurMethodInstance->mReturnType->ToTypeInstance(), mCurTypeInstance)))
				{
					Fail("The return type for the '++' or '--' operator must match the parameter type or be derived from the parameter type", operatorDef->mOperatorDeclaration->mReturnType);
				}
			}
			else
			{
				if (methodDef->mParams.size() != 0)
				{
					Fail("Non-static unary operators not declare any parameters", paramErrorRefNode);
				}

				if (!mCurMethodInstance->mReturnType->IsVoid())
				{
					Fail("The return type for non-static operator must be 'void'", operatorDef->mOperatorDeclaration->mReturnType);
				}
			}
		}
		else if (operatorDef->mOperatorDeclaration->mAssignOp != BfAssignmentOp_None)
		{
			if (methodDef->mParams.size() != 1)
			{
				Fail("Assignment operators must declare one parameter", paramErrorRefNode);
			}

			if (!mCurMethodInstance->mReturnType->IsVoid())
			{
				Fail("The return type for assignment operator must be 'void'", operatorDef->mOperatorDeclaration->mReturnType);
			}
		}
		else // Conversion
		{
			if (!operatorDef->mOperatorDeclaration->mIsConvOperator)
			{
				if (operatorDef->mOperatorDeclaration->mReturnType != NULL)
					Fail("Conversion operators must declare their target type after the 'operator' token, not before", operatorDef->mOperatorDeclaration->mReturnType);
			}

			if (methodDef->mParams.size() != 1)
			{
				auto refNode = paramErrorRefNode;
				if (!operatorDef->mOperatorDeclaration->mCommas.empty())
					refNode = operatorDef->mOperatorDeclaration->mCommas[0];
				Fail("Conversion operators must declare one parameter", refNode);
			}
			else if ((methodInstance->mMethodInfoEx == NULL) ||
				(methodInstance->mMethodInfoEx->mMethodGenericArguments.IsEmpty()) ||
				((methodInstance->mIsUnspecialized) && (!methodInstance->mIsUnspecializedVariation)))
			{
				auto checkParam0 = mCurMethodInstance->GetParamType(0);
				if ((checkParam0->IsRef()) && (!checkParam0->IsOut()))
					checkParam0 = checkParam0->GetUnderlyingType();

				if ((checkParam0 != mCurTypeInstance) && (!checkParam0->IsSelf()) &&
					(mCurMethodInstance->mReturnType != mCurTypeInstance) && (!mCurMethodInstance->mReturnType->IsSelf()))
					Fail("User-defined conversion must convert to or from the enclosing type", paramErrorRefNode);
				if (checkParam0 == mCurMethodInstance->mReturnType)
					Fail("User-defined operator cannot take an object of the enclosing type and convert to an object of the enclosing type", operatorDef->mOperatorDeclaration->mReturnType);

				// On type lookup error we default to 'object', so don't do the 'base class' error if that may have
				//  happened here
				if ((!mHadBuildError) || ((mCurMethodInstance->mReturnType != mContext->mBfObjectType) && (checkParam0 != mContext->mBfObjectType)))
				{
					auto isToBase = TypeIsSubTypeOf(mCurTypeInstance->mBaseType, mCurMethodInstance->mReturnType->ToTypeInstance());
					bool isFromBase = TypeIsSubTypeOf(mCurTypeInstance->mBaseType, checkParam0->ToTypeInstance());

					if ((mCurTypeInstance->IsObject()) && (isToBase || isFromBase))
						Fail("User-defined conversions to or from a base class are not allowed", paramErrorRefNode);
					else if ((mCurTypeInstance->IsStruct()) && (isToBase))
						Fail("User-defined conversions to a base struct are not allowed", paramErrorRefNode);
				}
			}
		}
	}

	bool wantsDIData = (mBfIRBuilder->DbgHasInfo()) && (!methodDef->IsEmptyPartial()) && (!mBfIRBuilder->mIgnoreWrites) && (mHasFullDebugInfo);
	if (methodDef->mMethodType == BfMethodType_Mixin)
		wantsDIData = false;
	if (mCurTypeInstance->IsUnspecializedType())
		wantsDIData = false;

	if ((mCurTypeInstance->IsBoxed()) && (methodDef->mMethodType != BfMethodType_Ctor))
		wantsDIData = false;

	bool wantsDIVariables = wantsDIData;
	if (methodDef->mIsExtern)
		wantsDIVariables = false;

	SizedArray<BfIRMDNode, 8> diParams;
	diParams.push_back(mBfIRBuilder->DbgGetType(methodInstance->mReturnType));

	bool isThisStruct = mCurTypeInstance->IsStruct() && !mCurTypeInstance->IsTypedPrimitive();
	BfType* thisType = NULL;
	if (!methodDef->mIsStatic)
	{
		if ((methodState.mClosureState != NULL) && (methodState.mClosureState->mClosureType != NULL))
			thisType = methodState.mClosureState->mClosureType;
		else
			thisType = mCurTypeInstance;
	}

	PopulateType(methodInstance->mReturnType, BfPopulateType_Data);

	ProcessMethod_SetupParams(methodInstance, thisType, wantsDIData, &diParams);

	//////////////////////////////////////////////////////////////////////////

	//
	{
		if (methodDeclaration != NULL)
			UpdateSrcPos(methodDeclaration, BfSrcPosFlag_NoSetDebugLoc);
		else if (methodDef->mBody != NULL)
			UpdateSrcPos(methodDef->mBody, BfSrcPosFlag_NoSetDebugLoc);
		else if ((methodDef->mDeclaringType != NULL) && (methodDef->mDeclaringType->GetRefNode() != NULL))
			UpdateSrcPos(methodDef->mDeclaringType->GetRefNode(), BfSrcPosFlag_NoSetDebugLoc);
		else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration, BfSrcPosFlag_NoSetDebugLoc);
	}

	BfIRMDNode diFunction;

	if (wantsDIData)
	{
		BP_ZONE("BfModule::DoMethodDeclaration.DISetup");

		BfIRMDNode diFuncType = mBfIRBuilder->DbgCreateSubroutineType(diParams);

		int defLine = mCurFilePosition.mCurLine;

		if (mDICompileUnit)
		{
			int flags = 0;
			BfIRMDNode funcScope = mBfIRBuilder->DbgGetTypeInst(mCurTypeInstance);

			if (methodDef->mProtection == BfProtection_Public)
				flags = llvm::DINode::FlagPublic;
			else if (methodDef->mProtection == BfProtection_Protected)
				flags = llvm::DINode::FlagProtected;
			else
				flags = llvm::DINode::FlagPrivate;
			if (methodDef->mIsOverride)
			{
				//flags |= llvm::DINode::FlagVirtual;
			}
			else if (methodDef->mIsVirtual)
				flags |= llvm::DINode::FlagIntroducedVirtual;
			if ((methodDef->mIsStatic) || (methodDef->mMethodType == BfMethodType_Mixin))
				flags |= llvm::DINode::FlagStaticMember;
			else
			{
				if ((mCurTypeInstance->IsValuelessType()) ||
					((!mIsComptimeModule) && (mCurTypeInstance->IsSplattable())))
					flags |= llvm::DINode::FlagStaticMember;
			}
			flags |= llvm::DINode::FlagPrototyped;
			if (methodDef->mMethodType == BfMethodType_Ctor)
			{
				flags |= llvm::DINode::FlagArtificial;
			}

			auto llvmFunction = methodInstance->mIRFunction;
			SizedArray<BfIRMDNode, 1> genericArgs;
			SizedArray<BfIRValue, 1> genericConstValueArgs;
			String methodName;

			if (methodInstance->GetForeignType() != NULL)
			{
				methodName += TypeToString(methodInstance->GetForeignType());
				methodName += ".";
			}

			if (methodInstance->GetExplicitInterface() != NULL)
			{
				methodName += TypeToString(methodInstance->GetExplicitInterface());
				methodName += ".";
			}

			methodName += methodDef->mName;

			if (methodInstance->GetNumGenericArguments() != 0)
			{
				for (auto genericArg : methodInstance->mMethodInfoEx->mMethodGenericArguments)
				{
					if (genericArg->IsConstExprValue())
					{
						BfConstExprValueType* constExprValueType = (BfConstExprValueType*)genericArg;
						while (genericConstValueArgs.size() < genericArgs.size())
							genericConstValueArgs.push_back(BfIRValue());

						genericConstValueArgs.push_back(mBfIRBuilder->CreateConst(BfTypeCode_UInt64, constExprValueType->mValue.mUInt64));
						genericArgs.push_back(mBfIRBuilder->DbgGetType(GetPrimitiveType(BfTypeCode_Int64)));
					}
					else
						genericArgs.push_back(mBfIRBuilder->DbgGetType(genericArg));
				}

				methodName += "<";
				for (int i = 0; i < (int)methodInstance->mMethodInfoEx->mMethodGenericArguments.size(); i++)
				{
					if (i > 0)
						methodName += ", ";
					BfType* type = methodInstance->mMethodInfoEx->mMethodGenericArguments[i];
					methodName += TypeToString(type);
				}
				methodName += ">";
			}

			if ((methodName.empty()) && (methodDeclaration != NULL))
			{
				if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
				{
					methodName += "operator";
					if (operatorDecl->mIsConvOperator)
					{
						methodName += " ";
						methodName += TypeToString(methodInstance->mReturnType);
					}
					else if (operatorDecl->mOpTypeToken != NULL)
						methodName += operatorDecl->mOpTypeToken->ToString();
				}
			}

			if (methodDef->mCheckedKind == BfCheckedKind_Checked)
				methodName += "$CHK";
			else if (methodDef->mCheckedKind == BfCheckedKind_Unchecked)
				methodName += "$UCHK";

			methodState.mDIFile = mCurFilePosition.mFileInstance->mDIFile;
// 			diFunction = mBfIRBuilder->DbgCreateMethod(funcScope, methodName, mangledName, methodState.mDIFile,
// 				defLine + 1, diFuncType, false, true,
// 				(methodInstance->mVirtualTableIdx != -1) ? llvm::dwarf::DW_VIRTUALITY_virtual : llvm::dwarf::DW_VIRTUALITY_none,
// 				(methodInstance->mVirtualTableIdx != -1) ? methodInstance->DbgGetVirtualMethodNum() : 0,
// 				nullptr, flags, IsOptimized(), llvmFunction, genericArgs, genericConstValueArgs);

			diFunction = mBfIRBuilder->DbgCreateMethod(funcScope, methodName, mangledName, methodState.mDIFile,
 				defLine + 1, diFuncType, false, true,
 				llvm::dwarf::DW_VIRTUALITY_none,
 				0,
				BfIRMDNode(), flags, IsOptimized(), llvmFunction, genericArgs, genericConstValueArgs);
		}
		else
		{
			methodState.mDIFile = mCurFilePosition.mFileInstance->mDIFile;
		}
	}

	//////////////////////////////////////////////////////////////////////////

	// Head and Init get rolled into Entry afterwards.

	methodState.mIRFunction = methodInstance->mIRFunction;
	methodState.mIRHeadBlock = mBfIRBuilder->CreateBlock("head", true);
	methodState.mIRInitBlock = mBfIRBuilder->CreateBlock("init", true);

	methodState.mIREntryBlock = mBfIRBuilder->CreateBlock("entry", true);
	methodState.mCurScope->mDIScope = diFunction;

	auto llvmEntryBlock = methodState.mIREntryBlock;

	mBfIRBuilder->SetInsertPoint(llvmEntryBlock);

	if (methodDef->mName == "__MALFORMED")
	{
		auto newBlock = mBfIRBuilder->CreateBlock("malformed", true);
		mBfIRBuilder->SetInsertPoint(newBlock);
	}

	if (methodDef->mBody != NULL)
	{
		UpdateSrcPos(methodDef->mBody, BfSrcPosFlag_NoSetDebugLoc);
	}
	else if ((mHasFullDebugInfo) && (diFunction) && (typeDef->mTypeDeclaration != NULL))
	{
		// We want to be able to step into delegate invokes -- we actually step over them
		if (methodDef->mName != "Invoke")
		{
			UpdateSrcPos(methodDef->mDeclaringType->GetRefNode());
			mBfIRBuilder->DbgCreateAnnotation(diFunction, "StepOver", GetConstValue32(1));
		}
	}

	// Clear out DebugLoc - to mark the ".addr" code as part of prologue
	mBfIRBuilder->ClearDebugLocation();

	bool isTypedPrimitiveFunc = mCurTypeInstance->IsTypedPrimitive() && (methodDef->mMethodType != BfMethodType_Ctor);
	int irParamCount = methodInstance->GetIRFunctionParamCount(this);

	if (methodInstance->GetImportKind() != BfImportKind_Import_Dynamic)
	{
		int localIdx = 0;
		int argIdx = 0;

		Array<BfIRValue> splatAddrValues;

		for ( ; argIdx < irParamCount; localIdx++)
		{
			bool isThis = ((localIdx == 0) && (!mCurMethodInstance->mMethodDef->mIsStatic));
			if ((isThis) && (thisType->IsValuelessType()))
				isThis = false;

			if ((!mIsComptimeModule) && (methodInstance->GetStructRetIdx() == argIdx))
			{
				argIdx++;
				if (argIdx == irParamCount)
					break;
			}

			BfLocalVariable* paramVar = NULL;
			while (true)
			{
				BF_ASSERT(localIdx < methodState.mLocals.size());
				paramVar = methodState.mLocals[localIdx];
				if ((paramVar->mCompositeCount == -1) &&
					(!paramVar->mIsConst) &&
                    ((!paramVar->mResolvedType->IsValuelessType()) || (paramVar->mResolvedType->IsMethodRef())))
					break;
				localIdx++;
			}

			if ((isThis) && (mCurTypeInstance->IsValueType()) && (methodDef->mMethodType != BfMethodType_Ctor) && (!methodDef->HasNoThisSplat()))
			{
				paramVar->mIsReadOnly = true;
			}

			bool wantsAddr = (wantsDIVariables) || (!paramVar->mIsReadOnly) ||
				((!mIsComptimeModule) && (paramVar->mResolvedType->GetLoweredType(BfTypeUsage_Parameter)));

			if (paramVar->mResolvedType->IsMethodRef())
				wantsAddr = false;

// 			if ((methodDef->mHasAppend) && (argIdx == 1))
// 			{
// 				BF_ASSERT(paramVar->mName == "appendIdx");
// 				wantsAddr = true;
// 			}

			if (paramVar->mIsSplat)
			{
				auto prevInsert = mBfIRBuilder->GetInsertBlock();
				mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
				BfTypeUtils::SplatIterate([&](BfType* checkType)
				{
					BfIRType splatIRType;
					if (checkType->IsComposite())
						splatIRType = mBfIRBuilder->MapType(CreatePointerType(checkType));
					else
						splatIRType = mBfIRBuilder->MapType(checkType);
					auto allocaInst = mBfIRBuilder->CreateAlloca(splatIRType);
					mBfIRBuilder->SetAllocaAlignment(allocaInst, checkType->mAlign);
					if (!paramVar->mAddr)
						paramVar->mAddr = allocaInst;
					if (WantsLifetimes())
						mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
					splatAddrValues.push_back(allocaInst);
				}, paramVar->mResolvedType);
				mBfIRBuilder->SetInsertPoint(prevInsert);
			}
			else if (isThis)
			{
				if (wantsAddr)
				{
					auto prevInsert = mBfIRBuilder->GetInsertBlock();
					mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
					BfIRType thisAddrType = mBfIRBuilder->MapType(thisType);
					if (paramVar->mIsSplat)
					{
						//
					}
					else
					{
 						bool wantPtr = (thisType->IsComposite()) && (!paramVar->mIsLowered);
 						if ((thisType->IsTypedPrimitive()) && (methodDef->HasNoThisSplat()))
 							wantPtr = true;

						if (wantPtr)
						{
							thisAddrType = mBfIRBuilder->MapTypeInstPtr(thisType->ToTypeInstance());
						}

						auto allocaInst = mBfIRBuilder->CreateAlloca(thisAddrType);
						mBfIRBuilder->SetName(allocaInst, paramVar->mName + ".addr");
						mBfIRBuilder->SetAllocaAlignment(allocaInst, mSystem->mPtrSize);
						paramVar->mAddr = allocaInst;
						if (WantsLifetimes())
							mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
					}
					mBfIRBuilder->SetInsertPoint(prevInsert);
				}
			}
			else if ((!isThis) && ((paramVar->mIsStruct) || (paramVar->mResolvedType->IsRef())))
			{
				// Address doesn't change so we don't have to alloca it
				BfIRType allocType;
				int alignSize = mSystem->mPtrSize;
				if (paramVar->mResolvedType->IsRef())
				{
					allocType = mBfIRBuilder->MapType(paramVar->mResolvedType);
				}
				else if ((paramVar->mIsSplat) || (paramVar->mIsLowered))
				{
					allocType = mBfIRBuilder->MapType(paramVar->mResolvedType);
					alignSize = paramVar->mResolvedType->mAlign;
				}
				else
				{
					paramVar->mHasLocalStructBacking = true;
					auto typeInst = paramVar->mResolvedType->ToTypeInstance();
					if (typeInst != NULL)
						allocType = mBfIRBuilder->MapTypeInstPtr(paramVar->mResolvedType->ToTypeInstance());
					else
						allocType = mBfIRBuilder->MapType(CreatePointerType(paramVar->mResolvedType));
				}

				if (wantsAddr)
				{
					auto prevInsert = mBfIRBuilder->GetInsertBlock();
					mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
					mBfIRBuilder->PopulateType(paramVar->mResolvedType);
					auto allocaInst = mBfIRBuilder->CreateAlloca(allocType);
					mBfIRBuilder->SetName(allocaInst, paramVar->mName + ".addr");
					mBfIRBuilder->SetAllocaAlignment(allocaInst, alignSize);
					paramVar->mAddr = allocaInst;
					mBfIRBuilder->SetInsertPoint(prevInsert);
					if (WantsLifetimes())
						mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
				}
			}
			else if (wantsAddr)
			{
				auto allocaInst = CreateAlloca(paramVar->mResolvedType);
				mBfIRBuilder->SetName(allocaInst, paramVar->mName + ".addr");
				paramVar->mAddr = allocaInst;
			}

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
			}
			else if (paramVar->mIsLowered)
			{
				BfTypeCode loweredTypeCode = BfTypeCode_None;
				BfTypeCode loweredTypeCode2 = BfTypeCode_None;
				paramVar->mResolvedType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2);
				argIdx++;
				if (loweredTypeCode2 != BfTypeCode_None)
					argIdx++;
			}
			else
			{
				argIdx++;
			}
		}

		if (methodDef->mBody != NULL)
			UpdateSrcPos(methodDef->mBody);
		else if ((methodDef->mDeclaringType != NULL) && (methodDef->mDeclaringType->GetRefNode() != NULL))
			UpdateSrcPos(methodDef->mDeclaringType->GetRefNode());
		else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration);

		localIdx = 0;
		argIdx = 0;

		int splatAddrIdx = 0;
		while (localIdx < (int)methodState.mLocals.size())
		{
			if ((!mIsComptimeModule) && (argIdx == methodInstance->GetStructRetIdx()))
				argIdx++;

			int curLocalIdx = localIdx++;
			BfLocalVariable* paramVar = methodState.mLocals[curLocalIdx];

			if (paramVar->mIsConst)
				continue;
			if (!paramVar->IsParam())
				continue;
			if (paramVar->mCompositeCount != -1)
				continue;

			bool isThis = ((curLocalIdx == 0) && (!mCurMethodInstance->mMethodDef->mIsStatic));
			if ((isThis) && (thisType->IsValuelessType()))
				isThis = false;

			if (paramVar->mValue.IsArg())
				BF_ASSERT(paramVar->mValue.mId == argIdx);

			BfIRMDNode diVariable;
			if (wantsDIData)
			{
				String paramName = paramVar->mName;

				BfIRMDNode diType = mBfIRBuilder->DbgGetType(paramVar->mResolvedType);
				if (isThis)
				{
					if ((mCurMethodState->mClosureState != NULL) && (mCurMethodState->mClosureState->mClosureType != NULL))
						paramName = "__closure";

					if ((paramVar->mResolvedType->IsValueType()) && (!paramVar->mIsSplat) && (!paramVar->mIsLowered))
					{
						diType = mBfIRBuilder->DbgGetType(paramVar->mResolvedType);
						bool wantRef = paramVar->mResolvedType->IsComposite();
						if ((paramVar->mResolvedType->IsTypedPrimitive()) && (methodDef->HasNoThisSplat()))
							wantRef = true;

						if (wantRef)
							diType = mBfIRBuilder->DbgCreateReferenceType(diType);
					}
					else if (!paramVar->mResolvedType->IsValueType())
					{
						diType = mBfIRBuilder->DbgCreateArtificialType(diType);
					}
				}
				else
				{
					if ((paramVar->mResolvedType->IsComposite()) && (!paramVar->mIsSplat) && (!paramVar->mIsLowered))
					{
						diType = mBfIRBuilder->DbgGetType(paramVar->mResolvedType);
						diType = mBfIRBuilder->DbgCreateReferenceType(diType);
						diType = mBfIRBuilder->DbgCreateConstType(diType);
					}
				}

				if (!paramVar->mIsSplat)
				{
					if ((paramVar->mParamIdx >= 0) && (paramVar->mParamIdx < methodInstance->mDefaultValues.mSize))
					{
						auto defaultValue = methodInstance->mDefaultValues[paramVar->mParamIdx];
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(defaultValue.mValue);
						if ((constant != NULL) &&
							((BfIRConstHolder::IsIntable(constant->mTypeCode)) || (BfIRConstHolder::IsFloat(constant->mTypeCode))))
						{
							int64 writeVal = constant->mInt64;
							if (constant->mTypeCode == BfTypeCode_Float)
							{
								// We need to do this because Singles are stored in mDouble, so we need to reduce here
								float floatVal = (float)constant->mDouble;
								writeVal = *(uint32*)&floatVal;
							}
							if (writeVal < 0)
								paramName += StrFormat("$_%llu", -writeVal);
							else
								paramName += StrFormat("$%llu", writeVal);
						}
					}

					if (paramVar->mResolvedType->IsValuelessType())
					{
						diVariable = mBfIRBuilder->DbgCreateAutoVariable(diFunction,
							paramName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType);
					}
					else if (paramVar->mIsImplicitParam)
					{
						// Annotate this specially?
						diVariable = mBfIRBuilder->DbgCreateParameterVariable(diFunction,
							paramName, argIdx + 1, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType, true, 0);
					}
					else
					{
						diVariable = mBfIRBuilder->DbgCreateParameterVariable(diFunction,
							paramName, argIdx + 1, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType, true, 0);
					}
					paramVar->mDbgVarInst = diVariable;
				}
				else if ((paramVar->mIsSplat) && (paramVar->mResolvedType->GetSplatCount() == 0))
				{
// 					if ((mBfIRBuilder->HasDebugLocation()) && (wantsDIVariables))
// 					{
// 						// Only add this placeholder if we don't have any values
// 						auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
// 							paramName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType);
// 						mBfIRBuilder->DbgInsertValueIntrinsic(GetConstValue(0), diVariable);
// 					}
				}
			}

			bool isTypedPrimCtor = mCurTypeInstance->IsTypedPrimitive() && (methodDef->mMethodType == BfMethodType_Ctor);

			if ((!paramVar->mParamFailed) && (paramVar->mAddr))
			{
				// Write our argument value into the .addr
				mBfIRBuilder->ClearDebugLocation();
				if (paramVar->mIsSplat)
				{
					mBfIRBuilder->PopulateType(paramVar->mResolvedType);
					//
				}
				else
				{
					bool handled = false;
					if (paramVar->mIsLowered)
					{
						BfTypeCode loweredTypeCode = BfTypeCode_None;
						BfTypeCode loweredTypeCode2 = BfTypeCode_None;
						if (paramVar->mResolvedType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2))
						{
							BfIRValue targetAddr = paramVar->mAddr;
							bool isTempTarget = false;

							int loweredSize = mBfIRBuilder->GetSize(loweredTypeCode) + mBfIRBuilder->GetSize(loweredTypeCode2);
							if (paramVar->mResolvedType->mSize < loweredSize)
							{
								isTempTarget = true;
								targetAddr = CreateAlloca(GetPrimitiveType(BfTypeCode_Int8), true, NULL, GetConstValue(loweredSize));
								mBfIRBuilder->SetAllocaAlignment(targetAddr,
									BF_MAX(paramVar->mResolvedType->mAlign,
										BF_MAX(mBfIRBuilder->GetSize(loweredTypeCode), mBfIRBuilder->GetSize(loweredTypeCode2))));
							}

							// We have a lowered type coming in, so we have to cast the .addr before storing
							auto primType = mBfIRBuilder->GetPrimitiveType(loweredTypeCode);
							auto primPtrType = mBfIRBuilder->GetPointerTo(primType);
							auto primPtrVal = mBfIRBuilder->CreateBitCast(targetAddr, primPtrType);
							mBfIRBuilder->CreateAlignedStore(paramVar->mValue, primPtrVal, mCurTypeInstance->mAlign);

							if (loweredTypeCode2 != BfTypeCode_None)
							{
								auto primType2 = mBfIRBuilder->GetPrimitiveType(loweredTypeCode2);
								auto primPtrType2 = mBfIRBuilder->GetPointerTo(primType2);
								BfIRValue primPtrVal2;
								if (mBfIRBuilder->GetSize(loweredTypeCode) < mBfIRBuilder->GetSize(loweredTypeCode2))
									primPtrVal2 = mBfIRBuilder->CreateInBoundsGEP(mBfIRBuilder->CreateBitCast(primPtrVal, primPtrType2), 1);
								else
									primPtrVal2 = mBfIRBuilder->CreateBitCast(mBfIRBuilder->CreateInBoundsGEP(primPtrVal, 1), primPtrType2);
								mBfIRBuilder->CreateStore(mBfIRBuilder->GetArgument(argIdx + 1), primPtrVal2);
							}

							if (isTempTarget)
							{
								auto castedTempPtr = mBfIRBuilder->CreateBitCast(targetAddr, mBfIRBuilder->MapType(CreatePointerType(paramVar->mResolvedType)));
								auto tempVal = mBfIRBuilder->CreateAlignedLoad(castedTempPtr, paramVar->mResolvedType->mAlign);
								mBfIRBuilder->CreateAlignedStore(tempVal, paramVar->mAddr, paramVar->mResolvedType->mAlign);
							}

							// We don't want to allow directly using value
							paramVar->mValue = BfIRValue();
							handled = true;
						}
						else
						{
							BF_ASSERT("Expected lowered");
						}
					}

					if (!handled)
						mBfIRBuilder->CreateAlignedStore(paramVar->mValue, paramVar->mAddr, paramVar->mResolvedType->mAlign);
				}
			}

			if (methodDef->mBody != NULL)
				UpdateSrcPos(methodDef->mBody);
			else if (methodDef->mDeclaringType->mTypeDeclaration != NULL)
				UpdateSrcPos(methodDef->mDeclaringType->mTypeDeclaration);
			else if (methodDeclaration == NULL)
				UseDefaultSrcPos();

			// Write our argument value into the .addr
			if (paramVar->mIsSplat)
			{
				int splatComponentIdx = 0;

				auto curArgIdx = argIdx;
				auto _FinishSplats = [&] (BfType* checkType, const StringImpl& name)
				{
					BfIRValue splatAddrValue = splatAddrValues[splatAddrIdx++];
					methodState.mSplatDecompAddrs.push_back(splatAddrValue);
					auto storeInst = mBfIRBuilder->CreateAlignedStore(mBfIRBuilder->GetArgument(curArgIdx), splatAddrValue, checkType->mAlign);
					mBfIRBuilder->ClearDebugLocation(storeInst);

					if (wantsDIData)
					{
						BfIRMDNode diType;
						if (checkType->IsComposite())
							diType = mBfIRBuilder->DbgGetType(CreateRefType(checkType));
						else
							diType = mBfIRBuilder->DbgGetType(checkType);
						auto diVariable = mBfIRBuilder->DbgCreateParameterVariable(diFunction,
							name, curArgIdx + 1, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType);
						if (wantsDIVariables)
						{
							mBfIRBuilder->DbgInsertDeclare(splatAddrValue, diVariable);
						}
					}

					curArgIdx++;
					splatComponentIdx++;
				};

				std::function<void(BfType*, const StringImpl&)> _FinishSplatsIterate = [&](BfType* checkType, const StringImpl& name)
				{
					if (checkType->IsStruct())
					{
						auto checkTypeInstance = checkType->ToTypeInstance();
						if (checkTypeInstance->mBaseType != NULL)
						{
							_FinishSplatsIterate(checkTypeInstance->mBaseType, name + "$b");
						}

						if (checkTypeInstance->mIsUnion)
						{
							auto unionInnerType = checkTypeInstance->GetUnionInnerType();
							if (!unionInnerType->IsValuelessType())
							{
								_FinishSplatsIterate(unionInnerType, name + "$u");
							}

							if (checkTypeInstance->IsEnum())
							{
								auto dscrType = checkTypeInstance->GetDiscriminatorType();
								_FinishSplatsIterate(dscrType, name + "$d");
							}
						}
						else
						{
							for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
							{
								auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
								auto fieldDef = fieldInstance->GetFieldDef();
								if (fieldInstance->mDataIdx >= 0)
								{
									_FinishSplatsIterate(fieldInstance->GetResolvedType(), name + "$m$" + fieldDef->mName);
								}
							}
						}
					}
					else if (checkType->IsMethodRef())
					{
						auto methodRefType = (BfMethodRefType*)checkType;
						BfMethodInstance* methodInstance = methodRefType->mMethodRef;
//						int implicitParamCount = methodInstance->GetImplicitParamCount();
// 						for (int implicitParamIdx = methodInstance->HasThis() ? -1 : 0; implicitParamIdx < implicitParamCount; implicitParamIdx++)
// 						{
// 							auto paramType = methodInstance->GetParamType(implicitParamIdx);
// 							if (!paramType->IsValuelessType())
// 								_FinishSplats(paramType, name + "$m$" + methodInstance->GetParamName(implicitParamIdx));
// 						}
						for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
						{
							int paramIdx = methodRefType->GetParamIdxFromDataIdx(dataIdx);
							String paramName = methodInstance->GetParamName(paramIdx);
							if (paramName == "this")
								paramName = "__this";
							if (methodRefType->WantsDataPassedAsSplat(dataIdx))
							{
								_FinishSplatsIterate(methodRefType->GetCaptureType(dataIdx), name + "$m$" + paramName);
							}
							else
							{
								_FinishSplats(methodRefType->GetCaptureType(dataIdx), name + "$m$" + paramName);
							}
						}
					}
					else if (!checkType->IsValuelessType())
					{
						_FinishSplats(checkType, name);
					}
				};

				mBfIRBuilder->PopulateType(paramVar->mResolvedType);
				if (!paramVar->mConstValue)
					_FinishSplatsIterate(paramVar->mResolvedType, "$" + paramVar->mName);
			}

			BfIRValue declareCall;
			if (diVariable)
			{
				if ((mBfIRBuilder->HasDebugLocation()) && (wantsDIVariables))
				{
					if (!paramVar->mAddr)
					{
						if ((!paramVar->mValue) || (paramVar->mValue.IsFake()))
						{
							if ((!paramVar->mIsThis) && (mCompiler->mOptions.mToolsetType != BfToolsetType_GNU)) // DWARF chokes on this:
							{
								// We don't need to set the location for this
								mBfIRBuilder->DbgInsertValueIntrinsic(BfIRValue(), diVariable);
							}
						}
						else
							declareCall = mBfIRBuilder->DbgInsertDeclare(paramVar->mValue, diVariable);
					}
					else
					{
						declareCall = mBfIRBuilder->DbgInsertDeclare(paramVar->mAddr, diVariable);
					}
				}
			}

			if ((isThis) && (!paramVar->mIsSplat) && (paramVar->mAddr))
			{
				// We don't allow actually assignment to "this", so we just do a single load
				//  Keep in mind we don't use the ACTUAL mValue value because that's a register, but
				//  we need to store it in the stack frame for debugging purposes
				auto loadedThis = mBfIRBuilder->CreateAlignedLoad(paramVar->mAddr/*, "this"*/, paramVar->mResolvedType->mAlign);
				mBfIRBuilder->ClearDebugLocation(loadedThis);
				paramVar->mValue = loadedThis;
			}

			if ((wantsDIData) && (declareCall))
				mBfIRBuilder->UpdateDebugLocation(declareCall);

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
			}
			else if (paramVar->mIsLowered)
			{
				argIdx++;
				BfTypeCode loweredTypeCode = BfTypeCode_None;
				BfTypeCode loweredTypeCode2 = BfTypeCode_None;
				paramVar->mResolvedType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2);
				if (loweredTypeCode2 != BfTypeCode_None)
					argIdx++;
			}
			else if (!paramVar->mResolvedType->IsValuelessType())
			{
				argIdx++;
			}
		}
		if (wantsDIData)
		{
            BF_ASSERT(splatAddrIdx == (int)splatAddrValues.size());
		}
	}

 	for (int varIdx = 0; varIdx < (int)mCurMethodState->mLocals.size(); varIdx++)
 	{
 		auto paramVar = mCurMethodState->mLocals[varIdx];

		// We don't need this because the debug func type records the type of the splat param, and then the same can be inferred from the first
		//  splat member.  If the "splat" is valueless then we do create a placeholder, but further up in this method
//  		if (paramVar->mIsSplat)
//  		{
//  			if (wantsDIVariables)
//  			{
//  				auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(diFunction, paramVar->mName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(paramVar->mResolvedType));
//  				mBfIRBuilder->DbgInsertValueIntrinsic(mBfIRBuilder->CreateConst(BfTypeCode_Int64, 0), diVariable);
//  			}
//  		}

		if (paramVar->mResolvedType->IsValuelessType())
		{
			if ((mBfIRBuilder->HasDebugLocation()) && (wantsDIVariables) && (mCompiler->mOptions.mToolsetType != BfToolsetType_GNU)) // DWARF chokes on this:
			{
				// Only add this placeholder if we don't have any values
				auto diType = mBfIRBuilder->DbgGetType(paramVar->mResolvedType);
				auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
					paramVar->mName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType);
				mBfIRBuilder->DbgInsertValueIntrinsic(BfIRValue(), diVariable);
			}
		}
 	}

	if (IsTargetingBeefBackend())
		mCurMethodState->mCurScope->mValueScopeStart = mBfIRBuilder->CreateValueScopeStart();

	if (methodState.mClosureState != NULL)
	{
		for (auto& constLocal : methodState.mClosureState->mConstLocals)
		{
			BfLocalVariable* localVar = new BfLocalVariable();
			*localVar = constLocal;
			localVar->mIsBumpAlloc = false;
			AddLocalVariableDef(localVar, true);
		}
	}

	bool wantsRemoveBody = false;
	bool skipBody = false;
	bool skipUpdateSrcPos = false;
	bool skipEndChecks = false;
	bool hasExternSpecifier = (methodDeclaration != NULL) && (methodDeclaration->mExternSpecifier != NULL) && (methodInstance->GetImportKind() != BfImportKind_Import_Dynamic);
	auto propertyDeclaration = methodDef->GetPropertyDeclaration();

	if ((methodDef != NULL) && (propertyDeclaration != NULL) && (propertyDeclaration->mExternSpecifier != NULL))
		hasExternSpecifier = true;

	// Allocate, clear, set classVData

	if ((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mIsStatic))
	{
		CreateStaticCtor();
	}
	else if ((mCurTypeInstance->IsBoxed()) && (methodDef->mMethodType != BfMethodType_Ctor) && (methodDef->mName != BF_METHODNAME_DYNAMICCAST) && (methodDef->mName != BF_METHODNAME_DYNAMICCAST_INTERFACE))
	{
		skipBody = true;
		skipEndChecks = true;

		if (HasExecutedOutput())
		{
			// Clear out DebugLoc - to mark the ".addr" code as part of prologue
			mBfIRBuilder->ClearDebugLocation();

			BfBoxedType* boxedType = (BfBoxedType*) mCurTypeInstance;
			BfTypeInstance* innerType = boxedType->mElementType->ToTypeInstance();
			PopulateType(innerType, BfPopulateType_DataAndMethods);
			mBfIRBuilder->PopulateType(mCurTypeInstance);
			BfGetMethodInstanceFlags flags = BfGetMethodInstanceFlag_None;
			if (methodInstance->mIsForeignMethodDef)
			{
				flags = BfGetMethodInstanceFlag_ForeignMethodDef;
			}
			BfTypeVector methodGenericArguments;
			if (methodInstance->mMethodInfoEx != NULL)
				methodGenericArguments = methodInstance->mMethodInfoEx->mMethodGenericArguments;
			auto innerMethodInstance = GetMethodInstance(innerType, methodDef, methodGenericArguments, flags, methodInstance->GetForeignType());

			if (innerMethodInstance.mMethodInstance->IsSkipCall())
			{
				if (!methodInstance->mReturnType->IsValuelessType())
				{
					auto retVal = GetDefaultValue(methodInstance->mReturnType);
					CreateReturn(retVal);
				}
				else
					mBfIRBuilder->CreateRetVoid();
			}
			else
			{
				auto innerMethodDef = innerMethodInstance.mMethodInstance->mMethodDef;
				BF_ASSERT(innerMethodDef == methodDef);

				SizedArray<BfIRValue, 8> innerParams;
				BfExprEvaluator exprEvaluator(this);

				if (!innerType->IsValuelessType())
				{
					BfIRValue thisValue = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, 1);
					BfTypedValue innerVal(thisValue, innerType, true);
					if (boxedType->IsBoxedStructPtr())
					{
						innerVal = LoadValue(innerVal);
						innerVal = BfTypedValue(innerVal.mValue, innerType, true);
					}

					exprEvaluator.PushThis(NULL, innerVal, innerMethodInstance.mMethodInstance, innerParams);
				}

				for (int i = 1; i < (int)mCurMethodState->mLocals.size(); i++)
				{
					BfLocalVariable* localVar = mCurMethodState->mLocals[i];
					BfTypedValue localVal = exprEvaluator.LoadLocal(localVar, true);
					exprEvaluator.PushArg(localVal, innerParams);
				}
				if (!innerMethodInstance.mFunc)
				{
					BF_ASSERT(innerType->IsUnspecializedType());
				}
				else if ((!mIsComptimeModule) && (methodInstance->GetStructRetIdx() != -1))
				{
					mBfIRBuilder->PopulateType(methodInstance->mReturnType);
					auto returnType = BfTypedValue(mBfIRBuilder->GetArgument(methodInstance->GetStructRetIdx()), methodInstance->mReturnType, true);
					exprEvaluator.mReceivingValue = &returnType;
					auto retVal = exprEvaluator.CreateCall(NULL, innerMethodInstance.mMethodInstance, innerMethodInstance.mFunc, true, innerParams, NULL, BfCreateCallFlags_TailCall);
					BF_ASSERT(exprEvaluator.mReceivingValue == NULL); // Ensure it was actually used
					mBfIRBuilder->CreateRetVoid();
				}
				else
				{
					mBfIRBuilder->PopulateType(methodInstance->mReturnType);
					auto retVal = exprEvaluator.CreateCall(NULL, innerMethodInstance.mMethodInstance, innerMethodInstance.mFunc, true, innerParams, NULL, BfCreateCallFlags_TailCall);
					if (mCurMethodInstance->mReturnType->IsValueType())
						retVal = LoadValue(retVal);
					CreateReturn(retVal.mValue);
				}
			}
		}

		mCurMethodState->SetHadReturn(true);
		mCurMethodState->mLeftBlockUncond = true;
	}
	else if (methodDef->mMethodType == BfMethodType_CtorClear)
	{
		SetIllegalSrcPos();
		mBfIRBuilder->ClearDebugLocation();
		PopulateType(mCurTypeInstance, BfPopulateType_Data);
		auto thisVal = GetThis();
		int prevSize = 0;
		if (mContext->mBfObjectType != NULL)
		{
			prevSize = mContext->mBfObjectType->mInstSize;
			PopulateType(mContext->mBfObjectType);

			// If we have object extensions, clear out starting at the extension
			for (auto& fieldInstance : mContext->mBfObjectType->mFieldInstances)
			{
				if (fieldInstance.GetFieldDef()->mDeclaringType->IsExtension())
				{
					prevSize = fieldInstance.mDataOffset;
					break;
				}
			}
		}
		auto int8PtrType = CreatePointerType(GetPrimitiveType(BfTypeCode_Int8));

		int curSize = mCurTypeInstance->mInstSize;
		if (curSize > prevSize)
		{
			auto int8PtrVal = mBfIRBuilder->CreateBitCast(thisVal.mValue, mBfIRBuilder->MapType(int8PtrType));
			int8PtrVal = mBfIRBuilder->CreateInBoundsGEP(int8PtrVal, GetConstValue(prevSize));
			mBfIRBuilder->CreateMemSet(int8PtrVal, GetConstValue8(0), GetConstValue(curSize - prevSize), GetConstValue(mCurTypeInstance->mInstAlign));
		}

		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
		{
			auto useThis = mCurMethodState->mLocals[0]->mValue;
			auto useThisType = mCurTypeInstance;

			auto checkTypeInst = mCurTypeInstance;
			while (checkTypeInst != NULL)
			{
				for (auto& fieldInstance : checkTypeInst->mFieldInstances)
				{
					auto fieldDef = fieldInstance.GetFieldDef();
					if ((fieldDef == NULL) || (fieldDef->mIsStatic))
						continue;
					if (fieldInstance.IsAppendedObject())
					{
						if (checkTypeInst != useThisType)
						{
							useThis = mBfIRBuilder->CreateBitCast(useThis, mBfIRBuilder->MapType(checkTypeInst));
							useThisType = checkTypeInst;
						}

						BfIRValue fieldAddr = mBfIRBuilder->CreateInBoundsGEP(useThis, 0, fieldInstance.mDataIdx);
						auto int8PtrVal = mBfIRBuilder->CreateBitCast(fieldAddr, mBfIRBuilder->MapType(int8PtrType));
						mBfIRBuilder->CreateStore(GetConstValue8(BfObjectFlag_Deleted), int8PtrVal);
					}
				}

				checkTypeInst = checkTypeInst->mBaseType;
			}
		}

		skipUpdateSrcPos = true;
	}
	else if (((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorNoBody)) && (!hasExternSpecifier))
	{
		EmitCtorBody(skipBody);
	}
	else if ((methodDef->mMethodType == BfMethodType_Dtor) && (!hasExternSpecifier))
	{
		EmitDtorBody();
		skipBody = true;
	}

	if ((!mCurTypeInstance->IsBoxed()) && (methodDeclaration != NULL) && (methodDeclaration->mHadYield) && (methodDef->mBody != NULL))
	{
		EmitIteratorBlock(skipBody);
	}

	if (methodDef->mName == BF_METHODNAME_MARKMEMBERS)
	{
		// We need to be able to mark deleted objects
		mCurMethodState->mIgnoreObjectAccessCheck = true;
	}
	auto customAttributes = methodInstance->GetCustomAttributes();
	if (customAttributes != NULL)
	{
		if (customAttributes->Contains(mCompiler->mDisableObjectAccessChecksAttributeTypeDef))
			mCurMethodState->mIgnoreObjectAccessCheck = true;
		if (customAttributes->Contains(mCompiler->mDisableChecksAttributeTypeDef))
			mCurMethodState->mDisableChecks = true;
	}

	if ((methodDef->mMethodType == BfMethodType_CtorNoBody) && (!methodDef->mIsStatic) &&
		((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
	{
		// We chain even non-default ctors to the default ctors
		auto defaultCtor = methodInstance;
		if (defaultCtor->mChainType == BfMethodChainType_ChainHead)
			CallChainedMethods(defaultCtor, false);
	}

	auto bodyBlock = BfNodeDynCast<BfBlock>(methodDef->mBody);
	if (!mCompiler->mHasRequiredTypes)
	{
		// Skip processing to avoid errors
	}
	else if (methodDef->mBody == NULL)
	{
		if (methodDeclaration != NULL)
		{
			if (auto operatorDeclaration = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
			{
				if (operatorDeclaration->mIsConvOperator)
					wantsRemoveBody = true;
			}
		}

		bool isDllImport = false;
		if ((importKind == BfImportKind_Import_Static) || (importKind == BfImportKind_Import_Dynamic))
		{
			if (importStrNum != -1)
			{
				if (importKind == BfImportKind_Import_Static)
				{
					if (!mImportFileNames.Contains(importStrNum))
					{
						mImportFileNames.Add(importStrNum);
					}
				}
			}
		}
		else if (methodInstance->GetImportKind() == BfImportKind_Import_Dynamic)
		{
			CreateDllImportMethod();
		}
		else if (((mCurTypeInstance->mTypeDef->mIsDelegate) || (mCurTypeInstance->mTypeDef->mIsFunction)) &&
			(methodDef->mName == "Invoke") && (methodDef->mMethodType == BfMethodType_Normal))
		{
			if (mCurTypeInstance->mTypeDef->mIsFunction)
			{
				// Emit nothing
			}
			else
			{
				CreateDelegateInvokeMethod();
				mCurMethodState->SetHadReturn(true);
				mCurMethodState->mLeftBlockUncond = true;
			}
		}
		else if ((methodDef->mName == BF_METHODNAME_DYNAMICCAST) || (methodDef->mName == BF_METHODNAME_DYNAMICCAST_INTERFACE))
		{
			if (mCurTypeInstance->IsObject())
			{
				CreateDynamicCastMethod();
			}
			else
			{
				mBfIRBuilder->CreateRet(GetDefaultValue(methodInstance->mReturnType));
				mCurMethodState->mHadReturn = true;
			}
		}
		else if ((methodDef->mName == BF_METHODNAME_ENUM_HASFLAG) && (mCurTypeInstance->IsEnum()) && (!mCurTypeInstance->IsBoxed()))
		{
			BfIRValue ret;
			if (mCurMethodState->mLocals[1]->mResolvedType != mCurTypeInstance)
			{
				// This can happen if we provide an invalid name
				AssertErrorState();
				ret = mBfIRBuilder->CreateRet(GetDefaultValue(GetPrimitiveType(BfTypeCode_Boolean)));
			}
			else
			{
				// Unfortunate DebugLoc shenanigans-
				//  Our params get removed if we don't have any DebugLocs, but we don't want to actually be able to step into this method,
				//  so we only set the loc on the CreateRet which gets inlined out
				mBfIRBuilder->SaveDebugLocation();
				mBfIRBuilder->ClearDebugLocation();
				BfIRValue fromBool;
				if (!mCurTypeInstance->IsTypedPrimitive())
				{
					fromBool = GetDefaultValue(methodInstance->mReturnType);
				}
				else
				{
					auto andResult = mBfIRBuilder->CreateAnd(mCurMethodState->mLocals[0]->mValue, mCurMethodState->mLocals[1]->mValue);
					auto toBool = mBfIRBuilder->CreateCmpNE(andResult, GetDefaultValue(mCurMethodState->mLocals[0]->mResolvedType));
					fromBool = mBfIRBuilder->CreateNumericCast(toBool, false, BfTypeCode_Boolean);
				}
				mBfIRBuilder->RestoreDebugLocation();

				ret = mBfIRBuilder->CreateRet(fromBool);
				//ExtendLocalLifetimes(0);
				EmitLifetimeEnds(&mCurMethodState->mHeadScope);
			}

			mCurMethodState->SetHadReturn(true);
			mCurMethodState->mLeftBlockUncond = true;
		}
		else if (((methodDef->mName == BF_METHODNAME_ENUM_GETUNDERLYINGREF) || (methodDef->mName == BF_METHODNAME_ENUM_GETUNDERLYING)) &&
			(mCurTypeInstance->IsEnum()) && (!mCurTypeInstance->IsBoxed()))
		{
			BfIRValue ret;
			// Unfortunate DebugLoc shenanigans-
			//  Our params get removed if we don't have any DebugLocs, but we don't want to actually be able to step into this method,
			//  so we only set the loc on the CreateRet which gets inlined out
			mBfIRBuilder->SaveDebugLocation();
			mBfIRBuilder->ClearDebugLocation();
			BfIRValue fromBool;
			mBfIRBuilder->RestoreDebugLocation();
			ret = mBfIRBuilder->CreateRet(GetThis().mValue);
			//ExtendLocalLifetimes(0);
			EmitLifetimeEnds(&mCurMethodState->mHeadScope);

			mCurMethodState->SetHadReturn(true);
			mCurMethodState->mLeftBlockUncond = true;
		}
		else if ((mCurTypeInstance->IsEnum()) && (!mCurTypeInstance->IsBoxed()) && (methodDef->mName == BF_METHODNAME_TO_STRING))
		{
			auto enumType = ResolveTypeDef(mCompiler->mEnumTypeDef);
			if (HasExecutedOutput())
			{
				EmitEnumToStringBody();
			}

			mBfIRBuilder->CreateRetVoid();
			mCurMethodState->SetHadReturn(true);
			mCurMethodState->mLeftBlockUncond = true;
			EmitLifetimeEnds(&mCurMethodState->mHeadScope);
		}
		else if ((mCurTypeInstance->IsTuple()) && (!mCurTypeInstance->IsBoxed()) && (methodDef->mName == BF_METHODNAME_TO_STRING))
		{
			auto enumType = ResolveTypeDef(mCompiler->mEnumTypeDef);
			if (HasExecutedOutput())
			{
				EmitTupleToStringBody();
			}

			mBfIRBuilder->CreateRetVoid();
			mCurMethodState->SetHadReturn(true);
			mCurMethodState->mLeftBlockUncond = true;
			EmitLifetimeEnds(&mCurMethodState->mHeadScope);
		}
		else if (methodDef->mName == BF_METHODNAME_DEFAULT_EQUALS)
		{
			CreateValueTypeEqualsMethod(false);
			skipBody = true;
			skipEndChecks = true;
		}
		else if (methodDef->mName == BF_METHODNAME_DEFAULT_STRICT_EQUALS)
		{
			CreateValueTypeEqualsMethod(true);
			skipBody = true;
			skipEndChecks = true;
		}
		else if ((methodDef->mName == BF_METHODNAME_EQUALS) && (typeDef->GetDefinition() == mCompiler->mValueTypeTypeDef))
		{
			CreateValueTypeEqualsMethod(false);
			skipBody = true;
			skipEndChecks = true;
		}
		else if ((methodDef->mName == BF_METHODNAME_EQUALS) && (mCurTypeInstance->IsDelegate()))
		{
			CreateDelegateEqualsMethod();
			skipBody = true;
			skipEndChecks = true;
		}
		else
		{
			auto propertyDeclaration = methodDef->GetPropertyDeclaration();
			if ((propertyDeclaration != NULL) && (!typeDef->HasAutoProperty(propertyDeclaration)))
			{
				if ((!mCurTypeInstance->IsInterface()) && (!hasExternSpecifier))
					Fail("Body expected", methodDef->mBody);
			}
			else if (methodDef->mMethodType == BfMethodType_PropertyGetter)
			{
				if ((methodInstance->mReturnType->IsRef()) && (!methodDef->mIsMutating) && (mCurTypeInstance->IsValueType()))
					Fail("Auto-implemented ref property getters must declare 'mut'", methodInstance->mMethodDef->GetRefNode());

				if (methodInstance->mReturnType->IsValuelessType())
				{
					mBfIRBuilder->CreateRetVoid();
				}
				else if (HasExecutedOutput())
				{
					String autoPropName = typeDef->GetAutoPropertyName(propertyDeclaration);
					BfFieldInstance* fieldInstance = GetFieldByName(mCurTypeInstance, autoPropName);
					BfFieldDef* fieldDef = NULL;
					if (fieldInstance != NULL)
						fieldDef = fieldInstance->GetFieldDef();

					BfType* retType = methodInstance->mReturnType;
					if (retType->IsRef())
						retType = retType->GetUnderlyingType();

					if ((fieldInstance != NULL) && (fieldInstance->GetResolvedType() == retType) &&
						((fieldInstance->mDataIdx >= 0) || (fieldDef->IsNonConstStatic())))
					{
						BfTypedValue lookupValue;
						if (fieldDef->IsNonConstStatic())
							lookupValue = ReferenceStaticField(fieldInstance);
						else if (mCurTypeInstance->IsObject())
							lookupValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(GetThis().mValue, 0, fieldInstance->mDataIdx), fieldInstance->mResolvedType, true);
						else
							lookupValue = ExtractValue(GetThis(), fieldInstance, fieldInstance->mDataIdx);
						if (methodInstance->mReturnType->IsRef())
						{
							if ((!lookupValue.IsAddr()) && (!lookupValue.mType->IsValuelessType()))
								lookupValue = MakeAddressable(lookupValue);
						}
						else
							lookupValue = LoadOrAggregateValue(lookupValue);

						CreateReturn(lookupValue.mValue);
						EmitLifetimeEnds(&mCurMethodState->mHeadScope);
					}
					else
					{
						// This can happen if we have two properties with the same name but different types
						CreateReturn(GetDefaultValue(mCurMethodInstance->mReturnType));
						EmitDefaultReturn();
					}
				}
				else
				{
					CreateReturn(GetDefaultValue(mCurMethodInstance->mReturnType));
					EmitDefaultReturn();
				}
				mCurMethodState->SetHadReturn(true);
			}
			else if (methodDef->mMethodType == BfMethodType_PropertySetter)
			{
				if ((!methodDef->mIsMutating) && (!methodDef->mIsStatic) && (mCurTypeInstance->IsValueType()))
				{
					Fail("Auto-setter must be marked as 'mut'", methodDef->GetRefNode(), true);
				}
				else if (!mCompiler->IsAutocomplete())
				{
					String autoPropName = typeDef->GetAutoPropertyName(propertyDeclaration);
					BfFieldInstance* fieldInstance = GetFieldByName(mCurTypeInstance, autoPropName);
					auto fieldDef = fieldInstance->GetFieldDef();
					auto& lastParam = mCurMethodState->mLocals.back();
					if ((fieldInstance != NULL) && (fieldInstance->GetResolvedType() == lastParam->mResolvedType) &&
						((fieldInstance->mDataIdx >= 0) || (fieldDef->IsNonConstStatic())))
					{
						BfIRValue lookupAddr;
						if (fieldDef->IsNonConstStatic())
							lookupAddr = ReferenceStaticField(fieldInstance).mValue;
						else
						{
							auto thisValue = GetThis();
							if (thisValue.IsSplat())
							{
								BFMODULE_FATAL(this, "Should not happen");
							}
							if (mCurTypeInstance->IsObject())
								thisValue = LoadValue(thisValue);
							lookupAddr = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInstance->mDataIdx);
						}

						BfExprEvaluator exprEvaluator(this);
						auto localVal = exprEvaluator.LoadLocal(lastParam);
						localVal = LoadOrAggregateValue(localVal);
						if (!localVal.mType->IsValuelessType())
							mBfIRBuilder->CreateAlignedStore(localVal.mValue, lookupAddr, localVal.mType->mAlign);
					}
					else if (!fieldInstance->mResolvedType->IsValuelessType())
					{
						// This can happen if we have two properties with the same name but different types
						AssertErrorState();
					}
				}
			}
			else if ((methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))
			{
				if (mCompiler->mOptions.mEnableRealtimeLeakCheck)
				{
					if (HasCompiledOutput())
						EmitGCMarkMembers();
				}
				else if (!mCurTypeInstance->IsObject())
				{
				}
			}
			else if (methodDef->mName == BF_METHODNAME_FIND_TLS_MEMBERS)
			{
				if (mCompiler->mOptions.mEnableRealtimeLeakCheck)
				{
					if (HasCompiledOutput())
						EmitGCFindTLSMembers();
				}
			}
		}
	}
	else if (!skipBody)
	{
		bool isEmptyBodied = BfNodeDynCast<BfTokenNode>(methodDef->mBody) != NULL;

		bool wantsRetVal = true;
		if ((mIsComptimeModule) && (methodDef->mMethodType != BfMethodType_CtorCalcAppend))
			wantsRetVal = false;
		else if (mCurMethodInstance->mReturnType->IsVar())
			wantsRetVal = false;

		if ((!mCurMethodInstance->mReturnType->IsValuelessType()) && (!isEmptyBodied))
		{
			mBfIRBuilder->PopulateType(mCurMethodInstance->mReturnType);

			if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
			{
				auto ptrType = CreatePointerType(mCurMethodInstance->mReturnType);
				auto allocaInst = AllocLocalVariable(ptrType, "__return.addr", false);
				auto storeInst = mBfIRBuilder->CreateAlignedStore(mBfIRBuilder->GetArgument(mCurMethodInstance->GetStructRetIdx()), allocaInst, mCurMethodInstance->mReturnType->mAlign);
				mBfIRBuilder->ClearDebugLocation(storeInst);
				mCurMethodState->mRetValAddr = allocaInst;
			}
			else if (wantsRetVal)
			{
				auto allocaInst = AllocLocalVariable(mCurMethodInstance->mReturnType, "__return", false);
				mCurMethodState->mRetVal = BfTypedValue(allocaInst, mCurMethodInstance->mReturnType, true);
			}
		}

		if (methodDef->mMethodType == BfMethodType_CtorCalcAppend)
		{
			mBfIRBuilder->CreateAlignedStore(GetConstValue(0), mCurMethodState->mRetVal.mValue, mCurMethodState->mRetVal.mType->mAlign);
			BfGetSymbolReferenceKind prevSymbolKind;
			BfAutoComplete* prevAutoComplete;
			if (mCompiler->mResolvePassData != NULL)
			{
				prevSymbolKind = mCompiler->mResolvePassData->mGetSymbolReferenceKind;
				prevAutoComplete = mCompiler->mResolvePassData->mAutoComplete;
				mCompiler->mResolvePassData->mGetSymbolReferenceKind = BfGetSymbolReferenceKind_None;
			}
			EmitCtorCalcAppend();
			if (mCompiler->mResolvePassData != NULL)
			{
				mCompiler->mResolvePassData->mGetSymbolReferenceKind = prevSymbolKind;
				mCompiler->mResolvePassData->mAutoComplete = prevAutoComplete;
			}
		}
		else if (!isEmptyBodied)
		{
			if (!mCurMethodState->mIRExitBlock)
				mCurMethodState->mIRExitBlock = mBfIRBuilder->CreateBlock("exit", true);

			bool isExpressionBody = false;
			if (methodDeclaration != NULL)
			{
				if (methodDeclaration->mFatArrowToken != NULL)
					isExpressionBody = true;
			}
			else if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
			{
				auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration();
				if ((propertyMethodDeclaration != NULL) && (propertyMethodDeclaration->mFatArrowToken != NULL))
					isExpressionBody = true;
				if (auto propBodyExpr = BfNodeDynCast<BfPropertyBodyExpression>(propertyDeclaration->mDefinitionBlock))
				{
					isExpressionBody = true;
				}
			}
			else
			{
				if (auto block = BfNodeDynCast<BfBlock>(methodDef->mBody))
				{
					if (!block->mChildArr.IsEmpty())
					{
						if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(block->mChildArr.GetLast()))
						{
						 	if (exprStmt->mTrailingSemicolon == NULL)
						 		isExpressionBody = true;
						}
					}
				}
				else
					isExpressionBody = true;
			}

			DoCEEmit(methodInstance);
			if (methodInstance->mCeCancelled)
				mIgnoreErrors = true;

			if (auto fieldDtorBody = BfNodeDynCast<BfFieldDtorDeclaration>(methodDef->mBody))
			{
				while (fieldDtorBody != NULL)
				{
					VisitEmbeddedStatement(fieldDtorBody->mBody);
					fieldDtorBody = fieldDtorBody->mNextFieldDtor;
				}
			}
			else if (!isExpressionBody)
			{
				auto bodyBlock = BfNodeDynCast<BfBlock>(methodDef->mBody);
				mCurMethodState->mHeadScope.mAstBlock = bodyBlock;
				mCurMethodState->mHeadScope.mCloseNode = bodyBlock->mCloseBrace;
				VisitCodeBlock(bodyBlock);
			}
			else if (auto expressionBody = BfNodeDynCast<BfExpression>(methodDef->mBody))
			{
// 				if ((methodDef->mMethodType != BfMethodType_Normal) && (propertyDeclaration == NULL))
// 				{
// 					BF_ASSERT(methodDeclaration->mFatArrowToken != NULL);
// 					Fail("Only normal methods can have expression bodies", methodDeclaration->mFatArrowToken);
// 				}

				auto expectingType = mCurMethodInstance->mReturnType;
				// What was this error for?
// 				if ((expectingType->IsVoid()) && (IsInSpecializedSection()))
// 				{
// 					Warn(0, "Using a 'void' return with an expression-bodied method isn't needed. Consider removing '=>' token", methodDeclaration->mFatArrowToken);
// 				}

				BfEvalExprFlags exprEvalFlags = (BfEvalExprFlags)(BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_IsExpressionBody);
				if (expectingType->IsVoid())
				{
					exprEvalFlags = (BfEvalExprFlags)(exprEvalFlags | BfEvalExprFlags_NoCast);

					bool wasReturnGenericParam = false;
					if ((mCurMethodState->mClosureState != NULL) && (mCurMethodState->mClosureState->mReturnType != NULL))
					{
						wasReturnGenericParam = mCurMethodState->mClosureState->mReturnType->IsGenericParam();
					}
					else
					{
						auto unspecializedMethodInstance = GetUnspecializedMethodInstance(mCurMethodInstance);
						if ((unspecializedMethodInstance != NULL) && (unspecializedMethodInstance->mReturnType->IsGenericParam()))
							wasReturnGenericParam = true;
					}

					// If we the void return was from a generic specialization, allow us to return a void result,
					//  otherwise treat expression as though it must be a statement
					bool isStatement = expressionBody->VerifyIsStatement(mCompiler->mPassInstance, wasReturnGenericParam);
					if (isStatement)
						expectingType = NULL;
				}

				BfExprEvaluator exprEvaluator(this);
				if (mCurMethodInstance->mMethodDef->mIsReadOnly)
					exprEvaluator.mAllowReadOnlyReference = true;

				UpdateSrcPos(expressionBody);
				auto retVal = CreateValueFromExpression(exprEvaluator, expressionBody, expectingType, exprEvalFlags);
				if ((retVal) && (!retVal.mType->IsVar()) && (expectingType != NULL))
				{
					mCurMethodState->mHadReturn = true;
					retVal = LoadOrAggregateValue(retVal);
					EmitReturn(retVal);
				}
			}
		}
	}

	BF_ASSERT(mCurMethodState->mCurScope == &mCurMethodState->mHeadScope);

	if (skipUpdateSrcPos)
	{
		// Skip
	}
	else if ((bodyBlock != NULL) && (bodyBlock->mCloseBrace != NULL))
		UpdateSrcPos(bodyBlock->mCloseBrace);
	else if (methodDef->mBody != NULL)
		UpdateSrcPos(methodDef->mBody);
	else if (methodDeclaration != NULL)
		UpdateSrcPos(methodDeclaration);
	else if (methodDef->mDeclaringType->mTypeDeclaration != NULL)
		UpdateSrcPos(methodDef->mDeclaringType->mTypeDeclaration);
	else
		UseDefaultSrcPos();

	if (methodDef->mMethodType == BfMethodType_CtorCalcAppend)
	{
		if (mCurMethodState->mRetVal)
		{
			mCurMethodState->SetHadReturn(true);
			auto retVal = mBfIRBuilder->CreateLoad(mCurMethodState->mRetVal.mValue);
			mBfIRBuilder->CreateRet(retVal);
		}
		else
			AssertErrorState();
	}

	mCurMethodState->mHeadScope.mDone = true;
	if (!mCurMethodState->mHadReturn)
	{
		// Clear off the stackallocs that have occurred after a scopeData break
		EmitDeferredScopeCalls(false, &mCurMethodState->mHeadScope, mCurMethodState->mIRExitBlock);
	}

	EmitDeferredCallProcessorInstances(&mCurMethodState->mHeadScope);

	if (mCurMethodState->mIRExitBlock)
	{
		for (auto preExitBlock : mCurMethodState->mHeadScope.mAtEndBlocks)
			mBfIRBuilder->MoveBlockToEnd(preExitBlock);

        mBfIRBuilder->MoveBlockToEnd(mCurMethodState->mIRExitBlock);
		mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRExitBlock);

		if ((!mCurMethodState->mDIRetVal) && (wantsDIData))
			CreateDIRetVal();
	}

	for (auto localVar : mCurMethodState->mLocals)
	{
		if (auto autoCtorDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(methodDeclaration))
		{
			//
		}
		else if ((skipEndChecks) || (methodDef->mBody == NULL))
			break;
		LocalVariableDone(localVar, true);
	}

	if (mCurMethodState->mIRExitBlock)
	{
		if ((mCurMethodState->mRetVal) &&
			((mIsComptimeModule) || (mCurMethodInstance->GetStructRetIdx() == -1)))
		{
			auto loadedVal = mBfIRBuilder->CreateAlignedLoad(mCurMethodState->mRetVal.mValue, mCurMethodState->mRetVal.mType->mAlign);

			CreateReturn(loadedVal);

			EmitLifetimeEnds(&mCurMethodState->mHeadScope);

			if (mCurMethodState->mDIRetVal)
				mBfIRBuilder->DbgLifetimeEnd(mCurMethodState->mDIRetVal);
		}
		else
		{
			// Have something on the last line to step onto
			if (mHasFullDebugInfo)
			{
				if ((bodyBlock != NULL) && (bodyBlock->mCloseBrace != NULL))
					UpdateSrcPos(bodyBlock->mCloseBrace);
				EmitEnsureInstructionAt();

				if ((irParamCount == 0) && (!IsTargetingBeefBackend()) && (mCompiler->mOptions.mAllowHotSwapping))
				{
					// This may be a case where we only emit 4 bytes, whereas we need 5 for a hot replace jump
					mBfIRBuilder->EnsureFunctionPatchable();
				}
			}
			mBfIRBuilder->CreateRetVoid();
			EmitLifetimeEnds(&mCurMethodState->mHeadScope);

			if (mCurMethodState->mDIRetVal)
				mBfIRBuilder->DbgLifetimeEnd(mCurMethodState->mDIRetVal);
		}
	}

	if ((mCurMethodInstance->mReturnType == NULL) || (mCurMethodInstance->mReturnType->IsValuelessType()))
	{
		if ((!mCurMethodState->mHadReturn) && (!mCurMethodState->mIRExitBlock))
		{
			if ((!mIsComptimeModule) && (irParamCount == 0) && (!IsTargetingBeefBackend()) && (mCompiler->mOptions.mAllowHotSwapping))
			{
				// This may be a case where we only emit 4 bytes, whereas we need 5 for a hot replace jump
				mBfIRBuilder->EnsureFunctionPatchable();
			}

			mBfIRBuilder->CreateRetVoid();
		}
	}
	else
	{
		if (!mCurMethodState->mHadReturn)
		{
			auto refNode = mCurMethodInstance->mMethodDef->GetRefNode();
			if (bodyBlock != NULL)
			{
				if (bodyBlock->mCloseBrace != NULL)
				{
					BfAstNode* target = bodyBlock->mCloseBrace;
					if (!mCompiler->mHasRequiredTypes)
					{
						AddFailType(mCurTypeInstance);
						mHadBuildError = true;
					}
					else
						Fail("Method must return value", target);
				}
				else
				{
					// It's possible to not have a closing brace if the method ends in an EOF
					AssertErrorState();
				}
			}
		}
	}

	// Move 'init' into 'entry'
	mBfIRBuilder->MergeBlockDown(mCurMethodState->mIRInitBlock, mCurMethodState->mIREntryBlock);
	mBfIRBuilder->MergeBlockDown(mCurMethodState->mIRHeadBlock, mCurMethodState->mIREntryBlock);

	if (((mCurMethodInstance->mIsUnspecialized) /*|| (typeDef->mIsFunction)*/ || (mCurTypeInstance->IsUnspecializedType())) &&
		(!mIsComptimeModule))
	{
		// Don't keep instructions for unspecialized types
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);
	}

	// Avoid linking any internal funcs that were just supposed to be comptime-accessible
	/*if ((methodDef->mHasComptime) && (!mIsComptimeModule))
		wantsRemoveBody = true;*/

	if ((hasExternSpecifier) && (!skipBody))
	{
		// If we hot swap, we want to make sure at least one method refers to this extern method so it gets pulled in
		//  incase it gets called later by some hot-loaded coded
		if ((mCompiler->mOptions.mAllowHotSwapping) && (mCurMethodInstance->mIRFunction) && (!mCurMethodInstance->mIRFunction.IsFake()) && (mCurTypeInstance != mContext->mBfObjectType))
		{
			if (!mCurMethodInstance->mMethodDef->mName.StartsWith("Comptime_"))
				CreateFakeCallerMethod(mangledName);
		}
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);
	}
	else if ((methodInstance->GetImportKind() == BfImportKind_Import_Dynamic) && (!mCompiler->IsHotCompile()))
	{
		// We only need the DLL stub when we may be hot swapping
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);
	}
	else if (wantsRemoveBody)
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);

	// We don't want to hold on to pointers to LLVMFunctions of unspecialized types.
	//  This allows us to delete the mScratchModule LLVM module without rebuilding all
	//  unspecialized types
	if (((mCurTypeInstance->IsUnspecializedType()) && (!mIsComptimeModule)) ||
		(mCurTypeInstance->IsInterface()))
	{
		BfLogSysM("ProcessMethod Clearing IRFunction: %p\n", methodInstance);
		methodInstance->mIRFunction = BfIRFunction();
	}

	CheckAddFailType();

	if (mHadBuildError)
		prevHadBuildError.CancelRestore();
	if (mHadBuildWarning)
		prevHadWarning.CancelRestore();

	if (!methodDef->mIsLocalMethod)
		ProcessMethod_ProcessDeferredLocals();

	prevIgnoreWrites.Restore();
	mBfIRBuilder->SetActiveFunction(prevActiveFunction);

	if (methodState.mHotDataReferenceBuilder != NULL)
	{
		AddHotDataReferences(&hotDataReferenceBuilder);
	}
	else
	{
		if ((methodInstance->mHotMethod != NULL) && (!methodInstance->mIsReified))
		{
			// Remove the hot method data
			auto hotMethod = methodInstance->mHotMethod;
			auto prevMethod = hotMethod->mPrevVersion;

			if (prevMethod != NULL)
			{
				// If there's a prev method then pull its data into the main HotMethod.
				//  This, in effect, removes the 'hotMethod' data and rebases 'prevMethod'
				//  over to the main HotMethod definition to keep the HotMethod address
				//  invariant
				for (auto ref : hotMethod->mReferences)
					ref->Deref();
				hotMethod->mReferences.Clear();
				BF_ASSERT(prevMethod->mRefCount == 1);

				hotMethod->mReferences = prevMethod->mReferences;

				if (hotMethod->mSrcTypeVersion != NULL)
					hotMethod->mSrcTypeVersion->Deref();
				hotMethod->mSrcTypeVersion = prevMethod->mSrcTypeVersion;
				prevMethod->mSrcTypeVersion = NULL;

				prevMethod->mReferences.Clear();
				hotMethod->mPrevVersion = prevMethod->mPrevVersion;
				prevMethod->mPrevVersion = NULL;
				prevMethod->Deref();
			}

			hotMethod->Deref();
			methodInstance->mHotMethod = NULL;
		}
	}
}

String BfModule::GetLocalMethodName(const StringImpl& baseName, BfAstNode* anchorNode, BfMethodState* declMethodState, BfMixinState* declMixinState)
{
	String name;

	bool found = false;
	auto checkMethodState = mCurMethodState;
	while (checkMethodState != NULL)
	{
		if (checkMethodState->mClosureState == NULL)
			break;
		if (checkMethodState->mClosureState->mClosureMethodDef != NULL)
		{
			found = true;
			name += checkMethodState->mClosureState->mClosureMethodDef->mName;
			break;
		}
		checkMethodState = checkMethodState->mPrevMethodState;
	}

	if (!found)
		name += mCurMethodInstance->mMethodDef->mName;

	int prevSepPos = (int)name.LastIndexOf('$');
	if (prevSepPos != -1)
	{
		name.RemoveToEnd(prevSepPos);
	}

	name += "@";
	name += baseName;

	HashContext hashCtx;
	if (anchorNode != NULL)
	{
		hashCtx.Mixin(anchorNode->GetStartCharId());
	}

	//
	{
		auto checkMethodState = declMethodState;
		auto checkMixinState = declMixinState;
		while (checkMethodState != NULL)
		{
			if (checkMixinState != NULL)
			{
				hashCtx.Mixin(checkMixinState->mSource->GetStartCharId());
			}
			checkMethodState = checkMethodState->mPrevMethodState;
			if (checkMethodState != NULL)
				checkMixinState = checkMethodState->mMixinState;
		}
	}

	auto rootMethodState = mCurMethodState->GetRootMethodState();
	if ((rootMethodState != NULL) && (rootMethodState->mMethodInstance != NULL) && (rootMethodState->mMethodInstance->mMethodInfoEx != NULL))
	{
		for (auto methodGenericArg : rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments)
		{
			StringT<512> genericTypeName;
			BfMangler::Mangle(genericTypeName, mCompiler->GetMangleKind(), methodGenericArg);
			hashCtx.MixinStr(genericTypeName);
		}
	}

	uint64 hashVal = hashCtx.Finish64();

	name += "$";
	name += BfTypeUtils::HashEncode64(hashVal);

	return name;
}

BfMethodDef* BfModule::GetLocalMethodDef(BfLocalMethod* localMethod)
{
	if (localMethod->mMethodDef != NULL)
		return localMethod->mMethodDef;

	auto rootMethodState = mCurMethodState->GetRootMethodState();
	auto declMethodState = localMethod->mDeclMethodState;
	auto declMixinState = localMethod->mDeclMixinState;

	auto typeInst = mCurTypeInstance;
	BfAstNode* body = NULL;
	BfAstNode* anchorNode = NULL;

 	auto _AllocDirectTypeRef = [&](BfType* type)
 	{
 		BfDirectTypeReference* directTypeRef = localMethod->mDirectTypeRefs.Alloc();
 		directTypeRef->Init(type);
 		return directTypeRef;
 	};

	auto methodDeclaration = localMethod->mMethodDeclaration;
	if (methodDeclaration != NULL)
	{
		body = methodDeclaration->mBody;
		anchorNode = methodDeclaration->mOpenParen;
	}
	else
	{
		body = localMethod->mLambdaBindExpr->mBody;
		anchorNode = localMethod->mLambdaBindExpr->mFatArrowToken;
	}

	BF_ASSERT(rootMethodState->mCurLocalVarId >= 0);
	int methodLocalIdx = rootMethodState->mCurLocalVarId++;

	BfMethodDef* methodDef;

	BfMethodDef* outerMethodDef = NULL;
	if (localMethod->mOuterLocalMethod != NULL)
		outerMethodDef = localMethod->mOuterLocalMethod->mMethodDef;
	else if (rootMethodState->mMethodInstance != NULL)
		outerMethodDef = rootMethodState->mMethodInstance->mMethodDef;

	if (methodDeclaration != NULL)
	{
		BfDefBuilder defBuilder(mCompiler->mSystem);
		defBuilder.mCurSource = localMethod->mMethodDeclaration->GetParser();
		defBuilder.mPassInstance = mCompiler->mPassInstance;
		defBuilder.mCurTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;
		defBuilder.mCurDeclaringTypeDef = defBuilder.mCurTypeDef;
		methodDef = defBuilder.CreateMethodDef(methodDeclaration, outerMethodDef);
	}
	else
	{
 		auto invokeMethod = localMethod->mLambdaInvokeMethodInstance;

 		methodDef = new BfMethodDef();
 		methodDef->mName = localMethod->mMethodName;
 		methodDef->mDeclaringType = mCurMethodInstance->mMethodDef->mDeclaringType;
 		methodDef->mReturnTypeRef = _AllocDirectTypeRef(invokeMethod->mReturnType);
 		methodDef->mBody = body;

 		for (int paramIdx = 0; paramIdx < invokeMethod->GetParamCount(); paramIdx++)
 		{
 			auto paramType = invokeMethod->GetParamType(paramIdx);
 			String paramName;
 			if (paramIdx < (int)localMethod->mLambdaBindExpr->mParams.size())
 				paramName = localMethod->mLambdaBindExpr->mParams[paramIdx]->ToString();
 			else
 				paramName = invokeMethod->GetParamName(paramIdx);

 			auto paramDef = new BfParameterDef();
 			paramDef->mTypeRef = _AllocDirectTypeRef(paramType);
 			paramDef->mName = paramName;
 			methodDef->mParams.Add(paramDef);
 		}

		if (outerMethodDef != NULL)
		{
			for (auto genericParam : outerMethodDef->mGenericParams)
			{
				auto* copiedGenericParamDef = new BfGenericParamDef();
				*copiedGenericParamDef = *genericParam;
				methodDef->mGenericParams.Add(copiedGenericParamDef);
			}
		}
	}

	methodDef->mIdx = ~methodLocalIdx;

	BF_ASSERT(!localMethod->mExpectedFullName.IsEmpty());
	methodDef->mName = localMethod->mExpectedFullName;

	// 		methodDef->mName = GetLocalMethodName(methodDef->mName, anchorNode, declMethodState, declMixinState);
	//
	// 		if (!localMethod->mExpectedFullName.IsEmpty())
	// 			BF_ASSERT(methodDef->mName == localMethod->mExpectedFullName);

	methodDef->mIsStatic = true;
	methodDef->mIsLocalMethod = true;
	methodDef->mIsVirtual = false;
	localMethod->mMethodDef = methodDef;

	auto methodInstanceGroup = new BfMethodInstanceGroup();
	localMethod->mMethodInstanceGroup = methodInstanceGroup;
	methodInstanceGroup->mMethodIdx = -1;
	methodInstanceGroup->mOwner = mCurTypeInstance;
	methodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;

	if ((declMethodState->mMixinState != NULL) && (declMethodState->mMixinState->mTarget))
	{
		methodInstanceGroup->mOwner = declMethodState->mMixinState->mTarget.mType->ToTypeInstance();
		BF_ASSERT(methodInstanceGroup->mOwner != NULL);
	}

	if ((mCompiler->mResolvePassData != NULL) && (methodDeclaration != NULL))
	{
		mCompiler->mResolvePassData->HandleLocalReference(methodDeclaration->mNameNode, typeInst->mTypeDef, rootMethodState->mMethodInstance->mMethodDef, methodLocalIdx);
		auto autoComplete = mCompiler->mResolvePassData->mAutoComplete;
		if ((autoComplete != NULL) && (autoComplete->IsAutocompleteNode(methodDeclaration->mNameNode)))
		{
			autoComplete->SetDefinitionLocation(methodDeclaration->mNameNode);
			autoComplete->mInsertStartIdx = methodDeclaration->mNameNode->GetSrcStart();
			autoComplete->mInsertEndIdx = methodDeclaration->mNameNode->GetSrcEnd();
			autoComplete->mDefType = typeInst->mTypeDef;
			autoComplete->mDefMethod = rootMethodState->mMethodInstance->mMethodDef;
			autoComplete->mReplaceLocalId = methodLocalIdx;
		}
	}
	return localMethod->mMethodDef;
}

BfModuleMethodInstance BfModule::GetLocalMethodInstance(BfLocalMethod* localMethod, const BfTypeVector& methodGenericArguments, BfMethodInstance* methodInstance, bool force)
{
	BP_ZONE_F("GetLocalMethodInstance %s", localMethod->mMethodName.c_str());

	BfLogSysM("GetLocalMethodInstance %p\n", localMethod);

	auto rootMethodState = mCurMethodState->GetRootMethodState();
	auto declMethodState = localMethod->mDeclMethodState;
	auto declMixinState = localMethod->mDeclMixinState;
	auto callerMethodState = mCurMethodState;

	auto typeInst = mCurTypeInstance;

	if (mCurMethodState->mMixinState != NULL)
	{
		typeInst = mCurMethodState->mMixinState->mMixinMethodInstance->GetOwner();
	}

	auto methodDef = GetLocalMethodDef(localMethod);

	BfAstNode* body = NULL;

	auto methodDeclaration = localMethod->mMethodDeclaration;
	if (methodDeclaration != NULL)
	{
		body = methodDeclaration->mBody;
	}
	else
	{
		body = localMethod->mLambdaBindExpr->mBody;
	}

	bool hadConcreteInterfaceGenericArgument = false;

	BfTypeVector sanitizedMethodGenericArguments;

	// Ignore the outermost method's generic method arguments for the purpose of determining if we are the 'default' (ie: unspecialized)
	//  version of this method for this pass through the outermost method
	int dependentGenericStartIdx = 0;
	if ((rootMethodState->mMethodInstance != NULL) && (rootMethodState->mMethodInstance->mMethodInfoEx != NULL))
		dependentGenericStartIdx = (int)rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();

	BfMethodInstance* outerMethodInstance = mCurMethodInstance;

	if (methodGenericArguments.size() == 0)
	{
		if ((rootMethodState->mMethodInstance != NULL) && (rootMethodState->mMethodInstance->mMethodInfoEx != NULL))
			sanitizedMethodGenericArguments = rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments;
	}
	else
	{
		int genericArgIdx = 0;
// 		for (; genericArgIdx < (int)outerMethodInstance->mMethodGenericArguments.size(); genericArgIdx++)
// 		{
// 			BF_ASSERT(methodGenericArguments[genericArgIdx] == outerMethodInstance->mMethodGenericArguments[genericArgIdx]);
// 			sanitizedMethodGenericArguments.push_back(methodGenericArguments[genericArgIdx]);
// 		}

		for (; genericArgIdx < (int)methodGenericArguments.size(); genericArgIdx++)
		{
			auto genericArgType = methodGenericArguments[genericArgIdx];
			BF_ASSERT(!genericArgType->IsRef());

			if (genericArgType->IsConcreteInterfaceType())
			{
				hadConcreteInterfaceGenericArgument = true;
				auto concreteInterfaceType = (BfConcreteInterfaceType*)genericArgType;
				genericArgType = concreteInterfaceType->mInterface;
			}

			sanitizedMethodGenericArguments.push_back(genericArgType);
		}
	}

	bool wantPrematureMethodInstance = false;
	if (!force)
	{
		wantPrematureMethodInstance = (callerMethodState->mClosureState != NULL) && (callerMethodState->mClosureState->mCaptureVisitingBody);
	}

	auto methodInstGroup = localMethod->mMethodInstanceGroup;

	bool isDefaultPass = true;
	BfTypeVector lookupMethodGenericArguments;
	for (int genericArgIdx = dependentGenericStartIdx; genericArgIdx < (int)methodGenericArguments.size(); genericArgIdx++)
	{
		auto genericArgType = methodGenericArguments[genericArgIdx];
		BF_ASSERT(!genericArgType->IsRef());
		lookupMethodGenericArguments.Add(genericArgType);

		if (genericArgType->IsGenericParam())
		{
			auto genericParam = (BfGenericParamType*)genericArgType;
			if ((genericParam->mGenericParamKind != BfGenericParamKind_Method) || (genericParam->mGenericParamIdx != genericArgIdx))
				isDefaultPass = false;
		}
		else
		{
			isDefaultPass = false;
		}
	}

	bool hadExistingMethodInstance = false;

	if (methodInstance != NULL)
	{
		hadExistingMethodInstance = true;
	}
	else
	{
		if (isDefaultPass)
		{
			methodInstance = methodInstGroup->mDefault;
			if (methodInstance != NULL)
			{
				hadExistingMethodInstance = true;
			}
			else
			{
				methodInstance = new BfMethodInstance();
				methodInstGroup->mDefault = methodInstance;
			}
		}
		else
		{
			BF_ASSERT(sanitizedMethodGenericArguments.size() != 0);
			if (methodInstGroup->mMethodSpecializationMap == NULL)
				methodInstGroup->mMethodSpecializationMap = new BfMethodInstanceGroup::MapType();

			BfMethodInstance** methodInstancePtr = NULL;
			if (!methodInstGroup->mMethodSpecializationMap->TryAdd(lookupMethodGenericArguments, NULL, &methodInstancePtr))
			{
				methodInstance = *methodInstancePtr;
				hadExistingMethodInstance = true;
			}
			else
			{
				methodInstance = new BfMethodInstance();
				*methodInstancePtr = methodInstance;
			}
		}
	}

	if (hadExistingMethodInstance)
	{
		if ((!methodInstance->mDisallowCalling) ||
			((wantPrematureMethodInstance) && (methodInstance->mDisallowCalling)))
			return BfModuleMethodInstance(methodInstance);
		BF_ASSERT((methodInstance->mMethodInfoEx == NULL) || (methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState == NULL));
	}
	else
	{
		auto methodInfoEx = methodInstance->GetMethodInfoEx();
		methodInfoEx->mClosureInstanceInfo = new BfClosureInstanceInfo();
		methodInfoEx->mClosureInstanceInfo->mLocalMethod = localMethod;

		methodInstance->mIsReified = mCurMethodInstance->mIsReified;
		methodInstance->mIsAutocompleteMethod = mCurMethodInstance->mIsAutocompleteMethod;

		methodInstance->mMethodDef = methodDef;
		methodInstance->mMethodInstanceGroup = localMethod->mMethodInstanceGroup;
		if (hadConcreteInterfaceGenericArgument)
			methodInstance->mIgnoreBody = true;

		methodInfoEx->mMethodGenericArguments = sanitizedMethodGenericArguments;

		for (auto genericArg : sanitizedMethodGenericArguments)
		{
			if (genericArg->IsPrimitiveType())
				genericArg = GetWrappedStructType(genericArg);
			AddDependency(genericArg, typeInst, BfDependencyMap::DependencyFlag_MethodGenericArg);
		}

		for (int genericParamIdx = (int)sanitizedMethodGenericArguments.size(); genericParamIdx < (int)methodDef->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParamType = GetGenericParamType(BfGenericParamKind_Method, genericParamIdx);
			methodInfoEx->mMethodGenericArguments.push_back(genericParamType);
		}
		SetupMethodIdHash(methodInstance);
	}

	auto _SetupMethodInstance = [&]()
	{
		BF_ASSERT(methodInstance->GetNumGenericParams() == 0);

		// Generic constraints
		for (int genericParamIdx = 0; genericParamIdx < (int)methodDef->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, genericParamIdx);
			methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
		}

		for (int externConstraintIdx = 0; externConstraintIdx < (int)methodDef->mExternalConstraints.size(); externConstraintIdx++)
		{
			auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, externConstraintIdx + (int)methodDef->mGenericParams.size());
			methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);
		}
	};

	//////////////////////////////////////////////////////////////////////////

	auto _VisitLambdaBody = [&]()
	{
		if (localMethod->mDeclOnly)
			return;
		if (mCompiler->mCanceling)
			return;

		if (auto blockBody = BfNodeDynCast<BfBlock>(body))
		{
			VisitCodeBlock(blockBody);
		}
		else if (auto bodyExpr = BfNodeDynCast<BfExpression>(body))
		{
			BfType* expectType = NULL;
			if (!methodInstance->mReturnType->IsVoid())
				expectType = methodInstance->mReturnType;
			CreateValueFromExpression(bodyExpr, expectType, BfEvalExprFlags_AllowRefExpr);
		}
	};

	auto _SafeResolveTypeRef = [&](BfTypeReference* typeRef)
	{
		SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
		auto result = ResolveTypeRef(typeRef);
		if (result == NULL)
			result = mContext->mBfObjectType;
		return result;
	};

	BfTypeInstance* outerClosure = NULL;
	if ((declMethodState->mClosureState != NULL) && (!declMethodState->mClosureState->mCapturing))
		outerClosure = declMethodState->mClosureState->mClosureType;

	BfMethodState methodState;
	methodState.mPrevMethodState = declMethodState;//mCurMethodState;

	BfIRFunctionType funcType;
	auto voidType = GetPrimitiveType(BfTypeCode_None);
	SizedArray<BfIRType, 0> paramTypes;
	funcType = mBfIRBuilder->CreateFunctionType(mBfIRBuilder->MapType(voidType), paramTypes, methodInstance->IsVarArgs());

	mBfIRBuilder->SaveDebugLocation();
	auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
	auto prevActiveFunction = mBfIRBuilder->GetActiveFunction();

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);

	BfDeferredLocalAssignData deferredLocalAssignData;
	//TODO: Why did we do this? It can cause us to pull in local variables that don't belong to us...
	/*if (declMethodState->mDeferredLocalAssignData != NULL)
		deferredLocalAssignData.ExtendFrom(declMethodState->mDeferredLocalAssignData);*/
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);

	methodState.mIRHeadBlock = mBfIRBuilder->CreateBlock("head", true);
	methodState.mIRInitBlock = mBfIRBuilder->CreateBlock("init", true);
	methodState.mIREntryBlock = mBfIRBuilder->CreateBlock("entry", true);
	methodState.mCurScope->mDIScope = localMethod->mDeclDIScope; //declMethodState->mCurScope->mDIScope ;
	//methodState.mCurLocalVarId = declMethodState->mCurLocalVarId;
	methodState.mIRFunction = declMethodState->mIRFunction;
	methodState.mDeferredLocalAssignData = &deferredLocalAssignData;

	if (auto blockBody = BfNodeDynCast<BfBlock>(body))
	{
		methodState.mCurScope->mAstBlock = blockBody;
		methodState.mCurScope->mCloseNode = blockBody->mCloseBrace;
	}

	mBfIRBuilder->SetInsertPoint(methodState.mIREntryBlock);

	BfClosureState closureState;
	if (methodDef->mMethodType == BfMethodType_Mixin)
	{
		if (declMethodState != NULL)
		{
			if ((declMethodState->mClosureState != NULL) && (declMethodState->mClosureState->mReturnType != NULL))
				closureState.mReturnType = declMethodState->mClosureState->mReturnType;
			else if (declMethodState->mMethodInstance != NULL)
				closureState.mReturnType = declMethodState->mMethodInstance->mReturnType;
		}
	}

	if (closureState.mReturnType == NULL)
		closureState.mReturnType = _SafeResolveTypeRef(methodDef->mReturnTypeRef);
	closureState.mCapturing = true;
	closureState.mLocalMethod = localMethod;
	closureState.mClosureInstanceInfo = methodInstance->mMethodInfoEx->mClosureInstanceInfo;
	closureState.mDeclaringMethodIsMutating = mCurMethodInstance->mMethodDef->mIsMutating;
	methodState.mClosureState = &closureState;
	closureState.mClosureType = outerClosure;

	int outerLocalsCount = (int)methodState.mLocals.size();

	if (!hadExistingMethodInstance)
	{
		BfLogSysM("LocalMethod %p premature DoMethodDeclaration %p (Source:%p)\n", localMethod, methodInstance, (methodDeclaration != NULL) ? methodDeclaration->GetSourceData() : NULL);

		SetAndRestoreValue<bool> prevIgnoreWrites(mWantsIRIgnoreWrites, true);
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);

		// Prematurely do declaration.  We will have to undo this later, so don't write this one and don't throw errors
		auto declareModule = this;
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(declareModule->mCurMethodInstance, methodInstance);
		SetAndRestoreValue<BfFilePosition> prevFilePos(declareModule->mCurFilePosition);
		BF_ASSERT(declareModule != NULL);
		methodInstance->mDeclModule = declareModule;
		_SetupMethodInstance();
		declareModule->DoMethodDeclaration(localMethod->mMethodDef->GetMethodDeclaration(), false, false);
		methodInstance->mDisallowCalling = true; // Don't allow us to ACTUALLY call until we add the captures
	}

	if (wantPrematureMethodInstance)
	{
		// Return a premature methodInstance -- since we are partway through determining the capture of a local method, we can't rely on its
		//  capture information.  This returned method instance can only be used for method matching, but we will visit it later to create
		//  its true method instance
		//auto moduleMethodInstance = _CheckReturnedMethod(methodInstance);
		//BF_ASSERT(moduleMethodInstance);
		return BfModuleMethodInstance(methodInstance);
	}

	auto checkInsertBlock = mBfIRBuilder->GetInsertBlock();

	BF_ASSERT(methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState == NULL);

	closureState.mCaptureStartAccessId = mCurMethodState->GetRootMethodState()->mCurAccessId;
	methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState = &closureState;
	closureState.mClosureMethodDef = methodDef;

	bool wantsVisitBody = true;
	if ((methodDef->mMethodType == BfMethodType_Mixin) && (!methodGenericArguments.IsEmpty()))
		wantsVisitBody = false;
	if (methodInstance->IsOrInUnspecializedVariation())
		wantsVisitBody = false;
	bool allowCapture = (methodDeclaration == NULL) || (methodDeclaration->mStaticSpecifier == NULL);

	if (wantsVisitBody)
	{
		BP_ZONE("VisitLambdaBody");

		// For generic methods, we capture for each specialization, so make sure mIsStatic is set to 'true' for
		//  this capture stage for each of those times
		SetAndRestoreValue<bool> prevWasStatis(methodDef->mIsStatic, true);

		/*
		// Only generate errors once. Also there are some errors that will occur during this scanning phase
		//  that will not occur during the actual lambda method generation, for example: returning a value
		//  when our current method is a 'void' method.  This shouldn't affect capture scanning since all
		//  our AST nodes will still be visited
		SetAndRestoreValue<bool> ignoreError(mIgnoreErrors, true);
		*/
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);

		//SetAndRestoreValue<bool> wantsIgnoreWrites(mWantsISIgnoreWrites, true);
		mBfIRBuilder->SaveDebugLocation();
		closureState.mCaptureVisitingBody = true;

		BF_ASSERT(methodInstance->mMethodInfoEx != NULL);
		methodState.mGenericTypeBindings = &methodInstance->mMethodInfoEx->mGenericTypeBindings;
		methodState.mMethodInstance = methodInstance;

		NewScopeState();
		ProcessMethod_SetupParams(methodInstance, NULL, false, NULL);
		if (methodInstance->mMethodDef->mMethodType == BfMethodType_Mixin)
		{
			// Allow assigning to arguments when we're capturing - which may or may not be legal at the injection site (we throw error there)
			for (auto localDef : methodState.mLocals)
			{
				if (localDef->mResolvedType->IsVar())
					continue;
				localDef->mAddr = CreateAlloca(localDef->mResolvedType);
			}
		}

		// Keep outs from being marked as assigned
		auto rootMethodState = mCurMethodState->GetRootMethodState();
		BfDeferredLocalAssignData deferredLocalAssignData(rootMethodState->mCurScope);
		deferredLocalAssignData.mVarIdBarrier = rootMethodState->mCurLocalVarId;

		auto prevDLA = rootMethodState->mDeferredLocalAssignData;
		while ((prevDLA != NULL) && (prevDLA->mIsChained))
			prevDLA = prevDLA->mChainedAssignData;
		if (prevDLA != NULL)
		{
			deferredLocalAssignData.mAssignedLocals = prevDLA->mAssignedLocals;
			deferredLocalAssignData.mLeftBlockUncond = prevDLA->mLeftBlockUncond;
		}

		SetAndRestoreValue<BfDeferredLocalAssignData*> sarDLA(rootMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);
		if (!mIgnoreErrors)
			localMethod->mDidBodyErrorPass = true;

		_VisitLambdaBody();
		RestoreScopeState();

		closureState.mCaptureVisitingBody = false;
		mBfIRBuilder->RestoreDebugLocation();
	}

	// If we ended up being called by a method with a lower captureStartAccessId, propagate that to whoever is calling us, too...
	if ((methodState.mPrevMethodState->mClosureState != NULL) && (methodState.mPrevMethodState->mClosureState->mCapturing))
	{
		auto prevClosureState = methodState.mPrevMethodState->mClosureState;
		if (closureState.mCaptureStartAccessId < prevClosureState->mCaptureStartAccessId)
			prevClosureState->mCaptureStartAccessId = closureState.mCaptureStartAccessId;
	}

	// Actually get the referenced local methods.  It's important we wait until after visiting the body, because then our own capture information
	//  can be used by these methods (which will be necessary if any of these methods call us directly or indirectly)
	closureState.mClosureMethodDef = NULL;
	for (auto methodInstance : closureState.mLocalMethodRefs)
	{
		GetLocalMethodInstance(methodInstance->mMethodInfoEx->mClosureInstanceInfo->mLocalMethod, BfTypeVector(), methodInstance);
	}

	methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureClosureState = NULL;
	prevIgnoreWrites.Restore();

	std::multiset<BfClosureCapturedEntry> capturedEntries;

	//
	{
		auto varMethodState = declMethodState;

		while (varMethodState != NULL)
		{
			if ((varMethodState->mMixinState != NULL) && (varMethodState->mMixinState->mLastTargetAccessId >= closureState.mCaptureStartAccessId))
			{
				BF_ASSERT(methodInstance->GetOwner() == varMethodState->mMixinState->mTarget.mType);
				methodDef->mIsStatic = false;
				if (rootMethodState->mMethodInstance->mMethodDef->mIsMutating)
					methodDef->mIsMutating = true;
			}

			bool copyOuterCaptures = false;
			for (int localIdx = 0; localIdx < varMethodState->mLocals.size(); localIdx++)
			{
				auto localVar = varMethodState->mLocals[localIdx];
				if ((localVar->mReadFromId >= closureState.mCaptureStartAccessId) || (localVar->mWrittenToId >= closureState.mCaptureStartAccessId))
				{
					if (!allowCapture)
					{
						if ((!localVar->mIsStatic) && (!localVar->mConstValue))
							continue;
					}

					if (localVar->mIsThis)
					{
						// We can only set mutating if our owning type is mutating
						if (localVar->mWrittenToId >= closureState.mCaptureStartAccessId)
						{
							if (typeInst->IsValueType())
							{
								if (rootMethodState->mMethodInstance->mMethodDef->mIsMutating)
									methodDef->mIsMutating = true;
							}
						}
						methodDef->mIsStatic = false;
						continue;
					}

					if ((localVar->mIsThis) && (outerClosure != NULL))
					{
						continue;
					}

		 			if ((localVar->mConstValue) || (localVar->mIsStatic) || (localVar->mResolvedType->IsValuelessType()))
		 			{
		 				closureState.mConstLocals.push_back(*localVar);
		 				continue;
		 			}

					BfClosureCapturedEntry capturedEntry;

					auto capturedType = localVar->mResolvedType;

					bool captureByRef = false;
					if (!capturedType->IsRef())
					{
						if (localVar->mIsThis)
						{
							if ((localVar->mResolvedType->IsValueType()) && (mCurMethodInstance->mMethodDef->HasNoThisSplat()))
								captureByRef = true;
						}
						else if ((!localVar->mIsReadOnly) && (localVar->mWrittenToId >= closureState.mCaptureStartAccessId))
							captureByRef = true;
					}
					else
					{
						if (localVar->mWrittenToId < closureState.mCaptureStartAccessId)
						{
							capturedType = ((BfRefType*)capturedType)->mElementType;
						}
					}

					if (captureByRef)
					{
						//capturedEntry.mIsByReference = true;
						capturedType = CreateRefType(capturedType);
					}
					else
					{
						//capturedEntry.mIsByReference = false;
					}
					capturedEntry.mNameNode = localVar->mNameNode;
					capturedEntry.mType = capturedType;
					capturedEntry.mName = localVar->mName;
					//capturedEntry.mLocalVarDef = localVar;
					capturedEntries.insert(capturedEntry);
				}
			}

			varMethodState = varMethodState->mPrevMethodState;
			if ((varMethodState == NULL) ||
				(varMethodState->mMixinState != NULL) ||
				((varMethodState->mClosureState != NULL) && (!varMethodState->mClosureState->mCapturing)))
				break;
		}
	}

	for (auto& copyField : closureState.mReferencedOuterClosureMembers)
	{
		auto fieldDef = copyField->GetFieldDef();
		BfClosureCapturedEntry capturedEntry;
		capturedEntry.mName = copyField->GetFieldDef()->mName;
		capturedEntry.mType = copyField->mResolvedType;
		if (capturedEntry.mType->IsRef())
		{
			// Keep by ref
		}
		else if (!fieldDef->mIsReadOnly)
		{
			capturedEntry.mType = CreateRefType(capturedEntry.mType);
		}

		capturedEntries.insert(capturedEntry);
	}

	int captureIdx = 0;
	for (auto& capturedEntry : capturedEntries)
	{
		methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries.Add(capturedEntry);
	}

	/*if (isDefaultPass)
	{
		// Insert captured members a params at the start.  If we added at the end then it would screw up varargs methods
		int captureIdx = 0;
		for (auto& capturedEntry : capturedEntries)
		{
			methodInstance->mClosureInstanceInfo->mCaptureNodes.Add(BfNodeDynCast<BfIdentifierNode>(capturedEntry.mNameNode));

			BfParameterDef* paramDef = new BfParameterDef();

			paramDef->mName = capturedEntry.mName;
			BfDirectTypeReference* directTypeRef = new BfDirectTypeReference();
			directTypeRef->mType = capturedEntry.mType;
			typeInst->mTypeDef->mDirectAllocNodes.push_back(directTypeRef);
			paramDef->mTypeRef = directTypeRef;
			paramDef->mParamKind = BfParamKind_ImplicitCapture;

			methodDef->mParams.Insert(captureIdx, paramDef);
			captureIdx++;
		}
	}
	else
	{
		auto defaultMethodInstance = methodInstGroup->mDefault;
		methodInstance->mClosureInstanceInfo->mCaptureNodes = defaultMethodInstance->mClosureInstanceInfo->mCaptureNodes;
	}*/

	methodState.Reset();
	closureState.mCapturing = false;

	//////////////////////////////////////////////////////////////////////////

	SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, false);

	if (methodInstance->mReturnType != NULL)
	{
		BF_ASSERT(methodInstance->mDisallowCalling);

		// We did a premature declaration (without captures)
		methodInstance->UndoDeclaration();
		methodInstance->mDisallowCalling = false;

		BfLogSysM("LocalMethod %p UndoingDeclaration %p\n", localMethod, methodInstance);
	}

	auto declareModule = this;
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(declareModule->mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfFilePosition> prevFilePos(declareModule->mCurFilePosition);
	BF_ASSERT(declareModule != NULL);
	methodInstance->mDeclModule = declareModule;
	_SetupMethodInstance();
	BfLogSysM("LocalMethod %p DoMethodDeclaration %p\n", localMethod, methodInstance);
	declareModule->DoMethodDeclaration(localMethod->mMethodDef->GetMethodDeclaration(), false, false);

	closureState.mReturnType = methodInstance->mReturnType;
	mCompiler->mStats.mMethodsQueued++;
	mCompiler->UpdateCompletion();
	declareModule->mIncompleteMethodCount++;
	mCompiler->mStats.mMethodsProcessed++;
	if (!methodInstance->mIsReified)
		mCompiler->mStats.mUnreifiedMethodsProcessed++;

	auto deferMethodState = rootMethodState;

	// Since we handle errors & warnings in the capture phase, we don't need to process any local methods for resolve-only (unless we're doing a mDbgVerifyCodeGen)
	if ((!localMethod->mDeclOnly) && (!methodInstance->IsOrInUnspecializedVariation()) &&
		(methodDef->mMethodType != BfMethodType_Mixin) &&
		((!mWantsIRIgnoreWrites) || (!localMethod->mDidBodyErrorPass)) &&
		(!mCompiler->IsDataResolvePass()))
	{
		BP_ZONE("BfDeferredLocalMethod:create");

		BfDeferredLocalMethod* deferredLocalMethod = new BfDeferredLocalMethod();
		deferredLocalMethod->mLocalMethod = localMethod;
		deferredLocalMethod->mConstLocals = closureState.mConstLocals;
		deferredLocalMethod->mMethodInstance = methodInstance;
		auto checkMethodState = declMethodState;
		while (checkMethodState != NULL)
		{
			if ((checkMethodState->mClosureState != NULL) && (checkMethodState->mClosureState->mActiveDeferredLocalMethod != NULL))
			{
				for (auto& mixinRecord : checkMethodState->mClosureState->mActiveDeferredLocalMethod->mMixinStateRecords)
					deferredLocalMethod->mMixinStateRecords.Add(mixinRecord);
			}

			if (checkMethodState->mMixinState != NULL)
			{
				BfMixinRecord mixinRecord;
				mixinRecord.mSource = checkMethodState->mMixinState->mSource;
				deferredLocalMethod->mMixinStateRecords.Add(mixinRecord);
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}

		rootMethodState->mDeferredLocalMethods.Add(deferredLocalMethod);
	}

	mBfIRBuilder->SetActiveFunction(prevActiveFunction);
	mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	mBfIRBuilder->RestoreDebugLocation();

	if (methodInstance == localMethod->mMethodInstanceGroup->mDefault)
	{
		auto checkMethodState = mCurMethodState;
		while (checkMethodState != NULL)
		{
			BfLocalMethod* nextLocalMethod = NULL;
			if (checkMethodState->mLocalMethodMap.TryGetValue(localMethod->mMethodName, &nextLocalMethod))
			{
				while (nextLocalMethod != NULL)
				{
					auto checkLocalMethod = nextLocalMethod;
					nextLocalMethod = nextLocalMethod->mNextWithSameName;

					if (checkLocalMethod == localMethod)
						continue;
					if (checkLocalMethod->mMethodInstanceGroup == NULL)
						continue;
					auto checkMethodInstance = checkLocalMethod->mMethodInstanceGroup->mDefault;
					if (checkMethodInstance == NULL)
						continue; // Not generated yet

					if (CompareMethodSignatures(localMethod->mMethodInstanceGroup->mDefault, checkMethodInstance))
					{
						String error = "Method already declared with the same parameter types";
						auto bfError = Fail(error, methodInstance->mMethodDef->GetRefNode(), true);
						if ((bfError != NULL) && (methodInstance->mMethodDef->GetRefNode() != checkMethodInstance->mMethodDef->GetRefNode()))
							mCompiler->mPassInstance->MoreInfo("First declaration", checkMethodInstance->mMethodDef->GetRefNode());
					}
				}
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}
	}

	return BfModuleMethodInstance(methodInstance);
}

int BfModule::GetLocalInferrableGenericArgCount(BfMethodDef* methodDef)
{
	if ((!methodDef->mIsLocalMethod) || (methodDef->mGenericParams.size() == 0))
		return 0;

	auto rootMethodState = mCurMethodState->GetRootMethodState();
	int rootMethodGenericParamCount = 0;
	if (rootMethodState->mMethodInstance->mMethodInfoEx != NULL)
		rootMethodGenericParamCount = (int)rootMethodState->mMethodInstance->mMethodInfoEx->mGenericParams.size();

	if (mCurMethodInstance == NULL)
		return rootMethodGenericParamCount;
	if ((mCurMethodInstance->mMethodInfoEx == NULL) || (mCurMethodInstance->mMethodInfoEx->mClosureInstanceInfo == NULL))
		return rootMethodGenericParamCount;

	BfLocalMethod* callerLocalMethod = mCurMethodInstance->mMethodInfoEx->mClosureInstanceInfo->mLocalMethod;
	if (callerLocalMethod == NULL)
		return rootMethodGenericParamCount;

	BfLocalMethod* calledLocalMethod = NULL;
	rootMethodState->mLocalMethodCache.TryGetValue(methodDef->mName, &calledLocalMethod);

	while ((calledLocalMethod != NULL) && (callerLocalMethod != NULL))
	{
		if (calledLocalMethod->mOuterLocalMethod == callerLocalMethod)
			return (int)callerLocalMethod->mMethodDef->mGenericParams.size();
		callerLocalMethod = calledLocalMethod->mOuterLocalMethod;
	}
	return rootMethodGenericParamCount;
}

void BfModule::GetMethodCustomAttributes(BfMethodInstance* methodInstance)
{
	auto methodDef = methodInstance->mMethodDef;

	auto customAttributes = methodInstance->GetCustomAttributes();
	if (customAttributes != NULL)
		return;

	auto methodDeclaration = methodDef->GetMethodDeclaration();
	auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration();
	auto typeInstance = methodInstance->GetOwner();

	if (typeInstance->IsInstanceOf(mCompiler->mValueTypeTypeDef))
		return;

	BfTypeState typeState(typeInstance);
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);

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

	if (attributeDirective != NULL)
	{
		if (methodInstance->GetMethodInfoEx()->mMethodCustomAttributes == NULL)
			methodInstance->mMethodInfoEx->mMethodCustomAttributes = new BfMethodCustomAttributes();

		if ((methodInstance == methodInstance->mMethodInstanceGroup->mDefault) && (methodInstance->mMethodInstanceGroup->mDefaultCustomAttributes != NULL))
		{
			// Take over prevoiusly-generated custom attributes
			methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes = methodInstance->mMethodInstanceGroup->mDefaultCustomAttributes;
			methodInstance->mMethodInstanceGroup->mDefaultCustomAttributes = NULL;
		}
		else
			methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes = GetCustomAttributes(attributeDirective, attrTarget);
	}

	if ((propertyMethodDeclaration != NULL) && (propertyMethodDeclaration->mPropertyDeclaration->mAttributes != NULL) && ((attrTarget & BfAttributeTargets_Property) == 0))
	{
		if (methodInstance->GetMethodInfoEx()->mMethodCustomAttributes != NULL)
		{
			GetCustomAttributes(methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes, propertyMethodDeclaration->mPropertyDeclaration->mAttributes, BfAttributeTargets_Property);
		}
		else
		{
			methodInstance->GetMethodInfoEx()->mMethodCustomAttributes = new BfMethodCustomAttributes();
			methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes = GetCustomAttributes(propertyMethodDeclaration->mPropertyDeclaration->mAttributes, BfAttributeTargets_Property);
		}
	}

	customAttributes = methodInstance->GetCustomAttributes();
	if (customAttributes == NULL)
	{
		auto owner = methodInstance->GetOwner();
		if ((owner->IsDelegate()) || (owner->IsFunction()))
			customAttributes = owner->mCustomAttributes;
	}

	methodInstance->mCallingConvention = methodDef->mCallingConvention;
	if (customAttributes != NULL)
	{
		auto linkNameAttr = customAttributes->Get(mCompiler->mCallingConventionAttributeTypeDef);
		if (linkNameAttr != NULL)
		{
			if (linkNameAttr->mCtorArgs.size() == 1)
			{
				auto constant = typeInstance->mConstHolder->GetConstant(linkNameAttr->mCtorArgs[0]);
				if (constant != NULL)
					methodInstance->mCallingConvention = (BfCallingConvention)constant->mInt32;
			}
		}

		if (methodDef->mHasComptime)
			methodInstance->mComptimeFlags = BfComptimeFlag_Comptime;

		auto comptimeAttr = customAttributes->Get(mCompiler->mComptimeAttributeTypeDef);
		if (comptimeAttr != NULL)
		{
			for (auto setProp : comptimeAttr->mSetProperties)
			{
				BfPropertyDef* propertyDef = setProp.mPropertyRef;
				if (propertyDef->mName == "OnlyFromComptime")
				{
					auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
					if ((constant != NULL) && (constant->mBool))
						methodInstance->mComptimeFlags = (BfComptimeFlags)(methodInstance->mComptimeFlags | BfComptimeFlag_OnlyFromComptime);
				}
				else if (propertyDef->mName == "ConstEval")
				{
					auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
					if ((constant != NULL) && (constant->mBool))
						methodInstance->mComptimeFlags = (BfComptimeFlags)(methodInstance->mComptimeFlags | BfComptimeFlag_ConstEval);
				}
			}
		}
	}

	auto delegateInfo = typeInstance->GetDelegateInfo();
	if ((delegateInfo != NULL) && (methodInstance->mMethodDef->mMethodType == BfMethodType_Normal) && (methodInstance->mMethodDef->mName == "Invoke"))
		methodInstance->mCallingConvention = delegateInfo->mCallingConvention;
}

void BfModule::SetupIRFunction(BfMethodInstance* methodInstance, StringImpl& mangledName, bool isTemporaryFunc, bool* outIsIntrinsic)
{
	if (methodInstance->GetImportCallKind() == BfImportCallKind_GlobalVar)
	{
		// Don't create a func
		return;
	}

	auto typeInstance = methodInstance->GetOwner();
	auto methodDef = methodInstance->mMethodDef;

	BfIRFunctionType funcType = mBfIRBuilder->MapMethod(methodInstance);

	if (isTemporaryFunc)
	{
		BF_ASSERT(((mCompiler->mIsResolveOnly) && (mCompiler->mResolvePassData->mAutoComplete != NULL)) ||
			(methodInstance->GetOwner()->mDefineState < BfTypeDefineState_Defined));
		mangledName = "autocomplete_tmp";
	}

	if (!mBfIRBuilder->mIgnoreWrites)
	{
		BfIRFunction prevFunc;

		// Remove old version (if we have one)
		prevFunc = mBfIRBuilder->GetFunction(mangledName);
		if (prevFunc)
		{
			if ((methodDef->mIsOverride) && (mCurTypeInstance->mTypeDef->mIsCombinedPartial))
			{
				bool takeover = false;

				mCurTypeInstance->mTypeDef->PopulateMemberSets();
				BfMethodDef* nextMethod = NULL;
				BfMemberSetEntry* entry = NULL;
				if (mCurTypeInstance->mTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
					nextMethod = (BfMethodDef*)entry->mMemberDef;
				while (nextMethod != NULL)
				{
					auto checkMethod = nextMethod;
					nextMethod = nextMethod->mNextWithSameName;

					if (checkMethod == methodDef)
						continue;
					if (checkMethod->mIsOverride)
					{
						auto checkMethodInstance = mCurTypeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault;
						if (checkMethodInstance == NULL)
							continue;
						if ((checkMethodInstance->mIRFunction == prevFunc) &&
							(checkMethodInstance->mMethodDef->mMethodDeclaration != NULL) &&
							(checkMethodInstance->mVirtualTableIdx < 0))
						{
							BfAstNode* refNode = methodDef->GetRefNode();
							if (auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration())
								refNode = propertyMethodDeclaration->mPropertyDeclaration->mVirtualSpecifier;
							else if (auto methodDeclaration = methodDef->GetMethodDeclaration())
								refNode = methodDeclaration->mVirtualSpecifier;

							auto error = Fail(StrFormat("Conflicting extension override method '%s'", MethodToString(mCurMethodInstance).c_str()), refNode);
							if (error != NULL)
								mCompiler->mPassInstance->MoreInfo("See previous override", checkMethod->GetRefNode());

							takeover = false;
							break;
						}
					}

					if (!checkMethod->mIsExtern)
						continue;
					auto checkMethodInstance = mCurTypeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault;
					if (checkMethodInstance == NULL)
						continue;
					if (!CompareMethodSignatures(checkMethodInstance, mCurMethodInstance))
						continue;
					// Take over function
					takeover = true;
				}

				if (takeover)
					mCurMethodInstance->mIRFunction = prevFunc;

				if (!mCurMethodInstance->mIRFunction)
				{
					BfLogSysM("Function collision from inner override erased prevFunc %p: %d\n", methodInstance, prevFunc.mId);
					if (!mIsComptimeModule)
						mBfIRBuilder->Func_SafeRenameFrom(prevFunc, mangledName);
				}
			}
			else if (methodDef->mIsExtern)
			{
				// Allow this
				BfLogSysM("Function collision allowed multiple extern functions for %p: %d\n", methodInstance, prevFunc.mId);
			}
			else if (typeInstance->IsIRFuncUsed(prevFunc))
			{
				// We can have a collision of names when we have generic methods that differ only in
				//  their constraints, but they should only collide in their unspecialized form
				//  since only one will be chosen for a given concrete type
				if (!mIsComptimeModule)
					mCurMethodInstance->mMangleWithIdx = true;
				mangledName.Clear();
				BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), mCurMethodInstance);
				prevFunc = mBfIRBuilder->GetFunction(mangledName);

				BfLogSysM("Function collision forced mangleWithIdx for %p: %d\n", methodInstance, prevFunc.mId);
			}
			else
			{
				BfLogSysM("Function collision erased prevFunc %p: %d\n", methodInstance, prevFunc.mId);
				if (!mIsComptimeModule)
					mBfIRBuilder->Func_SafeRenameFrom(prevFunc, mangledName);
			}
		}
	}

	if (typeInstance != mContext->mBfObjectType)
	{
		// Only allow attributes on System.Object methods that can be handled inside the DefBuilder
		GetMethodCustomAttributes(methodInstance);
	}

// 	if (prevFunc)
// 	{
// 		if (methodDef->mIsExtern)
// 		{
// 			BfLogSysM("Reusing function for %p: %d\n", methodInstance, prevFunc.mId);
// 			methodInstance->mIRFunction = prevFunc;
// 		}
// 		else
// 		{
// 			if ((methodInstance->IsSpecializedByAutoCompleteMethod()) ||
// 				((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mResolveType >= BfResolveType_GoToDefinition)))
// 			{
// 				// If we're doing re-pass over the methods for something like BfResolveType_ShowFileSymbolReferences, then allow this collision
// 				BfLogSysM("Ignoring function name collision\n");
// 			}
// 			else
// 			{
// 				if (methodDef->mMethodDeclaration == NULL)
// 				{
// 					AssertErrorState();
// 				}
// 				else
// 				{
// 					BF_ASSERT(mIsSpecialModule);
// 					if ((mCompiler->mRevision == 1) && (HasCompiledOutput()))
// 					{
// 						// We shouldn't have name collisions on the first run!
// 						AssertErrorState();
// 					}
// 				}
// 			}
// 			BfLogSysM("Before erase\n");
// 			mBfIRBuilder->Func_EraseFromParent(prevFunc);
// 		}
// 	}

	bool isIntrinsic = false;
	if (!methodInstance->mIRFunction)
	{
		BfIRFunction func;
		bool wantsLLVMFunc = ((!typeInstance->IsUnspecializedType() || (mIsComptimeModule)) &&
			(!methodDef->IsEmptyPartial())) && (funcType);

		/*if (mCurTypeInstance->mTypeDef->mName->ToString() == "ClassA")
		{
		if (!mIsReified)
		wantsLLVMFunc = false;
		}*/

		if (wantsLLVMFunc)
		{
			func = GetIntrinsic(methodInstance, true);
			if (func)
			{
				isIntrinsic = true;
			}
			else
			{
				func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, mangledName);
				BfLogSysM("Creating FuncId:%d %s in module %p\n", func.mId, mangledName.c_str(), this);
				if (methodInstance->mAlwaysInline)
					mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_AlwaysInline);

// 				if (prevFunc)
// 					BfLogSysM("Removing Func %d, replacing with %d\n", prevFunc.mId, func.mId);
			}

			methodInstance->mIRFunction = func;
			//if (!ignoreWrites)
				//BF_ASSERT(!func.IsFake());
		}
	}

	if (outIsIntrinsic != NULL)
		*outIsIntrinsic = isIntrinsic;

	if (!isIntrinsic)
		SetupIRMethod(methodInstance, methodInstance->mIRFunction, methodInstance->mAlwaysInline);
}

void BfModule::CheckHotMethod(BfMethodInstance* methodInstance, const StringImpl& inMangledName)
{
	if (methodInstance->mHotMethod != NULL)
		return;

	if ((mCompiler->mOptions.mAllowHotSwapping) && (!methodInstance->mIsUnspecialized))
	{
		auto srcTypeInst = methodInstance->GetOwner();
		if (srcTypeInst->mHotTypeData == NULL)
			return;

		StringT<128> mangledName = inMangledName;

		if (mangledName.IsEmpty())
			BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), methodInstance);

		// We always keep the current primary method at the same address
		BfHotMethod* hotMethod;
		BfHotMethod** hotMethodPtr;
		if (mCompiler->mHotData->mMethodMap.TryAdd(mangledName, NULL, &hotMethodPtr))
		{
			hotMethod = new BfHotMethod();
			*hotMethodPtr = hotMethod;
			hotMethod->mRefCount = 1;
			hotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_IsBound);
#ifdef BF_DBG_HOTMETHOD_IDX
			static int sMethodIdx = 0;
			hotMethod->mMethodIdx = sMethodIdx++;
#endif

			BfLogSysM("HotMethodData %p created for method %p %s - %s\n", hotMethod, methodInstance, MethodToString(methodInstance).c_str(), mangledName.c_str());
		}
		else
		{
			hotMethod = *hotMethodPtr;
			if ((hotMethod->mFlags & BfHotDepDataFlag_IsBound) != 0)
			{
				// This is a duplicate mangled name - we link to this new entry via a 'BfHotDupMethod'
				auto prevHotMethod = *hotMethodPtr;

				hotMethod = new BfHotMethod();
				hotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_IsBound);

				BfHotDupMethod* hotDupMethod = new BfHotDupMethod(hotMethod);
				hotDupMethod->mRefCount++;
				prevHotMethod->mReferences.Add(hotDupMethod);
				prevHotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_HasDup);

				BfLogSysM("HotMethodData %p (duplicate of %p) created for method %p %s - %s\n", hotMethod, prevHotMethod, methodInstance, MethodToString(methodInstance).c_str(), mangledName.c_str());
			}
			else if (mCompiler->IsHotCompile())
			{
				// Link the previous version into the mPrevVersion
				BfHotMethod* prevMethod = new BfHotMethod();
				prevMethod->mRefCount = 1;
				prevMethod->mPrevVersion = hotMethod->mPrevVersion;
				hotMethod->mPrevVersion = prevMethod;

				prevMethod->mSrcTypeVersion = hotMethod->mSrcTypeVersion;
				hotMethod->mSrcTypeVersion = NULL;
				prevMethod->mFlags = hotMethod->mFlags;
				hotMethod->mFlags = BfHotDepDataFlag_IsBound;
				hotMethod->mReferences.MoveTo(prevMethod->mReferences);
#ifdef BF_DBG_HOTMETHOD_NAME
				prevMethod->mMangledName = hotMethod->mMangledName;
#endif
				BfLogSysM("HotMethodData %p created for prevmethod of %p for method %p %s\n", prevMethod, hotMethod, methodInstance, MethodToString(methodInstance).c_str());
			}
			else
			{
				BfLogSysM("HotMethodData %p used for method %p %s - %s\n", hotMethod, methodInstance, MethodToString(methodInstance).c_str(), mangledName.c_str());
				hotMethod->Clear(true);
				hotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_IsBound);
			}
		}

		if (methodInstance->mIsClosure)
		{
			hotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_RetainMethodWithoutBinding);
		}

		hotMethod->mSrcTypeVersion = srcTypeInst->mHotTypeData->GetLatestVersion();
		hotMethod->mSrcTypeVersion->mRefCount++;
		hotMethod->mRefCount++;
#ifdef BF_DBG_HOTMETHOD_NAME
		hotMethod->mMangledName = mangledName;
#endif
		if ((methodInstance->mMethodInstanceGroup->IsImplemented()) && (!IsHotCompile()))
			hotMethod->mFlags = (BfHotDepDataFlags)(hotMethod->mFlags | BfHotDepDataFlag_IsOriginalBuild);

		methodInstance->mHotMethod = hotMethod;
	}
}

static void StackOverflow()
{
	int i = 0;
	if (i == 0)
		StackOverflow();
}

void BfModule::StartMethodDeclaration(BfMethodInstance* methodInstance, BfMethodState* prevMethodState)
{
	methodInstance->mHasStartedDeclaration = true;
	auto methodDef = methodInstance->mMethodDef;
	auto typeInstance = methodInstance->GetOwner();

	bool hasNonGenericParams = false;
	bool hasGenericParams = false;

	int dependentGenericStartIdx = 0;

	if (methodDef->mIsLocalMethod)
	{
		// If we're a local generic method inside an outer generic method, we can still be considered unspecialized
		//  if our outer method's generic args are specialized but ours are unspecialized. This is because locals get
		//  instantiated uniquely for each specialized or unspecialized pass through the outer method, but the local
		//  method still 'inherits' the outer's generic arguments -- but we still need to make an unspecialized pass
		//  over the local method each time
		auto rootMethodInstance = prevMethodState->GetRootMethodState()->mMethodInstance;
		dependentGenericStartIdx = 0;
		if (rootMethodInstance != NULL)
		{
			if (rootMethodInstance->mMethodInfoEx != NULL)
				dependentGenericStartIdx = (int)rootMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();

			methodInstance->mIsUnspecialized = rootMethodInstance->mIsUnspecialized;
			methodInstance->mIsUnspecializedVariation = rootMethodInstance->mIsUnspecializedVariation;
		}
	}

	for (int genericArgIdx = dependentGenericStartIdx; genericArgIdx < (int)methodInstance->GetNumGenericArguments(); genericArgIdx++)
	{
		auto genericArgType = methodInstance->mMethodInfoEx->mMethodGenericArguments[genericArgIdx];
		if (genericArgType->IsGenericParam())
		{
			hasGenericParams = true;
			auto genericParam = (BfGenericParamType*)genericArgType;
			methodInstance->mIsUnspecialized = true;
			if ((genericParam->mGenericParamKind != BfGenericParamKind_Method) || (genericParam->mGenericParamIdx != genericArgIdx))
				methodInstance->mIsUnspecializedVariation = true;
		}
		else
		{
			hasNonGenericParams = true;
			if (genericArgType->IsUnspecializedType())
			{
				methodInstance->mIsUnspecialized = true;
				methodInstance->mIsUnspecializedVariation = true;
			}
		}
	}

	if ((hasGenericParams) && (hasNonGenericParams))
	{
		methodInstance->mIsUnspecializedVariation = true;
	}

	if (typeInstance->IsUnspecializedType())
	{
		// A specialized method within an unspecialized type is considered an unspecialized variation -- in the sense that we don't
		//  actually want to do method processing on it
		if ((!methodInstance->mIsUnspecialized) && (methodInstance->GetNumGenericArguments() != 0))
			methodInstance->mIsUnspecializedVariation = true;
		methodInstance->mIsUnspecialized = true;
	}

	if (methodInstance->mIsUnspecializedVariation)
		BF_ASSERT(methodInstance->mIsUnspecialized);

	//TODO: Why did we do this?
	if (methodDef->mMethodType == BfMethodType_Mixin)
	{
		if (methodInstance->IsSpecializedGenericMethod())
			methodInstance->mIsUnspecializedVariation = true;
		methodInstance->mIsUnspecialized = true;
	}
}

// methodDeclaration is NULL for default constructors
void BfModule::DoMethodDeclaration(BfMethodDeclaration* methodDeclaration, bool isTemporaryFunc, bool addToWorkList)
{
	BF_ASSERT((mCompiler->mCeMachine == NULL) || (!mCompiler->mCeMachine->mDbgPaused));

	BP_ZONE("BfModule::DoMethodDeclaration");

	// We could trigger a DoMethodDeclaration from a const resolver or other location, so we reset it here
	//  to effectively make mIgnoreWrites method-scoped
	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mWantsIRIgnoreWrites || mCurMethodInstance->mIsUnspecialized || mCurTypeInstance->mResolvingVarField);
	SetAndRestoreValue<bool> prevIsCapturingMethodMatchInfo;
	SetAndRestoreValue<bool> prevAllowLockYield(mContext->mAllowLockYield, false);
	BfTypeState typeState(mCurTypeInstance);
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

	if (mCompiler->IsAutocomplete())
		prevIsCapturingMethodMatchInfo.Init(mCompiler->mResolvePassData->mAutoComplete->mIsCapturingMethodMatchInfo, false);

	if (mCurMethodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference)
		mCurMethodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingReference;

	BfMethodState methodState;
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
	methodState.mTempKind = BfMethodState::TempKind_Static;

	defer({ mCurMethodInstance->mHasBeenDeclared = true; });

	// If we are doing this then we may end up creating methods when var types are unknown still, failing on splat/zero-sized info
	BF_ASSERT((!mCurTypeInstance->mResolvingVarField) || (mBfIRBuilder->mIgnoreWrites));

	bool ignoreWrites = mBfIRBuilder->mIgnoreWrites;

//  	if ((!isTemporaryFunc) && (mCurTypeInstance->mDefineState < BfTypeDefineState_Defined))
//  	{
// 		BF_ASSERT(mContext->mResolvingVarField);
// 		isTemporaryFunc = true;
//  	}

	if ((mAwaitingInitFinish) && (!mBfIRBuilder->mIgnoreWrites))
		FinishInit();

	auto typeInstance = mCurTypeInstance;
	auto typeDef = typeInstance->mTypeDef;
	auto methodDef = mCurMethodInstance->mMethodDef;

	BF_ASSERT(methodDef->mName != "__ASSERTNAME");
	if (methodDef->mName == "__FATALERRORNAME")
		BFMODULE_FATAL(this, "__FATALERRORNAME");
	if (methodDef->mName == "__STACKOVERFLOW")
		StackOverflow();

	if (typeInstance->IsClosure())
	{
		if (methodDef->mName == "Invoke")
			return;
	}

	auto methodInstance = mCurMethodInstance;

	if ((methodInstance->IsSpecializedByAutoCompleteMethod()) || (mCurTypeInstance->IsFunction()))
		addToWorkList = false;

	if (!methodInstance->mHasStartedDeclaration)
		StartMethodDeclaration(methodInstance, prevMethodState.mPrevVal);

	BfAutoComplete* bfAutocomplete = NULL;
	if (mCompiler->mResolvePassData != NULL)
		bfAutocomplete = mCompiler->mResolvePassData->mAutoComplete;

	if (methodInstance->mMethodInfoEx != NULL)
	{
		BfTypeInstance* unspecializedTypeInstance = NULL;

		Array<BfTypeReference*> deferredResolveTypes;
		for (int genericParamIdx = 0; genericParamIdx < (int)methodInstance->mMethodInfoEx->mGenericParams.size(); genericParamIdx++)
		{
			auto genericParam = methodInstance->mMethodInfoEx->mGenericParams[genericParamIdx];
			if (genericParamIdx < (int)methodDef->mGenericParams.size())
			{
				genericParam->mExternType = GetGenericParamType(BfGenericParamKind_Method, genericParamIdx);
			}
			else
			{
				auto externConstraintDef = genericParam->GetExternConstraintDef();
				genericParam->mExternType = ResolveTypeRef(externConstraintDef->mTypeRef);

				auto autoComplete = mCompiler->GetAutoComplete();
				if (autoComplete != NULL)
					autoComplete->CheckTypeRef(externConstraintDef->mTypeRef, false);

				if (genericParam->mExternType != NULL)
				{
					//
				}
				else
					genericParam->mExternType = GetPrimitiveType(BfTypeCode_Var);
			}

			ResolveGenericParamConstraints(genericParam, methodInstance->mIsUnspecialized, &deferredResolveTypes);

			if (genericParamIdx < (int)methodDef->mGenericParams.size())
			{
				auto genericParamDef = methodDef->mGenericParams[genericParamIdx];
				if (bfAutocomplete != NULL)
				{
					for (auto nameNode : genericParamDef->mNameNodes)
					{
						HandleMethodGenericParamRef(nameNode, typeDef, methodDef, genericParamIdx);
					}
				}
			}
		}
		for (auto typeRef : deferredResolveTypes)
			auto constraintType = ResolveTypeRef(typeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_None);

		if (methodInstance->mMethodInfoEx != NULL)
		{
			ValidateGenericParams(BfGenericParamKind_Method,
				Span<BfGenericParamInstance*>((BfGenericParamInstance**)methodInstance->mMethodInfoEx->mGenericParams.mVals,
					methodInstance->mMethodInfoEx->mGenericParams.mSize));
		}

		for (auto genericParam : methodInstance->mMethodInfoEx->mGenericParams)
			AddDependency(genericParam, mCurTypeInstance);
	}

	if ((methodInstance->mIsAutocompleteMethod) && (methodDeclaration != NULL) && (!methodInstance->IsSpecializedGenericMethod()) && (methodDef->mIdx >= 0))
	{
		auto autoComplete = mCompiler->mResolvePassData->mAutoComplete;
		BfAstNode* nameNode = methodDeclaration->mNameNode;
		if (nameNode == NULL)
		{
			if (auto ctorDeclaration = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
				nameNode = ctorDeclaration->mThisToken;
		}

		if ((autoComplete->IsAutocompleteNode(nameNode)) && (autoComplete->mResolveType != BfResolveType_Autocomplete))
		{
			autoComplete->mInsertStartIdx = nameNode->GetSrcStart();
			autoComplete->mInsertEndIdx = nameNode->GetSrcEnd();
			autoComplete->mDefType = typeDef;
			autoComplete->mDefMethod = methodDef;
			autoComplete->SetDefinitionLocation(nameNode);

			if (methodDef->mIsOverride)
			{
				bool found = false;

				for (int virtIdx = 0; virtIdx < (int)typeInstance->mVirtualMethodTable.size(); virtIdx++)
				{
					auto& ventry = typeInstance->mVirtualMethodTable[virtIdx];
					if (ventry.mDeclaringMethod.mMethodNum == -1)
						continue;
					BfMethodInstance* virtMethod = ventry.mImplementingMethod;
					if (virtMethod != NULL)
					{
						auto virtMethodDeclaration = virtMethod->mMethodDef->GetMethodDeclaration();
						if ((virtMethod->GetOwner() == typeInstance) && (virtMethodDeclaration != NULL) &&
							(virtMethodDeclaration->GetSrcStart() == methodDeclaration->GetSrcStart()))
						{
							// Is matching method
							if (virtIdx < (int)typeInstance->mBaseType->mVirtualMethodTable.size())
							{
								auto baseType = typeInstance->mBaseType;
								if (baseType != NULL)
								{
									found = true;
									while ((baseType->mBaseType != NULL) && (virtIdx < baseType->mBaseType->mVirtualMethodTableSize))
										baseType = baseType->mBaseType;
									BfMethodInstance* baseVirtMethod = baseType->mVirtualMethodTable[virtIdx].mImplementingMethod;
									autoComplete->SetDefinitionLocation(baseVirtMethod->mMethodDef->GetRefNode(), true);
									autoComplete->mDefType = baseType->mTypeDef;
									autoComplete->mDefMethod = baseVirtMethod->mMethodDef;
								}
							}

							break;
						}
					}
				}

				if ((!found) && (autoComplete->mIsGetDefinition) && (methodDef->mDeclaringType->IsExtension()))
				{
					for (auto& methodGroup : typeInstance->mMethodInstanceGroups)
					{
						auto defaultMethod = methodGroup.mDefault;
						if (defaultMethod == NULL)
							continue;
						if (!defaultMethod->mMethodDef->mIsExtern)
							continue;
						if (!CompareMethodSignatures(defaultMethod, methodInstance))
							continue;

						autoComplete->SetDefinitionLocation(defaultMethod->mMethodDef->GetRefNode(), true);
						autoComplete->mDefType = typeInstance->mTypeDef;
						autoComplete->mDefMethod = defaultMethod->mMethodDef;
					}
				}
			}
		}
	}

	bool reportErrors = true;
	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		reportErrors = true;

	// Only the defining contexts needs to report errors here
	SetAndRestoreValue<bool> prevReporErrors(mReportErrors, reportErrors);

	if (methodDef->mMethodType == BfMethodType_Dtor)
	{
		if (methodDef->mIsStatic)
		{
		}
		else
		{
			if ((typeInstance->IsStruct()) || (typeInstance->IsTypedPrimitive()))
			{
				BfAstNode* refNode = methodDeclaration;
				if (refNode == NULL)
				{
					// Whatever caused this ctor to be generated should have caused another failure
					//  But that failure might not be generated until the ctor is generated
				}
				else
				{
					auto dtorDecl = BfNodeDynCast<BfDestructorDeclaration>(methodDeclaration);
					if (dtorDecl != NULL)
						refNode = dtorDecl->mThisToken;
					Fail("Structs cannot have destructors", refNode);
				}
			}
			auto methodDeclaration = methodDef->GetMethodDeclaration();
			if ((methodDeclaration != NULL) && (methodDeclaration->mVirtualSpecifier != NULL) &&
				(mCurTypeInstance != mContext->mBfObjectType))
			{
				Fail("Virtual specifier is not required, all destructors are implicitly virtual.", methodDeclaration->mVirtualSpecifier);
			}
		}
	}

	BfType* resolvedReturnType = NULL;
	if (((methodDef->mMethodType == BfMethodType_Normal) || (methodDef->mMethodType == BfMethodType_Extension) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend) ||
		 (methodDef->mMethodType == BfMethodType_PropertyGetter) || (methodDef->mMethodType == BfMethodType_Operator)) &&
		(methodDef->mReturnTypeRef != NULL))
	{
		SetAndRestoreValue<bool> prevIngoreErrors(mIgnoreErrors, mIgnoreErrors || (methodDef->GetPropertyDeclaration() != NULL));

		BfResolveTypeRefFlags flags = (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef | BfResolveTypeRefFlag_AllowRefGeneric);

		if ((((methodInstance->mComptimeFlags & BfComptimeFlag_ConstEval) != 0) || (methodInstance->mIsAutocompleteMethod))
			&& (methodDef->mReturnTypeRef->IsA<BfVarTypeReference>()))
			resolvedReturnType = GetPrimitiveType(BfTypeCode_Var);
		else
			resolvedReturnType = ResolveTypeRef(methodDef->mReturnTypeRef, BfPopulateType_Declaration, flags);

		if (resolvedReturnType == NULL)
			resolvedReturnType = GetPrimitiveType(BfTypeCode_Var);

		if ((methodDef->mIsReadOnly) && (!resolvedReturnType->IsRef()))
		{
			if (auto methodDeclaration = BfNodeDynCast<BfMethodDeclaration>(methodInstance->mMethodDef->mMethodDeclaration))
				if (methodDeclaration->mReadOnlySpecifier != NULL)
					Fail("Readonly specifier is only valid on 'ref' return types", methodDeclaration->mReadOnlySpecifier);
		}
	}
	else
	{
		resolvedReturnType = ResolveTypeDef(mSystem->mTypeVoid);
	}

	BF_ASSERT(resolvedReturnType != NULL);
	mCurMethodInstance->mReturnType = resolvedReturnType;

	//TODO: We used to NOT add the return value dependency for specialized methods, but when we have types that are
	//  specialized based on the method's generic param then they can get deleted if no one else has referred to them (yet)
	//if (!methodInstance->IsSpecializedGenericMethod())

	AddDependency(resolvedReturnType, typeInstance, BfDependencyMap::DependencyFlag_ParamOrReturnValue);

	if (methodDef->mExplicitInterface != NULL)
	{
		auto autoComplete = mCompiler->GetAutoComplete();

		if (autoComplete != NULL)
			autoComplete->CheckTypeRef(methodDef->mExplicitInterface, false);
		auto explicitType = ResolveTypeRef(methodDef->mExplicitInterface, BfPopulateType_Declaration);
		BfTypeInstance* explicitInterface = NULL;
		if (explicitType != NULL)
			explicitInterface = explicitType->ToTypeInstance();
		if (explicitInterface != NULL)
		{
			mCurMethodInstance->GetMethodInfoEx()->mExplicitInterface = explicitInterface->ToTypeInstance();
			if (autoComplete != NULL)
			{
				BfTokenNode* dotToken = NULL;
				BfAstNode* nameNode = NULL;
				if (auto methodDeclaration = BfNodeDynCast<BfMethodDeclaration>(methodDef->mMethodDeclaration))
				{
					dotToken = methodDeclaration->mExplicitInterfaceDotToken;
					nameNode = methodDeclaration->mNameNode;
				}

				autoComplete->CheckExplicitInterface(explicitInterface, dotToken, nameNode);
			}
		}
		if ((mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mExplicitInterface != NULL))
		{
			bool interfaceFound = false;
			for (auto ifaceInst : typeInstance->mInterfaces)
				interfaceFound |= ifaceInst.mInterfaceType == mCurMethodInstance->mMethodInfoEx->mExplicitInterface;
			if ((!interfaceFound) && (!typeInstance->mTypeFailed))
			{
				if (methodDef->mMethodDeclaration != NULL)
					Fail("Containing class has not declared to implement this interface", methodDef->mMethodDeclaration);
				else
				{
					// For property decls, we should have already given the error during type population
					if (mCompiler->mRevision == 1)
						AssertErrorState();
				}
			}
		}
	}

	bool isThisStruct = mCurTypeInstance->IsStruct();
	BfType* thisType = NULL;

	if ((!methodDef->mIsStatic) && (!mCurTypeInstance->IsValuelessType()))
	{
		if ((isThisStruct) || (mCurTypeInstance->IsTypedPrimitive()))
		{
			thisType = mCurTypeInstance;
			if (thisType == NULL)
				return;

			if ((thisType->IsSplattable()) && (!methodDef->HasNoThisSplat()))
			{
                BfTypeUtils::SplatIterate([&](BfType* checkType)
					{
						PopulateType(checkType, BfPopulateType_Data);
					}, thisType);
			}
		}
		else
		{
			thisType = mCurTypeInstance;
			PopulateType(thisType, BfPopulateType_Declaration);
		}
	}

	int implicitParamCount = 0;
	if ((mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mClosureInstanceInfo != NULL))
		implicitParamCount = (int)methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries.size();

	methodInstance->mMethodDef->mParams.Reserve((int)methodDef->mParams.size());

	bool hadDelegateParams = false;
	bool hadParams = false;
	for (int paramIdx = 0; paramIdx < (int)methodDef->mParams.size() + implicitParamCount; paramIdx++)
	{
		BfClosureCapturedEntry* closureCaptureEntry = NULL;
		BfParameterDef* paramDef = NULL;
		int paramDefIdx = -1;

		if (paramIdx < implicitParamCount)
			closureCaptureEntry = &methodInstance->mMethodInfoEx->mClosureInstanceInfo->mCaptureEntries[paramIdx];
		else
		{
			paramDefIdx = paramIdx - implicitParamCount;
			paramDef = methodDef->mParams[paramDefIdx];
		}

		if (hadParams)
			break;

		if ((paramDef != NULL) && (paramDef->mParamKind == BfParamKind_VarArgs))
			continue;

		if ((paramDef != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) &&
			(paramDef->mParamKind != BfParamKind_AppendIdx))
		{
			mCompiler->mResolvePassData->mAutoComplete->CheckTypeRef(paramDef->mTypeRef, false);
			if (mCompiler->mResolvePassData->mAutoComplete->IsAutocompleteNode(paramDef->mTypeRef))
				mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("token", "in"), paramDef->mTypeRef->ToString());
		}

		if ((paramDef != NULL) && (paramDef->mParamDeclaration != NULL) && (paramDef->mParamDeclaration->mAttributes != NULL))
		{
			if (methodInstance->GetMethodInfoEx()->mMethodCustomAttributes == NULL)
				methodInstance->mMethodInfoEx->mMethodCustomAttributes = new BfMethodCustomAttributes();
			while ((int)methodInstance->mMethodInfoEx->mMethodCustomAttributes->mParamCustomAttributes.size() < paramIdx)
				methodInstance->mMethodInfoEx->mMethodCustomAttributes->mParamCustomAttributes.push_back(NULL);

			auto customAttributes = GetCustomAttributes(paramDef->mParamDeclaration->mAttributes, BfAttributeTargets_Parameter);
			methodInstance->mMethodInfoEx->mMethodCustomAttributes->mParamCustomAttributes.push_back(customAttributes);
		}

		BfType* resolvedParamType = NULL;

		if (closureCaptureEntry != NULL)
			resolvedParamType = closureCaptureEntry->mType;
		else if ((paramDef->mTypeRef != NULL) && (paramDef->mTypeRef->IsA<BfLetTypeReference>()))
		{
			Fail("Cannot declare 'let' parameters", paramDef->mTypeRef);
			resolvedParamType = mContext->mBfObjectType;
		}
		else if ((paramDef->mTypeRef != NULL) && (paramDef->mTypeRef->IsA<BfVarTypeReference>()))
		{
			if (methodDef->mMethodType != BfMethodType_Mixin)
			{
				Fail("Cannot declare var parameters", paramDef->mTypeRef);
				resolvedParamType = mContext->mBfObjectType;
			}
			else
				resolvedParamType = GetPrimitiveType(BfTypeCode_Var);
		}

		BfType* unresolvedParamType = resolvedParamType;
		bool wasGenericParam = false;
		if (resolvedParamType == NULL)
		{
			BfResolveTypeRefFlags resolveFlags = (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef | BfResolveTypeRefFlag_AllowRefGeneric | BfResolveTypeRefFlag_AllowGenericMethodParamConstValue);
			if (paramDef->mParamKind == BfParamKind_ExplicitThis)
				resolveFlags = (BfResolveTypeRefFlags)(resolveFlags | BfResolveTypeRefFlag_NoWarnOnMut);
			resolvedParamType = ResolveTypeRef(paramDef->mTypeRef, BfPopulateType_Declaration, resolveFlags);
			if ((paramDef->mParamKind == BfParamKind_ExplicitThis) && (resolvedParamType != NULL) && (resolvedParamType->IsRef()))
				resolvedParamType = resolvedParamType->GetUnderlyingType();
		}
		if (resolvedParamType == NULL)
		{
			resolvedParamType = GetPrimitiveType(BfTypeCode_Var);
			unresolvedParamType = resolvedParamType;
			if (mCurTypeInstance->IsBoxed())
			{
				auto boxedType = (BfBoxedType*)mCurTypeInstance;
				// If we failed a lookup here then we better have also failed it in the original type
				BF_ASSERT(boxedType->mElementType->ToTypeInstance()->mModule->mHadBuildError || mContext->mFailTypes.ContainsKey(boxedType->mElementType->ToTypeInstance()));
			}
		}

		BF_ASSERT(!resolvedParamType->IsDeleting());

		if (!methodInstance->IsSpecializedGenericMethod())
			AddDependency(resolvedParamType, typeInstance, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
		PopulateType(resolvedParamType, BfPopulateType_Declaration);

		AddDependency(resolvedParamType, mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

		if ((paramDef != NULL) && (paramDef->mParamDeclaration != NULL) && (paramDef->mParamDeclaration->mInitializer != NULL) &&
			(!paramDef->mParamDeclaration->mInitializer->IsA<BfBlock>()))
		{
			if (paramDef->mParamKind == BfParamKind_Params)
			{
				BfAstNode* refNode = paramDef->mParamDeclaration->mEqualsNode;
				if (refNode != NULL)
					refNode = paramDef->mParamDeclaration->mModToken;
				Fail("Cannot specify a default value for a 'params' parameter", refNode);
			}

			BfTypedValue defaultValue;
			if (resolvedParamType->IsConstExprValue())
			{
				auto constExprType = (BfConstExprValueType*)resolvedParamType;

				BfExprEvaluator exprEvaluator(this);
				exprEvaluator.mExpectingType = constExprType->mType;
				exprEvaluator.GetLiteral(NULL, constExprType->mValue);
				defaultValue = exprEvaluator.GetResult();
			}
			else
			{
				BfTypeState typeState;
				typeState.mType = mCurTypeInstance;
				typeState.mCurTypeDef = methodDef->mDeclaringType;
				//typeState.mCurMethodDef = methodDef;
				SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

				BfConstResolver constResolver(this);
				defaultValue = constResolver.Resolve(paramDef->mParamDeclaration->mInitializer, resolvedParamType, (BfConstResolveFlags)(BfConstResolveFlag_NoCast | BfConstResolveFlag_AllowGlobalVariable));
				if ((defaultValue) && (defaultValue.mType != resolvedParamType))
				{
					SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
					auto castedDefaultValue = Cast(paramDef->mParamDeclaration->mInitializer, defaultValue, resolvedParamType, (BfCastFlags)(BfCastFlags_SilentFail | BfCastFlags_NoConversionOperator));
					if ((castedDefaultValue) && (castedDefaultValue.mValue.IsConst()))
					{
						defaultValue = castedDefaultValue; // Good!
					}
					else if (!CanCast(defaultValue, resolvedParamType))
					{
						// We only care that we get a constant value that can be implicitly casted at the callsite- even if that requires
						//  a conversion operator.

						// This should throw an error
						defaultValue = Cast(paramDef->mParamDeclaration->mInitializer, defaultValue, resolvedParamType);
						AssertErrorState();
						if (!defaultValue.mValue.IsConst())
							defaultValue = BfTypedValue();
					}
				}
			}

			if (!defaultValue)
			{
				defaultValue = GetDefaultTypedValue(resolvedParamType);
				if (!defaultValue.mValue.IsConst())
					defaultValue = BfTypedValue();
			}

			if (defaultValue)
			{
				if (defaultValue.mType->IsVar())
				{
					AssertErrorState();
				}
				else
				{
					BF_ASSERT(defaultValue.mValue.IsConst());
					while ((int)mCurMethodInstance->mDefaultValues.size() < paramDefIdx)
						mCurMethodInstance->mDefaultValues.Add(BfTypedValue());

					CurrentAddToConstHolder(defaultValue.mValue);
					mCurMethodInstance->mDefaultValues.Add(defaultValue);
				}
			}
		}

		if ((paramDef != NULL) && (paramDef->mParamKind == BfParamKind_Params))
		{
			bool addParams = true;
			bool isValid = false;

			auto resolvedParamTypeInst = resolvedParamType->ToTypeInstance();

			if ((resolvedParamTypeInst != NULL) && (resolvedParamTypeInst->IsInstanceOf(mCompiler->mSpanTypeDef)))
			{
				// Span<T>
				isValid = true;
			}
			else if (resolvedParamType->IsArray())
			{
				// Array is the 'normal' params type
				isValid = true;
			}
			else if (resolvedParamType->IsSizedArray())
			{
				isValid = true;
			}
			else if (resolvedParamType->IsVar())
			{
				isValid = true;
			}
			else if ((resolvedParamType->IsDelegate()) || (resolvedParamType->IsFunction()) || (resolvedParamType->IsMethodRef()))
			{
				hadDelegateParams = true;

				// This means we copy the params from a delegate
				BfMethodParam methodParam;
				methodParam.mResolvedType = resolvedParamType;
				methodParam.mParamDefIdx = paramDefIdx;
				methodParam.mDelegateParamIdx = 0;
				auto invokeMethodInstance = methodParam.GetDelegateParamInvoke();
				for (int delegateParamIdx = 0; delegateParamIdx < invokeMethodInstance->GetParamCount(); delegateParamIdx++)
				{
					methodParam.mDelegateParamIdx = delegateParamIdx;
					mCurMethodInstance->mParams.Add(methodParam);

					auto paramType = invokeMethodInstance->GetParamType(delegateParamIdx);
					if (!methodInstance->IsSpecializedGenericMethod())
						AddDependency(paramType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ParamOrReturnValue);
				}
				isValid = true;
				addParams = false;
			}
			else if (resolvedParamType->IsGenericParam())
			{
				auto genericParamInstance = GetGenericParamInstance((BfGenericParamType*)resolvedParamType);
				if (genericParamInstance->mTypeConstraint != NULL)
				{
					auto typeInstConstraint = genericParamInstance->mTypeConstraint->ToTypeInstance();
					if ((genericParamInstance->mTypeConstraint->IsArray()) || (genericParamInstance->mTypeConstraint->IsSizedArray()))
					{
						BfMethodParam methodParam;
						methodParam.mResolvedType = resolvedParamType;
						methodParam.mParamDefIdx = paramDefIdx;
						mCurMethodInstance->mParams.Add(methodParam);
						isValid = true;
					}
					else if ((genericParamInstance->mTypeConstraint->IsDelegate()) || (genericParamInstance->mTypeConstraint->IsFunction()) ||
						((genericParamInstance != NULL) && (typeInstConstraint != NULL) &&
						 ((typeInstConstraint->IsInstanceOf(mCompiler->mDelegateTypeDef)) || (typeInstConstraint->IsInstanceOf(mCompiler->mFunctionTypeDef)))))
					{
						mCurMethodInstance->mHadGenericDelegateParams = true;
						isValid = true;
						addParams = false;
					}
				}
			}
			else
			{
				auto paramTypeInst = resolvedParamType->ToTypeInstance();
				if ((paramTypeInst != NULL) &&
					((paramTypeInst->IsInstanceOf(mCompiler->mDelegateTypeDef)) || (paramTypeInst->IsInstanceOf(mCompiler->mFunctionTypeDef))))
				{
					// If we have a 'params T' and 'T' gets specialized with actually 'Delegate' or 'Function' then just ignore it
					isValid = true;
					addParams = false;
				}
			}

			if (!isValid)
			{
				Fail("Parameters with 'params' specifiers can only be used for array, span, delegate, or function types", paramDef->mParamDeclaration->mModToken);
				// Failure case, make it an Object[]
				resolvedParamType = CreateArrayType(mContext->mBfObjectType, 1);
			}

			if ((addParams) && (resolvedParamType != NULL))
			{
				BfMethodParam methodParam;
				methodParam.mResolvedType = resolvedParamType;
				methodParam.mParamDefIdx = paramDefIdx;
				mCurMethodInstance->mParams.push_back(methodParam);
			}

			if (paramDefIdx < (int)methodDef->mParams.size() - 1)
			{
				Fail("Only the last parameter can specify 'params'", paramDef->mParamDeclaration->mModToken);
			}
		}
		else
		{
			BfMethodParam methodParam;
			methodParam.mResolvedType = resolvedParamType;
			methodParam.mParamDefIdx = paramDefIdx;
			mCurMethodInstance->mParams.Add(methodParam);
		}
	}

	if (hadDelegateParams)
	{
		HashSet<String> usedNames;
		Dictionary<int, int> usedParamDefIdx;
		for (auto methodParam : methodDef->mParams)
		{
			usedNames.Add(methodParam->mName);
		}

		for (auto& methodParam : mCurMethodInstance->mParams)
		{
			if ((methodParam.mParamDefIdx != -1) && (methodParam.mDelegateParamIdx == 0))
			{
				int* refCount = NULL;
				usedParamDefIdx.TryAdd(methodParam.mParamDefIdx, NULL, &refCount);
				(*refCount)++;
			}
		}

		for (auto& methodParam : mCurMethodInstance->mParams)
		{
			if (methodParam.mDelegateParamIdx != -1)
			{
				if (usedParamDefIdx[methodParam.mParamDefIdx] > 1)
					methodParam.mDelegateParamNameCombine = true;
				BfMethodInstance* invokeMethodInstance = methodParam.GetDelegateParamInvoke();
				String paramName = invokeMethodInstance->GetParamName(methodParam.mDelegateParamIdx);
				if (usedNames.Contains(paramName))
					methodParam.mDelegateParamNameCombine = true;
			}
		}
	}

	int argIdx = 0;
	PopulateType(methodInstance->mReturnType, BfPopulateType_Data);
	if ((!methodDef->mIsStatic) && (!methodDef->mHasExplicitThis))
    {
		int thisIdx = methodDef->mHasExplicitThis ? 0 : -1;
		auto thisType = methodInstance->GetOwner();
		if (methodInstance->GetParamIsSplat(thisIdx))
			argIdx += methodInstance->GetParamType(thisIdx)->GetSplatCount();
		else if (!thisType->IsValuelessType())
		{
			BfTypeCode loweredTypeCode = BfTypeCode_None;
			BfTypeCode loweredTypeCode2 = BfTypeCode_None;
			if ((!mIsComptimeModule) && (!methodDef->mIsMutating))
				thisType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2);
			argIdx++;
			if (loweredTypeCode2 != BfTypeCode_None)
				argIdx++;
		}
	}

	if ((!mIsComptimeModule) && (methodInstance->GetStructRetIdx() != -1))
		argIdx++;

	for (int paramIdx = 0; paramIdx < mCurMethodInstance->mParams.size(); paramIdx++)
	{
		auto& methodParam = mCurMethodInstance->mParams[paramIdx];

		BfType* checkType = methodParam.mResolvedType;
		int checkArgIdx = argIdx;
		if ((paramIdx == 0) && (methodDef->mHasExplicitThis))
		{
			checkArgIdx = 0;
			checkType = methodInstance->GetThisType();
		}

		if (checkType->IsMethodRef())
		{
			methodParam.mIsSplat = true;
		}
		else if ((checkType->IsComposite()) && (methodInstance->AllowsSplatting(paramIdx)))
		{
			PopulateType(checkType, BfPopulateType_Data);
			if (checkType->IsSplattable())
			{
				bool isSplat = false;
				auto checkTypeInstance = checkType->ToTypeInstance();
				if ((checkTypeInstance != NULL) && (checkTypeInstance->mIsCRepr))
					isSplat = true;
				int splatCount = checkType->GetSplatCount();
				if (checkArgIdx + splatCount <= mCompiler->mOptions.mMaxSplatRegs)
					isSplat = true;
				if (isSplat)
				{
					methodParam.mIsSplat = true;
					argIdx += splatCount;
					continue;
				}
			}
			else if (!checkType->IsValuelessType())
			{
				BfTypeCode loweredTypeCode = BfTypeCode_None;
				BfTypeCode loweredTypeCode2 = BfTypeCode_None;
				if (!mIsComptimeModule)
					checkType->GetLoweredType(BfTypeUsage_Parameter, &loweredTypeCode, &loweredTypeCode2);
				argIdx++;
				if (loweredTypeCode2 != BfTypeCode_None)
					argIdx++;
				continue;
			}
		}

		argIdx++;
	}

	bool hasExternSpecifier = (methodDeclaration != NULL) && (methodDeclaration->mExternSpecifier != NULL);
	if ((hasExternSpecifier) && (methodDeclaration->mBody != NULL))
	{
		Fail("Cannot 'extern' and declare a body", methodDeclaration->mExternSpecifier);
	}
	else if ((methodDeclaration != NULL) && (methodDeclaration->mBody == NULL) && (!hasExternSpecifier) &&
		(!typeInstance->IsInterface()) && (!mCurTypeInstance->IsDelegate()) && (!mCurTypeInstance->IsFunction()) &&
		(!methodDef->mIsPartial) && (!methodDef->mIsAbstract) &&
		(!methodDef->mIsSkipCall))
	{
		if (methodDeclaration->mEndSemicolon == NULL)
		{
			if (auto autoCtorDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(methodDeclaration))
			{
				//
			}
			else
				AssertParseErrorState();
		}
		else
		{
			bool isError = true;

			if (typeInstance->IsTypedPrimitive())
			{
				if (auto operatorDeclaration = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
				{
					if ((operatorDeclaration->mIsConvOperator) && (methodInstance->mParams.size() == 1))
					{
						if (((methodInstance->mReturnType == typeInstance) && (methodInstance->GetParamType(0) == typeInstance->GetUnderlyingType())) ||
							((methodInstance->mReturnType == typeInstance->GetUnderlyingType()) && (methodInstance->GetParamType(0) == typeInstance)))
						{
							isError = false;
						}
					}
				}
			}

			if (isError)
			{
				Fail(StrFormat("'%s' must declare a body because it is not marked abstract, extern, or partial",
					MethodToString(methodInstance).c_str()),
					methodDef->GetRefNode());
			}
		}
	}
	if (methodDef->IsEmptyPartial())
		hasExternSpecifier = true;

	if (!methodInstance->IsAutocompleteMethod())
	{
		auto defaultMethodInstance = methodInstance->mMethodInstanceGroup->mDefault;

		int implicitParamCount = methodInstance->GetImplicitParamCount();
		int defaultImplicitParamCount = defaultMethodInstance->GetImplicitParamCount();

		int actualParamCount = (int)methodInstance->mParams.size() - implicitParamCount;

		BF_ASSERT((actualParamCount == defaultMethodInstance->mParams.size() - defaultImplicitParamCount) || (defaultMethodInstance->mHadGenericDelegateParams));
		mCurMethodInstance->mHadGenericDelegateParams = defaultMethodInstance->mHadGenericDelegateParams;

		int paramIdx = 0;
		int defaultParamIdx = 0;

		while (true)
		{
			bool isDone = paramIdx + implicitParamCount >= (int)methodInstance->mParams.size();
			bool isDefaultDone = defaultParamIdx + defaultImplicitParamCount >= (int)defaultMethodInstance->mParams.size();

			if ((!isDone) && (methodInstance->mParams[paramIdx + implicitParamCount].mDelegateParamIdx >= 0))
			{
				paramIdx++;
				continue;
			}

			if ((!isDefaultDone) && (defaultMethodInstance->mParams[defaultParamIdx + defaultImplicitParamCount].mDelegateParamIdx >= 0))
			{
				defaultParamIdx++;
				continue;
			}

			if ((isDone) || (isDefaultDone))
			{
				// If we have generic delegate params, it's possible we will fail constraints later if we specialize with an invalid type, but we can't allow that
				//  to cause us to throw an assertion in the declaration here
				if ((!defaultMethodInstance->mHadGenericDelegateParams) && (!methodInstance->mHasFailed) && (!defaultMethodInstance->mHasFailed))
					BF_ASSERT((isDone) && (isDefaultDone));

				break;
			}

			BfType* paramType = defaultMethodInstance->mParams[defaultParamIdx + defaultImplicitParamCount].mResolvedType;
			if (paramType->IsRef())
				paramType = paramType->GetUnderlyingType();

			methodInstance->mParams[paramIdx + implicitParamCount].mWasGenericParam = paramType->IsGenericParam();
			paramIdx++;
			defaultParamIdx++;
		}
	}

	StringT<4096> mangledName;
	BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), mCurMethodInstance);

	for (int paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
	{
		auto paramType = methodInstance->GetParamType(paramIdx);
		if (paramType->IsComposite())
			PopulateType(paramType, BfPopulateType_Data);

		if (!methodInstance->IsParamSkipped(paramIdx))
		{
			if (paramType->IsStruct())
			{
				//
			}
			else
			{
				PopulateType(paramType, BfPopulateType_Declaration);
			}
		}
	}

	// Only process method in default unspecialized mode, not in variations
	if (methodInstance->mIsUnspecializedVariation)
		return;

	PopulateType(resolvedReturnType, BfPopulateType_Data);
	auto retLLVMType = mBfIRBuilder->MapType(resolvedReturnType);
	if (resolvedReturnType->IsValuelessType())
		retLLVMType = mBfIRBuilder->GetPrimitiveType(BfTypeCode_None);

	if (!mBfIRBuilder->mIgnoreWrites)
	{
		bool isIntrinsic = false;
		if (!methodInstance->mIRFunction)
			SetupIRFunction(methodInstance, mangledName, isTemporaryFunc, &isIntrinsic);
		if (isIntrinsic)
		{
			addToWorkList = false;
		}
	}
	else
	{
		GetMethodCustomAttributes(methodInstance);
	}

	auto func = methodInstance->mIRFunction;

// 	if (methodInstance->mIsReified)
// 		CheckHotMethod(methodInstance, mangledName);

	for (auto& param : methodInstance->mParams)
	{
		BF_ASSERT(!param.mResolvedType->IsDeleting());
	}

	BfLogSysM("DoMethodDeclaration %s Module: %p Type: %p MethodInst: %p Reified: %d Unspecialized: %d IRFunction: %d MethodId:%llx\n", mangledName.c_str(), this, mCurTypeInstance, methodInstance, methodInstance->mIsReified, mCurTypeInstance->IsUnspecializedType(), methodInstance->mIRFunction.mId, methodInstance->mIdHash);

	SizedArray<BfIRMDNode, 8> diParams;
	diParams.push_back(mBfIRBuilder->DbgGetType(resolvedReturnType));

	if ((!methodDef->mIsStatic) && (typeDef->mIsStatic) && (methodDef->mMethodDeclaration != NULL))
	{
		//CS0708
		Fail("Cannot declare instance members in a static class", methodDef->GetRefNode());
	}

	if ((methodDef->mIsVirtual) && (methodDef->mGenericParams.size() != 0))
	{
		Fail("Virtual generic methods not supported", methodDeclaration->mVirtualSpecifier);
		methodDef->mIsVirtual = false;
	}

 	BfAstNode* mutSpecifier = NULL;
 	if (methodDeclaration != NULL)
 		mutSpecifier = methodDeclaration->mMutSpecifier;
 	else if (methodDef->GetPropertyMethodDeclaration() != NULL)
 	{
		mutSpecifier = methodDef->GetPropertyMethodDeclaration()->mMutSpecifier;
		if (mutSpecifier == NULL)
		{
			auto propertyDeclaration = methodDef->GetPropertyDeclaration();
			if (propertyDeclaration != NULL)
			{
				if (auto propExprBody = BfNodeDynCast<BfPropertyBodyExpression>(propertyDeclaration->mDefinitionBlock))
					mutSpecifier = propExprBody->mMutSpecifier;
			}
		}
	}

	if ((mutSpecifier != NULL) && (!mCurTypeInstance->IsBoxed()) && (!methodInstance->mIsForeignMethodDef))
	{
		if (methodDef->mIsStatic)
			Warn(0, "Unnecessary 'mut' specifier, static methods have no implicit 'this' target to mutate", mutSpecifier);
		else if ((!mCurTypeInstance->IsValueType()) && (!mCurTypeInstance->IsInterface()))
			Warn(0, "Unnecessary 'mut' specifier, methods of reference types are implicitly mutating", mutSpecifier);
		else if (methodDef->mMethodType == BfMethodType_Ctor)
			Warn(0, "Unnecessary 'mut' specifier, constructors are implicitly mutating", mutSpecifier);
		else if (methodDef->mMethodType == BfMethodType_Dtor)
			Warn(0, "Unnecessary 'mut' specifier, destructors are implicitly mutating", mutSpecifier);
	}

	if (isTemporaryFunc)
	{
		// This handles temporary methods for autocomplete types
		//BF_ASSERT(mIsScratchModule);
		//BF_ASSERT(mCompiler->IsAutocomplete());
		BfLogSysM("DoMethodDeclaration isTemporaryFunc bailout\n");
		return; // Bail out early for autocomplete pass
	}

	//TODO: We used to have this (this != mContext->mExternalFuncModule) check, but it caused us to keep around
	//  an invalid mFuncRefernce (which came from GetMethodInstanceAtIdx) which later got remapped by the
	//  autocompleter.  Why did we have this check anyway?
	/*if ((typeInstance->mContext != mContext) && (!methodDef->IsEmptyPartial()))
	{
		AddMethodReference(methodInstance);
		mFuncReferences[methodInstance] = func;
		BfLogSysM("Adding func reference (DoMethodDeclaration). Module:%p MethodInst:%p LLVMFunc:%p\n", this, methodInstance, func);
	}*/

	if (mParentModule != NULL)
	{
		// For extension modules we need to keep track of our own methods so we can know which methods
		//  we have defined ourselves and which are from the parent module or other extensions
		if (!func.IsFake())
		{
			if (func)
				mFuncReferences[methodInstance] = func;
		}
	}

	if (methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
	{
		methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
		auto owningModule = methodInstance->GetOwner()->mModule;
		if (!owningModule->mIsScratchModule)
			owningModule->mOnDemandMethodCount++;
		VerifyOnDemandMethods();
	}

	bool wasAwaitingDecl = methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl;
	if (wasAwaitingDecl)
		methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingReference;

	if (addToWorkList)
	{
		if ((!methodDef->mIsAbstract) && (!methodInstance->mIgnoreBody))
		{
			AddMethodToWorkList(methodInstance);
		}
		else
		{
			BfLogSysM("DoMethodDeclaration ignoring method body %d %d %d\n", hasExternSpecifier, methodDef->mIsAbstract, methodInstance->mIgnoreBody);

			methodInstance->mIgnoreBody = true;
			if (typeInstance->IsUnspecializedType())
			{
				// Don't hold on to actual Function for unspecialized types
				methodInstance->mIRFunction = BfIRFunction();
			}
		}
	}
	else
	{
		//BF_ASSERT(methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference);
	}

	if ((!methodInstance->IsSpecializedGenericMethodOrType()) && (!mCurTypeInstance->IsBoxed()) &&
		(!methodDef->mIsLocalMethod) &&
		(!CheckDefineMemberProtection(methodDef->mProtection, methodInstance->mReturnType)) &&
		(!methodDef->mReturnTypeRef->IsTemporary()))
	{
		if (methodDef->mMethodType == BfMethodType_PropertyGetter)
		{
			//TODO:
			//CS0053
			Fail(StrFormat("Inconsistent accessibility: property type '%s' is less accessible than property '%s'",
				TypeToString(methodInstance->mReturnType).c_str(), MethodToString(methodInstance).c_str()),
				methodDef->mReturnTypeRef, true);
		}
		else
		{
			//CS0050
			Fail(StrFormat("Inconsistent accessibility: return type '%s' is less accessible than method '%s'",
				TypeToString(methodInstance->mReturnType).c_str(), MethodToString(methodInstance).c_str()),
				methodDef->mReturnTypeRef, true);
		}
	}

	if (typeInstance->IsInterface())
	{
		if (methodDef->mMethodType == BfMethodType_Ctor)
			Fail("Interfaces cannot contain constructors", methodDeclaration);
		if (methodDeclaration != NULL)
		{
			if (methodDef->mBody == NULL)
			{
				if (methodDef->mProtection != BfProtection_Public) //TODO: MAKE AN ERROR
					Warn(0, "Protection specifiers can only be used with interface methods containing a default implementation body", methodDeclaration->mProtectionSpecifier);
				if ((methodDeclaration->mVirtualSpecifier != NULL) &&
					(methodDeclaration->mVirtualSpecifier->mToken != BfToken_Abstract) &&
					(methodDeclaration->mVirtualSpecifier->mToken != BfToken_Concrete)) //TODO: MAKE AN ERROR
					Warn(0, "Virtual specifiers can only be used with interface methods containing a default implementation body", methodDeclaration->mVirtualSpecifier);
			}
		}
	}

	bool foundHiddenMethod = false;

	BfTokenNode* virtualToken = NULL;
	auto propertyDeclaration = methodDef->GetPropertyDeclaration();
	if (propertyDeclaration != NULL)
		virtualToken = propertyDeclaration->mVirtualSpecifier;
	else if (methodDeclaration != NULL)
		virtualToken = methodDeclaration->mVirtualSpecifier;

	// Don't compare specialized generic methods against normal methods
	if ((((mCurMethodInstance->mIsUnspecialized) || (mCurMethodInstance->mMethodDef->mGenericParams.size() == 0))) &&
		(!methodDef->mIsLocalMethod) && (!mCurTypeInstance->IsUnspecializedTypeVariation()))
	{
		if ((!methodInstance->mIsForeignMethodDef) && (methodDef->mMethodType != BfMethodType_Init))
		{
			typeDef->PopulateMemberSets();
			BfMethodDef* nextMethod = NULL;
			BfMemberSetEntry* entry = NULL;
			if (typeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
				nextMethod = (BfMethodDef*)entry->mMemberDef;

			while (nextMethod != NULL)
			{
				auto checkMethod = nextMethod;
				nextMethod = nextMethod->mNextWithSameName;

				if (checkMethod == methodDef)
					continue;

				auto checkMethodInstance = typeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault;
				if (checkMethodInstance == NULL)
				{
					if ((methodDef->mDeclaringType->IsExtension()) && (!checkMethod->mDeclaringType->IsExtension()))
						checkMethodInstance = GetRawMethodInstanceAtIdx(typeInstance, checkMethod->mIdx);
					if (checkMethodInstance == NULL)
						continue;
				}

				if (((checkMethodInstance->mChainType == BfMethodChainType_None) || (checkMethodInstance->mChainType == BfMethodChainType_ChainHead)) &&
					(checkMethodInstance->GetExplicitInterface() == methodInstance->GetExplicitInterface()) &&
					(checkMethod->mIsMutating == methodDef->mIsMutating) &&
					(CompareMethodSignatures(checkMethodInstance, mCurMethodInstance)))
				{
					bool canChain = false;

					if ((methodDef->mParams.empty()) &&
						(checkMethodInstance->mMethodDef->mIsStatic == methodInstance->mMethodDef->mIsStatic))
					{
						if ((methodDef->mMethodType == BfMethodType_CtorNoBody) || (methodDef->mMethodType == BfMethodType_Dtor))
							canChain = true;
						else if (methodDef->mMethodType == BfMethodType_Normal)
						{
							if ((methodDef->mName == BF_METHODNAME_MARKMEMBERS) ||
								(methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC) ||
								(methodDef->mName == BF_METHODNAME_FIND_TLS_MEMBERS))
								canChain = true;
						}
					}

					if (canChain)
					{
						bool isBetter;
						bool isWorse;
						CompareDeclTypes(typeInstance, checkMethodInstance->mMethodDef->mDeclaringType, methodInstance->mMethodDef->mDeclaringType, isBetter, isWorse);
						if (isBetter && !isWorse)
						{
							methodInstance->mChainType = BfMethodChainType_ChainHead;
							checkMethodInstance->mChainType = BfMethodChainType_ChainMember;
						}
						else
						{
							checkMethodInstance->mChainType = BfMethodChainType_ChainHead;
							methodInstance->mChainType = BfMethodChainType_ChainMember;
						}
					}
					else
					{
						if (!typeInstance->IsTypeMemberAccessible(checkMethod->mDeclaringType, methodDef->mDeclaringType))
							continue;

						bool silentlyAllow = false;
						bool extensionWarn = false;
						if (checkMethod->mDeclaringType != methodDef->mDeclaringType)
						{
							if (typeInstance->IsInterface())
							{
								if (methodDef->mIsOverride)
									silentlyAllow = true;
							}
							else
							{
								if ((methodDef->mIsOverride) && (checkMethod->mIsExtern))
								{
									silentlyAllow = true;
									methodInstance->mIsInnerOverride = true;
									CheckOverridenMethod(methodInstance, checkMethodInstance);
								}
								else if (!checkMethod->mDeclaringType->IsExtension())
								{
									foundHiddenMethod = true;
									if ((methodDef->mMethodType == BfMethodType_Ctor) && (methodDef->mIsStatic))
										silentlyAllow = true;
									else if (methodDef->mIsNew)
									{
										silentlyAllow = true;
									}
									else if (checkMethod->GetMethodDeclaration() == NULL)
										silentlyAllow = true;
									else if (methodDef->mIsOverride)
										silentlyAllow = true;
									else
										extensionWarn = true;
								}
								else
									silentlyAllow = true;
							}
						}

						if ((checkMethod->mCommutableKind == BfCommutableKind_Reverse) || (methodDef->mCommutableKind == BfCommutableKind_Reverse))
							silentlyAllow = true;

						if (!silentlyAllow)
						{
							if ((!methodDef->mName.IsEmpty()) || (checkMethodInstance->mMethodDef->mIsOperator))
							{
								auto refNode = methodDef->GetRefNode();
								BfError* bfError;
								if (extensionWarn)
								{
									if (methodDef->mDeclaringType->mProject != checkMethod->mDeclaringType->mProject)
										bfError = Warn(BfWarning_CS0114_MethodHidesInherited,
											StrFormat("This method hides a method in the root type definition. Use the 'new' keyword if the hiding was intentional. Note that this method is not callable from project '%s'.",
												checkMethod->mDeclaringType->mProject->mName.c_str()), refNode);
									else
										bfError = Warn(BfWarning_CS0114_MethodHidesInherited,
											"This method hides a method in the root type definition. Use the 'new' keyword if the hiding was intentional.", refNode);
								}
								else
								{
									bfError = Fail(StrFormat("Method '%s' already declared with the same parameter types", MethodToString(checkMethodInstance).c_str()), refNode, true);
								}
								if ((bfError != NULL) && (checkMethod->GetRefNode() != refNode))
									mCompiler->mPassInstance->MoreInfo("First declaration", checkMethod->GetRefNode());
							}
						}
					}
				}
			}
		}

		// Virtual methods give their error while slotting
		if ((!typeInstance->IsBoxed()) && (!methodDef->mIsVirtual) && (methodDef->mProtection != BfProtection_Private) &&
			(!methodDef->mIsLocalMethod) &&
			(!methodInstance->mIsForeignMethodDef) && (typeInstance->mBaseType != NULL) &&
			(methodDef->mMethodType == BfMethodType_Normal) && (methodDef->mMethodDeclaration != NULL))
		{
			auto baseType = typeInstance->mBaseType;
			while (baseType != NULL)
			{
				auto baseTypeDef = baseType->mTypeDef;
				baseTypeDef->PopulateMemberSets();
				BfMethodDef* checkMethod = NULL;
				BfMemberSetEntry* entry = NULL;
				if (baseTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
					checkMethod = (BfMethodDef*)entry->mMemberDef;

				while (checkMethod != NULL)
				{
					if (checkMethod->mMethodDeclaration == NULL)
					{
						checkMethod = checkMethod->mNextWithSameName;
						continue;
					}

					if (baseType->mMethodInstanceGroups.size() == 0)
					{
						BF_ASSERT(baseType->IsIncomplete() && mCompiler->IsAutocomplete());
						break;
					}

					if (checkMethod == methodDef)
					{
						checkMethod = checkMethod->mNextWithSameName;
						continue;
					}
					if (checkMethod->mName != methodDef->mName)
					{
						checkMethod = checkMethod->mNextWithSameName;
						continue;
					}

					auto checkMethodInstance = GetRawMethodInstanceAtIdx(baseType, checkMethod->mIdx);
					if (checkMethodInstance != NULL)
					{
						if ((checkMethodInstance->GetExplicitInterface() == methodInstance->GetExplicitInterface()) &&
							(checkMethod->mProtection != BfProtection_Private) &&
							(CompareMethodSignatures(checkMethodInstance, mCurMethodInstance)))
						{
							if (!methodDef->mIsNew)
							{
								BfAstNode* refNode = methodInstance->mMethodDef->GetRefNode();
								if (refNode != NULL)
								{
									BfError* bfError = Warn(BfWarning_CS0114_MethodHidesInherited, StrFormat("Method hides inherited member from '%s'. Use the 'new' keyword if the hiding was intentional.", TypeToString(baseType).c_str()), refNode); //CDH TODO should we mention override keyword in warning text?
									if (bfError != NULL)
										bfError->mIsPersistent = true;
								}
							}
							foundHiddenMethod = true;
							break;
						}
					}

					checkMethod = checkMethod->mNextWithSameName;
				}

				if (foundHiddenMethod)
					break;
				baseType = baseType->mBaseType;
			}

			if ((methodDef->mIsNew) && (!foundHiddenMethod) && (!mCurTypeInstance->IsSpecializedType()))
			{
				auto propertyDeclaration = methodDef->GetPropertyDeclaration();
				auto tokenNode = (propertyDeclaration != NULL) ? propertyDeclaration->mNewSpecifier :
					methodDeclaration->mNewSpecifier;
				Fail("Method does not hide an inherited member. The 'new' keyword is not required", tokenNode, true);
			}
		}
	}

	if ((methodDef->mIsConcrete) && (!methodInstance->mIsForeignMethodDef) && (!mCurTypeInstance->IsInterface()))
	{
		Fail("Only interfaces methods can be declared as 'concrete'", methodDeclaration->mVirtualSpecifier);
	}

	if ((methodDef->mIsVirtual) && (methodDef->mIsStatic) && (!methodInstance->mIsInnerOverride))
	{
		if ((virtualToken != NULL) && (virtualToken->mToken == BfToken_Override) && (methodDef->mDeclaringType->mTypeCode == BfTypeCode_Extension))
			Fail("No suitable method found to override", virtualToken, true);
		else
			Fail("Static members cannot be marked as override, virtual, or abstract", virtualToken, true);
	}
	else if (methodDef->mIsVirtual)
	{
		if ((methodDef->mProtection == BfProtection_Private) && (virtualToken != NULL))
			Fail("Virtual or abstract members cannot be 'private'", virtualToken, true);
		if ((methodDef->mProtection == BfProtection_Internal) && (virtualToken != NULL))
			Fail("Virtual or abstract members cannot be 'internal'. Consider using the 'protected internal' access specifier.", virtualToken, true);
	}

	mCompiler->mStats.mMethodDeclarations++;
	mCompiler->UpdateCompletion();
}

void BfModule::UniqueSlotVirtualMethod(BfMethodInstance* methodInstance)
{
	BF_ASSERT(methodInstance->GetOwner() == mCurTypeInstance);

	auto typeInstance = mCurTypeInstance;
	auto methodDef = methodInstance->mMethodDef;

	int virtualMethodMatchIdx = -1;

	if (typeInstance->mHotTypeData != NULL)
	{
		if (typeInstance->mHotTypeData->mVTableOrigLength != -1)
		{
			BF_ASSERT(mCompiler->IsHotCompile());
			// In the 'normal' case we'd assert that mIsOverride is false, but if we can't find the declaring method then we
			//  may slot this override anyway (?)

			int vTableStart = 0;
			auto implBaseType = typeInstance->GetImplBaseType();
			if (implBaseType != NULL)
				vTableStart = implBaseType->mVirtualMethodTableSize;

			StringT<512> mangledName;
			BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), methodInstance);
			for (int checkIdxOfs = 0; checkIdxOfs < (int)typeInstance->mHotTypeData->mVTableEntries.size(); checkIdxOfs++)
			{
				// O(1) checking when virtual methods haven't changed
				int checkIdx = (typeInstance->mVirtualMethodTableSize - vTableStart + checkIdxOfs) % (int)typeInstance->mHotTypeData->mVTableEntries.size();

				auto& entry = typeInstance->mHotTypeData->mVTableEntries[checkIdx];
				if (mangledName == entry.mFuncName)
				{
					virtualMethodMatchIdx = vTableStart + checkIdx;
					break;
				}
			}

			if (virtualMethodMatchIdx != -1)
			{
				methodInstance->mVirtualTableIdx = virtualMethodMatchIdx;
				typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mDeclaringMethod = methodInstance;
				typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod = methodInstance;
			}
			typeInstance->mVirtualMethodTableSize = (int)typeInstance->mVirtualMethodTable.size();
		}
	}

	if (virtualMethodMatchIdx == -1)
	{
		methodInstance->mVirtualTableIdx = typeInstance->mVirtualMethodTableSize++;
		BfVirtualMethodEntry entry = { methodInstance, methodInstance };
		typeInstance->mVirtualMethodTable.push_back(entry);
	}
}

void BfModule::CompareDeclTypes(BfTypeInstance* typeInst, BfTypeDef* newDeclType, BfTypeDef* prevDeclType, bool& isBetter, bool& isWorse)
{
	if ((!prevDeclType->IsExtension()) && (newDeclType->IsExtension()))
	{
		// When we provide an extension override in the same project a root type override
		isBetter = true;
		isWorse = false;
	}
	else
	{
		isBetter = newDeclType->mProject->ContainsReference(prevDeclType->mProject);
		isWorse = prevDeclType->mProject->ContainsReference(newDeclType->mProject);
	}

	if ((isBetter == isWorse) && (typeInst != NULL) && (newDeclType->IsExtension()) && (prevDeclType->IsExtension()))
	{
		if ((typeInst->mGenericTypeInfo != NULL) && (typeInst->mGenericTypeInfo->mGenericExtensionInfo != NULL))
		{
			isBetter = false;
			isWorse = false;

			auto newConstraints = typeInst->GetGenericParamsVector(newDeclType);
			auto prevConstraints = typeInst->GetGenericParamsVector(prevDeclType);

			for (int genericIdx = 0; genericIdx < (int)newConstraints->size(); genericIdx++)
			{
				auto newConstraint = (*newConstraints)[genericIdx];
				auto prevConstraint = (*prevConstraints)[genericIdx];

				bool newIsSubset = AreConstraintsSubset(newConstraint, prevConstraint);
				bool prevIsSubset = AreConstraintsSubset(prevConstraint, newConstraint);

				if ((prevIsSubset) && (!newIsSubset))
					isBetter = true;
				if ((!prevIsSubset) && (newIsSubset))
					isWorse = true;
			}
		}
	}
}

bool BfModule::SlotVirtualMethod(BfMethodInstance* methodInstance, BfAmbiguityContext* ambiguityContext)
{
	BP_ZONE("BfModule::SlotVirtualMethod");

	if (mCurTypeInstance->IsUnspecializedTypeVariation())
		return false;

	auto _AddVirtualDecl = [&](BfMethodInstance* declMethodInstance)
	{
		if (!mCompiler->mOptions.mAllowHotSwapping)
			return;
		if ((!methodInstance->mIsReified) || (!declMethodInstance->mIsReified))
			return;

		if (methodInstance->mHotMethod == NULL)
			CheckHotMethod(methodInstance, "");

		if (methodInstance->mHotMethod == NULL)
			return;

		//BF_ASSERT(declMethodInstance->mHotMethod != NULL);
		if (declMethodInstance->mHotMethod == NULL)
			CheckHotMethod(declMethodInstance, "");
		auto virtualDecl = mCompiler->mHotData->GetVirtualDeclaration(declMethodInstance->mHotMethod);
		virtualDecl->mRefCount++;
		methodInstance->mHotMethod->mReferences.Add(virtualDecl);
	};

	auto typeInstance = mCurTypeInstance;
	auto typeDef = typeInstance->mTypeDef;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();
	auto propertyDeclaration = methodDef->GetPropertyDeclaration();
	auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration();

	if (methodInstance->mIsInnerOverride)
		return false;

	BfAstNode* declaringNode = methodDeclaration;
	if (propertyMethodDeclaration != NULL)
		declaringNode = propertyMethodDeclaration->mNameNode;

	BfMethodInstance* methodOverriden = NULL;
	bool usedMethod = false;

	BfTokenNode* virtualToken = NULL;
	if (propertyDeclaration != NULL)
		virtualToken = propertyDeclaration->mVirtualSpecifier;
	else if (methodDeclaration != NULL)
		virtualToken = methodDeclaration->mVirtualSpecifier;

	if ((methodDef->mIsVirtual) && (methodDef->mIsStatic))
	{
		 //Fail: Static members cannot be marked as override, virtual, or abstract
	}
	else if (methodDef->mIsVirtual)
	{
		if (IsHotCompile())
			mContext->EnsureHotMangledVirtualMethodName(methodInstance);

		BfTypeInstance* checkBase = typeInstance;
		// If we are in an extension, look in ourselves first
		if (methodDef->mDeclaringType->mTypeDeclaration == typeInstance->mTypeDef->mTypeDeclaration)
			checkBase = checkBase->mBaseType;

		if (typeInstance->IsValueType())
		{
			if (typeInstance->mBaseType == NULL)
				return false; // It's actually ValueType

			// We allow structs to override object methods for when they get boxed, so just ignore 'override' keyword until it gets boxed
			if (!methodDef->mIsOverride)
			{
				Fail("Structs cannot have virtual methods", virtualToken, true);
				return false;
			}

			checkBase = mContext->mBfObjectType;
			if (checkBase->mVirtualMethodTableSize == 0)
				PopulateType(checkBase, BfPopulateType_Full);
			if (checkBase->mDefineState < BfTypeDefineState_DefinedAndMethodsSlotted)
				return false; // System circular ref, don't do struct checking on those
		}

		int virtualMethodMatchIdx = -1;
		bool hadHidingError = false;

		BfMethodInstance* bestOverrideMethodInstance = NULL;
		BfMethodInstance* ambiguousOverrideMethodInstance = NULL;
		int bestOverrideMethodIdx = -1;
		int ambiguousOverrideMethodIdx = -1;

		if (checkBase != NULL)
		{
			auto& baseVirtualMethodTable = checkBase->mVirtualMethodTable;

			auto lookupType = checkBase;
			while (lookupType != NULL)
			{
				lookupType->mTypeDef->PopulateMemberSets();
				BfMethodDef* nextMethodDef = NULL;
				BfMemberSetEntry* entry;
				if (lookupType->mTypeDef->mMethodSet.TryGetWith((StringImpl&)methodInstance->mMethodDef->mName, &entry))
					nextMethodDef = (BfMethodDef*)entry->mMemberDef;

				while (nextMethodDef != NULL)
				{
					auto checkMethodDef = nextMethodDef;
					nextMethodDef = nextMethodDef->mNextWithSameName;

					if ((!checkMethodDef->mIsVirtual) || (checkMethodDef->mIsOverride))
						continue;

					BfMethodInstance* baseMethodInstance = NULL;

					int checkMethodIdx = -1;
					BfMethodInstance* lookupMethodInstance = lookupType->mMethodInstanceGroups[checkMethodDef->mIdx].mDefault;
					if ((lookupMethodInstance == NULL) || (lookupMethodInstance->mVirtualTableIdx == -1))
					{
						if (lookupType->IsUnspecializedTypeVariation())
						{
							if (!lookupMethodInstance->mMethodDef->mIsOverride)
							{
								baseMethodInstance = lookupMethodInstance;
								checkMethodIdx = -2;
							}
						}

						if (baseMethodInstance == NULL)
							continue;
					}

					if (baseMethodInstance == NULL)
					{
						checkMethodIdx = lookupMethodInstance->mVirtualTableIdx;
						if (checkMethodIdx >= baseVirtualMethodTable.mSize)
							FatalError("SlotVirtualMethod OOB in baseVirtualMethodTable[checkMethodIdx]");
						auto& baseMethodRef = baseVirtualMethodTable[checkMethodIdx];
						if (baseMethodRef.mDeclaringMethod.mMethodNum == -1)
						{
							BF_ASSERT(mCompiler->mOptions.mHasVDataExtender);
							continue;
						}

						baseMethodInstance = baseVirtualMethodTable[checkMethodIdx].mDeclaringMethod;
						if (baseMethodInstance == NULL)
						{
							AssertErrorState();
							continue;
						}
					}

					if ((baseMethodInstance != NULL) && (CompareMethodSignatures(baseMethodInstance, methodInstance)))
					{
						if (methodDef->mIsOverride)
						{
							BfMethodInstance* checkMethodInstance;
							if (checkMethodIdx == -2)
								checkMethodInstance = baseMethodInstance;
							else if (typeInstance->IsValueType())
								checkMethodInstance = checkBase->mVirtualMethodTable[checkMethodIdx].mDeclaringMethod;
							else
								checkMethodInstance = typeInstance->mVirtualMethodTable[checkMethodIdx].mDeclaringMethod;

							auto newDeclType = checkMethodInstance->mMethodDef->mDeclaringType;

							if (!typeInstance->IsTypeMemberAccessible(newDeclType, methodDef->mDeclaringType->mProject))
								continue;

							if (bestOverrideMethodInstance != NULL)
							{
								auto prevDeclType = bestOverrideMethodInstance->mMethodDef->mDeclaringType;

								bool isBetter = newDeclType->mProject->ContainsReference(prevDeclType->mProject);
								bool isWorse = prevDeclType->mProject->ContainsReference(newDeclType->mProject);
								if (isBetter == isWorse)
								{
									ambiguousOverrideMethodInstance = bestOverrideMethodInstance;
								}
								else
								{
									if (isWorse)
										continue;
									ambiguousOverrideMethodInstance = NULL;
								}
							}

							bestOverrideMethodInstance = checkMethodInstance;
							bestOverrideMethodIdx = checkMethodIdx;
						}
						else
						{
							if ((baseMethodInstance->GetOwner() != methodInstance->GetOwner()) && (!methodDef->mIsNew))
							{
								if (!hadHidingError)
									Warn(BfWarning_CS0114_MethodHidesInherited, StrFormat("Method hides inherited member from '%s'. Use either 'override' or 'new'.", TypeToString(checkBase).c_str()), declaringNode);
								hadHidingError = true;
							}
						}
					}
				}

				lookupType = lookupType->mBaseType;
			}
		}

		//TODO:
		if ((checkBase != NULL)
			&& (false)
			)
		{
			auto& baseVirtualMethodTable = checkBase->mVirtualMethodTable;
			for (int checkMethodIdx = (int) baseVirtualMethodTable.size() - 1; checkMethodIdx >= 0; checkMethodIdx--)
			{
				auto& baseMethodRef = baseVirtualMethodTable[checkMethodIdx];
				if (baseMethodRef.mDeclaringMethod.mMethodNum == -1)
				{
					BF_ASSERT(mCompiler->mOptions.mHasVDataExtender);
					continue;
				}

				BfMethodInstance* baseMethodInstance = baseVirtualMethodTable[checkMethodIdx].mDeclaringMethod;
				if (baseMethodInstance == NULL)
				{
					AssertErrorState();
					continue;
				}

				if ((baseMethodInstance != NULL) && (CompareMethodSignatures(baseMethodInstance, methodInstance)))
				{
					if (methodDef->mIsOverride)
					{
						BfMethodInstance* checkMethodInstance;
						if (typeInstance->IsValueType())
							checkMethodInstance = checkBase->mVirtualMethodTable[checkMethodIdx].mDeclaringMethod;
						else
							checkMethodInstance = typeInstance->mVirtualMethodTable[checkMethodIdx].mDeclaringMethod;

						auto newDeclType = checkMethodInstance->mMethodDef->mDeclaringType;

						if (!typeInstance->IsTypeMemberAccessible(newDeclType, methodDef->mDeclaringType->mProject))
							continue;

						if (bestOverrideMethodInstance != NULL)
						{
							auto prevDeclType = bestOverrideMethodInstance->mMethodDef->mDeclaringType;

							bool isBetter = newDeclType->mProject->ContainsReference(prevDeclType->mProject);
							bool isWorse = prevDeclType->mProject->ContainsReference(newDeclType->mProject);
							if (isBetter == isWorse)
							{
								ambiguousOverrideMethodInstance = bestOverrideMethodInstance;
							}
							else
							{
								if (isWorse)
									continue;
								ambiguousOverrideMethodInstance = NULL;
							}
						}

						bestOverrideMethodInstance = checkMethodInstance;
						bestOverrideMethodIdx = checkMethodIdx;
					}
					else
					{
						if ((baseMethodInstance->GetOwner() != methodInstance->GetOwner()) && (!methodDef->mIsNew))
						{
							if (!hadHidingError)
								Warn(BfWarning_CS0114_MethodHidesInherited, StrFormat("Method hides inherited member from '%s'. Use either 'override' or 'new'.", TypeToString(checkBase).c_str()), declaringNode);
							hadHidingError = true;
						}
					}
				}
			}
		}

		if (bestOverrideMethodInstance != NULL)
		{
			if (ambiguousOverrideMethodInstance != NULL)
			{
				bool allow = false;

				// If neither of these declarations "include" each other then it's okay.  This can happen when we have two extensions that create the same virtual method but with different constraints.
				//  When when specialize, the specialized type will determine wither it has illegally pulled in both declarations or not
				bool canSeeEachOther = false;
				//
				{
					SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, ambiguousOverrideMethodInstance);
					if (bestOverrideMethodInstance->GetOwner()->IsTypeMemberIncluded(bestOverrideMethodInstance->mMethodDef->mDeclaringType, ambiguousOverrideMethodInstance->mMethodDef->mDeclaringType, this))
						canSeeEachOther = true;
				}
				//
				{
					SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, bestOverrideMethodInstance);
					if (ambiguousOverrideMethodInstance->GetOwner()->IsTypeMemberIncluded(ambiguousOverrideMethodInstance->mMethodDef->mDeclaringType, bestOverrideMethodInstance->mMethodDef->mDeclaringType, this))
						canSeeEachOther = true;
				}

				if (!canSeeEachOther)
				{
					BF_ASSERT(bestOverrideMethodInstance->GetOwner()->IsUnspecializedType() && ambiguousOverrideMethodInstance->GetOwner()->IsUnspecializedType());
				}
				else
				{
					auto error = Fail(StrFormat("Method '%s' is an ambiguous override", MethodToString(methodInstance).c_str()), methodDef->GetRefNode());
					if (error != NULL)
					{
						mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", MethodToString(bestOverrideMethodInstance).c_str()), bestOverrideMethodInstance->mMethodDef->GetRefNode());
						mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", MethodToString(ambiguousOverrideMethodInstance).c_str()), ambiguousOverrideMethodInstance->mMethodDef->GetRefNode());
					}
				}
			}
		}

		if (bestOverrideMethodIdx == -2)
		{
			// Comes from an unspecialized variation
			virtualMethodMatchIdx = bestOverrideMethodIdx;
		}
		else if ((bestOverrideMethodInstance != NULL) && (bestOverrideMethodIdx != -1))
		{
			auto& baseVirtualMethodTable = checkBase->mVirtualMethodTable;
			BfMethodInstance* baseVirtualMethodInstance = baseVirtualMethodTable[bestOverrideMethodIdx].mDeclaringMethod;
			if ((baseVirtualMethodInstance != methodInstance) && (methodDef->mIsOverride))
			{
				if (baseVirtualMethodInstance->mReturnType != methodInstance->mReturnType)
				{
					BfTypeReference* returnTypeRef;
					if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
					{
						returnTypeRef = propertyDeclaration->mTypeRef;
						Fail("Property differs from overridden property by type", returnTypeRef, true);
					}
					else if (auto methodDeclaration = methodDef->GetMethodDeclaration())
					{
						returnTypeRef = methodDeclaration->mReturnType;
						Fail("Method differs from overridden method by return value", returnTypeRef, true);
					}
					else
					{
						Fail(StrFormat("Internal error with method '%s'", MethodToString(methodInstance).c_str()));
					}

					return usedMethod;
				}
			}

			if (methodDef->mIsOverride)
			{
				// We can have multiple matches when a virtual method is used to implement interface methods
				//  It doesn't matter what we set mVirtualTableIdx, as all base classes will override all matching
				//  vtable entries for this method signature
				virtualMethodMatchIdx = bestOverrideMethodIdx;
				if (!typeInstance->IsValueType())
					methodInstance->mVirtualTableIdx = virtualMethodMatchIdx;
				if (typeInstance->IsValueType())
				{
					methodOverriden = checkBase->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod;
				}
				else
				{
					methodInstance->mVirtualTableIdx = virtualMethodMatchIdx;

					bool preferRootDefinition = (methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mMethodType == BfMethodType_Dtor);

					BfMethodInstance* setMethodInstance = methodInstance;
					bool doOverride = false;

					auto& overridenRef = typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod;
					if (overridenRef.mKind != BfMethodRefKind_AmbiguousRef)
					{
						methodOverriden = overridenRef;

						doOverride = true;
						if (methodOverriden->GetOwner() == methodInstance->GetOwner())
						{
							bool isBetter = false;
							bool isWorse = false;
							CompareDeclTypes(typeInstance, methodInstance->mMethodDef->mDeclaringType, methodOverriden->mMethodDef->mDeclaringType, isBetter, isWorse);
							if (isBetter == isWorse)
							{
								// We have to resolve later per-project
								if ((ambiguityContext != NULL) && (ambiguityContext->mIsProjectSpecific))
								{
									auto declMethodInstance = checkBase->mVirtualMethodTable[virtualMethodMatchIdx].mDeclaringMethod;
									ambiguityContext->Add(virtualMethodMatchIdx, NULL, -1, methodOverriden, methodInstance);
								}

								if ((ambiguityContext == NULL) || (!ambiguityContext->mIsProjectSpecific))
									typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod.mKind = BfMethodRefKind_AmbiguousRef;
								doOverride = false;
							}
							else
							{
								if ((isBetter) && (ambiguityContext != NULL))
								{
									ambiguityContext->Remove(virtualMethodMatchIdx);
								}

								if ((isBetter) && (methodInstance->GetOwner() == methodOverriden->GetOwner()))
								{
									if (preferRootDefinition)
									{
										// Leave the master GCMarkMember
										isBetter = false;
										isWorse = true;
									}
								}

								doOverride = isBetter;
							}
						}
					}
					else if ((preferRootDefinition) && (!methodDef->mDeclaringType->IsExtension()))
					{
						methodOverriden = overridenRef;
						doOverride = true;
					}

					if (doOverride)
					{
						auto declMethodInstance = (BfMethodInstance*)typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mDeclaringMethod;
						_AddVirtualDecl(declMethodInstance);
						setMethodInstance->mVirtualTableIdx = virtualMethodMatchIdx;

						auto& implMethodRef = typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod;
						if ((!mCompiler->mIsResolveOnly) && (implMethodRef.mMethodNum >= 0) &&
							(implMethodRef.mTypeInstance == typeInstance) && (methodInstance->GetOwner() == typeInstance))
						{
							auto prevImplMethodInstance = (BfMethodInstance*)implMethodRef;
							if (prevImplMethodInstance->mMethodDef->mDeclaringType->mProject != methodInstance->mMethodDef->mDeclaringType->mProject)
							{
								// We may need to have to previous method reified when we must re-slot in another project during vdata creation
								BfReifyMethodDependency dep;
								dep.mDepMethod = typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mDeclaringMethod;
								dep.mMethodIdx = implMethodRef.mMethodNum;
								typeInstance->mReifyMethodDependencies.Add(dep);
							}

							if (!methodInstance->mMangleWithIdx)
							{
								// Keep mangled names from conflicting
								methodInstance->mMangleWithIdx = true;
								if ((methodInstance->mIRFunction) && (methodInstance->mDeclModule->mIsModuleMutable))
								{
									StringT<4096> mangledName;
									BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), methodInstance);
									methodInstance->mDeclModule->mBfIRBuilder->SetFunctionName(methodInstance->mIRFunction, mangledName);
								}
							}
						}

						typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod = setMethodInstance;
					}
				}

				if (methodOverriden != NULL)
				{
					CheckOverridenMethod(methodInstance, methodOverriden);
				}
			}
		}

		if ((virtualMethodMatchIdx == -1) && ((ambiguityContext == NULL) || (!ambiguityContext->mIsReslotting)))
		{
			if (methodDef->mIsOverride)
			{
				BfTokenNode* overrideToken = NULL;
				if (auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration())
					overrideToken = propertyMethodDeclaration->mPropertyDeclaration->mVirtualSpecifier;
				else if (auto methodDeclaration = methodDef->GetMethodDeclaration())
					overrideToken = methodDeclaration->mVirtualSpecifier;
				if (overrideToken != NULL)
				{
					if ((propertyDeclaration != NULL) && (propertyDeclaration->mNameNode != NULL) &&
						((methodDef->mMethodType == BfMethodType_PropertyGetter) || (methodDef->mMethodType == BfMethodType_PropertySetter)))
						Fail(StrFormat("No suitable method found to override for '%s.%s'",
							propertyDeclaration->mNameNode->ToString().c_str(), (methodDef->mMethodType == BfMethodType_PropertyGetter) ? "get" : "set"), overrideToken, true);
					else
						Fail("No suitable method found to override", overrideToken, true);
				}

				return usedMethod;
			}

			UniqueSlotVirtualMethod(methodInstance);
		}
		else
			usedMethod = true;

		if (typeInstance->IsValueType())
			return usedMethod;
	}

	bool foundInterface = false;
	bool hadAnyMatch = false;

	// See if this method matches interfaces
	for (auto& ifaceTypeInst : typeInstance->mInterfaces)
	{
		auto ifaceInst = ifaceTypeInst.mInterfaceType;
		if (ifaceInst->IsIncomplete())
			PopulateType(ifaceInst, BfPopulateType_DataAndMethods);
		int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
		int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();

		// See "bidirectional" rules mentioned in DoTypeInstanceMethodProcessing
		if ((!typeInstance->IsTypeMemberAccessible(methodDef->mDeclaringType, ifaceTypeInst.mDeclaringType)) &&
		    (!typeInstance->IsTypeMemberAccessible(ifaceTypeInst.mDeclaringType, methodDef->mDeclaringType)))
			continue;
		if (!typeInstance->IsTypeMemberIncluded(methodDef->mDeclaringType, ifaceTypeInst.mDeclaringType, this))
			continue;

		bool hadMatch = false;
		BfMethodInstance* hadNameMatch = NULL;
		BfType* expectedReturnType = NULL;

		bool showedError = false;

		// We go through our VTable looking for NULL entries within the interface sections
		//  The only instance of finding a "better" method is when we have explicitly interface-dotted method
		//  because a normal method declared in this type could have matched earlier
		//for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)

		ifaceInst->mTypeDef->PopulateMemberSets();
		BfMethodDef* checkMethodDef = NULL;
		BfMemberSetEntry* entry;
		if (ifaceInst->mTypeDef->mMethodSet.TryGetWith((StringImpl&)methodInstance->mMethodDef->mName, &entry))
			checkMethodDef = (BfMethodDef*)entry->mMemberDef;

		while (checkMethodDef != NULL)
		{
			int iMethodIdx = checkMethodDef->mIdx;

			int iTableIdx = iMethodIdx + startIdx;
			BfTypeInterfaceMethodEntry* interfaceMethodEntry = &typeInstance->mInterfaceMethodTable[iTableIdx];
			auto iMethodPtr = &interfaceMethodEntry->mMethodRef;
			bool storeIFaceMethod = false;

			if ((mCompiler->mPassInstance->HasFailed()) && (iMethodIdx >= (int)ifaceInst->mMethodInstanceGroups.size()))
			{
				checkMethodDef = checkMethodDef->mNextWithSameName;
				continue;
			}

			auto& iMethodGroup = ifaceInst->mMethodInstanceGroups[iMethodIdx];
			auto iMethodInst = iMethodGroup.mDefault;
			if (iMethodInst == NULL)
			{
				if ((!ifaceInst->IsGenericTypeInstance()) || (!ifaceInst->mTypeDef->mIsCombinedPartial))
					AssertErrorState();
				checkMethodDef = checkMethodDef->mNextWithSameName;
				continue;
			}
			if (iMethodInst->mMethodDef->mName == methodInstance->mMethodDef->mName)
				hadNameMatch = iMethodInst;

			bool doesMethodSignatureMatch = CompareMethodSignatures(iMethodInst, methodInstance);
			if ((!doesMethodSignatureMatch) && (interfaceMethodEntry->mMethodRef.IsNull()))
			{
				doesMethodSignatureMatch = IsCompatibleInterfaceMethod(iMethodInst, methodInstance);
			}

			if ((doesMethodSignatureMatch) && (methodInstance->GetOwner()->IsValueType()))
			{
				if ((!iMethodInst->mMethodDef->mIsMutating) && (methodInstance->mMethodDef->mIsMutating))
				{
					if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface == ifaceInst))
					{
						auto error = mCompiler->mPassInstance->Fail(StrFormat("Implementation method '%s' cannot specify 'mut' because the interface method does not allow it",
							MethodToString(methodInstance).c_str()), methodInstance->mMethodDef->GetMutNode());
						if (error != NULL)
							mCompiler->mPassInstance->MoreInfo(StrFormat("Declare the interface method as 'mut' to allow matching 'mut' implementations"), iMethodInst->mMethodDef->mMethodDeclaration);
						showedError = true;
					}
				}
			}

			if (doesMethodSignatureMatch)
			{
				usedMethod = true;
				hadMatch = true;
				hadAnyMatch = true;
				storeIFaceMethod = true;

				if ((iMethodPtr->mKind != BfMethodRefKind_AmbiguousRef) && (iMethodPtr->mTypeInstance != NULL))
				{
					auto prevMethod = (BfMethodInstance*)*iMethodPtr;
					if ((mCompiler->mIsResolveOnly) && (prevMethod == methodInstance) && (!mIsComptimeModule))
					{
						// When autocompletion regenerates a single method body but not the whole type then
						//  we will see ourselves in the vtable already
						return usedMethod;
					}

					bool isBetter = false;
					bool isWorse = false;
					isBetter = (methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface != NULL);
					isWorse = (prevMethod->mMethodInfoEx != NULL) && (prevMethod->mMethodInfoEx->mExplicitInterface != NULL);
					if (isBetter == isWorse)
					{
						isBetter = methodInstance->mReturnType == iMethodInst->mReturnType;
						isWorse = prevMethod->mReturnType == iMethodInst->mReturnType;
					}

					if (isBetter == isWorse)
						CompareDeclTypes(typeInstance, methodInstance->mMethodDef->mDeclaringType, prevMethod->mMethodDef->mDeclaringType, isBetter, isWorse);
					if (isBetter == isWorse)
					{
						if (ambiguityContext != NULL)
						{
							ambiguityContext->Add(~iTableIdx, &ifaceTypeInst, iMethodIdx, prevMethod, methodInstance);
						}

						iMethodPtr->mKind = BfMethodRefKind_AmbiguousRef;
						storeIFaceMethod = true;
					}
					else
					{
						if ((isBetter) && (ambiguityContext != NULL))
							ambiguityContext->Remove(~iTableIdx);
						storeIFaceMethod = isBetter;
					}
				}
			}

			if (storeIFaceMethod)
			{
				if (methodInstance->GetNumGenericParams() != 0)
					_AddVirtualDecl(iMethodInst);
				*iMethodPtr = methodInstance;
			}

			checkMethodDef = checkMethodDef->mNextWithSameName;
		}

		if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface == ifaceInst) && (!hadMatch) && (!showedError))
		{
			if (expectedReturnType != NULL)
				Fail(StrFormat("Wrong return type, expected '%s'", TypeToString(expectedReturnType).c_str()), declaringNode, true);
			else if (hadNameMatch != NULL)
			{
				auto error = Fail("Method parameters don't match interface method", declaringNode, true);
				if (error != NULL)
					mCompiler->mPassInstance->MoreInfo("See interface method declaration", hadNameMatch->mMethodDef->GetRefNode());
			}
			else
			{
				auto propertyDecl = methodDef->GetPropertyDeclaration();
				if (propertyDecl != NULL)
				{
					auto propertyMethodDecl = methodDef->GetPropertyMethodDeclaration();

					String name;
					if (auto indexerDeclaration = BfNodeDynCast<BfIndexerDeclaration>(propertyDecl))
						name = "this[]";
					else if (propertyDecl->mNameNode != NULL)
						propertyDecl->mNameNode->ToString(name);

					Fail(StrFormat("Property '%s' %s accessor not defined in interface '%s'", name.c_str(),
						(methodDef->mMethodType == BfMethodType_PropertyGetter) ? "get" : "set", TypeToString(ifaceInst).c_str()), methodDef->GetRefNode(), true);
				}
				else
					Fail(StrFormat("Method '%s' not found in interface '%s'", methodDef->mName.c_str(), TypeToString(ifaceInst).c_str()), methodDef->GetRefNode(), true);
			}
		}
	}

	// Any overriden virtual methods that were used in interfaces also need to be replaced
	if (methodOverriden != NULL)
	{
		for (auto& methodEntry : typeInstance->mInterfaceMethodTable)
		{
			if (methodEntry.mMethodRef == methodOverriden)
				methodEntry.mMethodRef = methodInstance;
		}
	}

	return usedMethod;
}

void BfModule::CheckOverridenMethod(BfMethodInstance* methodInstance, BfMethodInstance* methodOverriden)
{
	auto methodDef = methodInstance->mMethodDef;
	auto prevProtection = methodOverriden->mMethodDef->mProtection;
	if ((methodDef->mProtection != prevProtection) && (methodDef->mMethodType != BfMethodType_Dtor))
	{
		const char* protectionNames[] = { "hidden", "private", "internal", "protected", "protected internal", "public" };
		BF_STATIC_ASSERT(BF_ARRAY_COUNT(protectionNames) == BfProtection_COUNT);
		BfAstNode* protectionRefNode = NULL;
		if (auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration())
		{
			protectionRefNode = propertyMethodDeclaration->mProtectionSpecifier;
			if (protectionRefNode == NULL)
				protectionRefNode = propertyMethodDeclaration->mPropertyDeclaration->mProtectionSpecifier;
			if (protectionRefNode == NULL)
				protectionRefNode = propertyMethodDeclaration->mPropertyDeclaration->mNameNode;
		}
		else if (auto methodDeclaration = methodDef->GetMethodDeclaration())
		{
			protectionRefNode = methodDeclaration->mProtectionSpecifier;
			if (protectionRefNode == NULL)
				protectionRefNode = methodDeclaration->mNameNode;
		}
		if (protectionRefNode != NULL)
			Fail(StrFormat("Cannot change access modifiers when overriding '%s' inherited member", protectionNames[(int)prevProtection]), protectionRefNode, true);
	}
}

bool BfModule::SlotInterfaceMethod(BfMethodInstance* methodInstance)
{
	auto typeInstance = mCurTypeInstance;
	auto methodDef = methodInstance->mMethodDef;

	if (methodDef->mMethodType == BfMethodType_Ctor)
		return true;

	bool foundOverride = false;

	if ((methodDef->mBody == NULL) && (methodDef->mProtection == BfProtection_Private))
	{
		auto methodDeclaration = methodDef->GetMethodDeclaration();
		BfAstNode* refNode = NULL;
		if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
			refNode = propertyDeclaration->mProtectionSpecifier;
		else if (methodDeclaration != NULL)
			refNode = methodDeclaration->mProtectionSpecifier;
		Fail("Private interface methods must provide a body", refNode);
	}

	if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface != NULL) && (!methodDef->mIsOverride))
	{
		Fail("Explicit interfaces can only be specified for overrides in interface declarations", methodDef->GetMethodDeclaration()->mExplicitInterface);
	}

	BfAstNode* declaringNode = methodDef->GetRefNode();

	for (auto& ifaceTypeInst : typeInstance->mInterfaces)
	{
		auto ifaceInst = ifaceTypeInst.mInterfaceType;
		int startIdx = ifaceTypeInst.mStartInterfaceTableIdx;
		int iMethodCount = (int)ifaceInst->mMethodInstanceGroups.size();

		if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface != ifaceInst))
			continue;

		ifaceInst->mTypeDef->PopulateMemberSets();
		BfMethodDef* nextMethod = NULL;
		BfMemberSetEntry* entry = NULL;
		if (ifaceInst->mTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
			nextMethod = (BfMethodDef*)entry->mMemberDef;

		while (nextMethod != NULL)
		{
			auto checkMethod = nextMethod;
			nextMethod = nextMethod->mNextWithSameName;

			auto ifaceMethod = ifaceInst->mMethodInstanceGroups[checkMethod->mIdx].mDefault;
			if (ifaceMethod == NULL)
				continue;
			if (CompareMethodSignatures(ifaceMethod, methodInstance))
			{
				if (methodDef->mIsOverride)
				{
					if (ifaceMethod->mMethodDef->mProtection == BfProtection_Private)
					{
						auto error = Fail(StrFormat("Interface method '%s' cannot override private interface method '%s'", MethodToString(methodInstance).c_str(), MethodToString(ifaceMethod).c_str()), declaringNode);
						if (error != NULL)
						{
							mCompiler->mPassInstance->MoreInfo("See base interface method", ifaceMethod->mMethodDef->GetRefNode());
						}
					}

					foundOverride = true;
				}
				else if (!methodDef->mIsNew)
				{
					Warn(BfWarning_CS0114_MethodHidesInherited, StrFormat("Method hides inherited member from '%s'. Use the 'new' keyword if hiding was intentional.", TypeToString(ifaceInst).c_str()), declaringNode);
				}
			}
		}
	}

	if ((methodDef->mIsOverride) && (!foundOverride))
	{
		// This allows us to declare a method in the base definition or in an extension and then provide
		//  an implementation in a more-specific extension
		typeInstance->mTypeDef->PopulateMemberSets();
		BfMethodDef* nextMethod = NULL;
		BfMemberSetEntry* entry = NULL;
		if (typeInstance->mTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
			nextMethod = (BfMethodDef*)entry->mMemberDef;

		while (nextMethod != NULL)
		{
			auto checkMethod = nextMethod;
			nextMethod = nextMethod->mNextWithSameName;

			auto ifaceMethod = typeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault;
			if (ifaceMethod == NULL)
			 	continue;
			if (ifaceMethod->mMethodDef->mDeclaringType == methodInstance->mMethodDef->mDeclaringType)
			 	continue;

			if (CompareMethodSignatures(ifaceMethod, methodInstance))
			{
			 	foundOverride = true;
			}
		}

// 		for (int methodIdx = 0; methodIdx < typeInstance->mMethodInstanceGroups.size(); methodIdx++)
// 		{
// 			auto ifaceMethod = typeInstance->mMethodInstanceGroups[methodIdx].mDefault;
// 			if (ifaceMethod == NULL)
// 				continue;
// 			if (ifaceMethod->mMethodDef->mDeclaringType == methodInstance->mMethodDef->mDeclaringType)
// 				continue;
//
// 			if (CompareMethodSignatures(ifaceMethod, methodInstance))
// 			{
// 				foundOverride = true;
// 			}
// 		}
	}

	if (methodDef->mIsOverride)
	{
		if (!foundOverride)
		{
			BfTokenNode* overrideToken = NULL;
			if (auto propertyDeclaration = methodDef->GetPropertyDeclaration())
				overrideToken = propertyDeclaration->mVirtualSpecifier;
			else if (auto methodDeclaration = methodDef->GetMethodDeclaration())
				overrideToken = methodDeclaration->mVirtualSpecifier;
			if (overrideToken != NULL)
			{
				Fail("No suitable method found to override", overrideToken, true);
			}
			else
			{
				// Possible cause - DTOR with params
				AssertErrorState();
			}
		}
		return true;
	}

	// Generic methods can't be called virtually
	if (methodInstance->GetNumGenericParams() != 0)
		return true;

	// Only public methods can be called virtually
	if (methodDef->mProtection != BfProtection_Public)
		return true;

	UniqueSlotVirtualMethod(methodInstance);

	return true;
}

void BfModule::SetMethodDependency(BfMethodInstance* methodInstance)
{
	if (methodInstance->mMethodInfoEx == NULL)
		return;

	int wantMinDepth = -1;

	if (mCurTypeInstance != NULL)
		wantMinDepth = mCurTypeInstance->mDependencyMap.mMinDependDepth + 1;

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mMinDependDepth != -1))
	{
		int wantTypeMinDepth = mCurMethodInstance->mMethodInfoEx->mMinDependDepth + 1;
		if ((wantMinDepth == -1) || (wantTypeMinDepth < wantMinDepth))
			wantMinDepth = wantTypeMinDepth;
	}

	if ((methodInstance->mMethodInfoEx->mMinDependDepth == -1) || (wantMinDepth < methodInstance->mMethodInfoEx->mMinDependDepth))
		methodInstance->mMethodInfoEx->mMinDependDepth = wantMinDepth;
}

void BfModule::DbgFinish()
{
	if ((mBfIRBuilder == NULL) || (mExtensionCount != 0))
		return;

	if (mAwaitingInitFinish)
		FinishInit();

	if (mHasForceLinkMarker)
		return;

	String markerName;
	BfIRValue linkMarker;

	if (mBfIRBuilder->DbgHasInfo())
	{
		bool needForceLinking = false;
		for (auto& ownedType : mOwnedTypeInstances)
		{
			bool hasConfirmedReference = false;
			for (auto& methodInstGroup : ownedType->mMethodInstanceGroups)
			{
				if ((methodInstGroup.IsImplemented()) && (methodInstGroup.mDefault != NULL) &&
					(!methodInstGroup.mDefault->mMethodDef->mIsStatic) && (methodInstGroup.mDefault->mIsReified) && (!methodInstGroup.mDefault->mAlwaysInline) &&
					((methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude) || (methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_Referenced)) &&
					(methodInstGroup.mHasEmittedReference))
				{
					hasConfirmedReference = true;
				}
			}
			if ((!hasConfirmedReference) || (ownedType->IsBoxed()))
				needForceLinking = true;
		}

		if ((needForceLinking) && (mProject->mCodeGenOptions.mAsmKind == BfAsmKind_None))
		{
			BfMethodState methodState;
			SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
			methodState.mTempKind = BfMethodState::TempKind_Static;

			mHasForceLinkMarker = true;

			auto voidType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_None));
			SizedArray<BfIRType, 0> paramTypes;
			BfIRFunctionType funcType = mBfIRBuilder->CreateFunctionType(voidType, paramTypes);
			String linkName = "FORCELINKMOD_" + mModuleName;
			BfIRFunction func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, linkName);
			mBfIRBuilder->SetActiveFunction(func);
			auto entryBlock = mBfIRBuilder->CreateBlock("main", true);
			mBfIRBuilder->SetInsertPoint(entryBlock);

			auto firstType = mOwnedTypeInstances[0];

			UpdateSrcPos(mContext->mBfObjectType->mTypeDef->GetRefNode());
			SizedArray<BfIRMDNode, 1> diParamTypes;
			diParamTypes.Add(mBfIRBuilder->DbgGetType(GetPrimitiveType(BfTypeCode_None)));
			BfIRMDNode diFuncType = mBfIRBuilder->DbgCreateSubroutineType(diParamTypes);
			auto diScope = mBfIRBuilder->DbgCreateFunction(mDICompileUnit, "FORCELINKMOD", linkName, mCurFilePosition.mFileInstance->mDIFile,
				mCurFilePosition.mCurLine + 1, diFuncType, false, true, mCurFilePosition.mCurLine + 1, 0, false, func);

			methodState.mCurScope->mDIScope = diScope;
			UpdateSrcPos(mContext->mBfObjectType->mTypeDef->GetRefNode(), BfSrcPosFlag_Force);

			SizedArray<BfIRValue, 8> args;
			BfExprEvaluator exprEvaluator(this);

			for (auto& ownedType : mOwnedTypeInstances)
			{
				auto alloca = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_Int8)));
				auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(diScope, "variable", mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(ownedType));
				mBfIRBuilder->DbgInsertDeclare(alloca, diVariable);
			}

			mBfIRBuilder->CreateRetVoid();
			mBfIRBuilder->SetActiveFunction(BfIRFunction());
		}
	}
}

bool BfModule::Finish()
{
	BP_ZONE("BfModule::Finish");
	BfLogSysM("BfModule finish: %p\n", this);

	if (mHadBuildError)
	{
		// Don't AssertErrorState here, this current pass may not have failed but
		//  the module was still queued in mFinishedModuleWorkList
		ClearModule();
		return true;
	}

	if (mUsedSlotCount != -1)
	{
		BF_ASSERT(mCompiler->mMaxInterfaceSlots != -1);
		mUsedSlotCount = mCompiler->mMaxInterfaceSlots;
	}

	if ((!mGeneratesCode) && (!mAddedToCount))
		return true;

	BF_ASSERT(mAddedToCount);
	mAddedToCount = false;
	mAwaitingFinish = false;

	mCompiler->mStats.mModulesFinished++;

	if (HasCompiledOutput())
	{
		BF_ASSERT(!mBfIRBuilder->mIgnoreWrites);

		DbgFinish();

		if (mAwaitingInitFinish)
			FinishInit();

		for (auto ownedTypeInst : mOwnedTypeInstances)
		{
			// Generate dbg info and add global variables if we haven't already
			mBfIRBuilder->PopulateType(ownedTypeInst);
		}

		if (mBfIRBuilder->DbgHasInfo())
		{
			mBfIRBuilder->DbgFinalize();
		}

		String objOutputPath;
		String irOutputPath;
		mIsModuleMutable = false;
		//mOutFileNames.Clear();

		BF_ASSERT(((int)mOutFileNames.size() >= mExtensionCount) || (mParentModule != NULL));

		bool writeModule = mBfIRBuilder->HasExports();
		String outputPath;

		BfCodeGenOptions codeGenOptions = mProject->mCodeGenOptions;
		auto& compilerOpts = mCompiler->mOptions;

		codeGenOptions.mSIMDSetting = compilerOpts.mSIMDSetting;
		auto moduleOptions = GetModuleOptions();
		codeGenOptions.mOptLevel = moduleOptions.mOptLevel;
		codeGenOptions.mSIMDSetting = moduleOptions.mSIMDSetting;
		codeGenOptions.mWriteLLVMIR = mCompiler->mOptions.mWriteIR;
		codeGenOptions.mWriteObj = mCompiler->mOptions.mGenerateObj;
		codeGenOptions.mWriteBitcode = mCompiler->mOptions.mGenerateBitcode;
		codeGenOptions.mVirtualMethodOfs = 1 + mCompiler->GetDynCastVDataCount() + mCompiler->mMaxInterfaceSlots;
		codeGenOptions.mDynSlotOfs = mSystem->mPtrSize - mCompiler->GetDynCastVDataCount() * 4;

		mCompiler->mStats.mIRBytes += mBfIRBuilder->mStream.GetSize();
		mCompiler->mStats.mConstBytes += mBfIRBuilder->mTempAlloc.GetAllocSize();

		bool allowWriteToLib = true;
		if ((allowWriteToLib) && (codeGenOptions.mOptLevel == BfOptLevel_OgPlus) &&
			(!mCompiler->IsHotCompile()) && (mModuleName != "vdata"))
		{
			codeGenOptions.mWriteToLib = true;
			mWroteToLib = true;
		}
		else
		{
			mWroteToLib = false;
		}

		for (int fileIdx = 0; fileIdx <= mExtensionCount; fileIdx++)
		{
			outputPath = mModuleName;
			outputPath = mCompiler->mOutputDirectory + "/" + mProject->mName + "/" + outputPath;

			BfModuleFileName moduleFileName;

			if (mParentModule != NULL)
			{
				for (auto&& checkPair : mParentModule->mSpecializedMethodModules)
				{
					if (checkPair.mValue == this)
						moduleFileName.mProjects = checkPair.mKey;
				}
			}
			moduleFileName.mProjects.Add(mProject);

			if (fileIdx > 0)
				outputPath += StrFormat("@%d", fileIdx + 1);
			if (mCompiler->mOptions.mWriteIR)
				irOutputPath = outputPath + ".ll";
			if (mCompiler->mOptions.mGenerateObj)
			{
                objOutputPath = outputPath + BF_OBJ_EXT;
				moduleFileName.mFileName = objOutputPath;
			}
			else if (mCompiler->mOptions.mWriteIR)
			{
				moduleFileName.mFileName = irOutputPath;
			}
			else if ((!mCompiler->mOptions.mGenerateObj) && (!mCompiler->mOptions.mGenerateBitcode) && (!mCompiler->mOptions.mWriteIR))
			{
				BFMODULE_FATAL(this, "Neither obj nor IR specified");
			}

			moduleFileName.mModuleWritten = writeModule;
			moduleFileName.mWroteToLib = mWroteToLib;
			if (!mOutFileNames.Contains(moduleFileName))
				mOutFileNames.Add(moduleFileName);
		}

		if (mCompiler->IsHotCompile())
		{
			codeGenOptions.mIsHotCompile = true;
			if (mParentModule != NULL)
				mParentModule->mHadHotObjectWrites = true;
			mHadHotObjectWrites = true;
		}

		//TODO: Testing VDATA
		/*if (mModuleName == "vdata")
		{
			codeGenOptions.mOptLevel = 4;
		}*/

		codeGenOptions.GenerateHash();
		BP_ZONE("BfModule::Finish.WriteObjectFile");

		if ((writeModule) && (!mBfIRBuilder->mIgnoreWrites))
			mCompiler->mCodeGen.WriteObjectFile(this, outputPath, codeGenOptions);
		mLastModuleWrittenRevision = mCompiler->mRevision;
	}
	else
	{
		for (auto type : mOwnedTypeInstances)
		{
			BF_ASSERT((!type->IsIncomplete()) || (type->IsSpecializedByAutoCompleteMethod()));
		}
	}

	for (auto& specModulePair : mSpecializedMethodModules)
	{
		auto specModule = specModulePair.mValue;
		if ((specModule->mIsModuleMutable) && (!specModule->mAwaitingFinish))
		{
			BfLogSysM(" Mutable spec module remaining: %p\n", specModule);
		}
	}

	CleanupFileInstances();

	// We can't ClearModuleData here because we need the mSpecializedMethodModules to remain valid until we Finish them too

	return true;
}

void BfModule::ReportMemory(MemReporter* memReporter)
{
	memReporter->Add(sizeof(BfModule));
	if (mBfIRBuilder != NULL)
	{
		memReporter->BeginSection("IRBuilder_ConstData");
		memReporter->Add("Used", mBfIRBuilder->mTempAlloc.GetAllocSize());
		memReporter->Add("Unused", mBfIRBuilder->mTempAlloc.GetTotalAllocSize() - mBfIRBuilder->mTempAlloc.GetAllocSize());
		memReporter->EndSection();

		memReporter->BeginSection("IRBuilder_BFIR");
		memReporter->Add("Used", (int)(mBfIRBuilder->mStream.GetSize()));
		memReporter->Add("Unused", (int)(mBfIRBuilder->mStream.mPools.size() * ChunkedDataBuffer::ALLOC_SIZE) - mBfIRBuilder->mStream.GetSize());
		memReporter->EndSection();

		memReporter->Add(sizeof(BfIRBuilder));
	}

	memReporter->BeginSection("FileInstanceMap");
	memReporter->AddMap(mFileInstanceMap);
	memReporter->AddMap(mNamedFileInstanceMap);
	memReporter->Add((int)mNamedFileInstanceMap.size() * sizeof(BfFileInstance));
	memReporter->EndSection();

	memReporter->AddVec(mOwnedTypeInstances, false);
	memReporter->AddVec(mSpecializedMethodModules, false);
	memReporter->AddVec(mOutFileNames, false);
	memReporter->AddMap("FuncReferences", mFuncReferences, false);
	memReporter->AddMap(mInterfaceSlotRefs, false);
	memReporter->AddMap(mStaticFieldRefs, false);
	memReporter->AddMap(mClassVDataRefs, false);
	memReporter->AddMap("VDataExtMap", mClassVDataExtRefs, false);
	memReporter->AddMap(mTypeDataRefs, false);
	memReporter->AddMap(mDbgRawAllocDataRefs, false);
	memReporter->AddMap(mDeferredMethodCallData, false);
	memReporter->AddHashSet(mDeferredMethodIds, false);
	memReporter->AddHashSet("ModuleRefs", mModuleRefs, false);
}

// ClearModuleData is called immediately after the module is compiled, so don't clear out any data that needs to
//  be transient through the next compile
void BfModule::ClearModuleData(bool clearTransientData)
{
	BfLogSysM("ClearModuleData %p\n", this);

	if (mAddedToCount)
	{
		mCompiler->mStats.mModulesFinished++;
		mAddedToCount = false;
	}

	mDICompileUnit = BfIRMDNode();
	if (clearTransientData)
		mIncompleteMethodCount = 0;
	mHasGenericMethods = false;

	// We don't want to clear these because we want it to persist through module extensions-
	//  otherwise we may think an extension succeeds even though the base module failed
	//mHadBuildError = false;
	//mHadBuildWarning = false;

	mClassVDataRefs.Clear();
	mClassVDataExtRefs.Clear();
	for (auto& kv : mTypeDataRefs)
		kv.mValue = BfIRValue();
	mStringCharPtrPool.Clear();
	mStringObjectPool.Clear();
	mStaticFieldRefs.Clear();
	BF_ASSERT(!mIgnoreErrors);

	for (auto prevIRBuilder : mPrevIRBuilders)
		delete prevIRBuilder;
	mPrevIRBuilders.Clear();

	for (auto& specPair : mSpecializedMethodModules)
	{
		auto specModule = specPair.mValue;
		specModule->ClearModuleData();
	}

	for (int i = 0; i < BfBuiltInFuncType_Count; i++)
		mBuiltInFuncs[i] = BfIRFunction();

	if (mNextAltModule != NULL)
		mNextAltModule->ClearModuleData();

	BfLogSysM("ClearModuleData. Deleting IRBuilder: %p\n", mBfIRBuilder);
	mIsModuleMutable = false;
	delete mBfIRBuilder;
	mBfIRBuilder = NULL;
	mWantsIRIgnoreWrites = false;
}

void BfModule::DisownMethods()
{
	for (int i = 0; i < BfBuiltInFuncType_Count; i++)
		mBuiltInFuncs[i] = BfIRFunction();

	BfModule* methodModule = this;
	if (mParentModule != NULL)
		methodModule = mParentModule;

	for (auto typeInst : methodModule->mOwnedTypeInstances)
	{
		for (auto& methodGroup : typeInst->mMethodInstanceGroups)
		{
			if (methodGroup.mDefault != NULL)
			{
				if (methodGroup.mDefault->mIRFunction)
				{
					BF_ASSERT(methodGroup.mDefault->mDeclModule != NULL);
					if (methodGroup.mDefault->mDeclModule == this)
					{
						methodGroup.mDefault->mIRFunction = BfIRFunction();
					}
				}
			}

			if (methodGroup.mMethodSpecializationMap != NULL)
			{
				for (auto& mapPair : *methodGroup.mMethodSpecializationMap)
				{
					auto methodInstance = mapPair.mValue;
					if (methodInstance->mDeclModule == this)
					{
						methodInstance->mIRFunction = BfIRFunction();
					}
				}
			}
		}
	}
}

void BfModule::ClearModule()
{
	ClearModuleData();

	DisownMethods();

	for (auto& specPair : mSpecializedMethodModules)
	{
		auto specModule = specPair.mValue;
		specModule->ClearModule();
	}
	if (mNextAltModule != NULL)
		mNextAltModule->ClearModule();
}