//#define USE_THUNKED_MddLLOC..

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(push) // 6
#pragma warning(disable:4996)

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfCodeGen.h"
#include "BfExprEvaluator.h"
#include "../Backend/BeLibManger.h"
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
#include "BfDeferEvalChecker.h"

#pragma warning(pop)

//////////////////////////////////////////////////////////////////////////

static bool gDebugStuff = false;

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

void BfLocalVariable::Init()
{	
	if (mResolvedType->IsValuelessType())
	{
		mIsAssigned = true;
		return;
	}

	if (mIsAssigned)
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
			mIsAssigned = true;
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

	delete mMethodDef;
	delete mMethodInstanceGroup;	
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

	//TODO: We got rid of this case because we now set the proper assigned data when we do a return
	// If one of these had a return then treat that case as if it did have an assign -- because it doesn't
	//  cause an UNASSIGNED value to be used
// 	if (mHadReturn || otherLocalAssignData.mHadReturn)
// 	{
// 		SetUnion(otherLocalAssignData);
// 		mHadFallthrough = mHadFallthrough && otherLocalAssignData.mHadFallthrough;
// 		return;
// 	}

	for (int i = 0; i < (int)mAssignedLocals.size(); )
	{
		auto& local = mAssignedLocals[i];
		if (!otherLocalAssignData.mAssignedLocals.Contains(local))
		{
			mAssignedLocals.RemoveAt(i);
		}
		else
			i++;
	}

	mHadFallthrough = mHadFallthrough && otherLocalAssignData.mHadFallthrough;
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
		delete local;

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

void BfMethodState::LocalDefined(BfLocalVariable* localVar, int fieldIdx)
{	
	auto localVarMethodState = GetMethodStateForLocal(localVar);
	if (localVarMethodState != this)
	{
		return;
	}
	//BF_ASSERT(localVarMethodState == this);

	if (!localVar->mIsAssigned)
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

		if ((deferredLocalAssignData == NULL) || (localVar->mLocalVarId >= deferredLocalAssignData->mVarIdBarrier))
		{
			if (fieldIdx >= 0)
			{
				localVar->mUnassignedFieldFlags &= ~((int64)1 << fieldIdx);
				/*if ((localVar->mResolvedTypeRef != NULL) && (localVar->mResolvedTypeRef->IsUnion()))
				{
					
				}*/
				if (localVar->mUnassignedFieldFlags == 0)
					localVar->mIsAssigned = true;
			}
			else
			{
				localVar->mIsAssigned = true;
			}
		}
		else
		{				
			BF_ASSERT(deferredLocalAssignData->mVarIdBarrier != -1);

			BfAssignedLocal defineVal = {localVar, fieldIdx + 1};			
			auto& assignedLocals = deferredLocalAssignData->mAssignedLocals;			
			if (!assignedLocals.Contains(defineVal))			
				assignedLocals.push_back(defineVal);			

			if (ifDeferredLocalAssignData != NULL)
			{
				auto& assignedLocals = ifDeferredLocalAssignData->mAssignedLocals;
				if (!assignedLocals.Contains(defineVal))
					assignedLocals.push_back(defineVal);
			}
		}
	}
	localVar->mWrittenToId = GetRootMethodState()->mCurAccessId++;
}

void BfMethodState::ApplyDeferredLocalAssignData(const BfDeferredLocalAssignData& deferredLocalAssignData)
{
	BF_ASSERT(&deferredLocalAssignData != mDeferredLocalAssignData);

	for (auto& assignedLocal : deferredLocalAssignData.mAssignedLocals)
	{
		LocalDefined(assignedLocal.mLocalVar);
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
			for (auto candidate : entry->mCandidates)
			{
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(candidate).c_str()), candidate->mMethodDef->GetRefNode());
			}
		}
		else
		{
			auto iMethodInst = entry->mInterfaceEntry->mInterfaceType->mMethodInstanceGroups[entry->mMethodIdx].mDefault;
			auto error = mModule->Fail(StrFormat("Interface method '%s' has ambiguous implementations", mModule->MethodToString(iMethodInst).c_str()), entry->mInterfaceEntry->mDeclaringType->GetRefNode());
			for (auto candidate : entry->mCandidates)
			{
				mModule->mCompiler->mPassInstance->MoreInfo(StrFormat("'%s' is a candidate", mModule->MethodToString(candidate).c_str()), candidate->mMethodDef->GetRefNode());
			}
		}
	}
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
		mCurAppendAlign = 0;
	}

	void EmitAppendAlign(int align, int sizeMultiple = 0)
	{
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
					origResolvedTypeRef = mModule->ResolveTypeRef(arrayTypeRef->mElementType);
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

					if (isRawArrayAlloc)
					{
						curAlign = resultType->mAlign;
						EmitAppendAlign(resultType->mAlign);
						sizeValue = mModule->mBfIRBuilder->CreateMul(mModule->GetConstValue(resultType->mSize), allocCount);
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
						BfIRValue elementDataSize = mModule->mBfIRBuilder->CreateMul(mModule->GetConstValue(resultType->mSize), allocCount);
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
														
							auto subDependSize = mModule->TryConstCalcAppend(bindResult.mMethodInstance, calcAppendArgs);							
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

								subDependSize = exprEvaluator.CreateCall(calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, calcAppendArgs);
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
						auto prevVal = mModule->mBfIRBuilder->CreateLoad(accumValuePtr);
						auto addedVal = mModule->mBfIRBuilder->CreateAdd(sizeValue, prevVal);
						mModule->mBfIRBuilder->CreateStore(addedVal, accumValuePtr);
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
		mContext->mUsedModuleNames.Add(ToUpper(moduleName));
	mAddedToCount = false;

	mParentModule = NULL;
	mNextAltModule = NULL;
	mBfIRBuilder = NULL;
	mWantsIRIgnoreWrites = false;
	mModuleOptions = NULL;
	mLastUsedRevision = -1;		
	mUsedSlotCount = -1;

	mIsReified = true;
	mReifyQueued = false;
	mIsSpecialModule = false;
	mIsScratchModule = false;	
	mIsSpecializedMethodModuleRoot = false; // There may be mNextAltModules extending from this
	mHadBuildError = false;
	mHadBuildWarning = false;
	mIgnoreErrors = false;
	mIgnoreWarnings = false;
	mHadIgnoredError = false;
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
#ifdef _DEBUG
	EnsureIRBuilder(mCompiler->mLastAutocompleteModule == this);	
#else
	EnsureIRBuilder(false);
#endif

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

void BfModule::FinishInit()
{
	BF_ASSERT(mAwaitingInitFinish);

	auto moduleOptions = GetModuleOptions();

	mBfIRBuilder->Start(mModuleName, mCompiler->mSystem->mPtrSize, IsOptimized());

#ifdef BF_PLATFORM_WINDOWS
	if (mCompiler->mOptions.mToolsetType == BfToolsetType_GNU)
	{
		if (mCompiler->mOptions.mMachineType == BfMachineType_x86)
			mBfIRBuilder->Module_SetTargetTriple("i686-pc-windows-gnu");
		else
			mBfIRBuilder->Module_SetTargetTriple("x86_64-pc-windows-gnu");
	}
	else //if (mCompiler->mOptions.mToolsetType == BfToolsetType_Microsoft)
	{
		if (mCompiler->mOptions.mMachineType == BfMachineType_x86)
			mBfIRBuilder->Module_SetTargetTriple("i686-pc-windows-msvc");
		else
			mBfIRBuilder->Module_SetTargetTriple("x86_64-pc-windows-msvc");
	}
#elif defined BF_PLATFORM_LINUX
	if (mCompiler->mOptions.mMachineType == BfMachineType_x86)
		mBfIRBuilder->Module_SetTargetTriple("i686-unknown-linux-gnu");
	else
		mBfIRBuilder->Module_SetTargetTriple("x86_64-unknown-linux-gnu");
#else
	// Leave it default
	mBfIRBuilder->Module_SetTargetTriple("");
#endif

	mBfIRBuilder->SetBackend(IsTargetingBeefBackend());	

	if (moduleOptions.mOptLevel == BfOptLevel_OgPlus)
	{
		// Og+ requires debug info
		moduleOptions.mEmitDebugInfo = 1;
	}

	mHasFullDebugInfo = moduleOptions.mEmitDebugInfo == 1;
	
	// We need to create DIBuilder for mIsSpecialModule so we have it around when we need it
// 	if ((!mCompiler->mIsResolveOnly) && ((mIsScratchModule) || (moduleOptions.mEmitDebugInfo != 0)))
// 	{
// 		BF_ASSERT((!mBfIRBuilder->mIgnoreWrites) || (mIsScratchModule) || (!mIsReified));
// 		mBfIRBuilder->DbgInit();
// 	}
// 	else
// 		mHasFullDebugInfo = false;

	if ((!mCompiler->mIsResolveOnly) && (!mIsScratchModule) && (moduleOptions.mEmitDebugInfo != 0) && (mIsReified))
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

		mDICompileUnit = mBfIRBuilder->DbgCreateCompileUnit(llvm::dwarf::DW_LANG_C_plus_plus, mModuleName, ".", "Beef Compiler 0.42", /*moduleOptions.mOptLevel > 0*/false, "", 0, !mHasFullDebugInfo);
	}	

	mAwaitingInitFinish = false;	
}

void BfModule::ReifyModule()
{	
	BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));

	BfLogSysM("ReifyModule %@ %s\n", this, mModuleName.c_str());
	BF_ASSERT((this != mContext->mScratchModule) && (this != mContext->mUnreifiedModule));
	mIsReified = true;
	mReifyQueued = false;
	StartNewRevision(RebuildKind_SkipOnDemandTypes, true);
	mCompiler->mStats.mModulesReified++;
}

void BfModule::UnreifyModule()
{
	BfLogSysM("UnreifyModule %p %s\n", this, mModuleName.c_str());
	BF_ASSERT((this != mContext->mScratchModule) && (this != mContext->mUnreifiedModule));
	mIsReified = false;	
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
		else if (!mIsReified)
		{			
			mBfIRBuilder->mIgnoreWrites = true;
		}
		else
		{
			// We almost always want this to be 'false' unless we need need to be able to inspect the generated LLVM
			//  code as we walk the AST
			//mBfIRBuilder->mDbgVerifyCodeGen = true;			
			if (
                (mModuleName == "Program")
				//|| (mModuleName == "System_Internal")
				//|| (mModuleName == "vdata")
				//|| (mModuleName == "Hey_Dude_Bro_TestClass")
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

	// Already on new revision?
	if ((mRevision == mCompiler->mRevision) && (!force))
		return;
	
	mHadBuildError = false;
	mHadBuildWarning = false;
	mExtensionCount = 0;
	mRevision = mCompiler->mRevision;	
	mRebuildIdx++;
	ClearModuleData();	

	// Clear this here, not in ClearModuleData, so we preserve those references even after writing out module
	if (rebuildKind != BfModule::RebuildKind_None) // Leave string pool refs for when we need to use things like [LinkName("")] methods bofore re-reification
		mStringPoolRefs.Clear();	
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
			delete specModule;
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
						mContext->RebuildType(typeInst, false, false, false);
				}
				else
				{
					for (auto& methodGroup : typeInst->mMethodInstanceGroups)
						if ((methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) ||
							(methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingReference))
						{
							oldOnDemandCount++;
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
	BfIRValue vDataValue;
	if (mCompiler->mOptions.mObjectHasDebugFlags)
		vDataValue = mBfIRBuilder->CreatePtrToInt(classVData, BfTypeCode_IntPtr);
	else
		vDataValue = mBfIRBuilder->CreateBitCast(classVData, mBfIRBuilder->MapType(mContext->mBfClassVDataPtrType));	
	typeValueParams.push_back(vDataValue);
	if (mCompiler->mOptions.mObjectHasDebugFlags)
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
		type->IsRetTypeType() || type->IsConcreteInterfaceType())
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
	if (type->IsVoid())
		return mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(type));	
	return mBfIRBuilder->CreateConstStructZero(mBfIRBuilder->MapType(type));
}

BfTypedValue BfModule::GetFakeTypedValue(BfType* type)
{
	// This is a conservative "IsValueless", since it's not an error to use a fakeVal even if we don't need one
	if (type->mSize == 0)
		return BfTypedValue(BfIRValue(), type);
	else
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), type);
}

BfTypedValue BfModule::GetDefaultTypedValue(BfType* type, bool allowRef, BfDefaultValueKind defaultValueKind)
{
	PopulateType(type, BfPopulateType_Data);
	mBfIRBuilder->PopulateType(type, type->IsValueType() ? BfIRPopulateType_Full : BfIRPopulateType_Declaration);

	if (defaultValueKind == BfDefaultValueKind_Undef)
	{
		auto primType = type->ToPrimitiveType();
		if (primType != NULL)
		{
			return BfTypedValue(mBfIRBuilder->GetUndefConstValue(primType->mTypeDef->mTypeCode), type);
		}
		return BfTypedValue(mBfIRBuilder->CreateUndefValue(mBfIRBuilder->MapType(type)), type);
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

	BfIRType strCharType;
	//
	{		
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mWantsIRIgnoreWrites);
		strCharType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->GetPrimitiveType(BfTypeCode_Char8), (int)str.length() + 1);
	}

	BfIRValue strConstant;
	if (define)
	{		
		strConstant = mBfIRBuilder->CreateConstString(str);		
	}
	BfIRValue gv = mBfIRBuilder->CreateGlobalVariable(strCharType,
		true, BfIRLinkageType_External,
		strConstant, stringDataName);

	if (define)
		mBfIRBuilder->GlobalVar_SetUnnamedAddr(gv, true);	
	return mBfIRBuilder->CreateInBoundsGEP(gv, 0, 0);
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
		auto objData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(stringTypeInst->mBaseType, BfIRPopulateType_Full), typeValueParams);

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
		
		stringValData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(stringTypeInst, BfIRPopulateType_Full), typeValueParams);
	}
	
	auto stringValLiteral = mBfIRBuilder->CreateGlobalVariable(
		mBfIRBuilder->MapTypeInst(stringTypeInst),
		true,
		BfIRLinkageType_External,
		define ? stringValData : BfIRValue(),
		stringObjName);

	auto stringVal = mBfIRBuilder->CreateBitCast(stringValLiteral, mBfIRBuilder->MapType(stringTypeInst, BfIRPopulateType_Full));		
	return stringVal;
}

int BfModule::GetStringPoolIdx(BfIRValue constantStr, BfIRConstHolder* constHolder)
{	
	BF_ASSERT(constantStr.IsConst());

	if (constHolder == NULL)
		constHolder = mBfIRBuilder;

	auto constant = constHolder->GetConstant(constantStr);
	if (constant->mTypeCode == BfTypeCode_Int32)
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

BfIRValue BfModule::GetStringCharPtr(int stringId)
{
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

BfIRValue BfModule::GetStringCharPtr(BfIRValue strValue)
{
	if (strValue.IsConst())
	{
		int stringId = GetStringPoolIdx(strValue);
		BF_ASSERT(stringId != -1);
		return GetStringCharPtr(stringId);
	}

	BfIRValue charPtrPtr = mBfIRBuilder->CreateInBoundsGEP(strValue, 0, 1);	
	BfIRValue charPtr = mBfIRBuilder->CreateLoad(charPtrPtr);	
	return charPtr;
}

BfIRValue BfModule::GetStringCharPtr(const StringImpl& str)
{
	return GetStringCharPtr(GetStringObjectValue(str));	
}

BfIRValue BfModule::GetStringObjectValue(int strId)
{	
	BfIRValue* objValue;
	if (mStringObjectPool.TryGetValue(strId, &objValue))
		return *objValue;

	auto stringPoolEntry = mContext->mStringObjectIdMap[strId];
	return GetStringObjectValue(stringPoolEntry.mString, true);
}

BfIRValue BfModule::GetStringObjectValue(const StringImpl& str, bool define)
{	
	auto stringType = ResolveTypeDef(mCompiler->mStringTypeDef, define ? BfPopulateType_Data : BfPopulateType_Declaration);
	mBfIRBuilder->PopulateType(stringType);

	int strId = mContext->GetStringLiteralId(str);

	BfIRValue* irValuePtr = NULL;
	if (!mStringObjectPool.TryAdd(strId, NULL, &irValuePtr))
		return *irValuePtr;	

	// If this wasn't in neither dictionary yet, add to mStringPoolRefs
	if (!mStringCharPtrPool.ContainsKey(strId))
		mStringPoolRefs.Add(strId);

	BfIRValue strObject = CreateStringObjectValue(str, strId, define);
	
	*irValuePtr = strObject;
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
		curScope->mLabelNode->ToString(curScope->mLabel);

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
}

void BfModule::RestoreScoreState_LocalVariables()
{
	while (mCurMethodState->mCurScope->mLocalVarStart < (int)mCurMethodState->mLocals.size())
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

	if (!mCurMethodState->mLeftBlockUncond)
		EmitDeferredScopeCalls(true, mCurMethodState->mCurScope);

	RestoreScoreState_LocalVariables();

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

void BfModule::AddStackAlloc(BfTypedValue val, BfAstNode* refNode, BfScopeData* scopeData, bool condAlloca, bool mayEscape)
{
	//This was removed because we want the alloc to be added to the __deferred list if it's actually a "stack"
	// 'stack' in a head scopeData is really the same as 'scopeData', so use the simpler scopeData handling	
	/*if (mCurMethodState->mInHeadScope)
		isScopeAlloc = true;*/

	if (scopeData == NULL)
		return;
	
	auto checkBaseType = val.mType->ToTypeInstance();
	if ((checkBaseType != NULL) && (checkBaseType->IsObject()))
	{	
		bool hadDtorCall = false;
		while (checkBaseType != NULL)
		{
			if ((checkBaseType->mTypeDef->mDtorDef != NULL) /*&& (checkBaseType != mContext->mBfObjectType)*/)
			{
				bool isDynAlloc = (scopeData != NULL) && (mCurMethodState->mCurScope->IsDyn(scopeData));

				auto dtorMethodInstance = GetMethodInstance(checkBaseType, checkBaseType->mTypeDef->mDtorDef, BfTypeVector());
				
				BfIRValue useVal = val.mValue;
				useVal = mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapTypeInstPtr(checkBaseType));				

				if (isDynAlloc)
				{
					//
				}
				else if (condAlloca)
				{
					BF_ASSERT(!IsTargetingBeefBackend());
					BF_ASSERT(!isDynAlloc);
					auto valPtr = CreateAlloca(checkBaseType);
					mBfIRBuilder->CreateStore(useVal, valPtr);
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

			checkBaseType = checkBaseType->mBaseType;
		}
		return;
	}	
	
	//TODO: In the future we could be smarter about statically determining that our value hasn't escaped and eliding this		
	if (mayEscape)
	{
		// Is this worth it?
// 		if ((!IsOptimized()) && (val.mType->IsComposite()) && (!val.mType->IsValuelessType()) && (!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mIsResolveOnly))
// 		{			
// 			auto nullPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
// 			bool isDyn = mCurMethodState->mCurScope->IsDyn(scopeData);
// 			if (!isDyn)
// 			{
// 				int clearSize = val.mType->mSize;				
// 				BfModuleMethodInstance dtorMethodInstance = GetInternalMethod("MemSet");
// 				BF_ASSERT(dtorMethodInstance.mMethodInstance != NULL);
// 				SizedArray<BfIRValue, 1> llvmArgs;
// 				llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(nullPtrType)));
// 				llvmArgs.push_back(GetConstValue8(0xDD));
// 				llvmArgs.push_back(GetConstValue(clearSize));
// 				llvmArgs.push_back(GetConstValue32(val.mType->mAlign));
// 				llvmArgs.push_back(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0));
// 				AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
// 			}
// 			else
// 			{
// 				const char* funcName = "SetDeleted1";
// 				if (val.mType->mSize >= 16)
// 					funcName = "SetDeleted16";
// 				else if (val.mType->mSize >= 8)
// 					funcName = "SetDeleted8";
// 				else if (val.mType->mSize >= 4)
// 					funcName = "SetDeleted4";
// 
// 				BfModuleMethodInstance dtorMethodInstance = GetInternalMethod(funcName);
// 				BF_ASSERT(dtorMethodInstance.mMethodInstance != NULL);
// 				SizedArray<BfIRValue, 1> llvmArgs;
// 				llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(nullPtrType)));
// 				AddDeferredCall(dtorMethodInstance, llvmArgs, scopeData, refNode, true);
// 			}
// 		}
	}
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
					
					if ((fieldInstance.mDataIdx != -1) && (!mBfIRBuilder->mIgnoreWrites) && (!mCompiler->mIsResolveOnly))
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
						localVar->mIsAssigned = true;
				}
			}
		}
		checkTypeInstance = checkTypeInstance->mBaseType;
	}

	return localVar->mIsAssigned;
}

void BfModule::LocalVariableDone(BfLocalVariable* localVar, bool isMethodExit)
{
	//if (localVar->mAddr == NULL)

	/*if ((localVar->mValue) && (!localVar->mAddr) && (IsTargetingBeefBackend()))
	{
		if ((!localVar->mValue.IsConst()) && (!localVar->mValue.IsArg()) && (!localVar->mValue.IsFake()))
		{
			if (mCurMethodInstance->mMethodDef->mName == "FuncA")
				mBfIRBuilder->CreateLifetimeEnd(localVar->mValue);
		}
	}*/

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
			if ((!localVar->mIsAssigned) & (localVar->IsParam()))
				TryLocalVariableInit(localVar);

			// We may skip processing of local methods, so we won't know if it bind to any of our local variables or not
			bool deferFullAnalysis = mCurMethodState->GetRootMethodState()->mLocalMethodCache.size() != 0;
			//bool deferFullAnalysis = true;
			bool deferUsageWarning = deferFullAnalysis && mCompiler->IsAutocomplete();

			if (!localVar->mIsAssigned)
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
							if (fieldInstance.mMergedDataIdx != -1)
							{								
								int checkMask = 1 << fieldInstance.mMergedDataIdx;
								if ((localVar->mUnassignedFieldFlags & checkMask) != 0)
								{
									auto fieldDef = fieldInstance.GetFieldDef();
									if (fieldDef->mProtection != BfProtection_Hidden)										
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

		if ((mBfIRBuilder != NULL) && (mBfIRBuilder->DbgHasInfo()))
		{
			String fileName = bfParser->mFileName;

			for (int i = 0; i < (int)fileName.length(); i++)
			{
				if (fileName[i] == DIR_SEP_CHAR_ALT)
					fileName[i] = DIR_SEP_CHAR;
			}

			bfFileInstance->mDIFile = mBfIRBuilder->DbgCreateFile(fileName.Substring(slashPos + 1), fileName.Substring(0, slashPos));
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
	if (((flags & BfSrcPosFlag_NoSetDebugLoc) == 0) && (mBfIRBuilder->DbgHasInfo()) && (mCurMethodState != NULL))
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
	UpdateSrcPos(mCompiler->mBfObjectTypeDef->mTypeDeclaration, flags, debugLocOffset);
}

void BfModule::SetIllegalSrcPos(BfSrcPosFlags flags)
{
	if ((mBfIRBuilder->DbgHasInfo()) && (mCurMethodState != NULL))
	{
		if ((mCurMethodState->mCurScope->mDIInlinedAt) && (mCompiler->mOptions.IsCodeView()))
		{
			// Currently, CodeView does not record column positions so we can't use an illegal column position as an "invalid" marker
			mBfIRBuilder->SetCurrentDebugLocation(0, 0, mCurMethodState->mCurScope->mDIScope, mCurMethodState->mCurScope->mDIInlinedAt);
		}
		else
		{
			// Set to whatever it previously was but at column zero, which we will know to be illegal		
			mBfIRBuilder->SetCurrentDebugLocation(mCurFilePosition.mCurLine + 1, 0, mCurMethodState->mCurScope->mDIScope, mCurMethodState->mCurScope->mDIInlinedAt);
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

bool BfModule::CheckProtection(BfProtection protection, bool allowProtected, bool allowPrivate)
{
	if ((protection == BfProtection_Public) ||
		((protection == BfProtection_Protected) && (allowProtected)) ||
		((protection == BfProtection_Private) && (allowPrivate)))
		return true;
	if ((mAttributeState != NULL) && (mAttributeState->mCustomAttributes != NULL) && (mAttributeState->mCustomAttributes->Contains(mCompiler->mFriendAttributeTypeDef)))
		return true;
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

bool BfModule::CheckProtection(BfProtectionCheckFlags& flags, BfTypeInstance* memberOwner, BfProtection memberProtection, BfTypeInstance* lookupStartType)
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
		bool allowPrivate = (memberOwner->mTypeDef == curCheckType->mTypeDef) || (IsInnerType(curCheckType->mTypeDef, memberOwner->mTypeDef));
		if (allowPrivate)
			flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_AllowPrivate | BfProtectionCheckFlag_CheckedPrivate);
		else
			flags = (BfProtectionCheckFlags)(flags | BfProtectionCheckFlag_CheckedPrivate);
	}
	if ((flags & BfProtectionCheckFlag_AllowPrivate) != 0)
		return true;
	if (memberProtection == BfProtection_Protected)		
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
		return true;

	return false;
}

void BfModule::SetElementType(BfAstNode* astNode, BfSourceElementType elementType)
{
	if ((mCompiler->mResolvePassData != NULL) &&
		(mCompiler->mResolvePassData->mSourceClassifier != NULL) &&
		(astNode->IsFromParser( mCompiler->mResolvePassData->mParser)))
	{
		mCompiler->mResolvePassData->mSourceClassifier->SetElementType(astNode, elementType);
	}
}

void BfModule::SetHadVarUsage()
{
	mHadVarUsage = true;
	mHadBuildError = true;
}

BfError* BfModule::Fail(const StringImpl& error, BfAstNode* refNode, bool isPersistent)
{	
	BP_ZONE("BfModule::Fail");

	if (mIgnoreErrors)
	{
		mHadIgnoredError = true;
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

	if (mCurMethodInstance != NULL)
		mCurMethodInstance->mHasFailed = true;

	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsUnspecializedTypeVariation()))
		return NULL;

 	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsUnspecializedVariation))
		return NULL; // Ignore errors on unspecialized variations, they are always dups

 	if (!mHadBuildError)
		mHadBuildError = true;
	if (mParentModule != NULL)
		mParentModule->mHadBuildError = true;

	if (mCurTypeInstance != NULL)
		AddFailType(mCurTypeInstance);

	BfLogSysM("BfModule::Fail module %p type %p %s\n", this, mCurTypeInstance, error.c_str());
	
	String errorString = error;
	bool isWhileSpecializing = false;
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

	if ((mCurTypeInstance != NULL) && (mCurMethodState != NULL))
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

			// Propogatea the fail all the way to the main method (assuming we're in a local method or lambda)
			methodInstance->mHasFailed = true;

			bool isSpecializedMethod = ((methodInstance != NULL) && (!methodInstance->mIsUnspecialized) && (methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mMethodGenericArguments.size() != 0));
			if (isSpecializedMethod)
			{
				//auto unspecializedMethod = &mCurMethodInstance->mMethodInstanceGroup->mMethodSpecializationMap.begin()->second;
				auto unspecializedMethod = methodInstance->mMethodInstanceGroup->mDefault;
				if (unspecializedMethod->mHasFailed)
					return NULL; // At least SOME error has already been reported
			}

			if (isSpecializedMethod)
			{
				errorString += StrFormat("\n  while specializing method '%s'", MethodToString(methodInstance).c_str());
				isWhileSpecializing = true;
				isWhileSpecializingMethod = true;
			}
			else if ((methodInstance != NULL) && (methodInstance->mIsForeignMethodDef))
			{
				errorString += StrFormat("\n  while implementing default interface method '%s'", MethodToString(methodInstance).c_str());
				isWhileSpecializing = true;
				isWhileSpecializingMethod = true;
			}
			else if ((mCurTypeInstance->IsGenericTypeInstance()) && (!mCurTypeInstance->IsUnspecializedType()))
			{
				errorString += StrFormat("\n  while specializing type '%s'", TypeToString(mCurTypeInstance).c_str());
				isWhileSpecializing = true;
			}

			checkMethodState = checkMethodState->mPrevMethodState;
		}

		// Keep in mind that all method specializations with generic type instances as its method generic params
		//  need to be deferred because the specified generic type instance might get deleted at the end of the 
		//  compilation due to no longer having indirect references removed, and we have to ignore errors from
		//  those method specializations if that occurs
		if (isWhileSpecializing)
		{
			BfError* bfError = mCompiler->mPassInstance->DeferFail(errorString, refNode);
			if (bfError != NULL)
				bfError->mIsWhileSpecializing = isWhileSpecializing;
			return bfError;
		}
	}

	if (!mHadBuildError)
		mHadBuildError = true;

	// Check mixins
	{
		auto checkMethodInstance = mCurMethodState;
		while (checkMethodInstance != NULL)
		{
			auto rootMixinState = checkMethodInstance->GetRootMixinState();
			if (rootMixinState != NULL)
			{
				BfError* bfError = mCompiler->mPassInstance->Fail(StrFormat("Failed to inject mixin '%s'", MethodToString(rootMixinState->mMixinMethodInstance).c_str()), rootMixinState->mSource);
				mCompiler->mPassInstance->MoreInfo(errorString, refNode);

				auto mixinState = checkMethodInstance->mMixinState;
				while ((mixinState != NULL) && (mixinState->mPrevMixinState != NULL))
				{
					mCompiler->mPassInstance->MoreInfo(StrFormat("Injected from mixin '%s'", MethodToString(mixinState->mPrevMixinState->mMixinMethodInstance).c_str()), mixinState->mSource);
					mixinState = mixinState->mPrevMixinState;
				}

				return bfError;
			}

			checkMethodInstance = checkMethodInstance->mPrevMethodState;
		}
	}

	BfError* bfError = NULL;
	if (refNode == NULL)
		bfError = mCompiler->mPassInstance->Fail(errorString);
	else
	{		
		bfError = mCompiler->mPassInstance->Fail(errorString, refNode);
	}		
	if (bfError != NULL)
	{
		bfError->mIsPersistent = isPersistent;
		bfError->mIsWhileSpecializing = isWhileSpecializing;

		if ((mCurMethodState != NULL) && (mCurMethodState->mDeferredCallEmitState != NULL) && (mCurMethodState->mDeferredCallEmitState->mCloseNode != NULL))
			mCompiler->mPassInstance->MoreInfo("Error during deferred statement handling", mCurMethodState->mDeferredCallEmitState->mCloseNode);
	}

	return bfError;
}

BfError* BfModule::FailAfter(const StringImpl& error, BfAstNode* refNode)
{
	if (mIgnoreErrors)
		return NULL;

	if (refNode != NULL)
		refNode = BfNodeToNonTemporary(refNode);

	mHadBuildError = true;	
	return mCompiler->mPassInstance->FailAfter(error, refNode);
}

BfError* BfModule::Warn(int warningNum, const StringImpl& warning, BfAstNode* refNode, bool isPersistent)
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
			return NULL;
		if (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend)
			return NULL; // No ctorCalcAppend warnings
	}
	if ((mCurTypeInstance != NULL) && (mCurTypeInstance->IsSpecializedType()))
		return NULL;

	if (refNode != NULL)
		refNode = BfNodeToNonTemporary(refNode);

	if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL))
	{
		// We used to bubble up warnings into the mixin injection site, BUT
		//  we are now assuming any relevant warnings will be given at the declaration site
		return NULL;
// 		AddFailType(mCurTypeInstance);
// 
// 		BfError* bfError = mCompiler->mPassInstance->Warn(warningNum, "Warning while injecting mixin", mCurMethodState->mMixinState->GetRoot()->mSource);
// 		if (bfError == NULL)
// 			return NULL;
// 		mHadBuildWarning = true;
// 		return mCompiler->mPassInstance->MoreInfo(warning, refNode);	
	}

	BfError* bfError = mCompiler->mPassInstance->WarnAt(warningNum, warning, refNode->GetSourceData(), refNode->GetSrcStart(), refNode->GetSrcLength());
	if (bfError != NULL)
	{
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
			}				
		}
	}
	return bfError;
}

void BfModule::CheckRangeError(BfType* type, BfAstNode* refNode)
{
	if (mBfIRBuilder->mOpFailed)
		Fail(StrFormat("Result out of range for type '%s'", TypeToString(type).c_str()), refNode);
}

void BfModule::NotImpl(BfAstNode* astNode)
{
	Fail("INTERNAL ERROR: Not implemented", astNode);
}


bool BfModule::CheckAccessMemberProtection(BfProtection protection, BfType* memberType)
{
	bool allowPrivate = (memberType == mCurTypeInstance) || (IsInnerType(mCurTypeInstance, memberType));
	if (!allowPrivate)
		allowPrivate |= memberType->IsInterface();
	bool allowProtected = allowPrivate;// allowPrivate || TypeIsSubTypeOf(mCurTypeInstance, memberType->ToTypeInstance());
	if (!CheckProtection(protection, allowProtected, allowPrivate))
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
		auto genericTypeInstance = (BfGenericTypeInstance*) memberTypeInstance;
		for (auto typeGenericArg : genericTypeInstance->mTypeGenericArguments)
		{
			if (!CheckDefineMemberProtection(protection, typeGenericArg))
				return false;
		}
	}

	return true;
}

void BfModule::AddDependency(BfType* usedType, BfType* usingType, BfDependencyMap::DependencyDependencyFlag flags)
{	
	if (usedType == usingType)
		return;	

	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mIsAutocompleteMethod))
		return;

	if (usedType->IsSpecializedByAutoCompleteMethod())
		return;

	if ((mCurMethodState != NULL) && (mCurMethodState->mHotDataReferenceBuilder != NULL) && (usedType != mCurTypeInstance) &&
		((flags & (BfDependencyMap::DependencyFlag_ReadFields | BfDependencyMap::DependencyFlag_LocalUsage | BfDependencyMap::DependencyFlag_Allocates)) != 0))
	{
		bool addType = true;
		auto checkType = usedType;

		while (true)
		{
			if ((checkType->IsPointer()) || (checkType->IsRef()) || (checkType->IsSizedArray()))
				checkType = checkType->GetUnderlyingType();			
			else if (checkType->IsArray())
			{
				checkType = ((BfGenericTypeInstance*)checkType)->mTypeGenericArguments[0];
			}
			else
				break;
		}
		
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
			auto hotTypeVersion = checkTypeInst->mHotTypeData->GetLatestVersion();
			BF_ASSERT(hotTypeVersion != NULL);

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

	//BP_ZONE("BfModule::AddDependency");
	
	/*if (usedType->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)usedType;
		AddDependency(methodRefType->mMethodRef->GetOwner(), usingType, flags);
		return;
	}*/

	// Why in the world were we doing this?
	//  This caused functions to get immediately deleted since they appear to have no references ever....
// 	if (usedType->IsFunction())
// 	{
// 		usedType = ResolveTypeDef(mCompiler->mFunctionTypeDef);
// 	}

	if ((!mCompiler->mIsResolveOnly) && (mIsReified))
	{
		auto usingModule = usingType->GetModule();		
		BfModule* usedModule;
		if (usedType->IsFunction())
		{
			auto typeInst = usedType->ToTypeInstance();
			if (typeInst->mBaseType == NULL)
				PopulateType(typeInst);
			usedModule = typeInst->mBaseType->GetModule();
		}
		else
			usedModule = usedType->GetModule();

		if ((usingModule != NULL) && (!usingModule->mIsSpecialModule) &&
			(usedModule != NULL) && (!usedModule->mIsSpecialModule))
		{			
			usingModule->mModuleRefs.Add(usedModule);
		}
	}
		
	if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodInfoEx != NULL) && (flags != BfDependencyMap::DependencyFlag_MethodGenericArg))
	{
		// When we are specializing a method, usage of that specialized type is already handled with DependencyFlag_MethodGenericArg
		for (auto genericArg : mCurMethodInstance->mMethodInfoEx->mMethodGenericArguments)
			if (genericArg == usedType)
				return;
	}


	auto underlyingType = usedType->GetUnderlyingType();
	if (underlyingType != NULL) // Not really a "GenericArg", but... same purpose.
		AddDependency(underlyingType, usingType, BfDependencyMap::DependencyFlag_GenericArgRef);

	BfDependedType* checkDType = usedType->ToDependedType();
	if (checkDType == NULL)
		return;

	if ((usedType->mRebuildFlags & BfTypeRebuildFlag_AwaitingReference) != 0)
		mContext->MarkAsReferenced(checkDType);

	checkDType->mDependencyMap.AddUsedBy(usingType, flags);
	if (checkDType->IsGenericTypeInstance())
	{
		auto genericTypeInstance = (BfGenericTypeInstance*) checkDType;
		for (auto genericArg : genericTypeInstance->mTypeGenericArguments)
		{
			AddDependency(genericArg, usingType, BfDependencyMap::DependencyFlag_GenericArgRef);
		}
	}	
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
		if (checkTypeInst->mTypeDef == mCompiler->mAttributeTypeDef)
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

	BF_ASSERT((headModule->mOwnedTypeInstances.size() > 0) || (mModuleName == "vdata"));

	if (headModule->mOwnedTypeInstances.size() > 0)
	{
		auto typeInst = headModule->mOwnedTypeInstances[0];
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
	bool runtimeChecks = mCompiler->mOptions.mRuntimeChecks;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		runtimeChecks = BfTypeOptions::Apply(runtimeChecks, typeOptions->mRuntimeChecks);
	return runtimeChecks ? BfCheckedKind_Checked : BfCheckedKind_Unchecked;
}

void BfModule::AddFailType(BfTypeInstance* typeInstance)
{
	mContext->mFailTypes.Add(typeInstance);
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

	if (!field->mIsExtern)
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
		StringT<128> staticVarName;
		BfMangler::Mangle(staticVarName, mCompiler->GetMangleKind(), fieldInstance);
		if (!fieldType->IsValuelessType())
		{
			BfIRValue globalVar = mBfIRBuilder->CreateGlobalVariable(					
				mBfIRBuilder->MapType(fieldType, BfIRPopulateType_Eventually_Full),
				false,
				BfIRLinkageType_External,
				initValue,
				staticVarName,					
				isThreadLocal);
			
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
				fieldType = ResolveTypeRef(fieldDef->mTypeRef);
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
			Fail("Extern consts must be pointer types", fieldDef->mFieldDeclaration->mTypeRef);
		}

		if (fieldDef->mInitializer != NULL)
		{
			Fail("Extern consts cannot have initializers", fieldDef->mFieldDeclaration->mNameNode);
		}
	}
	else if (fieldDef->mInitializer == NULL)
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
			Fail("Const requires initializer", fieldDef->mFieldDeclaration->mNameNode);			
		}
	}
	else
	{		
		SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);

		SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, NULL);
		
		BfConstResolver constResolver(this);

		BfMethodState methodState;
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
		methodState.mTempKind = BfMethodState::TempKind_Static;

		auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();

		if (fieldType->IsVar())
		{
			auto initValue = GetFieldInitializerValue(fieldInstance, fieldDef->mInitializer, fieldDef, fieldType);
			if (!initValue)
			{
				AssertErrorState();
				initValue = GetDefaultTypedValue(mContext->mBfObjectType);
			}
			if (fieldInstance != NULL)
			{
				fieldInstance->mResolvedType = initValue.mType;
			}
			constValue = initValue.mValue;
		}
		else
		{
			auto uncastedInitValue = GetFieldInitializerValue(fieldInstance, fieldDef->mInitializer, fieldDef, fieldType);
			constValue = uncastedInitValue.mValue;
		}

		mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	}

	if (fieldInstance != NULL)
	{
		fieldType = fieldInstance->GetResolvedType();
		if ((fieldType == NULL) || (fieldType->IsVar()))
		{
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
	bool isDeclType = BfNodeDynCastExact<BfDeclTypeRef>(field->mFieldDeclaration->mTypeRef) != NULL;

	auto fieldType = fieldInstance->GetResolvedType();
	if ((field->mIsConst) && (!isDeclType))
	{
		ResolveConstField(typeInstance, fieldInstance, field);
		return fieldInstance->GetResolvedType();
	}

	bool staticOnly = (field->mIsStatic) && (!isDeclType);
	
	if (!fieldInstance->mIsInferredType)
		return fieldType;
	if (!fieldType->IsVar())
		return fieldType;	
	
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);

	BfTypeState typeState(mCurTypeInstance, mContext->mCurTypeState);
	SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState, mContext->mCurTypeState->mTypeInstance != typeInstance);

	auto typeDef = typeInstance->mTypeDef;

	if ((!field->mIsStatic) && (typeDef->mIsStatic))
	{
		AssertErrorState();
		SetHadVarUsage();
		return GetPrimitiveType(BfTypeCode_Var);
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
			
			SetHadVarUsage();
			return GetPrimitiveType(BfTypeCode_Var);
		}
	}
	mContext->mFieldResolveReentrys.push_back(fieldInstance);

	AutoPopBack<decltype (mContext->mFieldResolveReentrys)> popTypeResolveReentry(&mContext->mFieldResolveReentrys);
	SetAndRestoreValue<bool> prevResolvingVar(typeInstance->mResolvingVarField, true);	
	SetAndRestoreValue<bool> prevCtxResolvingVar(mContext->mResolvingVarField, true);	
	
	if ((field->mInitializer == NULL) && (!isDeclType))
	{
		if ((field->mTypeRef->IsA<BfVarTypeReference>()) || (field->mTypeRef->IsA<BfLetTypeReference>()))
			Fail("Implicitly-typed fields must be initialized", field->GetRefNode());
		SetHadVarUsage();
		return GetPrimitiveType(BfTypeCode_Var);
	}

	BfType* resolvedType = NULL;
	if (!hadInferenceCycle)
	{		
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
			resolvedType = valueFromExpr.mType;
		}

		mBfIRBuilder->SetInsertPoint(prevInsertBlock);
	}

	if (resolvedType == NULL)
		return GetPrimitiveType(BfTypeCode_Var);
				
	fieldInstance->SetResolvedType(resolvedType);

	if (field->mIsStatic)
	{
		
	}
	else if (fieldInstance->mDataIdx <= 0)
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
		int fieldDataCount = 1;
		if (fieldType->IsStruct())
		{
			auto structTypeInst = fieldType->ToTypeInstance();
			fieldDataCount = structTypeInst->mMergedFieldDataCount;
		}		

		//TODO: Under what circumstances could 'thisVariable' be NULL?
		auto thisVariable = GetThisVariable();
		if (thisVariable != NULL)
		{
			for (int i = 0; i < fieldDataCount; i++)
				mCurMethodState->LocalDefined(thisVariable, fieldInstance->mMergedDataIdx + i);
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
			if (customAttr.mType->ToTypeInstance()->mTypeDef == mCompiler->mThreadStaticAttributeTypeDef)
				return true;			
		}
	}
	return false;
}

BfTypedValue BfModule::GetFieldInitializerValue(BfFieldInstance* fieldInstance, BfExpression* initializer, BfFieldDef* fieldDef, BfType* fieldType)
{	
	if (fieldDef == NULL)
		fieldDef = fieldInstance->GetFieldDef();
	if (fieldType == NULL)
		fieldType = fieldInstance->GetResolvedType();
	if (initializer == NULL)
	{
		if (fieldDef == NULL)
			return BfTypedValue();
		initializer = fieldDef->mInitializer;
	}

	BfTypedValue result;
	if (initializer == NULL)
	{
		result = GetDefaultTypedValue(fieldInstance->mResolvedType);
	}
	else
	{
		if ((mCompiler->mIsResolveOnly) && (mCompiler->mResolvePassData->mSourceClassifier != NULL) && (initializer->IsFromParser(mCompiler->mResolvePassData->mParser)))
		{
			mCompiler->mResolvePassData->mSourceClassifier->SetElementType(initializer, BfSourceElementType_Normal);
			mCompiler->mResolvePassData->mSourceClassifier->VisitChildNoRef(initializer);
		}

		if ((mCurTypeInstance->IsPayloadEnum()) && (fieldDef->IsEnumCaseEntry()))
		{
			Fail("Enums with payloads cannot have explicitly-defined discriminators", initializer);
			BfConstResolver constResolver(this);
			constResolver.Resolve(initializer);
			return GetDefaultTypedValue(fieldType);
		}
		else if (fieldDef->mIsConst)
		{
			BfTypeState typeState;
			typeState.mCurTypeDef = fieldDef->mDeclaringType;
			SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

			BfConstResolver constResolver(this);
			if (fieldType->IsVar())
				return constResolver.Resolve(initializer);
			else
				return constResolver.Resolve(initializer, fieldType);
		}
		
		if (fieldType->IsVar())
			result = CreateValueFromExpression(initializer, NULL, (BfEvalExprFlags)(BfEvalExprFlags_NoValueAddr | BfEvalExprFlags_FieldInitializer));
		else
			result = CreateValueFromExpression(initializer, fieldType, (BfEvalExprFlags)(BfEvalExprFlags_NoValueAddr | BfEvalExprFlags_FieldInitializer));
	}

	if (fieldInstance != NULL)
		MarkFieldInitialized(fieldInstance);

	return result;
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
					exChecks->push_back(ifaceInst.mInterfaceType);
					continue;
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
		auto unboundType = ResolveTypeDef(mCurTypeInstance->mTypeDef, genericArgs, BfPopulateType_Declaration);
		typeMatches.push_back(unboundType->mTypeId);
	}	

	if (mCurTypeInstance->IsBoxed())
	{
		BfBoxedType* boxedType = (BfBoxedType*)mCurTypeInstance;
		BfTypeInstance* innerType = boxedType->mElementType;
		
		FindSubTypes(innerType, &typeMatches, &exChecks, isInterfacePass);

		if (innerType->IsTypedPrimitive())
		{
			auto underlyingType = innerType->GetUnderlyingType();
			typeMatches.push_back(underlyingType->mTypeId);
		}

		auto innerTypeInst = innerType->ToTypeInstance();
		if ((innerTypeInst->mTypeDef == mCompiler->mSizedArrayTypeDef) ||
			(innerTypeInst->mTypeDef == mCompiler->mPointerTTypeDef) ||
			(innerTypeInst->mTypeDef == mCompiler->mMethodRefTypeDef))
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

void BfModule::EmitEquals(BfTypedValue leftValue, BfTypedValue rightValue, BfIRBlock exitBB)
{
	BfExprEvaluator exprEvaluator(this);
	exprEvaluator.mExpectingType = mCurMethodInstance->mReturnType;
	exprEvaluator.PerformBinaryOperation((BfAstNode*)NULL, (BfAstNode*)NULL, BfBinaryOp_Equality, NULL, BfBinOpFlag_None, leftValue, rightValue);
	BfTypedValue result = exprEvaluator.GetResult();
	if (result)
	{
		auto nextBB = mBfIRBuilder->CreateBlock("next");
		mBfIRBuilder->CreateCondBr(result.mValue, nextBB, exitBB);
		mBfIRBuilder->AddBlock(nextBB);
		mBfIRBuilder->SetInsertPoint(nextBB);
	}
}

void BfModule::CreateFakeCallerMethod(const String& funcName)
{	
	BF_ASSERT(mCurMethodInstance->mIRFunction);

	auto voidType = mBfIRBuilder->MapType(GetPrimitiveType(BfTypeCode_None));
	SizedArray<BfIRType, 4> paramTypes;
	BfIRFunctionType funcType = mBfIRBuilder->CreateFunctionType(voidType, paramTypes);
	BfIRFunction func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_Internal, "FORCELINK_" + funcName);	
	mBfIRBuilder->SetActiveFunction(func);
	auto entryBlock = mBfIRBuilder->CreateBlock("main", true);
	mBfIRBuilder->SetInsertPoint(entryBlock);

	SizedArray<BfIRValue, 8> args;
	BfExprEvaluator exprEvaluator(this);

	if (mCurMethodInstance->HasThis())
	{
		auto thisValue = GetDefaultTypedValue(mCurMethodInstance->GetOwner(), true);
		exprEvaluator.PushThis(NULL, thisValue, mCurMethodInstance, args);
	}

	for (int paramIdx = 0; paramIdx < mCurMethodInstance->GetParamCount(); paramIdx++)
	{
		auto paramType = mCurMethodInstance->GetParamType(paramIdx);
		if (paramType->IsValuelessType())
			continue;				
		exprEvaluator.PushArg(GetDefaultTypedValue(paramType, true), args);
	}

	mBfIRBuilder->CreateCall(mCurMethodInstance->mIRFunction, args);
	mBfIRBuilder->CreateRetVoid();
}

void BfModule::CreateValueTypeEqualsMethod()
{
	if (mCurMethodInstance->mIsUnspecialized)
		return;

	if (mCurTypeInstance->IsTypedPrimitive())
		return;

	if (mBfIRBuilder->mIgnoreWrites)
		return;

	BF_ASSERT(!mCurTypeInstance->IsBoxed());

	auto compareType = mCurMethodInstance->mParams[0].mResolvedType;	
	bool isValid = true;

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

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
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

	if (compareType->IsSizedArray())
	{
		auto sizedArrayType = (BfSizedArrayType*)compareType;
		
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
			BfTypedValue leftValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, GetDefaultValue(intPtrType), loadedItr), sizedArrayType->mElementType, BfTypedValueKind_Addr);
			BfTypedValue rightValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[1]->mValue, GetDefaultValue(intPtrType), loadedItr), sizedArrayType->mElementType, BfTypedValueKind_Addr);
			EmitEquals(leftValue, rightValue, exitBB);
			auto incValue = mBfIRBuilder->CreateAdd(loadedItr, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1));
			mBfIRBuilder->CreateStore(incValue, itr);
			mBfIRBuilder->CreateBr(loopBB);

			mBfIRBuilder->SetInsertPoint(doneBB);
		}
		else
		{
			for (int dataIdx = 0; dataIdx < sizedArrayType->mElementCount; dataIdx++)
			{
				BfTypedValue leftValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, dataIdx), sizedArrayType->mElementType, BfTypedValueKind_Addr);
				BfTypedValue rightValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[1]->mValue, 0, dataIdx), sizedArrayType->mElementType, BfTypedValueKind_Addr);
				EmitEquals(leftValue, rightValue, exitBB);
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
			EmitEquals(leftValue, rightValue, exitBB);
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

		EmitEquals(leftValue, rightValue, exitBB);
		
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
						
						EmitEquals(leftTuple, rightTuple, exitBB);
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
			BfTypedValue leftUnionTypedVal = ExtractValue(leftTypedVal, NULL, 1);
			BfTypedValue rightTypedVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);
			BfTypedValue rightUnionTypedVal = ExtractValue(rightTypedVal, NULL, 1);

			EmitEquals(leftUnionTypedVal, rightUnionTypedVal, exitBB);
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
						
			BfTypedValue leftValue = ExtractValue(leftTypedVal, fieldInstance, fieldInstance->mDataIdx);			
			BfTypedValue rightValue = ExtractValue(rightTypedVal, fieldInstance, fieldInstance->mDataIdx);

			if (!fieldInstance->mResolvedType->IsComposite())
			{
				leftValue = LoadValue(leftValue);
				rightValue = LoadValue(rightValue);
			}

			EmitEquals(leftValue, rightValue, exitBB);
		}

		auto baseTypeInst = compareTypeInst->mBaseType;
		if ((baseTypeInst != NULL) && (baseTypeInst->mTypeDef != mCompiler->mValueTypeTypeDef))
		{
			BfTypedValue leftOrigValue(mCurMethodState->mLocals[0]->mValue, compareTypeInst, true);
			BfTypedValue rightOrigValue(mCurMethodState->mLocals[1]->mValue, compareTypeInst, true);
			BfTypedValue leftValue = Cast(NULL, leftOrigValue, baseTypeInst);
			BfTypedValue rightValue = Cast(NULL, rightOrigValue, baseTypeInst);
			EmitEquals(leftValue, rightValue, exitBB);
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

	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	BfType* classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);
		
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
	StringT<128> classVDataName;
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
				auto moduleMethodInst = GetMethodInstanceAtIdx(methodInstance->mMethodInstanceGroup->mOwner, methodInstance->mMethodInstanceGroup->mMethodIdx);
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
	BfIRValue extVTableConst = mBfIRBuilder->CreateConstArray(extVTableType, vData);
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
	
	BfIRValue globalVariable;
	
	BfIRValue* globalVariablePtr = NULL;
	if (mTypeDataRefs.TryGetValue(type, &globalVariablePtr))
	{
		globalVariable = *globalVariablePtr;
		if (globalVariable)
			return globalVariable;
	}
	
	auto typeTypeDef = ResolveTypeDef(mCompiler->mTypeTypeDef);
	auto typeTypeInst = typeTypeDef->ToTypeInstance();
	auto typeInstance = type->ToTypeInstance();

	StringT<128> typeDataName;
	if (typeInstance != NULL)
	{
		BfMangler::MangleStaticFieldName(typeDataName, mCompiler->GetMangleKind(), typeInstance, "sBfTypeData");
	}
	else
	{
		typeDataName += "sBfTypeData.";
		BfMangler::Mangle(typeDataName, mCompiler->GetMangleKind(), type);
	}

	BfLogSysM("Creating TypeData %s\n", typeDataName.c_str());
				
	globalVariable = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(typeTypeInst, BfIRPopulateType_Full), true, BfIRLinkageType_External, BfIRValue(), typeDataName);

	mTypeDataRefs[type] = globalVariable;	
	return globalVariable;
}

BfIRValue BfModule::CreateTypeData(BfType* type, Dictionary<int, int>& usedStringIdMap, bool forceReflectFields, bool needsTypeData, bool needsTypeNames, bool needsVData)
{
	if ((mCompiler->IsHotCompile()) && (!type->mDirty))
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
		else if (type->IsSizedArray())
			typeDataSource = ResolveTypeDef(mCompiler->mReflectSizedArrayType)->ToTypeInstance();
		else
			typeDataSource = mContext->mBfTypeType;
				
		if ((!mTypeDataRefs.ContainsKey(typeDataSource)) && (typeDataSource != type))
		{
			CreateTypeData(typeDataSource, usedStringIdMap, false, true, needsTypeNames, true);
		}				

		typeTypeData = CreateClassVDataGlobal(typeDataSource);
	}	
	else
		typeTypeData = CreateClassVDataGlobal(typeInstanceType->ToTypeInstance());

	BfType* longType = GetPrimitiveType(BfTypeCode_Int64);
	BfType* intType = GetPrimitiveType(BfTypeCode_Int32);
	BfType* shortType = GetPrimitiveType(BfTypeCode_Int16);
	BfType* byteType = GetPrimitiveType(BfTypeCode_Int8);	

	BfType* typeIdType = intType;

	auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
	auto voidPtrIRType = mBfIRBuilder->MapType(voidPtrType);
	auto voidPtrPtrIRType = mBfIRBuilder->GetPointerTo(voidPtrIRType);
	auto voidPtrNull = GetDefaultValue(voidPtrType);

	SizedArray<BfIRValue, 4> typeValueParams;
	GetConstClassValueParam(typeTypeData, typeValueParams);
	
	BfIRValue objectData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(mContext->mBfObjectType, BfIRPopulateType_Full), typeValueParams);

	StringT<128> typeDataName;
	if ((typeInstance != NULL) && (!typeInstance->IsTypeAlias()))
	{
		BfMangler::MangleStaticFieldName(typeDataName, mCompiler->GetMangleKind(), typeInstance, "sBfTypeData");
		if (typeInstance->mTypeDef->IsGlobalsContainer())
			typeDataName += "@" + typeInstance->mTypeDef->mProject->mName;
	}
	else
	{
		typeDataName += "sBfTypeData.";
		BfMangler::Mangle(typeDataName, mCompiler->GetMangleKind(), type);
	}
	
	int typeCode = BfTypeCode_None;		

	if (typeInstance != NULL)
	{
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
	
	if (type->IsObject())
	{
		typeFlags |= BfTypeFlags_Object;	
		BfMethodInstance* methodInstance = typeInstance->mVirtualMethodTable[mCompiler->GetVTableMethodOffset() + 0].mImplementingMethod;
		if (methodInstance->GetOwner() != mContext->mBfObjectType)
			typeFlags |= BfTypeFlags_HasDestructor;
	}
	if (type->IsStruct())	
		typeFlags |= BfTypeFlags_Struct;
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
	if (type->IsSplattable())
		typeFlags |= BfTypeFlags_Splattable;
	if (type->IsUnion())
		typeFlags |= BfTypeFlags_Union;
	if (type->IsDelegate())
		typeFlags |= BfTypeFlags_Delegate;
	if (type->WantsGCMarking())
		typeFlags |= BfTypeFlags_WantsMarking;

	int memberDataOffset = 0;
	if (typeInstance != NULL)
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
	SizedArray<BfIRValue, 8> typeDataParams =
	{
		objectData,
		GetConstValue(type->mSize, intType), // mSize
		GetConstValue(type->mTypeId, typeIdType), // mTypeId
		GetConstValue(typeFlags, intType), // mTypeFlags
		GetConstValue(memberDataOffset, intType), // mMemberDataOffset		
		GetConstValue(typeCode, byteType), // mTypeCode
		GetConstValue(type->mAlign, byteType),
	};
	auto typeData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(mContext->mBfTypeType, BfIRPopulateType_Full), typeDataParams);
		
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
				auto pointerTypeData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectPointerType, BfIRPopulateType_Full), pointerTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectPointerType), true,
					BfIRLinkageType_External, pointerTypeData, typeDataName);
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
				auto sizedArrayTypeData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectSizedArrayType, BfIRPopulateType_Full), sizedArrayTypeDataParms);
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(reflectSizedArrayType), true,
					BfIRLinkageType_External, sizedArrayTypeData, typeDataName);
				typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));
			}
			else
			{
				typeDataVar = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapTypeInst(mContext->mBfTypeType), true,
					BfIRLinkageType_External, typeData, typeDataName);
			}
		}
		else
			typeDataVar = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(mContext->mBfTypeType));
		
		mTypeDataRefs[typeInstance] = typeDataVar;

		return typeDataVar;
	}
	
	// Reserve position
	mTypeDataRefs[typeInstance] = BfIRValue();
	
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);
	
	BfTypeDef* typeDef = typeInstance->mTypeDef;
	StringT<128> mangledName;
	BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), typeInstance);
	
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

	SizedArray<BfIRValue, 32> vData;
	BfIRValue classVDataVar;
	String classVDataName;
	if (typeInstance->mSlotNum >= 0)
	{
		// For interfaces we ONLY emit the slot num		
		int virtSlotIdx = typeInstance->mSlotNum + 1 + mCompiler->GetDynCastVDataCount();

		StringT<128> slotVarName;
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

				if (!typeInstance->IsTypeMemberAccessible(interfaceEntry.mDeclaringType, mProject))
					continue;

				_InterfaceMatchEntry* matchEntry = NULL;
				if (interfaceMap.TryGetValue(interfaceEntry.mInterfaceType, &matchEntry))
				{
					auto prevEntry = matchEntry->mEntry;
					bool isBetter = TypeIsSubTypeOf(checkTypeInst, matchEntry->mEntrySource);
					bool isWorse = TypeIsSubTypeOf(matchEntry->mEntrySource, checkTypeInst);
					if (forceInterfaceSet)
					{
						isBetter = true;
						isWorse = false;
					}
					if (isBetter == isWorse)								
						CompareDeclTypes(interfaceEntry.mDeclaringType, prevEntry->mDeclaringType, isBetter, isWorse);
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

		if ((!typeInstance->IsInterface()) && (typeInstance->mVirtualMethodTableSize > 0) && (needsVData))
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
					if ((methodInstance == NULL) || (!typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject)))
					{
						if (origVTable.empty())
							origVTable = typeInstance->mVirtualMethodTable;

						BfMethodInstance* declMethodInstance = entry.mDeclaringMethod;
						if (typeInstance->IsTypeMemberAccessible(declMethodInstance->mMethodDef->mDeclaringType, mProject))
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

						if (!typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject))
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
							BF_ASSERT(typeInstance->IsTypeMemberAccessible(methodInstance->mMethodDef->mDeclaringType, mProject));
							moduleMethodInst = GetMethodInstanceAtIdx(methodInstance->mMethodInstanceGroup->mOwner, methodInstance->mMethodInstanceGroup->mMethodIdx);
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

			bool makeEmpty = false;
			if (!typeInstance->IsTypeMemberAccessible(interfaceEntry->mDeclaringType, mProject))
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
						auto moduleMethodInst = ReferenceExternalMethodInstance(methodInstance);
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
						auto moduleMethodInst = ReferenceExternalMethodInstance(methodInstance);
						auto funcPtr = mBfIRBuilder->CreateBitCast(moduleMethodInst.mFunc, voidPtrIRType);

						int idx = checkIFace.mStartVirtualIdx + ifaceMethodInstance->mVirtualTableIdx;
						vData[iFaceMethodStartIdx + idx] = funcPtr;
					}

				}
			}
		}
				
		if ((needsVData) && (!typeInstance->mTypeDef->mIsStatic))
		{
			BfIRValue ifaceMethodExtVar;
			if (!ifaceMethodExtData.IsEmpty())
			{
				StringT<128> classVDataName;
				BfMangler::MangleStaticFieldName(classVDataName, mCompiler->GetMangleKind(), typeInstance, "bf_hs_replace_IFaceExt");
				auto arrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr), (int)ifaceMethodExtData.size());
				ifaceMethodExtVar = mBfIRBuilder->CreateGlobalVariable(arrayType, true,
					BfIRLinkageType_External, BfIRValue(), classVDataName);

				BfIRType extVTableType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)ifaceMethodExtData.size());
				BfIRValue extVTableConst = mBfIRBuilder->CreateConstArray(extVTableType, ifaceMethodExtData);
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

					if (endVirtualIdx > ifaceMethodExtStart)
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
		typeNameConst = GetStringObjectValue(typeDef->mName->mString, true);
		namespaceConst = GetStringObjectValue(typeDef->mNamespace.ToString(), true);
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

	enum ReflectKind
	{
		ReflectKind_None = 0,
		ReflectKind_Type = 1,
		ReflectKind_NonStaticFields = 2,
		ReflectKind_StaticFields = 4,
		ReflectKind_DefaultConstructor = 8,
		ReflectKind_Constructors = 0x10,
		ReflectKind_StaticMethods = 0x20,
		ReflectKind_Methods = 0x40,
		ReflectKind_User = 0x80,
		ReflectKind_All = 0x7F,

		ReflectKind_ApplyToInnerTypes = 0x80
	};

	ReflectKind reflectKind = ReflectKind_Type;

	auto _GetReflectUser = [&](BfTypeInstance* attrType)
	{
		ReflectKind reflectKind = ReflectKind_None;

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
								reflectKind = (ReflectKind)(reflectKind | (ReflectKind)constant->mInt32);
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
						reflectKind = (ReflectKind)(reflectKind | ReflectKind_User);
				}
			}
		}

		return reflectKind;
	};

	auto _GetReflectUserFromDirective = [&](BfAttributeDirective* attributesDirective, BfAttributeTargets attrTarget)
	{
		ReflectKind reflectKind = ReflectKind_None;
		auto customAttrs = GetCustomAttributes(attributesDirective, attrTarget);
		if (customAttrs != NULL)
		{
			for (auto& attr : customAttrs->mAttributes)
			{
				reflectKind = (ReflectKind)(reflectKind | _GetReflectUser(attr.mType));
			}
		}
		return reflectKind;
	};

	//
	{

		auto checkTypeInstance = typeInstance;
		while (checkTypeInstance != NULL)
		{
			if (checkTypeInstance->mCustomAttributes != NULL)
			{
				auto checkReflectKind = ReflectKind_None;

				for (auto& customAttr : checkTypeInstance->mCustomAttributes->mAttributes)
				{					
					if (customAttr.mType->mTypeDef->mName->ToString() == "ReflectAttribute")
					{
						if (customAttr.mCtorArgs.size() > 0)
						{
							auto constant = checkTypeInstance->mConstHolder->GetConstant(customAttr.mCtorArgs[0]);
							checkReflectKind = (ReflectKind)((int)checkReflectKind | constant->mInt32);
						}
						else
						{
							checkReflectKind = ReflectKind_All;
						}
					}
					else
					{
						auto userReflectKind = _GetReflectUser(customAttr.mType);
						checkReflectKind = (ReflectKind)(checkReflectKind | userReflectKind);
					}
				}

				if ((checkTypeInstance == typeInstance) ||
					((checkReflectKind & ReflectKind_ApplyToInnerTypes) != 0))
				{
					reflectKind = (ReflectKind)(reflectKind | checkReflectKind);
				}
			}

			checkTypeInstance = GetOuterType(checkTypeInstance);
		}
	}

	// Fields
	BfType* reflectFieldDataType = ResolveTypeDef(mCompiler->mReflectFieldDataDef);
	BfIRValue emptyValueType = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance()->mBaseType), SizedArray<BfIRValue, 1>());
	
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
#define PUSH_INT64(val) { data.push_back(val & 0xFF); data.push_back((val >> 8) & 0xFF); data.push_back((val >> 16) & 0xFF); data.push_back((val >> 24) & 0xFF); \
			data.push_back((val >> 32) & 0xFF); data.push_back((val >> 40) & 0xFF); data.push_back((val >> 48) & 0xFF); data.push_back((val >> 56) & 0xFF); }

		SizedArray<uint8, 16> data;

		int customAttrIdx = (int)customAttrs.size();
		
		data.push_back((uint8)reflectAttributes.size());

		for (auto attr : reflectAttributes)
		{			
			// Size prefix
			int sizeIdx = (int)data.size();
			PUSH_INT16(0); // mSize

			PUSH_INT32(attr->mType->mTypeId); // mType
			PUSH_INT16(attr->mCtor->mIdx);

			auto ctorMethodInstance = GetRawMethodInstanceAtIdx(attr->mType, attr->mCtor->mIdx);
			int argIdx = 0;
			
			for (auto arg : attr->mCtorArgs)
			{
				auto constant = typeInstance->mConstHolder->GetConstant(arg);
				bool handled = false;

				if (constant != NULL)
				{
					auto argType = ctorMethodInstance->GetParamType(argIdx);
					if (argType->IsObject())
					{
						BF_ASSERT(argType->ToTypeInstance()->mTypeDef == mCompiler->mStringTypeDef);

						int stringId = constant->mInt32;
						int* orderedIdPtr;
						if (usedStringIdMap.TryAdd(stringId, NULL, &orderedIdPtr))
						{
							*orderedIdPtr = (int)usedStringIdMap.size() - 1;							
						}

						GetStringObjectValue(stringId);
						PUSH_INT8(0xFF); // String
						PUSH_INT32(*orderedIdPtr);
						continue;
					}

					PUSH_INT8(constant->mTypeCode);
					if ((constant->mTypeCode == BfTypeCode_Int64) ||
						(constant->mTypeCode == BfTypeCode_UInt64) ||
						(constant->mTypeCode == BfTypeCode_Double))
					{
						PUSH_INT64(constant->mInt64);
					}
					else if ((constant->mTypeCode == BfTypeCode_Int32) ||
						(constant->mTypeCode == BfTypeCode_UInt32))
					{
						PUSH_INT32(constant->mInt32);
					}
					else if (constant->mTypeCode == BfTypeCode_Single)
					{
						float val = (float)constant->mDouble;
						PUSH_INT32(*(int*)&val);
					}
					else if ((constant->mTypeCode == BfTypeCode_Int16) ||
						(constant->mTypeCode == BfTypeCode_UInt16))
					{
						PUSH_INT16(constant->mInt16);
					}
					else if ((constant->mTypeCode == BfTypeCode_Int8) ||
						(constant->mTypeCode == BfTypeCode_UInt8))
					{
						PUSH_INT8(constant->mInt8);
					}
					else if (constant->mTypeCode == BfTypeCode_Object)
					{
						BF_FATAL("Unhandled");
					}
					else
					{
						BF_FATAL("Unhandled");
					}
				}
				else if (!handled)
				{
					BF_FATAL("Unhandled");
				}

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
		auto customAttrConst = mBfIRBuilder->CreateConstArray(dataArrayType, dataValues);

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

			auto fieldReflectKind = (ReflectKind)(reflectKind & ~ReflectKind_User);

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
						auto userReflectKind = _GetReflectUser(customAttr.mType);
						fieldReflectKind = (ReflectKind)(fieldReflectKind | userReflectKind);
					}
				}
			}

			if ((fieldReflectKind & ReflectKind_User) != 0)
				includeField = true;
			if ((!fieldDef->mIsStatic) && ((fieldReflectKind & ReflectKind_NonStaticFields) != 0))
				includeField = true;
			if ((fieldDef->mIsStatic) && ((fieldReflectKind & ReflectKind_StaticFields) != 0))
				includeField = true;			
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

	enum FieldFlags
	{
		FieldFlags_Protected = 3,
		FieldFlags_Public = 6,
		FieldFlags_Static = 0x10,
		FieldFlags_Const = 0x40,
		FieldFlags_SpecialName = 0x80,
		FieldFlags_EnumPayload = 0x100,
		FieldFlags_EnumDiscriminator = 0x200
	};

	if ((typeInstance->IsPayloadEnum()) && (!typeInstance->IsBoxed()))
	{	
		BfType* payloadType = typeInstance->GetUnionInnerType();		
		if (!payloadType->IsValuelessType())
		{
			BfIRValue payloadNameConst = GetStringObjectValue("$payload", true);
			SizedArray<BfIRValue, 8> payloadFieldVals =
			{
				emptyValueType,
				payloadNameConst, // mName
				GetConstValue(0, longType), // mConstValue
				GetConstValue(0, intType), // mDataOffset
				GetConstValue(payloadType->mTypeId, typeIdType), // mFieldTypeId			
				GetConstValue(FieldFlags_SpecialName | FieldFlags_EnumPayload, shortType), // mFlags
				GetConstValue(-1, intType), // mCustomAttributesIdx
			};
			auto payloadFieldData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance(), BfIRPopulateType_Full), payloadFieldVals);
			fieldTypes.push_back(payloadFieldData);
		}

		BfType* dscrType = typeInstance->GetDiscriminatorType();
		BfIRValue dscrNameConst = GetStringObjectValue("$discriminator", true);
		SizedArray<BfIRValue, 8> dscrFieldVals =
		{
			emptyValueType,
			dscrNameConst, // mName
			GetConstValue(0, longType), // mConstValue
			GetConstValue(BF_ALIGN(payloadType->mSize, dscrType->mAlign), intType), // mDataOffset
			GetConstValue(dscrType->mTypeId, typeIdType), // mFieldTypeId			
			GetConstValue(FieldFlags_SpecialName | FieldFlags_EnumDiscriminator, shortType), // mFlags
			GetConstValue(-1, intType), // mCustomAttributesIdx
		};
		auto dscrFieldData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance(), BfIRPopulateType_Full), dscrFieldVals);
		fieldTypes.push_back(dscrFieldData);
	}	

	for (auto fieldIdx : reflectFieldIndices)
	{
		if (!needsTypeData)
			break;

		BfFieldInstance* fieldInstance = &typeInstance->mFieldInstances[fieldIdx];
		BfFieldDef* fieldDef = fieldInstance->GetFieldDef();

		BfIRValue fieldNameConst = GetStringObjectValue(fieldDef->mName, true);

		int typeId = 0;
		auto fieldType = fieldInstance->GetResolvedType();
		if (fieldType->IsGenericParam())
		{
			//TODO: 
		}
		else
			typeId = fieldType->mTypeId;		

		FieldFlags fieldFlags = (FieldFlags)0;

		if (fieldDef->mProtection == BfProtection_Protected)
			fieldFlags = (FieldFlags)(fieldFlags | FieldFlags_Protected);
		if (fieldDef->mProtection == BfProtection_Public)
			fieldFlags = (FieldFlags)(fieldFlags | FieldFlags_Public);
		if (fieldDef->mIsStatic)
			fieldFlags = (FieldFlags)(fieldFlags | FieldFlags_Static);
		if (fieldDef->mIsConst)
			fieldFlags = (FieldFlags)(fieldFlags | FieldFlags_Const);

		int customAttrIdx = _HandleCustomAttrs(fieldInstance->mCustomAttributes);
		BfIRValue constValue = GetConstValue(0, longType);
		if (fieldInstance->GetFieldDef()->mIsConst)
		{			
			if (fieldInstance->mConstIdx != -1)
			{
				auto constant = typeInstance->mConstHolder->GetConstantById(fieldInstance->mConstIdx);
				constValue = mBfIRBuilder->CreateConst(BfTypeCode_UInt64, constant->mUInt64);
			}
		}		

		SizedArray<BfIRValue, 8> fieldVals =
			{
				emptyValueType,
				fieldNameConst, // mName
				constValue, // mConstValue
				GetConstValue(fieldInstance->mDataOffset, intType), // mDataOffset
				GetConstValue(typeId, typeIdType), // mFieldTypeId			
				GetConstValue(fieldFlags, shortType), // mFlags
				GetConstValue(customAttrIdx, intType), // mCustomAttributesIdx
			};
		auto fieldData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectFieldDataType->ToTypeInstance(), BfIRPopulateType_Full), fieldVals);
		fieldTypes.push_back(fieldData);
	}	

	auto reflectFieldDataIRType = mBfIRBuilder->MapType(reflectFieldDataType);
	BfIRValue fieldDataPtr;
	BfIRType fieldDataPtrType = mBfIRBuilder->GetPointerTo(reflectFieldDataIRType);
	if (fieldTypes.size() == 0)
	{
		if ((type->IsSplattable()) && (type->IsStruct()) && (!type->IsValuelessType()))
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
						if (!checkTypeInstance->mIsPacked)
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

			BfIRValue splatTypesConst = mBfIRBuilder->CreateConstArray(mBfIRBuilder->MapType(reflectFieldSplatDataType->mFieldInstances[0].mResolvedType), splatTypes);
			BfIRValue splatOffsetsConst = mBfIRBuilder->CreateConstArray(mBfIRBuilder->MapType(reflectFieldSplatDataType->mFieldInstances[1].mResolvedType), splatOffsets);
			SizedArray<BfIRValue, 8> splatVals =
			{
				emptyValueType,
				splatTypesConst,
				splatOffsetsConst
			};
			auto fieldSplatData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectFieldSplatDataType->ToTypeInstance(), BfIRPopulateType_Full), splatVals);
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
		BfIRValue fieldDataConst = mBfIRBuilder->CreateConstArray(fieldDataConstType, fieldTypes);
		BfIRValue fieldDataArray = mBfIRBuilder->CreateGlobalVariable(fieldDataConstType, true, BfIRLinkageType_Internal,
			fieldDataConst, "fields." + typeDataName);
		fieldDataPtr = mBfIRBuilder->CreateBitCast(fieldDataArray, fieldDataPtrType);
	}

	/// Methods

	BfType* reflectMethodDataType = ResolveTypeDef(mCompiler->mReflectMethodDataDef);	
	BfType* reflectParamDataType = ResolveTypeDef(mCompiler->mReflectParamDataDef);
	BfType* reflectParamDataPtrType = CreatePointerType(reflectParamDataType);

	SizedArray<BfIRValue, 16> methodTypes;
	for (int methodIdx = 0; methodIdx < (int)typeDef->mMethods.size(); methodIdx++)
	{
		if (!needsTypeData)
			break;

		auto methodInstanceGroup = &typeInstance->mMethodInstanceGroups[methodIdx];
		if (!methodInstanceGroup->IsImplemented())
			continue;		
		auto methodDef = typeDef->mMethods[methodIdx];
		if (methodDef->mNoReflect)
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
		
		auto methodReflectKind = (ReflectKind)(reflectKind & ~ReflectKind_User);

		bool includeMethod = reflectIncludeAllMethods;

		BfCustomAttributes* methodCustomAttributes = NULL;
		if ((defaultMethod->mMethodInfoEx != NULL) && (defaultMethod->mMethodInfoEx->mMethodCustomAttributes != NULL) && (defaultMethod->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes != NULL))
		{
			methodCustomAttributes = defaultMethod->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes;
			for (auto& customAttr : defaultMethod->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes->mAttributes)
			{
				if (customAttr.mType->mTypeDef->mName->ToString() == "ReflectAttribute")
				{
					includeMethod = true;
				}
				else
				{
					auto userReflectKind = _GetReflectUser(customAttr.mType);
					methodReflectKind = (ReflectKind)(methodReflectKind | userReflectKind);
				}
			}
		}
		
		if (!typeInstance->IsTypeMemberAccessible(methodDef->mDeclaringType, mProject))
			continue;

		if (auto methodDeclaration = methodDef->GetMethodDeclaration())
		{			
			for (BfParameterDeclaration* paramDecl : methodDeclaration->mParams)
			{								
				if (paramDecl->mAttributes != NULL)				
					methodReflectKind = (ReflectKind)(methodReflectKind | _GetReflectUserFromDirective(paramDecl->mAttributes, BfAttributeTargets_Parameter));
			}
			if (methodDeclaration->mReturnAttributes != NULL)
				methodReflectKind = (ReflectKind)(methodReflectKind | _GetReflectUserFromDirective(methodDeclaration->mReturnAttributes, BfAttributeTargets_ReturnValue));
		}

		if ((methodReflectKind & (ReflectKind_Methods | ReflectKind_User)) != 0)
			includeMethod = true;
		if ((methodDef->mIsStatic) && ((methodReflectKind & ReflectKind_StaticMethods) != 0))
			includeMethod = true;
				
		if (!includeMethod)
			continue;

		BfModuleMethodInstance moduleMethodInstance;
		BfIRValue funcVal = voidPtrNull;
		
		if (((!typeInstance->IsBoxed()) || (!methodDef->mIsStatic)) &&
			(!typeInstance->IsUnspecializedType()) &&
			(methodDef->mMethodType != BfMethodType_Ignore) &&
			(methodDef->mMethodType != BfMethodType_Mixin) &&
			(!methodDef->mIsAbstract) &&
			(methodDef->mGenericParams.size() == 0))
		{
			moduleMethodInstance = GetMethodInstanceAtIdx(typeInstance, methodIdx);
			if (moduleMethodInstance.mFunc)
				funcVal = mBfIRBuilder->CreateBitCast(moduleMethodInstance.mFunc, voidPtrIRType);
		}
				
		BfIRValue methodNameConst = GetStringObjectValue(methodDef->mName, true);
				
		enum MethodFlags
		{
			MethodFlags_Protected = 3,
			MethodFlags_Public = 6,
			MethodFlags_Static = 0x10,
			MethodFlags_Virtual = 0x40,
			MethodFlags_StdCall = 0x1000,
			MethodFlags_FastCall = 0x2000,
			MethodFlags_Mutating = 0x4000,
		};

		MethodFlags methodFlags = (MethodFlags)0;

		if (methodDef->mProtection == BfProtection_Protected)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_Protected);
		if (methodDef->mProtection == BfProtection_Public)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_Public);
		if (methodDef->mIsStatic)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_Static);
		if (methodDef->mIsVirtual)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_Virtual);
		if (methodDef->mCallingConvention == BfCallingConvention_Fastcall)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_FastCall);
		if (methodDef->mIsMutating)
			methodFlags = (MethodFlags)(methodFlags | MethodFlags_Mutating);
		
		int customAttrIdx = _HandleCustomAttrs(methodCustomAttributes);

		enum ParamFlags
		{
			ParamFlag_None = 0,
			ParamFlag_Splat = 1,
			ParamFlag_Implicit = 2,
		};

		SizedArray<BfIRValue, 8> paramVals;
		for (int paramIdx = 0; paramIdx < defaultMethod->GetParamCount(); paramIdx++)
		{
			String paramName = defaultMethod->GetParamName(paramIdx);
			BfType* paramType = defaultMethod->GetParamType(paramIdx);

			ParamFlags paramFlags = ParamFlag_None;
			if (defaultMethod->GetParamIsSplat(paramIdx))
				paramFlags = (ParamFlags)(paramFlags | ParamFlag_Splat);

			BfIRValue paramNameConst = GetStringObjectValue(paramName, true);

			SizedArray<BfIRValue, 8> paramDataVals =
				{
					emptyValueType,
					paramNameConst,
					GetConstValue(paramType->mTypeId, typeIdType),
					GetConstValue((int32)paramFlags, shortType),
					GetConstValue(customAttrIdx, intType) // defaultIdx
				};
			auto paramData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapType(reflectParamDataType, BfIRPopulateType_Full), paramDataVals);
			paramVals.Add(paramData);
		}
	
		BfIRValue paramsVal;
		if (paramVals.size() > 0)
		{
			BfIRType paramDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(reflectParamDataType, BfIRPopulateType_Full), (int)paramVals.size());
			BfIRValue paramDataConst = mBfIRBuilder->CreateConstArray(paramDataArrayType, paramVals);

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
			vDataIdx = 1 + mCompiler->GetDynCastVDataCount() + mCompiler->mMaxInterfaceSlots;
			if ((mCompiler->mOptions.mHasVDataExtender) && (mCompiler->IsHotCompile()))
			{
				auto typeInst = defaultMethod->mMethodInstanceGroup->mOwner;

				int extMethodIdx = (defaultMethod->mVirtualTableIdx - typeInst->GetBaseVTableSize()) - typeInst->GetOrigSelfVTableSize();
				if (extMethodIdx >= 0)
				{
					// Extension?
					int vExtOfs = typeInst->GetOrigBaseVTableSize();
					vDataVal = ((vDataIdx + vExtOfs + 1) << 20) | (extMethodIdx);
				}
				else
				{
					// Map this new virtual index back to the original index
					vDataIdx += (defaultMethod->mVirtualTableIdx - typeInst->GetBaseVTableSize()) + typeInst->GetOrigBaseVTableSize();
				}
			}
			else
			{
				vDataIdx += defaultMethod->mVirtualTableIdx;
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
				GetConstValue(vDataVal, intType),
				GetConstValue(-1, intType),
			};
		auto methodData = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapTypeInst(reflectMethodDataType->ToTypeInstance(), BfIRPopulateType_Full), methodDataVals);
		methodTypes.push_back(methodData);
	}

	BfIRValue methodDataPtr;
	BfIRType methodDataPtrType = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(reflectMethodDataType));
	if (methodTypes.size() == 0)
		methodDataPtr = mBfIRBuilder->CreateConstNull(methodDataPtrType);
	else
	{
		BfIRType methodDataArrayType = mBfIRBuilder->GetSizedArrayType(mBfIRBuilder->MapType(reflectMethodDataType, BfIRPopulateType_Full), (int)methodTypes.size());
		BfIRValue methodDataConst = mBfIRBuilder->CreateConstArray(methodDataArrayType, methodTypes);
		BfIRValue methodDataArray = mBfIRBuilder->CreateGlobalVariable(methodDataArrayType, true, BfIRLinkageType_Internal,
			methodDataConst, "methods." + typeDataName);
		methodDataPtr = mBfIRBuilder->CreateBitCast(methodDataArray, methodDataPtrType);
	}

	/////

	int underlyingType = 0;	
	if ((typeInstance->IsTypedPrimitive()) || (typeInstance->IsBoxed()))
	{		
		underlyingType = typeInstance->GetUnderlyingType()->mTypeId;
	}

	int outerTypeId = 0;
	auto outerType = GetOuterType(typeInstance);
	if (outerType != NULL)
		outerTypeId = outerType->mTypeId;

	BfIRValue customAttrDataPtr;
	if (customAttrs.size() > 0)
	{
		BfIRType customAttrsArrayType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)customAttrs.size());
		BfIRValue customAttrsConst =  mBfIRBuilder->CreateConstArray(customAttrsArrayType, customAttrs);
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
			GetConstValue(0, byteType), // mInterfaceCount
			GetConstValue((int)methodTypes.size(), shortType), // mMethodDataCount
			GetConstValue(0, shortType), // mPropertyDataCount
			GetConstValue((int)fieldTypes.size(), shortType), // mFieldDataCount
			GetConstValue(0, shortType), // mConstructorDataCount

			voidPtrNull, // mInterfaceDataPtr
			methodDataPtr, // mMethodDataPtr
			voidPtrNull, // mPropertyDataPtr
			fieldDataPtr, // mFieldDataPtr
			voidPtrNull, // mConstructorDataPtr

			customAttrDataPtr, // mCustomAttrDataPtr
		};

	BfIRType typeInstanceDataType = mBfIRBuilder->MapTypeInst(typeInstanceType->ToTypeInstance(), BfIRPopulateType_Full);
	auto typeInstanceData = mBfIRBuilder->CreateConstStruct(typeInstanceDataType, typeDataVals);
		
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
			GetConstValue((int)genericTypeInstance->mTypeGenericArguments.size(), byteType), // mGenericParamCount			
		};
		auto reflectUnspecializedGenericType = ResolveTypeDef(mCompiler->mReflectUnspecializedGenericType);
		typeInstanceDataType = mBfIRBuilder->MapTypeInst(reflectUnspecializedGenericType->ToTypeInstance(), BfIRPopulateType_Full);
		typeInstanceData = mBfIRBuilder->CreateConstStruct(typeInstanceDataType, unspecializedData);
	}
	else if (typeInstance->IsGenericTypeInstance())
	{
		auto genericTypeInstance = typeInstance->ToGenericTypeInstance();
		auto reflectSpecializedGenericType = ResolveTypeDef(mCompiler->mReflectSpecializedGenericType);
		auto unspecializedType = ResolveTypeDef(typeInstance->mTypeDef);

		SizedArray<BfIRValue, 4> resolvedTypes;
		for (auto typeGenericArg : genericTypeInstance->mTypeGenericArguments)
			resolvedTypes.push_back(GetConstValue(typeGenericArg->mTypeId, typeIdType));

		auto typeIRType = mBfIRBuilder->MapType(typeIdType);
		auto typePtrIRType = mBfIRBuilder->GetPointerTo(typeIRType);
		auto genericArrayType = mBfIRBuilder->GetSizedArrayType(typeIRType, (int)resolvedTypes.size());
		BfIRValue resolvedTypesConst = mBfIRBuilder->CreateConstArray(genericArrayType, resolvedTypes);
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
		typeInstanceData = mBfIRBuilder->CreateConstStruct(typeInstanceDataType, specGenericData);
			
		if (typeInstance->IsArray())
		{
			auto arrayType = (BfArrayType*)typeInstance;

			BfType* elementType = genericTypeInstance->mTypeGenericArguments[0];
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
			typeInstanceData = mBfIRBuilder->CreateConstStruct(typeInstanceDataType, arrayData);
		}
	}
		
	BfIRValue typeDataVar;
	if (needsTypeData)
	{
		typeDataVar = mBfIRBuilder->CreateGlobalVariable(typeInstanceDataType, true,
			BfIRLinkageType_External, typeInstanceData, typeDataName);

		if (mBfIRBuilder->DbgHasInfo())
		{
			mBfIRBuilder->DbgCreateGlobalVariable(mDICompileUnit, typeDataName, typeDataName, NULL, 0, mBfIRBuilder->DbgGetTypeInst(typeInstanceType->ToTypeInstance()), false, typeDataVar);
		}
	}
	else
	{
		typeDataVar = mBfIRBuilder->CreateConstNull(mBfIRBuilder->MapType(mContext->mBfTypeType));
	}
	typeDataVar = mBfIRBuilder->CreateBitCast(typeDataVar, mBfIRBuilder->MapType(mContext->mBfTypeType));

	mTypeDataRefs[typeInstance] = typeDataVar;

	if (classVDataVar)
	{	
		BF_ASSERT(!classVDataName.IsEmpty());

		vData[0] = mBfIRBuilder->CreateBitCast(typeDataVar, voidPtrIRType);
		auto classVDataConstDataType = mBfIRBuilder->GetSizedArrayType(voidPtrIRType, (int)vData.size());
		auto classVDataConstData = mBfIRBuilder->CreateConstArray(classVDataConstDataType, vData);

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
			mBfIRBuilder->DbgCreateGlobalVariable(mDICompileUnit, classVDataName, classVDataName, NULL, 0, arrayType, false, classVDataVar);
		}
    }
		
	return typeDataVar;
}

BfIRValue BfModule::FixClassVData(BfIRValue value)
{
	if (!mCompiler->mOptions.mObjectHasDebugFlags)
		return value;	
	auto intptrValue = mBfIRBuilder->CreatePtrToInt(value, BfTypeCode_IntPtr);
	auto maskedValue = mBfIRBuilder->CreateAnd(intptrValue, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)~0xFFULL));
	return mBfIRBuilder->CreateIntToPtr(maskedValue, mBfIRBuilder->GetType(value));
}

void BfModule::CheckStaticAccess(BfTypeInstance* typeInstance)
{	
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

	bool hasExternSpecifier = (methodDeclaration != NULL) && (methodDeclaration->mExternSpecifier != NULL);
	if (!hasExternSpecifier)
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
			
			if ((constant != NULL) && (BfIRConstHolder::IsInt(constant->mTypeCode)))
			{
				int stringId = constant->mInt32;
				auto entry = mContext->mStringObjectIdMap[stringId];
				int intrinId = BfIRCodeGen::GetIntrinsicId(entry.mString);				
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
					return mBfIRBuilder->GetIntrinsic(intrinId, paramTypes);
				}
				else if (reportFailure)
					error = StrFormat("Unable to find intrinsic '%s'", entry.mString.c_str());
			}
			else if (reportFailure)
				error = "Intrinsic name must be a constant string";

			if (reportFailure)
			{
				BfAstNode* ref = methodDeclaration->mAttributes;
				Fail(error, ref);
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
			funcType = mBfIRBuilder->CreateFunctionType(int32Type, paramTypes, true);
			func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, "PrintF");
			break;
		case BfBuiltInFuncType_Malloc:			
			{
				if (mCompiler->mOptions.mDebugAlloc)
				{
					func = GetInternalMethod("Dbg_RawAlloc", 1).mFunc;
				}
				else
				{
					String funcName = mCompiler->mOptions.mMallocLinkName;
					if (funcName.IsEmpty())
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
				if (mCompiler->mOptions.mDebugAlloc)	 
				{
					func = GetInternalMethod("Dbg_RawFree").mFunc;
				}
				else
				{
					String funcName = mCompiler->mOptions.mFreeLinkName;
					if (funcName.IsEmpty())
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

void BfModule::ResolveGenericParamConstraints(BfGenericParamInstance* genericParamInstance, const Array<BfGenericParamDef*>& genericParamDefs, int genericParamIdx)
{
	BfGenericParamDef* genericParamDef = genericParamDefs[genericParamIdx];

	BfType* startingTypeConstraint = genericParamInstance->mTypeConstraint;

	BfAutoComplete* bfAutocomplete = NULL;
	if (mCompiler->mResolvePassData != NULL)
		bfAutocomplete = mCompiler->mResolvePassData->mAutoComplete;	

	if (bfAutocomplete != NULL)
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
					for (auto checkGenericParam : genericParamDefs)
						bfAutocomplete->AddEntry(AutoCompleteEntry("generic", checkGenericParam->mName.c_str()), filter);
				}
			}
		}		
	}

	for (auto constraintTypeRef : genericParamDef->mInterfaceConstraints)
	{
		if (bfAutocomplete != NULL)
			bfAutocomplete->CheckTypeRef(constraintTypeRef, true);
		//TODO: Constraints may refer to other generic params (of either type or method)
		//  TO allow resolution, perhaps move this generic param initalization into GetMethodInstance (passing a genericPass bool)
		auto constraintType = ResolveTypeRef(constraintTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_AllowGenericMethodParamConstValue);
		if (constraintType != NULL)
		{
			if ((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
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

				switch (typeCode)
				{
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
				case BfTypeCode_Single:
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
				if (constraintType->IsPrimitiveType())
				{
					Fail("Primitive constraints are not allowed unless preceded with 'const'", constraintTypeRef);
					continue;
				}

				if (constraintType->IsArray())
				{
					Fail("Array constraints are not allowed.  If a constant-sized array was intended, an type parameterized by a const generic param can be used (ie: where T : int[T2] where T2 : const int)", constraintTypeRef);
					continue;
				}

				if ((!constraintType->IsTypeInstance()) && (!constraintType->IsSizedArray()))
				{
					Fail(StrFormat("Type '%s' is not allowed as a generic constraint", TypeToString(constraintType).c_str()), constraintTypeRef);
					continue;
				}

				if (constraintType->IsInterface())
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
								genericParamInstance->mGenericParamFlags |= BfGenericParamFlag_Class;
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

	if (((genericParamDef->mGenericParamFlags & BfGenericParamFlag_Const) != 0) &&
		(genericParamInstance->mTypeConstraint == NULL))
		genericParamInstance->mTypeConstraint = GetPrimitiveType(BfTypeCode_IntPtr);
}

String BfModule::GenericParamSourceToString(const BfGenericParamSource & genericParamSource)
{
	if (genericParamSource.mMethodInstance != NULL)
	{		
		auto methodInst = GetUnspecializedMethodInstance(genericParamSource.mMethodInstance);
		SetAndRestoreValue<BfMethodInstance*> prevMethodInst(mCurMethodInstance, methodInst);
		return MethodToString(methodInst);
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
	bool ignoreErrors = mIgnoreErrors || (errorOut == NULL) ||
		((genericParamSource.mMethodInstance == NULL) && (genericParamSource.mTypeInstance == NULL));

	BfType* origCheckArgType = checkArgType;

	if (origCheckArgType->IsRef())
		origCheckArgType = origCheckArgType->GetUnderlyingType();

	bool argMayBeReferenceType = false;

	int checkGenericParamFlags = 0;
	if (checkArgType->IsGenericParam())
	{
		auto checkGenericParamInst = GetGenericParamInstance((BfGenericParamType*)checkArgType);
		checkGenericParamFlags = checkGenericParamInst->mGenericParamFlags;
		if (checkGenericParamInst->mTypeConstraint != NULL)
			checkArgType = checkGenericParamInst->mTypeConstraint;
		
		if ((checkGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_StructPtr)) != 0)
		{
			argMayBeReferenceType = false;
		}
		else
		{
			argMayBeReferenceType = true;
		}
	}	

	if (checkArgType->IsObjectOrInterface())
		argMayBeReferenceType = true;

	BfTypeInstance* typeConstraintInst = NULL;
	if (genericParamInst->mTypeConstraint != NULL)
		typeConstraintInst = genericParamInst->mTypeConstraint->ToTypeInstance();
	
	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Struct) && 
		((checkGenericParamFlags & (BfGenericParamFlag_Struct | BfGenericParamFlag_Var)) == 0) && (!checkArgType->IsValueType()))
	{
		if (!ignoreErrors)
			*errorOut = Fail(StrFormat("The type '%s' must be a value type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetGenericParamDef()->mName.c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}
	
	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_StructPtr) && 
		((checkGenericParamFlags & (BfGenericParamFlag_StructPtr | BfGenericParamFlag_Var)) == 0) && (!checkArgType->IsPointer()))
	{
		if (!ignoreErrors)
			*errorOut = Fail(StrFormat("The type '%s' must be a pointer type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetGenericParamDef()->mName.c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Class) && 
		((checkGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_Var)) == 0) && (!argMayBeReferenceType))
	{
		if (!ignoreErrors)
			*errorOut = Fail(StrFormat("The type '%s' must be a reference type in order to use it as parameter '%s' for '%s'",
				TypeToString(origCheckArgType).c_str(), genericParamInst->GetGenericParamDef()->mName.c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
		return false;
	}

	if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Const) != 0)
	{
		if (((checkGenericParamFlags & BfGenericParamFlag_Const) == 0) && (!checkArgType->IsConstExprValue()))
		{
			if (!ignoreErrors)
				*errorOut = Fail(StrFormat("The type '%s' must be a const value in order to use it as parameter '%s' for '%s'",
					TypeToString(origCheckArgType).c_str(), genericParamInst->GetGenericParamDef()->mName.c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}		
	}
	else
	{
		if (checkArgType->IsConstExprValue())
		{
			if (!ignoreErrors)
				*errorOut = Fail(StrFormat("The value '%s' cannot be used for generic type parameter '%s' for '%s'",
					TypeToString(origCheckArgType).c_str(), genericParamInst->GetGenericParamDef()->mName.c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}
	}

	if ((genericParamInst->mInterfaceConstraints.size() == 0) && (genericParamInst->mTypeConstraint == NULL))
		return true;
	
	if (checkArgType->IsPointer())
	{
		auto ptrType = (BfPointerType*)checkArgType;
		checkArgType = ptrType->mElementType;
	}

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
						if (BfIRConstHolder::IsInt(constExprValueType->mValue.mTypeCode))
						{
							if (BfIRConstHolder::IsInt(primType->mTypeDef->mTypeCode))
							{
								if (!mCompiler->mSystem->DoesLiteralFit(primType->mTypeDef->mTypeCode, constExprValueType->mValue.mInt64))
								{
									if (!ignoreErrors)
										*errorOut = Fail(StrFormat("Const generic argument '%s', declared with const '%lld', does not fit into const constraint '%s' for '%s'", genericParamInst->GetGenericParamDef()->mName.c_str(),
											constExprValueType->mValue.mInt64, TypeToString(genericParamInst->mTypeConstraint).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
									return false;
								}
							}
							else
							{
								if (!ignoreErrors)
									*errorOut = Fail(StrFormat("Const generic argument '%s', declared with integer const '%lld', is not compatible with const constraint '%s' for '%s'", genericParamInst->GetGenericParamDef()->mName.c_str(),
										constExprValueType->mValue.mInt64, TypeToString(genericParamInst->mTypeConstraint).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
								return false;
							}
						}
						else
						{
							if (BfIRConstHolder::IsInt(primType->mTypeDef->mTypeCode))
							{
								char valStr[64];
								ExactMinimalDoubleToStr(constExprValueType->mValue.mDouble, valStr);
								if (!ignoreErrors)
									*errorOut = Fail(StrFormat("Const generic argument '%s', declared with floating point const '%s', is not compatible with const constraint '%s' for '%s'", genericParamInst->GetGenericParamDef()->mName.c_str(),
										valStr, TypeToString(genericParamInst->mTypeConstraint).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
								return false;
							}
						}
					}
				}
			}
		}
		else
		{
			BfType* convCheckConstraint = genericParamInst->mTypeConstraint;
			if ((convCheckConstraint->IsUnspecializedType()) && (methodGenericArgs != NULL))
				convCheckConstraint = ResolveGenericType(convCheckConstraint, *methodGenericArgs);
			if ((checkArgType->IsMethodRef()) && (convCheckConstraint->IsDelegate()))
			{
				auto methodRefType = (BfMethodRefType*)checkArgType;
				auto invokeMethod = GetRawMethodInstanceAtIdx(convCheckConstraint->ToTypeInstance(), 0, "Invoke");

				BfExprEvaluator exprEvaluator(this);
				if (exprEvaluator.IsExactMethodMatch(methodRefType->mMethodRef, invokeMethod))
					constraintMatched = true;

			}
			else if (CanImplicitlyCast(GetFakeTypedValue(checkArgType), convCheckConstraint))
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
						auto convCheckConstraintInst = (BfGenericTypeInstance*)convCheckConstraint;
						if (convCheckConstraintInst->mTypeDef == mCompiler->mSizedArrayTypeDef)
						{
							if (convCheckConstraintInst->mTypeGenericArguments[0] == sizedArrayType->mElementType)
							{
								auto constExprValueType = (BfConstExprValueType*)convCheckConstraintInst->mTypeGenericArguments[1];
								if (sizedArrayType->mElementCount == constExprValueType->mValue.mInt64)
									constraintMatched = true;
							}							
						}
					}
				}

				if (!constraintMatched)
				{
					BfType* wrappedStructType = GetWrappedStructType(origCheckArgType, false);
					if (CanImplicitlyCast(GetFakeTypedValue(wrappedStructType), convCheckConstraint))
						constraintMatched = true;
				}
			}

			if (!constraintMatched)
			{
				if (!ignoreErrors)
					*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must derive from '%s'", genericParamInst->GetGenericParamDef()->mName.c_str(),
						TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(), TypeToString(convCheckConstraint).c_str(),
						TypeToString(genericParamInst->mTypeConstraint).c_str()), checkArgTypeRef);
				return false;
			}
		}
	}

	if ((genericParamInst->mInterfaceConstraints.size() > 0) && (origCheckArgType->IsInterface()))
	{	
		PopulateType(origCheckArgType);
		auto checkTypeInst = origCheckArgType->ToTypeInstance();
		if (checkTypeInst->mTypeDef->mIsConcrete)
		{
			if (!ignoreErrors)
				*errorOut = Fail(StrFormat("Generic argument '%s', declared to be concrete interface '%s' for '%s', must be a concrete type", genericParamInst->GetGenericParamDef()->mName.c_str(),
					TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str()), checkArgTypeRef);
			return false;
		}
	}

	for (auto checkConstraint : genericParamInst->mInterfaceConstraints)
	{
		BfType* convCheckConstraint = checkConstraint;
		if (convCheckConstraint->IsUnspecializedType())
			convCheckConstraint = ResolveGenericType(convCheckConstraint, *methodGenericArgs);		

		BfTypeInstance* typeConstraintInst = convCheckConstraint->ToTypeInstance();	
		
		bool implementsInterface = false;
		if (origCheckArgType != checkArgType)
		{
			implementsInterface = CanImplicitlyCast(origCheckArgType, convCheckConstraint);
		}

		if (!implementsInterface)
			implementsInterface = CanImplicitlyCast(checkArgType, convCheckConstraint);

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
			if (!ignoreErrors)
				*errorOut = Fail(StrFormat("Generic argument '%s', declared to be '%s' for '%s', must implement '%s'", genericParamInst->GetGenericParamDef()->mName.c_str(), 
					TypeToString(origCheckArgType).c_str(), GenericParamSourceToString(genericParamSource).c_str(), TypeToString(checkConstraint).c_str()), checkArgTypeRef);
			return false;
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
	BfResolvedTypeSet::Entry* typeEntry = NULL;
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
			if (elementType->IsValuelessType())
			{				
				BfIRValue ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 1); // mHasValue
				mBfIRBuilder->CreateStore(GetConstValue(1, GetPrimitiveType(BfTypeCode_Boolean)), ptrValue);
				result = nullableTypedValue;
			}
			else
			{
				BfIRValue ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 1); // mValue
				mBfIRBuilder->CreateStore(result.mValue, ptrValue);
				ptrValue = mBfIRBuilder->CreateInBoundsGEP(nullableTypedValue.mValue, 0, 2); // mHasValue
				mBfIRBuilder->CreateStore(GetConstValue(1, GetPrimitiveType(BfTypeCode_Boolean)), ptrValue);
				result = nullableTypedValue;
			}			
		}
		mBfIRBuilder->CreateBr(pendingNullCond->mDoneBB);

		AddBasicBlock(pendingNullCond->mDoneBB);
		if (nullableType == NULL)
		{
			auto phi = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(result.mType), 2);
			mBfIRBuilder->AddPhiIncoming(phi, result.mValue, notNullBB);
			mBfIRBuilder->AddPhiIncoming(phi, GetDefaultValue(result.mType), pendingNullCond->mCheckBB);
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

BF_NOINLINE void BfModule::EvaluateWithNewScope(BfExprEvaluator& exprEvaluator, BfExpression* expr, BfEvalExprFlags flags)
{
	BfScopeData newScope;
	newScope.mOuterIsConditional = true;
	newScope.mAllowTargeting = false;
	mCurMethodState->AddScope(&newScope);
	NewScopeState();
	exprEvaluator.mBfEvalExprFlags = flags;
	exprEvaluator.Evaluate(expr, (flags & BfEvalExprFlags_PropogateNullConditional) != 0, (flags & BfEvalExprFlags_IgnoreNullConditional) != 0, true);
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
	exprEvaluator.mBfEvalExprFlags = flags;
	exprEvaluator.mExplicitCast = (flags & BfEvalExprFlags_ExplicitCast) != 0;

	if ((flags & BfEvalExprFlags_CreateConditionalScope) != 0)
	{
		EvaluateWithNewScope(exprEvaluator, expr, flags);
	}
	else
		exprEvaluator.Evaluate(expr, (flags & BfEvalExprFlags_PropogateNullConditional) != 0, (flags & BfEvalExprFlags_IgnoreNullConditional) != 0, true);

	if (!exprEvaluator.mResult)
	{
		if (!mCompiler->mPassInstance->HasFailed())
			Fail("INTERNAL ERROR: No expression result returned but no error caught in expression evaluator", expr);
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
				auto primType = genericTypeConstraint->ToPrimitiveType();
				if (primType != NULL)
				{
					BfTypedValue result;
					result.mKind = BfTypedValueKind_Value;
					result.mType = genericTypeConstraint;
					result.mValue = mBfIRBuilder->GetUndefConstValue(primType->mTypeDef->mTypeCode);
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
	exprEvaluator.CheckResultForReading(typedVal);

	if ((wantTypeRef == NULL) || (!wantTypeRef->IsRef()))
	{
		if ((flags & BfEvalExprFlags_NoCast) == 0)
		{
			// Only allow a 'ref' type if we have an explicit 'ref' operator
			bool allowRef = false;
			BfExpression* checkExpr = expr;
			while (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(checkExpr))
				checkExpr = parenExpr->mExpression;
			if (auto unaryOp = BfNodeDynCast<BfUnaryOperatorExpression>(checkExpr))
			{
				if ((unaryOp->mOp == BfUnaryOp_Ref) || (unaryOp->mOp == BfUnaryOp_Mut))
					allowRef = true;
			}
			if (!allowRef)
				typedVal = RemoveRef(typedVal);
		}
	}

	if (!typedVal.mType->IsComposite()) // Load non-structs by default
		typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);

	if (wantTypeRef != NULL)
	{
		if (outOrigType != NULL)
			*outOrigType = typedVal.mType;

		if ((flags & BfEvalExprFlags_NoCast) == 0)
		{
			BfCastFlags castFlags = ((flags & BfEvalExprFlags_ExplicitCast) != 0) ? BfCastFlags_Explicit : BfCastFlags_None;
			if ((flags & BfEvalExprFlags_FieldInitializer) != 0)
				castFlags = (BfCastFlags)(castFlags | BfCastFlags_WarnOnBox);
			typedVal = Cast(expr, typedVal, wantTypeRef, castFlags);
			if (!typedVal)
				return typedVal;
		}

		//WTF- why did we have this?
		/*if ((flags & BfEvalExprFlags_ExplicitCast) == 0)
			typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);*/
		if (exprEvaluator.mIsVolatileReference)
			typedVal = LoadValue(typedVal, 0, exprEvaluator.mIsVolatileReference);
	}

// 	if ((typedVal.IsSplat()) && ((flags & BfEvalExprFlags_AllowSplat) == 0))
// 		typedVal = MakeAddressable(typedVal);

	//typedVal = AggregateSplat(typedVal);	

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
		if (mCompiler->mOptions.mObjectHasDebugFlags)
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
			exprEvaluator.DoInvocation(allocTarget.mScopedInvocationTarget, NULL, argExprs, NULL);
			allocResult = LoadValue(exprEvaluator.mResult);
		}
		else if (allocTarget.mCustomAllocator)
		{						
			auto customTypeInst = allocTarget.mCustomAllocator.mType->ToTypeInstance();
			if (customTypeInst != NULL)
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
					allocResult = exprEvaluator.MatchMethod(refNode, NULL, allocTarget.mCustomAllocator, false, false, allocMethodName, argValues, NULL);
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
	
	if (mCompiler->mOptions.mDebugAlloc)
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
		BfMethodMatcher methodMatcher(NULL, this, "Mark", resolvedArgs, NULL);
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
	dataValues.Add(mBfIRBuilder->CreateConstStructZero(mBfIRBuilder->MapType(dbgRawAllocDataType->mBaseType, BfIRPopulateType_Full)));
	dataValues.Add(typeDataRef);
	dataValues.Add(markFuncPtr);
	dataValues.Add(mBfIRBuilder->CreateConst(BfTypeCode_Int32, stackCount));
	BfIRValue dataStruct = mBfIRBuilder->CreateConstStruct(mBfIRBuilder->MapType(dbgRawAllocDataType, BfIRPopulateType_Full), dataValues);
	allocDataValue = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(dbgRawAllocDataType), true, BfIRLinkageType_Internal, dataStruct, "__allocData_" + BfSafeMangler::Mangle(type));

	mDbgRawAllocDataRefs.TryAdd(type, allocDataValue);
	return allocDataValue;
}

BfIRValue BfModule::AllocFromType(BfType* type, const BfAllocTarget& allocTarget, BfIRValue appendSizeValue, BfIRValue arraySize, int arrayDim, BfAllocFlags allocFlags, int alignOverride)
{
	BP_ZONE("AllocFromType");

	BfScopeData* scopeData = allocTarget.mScopeData;

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
		if (arraySize)
		{
			if (isRawArrayAlloc)
			{
				BfPointerType* ptrType = CreatePointerType(type);
				return GetDefaultValue(ptrType);
			}
			BfArrayType* arrayType = CreateArrayType(type, arrayDim);
			return GetDefaultValue(arrayType);
		}

		if (type->IsValueType())
		{
			BfPointerType* ptrType = CreatePointerType(type);			
			return GetDefaultValue(ptrType);
		}

		return mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(type));
	}

	AddDependency(type, mCurTypeInstance, BfDependencyMap::DependencyFlag_Allocates);

	BfIRValue sizeValue;
	BfIRType allocType = mBfIRBuilder->MapType(type);
	int allocSize = type->mSize;
	int allocAlign = type->mAlign;
	if (typeInstance != NULL)
	{	
		if (!mBfIRBuilder->mIgnoreWrites)
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
						sizeValue = mBfIRBuilder->CreateMul(GetConstValue(typeSize), arraySize);
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
					mBfIRBuilder->SetAllocaAlignment(allocaInst, type->mAlign);	
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

						if (!isConstSize)
						{
							clearBlock = mBfIRBuilder->CreateBlock("clear");
							contBlock = mBfIRBuilder->CreateBlock("clearCont");
							mBfIRBuilder->CreateCondBr(mBfIRBuilder->CreateCmpNE(arraySize, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0)), clearBlock, contBlock);

							mBfIRBuilder->AddBlock(clearBlock);
							mBfIRBuilder->SetInsertPoint(clearBlock);
						}

						AddStackAlloc(typedVal, NULL, scopeData, false, true);

						if (!isConstSize)
						{
							mBfIRBuilder->CreateBr(contBlock);

							mBfIRBuilder->AddBlock(contBlock);
							mBfIRBuilder->SetInsertPoint(contBlock);
						}
					}
					//InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);

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
						AddStackAlloc(typedVal, NULL, scopeData, false, true);
					InitTypeInst(typedVal, scopeData, zeroMemory, sizeValue);
					return typedVal.mValue;
				}
				else
				{
					BfArrayType* arrayType = CreateArrayType(type, arrayDim);
					auto firstElementField = &arrayType->mFieldInstances.back();
					int arrayClassSize = firstElementField->mDataOffset;
					sizeValue = GetConstValue(arrayClassSize);
					BfIRValue elementDataSize = mBfIRBuilder->CreateMul(GetConstValue(type->GetStride()), arraySize);
					sizeValue = mBfIRBuilder->CreateAdd(sizeValue, elementDataSize);
					
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
					mBfIRBuilder->SetAllocaAlignment(allocaInst, arrayType->mAlign);
					auto typedVal = BfTypedValue(mBfIRBuilder->CreateBitCast(allocaInst, mBfIRBuilder->MapType(arrayType)), arrayType);
					mBfIRBuilder->ClearDebugLocation_Last();
					if (!isDynAlloc)
						mBfIRBuilder->SetInsertPoint(prevBlock);
					if (!noDtorCall)
						AddStackAlloc(typedVal, NULL, scopeData, false, true);
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
					AddStackAlloc(typedVal, NULL, scopeData, mCurMethodState->mInConditionalBlock, true);
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
					AddStackAlloc(typedVal, NULL, scopeData, doCondAlloca, true);
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
				AddStackAlloc(typedVal, NULL, scopeData, false, true);
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
				BfTypedValue allocResult = exprEvaluator.MatchMethod(allocTarget.mRefNode, NULL, allocTarget.mCustomAllocator, false, false, "AllocObject", argValues, NULL);
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

		if (!result)
		{			
			if (hasCustomAllocator)
				result = AllocBytes(allocTarget.mRefNode, allocTarget, typeInstance, sizeValue, GetConstValue(typeInstance->mInstAlign), (BfAllocFlags)(BfAllocFlags_ZeroMemory | BfAllocFlags_NoDefaultToMalloc));				
		}

		if (result)
		{	
			if (mCompiler->mOptions.mObjectHasDebugFlags)
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
			if ((mBfIRBuilder->mIgnoreWrites) || (mCompiler->mIsResolveOnly))
				return GetDefaultValue(typeInstance);

			auto classVDataType = ResolveTypeDef(mCompiler->mClassVDataTypeDef);			
			auto vData = mBfIRBuilder->CreateBitCast(vDataRef, mBfIRBuilder->MapTypeInstPtr(classVDataType->ToTypeInstance()));			

			if (mCompiler->mOptions.mObjectHasDebugFlags)
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
				if (mCompiler->mOptions.mDebugAlloc)
				{
					auto moduleMethodInstance = GetInternalMethod("Dbg_RawObjectAlloc", 1);
					irFunc = moduleMethodInstance.mFunc;
				}
				if (!irFunc)
					irFunc = GetBuiltInFunc(BfBuiltInFuncType_Malloc);				
				BfIRValue objectVal = mBfIRBuilder->CreateCall(irFunc, llvmArgs);
				auto objResult = mBfIRBuilder->CreateBitCast(objectVal, mBfIRBuilder->MapType(mContext->mBfObjectType, BfIRPopulateType_Full));
				auto vdataPtr = mBfIRBuilder->CreateInBoundsGEP(objResult, 0, 0);
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

void BfModule::EmitAlign(BfIRValue& appendCurIdx, int align)
{	
	appendCurIdx = mBfIRBuilder->CreateAdd(appendCurIdx, GetConstValue(align - 1));
	appendCurIdx = mBfIRBuilder->CreateAnd(appendCurIdx, GetConstValue(~(align - 1)));
}

void BfModule::EmitAppendAlign(int align, int sizeMultiple)
{	
	if (sizeMultiple == 0)
		sizeMultiple = align;
	if ((mCurMethodState->mCurAppendAlign != 0) && (mCurMethodState->mCurAppendAlign % align != 0))
	{
		if (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Ctor)
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
		if (mCompiler->mOptions.mObjectHasDebugFlags)
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

		if (mCompiler->mOptions.mObjectHasDebugFlags)
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
	int optLevel = GetModuleOptions().mOptLevel;
	return (optLevel >= BfOptLevel_O1) && (optLevel <= BfOptLevel_O3);
}

bool BfModule::IsTargetingBeefBackend()
{
	if (mProject == NULL)
		return false;
	return GetModuleOptions().mOptLevel == BfOptLevel_OgPlus;
}

bool BfModule::WantsLifetimes()
{
	if (mProject == NULL)
		return false;
	return GetModuleOptions().mOptLevel == BfOptLevel_OgPlus;
}

bool BfModule::HasCompiledOutput()
{
	return (!mSystem->mIsResolveOnly) && (mIsReified);
}

// We will skip the object access check for any occurances of this value
void BfModule::SkipObjectAccessCheck(BfTypedValue typedVal)
{
	if ((mBfIRBuilder->mIgnoreWrites) || (!typedVal.mType->IsObjectOrInterface()) || (mCurMethodState == NULL) || (mCurMethodState->mIgnoreObjectAccessCheck))
		return;

	if (!mCompiler->mOptions.mObjectHasDebugFlags)
		return;
	
	if ((typedVal.mValue.mFlags & BfIRValueFlags_Value) == 0)
		return;

	mCurMethodState->mSkipObjectAccessChecks.Add(typedVal.mValue.mId);
}

void BfModule::EmitObjectAccessCheck(BfTypedValue typedVal)
{
	if ((mBfIRBuilder->mIgnoreWrites) || (!typedVal.mType->IsObjectOrInterface()) || (mCurMethodState == NULL) || (mCurMethodState->mIgnoreObjectAccessCheck))
		return;

	if (!mCompiler->mOptions.mObjectHasDebugFlags)
		return;

	bool emitObjectAccessCheck = mCompiler->mOptions.mEmitObjectAccessCheck;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		emitObjectAccessCheck = BfTypeOptions::Apply(emitObjectAccessCheck, typeOptions->mEmitObjectAccessCheck);
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
	if (mProject == NULL)
		return;

	if ((mBfIRBuilder->mIgnoreWrites) || (!mHasFullDebugInfo) || (IsOptimized()) || (mCompiler->mOptions.mOmitDebugHelpers))
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
		auto callResult = exprEvaluator.CreateCall(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, irArgs);
		auto resultType = ResolveTypeDef(mSystem->mTypeBool);
		auto cmpResult = mBfIRBuilder->CreateCmpNE(callResult.mValue, GetDefaultValue(callResult.mType));
		irb->CreateCondBr(cmpResult, trueBlock, falseBlock);
	}
	else
	{
		AddBasicBlock(checkBB);
		BfIRValue vDataPtr = irb->CreateBitCast(targetValue.mValue, irb->MapType(intPtrType));
		vDataPtr = irb->CreateLoad(vDataPtr);
		if (mCompiler->mOptions.mObjectHasDebugFlags)
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
	bool emitDynamicCastCheck = mCompiler->mOptions.mEmitDynamicCastCheck;
	auto typeOptions = GetTypeOptions();
	if (typeOptions != NULL)
		emitDynamicCastCheck = BfTypeOptions::Apply(emitDynamicCastCheck, typeOptions->mEmitDynamicCastCheck);	

	if (emitDynamicCastCheck)
	{		
		int wantTypeId = 0;
		if (!type->IsGenericParam())
			wantTypeId = type->mTypeId;

		BfIRBlock failBlock = mBfIRBuilder->CreateBlock("dynFail");
		BfIRBlock endBlock = mBfIRBuilder->CreateBlock("dynEnd");

		EmitDynamicCastCheck(typedVal, type, endBlock, failBlock, allowNull ? true : false);

		AddBasicBlock(failBlock);
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
		mBfIRBuilder->CreateBr(endBlock);

		AddBasicBlock(endBlock);
	}
}

BfTypedValue BfModule::BoxValue(BfAstNode* srcNode, BfTypedValue typedVal, BfType* toType, const BfAllocTarget& allocTarget, bool callDtor)
{
	BP_ZONE("BoxValue");

	BfTypeInstance* fromStructTypeInstance = typedVal.mType->ToTypeInstance();
	if (typedVal.mType->IsNullable())
	{
		typedVal = MakeAddressable(typedVal);

		auto innerType = typedVal.mType->GetUnderlyingType();
		if (!innerType->IsValueType())
		{
			Fail("Only value types can be boxed", srcNode);
			return BfTypedValue();
		}

		auto prevBB = mBfIRBuilder->GetInsertBlock();
		auto boxBB = mBfIRBuilder->CreateBlock("boxedN.notNull");
		auto endBB = mBfIRBuilder->CreateBlock("boxedN.end");

		mBfIRBuilder->PopulateType(typedVal.mType);
		auto hasValueAddr = mBfIRBuilder->CreateInBoundsGEP(typedVal.mValue, 0, innerType->IsValuelessType() ? 1 : 2); // has_value
		auto hasValue = mBfIRBuilder->CreateLoad(hasValueAddr);

		mBfIRBuilder->CreateCondBr(hasValue, boxBB, endBB);

		auto boxedType = CreateBoxedType(innerType);
		AddDependency(boxedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
		auto resultType = toType;
		if (resultType == NULL)
			resultType = boxedType;

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
		
		auto boxedVal = BoxValue(srcNode, BfTypedValue(loadedVal, fromStructTypeInstance->GetUnderlyingType()), resultType, allocTarget, callDtor);
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
		fromStructTypeInstance = GetWrappedStructType(typedVal.mType);

		if (isStructPtr)
		{
			if ((toTypeInstance != NULL) && (TypeIsSubTypeOf(fromStructTypeInstance, toTypeInstance)))
				alreadyCheckedCast = true;

			fromStructTypeInstance = typedVal.mType->GetUnderlyingType()->ToTypeInstance();
		}		
	}
	if (fromStructTypeInstance == NULL)
		return BfTypedValue();	

	// Need to box it
	auto boxedType = CreateBoxedType(typedVal.mType);
	bool isBoxedType = (fromStructTypeInstance != NULL) && (toType->IsBoxed()) && (boxedType == toType);

	if ((toType == NULL) || (toType == mContext->mBfObjectType) || (isBoxedType) || (alreadyCheckedCast) ||  (TypeIsSubTypeOf(fromStructTypeInstance, toTypeInstance)))
	{
		if (typedVal.mType->IsPointer())
		{
			NOP;
		}
		
		mBfIRBuilder->PopulateType(boxedType);
		AddDependency(boxedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ReadFields);
		auto allocaInst = AllocFromType(boxedType, allocTarget, BfIRValue(), BfIRValue(), 0, callDtor ? BfAllocFlags_None : BfAllocFlags_NoDtorCall);
		
		BfTypedValue boxedTypedValue(allocaInst, boxedType);			
		mBfIRBuilder->SetName(allocaInst, "boxed." + fromStructTypeInstance->mTypeDef->mName->ToString());
		
		if (boxedType->IsUnspecializedType())
		{
			BF_ASSERT(mCurMethodInstance->mIsUnspecialized);
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
					mBfIRBuilder->CreateStore(typedVal.mValue, valPtr);
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
		if (typeInstance->IsIncomplete())
			PopulateType(typeInstance, BfPopulateType_DataAndMethods);
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
	
	auto& methodGroup = typeInstance->mMethodInstanceGroups[methodIdx];
	if (methodGroup.mDefault == NULL)
	{
		if (!mCompiler->mIsResolveOnly)		
		{
			BF_ASSERT((methodGroup.mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference) || (methodGroup.mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl));
			methodGroup.mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;

			// Get it from the owning module so we don't create a reference prematurely...
			auto declModule = typeInstance->mModule;			
			return declModule->GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector(), (BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_UnspecializedPass | BfGetMethodInstanceFlag_Unreified)).mMethodInstance;
		}
		else
		{
			auto declModule = typeInstance->mModule;
			return declModule->GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector(), (BfGetMethodInstanceFlags)(BfGetMethodInstanceFlag_UnspecializedPass)).mMethodInstance;
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

	return GetRawMethodInstanceAtIdx(typeInstance, methodDef->mIdx);
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

BfMethodInstance* BfModule::GetUnspecializedMethodInstance(BfMethodInstance* methodInstance)
{
	if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mMethodGenericArguments.size() != 0))
		methodInstance = methodInstance->mMethodInstanceGroup->mDefault;

	auto owner = methodInstance->mMethodInstanceGroup->mOwner;
	if (!owner->IsGenericTypeInstance())
		return methodInstance;
	
	auto genericType = (BfGenericTypeInstance*)owner;
	if (genericType->IsUnspecializedType())
		return methodInstance;
	
	auto unspecializedType = ResolveTypeDef(genericType->mTypeDef);	
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

BfModule* BfModule::GetSpecializedMethodModule(const Array<BfProject*>& projectList)
{
	BF_ASSERT(!mIsScratchModule);
	BF_ASSERT(mIsReified);

	BfModule* mainModule = this;
	if (mParentModule != NULL)
		mainModule = mParentModule;

	BfModule* specModule = NULL;	
	BfModule** specModulePtr = NULL;
	if (mainModule->mSpecializedMethodModules.TryGetValue(projectList, &specModulePtr))
	{
		return *specModulePtr;
	}
	else
	{		
		String specModuleName = mModuleName;
		for (auto bfProject : projectList)
			specModuleName += StrFormat("@%s", bfProject->mName.c_str());
		specModule = new BfModule(mContext, specModuleName);
		specModule->mProject = mainModule->mProject;
		specModule->mParentModule = mainModule;
		specModule->mIsSpecializedMethodModuleRoot = true;
		specModule->Init();
		mainModule->mSpecializedMethodModules[projectList] = specModule;			
	}
	return specModule;
}

BfIRValue BfModule::CreateFunctionFrom(BfMethodInstance* methodInstance, bool tryExisting, bool isInlined)
{	
	if (mCompiler->IsSkippingExtraResolveChecks())
		return BfIRValue();

	if (methodInstance->mMethodInstanceGroup->mOwner->IsInterface())
	{
		//BF_ASSERT(!methodInstance->mIRFunction);
		return BfIRValue();
	}

	auto methodDef = methodInstance->mMethodDef;
	StringT<128> methodName;
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

	if (auto methodDeclaration = methodDef->GetMethodDeclaration())
	{
		if (methodDeclaration->mExternSpecifier != NULL)
		{	
			auto intrinsic = GetIntrinsic(methodInstance);
			if (intrinsic)
				return intrinsic;

			// If we have multiple entries with the same name, they could have different arguments and will generate other errors...
			/*auto func = mBfIRBuilder->GetFunction(methodName);
			if (func)
				return func;*/			
		}
	}
	
	if (methodInstance->GetImportCallKind() != BfImportCallKind_None)
	{
		return CreateDllImportGlobalVar(methodInstance, false);
	}

	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	methodInstance->GetIRFunctionInfo(this, returnType, paramTypes);
	auto funcType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes);

	auto func = mBfIRBuilder->CreateFunction(funcType, BfIRLinkageType_External, methodName);
	auto callingConv = GetCallingConvention(methodInstance->GetOwner(), methodDef);
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

BfModuleMethodInstance BfModule::GetMethodInstanceAtIdx(BfTypeInstance* typeInstance, int methodIdx, const char* assertName)
{	
	if (assertName != NULL)
	{
		BF_ASSERT(typeInstance->mTypeDef->mMethods[methodIdx]->mName == assertName);
	}	
	
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	auto methodInstance = typeInstance->mMethodInstanceGroups[methodIdx].mDefault;

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

	return GetMethodInstance(typeInstance, typeInstance->mTypeDef->mMethods[methodIdx], BfTypeVector());	
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
 				return GetMethodInstanceAtIdx(typeInstance, methodDef->mIdx);
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

BfFieldInstance* BfModule::GetFieldByName(BfTypeInstance* typeInstance, const StringImpl& fieldName)
{
	PopulateType(typeInstance, BfPopulateType_DataAndMethods);

	typeInstance->mTypeDef->PopulateMemberSets();
	BfMemberSetEntry* entry = NULL;
	BfFieldDef* fieldDef = NULL;
	if (typeInstance->mTypeDef->mFieldSet.TryGetWith(fieldName, &entry))
	{
		fieldDef = (BfFieldDef*)entry->mMemberDef;
		return &typeInstance->mFieldInstances[fieldDef->mIdx];
	}

// 	for (auto& fieldInst : typeInstance->mFieldInstances)
// 	{
// 		auto fieldDef = fieldInst.GetFieldDef();
// 		if ((fieldDef != NULL) && (fieldDef->mName == fieldName))
// 			return &fieldInst;
// 	}

	return NULL;
}

String BfModule::MethodToString(BfMethodInstance* methodInst, BfMethodNameFlags methodNameFlags, BfTypeVector* methodGenericArgs)
{
	auto methodDef = methodInst->mMethodDef;	
	bool allowResolveGenericParamNames = ((methodNameFlags & BfMethodNameFlag_ResolveGenericParamNames) != 0);

	BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None;
	if ((mCurTypeInstance == NULL) || (!mCurTypeInstance->IsUnspecializedTypeVariation()))
		typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;

	BfType* type = methodInst->mMethodInstanceGroup->mOwner;
	if ((methodGenericArgs != NULL) && (type->IsUnspecializedType()))
		type = ResolveGenericType(type, *methodGenericArgs);
	String methodName;
	if ((methodNameFlags & BfMethodNameFlag_OmitTypeName) == 0)
	{
		methodName = TypeToString(type, typeNameFlags);
		if (methodName == "$")
			methodName = "";
		else
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
		methodName += TypeToString(methodInst->mMethodInfoEx->mExplicitInterface, typeNameFlags);
		methodName += ".";
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
		methodDef->GetRefNode()->ToString(methodName);
		methodName += " get accessor";
		return methodName;
	}
	else if (methodDef->mMethodType == BfMethodType_PropertySetter)
	{
		methodDef->GetRefNode()->ToString(methodName);
		methodName += " set accessor";
		return methodName;
	}
	else
		methodName += methodDefName;	

	BfTypeVector newMethodGenericArgs;
	if ((methodInst->mMethodInfoEx != NULL) && (methodInst->mMethodInfoEx->mMethodGenericArguments.size() != 0))
	{			
		methodName += "<";
		for (int i = 0; i < (int) methodInst->mMethodInfoEx->mMethodGenericArguments.size(); i++)
		{
			if (i > 0)
				methodName += ", ";
			BfTypeNameFlags typeNameFlags = BfTypeNameFlags_None;
			//Why did we have this methodInst->mIsUnspecializedVariation check?  Sometimes we do need to show errors calling methods that refer back to our types
			//if (!methodInst->mIsUnspecializedVariation && allowResolveGenericParamNames)
			if (allowResolveGenericParamNames)
				typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
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
					type = ResolveGenericType(type, *methodGenericArgs);
			}			
			methodName += TypeToString(type, typeNameFlags);
		}
		methodName += ">";		
	}
	if (accessorString.length() == 0)
	{
		int dispParamIdx = 0;
		methodName += "(";
		for (int paramIdx = 0; paramIdx < (int) methodInst->GetParamCount(); paramIdx++)
		{	
			int paramKind = methodInst->GetParamKind(paramIdx);
			if (paramKind == BfParamKind_ImplicitCapture)
				continue;

			if (dispParamIdx > 0)
				methodName += ", ";
						
			if (paramKind == BfParamKind_Params)
				methodName += "params ";

			typeNameFlags = BfTypeNameFlag_ResolveGenericParamNames;
			BfType* type = methodInst->GetParamType(paramIdx);
			if ((methodGenericArgs != NULL) && (type->IsUnspecializedType()))
				type = ResolveGenericType(type, *methodGenericArgs);
			methodName += TypeToString(type, typeNameFlags);

			methodName += " ";
			methodName += methodInst->GetParamName(paramIdx);
			dispParamIdx++;
		}
		methodName += ")";
	}
	
	if (accessorString.length() != 0)
	{
		methodName += " " + accessorString;
	}
	return methodName;
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
	if (resultStr.IsEmpty())
		return "<nothing>";
	return resultStr;
}

void BfModule::CurrentAddToConstHolder(BfIRValue& irVal)
{
	auto constant = mBfIRBuilder->GetConstant(irVal);
	
	int stringPoolIdx = GetStringPoolIdx(irVal, mBfIRBuilder);
	if (stringPoolIdx != -1)
	{
		irVal = mCurTypeInstance->GetOrCreateConstHolder()->CreateConst(BfTypeCode_Int32, stringPoolIdx);
		return;
	}

	if (constant->mConstType == BfConstType_Array)
	{
		auto constArray = (BfConstantArray*)constant;
		
		SizedArray<BfIRValue, 8> newVals;
		for (auto val : constArray->mValues)
		{
			auto newVal = val;
			CurrentAddToConstHolder(newVal);
			newVals.push_back(newVal);			
		}

		irVal = mCurTypeInstance->mConstHolder->CreateConstArray(constArray->mType, newVals);
		return;
	}

	auto origConst = irVal;		
	if ((constant->mConstType == BfConstType_BitCast) || (constant->mConstType == BfConstType_BitCastNull))
	{
		auto bitcast = (BfConstantBitCast*)constant;
		constant = mBfIRBuilder->GetConstantById(bitcast->mTarget);
	}

	irVal = mCurTypeInstance->CreateConst(constant, mBfIRBuilder);		
}

void BfModule::ClearConstData()
{
	mBfIRBuilder->ClearConstData();
	mStringObjectPool.Clear();
	mStringCharPtrPool.Clear();
	mStringPoolRefs.Clear();
}

BfTypedValue BfModule::GetTypedValueFromConstant(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType)
{		

	bool isString = false;
	isString = (wantType->IsObject()) && (wantType->ToTypeInstance()->mTypeDef == mCompiler->mStringTypeDef);
	if (wantType->IsPointer())
		isString = true;
	
	if (!isString)	
	{
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
		case BfTypeCode_Char8:
		case BfTypeCode_Char16:
		case BfTypeCode_Char32:
		case BfTypeCode_Single:
		case BfTypeCode_Double:
			{
				auto constVal = mBfIRBuilder->CreateConst(constant, constHolder);
				auto typedValue = BfTypedValue(constVal, GetPrimitiveType(constant->mTypeCode));
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
				BF_ASSERT(castedTypedValue);
				return castedTypedValue;
			}
			break;
		default: break;
		}
	}
	BfIRValue irValue = ConstantToCurrent(constant, constHolder, wantType);
	BF_ASSERT(irValue);
	if (!irValue)
		return BfTypedValue();
	return BfTypedValue(irValue, wantType, false);
}

BfIRValue BfModule::ConstantToCurrent(BfConstant* constant, BfIRConstHolder* constHolder, BfType* wantType)
{
	if (constant->mTypeCode == BfTypeCode_NullPtr)
	{
		return GetDefaultValue(wantType);
	}

	bool isString = false;
	isString = (wantType->IsObject()) && (wantType->ToTypeInstance()->mTypeDef == mCompiler->mStringTypeDef);
	if (((wantType->IsPointer()) || (isString)) && (mBfIRBuilder->IsInt(constant->mTypeCode)))
	{		
		const StringImpl& str = mContext->mStringObjectIdMap[constant->mInt32].mString;
		BfIRValue stringObjConst = GetStringObjectValue(str);
		if (wantType->IsObject())
			return stringObjConst;		
		return GetStringCharPtr(stringObjConst);
	}	
		
	if (constant->mConstType == BfConstType_Array)
	{
		auto elementType = wantType->GetUnderlyingType();
		auto constArray = (BfConstantArray*)constant;

		SizedArray<BfIRValue, 8> newVals;
		for (auto val : constArray->mValues)
		{
			newVals.push_back(ConstantToCurrent(constHolder->GetConstant(val), constHolder, elementType));
		}

		return mBfIRBuilder->CreateConstArray(mBfIRBuilder->MapType(wantType), newVals);
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
				customAttribute.mRef->ToString().c_str(), GetAttributesTargetListString(customAttribute.mType->mAttributeData->mAttributeTargets).c_str()), customAttribute.mRef->mAttributeTypeRef);	// CS0592
		}		

		customAttribute.mAwaitingValidation = false;
	}
}

void BfModule::GetCustomAttributes(BfCustomAttributes* customAttributes, BfAttributeDirective* attributesDirective, BfAttributeTargets attrTarget)
{
	if ((attributesDirective != NULL) && (mCompiler->mResolvePassData != NULL) && 
		(attributesDirective->IsFromParser(mCompiler->mResolvePassData->mParser)) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
	{
		mCompiler->mResolvePassData->mSourceClassifier->VisitChild(attributesDirective);
	}

	SetAndRestoreValue<bool> prevIsCapturingMethodMatchInfo;
	if (mCompiler->IsAutocomplete())
		prevIsCapturingMethodMatchInfo.Init(mCompiler->mResolvePassData->mAutoComplete->mIsCapturingMethodMatchInfo, false);
	
	BfTypeInstance* baseAttrTypeInst = mContext->mUnreifiedModule->ResolveTypeDef(mCompiler->mAttributeTypeDef)->ToTypeInstance();
	BfAttributeTargets targetOverride = (BfAttributeTargets)0;

	BfAutoComplete* autoComplete = NULL;
	if (mCompiler->mResolvePassData != NULL)
		autoComplete = mCompiler->mResolvePassData->mAutoComplete;

	for (; attributesDirective != NULL; attributesDirective = attributesDirective->mNextAttribute)
	{
		BfAutoParentNodeEntry autoParentNodeEntry(this, attributesDirective);

		BfCustomAttribute customAttribute;
		customAttribute.mAwaitingValidation = true;
		customAttribute.mRef = attributesDirective;

		if (attributesDirective->mAttrOpenToken != NULL)
			targetOverride = (BfAttributeTargets)0;

		if (attributesDirective->mAttributeTypeRef == NULL)
		{
			AssertErrorState();
			continue;
		}

		String attrName = attributesDirective->mAttributeTypeRef->ToString();
		BfType* attrType = NULL;
		BfAtomComposite nameComposite;
		BfTypeDef* attrTypeDef = NULL;
		if (mSystem->ParseAtomComposite(attrName + "Attribute", nameComposite))
		{
			BfTypeLookupError error;
			error.mRefNode = attributesDirective->mAttributeTypeRef;
			attrTypeDef = FindTypeDefRaw(nameComposite, 0, mCurTypeInstance, GetActiveTypeDef(NULL, true), &error);			
		}
				
		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		{
			mCompiler->mResolvePassData->mAutoComplete->CheckAttributeTypeRef(attributesDirective->mAttributeTypeRef);
			if (attrTypeDef != NULL)
				mCompiler->mResolvePassData->HandleTypeReference(attributesDirective->mAttributeTypeRef, attrTypeDef);
		}

		if (attrTypeDef == NULL)
		{
			TypeRefNotFound(attributesDirective->mAttributeTypeRef, "Attribute");
			continue;
		}

		bool isBypassedAttr = false;

		if (attrTypeDef != NULL)
		{
			attrType = mContext->mUnreifiedModule->ResolveTypeDef(attrTypeDef, BfPopulateType_Identity);
						
			// 'Object' has some dependencies on some attributes, but those attributes are classes so we have a circular dependency issue
			//  We solve it by having a 'bypass' for known attributes that Object depends on
			if ((attributesDirective->mArguments.empty()) && (autoComplete == NULL) && (attrType != NULL) && (attrType->IsTypeInstance()))
			{
				if (attrTypeDef == mCompiler->mCReprAttributeTypeDef)
				{
					BfTypeInstance* attrTypeInst = attrType->ToTypeInstance();

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
		
		// Don't allow this
		/*else if (mContext->mCurTypeState != NULL)
		{
			SetAndRestoreValue<BfTypeReference*> prevTypeRef(mContext->mCurTypeState->mCurAttributeTypeRef, attributesDirective->mAttributeTypeRef);
			attrType = mContext->mUnreifiedModule->ResolveTypeRef(attributesDirective->mAttributeTypeRef, BfPopulateType_DataAndMethods);
		}
		else
		{
			attrType = mContext->mUnreifiedModule->ResolveTypeRef(attributesDirective->mAttributeTypeRef);
		}*/
		
		BfTypeInstance* attrTypeInst = NULL;
		if (attrType == NULL)
			continue;
		mContext->mUnreifiedModule->PopulateType(attrType, BfPopulateType_DataAndMethods); 
		attrTypeInst = attrType->ToTypeInstance();
		if ((attrTypeInst == NULL) || (!TypeIsSubTypeOf(attrTypeInst, baseAttrTypeInst)) || (attrTypeInst->mAttributeData == NULL))
		{
			Fail(StrFormat("'%s' is not an attribute class", TypeToString(attrType).c_str()), attributesDirective->mAttributeTypeRef); //CS0616
			continue;
		}

		if (mCurTypeInstance != NULL)
			AddDependency(attrTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_CustomAttribute);

		customAttribute.mType = attrTypeInst;		
		
		BfConstResolver constResolver(this);		
		
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
					autoComplete->CheckNode(assignExpr->mLeft);
				
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
						BfTypedValue result = constResolver.Resolve(assignExpr->mRight, fieldTypeInst.mResolvedType);
						if (result)
						{
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
								BfTypedValue result = constResolver.Resolve(assignExpr->mRight, propType);
								if (result)
								{
									BF_ASSERT(result.mType == propType);
									CurrentAddToConstHolder(result.mValue);
									setProperty.mParam = result;
									customAttribute.mSetProperties.push_back(setProperty);
								}
							}
						}
					}
				}

				if ((!handledExpr) && (assignExpr->mRight != NULL))
					constResolver.Resolve(assignExpr->mRight);
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
					resolvedArg.mTypedValue = constResolver.Resolve(arg);

				if (!inPropSet)
					argValues.push_back(resolvedArg);
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
					
		BfMethodMatcher methodMatcher(attributesDirective, this, "", argValues, NULL);
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

				if ((!isFailurePass) && (!CheckProtection(checkMethod->mProtection, false, false)))
					continue;

				methodMatcher.CheckMethod(attrTypeInst, checkMethod, isFailurePass);
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
					constResolver.Resolve(expr);
			}
		}

		// Move all those to the constHolder
		for (auto& ctorArg : customAttribute.mCtorArgs)
		{
			CurrentAddToConstHolder(ctorArg);			
		}
		
		if (attributesDirective->mAttributeTargetSpecifier != NULL)
		{
			targetOverride = BfAttributeTargets_ReturnValue;
			if (attrTarget != BfAttributeTargets_Method)
			{
				Fail(StrFormat("'%s' is not a valid attribute location for this declaration. Valid attribute locations for this declaration are '%s'. All attributes in this block will be ignored.",
					GetAttributesTargetListString(targetOverride).c_str(), GetAttributesTargetListString(attrTarget).c_str()), attributesDirective->mAttributeTargetSpecifier); // CS0657
			}
			
			success = false;
		}

		if ((success) && (targetOverride != (BfAttributeTargets)0))
		{
			if ((targetOverride == BfAttributeTargets_ReturnValue) && (attrTarget == BfAttributeTargets_Method))
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
			else
			{
				// Failed - ignore
				success = false;
			}
		}
		
		if (success)
		{
			if (!attrTypeInst->mAttributeData->mAllowMultiple)
			{
				for (auto& prevCustomAttribute : customAttributes->mAttributes)
				{
					if (prevCustomAttribute.mType == attrTypeInst)
					{
						Fail(StrFormat("Duplicate '%s' attribute", attributesDirective->mAttributeTypeRef->ToString().c_str()), attributesDirective->mAttributeTypeRef); // CS0579						
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

BfCustomAttributes* BfModule::GetCustomAttributes(BfAttributeDirective* attributesDirective, BfAttributeTargets attrType)
{
	BfCustomAttributes* customAttributes = new BfCustomAttributes();
	GetCustomAttributes(customAttributes, attributesDirective, attrType);
	return customAttributes;
}

void BfModule::ProcessTypeInstCustomAttributes(bool& isPacked, bool& isUnion, bool& isCRepr, bool& isOrdered)
{
	if (mCurTypeInstance->mCustomAttributes != NULL)
	{
		for (auto& customAttribute : mCurTypeInstance->mCustomAttributes->mAttributes)
		{
			String typeName = TypeToString(customAttribute.mType);
			if (typeName == "System.PackedAttribute")
			{
				isPacked = true;
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
				for (auto setProp : customAttribute.mSetProperties)
				{
					BfPropertyDef* propertyDef = setProp.mPropertyRef;
					if (propertyDef->mName == "AssumeInstantiated")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if ((constant != NULL) && (constant->mBool))
							mCurTypeInstance->mHasBeenInstantiated = true;
					}
				}
			}
		}
	}
}

// Checking to see if we're an attribute or not
void BfModule::ProcessCustomAttributeData()
{
	bool isAttribute = false;
	auto checkTypeInst = mCurTypeInstance->mBaseType;
	while (checkTypeInst != NULL)
	{
		if (checkTypeInst->mTypeDef == mCompiler->mAttributeTypeDef)
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
			if (customAttribute.mType->mTypeDef == mCompiler->mAttributeUsageAttributeTypeDef)
			{
				if (customAttribute.mCtorArgs.size() > 0)
				{
					auto constant = mCurTypeInstance->mConstHolder->GetConstant(customAttribute.mCtorArgs[0]);
					if ((constant != NULL) && (mBfIRBuilder->IsInt(constant->mTypeCode)))
						attributeData->mAttributeTargets = (BfAttributeTargets)constant->mInt32;					
				}

				for (auto& setProp : customAttribute.mSetProperties)
				{
					BfPropertyDef* propDef = setProp.mPropertyRef;

					if (propDef->mName == "AllowMultiple")
					{						
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if (constant != NULL)
							attributeData->mAllowMultiple = constant->mBool;
					}
					else if (propDef->mName == "Inherited")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if (constant != NULL)
							attributeData->mInherited = constant->mBool;
					}
					else if (propDef->mName == "ValidOn")
					{
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(setProp.mParam.mValue);
						if (constant != NULL)
							attributeData->mAttributeTargets = (BfAttributeTargets)constant->mInt32;
					}
				}

				hasCustomAttribute = true;
			}
		}
	}

	if ((!hasCustomAttribute) && (mCurTypeInstance->mBaseType->mAttributeData != NULL))
	{		
		attributeData->mAttributeTargets = mCurTypeInstance->mBaseType->mAttributeData->mAttributeTargets;
		attributeData->mInherited = mCurTypeInstance->mBaseType->mAttributeData->mInherited;
		attributeData->mAllowMultiple = mCurTypeInstance->mBaseType->mAttributeData->mAllowMultiple;
	}

	mCurTypeInstance->mAttributeData = attributeData;
}

bool BfModule::TryGetConstString(BfIRConstHolder* constHolder, BfIRValue irValue, StringImpl& str)
{
	auto constant = constHolder->GetConstant(irValue);
	if (constant == NULL)
		return false;
	if (!BfIRConstHolder::IsInt(constant->mTypeCode))
		return false;
	int stringId = constant->mInt32;
	BfStringPoolEntry* entry = NULL;
	if (mContext->mStringObjectIdMap.TryGetValue(stringId, &entry))
	{
		str = entry->mString;
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

	if (primType)
	{
		auto constant = mBfIRBuilder->GetConstant(value.mValue);
		if (constant != NULL)
		{
			if ((allowUndef) && (constant->mConstType == BfConstType_Undef))
			{
				variant.mTypeCode = BfTypeCode_Let;
				return variant;
			}

			switch (constant->mTypeCode)
			{
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
			case BfTypeCode_Single:
			case BfTypeCode_Double:
			case BfTypeCode_Char8:
			case BfTypeCode_Char16:
			case BfTypeCode_Char32:
				variant.mTypeCode = constant->mTypeCode;
				variant.mInt64 = constant->mInt64;
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
		auto elementType = typedValue.mType->GetUnderlyingType();
		if (typedValue.IsAddr())
		{
			if (elementType->IsValuelessType())
			{
				BF_ASSERT(!typedValue.mValue);
				typedValue = BfTypedValue(typedValue.mValue, elementType, true);
			}
			else
				typedValue = BfTypedValue(mBfIRBuilder->CreateLoad(typedValue.mValue), elementType, true);
		}
		else
			typedValue = BfTypedValue(typedValue.mValue, elementType, true);

		if (typedValue.mType->IsValuelessType())
		{
			BF_ASSERT(typedValue.mValue.IsFake());
		}
	}
	return typedValue;
}

BfTypedValue BfModule::LoadValue(BfTypedValue typedValue, BfAstNode* refNode, bool isVolatile)
{
	if (!typedValue.IsAddr())
		return typedValue;

	if (typedValue.mType->IsValuelessType())
		return BfTypedValue(mBfIRBuilder->GetFakeVal(), typedValue.mType, false);

	BfIRValue loadedVal = typedValue.mValue;
	if (loadedVal)
	{
		/*if (isVolatile)
			mBfIRBuilder->CreateFence(BfIRFenceType_AcquireRelease);*/
		PopulateType(typedValue.mType, BfPopulateType_Data);
		loadedVal = mBfIRBuilder->CreateAlignedLoad(loadedVal, std::max(1, (int)typedValue.mType->mAlign), isVolatile);		
	}
	return BfTypedValue(loadedVal, typedValue.mType, false);
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

BfTypedValue BfModule::MakeAddressable(BfTypedValue typedVal)
{
	bool wasReadOnly = typedVal.IsReadOnly();

 	if ((typedVal.mType->IsValueType()) &&
 		(!typedVal.mType->IsValuelessType()))
	{
		wasReadOnly = true; // Any non-addr is implicitly read-only
	
		//static int gCallIdx = 0;

		if (typedVal.IsAddr())
			return typedVal;
		BfType* type = typedVal.mType;		
		PopulateType(type);
		auto tempVar = CreateAlloca(type);
		if (typedVal.IsSplat())
			AggregateSplatIntoAddr(typedVal, tempVar);
		else
			mBfIRBuilder->CreateAlignedStore(typedVal.mValue, tempVar, type->mAlign);
		
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

BfIRValue BfModule::ExtractSplatValue(BfTypedValue typedValue, int componentIdx, BfType* wantType, bool* isAddr)
{
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
						val = mBfIRBuilder->CreateLoad(val);
						break;
					}
				}
			}

			if (val)
				break;

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

	BF_FATAL("Splat not found");
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
					BfTypedValue innerVal = typedValue;
					innerVal.mType = fieldType;
					return innerVal;
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
					BfTypedValue innerVal = typedValue;
					innerVal.mType = fieldType;
					return innerVal;
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
		return mBfIRBuilder->CreateLoad(addrVal);
	}	
	return mBfIRBuilder->CreateExtractValue(typedValue.mValue, dataIdx);
}

BfIRValue BfModule::CreateIndexedValue(BfType* elementType, BfIRValue value, BfIRValue indexValue, bool isElementIndex)
{
	if (elementType->IsSizeAligned())
	{
		if (isElementIndex)
			return mBfIRBuilder->CreateInBoundsGEP(value, GetConstValue(0), indexValue);
		else
			return mBfIRBuilder->CreateInBoundsGEP(value, indexValue);
	}
	
	auto ptrType = CreatePointerType(elementType);
	auto ofsVal = mBfIRBuilder->CreateNumericCast(indexValue, true, BfTypeCode_IntPtr);
	auto ofsScaledVal = mBfIRBuilder->CreateMul(ofsVal, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, elementType->GetStride()));
	auto intVal = mBfIRBuilder->CreatePtrToInt(value, BfTypeCode_IntPtr);
	auto newIntVal = mBfIRBuilder->CreateAdd(intVal, ofsScaledVal);
	return mBfIRBuilder->CreateIntToPtr(newIntVal, mBfIRBuilder->MapType(ptrType));
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


	if (typedValue.mKind == BfTypedValueKind_TempAddr)
	{
		Fail(StrFormat("Cannot %s value", modifyType), refNode);
		return false;
	}

	return true;
}

bool BfModule::CompareMethodSignatures(BfMethodInstance* methodA, BfMethodInstance* methodB)
{
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
		if (operatorA->mOperatorDeclaration->mBinOp != operatorB->mOperatorDeclaration->mBinOp)
			return false;
		if (operatorA->mOperatorDeclaration->mUnaryOp != operatorB->mOperatorDeclaration->mUnaryOp)
			return false;
		if (operatorA->mOperatorDeclaration->mIsConvOperator)
		{
			if (methodA->mReturnType != methodB->mReturnType)
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
		if ((methodA->GetParamType(paramIdx + implicitParamCountA) != methodB->GetParamType(paramIdx + implicitParamCountB)) ||
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

bool BfModule::IsCompatibleInterfaceMethod(BfMethodInstance* iMethodInst, BfMethodInstance* methodInstance)
{
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

	for (int paramIdx = 0; paramIdx < (int)iMethodInst->GetParamCount(); paramIdx++)
	{
		if (iMethodInst->GetParamKind(paramIdx) != methodInstance->GetParamKind(paramIdx))
			return false;

		BfType* iParamType = iMethodInst->GetParamType(paramIdx);
		BfType* methodParamType = methodInstance->GetParamType(paramIdx);

		if (iParamType->IsSelf())
		{
			if (methodParamType != methodInstance->GetOwner())
				return false;
		}
		else if (!iParamType->IsGenericParam())
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

	if (mBfIRBuilder->mIgnoreWrites)
		return BfModuleMethodInstance(methodInstance, mBfIRBuilder->GetFakeVal());

	if (mCompiler->IsSkippingExtraResolveChecks())
		return BfModuleMethodInstance(methodInstance, BfIRFunction());

	if (methodInstance->mMethodDef->mMethodType == BfMethodType_Mixin)
	{
		return BfModuleMethodInstance(methodInstance, BfIRFunction());
	}	
	
	bool isInlined = (methodInstance->mAlwaysInline) || ((flags & BfGetMethodInstanceFlag_ForceInline) != 0);
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
		mFuncReferences[methodRef] = func;
	BfLogSysM("Adding func reference. Module:%p MethodInst:%p NewLLVMFunc:%d OldLLVMFunc:%d\n", this, methodInstance, func.mId, methodInstance->mIRFunction.mId);

	if ((isInlined) && (!mIsScratchModule))
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
			if ((attributesDirective != NULL) && (mCompiler->mResolvePassData != NULL) &&
				(attributesDirective->IsFromParser(mCompiler->mResolvePassData->mParser)) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
			{
				mCompiler->mResolvePassData->mSourceClassifier->VisitChild(attributesDirective);
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
	if (((flags & BfGetMethodInstanceFlag_ForceInline) != 0) && (mCompiler->mIsResolveOnly))
	{
		// Don't bother inlining for resolve-only
		flags = (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline);
	}

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

 		BF_FATAL("Cannot find local method");
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
		PopulateType(typeInst, BfPopulateType_Full);
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
		if ((!mIsReified) && (instModule->mIsReified))
		{
			BF_ASSERT(!mCompiler->mIsResolveOnly);
			// A resolve-only module is specializing a method from a type in a reified module, 
			//  we need to take care that this doesn't cause anything new to become reified
			return mContext->mUnreifiedModule->GetMethodInstance(typeInst, methodDef, methodGenericArguments, (BfGetMethodInstanceFlags)(flags | BfGetMethodInstanceFlag_ExplicitResolveOnlyPass), foreignType);			
		}
		else
		{
			if ((mIsReified) && (!instModule->mIsReified))
			{
				if (!instModule->mReifyQueued)
				{
					BF_ASSERT((mCompiler->mCompileState != BfCompiler::CompileState_Unreified) && (mCompiler->mCompileState != BfCompiler::CompileState_VData));

					BfLogSysM("Queueing ReifyModule: %p\n", instModule);
					mContext->mReifyModuleWorkList.Add(instModule);
					instModule->mReifyQueued = true;
				}

				// This ensures that the method will actually be created when it gets reified
				BfMethodSpecializationRequest* specializationRequest = mContext->mMethodSpecializationWorkList.Alloc();
				specializationRequest->mFromModule = typeInst->mModule;
				specializationRequest->mFromModuleRevision = typeInst->mModule->mRevision;
				specializationRequest->mMethodDef = methodDef;
				specializationRequest->mMethodGenericArguments = methodGenericArguments;
				specializationRequest->mType = typeInst;				
			}

			auto defFlags = (BfGetMethodInstanceFlags)(flags & ~BfGetMethodInstanceFlag_ForceInline);

			// Not extern
			// Create the instance in the proper module and then create a reference in this one
			moduleMethodInst = instModule->GetMethodInstance(typeInst, methodDef, methodGenericArguments, defFlags, foreignType);
			tryModuleMethodLookup = true;

			if ((mIsReified) && (!moduleMethodInst.mMethodInstance->mIsReified))
			{
				CheckHotMethod(moduleMethodInst.mMethodInstance, "");
			}
		}
	}		

	if (tryModuleMethodLookup)
		return ReferenceExternalMethodInstance(moduleMethodInst.mMethodInstance, flags);

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
	Array<BfProject*> projectList;
	
	bool isUnspecializedPass = (flags & BfGetMethodInstanceFlag_UnspecializedPass) != 0;
	if ((isUnspecializedPass) && (methodDef->mGenericParams.size() == 0))
		isUnspecializedPass = false;

	// Check for whether we are an extension method from a project that does not hold the root type, AND that isn't already the project for this type
	bool isExternalExtensionMethod = false; 
	if ((!typeInst->IsUnspecializedType()) && (!isUnspecializedPass))
	{
		if (((flags & BfGetMethodInstanceFlag_ForeignMethodDef) == 0) && (methodDef->mDeclaringType->mProject != typeInst->mTypeDef->mProject))
		{
			auto specProject = methodDef->mDeclaringType->mProject;

			isExternalExtensionMethod = true;
			if (typeInst->IsGenericTypeInstance())
			{
				auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
				if (genericTypeInst->mProjectsReferenced.empty())
					genericTypeInst->GenerateProjectsReferenced();
				if (genericTypeInst->mProjectsReferenced.Contains(specProject))
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
			auto genericTypeInst = (BfGenericTypeInstance*)typeInst;
			if (genericTypeInst->mProjectsReferenced.empty())
				genericTypeInst->GenerateProjectsReferenced();			
			typeProjectsCounts = (int)genericTypeInst->mProjectsReferenced.size();
			projectList.Insert(0, &genericTypeInst->mProjectsReferenced[0], genericTypeInst->mProjectsReferenced.size());
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
				methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingDecl;
		}
	}
	else
	{
		methodInstGroup = &typeInst->mMethodInstanceGroups[methodDef->mIdx];	
	}

	if (methodInstGroup->mOnDemandKind == BfMethodOnDemandKind_NotSet)
	{
		BfLogSysM("Forcing BfMethodOnDemandKind_NotSet to BfMethodOnDemandKind_AlwaysInclude for Method:%s in Type:%p\n", methodDef->mName.c_str(), typeInst);
		methodInstGroup->mOnDemandKind = BfMethodOnDemandKind_AlwaysInclude;
	}
	
	BfIRFunction prevIRFunc;
	
	bool doingRedeclare = false;
	BfMethodInstance* methodInstance = NULL;

	auto _SetReified = [&]()
	{
		methodInstance->mIsReified = true;		
	};

	if (lookupMethodGenericArguments.size() == 0)
	{
		methodInstance = methodInstGroup->mDefault;
		
		if ((methodInstance != NULL) && (isReified) && (!methodInstance->mIsReified))
		{
			MarkDerivedDirty(typeInst);

			if (mIsScratchModule)
			{
				BfLogSysM("Marking scratch module method instance as reified: %p\n", methodInstance);
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
						BF_ASSERT(methodInstance->mMethodProcessRequest->mFromModule == mContext->mUnreifiedModule);
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
							StringT<128> mangledName;
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
		BF_ASSERT(lookupMethodGenericArguments.size() != 0);
		if (methodInstGroup->mMethodSpecializationMap == NULL)
			methodInstGroup->mMethodSpecializationMap = new BfMethodInstanceGroup::MapType();
		
		BfMethodInstance** methodInstancePtr = NULL;
		if (methodInstGroup->mMethodSpecializationMap->TryGetValue(lookupMethodGenericArguments, &methodInstancePtr))
		{
			methodInstance = *methodInstancePtr;

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
				}
			}
		}
	}
	
	if ((methodInstance != NULL) && (!doingRedeclare))
	{					
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
				AddMethodToWorkList(methodInstance);				
			}
			else
			{
				methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Referenced;
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
				methodInstance->mIRFunction = CreateFunctionFrom(methodInstance, false, isInlined);
				BF_ASSERT((methodInstance->mDeclModule == this) || (methodInstance->mDeclModule == mContext->mUnreifiedModule) || (methodInstance->mDeclModule == NULL));
				methodInstance->mDeclModule = this;

				// Add this inlined def to ourselves
				if ((methodInstance->mAlwaysInline) && (HasCompiledOutput()) && (!methodInstance->mIsUnspecialized))
				{
					mIncompleteMethodCount++;
					BfInlineMethodRequest* inlineMethodRequest = mContext->mInlineMethodWorkList.Alloc();
					inlineMethodRequest->mType = typeInst;
					inlineMethodRequest->mFromModule = this;
					inlineMethodRequest->mFunc = methodInstance->mIRFunction;
					inlineMethodRequest->mFromModuleRevision = mRevision;
					inlineMethodRequest->mMethodInstance = methodInstance;

					BfLogSysM("mInlineMethodWorkList %p for method %p in module %p in GetMethodInstance\n", inlineMethodRequest, methodInstance, this);
					BF_ASSERT(mIsModuleMutable);
				}
			}
		}

		if (mCompiler->IsSkippingExtraResolveChecks())
			return BfModuleMethodInstance(methodInstance, BfIRFunction());
		else
		{
			if (methodInstance->mDeclModule != this)
				return ReferenceExternalMethodInstance(methodInstance, flags);				

			if ((!methodInstance->mIRFunction) && (mIsModuleMutable) && (!mBfIRBuilder->mIgnoreWrites))
			{
				auto importKind = methodInstance->GetImportCallKind();
				if (importKind != BfImportCallKind_None)
				{
					BfLogSysM("DllImportGlobalVar creating %p\n", methodInstance);
					methodInstance->mIRFunction = mBfIRBuilder->GetFakeVal();
					mFuncReferences[methodInstance] = CreateDllImportGlobalVar(methodInstance, true);;
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
		if (lookupMethodGenericArguments.size() == 0)
		{
			BF_ASSERT(methodInstGroup->mDefault == NULL);
			methodInstance = new BfMethodInstance();
			methodInstGroup->mDefault = methodInstance;

			BfLogSysM("Created Default MethodInst: %p TypeInst: %p Group: %p\n", methodInstance, typeInst, methodInstGroup);
		}
		else
		{			
			BfMethodInstance** methodInstancePtr = NULL;
			bool added = methodInstGroup->mMethodSpecializationMap->TryAdd(lookupMethodGenericArguments, NULL, &methodInstancePtr);
			BF_ASSERT(added);
			methodInstance = new BfMethodInstance();
			*methodInstancePtr = methodInstance;

			BfLogSysM("Created Specialized MethodInst: %p TypeInst: %p\n", methodInstance, typeInst);
		}

		if ((prevIRFunc) && (!prevIRFunc.IsFake()))
			methodInstance->mIRFunction = prevIRFunc; // Take it over
	}
		
	/*// 24 bits for typeid, 20 for method id, 20 for specialization index
	methodInstance->mMethodId = ((int64)typeInst->mTypeId) + ((int64)methodInstGroup->mMethodIdx << 24);
	if (methodInstGroup->mMethodSpecializationMap != NULL)
		methodInstance->mMethodId += ((int64)methodInstGroup->mMethodSpecializationMap->size() << 44);*/
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
	
	if ((typeInst->mTypeDef == mCompiler->mValueTypeTypeDef) && (methodDef->mName == BF_METHODNAME_EQUALS))
	{
		if (!lookupMethodGenericArguments.empty())
		{
			auto compareType = lookupMethodGenericArguments[0];
			PopulateType(compareType, BfPopulateType_Data);
			if (compareType->IsSplattable())
				methodInstance->mAlwaysInline = true;
		}
	}

	if (methodDef->mDeclaringType->mTypeDeclaration != typeInst->mTypeDef->mTypeDeclaration)
	{
		// With extensions we can have multiple definitions of the same method
		//methodInstance->mMangleWithIdx = true;
	}

	BfModule* declareModule = GetOrCreateMethodModule(methodInstance);
		
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
			BF_FATAL("Generic method argument counts mismatch");
			return NULL;
		}

		for (auto genericArg : sanitizedMethodGenericArguments)
		{
			if (genericArg->IsPrimitiveType())
				genericArg = GetWrappedStructType(genericArg);
			AddDependency(genericArg, typeInst, BfDependencyMap::DependencyFlag_MethodGenericArg);
			if (genericArg->IsMethodRef())
			{
				auto methodRefType = (BfMethodRefType*)genericArg;
				//AddDependency(genericArg, typeInst, BfDependencyMap::DependencyFlag_);
			}
		}
	}

	// Generic constraints
	for (int genericParamIdx = 0; genericParamIdx < (int)methodDef->mGenericParams.size(); genericParamIdx++)
	{
		auto genericParamInstance = new BfGenericMethodParamInstance(methodDef, genericParamIdx);
		methodInstance->GetMethodInfoEx()->mGenericParams.push_back(genericParamInstance);		
	}
	
	bool addToWorkList = !processNow;
	if (mCompiler->GetAutoComplete() != NULL)
	{
		if (typeInst->IsSpecializedByAutoCompleteMethod())
			addToWorkList = false;
	}

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
		BF_ASSERT(!methodInstance->mIsReified);
		declareModule = mContext->mUnreifiedModule;
	}

	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(declareModule->mCurMethodInstance, methodInstance);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(declareModule->mCurTypeInstance, typeInst);
	SetAndRestoreValue<BfFilePosition> prevFilePos(declareModule->mCurFilePosition);

	declareModule->DoMethodDeclaration(methodDef->GetMethodDeclaration(), false, addToWorkList);
	
	if (processNow)
	{
		SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);
		ProcessMethod(methodInstance);
	}

	if (mCompiler->IsSkippingExtraResolveChecks())	
		return BfModuleMethodInstance(methodInstance, BfIRFunction());
	
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
		BF_ASSERT((outmostMethodInstance->mIdHash != 0) || (outmostMethodInstance->mIsAutocompleteMethod));
		hashCtx.Mixin(outmostMethodInstance->mIdHash);
	}	

	methodInstance->mIdHash = (int64)hashCtx.Finish64();
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
		
		StringT<128> slotVarName;
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

	return mBfIRBuilder->CreateLoad(globalValue/*, "slotOfs"*/);
}

void BfModule::HadSlotCountDependency()
{
	if (mCompiler->mIsResolveOnly)
		return;
	BF_ASSERT(!mBfIRBuilder->mIgnoreWrites);
	BF_ASSERT((mUsedSlotCount == BF_MAX(mCompiler->mMaxInterfaceSlots, 0)) || (mUsedSlotCount == -1));
	mUsedSlotCount = BF_MAX(mCompiler->mMaxInterfaceSlots, 0);	
}

BfTypedValue BfModule::ReferenceStaticField(BfFieldInstance* fieldInstance)
{	
	if (mIsScratchModule)
	{
		// Just fake it for the extern and unspecialized modules
		auto ptrType = CreatePointerType(fieldInstance->GetResolvedType());
		return BfTypedValue(GetDefaultValue(ptrType), fieldInstance->GetResolvedType(), true);
	}
	
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
	
	BfIRValue* globalValuePtr = NULL;
	if (mStaticFieldRefs.TryGetValue(fieldInstance, &globalValuePtr))
	{
		globalValue = *globalValuePtr;
		BF_ASSERT(globalValue);
	}
	else
	{				
		StringT<128> staticVarName;
		BfMangler::Mangle(staticVarName, mCompiler->GetMangleKind(), fieldInstance);

		auto typeType = fieldInstance->GetResolvedType();
		if ((fieldDef->mIsExtern) && (fieldDef->mIsConst) && (typeType->IsPointer()))
		{
			typeType = typeType->GetUnderlyingType();
		}

		PopulateType(typeType);
		if ((typeType != NULL) && (!typeType->IsValuelessType()))
		{
			globalValue = mBfIRBuilder->CreateGlobalVariable(
				mBfIRBuilder->MapType(typeType),
				false,
				BfIRLinkageType_External,
				BfIRValue(),
				staticVarName,
				IsThreadLocal(fieldInstance));

			if (!mBfIRBuilder->mIgnoreWrites)
			{
				// Only store this if we actually did the creation
				BF_ASSERT(globalValue);
				mStaticFieldRefs[fieldInstance] = globalValue;
			}
				
			BfLogSysM("Mod:%p Type:%p ReferenceStaticField %p -> %p\n", this, fieldInstance->mOwner, fieldInstance, globalValue);

		}
	}	

	auto type = fieldInstance->GetResolvedType();
	if (type->IsValuelessType())
		return BfTypedValue(globalValue, type);

	return BfTypedValue(globalValue, type, !fieldDef->mIsConst);
}

BfTypedValue BfModule::GetThis()
{
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

// 	if (useMethodState->HasNonStaticMixin())
// 	{
// 		auto checkMethodState = useMethodState;
// 		while (checkMethodState != NULL)
// 		{
// 			for (int localIdx = (int)checkMethodState->mLocals.size() - 1; localIdx >= 0; localIdx--)
// 			{
// 				auto varDecl = checkMethodState->mLocals[localIdx];
// 				if (varDecl->mName == "this")
// 				{
// 					varDecl->mReadFromId = useMethodState->GetRootMethodState()->mCurAccessId++;
// 					if (varDecl->mIsSplat)
// 					{
// 						return BfTypedValue(varDecl->mValue, varDecl->mResolvedType, BfTypedValueKind_ThisSplatHead);
// 					}
// 					else if ((varDecl->mResolvedType->IsValueType()) && (varDecl->mAddr))
// 					{
// 						return BfTypedValue(varDecl->mAddr, varDecl->mResolvedType, BfTypedValueKind_ThisAddr);
// 					}
// 					return BfTypedValue(varDecl->mValue, varDecl->mResolvedType, varDecl->mResolvedType->IsValueType() ? BfTypedValueKind_ThisAddr : BfTypedValueKind_ThisValue);
// 				}
// 			}
// 		
// 			checkMethodState = checkMethodState->mPrevMethodState;
// 		}
// 	}

	// Check mixin state for 'this'
	{
		auto checkMethodState = useMethodState;
		while (checkMethodState != NULL)
		{
			if (checkMethodState->mMixinState != NULL)
			{
				BfTypedValue thisValue = checkMethodState->mMixinState->mTarget;
				if (thisValue)
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
	
	bool preferValue = !IsTargetingBeefBackend(); 

	if (useMethodState->mLocals.IsEmpty())
	{
		// This can happen in rare non-capture cases, such as when we need to do a const expression resolve for a sized-array return type on a local method
		BF_ASSERT(useMethodState->mPrevMethodState != NULL);
		return BfTypedValue();
	}

	auto thisLocal = useMethodState->mLocals[0];
	BF_ASSERT(thisLocal->mIsThis);
	BfIRValue thisValue;
	if ((preferValue) || (!thisLocal->mAddr) /*|| (thisLocal->mIsSplat)*/)
		thisValue = thisLocal->mValue;
	else if ((thisLocal->mIsSplat) || (thisLocal->mIsLowered))
		thisValue = thisLocal->mAddr;
	else
		thisValue = mBfIRBuilder->CreateLoad(thisLocal->mAddr);
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
	return (mCurMethodInstance->GetNumGenericArguments() != 0) || (mCurTypeInstance->IsGenericTypeInstance());
}

bool BfModule::IsInSpecializedGeneric()
{
	if ((mCurMethodInstance == NULL) || (mCurMethodInstance->mIsUnspecialized))
		return false;
	return (mCurMethodInstance->GetNumGenericArguments() != 0) || (mCurTypeInstance->IsGenericTypeInstance());
}

bool BfModule::IsInSpecializedSection()
{
	return IsInSpecializedGeneric() || 
		((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL));
}

bool BfModule::IsInUnspecializedGeneric()
{
	if (mCurMethodInstance != NULL)
		return mCurMethodInstance->mIsUnspecialized;
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
		initLocalVariables = BfTypeOptions::Apply(initLocalVariables, typeOptions->mInitLocalVariables);	
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
	if (localVar->mResolvedType->IsVar())
	{
		BF_ASSERT((mCurMethodInstance->mIsUnspecialized) || (mCurMethodState->mClosureState != NULL) || (mHadVarUsage));
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

BfLocalVariable* BfModule::AddLocalVariableDef(BfLocalVariable* localVarDef, bool addDebugInfo, bool doAliasValue, BfIRValue declareBefore, BfIRInitType initType)
{
	if ((localVarDef->mValue) && (!localVarDef->mAddr) && (IsTargetingBeefBackend()))
	{
		if ((!localVarDef->mValue.IsConst()) && (!localVarDef->mValue.IsArg()) && (!localVarDef->mValue.IsFake()))
		{
			mBfIRBuilder->CreateValueScopeRetain(localVarDef->mValue);
			mCurMethodState->mCurScope->mHadScopeValueRetain = true;
		}
	}

	if ((addDebugInfo) && (mBfIRBuilder->DbgHasInfo()) &&
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
					(constant->mConstType == BfConstType_Array);
				if (isConstant)
				{
					if (localVarDef->mResolvedType->IsComposite())
					{
						mBfIRBuilder->PopulateType(localVarDef->mResolvedType);
						auto constMem = mBfIRBuilder->ConstToMemory(localVarDef->mConstValue);
												
						if (IsTargetingBeefBackend())
						{	
							diValue = mBfIRBuilder->CreateAliasValue(constMem);
							didConstToMem = true;

							diType = mBfIRBuilder->DbgCreateReferenceType(diType);
						}
						else
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
						
			auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
				localVarDef->mName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType, initType);
			localVarDef->mDbgVarInst = diVariable;
			
			if (mBfIRBuilder->HasDebugLocation())
			{
				if ((isConstant) && (!didConstToMem))
				{
					localVarDef->mDbgDeclareInst = mBfIRBuilder->DbgInsertValueIntrinsic(localVarDef->mConstValue, diVariable);
				}
				else
				{					
					if ((IsTargetingBeefBackend()) && (doAliasValue))
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

	localVarDef->mDeclBlock = mBfIRBuilder->GetInsertBlock();
	DoAddLocalVariable(localVarDef);
	
	auto rootMethodState = mCurMethodState->GetRootMethodState();

	if (localVarDef->mLocalVarId == -1)
	{
		BF_ASSERT(rootMethodState->mCurLocalVarId >= 0);
		localVarDef->mLocalVarId = rootMethodState->mCurLocalVarId++;
	}
	if ((localVarDef->mNameNode != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		mCompiler->mResolvePassData->mAutoComplete->CheckLocalDef(localVarDef->mNameNode, localVarDef);

	if ((localVarDef->mNameNode != NULL) && (mCurMethodInstance != NULL))
	{		
		bool isClosureProcessing = (mCurMethodState->mClosureState != NULL) && (!mCurMethodState->mClosureState->mCapturing);		
		if ((!isClosureProcessing) && (mCompiler->mResolvePassData != NULL) && (localVarDef->mNameNode != NULL))
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
		BF_ASSERT((!mBfIRBuilder->mIgnoreWrites) || (mHadVarUsage));

		BfType* dbgType = mCurMethodInstance->mReturnType;
		BfIRValue dbgValue = mCurMethodState->mRetVal.mValue;
		if (mCurMethodInstance->HasStructRet())
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
			"__return", mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(dbgType));
		auto declareCall = mBfIRBuilder->DbgInsertDeclare(dbgValue, mCurMethodState->mDIRetVal);
	}	
}

void BfModule::CheckVariableDef(BfLocalVariable* variableDef)
{	
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
				error = Fail(StrFormat("A parameter named '%s' has already been declared.", variableDef->mName.c_str()), variableDef->mNameNode);
			else if (checkLocal->mLocalVarIdx < mCurMethodState->mCurScope->mLocalVarStart)
				error = Fail(StrFormat("A variable named '%s' has already been declared in this parent's scope.", variableDef->mName.c_str()), variableDef->mNameNode);
			else
				error = Fail(StrFormat("A variable named '%s' has already been declared in this scope.", variableDef->mName.c_str()), variableDef->mNameNode);
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
	return mSystem->GetTypeOptions(mCurTypeInstance->mTypeOptionsIdx);
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

			std::unordered_set<BfDeferredCallEntry*> handledSet;
			BfDeferredCallEntry* deferredCallEntry = checkScope->mDeferredCallEntries.mHead;
			while (deferredCallEntry != NULL)
			{
				BfDeferredCallEmitState deferredCallEmitState;
				deferredCallEmitState.mCloseNode = deferCloseNode;
				SetAndRestoreValue<BfDeferredCallEmitState*> prevDeferredCallEmitState(mCurMethodState->mDeferredCallEmitState, &deferredCallEmitState);
				SetAndRestoreValue<bool> prevIgnoredWrites(mBfIRBuilder->mIgnoreWrites, deferredCallEntry->mIgnored);

				if (deferCloseNode != NULL)
				{										
					UpdateSrcPos(deferCloseNode);					
				}
				if (wantsNop)
					EmitEnsureInstructionAt();
				wantsNop = false;

				if (deferredCallEntry->mDeferredBlock != NULL)
				{
					auto itr = handledSet.insert(deferredCallEntry);
					if (!itr.second)
					{
						// Already handled, can happen if we defer again within the block
						deferredCallEntry = deferredCallEntry->mNext;
						continue;
					}
					auto prevHead = checkScope->mDeferredCallEntries.mHead;
					EmitDeferredCall(*deferredCallEntry);
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
					EmitDeferredCall(*deferredCallEntry);
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

void BfModule::MarkScopeLeft(BfScopeData* scopeData)
{
	if (mCurMethodState->mDeferredLocalAssignData != NULL)
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

	//for (int localIdx = scopeData->mLocalVarStart; localIdx < (int)mCurMethodState->mLocals.size(); localIdx++)

	// We mark all unassigned variables as assigned now, for avoiding "may be unassigned" usage cases like:
	//  int b;
	//  if (cond) return; 
	//  else b = 1;
	//  Use(b);
	for (int localIdx = 0; localIdx < (int)mCurMethodState->mLocals.size(); localIdx++)
	{
		auto localDef = mCurMethodState->mLocals[localIdx];
		if ((!localDef->mIsAssigned) && (!localDef->IsParam()))
			mCurMethodState->LocalDefined(localDef);
	}
}

void BfModule::CreateReturn(BfIRValue val)
{
	if (mCurMethodInstance->mReturnType->IsComposite())
	{
		// Store to sret
		BF_ASSERT(val);
		mBfIRBuilder->CreateStore(val, mBfIRBuilder->GetArgument(0));
		mBfIRBuilder->CreateRetVoid();
	}
	else
	{
		if (mCurMethodInstance->mReturnType->IsValuelessType())
		{
			mBfIRBuilder->CreateRetVoid();
		}
		else
		{
			BF_ASSERT(val);
			mBfIRBuilder->CreateRet(val);
		}
	}
}

void BfModule::EmitReturn(BfIRValue val)
{
	if (mCurMethodState->mIRExitBlock)
	{
		if (!mCurMethodInstance->mReturnType->IsValuelessType())
		{
			if (val) // We allow for val to be empty if we know we've already written the value to mRetVal
			{
				BfIRValue retVal = mCurMethodState->mRetVal.mValue;
				if (!mCurMethodState->mRetVal)
					retVal = mBfIRBuilder->CreateLoad(mCurMethodState->mRetValAddr);
				
				mBfIRBuilder->CreateStore(val, retVal);
			}
		}
		EmitDeferredScopeCalls(true, NULL, mCurMethodState->mIRExitBlock);
	}
	else
	{
		EmitDeferredScopeCalls(false, NULL);
		if (mCurMethodInstance->mReturnType->IsValuelessType())
			mBfIRBuilder->CreateRetVoid();
		else
			mBfIRBuilder->CreateRet(val);
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
		if (mCurMethodInstance->mReturnType->IsVoid())
			mBfIRBuilder->CreateRetVoid();
		else
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
		if (mCurMethodInstance->mIsUnspecializedVariation)
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
		if (mCurTypeInstance->mTypeDef->mSource->mParsingFailed)
			return;
	}
	if (mCurMethodInstance != NULL)
	{
		if ((mCurMethodInstance->mMethodDef->mDeclaringType != NULL) && (mCurMethodInstance->mMethodDef->mDeclaringType->mSource->mParsingFailed))
			return;
		if ((mCurMethodState != NULL) && (mCurMethodState->mMixinState != NULL) && (mCurMethodState->mMixinState->mMixinMethodInstance->mMethodDef->mDeclaringType->mSource->mParsingFailed))
			return;
	}
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
	auto fieldVal = mBfIRBuilder->CreateLoad(fieldPtr);	
	
	BfExprEvaluator exprEvaluator(this);
	
	SizedArray<BfIRType, 8> origParamTypes;
	BfIRType origReturnType;
	mCurMethodInstance->GetIRFunctionInfo(this, origReturnType, origParamTypes);

	if (mCurMethodInstance->mReturnType->IsValueType())
		mBfIRBuilder->PopulateType(mCurMethodInstance->mReturnType, BfIRPopulateType_Full);

	int thisIdx = 0;
	if (mCurMethodInstance->HasStructRet())
	{
		thisIdx = 1;
		staticFuncArgs.push_back(mBfIRBuilder->GetArgument(0));
		memberFuncArgs.push_back(mBfIRBuilder->GetArgument(0));
	}
	
	memberFuncArgs.push_back(BfIRValue()); // Push 'target'

	mCurMethodInstance->GetIRFunctionInfo(this, origReturnType, staticParamTypes, true);
	
	for (int i = 1; i < (int)mCurMethodState->mLocals.size(); i++)
	{
		BfTypedValue localVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[i], true);
		exprEvaluator.PushArg(localVal, staticFuncArgs);
		exprEvaluator.PushArg(localVal, memberFuncArgs);		
	}

	auto staticFunc = mBfIRBuilder->CreateFunctionType(origReturnType, staticParamTypes, false);
	auto staticFuncPtr = mBfIRBuilder->GetPointerTo(staticFunc);
	auto staticFuncPtrPtr = mBfIRBuilder->GetPointerTo(staticFuncPtr);

	auto trueBB = mBfIRBuilder->CreateBlock("if.then", true);
	auto falseBB = mBfIRBuilder->CreateBlock("if.else");	
	auto doneBB = mBfIRBuilder->CreateBlock("done");

	auto checkTargetNull = mBfIRBuilder->CreateIsNotNull(fieldVal);
	mBfIRBuilder->CreateCondBr(checkTargetNull, trueBB, falseBB);	

	BfIRValue nonStaticResult;
	BfIRValue staticResult;

	auto callingConv = GetCallingConvention(mCurTypeInstance, mCurMethodInstance->mMethodDef);

	/// Non-static invocation
	{	
		auto memberFuncPtr = mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapMethod(mCurMethodInstance));
		auto memberFuncPtrPtr = mBfIRBuilder->GetPointerTo(memberFuncPtr);
		
		mBfIRBuilder->SetInsertPoint(trueBB);
		memberFuncArgs[thisIdx] = mBfIRBuilder->CreateBitCast(fieldVal, mBfIRBuilder->MapType(mCurTypeInstance));
		auto fieldPtr = mBfIRBuilder->CreateInBoundsGEP(multicastDelegate, 0, 1); // Load 'delegate.mFuncPtr'
		auto funcPtrPtr = mBfIRBuilder->CreateBitCast(fieldPtr, memberFuncPtrPtr);
		auto funcPtr = mBfIRBuilder->CreateLoad(funcPtrPtr);		
		nonStaticResult = mBfIRBuilder->CreateCall(funcPtr, memberFuncArgs);
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
		auto funcPtr = mBfIRBuilder->CreateLoad(funcPtrPtr);		
		staticResult = mBfIRBuilder->CreateCall(funcPtr, staticFuncArgs);
		if (callingConv == BfIRCallingConv_ThisCall)
			callingConv = BfIRCallingConv_CDecl;
		if (callingConv != BfIRCallingConv_CDecl)
			mBfIRBuilder->SetCallCallingConv(staticResult, callingConv);
// 		if (!mSystem->IsCompatibleCallingConvention(BfCallingConvention_Unspecified, mCurMethodInstance->mMethodDef->mCallingConvention))
// 		{
// 			if (mCurMethodInstance->mMethodDef->mCallingConvention == BfCallingConvention_Stdcall)
// 				
// 		}		
		mCurMethodState->SetHadReturn(false);
		mCurMethodState->mLeftBlockUncond = false;
		mCurMethodState->mLeftBlockCond = false;
		mBfIRBuilder->CreateBr(doneBB);
	}

	mBfIRBuilder->AddBlock(doneBB);
	mBfIRBuilder->SetInsertPoint(doneBB);
	if ((mCurMethodInstance->mReturnType->IsValuelessType()) || (mCurMethodInstance->HasStructRet()))
	{
		mBfIRBuilder->CreateRetVoid();
	}
	else
	{
		auto phi = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(mCurMethodInstance->mReturnType), 2);
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
	
	if ((mCompiler->mResolvePassData->mParser != NULL) && (typeDef->mTypeDeclaration->IsFromParser(mCompiler->mResolvePassData->mParser)))
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

BfTypedValue BfModule::TryConstCalcAppend(BfMethodInstance* methodInst, SizedArrayImpl<BfIRValue>& args)
{
	BP_ZONE("BfModule::TryConstCalcAppend");

	if (mCompiler->mIsResolveOnly)
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
	bool isFirstRun = methodInst->mAppendAllocAlign < 0;
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
			if (methodInst->GetParamIsSplat(paramIdx))
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
		return BfTypedValue();

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
		if (!appendAllocVisitor.mFailed)
			constValue = appendAllocVisitor.mConstAccum;
		if (isFirstRun)
			mCurMethodInstance->mEndingAppendAllocAlign = appendAllocVisitor.mCurAppendAlign;

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
	BfTypeInstance* targetType = NULL;
	if (ctorDeclaration->mInitializer != NULL)
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorDeclaration->mInitializer->mTarget);
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
	if ((ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL))
	{
		argValues.Init(&ctorDeclaration->mInitializer->mArguments);
	}
	
	//
	{
		
	}
	BfFunctionBindResult bindResult;
	bindResult.mSkipThis = true;
	bindResult.mWantsArgs = true;
	{
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true);
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgFlag_DeferParamEval);
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
	BfTypedValue appendSizeTypedValue = TryConstCalcAppend(calcAppendMethodModule.mMethodInstance, bindResult.mIRArgs);
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
		argValues.Init(&ctorDeclaration->mInitializer->mArguments);
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgFlag_DeferParamEval);

		BfFunctionBindResult bindResult;
		bindResult.mSkipThis = true;
		bindResult.mWantsArgs = true;
		SetAndRestoreValue<BfFunctionBindResult*> prevBindResult(exprEvaluator.mFunctionBindResult, &bindResult);
		exprEvaluator.MatchConstructor(targetRefNode, NULL, target, targetType, argValues, true, true);		
		BF_ASSERT(bindResult.mIRArgs[0].IsFake());
		bindResult.mIRArgs.RemoveAt(0);
		calcAppendArgs = bindResult.mIRArgs;
	}	
	BF_ASSERT(calcAppendMethodModule.mFunc);
	appendSizeTypedValue = exprEvaluator.CreateCall(calcAppendMethodModule.mMethodInstance, calcAppendMethodModule.mFunc, false, calcAppendArgs);

	
	BF_ASSERT(appendSizeTypedValue.mType == GetPrimitiveType(BfTypeCode_IntPtr));
	return appendSizeTypedValue;
}

// This method never throws errors - it relies on the proper ctor actually throwing the errors
void BfModule::EmitCtorCalcAppend()
{
	if (mCompiler->mIsResolveOnly)
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
			if ((!fieldDef->mIsConst) && (fieldDef->mIsStatic) && (fieldDef->mInitializer != NULL))
			{
				// For extensions, only handle these fields in the appropriate extension
				if ((fieldDef->mDeclaringType->mTypeDeclaration != methodDef->mDeclaringType->mTypeDeclaration))
					continue;
								
				UpdateSrcPos(fieldDef->mInitializer);				
				
				auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldDef->mIdx];					
				if (!fieldInst->mFieldIncluded)
					continue;
				if (fieldInst->mResolvedType->IsVar())
				{
					BF_ASSERT(mHadVarUsage);
					continue;
				}
				auto assignValue = GetFieldInitializerValue(fieldInst);
				if (assignValue)
				{
					assignValue = LoadValue(assignValue);
					if (!assignValue.mType->IsValuelessType())
					{
						auto staticVarRef = ReferenceStaticField(fieldInst).mValue;
						mBfIRBuilder->CreateStore(assignValue.mValue, staticVarRef);
					}
				}
			}
		}
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
						if (fieldDef->mInitializer != NULL)
						{
							if (mCompiler->mResolvePassData->mSourceClassifier != NULL)
							{
								mCompiler->mResolvePassData->mSourceClassifier->SetElementType(fieldDef->mInitializer, BfSourceElementType_Normal);
								mCompiler->mResolvePassData->mSourceClassifier->VisitChild(fieldDef->mInitializer);
							}
							BfType* wantType = NULL;
							if ((!BfNodeIsA<BfVarTypeReference>(fieldDef->mTypeRef)) && (!BfNodeIsA<BfLetTypeReference>(fieldDef->mTypeRef)))
							{
								wantType = ResolveTypeRef(fieldDef->mTypeRef);
							}							
							CreateValueFromExpression(fieldDef->mInitializer, wantType, BfEvalExprFlags_FieldInitializer);
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
		auto basePtr = mBfIRBuilder->CreateBitCast(thisVal.mValue, mBfIRBuilder->MapTypeInstPtr(mContext->mBfObjectType));
		SizedArray<BfIRValue, 1> vals = { basePtr };
		result = mBfIRBuilder->CreateCall(dtorFunc.mFunc, vals);
		mBfIRBuilder->SetCallCallingConv(result, GetCallingConvention(dtorFunc.mMethodInstance));
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
		VisitCodeBlock(bodyBlock);
		if (bodyBlock->mCloseBrace != NULL)
		{
			UpdateSrcPos(bodyBlock->mCloseBrace);						
		}
	}
	else
	{
		if (methodDeclaration != NULL)
			UpdateSrcPos(methodDeclaration);
		else if (typeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(typeDef->mTypeDeclaration);
		if ((methodDeclaration != NULL) && (methodDeclaration->mFatArrowToken != NULL))
		{
			Fail("Destructors cannot have expression bodies", methodDeclaration->mFatArrowToken);
		}
	}		

	if ((!mCompiler->mIsResolveOnly) || (mCompiler->mResolvePassData->mAutoComplete == NULL))
	{		
		for (int fieldIdx = (int)mCurTypeInstance->mFieldInstances.size() - 1; fieldIdx >= 0; fieldIdx--)
		{
			auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldIdx];
			auto fieldDef = fieldInst->GetFieldDef();
			if ((fieldDef != NULL) && (fieldDef->mIsStatic == methodDef->mIsStatic) && (fieldDef->mFieldDeclaration != NULL) && (fieldDef->mFieldDeclaration->mFieldDtor != NULL))
			{
				if ((!methodDef->mIsStatic) && (mCurTypeInstance->IsValueType()))
				{
					Fail("Structs cannot have field destructors", fieldDef->mFieldDeclaration->mFieldDtor->mTildeToken);
				}

				SetAndRestoreValue<BfFilePosition> prevFilePos(mCurFilePosition);

				auto fieldDtor = fieldDef->mFieldDeclaration->mFieldDtor;

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
					if (!mCurTypeInstance->IsValueType())
					{
						auto thisValue = GetThis();
						value = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInst->mDataIdx);						
					}
					else
					{						
						AssertErrorState();
						value = mBfIRBuilder->CreateConstNull(mBfIRBuilder->GetPointerTo(mBfIRBuilder->MapType(fieldInst->mResolvedType)));						
					}
				}

				BfIRValue staticVal;
				
				if (hasDbgInfo)
				{
					BfIRValue dbgShowValue = value;
					
					BfLocalVariable* localDef = new BfLocalVariable();
					localDef->mName = "_";
					localDef->mResolvedType = fieldInst->mResolvedType;
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
					
					mBfIRBuilder->RestoreDebugLocation();
										
					auto defLocalVar = AddLocalVariableDef(localDef, true);
					// Put back so we actually modify the correct value*/
					defLocalVar->mResolvedType = fieldInst->mResolvedType;
					defLocalVar->mAddr = value; 
				}								

				while (fieldDtor != NULL)
				{
					if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
					{
						mCompiler->mResolvePassData->mSourceClassifier->SetElementType(fieldDtor, BfSourceElementType_Normal);
						mCompiler->mResolvePassData->mSourceClassifier->VisitChild(fieldDtor);
					}

					UpdateSrcPos(fieldDtor);										
					VisitEmbeddedStatement(fieldDtor->mBody);
					fieldDtor = fieldDtor->mNextFieldDtor;
				}

				RestoreScopeState();
			}
		}

		if (!methodDef->mIsStatic)
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

				if (checkBaseType->mTypeDef->mDtorDef != NULL)
				{
					auto dtorMethodInstance = GetMethodInstance(checkBaseType, checkBaseType->mTypeDef->mDtorDef, BfTypeVector());
					if (mCompiler->IsSkippingExtraResolveChecks())
					{
						// Nothing
					}
					else if (dtorMethodInstance.mMethodInstance->GetParamCount() == 0)
					{
						if ((!mCompiler->IsSkippingExtraResolveChecks()) && (!checkBaseType->IsUnspecializedType()))
						{
							auto basePtr = mBfIRBuilder->CreateBitCast(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->MapTypeInstPtr(checkBaseType));
							SizedArray<BfIRValue, 1> vals = { basePtr };
							auto callInst = mBfIRBuilder->CreateCall(dtorMethodInstance.mFunc, vals);
							mBfIRBuilder->SetCallCallingConv(callInst, GetCallingConvention(dtorMethodInstance.mMethodInstance));
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
					if ((fieldDef->mIsStatic == methodDef->mIsStatic) && (fieldDef->mFieldDeclaration != NULL) && 
						(fieldDef->mFieldDeclaration->mFieldDtor != NULL) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
					{
						BfType* fieldType = NULL;

						for (int curFieldIdx = 0; curFieldIdx < (int)mCurTypeInstance->mFieldInstances.size(); curFieldIdx++)
						{
							auto curFieldInstance = &mCurTypeInstance->mFieldInstances[curFieldIdx];
							auto curFieldDef = curFieldInstance->GetFieldDef();
							if ((curFieldDef != NULL) && (fieldDef->mName == curFieldDef->mName))
								fieldType = curFieldInstance->GetResolvedType();							
						}
						
						auto fieldDtor = fieldDef->mFieldDeclaration->mFieldDtor;

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
							localDef->mIsAssigned = true;
							AddLocalVariableDef(localDef);
						}

						while (fieldDtor != NULL)
						{
							mCompiler->mResolvePassData->mSourceClassifier->SetElementType(fieldDtor, BfSourceElementType_Normal);
							mCompiler->mResolvePassData->mSourceClassifier->VisitChild(fieldDtor);

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
	auto typeInstance = methodInstance->GetOwner();

	bool foundDllImportAttr = false;
	BfCallingConvention callingConvention = methodInstance->mMethodDef->mCallingConvention;
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

	String name = "bf_hs_preserve@";
	BfMangler::Mangle(name, mCompiler->GetMangleKind(), methodInstance);
	name += "__imp";
	
	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	methodInstance->GetIRFunctionInfo(this, returnType, paramTypes);
	
	BfIRFunctionType externFunctionType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes, false);
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

	auto callingConvention = GetCallingConvention(mCurTypeInstance, mCurMethodInstance->mMethodDef);

	BfIRType returnType;
	SizedArray<BfIRType, 8> paramTypes;
	mCurMethodInstance->GetIRFunctionInfo(this, returnType, paramTypes);

	SizedArray<BfIRValue, 8> args;

	for (int i = 0; i < (int)paramTypes.size(); i++)
		args.push_back(mBfIRBuilder->GetArgument(i));

	BfIRFunctionType externFunctionType = mBfIRBuilder->CreateFunctionType(returnType, paramTypes, false);

	if (isHotCompile)
	{
		BfIRValue funcVal = mBfIRBuilder->CreateLoad(globalVar);
		auto result = mBfIRBuilder->CreateCall(funcVal, args);
		if (callingConvention == BfIRCallingConv_StdCall)
			mBfIRBuilder->SetCallCallingConv(result, BfIRCallingConv_StdCall);
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

BfIRCallingConv BfModule::GetCallingConvention(BfTypeInstance* typeInst, BfMethodDef* methodDef)
{
	if (mSystem->mPtrSize == 8)
		return BfIRCallingConv_CDecl;
	if (methodDef->mCallingConvention == BfCallingConvention_Stdcall)
		return BfIRCallingConv_StdCall;
	if ((!methodDef->mIsStatic) && (!typeInst->IsValuelessType()) &&
		((!typeInst->IsSplattable()) || (methodDef->HasNoThisSplat())))
		return BfIRCallingConv_ThisCall;
	return BfIRCallingConv_CDecl;
}

BfIRCallingConv BfModule::GetCallingConvention(BfMethodInstance* methodInstance)
{
	return GetCallingConvention(methodInstance->GetOwner(), methodInstance->mMethodDef);
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

	if (methodDef->mImportKind == BfImportKind_Export)
		mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_DllExport);
	if (methodDef->mNoReturn)
		mBfIRBuilder->Func_AddAttribute(func, -1, BfIRAttribute_NoReturn);
	auto callingConv = GetCallingConvention(methodInstance->GetOwner(), methodDef);
	if (callingConv != BfIRCallingConv_CDecl)
		mBfIRBuilder->SetFuncCallingConv(func, callingConv);
	
	if (isInlined)
		mBfIRBuilder->Func_AddAttribute(func, -1, BFIRAttribute_AlwaysInline);

	int argIdx = 0;
	int paramIdx = 0;

	if (methodInstance->HasThis())
	{
		paramIdx = -1;			
	}

	int argCount = methodInstance->GetIRFunctionParamCount(this);

	if (methodInstance->HasStructRet())
	{				
		mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_NoAlias);
		mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_StructRet);
		argIdx++;
	}

	while (argIdx < argCount)
	{
		while ((paramIdx != -1) && (methodInstance->IsParamSkipped(paramIdx)))
			paramIdx++;

		BfType* resolvedTypeRef = NULL;
		String paramName;
		bool isSplattable = false;
		bool isThis = paramIdx == -1;
		if (isThis)
		{
			paramName = "this";
			if (methodInstance->mIsClosure)
				resolvedTypeRef = mCurMethodState->mClosureState->mClosureType;
			else
				resolvedTypeRef = methodInstance->GetOwner();
			isSplattable = (resolvedTypeRef->IsSplattable()) && (!methodDef->HasNoThisSplat());
        }
		else if (!methodDef->mNoSplat)
		{
			paramName = methodInstance->GetParamName(paramIdx);
        	resolvedTypeRef = methodInstance->GetParamType(paramIdx);
			if (resolvedTypeRef->IsMethodRef())
				isSplattable = true;
			else if (resolvedTypeRef->IsSplattable())
			{
				if (resolvedTypeRef->GetSplatCount() + argIdx <= mCompiler->mOptions.mMaxSplatRegs)
					isSplattable = true;
			}			
		}

		//
		{
			auto loweredTypeCode = resolvedTypeRef->GetLoweredType();
			if (loweredTypeCode != BfTypeCode_None)
				resolvedTypeRef = GetPrimitiveType(loweredTypeCode);
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
				if ((addDeref <= 0) && (!elementType->IsValuelessType()))
					AssertErrorState();
			}
			if ((resolvedTypeRef->IsComposite()) && (!resolvedTypeRef->IsTypedPrimitive()))
			{
				if (mCompiler->mOptions.mAllowStructByVal)
				{
					mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_ByVal);
				}
				else
				{
					mBfIRBuilder->Func_AddAttribute(func, argIdx + 1, BfIRAttribute_NoCapture);
					PopulateType(resolvedTypeRef, BfPopulateType_Data);
					addDeref = resolvedTypeRef->mSize;
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
		
		//argIdx++;
		paramIdx++;
	}
}

void BfModule::EmitCtorBody(bool& skipBody)
{
	auto methodInstance = mCurMethodInstance;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->mMethodDeclaration;
	auto ctorDeclaration = (BfConstructorDeclaration*)methodDef->mMethodDeclaration;
	auto typeDef = mCurTypeInstance->mTypeDef;

	// Prologue
	mBfIRBuilder->ClearDebugLocation();

	bool hadThisInitializer = false;	
	if ((ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL))
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorDeclaration->mInitializer->mTarget);
		auto targetType = (targetToken->GetToken() == BfToken_This) ? mCurTypeInstance : mCurTypeInstance->mBaseType;
		if (targetToken->GetToken() == BfToken_This)
		{
			hadThisInitializer = true;
			// Since we call a 'this', it's that other ctor's responsibility to fully assign the fields
			mCurMethodState->LocalDefined(GetThisVariable());
		}
	}

	// Zero out memory for default ctor
	if ((methodDeclaration == NULL) && (mCurTypeInstance->IsStruct()))
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
	if ((ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL) && (ctorDeclaration->mInitializer->mTarget != NULL))
		baseCtorNode = ctorDeclaration->mInitializer->mTarget;
	else if (methodDef->mBody != NULL)
		baseCtorNode = methodDef->mBody;
	else if (ctorDeclaration != NULL)
		baseCtorNode = ctorDeclaration;
	else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
		baseCtorNode = mCurTypeInstance->mTypeDef->mTypeDeclaration->mNameNode;
	else if ((mCurTypeInstance->mBaseType != NULL) && (mCurTypeInstance->mBaseType->mTypeDef->mTypeDeclaration != NULL))
		baseCtorNode = mCurTypeInstance->mBaseType->mTypeDef->mTypeDeclaration;
	else
		baseCtorNode = mContext->mBfObjectType->mTypeDef->mTypeDeclaration;

	if ((!mCurTypeInstance->IsBoxed()) && (methodDef->mMethodType == BfMethodType_Ctor))
	{		
		// Call the root type's default ctor (with no body) to initialize its fields and call the chained ctors
		if (methodDef->mDeclaringType->mTypeCode == BfTypeCode_Extension)
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
				exprEvaluator.CreateCall(moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, false, irArgs);
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

		if ((!mCompiler->mIsResolveOnly) || 
			(mCompiler->mResolvePassData->mAutoComplete == NULL) ||
			(mCompiler->mResolvePassData->mAutoComplete->mResolveType == BfResolveType_ShowFileSymbolReferences))
		{
			bool hadInlineInitBlock = false;
			BfScopeData scopeData;
			
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

					if (fieldDef->mInitializer == NULL)
					{
						if (fieldDef->mProtection != BfProtection_Hidden)
							continue;
						if (mCurTypeInstance->IsObject()) // Already zeroed out
							continue;						
					}

					if (fieldInst->mResolvedType == NULL)
					{
						BF_ASSERT(mHadBuildError);
						continue;
					}

					if (fieldDef->mInitializer != NULL)
					{
						if (!hadInlineInitBlock)
						{
							if ((mBfIRBuilder->DbgHasInfo()) && (!mCurTypeInstance->IsUnspecializedType()) && (mHasFullDebugInfo))
							{
								UpdateSrcPos(baseCtorNode);
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
								mCurMethodState->mCurScope->mDIScope = mBfIRBuilder->DbgCreateFunction(mBfIRBuilder->DbgGetTypeInst(mCurTypeInstance), "this$initFields", "", mCurFilePosition.mFileInstance->mDIFile,
									mCurFilePosition.mCurLine + 1, diFuncType, false, true, mCurFilePosition.mCurLine + 1, flags, false, BfIRValue());

								UpdateSrcPos(fieldDef->mInitializer);

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
						}

						UpdateSrcPos(fieldDef->mInitializer);
					}

					BfIRValue fieldAddr;
					if (!mCurTypeInstance->IsTypedPrimitive())
					{
						fieldAddr = mBfIRBuilder->CreateInBoundsGEP(mCurMethodState->mLocals[0]->mValue, 0, fieldInst->mDataIdx /*, fieldDef->mName*/);
					}
					else
					{
						AssertErrorState();
					}
					auto assignValue = GetFieldInitializerValue(fieldInst);					
					if ((fieldAddr) && (assignValue))
						mBfIRBuilder->CreateStore(assignValue.mValue, fieldAddr);
				}
			}

			if (hadInlineInitBlock)
			{
				RestoreScopeState();
				// This gets set to false because of AddScope but we actually are still in the head block
				mCurMethodState->mInHeadScope = true;
			}
		}
		else // Autocomplete case
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
						if ((!fieldDef->mIsStatic) && (fieldDef->mInitializer != NULL) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
						{
							mCompiler->mResolvePassData->mSourceClassifier->SetElementType(fieldDef->mInitializer, BfSourceElementType_Normal);
							mCompiler->mResolvePassData->mSourceClassifier->VisitChild(fieldDef->mInitializer);

							auto wantType = ResolveTypeRef(fieldDef->mTypeRef);
							if ((wantType != NULL) &&
								((wantType->IsVar()) || (wantType->IsLet()) || (wantType->IsRef())))
								wantType = NULL;
							CreateValueFromExpression(fieldDef->mInitializer, wantType, BfEvalExprFlags_FieldInitializer);
						}
					}
				}
			}

			// Mark fields from full type with initializers as initialized, to give proper initialization errors within ctor
			for (auto fieldDef : typeDef->mFields)
			{
				if ((!fieldDef->mIsConst) && (!fieldDef->mIsStatic) && (fieldDef->mInitializer != NULL))
				{
					auto fieldInst = &mCurTypeInstance->mFieldInstances[fieldDef->mIdx];
					if (fieldDef->mInitializer != NULL)
						MarkFieldInitialized(fieldInst);
				}
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
	}
	if ((ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL))
	{
		auto targetToken = BfNodeDynCast<BfTokenNode>(ctorDeclaration->mInitializer->mTarget);
		targetType = (targetToken->GetToken() == BfToken_This) ? mCurTypeInstance : mCurTypeInstance->mBaseType;
	}
	else if ((mCurTypeInstance->mBaseType != NULL) && (!mCurTypeInstance->IsUnspecializedType()))
	{		
		auto baseType = mCurTypeInstance->mBaseType;
		if ((!mCurTypeInstance->IsTypedPrimitive()) &&
			(baseType->mTypeDef != mCompiler->mValueTypeTypeDef) &&
			(baseType->mTypeDef != mCompiler->mBfObjectTypeDef))
		{
			// Try to find a ctor without any params first
			bool foundBaseCtor = false;
			bool hadCtorWithAllDefaults = false;

			for (int pass = 0; pass < 2; pass++)
			{
				for (auto checkMethodDef : baseType->mTypeDef->mMethods)
				{
					bool allowPriv = checkMethodDef->mProtection != BfProtection_Private;
					// Allow calling of the default base ctor if it is implicitly defined
					if ((checkMethodDef->mMethodDeclaration == NULL) && (pass == 1) && (!hadCtorWithAllDefaults))
						allowPriv = true;
						
					if ((checkMethodDef->mMethodType == BfMethodType_Ctor) && (!checkMethodDef->mIsStatic) && (allowPriv))
					{
						if (checkMethodDef->mParams.size() == 0)
						{
							foundBaseCtor = true;
							if (HasCompiledOutput())
							{
								SizedArray<BfIRValue, 1> args;
								auto ctorBodyMethodInstance = GetMethodInstance(mCurTypeInstance->mBaseType, checkMethodDef, BfTypeVector());
								if (!mCurTypeInstance->mBaseType->IsValuelessType())
								{
									auto basePtr = mBfIRBuilder->CreateBitCast(mCurMethodState->mLocals[0]->mValue, mBfIRBuilder->MapTypeInstPtr(mCurTypeInstance->mBaseType));
									args.push_back(basePtr);
								}
								AddCallDependency(ctorBodyMethodInstance.mMethodInstance, true);
								auto callInst = mBfIRBuilder->CreateCall(ctorBodyMethodInstance.mFunc, args);
								auto callingConv = GetCallingConvention(ctorBodyMethodInstance.mMethodInstance->GetOwner(), ctorBodyMethodInstance.mMethodInstance->mMethodDef);
								if (callingConv != BfIRCallingConv_CDecl)
									mBfIRBuilder->SetCallCallingConv(callInst, callingConv);
// 								if (!mCurTypeInstance->mBaseType->IsValuelessType())
// 									mBfIRBuilder->SetCallCallingConv(callInst, BfIRCallingConv_ThisCall);
								break;
							}
						}
						else if ((checkMethodDef->mParams[0]->mParamDeclaration != NULL) && (checkMethodDef->mParams[0]->mParamDeclaration->mInitializer != NULL))
							hadCtorWithAllDefaults = true;
					}			
				}

				if (foundBaseCtor)
					break;
			}

			if (!foundBaseCtor)
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

	if (targetType != NULL)
	{
		auto autoComplete = mCompiler->GetAutoComplete();
		auto wasCapturingMethodInfo = (autoComplete != NULL) && (autoComplete->mIsCapturingMethodMatchInfo);
		if ((autoComplete != NULL) && (ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL))
		{
			auto invocationExpr = ctorDeclaration->mInitializer;
			autoComplete->CheckInvocation(invocationExpr, invocationExpr->mOpenParen, invocationExpr->mCloseParen, invocationExpr->mCommas);
		}

		BfType* targetThisType = targetType;
		auto target = Cast(targetRefNode, GetThis(), targetThisType);
		BfExprEvaluator exprEvaluator(this);
		BfResolvedArgs argValues;
		if ((ctorDeclaration != NULL) && (ctorDeclaration->mInitializer != NULL))
		{			
			argValues.Init(&ctorDeclaration->mInitializer->mArguments);
			if (gDebugStuff)
			{
				OutputDebugStrF("Expr: %@  %d\n", argValues.mArguments->mVals, argValues.mArguments->mSize);
			}
		}
		exprEvaluator.ResolveArgValues(argValues, BfResolveArgFlag_DeferParamEval);

		BfTypedValue appendIdxVal;
		if (methodDef->mHasAppend)
		{			
			auto localVar = mCurMethodState->GetRootMethodState()->mLocals[1];
			BF_ASSERT(localVar->mName == "appendIdx");
			auto intRefType = CreateRefType(localVar->mResolvedType);
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
			exprEvaluator.CreateCall(appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
						
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
				exprEvaluator.CreateCall(toStringMethod.mMethodInstance, toStringMethod.mFunc, true, irArgs);
			}

			mBfIRBuilder->CreateBr(endBlock);
			continue;
		}

		if (fieldInstance.mConstIdx == -1)
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
	exprEvaluator.CreateCall(appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
	mBfIRBuilder->CreateBr(endBlock);

	mBfIRBuilder->AddBlock(noMatchBlock);
	mBfIRBuilder->SetInsertPoint(noMatchBlock);
	auto int64Val = mBfIRBuilder->CreateNumericCast(enumVal, false, BfTypeCode_Int64);
	auto toStringModuleMethodInstance = GetMethodByName(int64StructType, "ToString", 1);
	args.clear();
	args.Add(int64Val);
	stringDestVal = LoadValue(stringDestAddr);
	args.Add(stringDestVal.mValue);
	exprEvaluator.CreateCall(toStringModuleMethodInstance.mMethodInstance, toStringModuleMethodInstance.mFunc, false, args);
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
		exprEvaluator.CreateCall(appendCharModuleMethodInstance.mMethodInstance, appendCharModuleMethodInstance.mFunc, false, args);
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
			exprEvaluator.CreateCall(appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
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
			BfMethodMatcher methodMatcher(NULL, this, printModuleMethodInstance.mMethodInstance, resolvedArgs);
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
			BF_ASSERT(!fieldValue.IsAddr());
			SizedArray<BfIRValue, 2> args;
			args.Add(mBfIRBuilder->CreateBitCast(fieldValue.mValue, mBfIRBuilder->MapType(mContext->mBfObjectType)));
			auto stringDestVal = exprEvaluator.LoadLocal(mCurMethodState->mLocals[1]);
			stringDestVal = LoadValue(stringDestVal);
			args.Add(stringDestVal.mValue);			
			exprEvaluator.CreateCall(toStringSafeModuleMethodInstance.mMethodInstance, toStringSafeModuleMethodInstance.mFunc, false, args);
			continue;
		}

		BfExprEvaluator exprEvaluator(this);		
		SizedArray<BfResolvedArg, 0> resolvedArgs;		
		BfMethodMatcher methodMatcher(NULL, this, toStringModuleMethodInstance.mMethodInstance, resolvedArgs);
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
			exprEvaluator.CreateCall(appendModuleMethodInstance.mMethodInstance, appendModuleMethodInstance.mFunc, false, args);
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
		if ((retTypeInst->mTypeDef == mCompiler->mGenericIEnumerableTypeDef) || 
			(retTypeInst->mTypeDef == mCompiler->mGenericIEnumeratorTypeDef))
		{						
			innerRetType = retTypeInst->mTypeGenericArguments[0];
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

void BfModule::EmitGCMarkValue(BfTypedValue markVal, BfModuleMethodInstance markFromGCThreadMethodInstance)
{
	auto fieldTypeInst = markVal.mType->ToTypeInstance();
	if (fieldTypeInst == NULL)
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
		exprEvaluator.CreateCall(markFromGCThreadMethodInstance.mMethodInstance, markFromGCThreadMethodInstance.mFunc, false, args);
	}
	else if ((fieldTypeInst->IsComposite()) && (!fieldTypeInst->IsTypedPrimitive()))
	{
		auto markMemberMethodInstance = GetMethodByName(fieldTypeInst, BF_METHODNAME_MARKMEMBERS, 0, true);
		if (markMemberMethodInstance)
		{
			SizedArray<BfIRValue, 1> args;
            //(1, markVal.mValue);
			//exprEvaluator.PushArg(markVal, args);
			exprEvaluator.PushThis(NULL, markVal, markMemberMethodInstance.mMethodInstance, args);
			exprEvaluator.CreateCall(markMemberMethodInstance.mMethodInstance, markMemberMethodInstance.mFunc, false, args);
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
			CompareDeclTypes(lhs->mMethodDef->mDeclaringType, rhs->mMethodDef->mDeclaringType, isBetter, isWorse);
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
		exprEvaluator.CreateCall(moduleMethodInst.mMethodInstance, moduleMethodInst.mFunc, true, args);
	}
}

void BfModule::AddHotDataReferences(BfHotDataReferenceBuilder* builder)
{	
	BF_ASSERT(mCurMethodInstance->mIsReified);

	if (mCurTypeInstance->mHotTypeData == NULL)
		mCurTypeInstance->mHotTypeData = new BfHotTypeData();

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

	//hotMethod->mReferences.Sort([](BfHotDepData* lhs, BfHotDepData* rhs) { return lhs < rhs; });
}

void BfModule::ProcessMethod_SetupParams(BfMethodInstance* methodInstance, BfType* thisType, bool wantsDIData, SizedArrayImpl<BfIRMDNode>* diParams)
{
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->mMethodDeclaration;

	bool isThisStruct = false;
	if (thisType != NULL)
		isThisStruct = thisType->IsStruct() && !thisType->IsTypedPrimitive();
	
	int argIdx = 0;
	if (methodInstance->HasStructRet())
		argIdx++;

	if (!methodDef->mIsStatic)
	{
		BfLocalVariable* paramVar = new BfLocalVariable();
		paramVar->mResolvedType = thisType;
		paramVar->mName = "this";
		if (!thisType->IsValuelessType())
			paramVar->mValue = mBfIRBuilder->GetArgument(argIdx);
		else
			paramVar->mValue = mBfIRBuilder->GetFakeVal();
		if ((thisType->IsComposite()) && (!methodDef->HasNoThisSplat()))
		{
			if (thisType->IsSplattable())
				paramVar->mIsSplat = true;
			else
				paramVar->mIsLowered = thisType->GetLoweredType() != BfTypeCode_None;
		}

		auto thisTypeInst = thisType->ToTypeInstance();
		paramVar->mIsStruct = isThisStruct;
		paramVar->mAddr = BfIRValue();
		paramVar->mIsThis = true;
		paramVar->mParamIdx = -1;
		if ((methodDef->mMethodType == BfMethodType_Ctor) && (mCurTypeInstance->IsStruct()))
		{
			paramVar->mIsAssigned = false;
		}
		else
		{
			paramVar->mIsAssigned = true;
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
				diParams->push_back(diType);
			}

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
			}
			else
			{
				argIdx++;
			}
		}
	}

	bool hadParams = false;
	bool hasDefault = false;

	int compositeVariableIdx = -1;
	int paramIdx = 0;
	for (paramIdx = 0; paramIdx < methodInstance->GetParamCount(); paramIdx++)
	{
		// We already issues a type error for this param if we had one in declaration processing
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
		BfLocalVariable* paramVar = new BfLocalVariable();

		bool isParamSkipped = methodInstance->IsParamSkipped(paramIdx);

		BfType* unresolvedType = methodInstance->GetParamType(paramIdx, false);
		if (unresolvedType == NULL)
		{
			AssertErrorState();
			unresolvedType = mContext->mBfObjectType;
			paramVar->mParamFailed = true;
		}
		auto resolvedType = methodInstance->GetParamType(paramIdx, true);
		prevIgnoreErrors.Restore();
		PopulateType(resolvedType, BfPopulateType_Declaration);
		paramVar->mResolvedType = resolvedType;
		paramVar->mName = methodInstance->GetParamName(paramIdx);
		paramVar->mNameNode = methodInstance->GetParamNameNode(paramIdx);
		if (!isParamSkipped)
		{
			paramVar->mValue = mBfIRBuilder->GetArgument(argIdx);

			if (resolvedType->IsMethodRef())
			{
				paramVar->mIsSplat = true;
			}
			else if (resolvedType->IsComposite() && resolvedType->IsSplattable())
			{
				int splatCount = resolvedType->GetSplatCount();
				if (argIdx + splatCount <= mCompiler->mOptions.mMaxSplatRegs)
					paramVar->mIsSplat = true;
			}
		}
		else
		{
			paramVar->mIsSplat = true; // Treat skipped (valueless) as a splat
			paramVar->mValue = mBfIRBuilder->GetFakeVal();
		}
		paramVar->mIsLowered = resolvedType->GetLoweredType() != BfTypeCode_None;
		paramVar->mIsStruct = resolvedType->IsComposite() && !resolvedType->IsTypedPrimitive();
		paramVar->mParamIdx = paramIdx;
		paramVar->mIsImplicitParam = methodInstance->IsImplicitCapture(paramIdx);
		paramVar->mReadFromId = 0;
		if (resolvedType->IsRef())
		{
			auto refType = (BfRefType*)resolvedType;
			paramVar->mIsAssigned = refType->mRefKind != BfRefType::RefKind_Out;
		}
		else
		{
			paramVar->mIsAssigned = true;
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

						if (exprEvaluator.mResult.mType == checkType)
						{
							paramVar->mResolvedType = genericParamInst->mTypeConstraint;
							paramVar->mConstValue = exprEvaluator.mResult.mValue;
							BF_ASSERT(paramVar->mConstValue.IsConst());
						}
						else
						{
							AssertErrorState();
							paramVar->mResolvedType = genericParamInst->mTypeConstraint;
							paramVar->mConstValue = GetDefaultValue(genericParamInst->mTypeConstraint);
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
				localVar->mResolvedType = ResolveTypeRef(paramDef->mTypeRef, BfPopulateType_Declaration, BfResolveTypeRefFlag_NoResolveGenericParam);
				localVar->mCompositeCount = 0;
				DoAddLocalVariable(localVar);
			}
			mCurMethodState->mLocals[compositeVariableIdx]->mCompositeCount++;
		}

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
				AssertErrorState();
			}
			else if (paramsType->IsGenericParam())
			{				
				auto genericParamInstance = GetGenericParamInstance((BfGenericParamType*)paramsType);

				if (genericParamInstance->mTypeConstraint != NULL)
				{
					auto typeInstConstraint = genericParamInstance->mTypeConstraint->ToTypeInstance();
					if ((genericParamInstance->mTypeConstraint->IsDelegate()) || (genericParamInstance->mTypeConstraint->IsFunction()) || 
						((typeInstConstraint != NULL) && 
						 ((typeInstConstraint->mTypeDef == mCompiler->mDelegateTypeDef) || (typeInstConstraint->mTypeDef == mCompiler->mFunctionTypeDef))))
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

	auto _ClearState = [&]()
	{
		mCurMethodState->mLocalMethods.Clear();

		for (auto& local : mCurMethodState->mLocals)
			delete local;
		mCurMethodState->mLocals.Clear();
		mCurMethodState->mLocalVarSet.Clear();
	};
	
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

			mSystem->CheckLockYield();
		}
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

				if (lambdaInstance->mDtorMethodInstance != NULL)
				{
					lambdaInstance->mDtorMethodInstance->mMethodInstanceGroup = &methodInstanceGroup;
					ProcessMethod(lambdaInstance->mDtorMethodInstance);
				}
			}
		}

		mSystem->CheckLockYield();
	}
}

void BfModule::EmitGCMarkValue(BfTypedValue& thisValue, BfType* checkType, int memberDepth, int curOffset, HashSet<int>& objectOffsets, BfModuleMethodInstance markFromGCThreadMethodInstance)
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
		callMarkMethod = true;				
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
		EmitGCMarkValue(thisValue, fieldInst.mResolvedType, memberDepth + 1, curOffset + fieldInst.mDataOffset, objectOffsets, markFromGCThreadMethodInstance);
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
						BF_FATAL("Not handled");
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
						if (methodBaseType != mContext->mBfObjectType)
						{
							auto thisValue = GetThis();
							auto baseValue = Cast(NULL, thisValue, methodBaseType, BfCastFlags_Explicit);

							SizedArray<BfIRValue, 1> args;							
							if (moduleMethodInst.mMethodInstance->GetParamIsSplat(-1))
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

						auto fieldTypeInst = fieldInst.mResolvedType->ToTypeInstance();
						if (fieldTypeInst != NULL)
						{
							auto fieldDef = fieldInst.GetFieldDef();
							BfTypedValue markVal;

							if (fieldDef->mIsConst)
								continue;

							if ((!fieldTypeInst->IsObjectOrInterface()) && (!fieldTypeInst->IsComposite()))
								continue;

							mBfIRBuilder->PopulateType(fieldTypeInst);

							if (fieldTypeInst->IsValuelessType())
								continue;

							if ((fieldDef->mIsStatic) && (!fieldDef->mIsConst))
							{
								markVal = ReferenceStaticField(&fieldInst);
							}
							else if (!fieldDef->mIsStatic)
							{
								markVal = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInst.mDataIdx/*, fieldDef->mName*/), fieldInst.mResolvedType, true);
							}

							if (markVal)
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

void BfModule::ProcessMethod(BfMethodInstance* methodInstance, bool isInlineDup)
{
	BP_ZONE_F("BfModule::ProcessMethod %s", BP_DYN_STR(methodInstance->mMethodDef->mName.c_str()));
	
	if (mAwaitingInitFinish)
		FinishInit();

	if (!methodInstance->mIsReified)
		BF_ASSERT(!mIsReified);
	
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

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mWantsIRIgnoreWrites || methodInstance->mIsUnspecialized);
	bool ignoreWrites = mBfIRBuilder->mIgnoreWrites;

	if ((HasCompiledOutput()) && (!mBfIRBuilder->mIgnoreWrites))
	{
		BF_ASSERT(!methodInstance->mIRFunction.IsFake() || (methodInstance->GetImportCallKind() != BfImportCallKind_None));
	}

	SetAndRestoreValue<BfSourceClassifier*> prevSourceClassifier;
	if (((methodInstance->mMethodDef->mMethodType == BfMethodType_CtorCalcAppend) || (methodInstance->mIsForeignMethodDef) || (methodInstance->IsSpecializedGenericMethod())) && 
		(mCompiler->mResolvePassData != NULL))
	{
		// Don't classify on the CtorCalcAppend, just on the actual Ctor
		prevSourceClassifier.Init(mCompiler->mResolvePassData->mSourceClassifier, NULL);
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

	BfMethodInstance* defaultMethodInstance = methodInstance->mMethodInstanceGroup->mDefault;

	BF_ASSERT(methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_NotSet);

	if ((methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_AlwaysInclude) &&
		(methodInstance->mMethodInstanceGroup->mOnDemandKind != BfMethodOnDemandKind_Referenced))
	{
		methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Referenced;
		
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
	}

	// We set mHasBeenProcessed to true immediately -- this helps avoid stack overflow during recursion for things like
	//  self-referencing append allocations in ctor@calcAppend
	methodInstance->mHasBeenProcessed = true;
	mIncompleteMethodCount--;
	BF_ASSERT((mIsSpecialModule) || (mIncompleteMethodCount >= 0));

	auto typeDef = methodInstance->mMethodInstanceGroup->mOwner->mTypeDef;
	auto methodDef = methodInstance->mMethodDef;
	auto methodDeclaration = methodDef->GetMethodDeclaration();

	if ((methodInstance->mIsReified) && (methodInstance->mVirtualTableIdx != -1))
	{
		// If we reify a virtual method in a HotCompile then we need to make sure the type data gets written out
		//  so we get the new vdata for it
		methodInstance->GetOwner()->mDirty = true;
	}

	int dependentGenericStartIdx = 0;
	if (methodDef->mIsLocalMethod) // See DoMethodDeclaration for an explaination of dependentGenericStartIdx
		dependentGenericStartIdx = (int)mCurMethodState->GetRootMethodState()->mMethodInstance->GetNumGenericArguments();

	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);	
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, methodInstance->mMethodInstanceGroup->mOwner);
	SetAndRestoreValue<BfFilePosition> prevFilePos(mCurFilePosition);	
	SetAndRestoreValue<bool> prevHadBuildError(mHadBuildError, false);
	SetAndRestoreValue<bool> prevHadWarning(mHadBuildWarning, false);
	SetAndRestoreValue<bool> prevIgnoreWarnings(mIgnoreWarnings, false);

	if ((methodInstance->mIsReified) &&
		((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorNoBody)))
	{
		mCurTypeInstance->mHasBeenInstantiated = true;
	}

	// Warnings are shown in the capture phase
	if ((methodDef->mIsLocalMethod) || (mCurTypeInstance->IsClosure()))
		mIgnoreWarnings = true;

	if ((methodDeclaration == NULL) && (mCurMethodState == NULL))
		UseDefaultSrcPos();

	// We may not actually be populated in relatively rare autocompelte cases
	PopulateType(mCurTypeInstance, BfPopulateType_DataAndMethods);
	mBfIRBuilder->PopulateType(mCurTypeInstance, BfIRPopulateType_Full);

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
		(mCompiler->mResolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Method) && (methodDef->mIdx >= 0))
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

	BfLogSysM("ProcessMethod %p Unspecialized: %d Module: %p IRFunction: %d Reified: %d\n", methodInstance, mCurTypeInstance->IsUnspecializedType(), this, methodInstance->mIRFunction.mId, methodInstance->mIsReified);
	
	if (methodInstance->GetImportCallKind() != BfImportCallKind_None)
	{
		if (mBfIRBuilder->mIgnoreWrites)
			return; 				

		BfLogSysM("DllImportGlobalVar processing %p\n", methodInstance);

		if (!methodInstance->mIRFunction)
		{
			BfIRValue dllImportGlobalVar = CreateDllImportGlobalVar(methodInstance, true);
			methodInstance->mIRFunction = mBfIRBuilder->GetFakeVal();
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

	StringT<128> mangledName;
	BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), mCurMethodInstance);
	if (!methodInstance->mIRFunction)
	{						
		bool isIntrinsic = false;
		SetupIRFunction(methodInstance, mangledName, false, &isIntrinsic);		
	}
	if (methodInstance->mIsIntrinsic)
		return;
			
	auto prevActiveFunction = mBfIRBuilder->GetActiveFunction();
	mBfIRBuilder->SetActiveFunction(mCurMethodInstance->mIRFunction);	

	if (methodDef->mBody != NULL)
		UpdateSrcPos(methodDef->mBody, BfSrcPosFlag_NoSetDebugLoc);
	else if (methodDeclaration != NULL)		
		UpdateSrcPos(methodDeclaration, BfSrcPosFlag_NoSetDebugLoc);	
	else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
		UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration, BfSrcPosFlag_NoSetDebugLoc);		
		
	if (mCurMethodState == NULL) // Only do initial classify for the 'outer' method state, not any local methods or lambdas
	{
		if ((mCompiler->mIsResolveOnly) && (methodDef->mBody != NULL) && (!mCurTypeInstance->IsBoxed()) &&
			(methodDef->mBody->IsFromParser(mCompiler->mResolvePassData->mParser)) && (mCompiler->mResolvePassData->mSourceClassifier != NULL))
		{
			mCompiler->mResolvePassData->mSourceClassifier->VisitChildNoRef(methodDef->mBody);
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
		((mCurTypeInstance->IsGenericTypeInstance()) && (!isGenericVariation))))
	{		
		unspecializedMethodInstance = GetUnspecializedMethodInstance(methodInstance);
		
		BF_ASSERT(unspecializedMethodInstance != methodInstance);
		if (!unspecializedMethodInstance->mHasBeenProcessed)
		{			
			// Make sure the unspecialized method is processed so we can take its bindings

			// Clear mCurMethodState so we don't think we're in a local method
			SetAndRestoreValue<BfMethodState*> prevMethodState_Unspec(mCurMethodState, prevMethodState.mPrevVal);
			mContext->ProcessMethod(unspecializedMethodInstance);
		}
		methodState.mGenericTypeBindings = &unspecializedMethodInstance->GetMethodInfoEx()->mGenericTypeBindings;
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
				if ((mCurMethodInstance->GetParamType(0) != mCurTypeInstance) && (!mCurMethodInstance->GetParamType(0)->IsSelf()) &&
					(mCurMethodInstance->GetParamType(1) != mCurTypeInstance) && (!mCurMethodInstance->GetParamType(1)->IsSelf()))
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
			if (methodDef->mParams.size() != 1)
			{
				Fail("Unary operators must declare one parameter", paramErrorRefNode);
			}
			else if ((mCurMethodInstance->GetParamType(0) != mCurTypeInstance) && (!mCurMethodInstance->GetParamType(0)->IsSelf()))
			{				
				Fail("The parameter of a unary operator must be the containing type", paramErrorRefNode);
			}

			if (((operatorDef->mOperatorDeclaration->mUnaryOp == BfUnaryOp_Increment) ||
				(operatorDef->mOperatorDeclaration->mUnaryOp == BfUnaryOp_Decrement)) &&
				(!TypeIsSubTypeOf(mCurMethodInstance->mReturnType->ToTypeInstance(), mCurTypeInstance)))
			{
				Fail("The return type for the '++' or '--' operator must match the parameter type or be derived from the parameter type", operatorDef->mOperatorDeclaration->mReturnType);
			}
		}
		else if (operatorDef->mOperatorDeclaration->mAssignOp != BfAssignmentOp_None)
		{
			if (methodDef->mParams.size() != 1)
			{
				Fail("Assignment operators must declare one parameter", paramErrorRefNode);
			}
			
			if (mCurMethodInstance->mReturnType->IsVoid())
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
			else
			{
				if ((mCurMethodInstance->GetParamType(0) != mCurTypeInstance) && (!mCurMethodInstance->GetParamType(0)->IsSelf()) && 
					(mCurMethodInstance->mReturnType != mCurTypeInstance) && (!mCurMethodInstance->mReturnType->IsSelf()))
					Fail("User-defined conversion must convert to or from the enclosing type", paramErrorRefNode);
				if (mCurMethodInstance->GetParamType(0) == mCurMethodInstance->mReturnType)
					Fail("User-defined operator cannot take an object of the enclosing type and convert to an object of the enclosing type", operatorDef->mOperatorDeclaration->mReturnType);			

				// On type lookup error we default to 'object', so don't do the 'base class' error if that may have
				//  happened here
				if ((!mHadBuildError) || ((mCurMethodInstance->mReturnType != mContext->mBfObjectType) && (mCurMethodInstance->GetParamType(0) != mContext->mBfObjectType)))
				{
					auto isToBase = TypeIsSubTypeOf(mCurTypeInstance->mBaseType, mCurMethodInstance->mReturnType->ToTypeInstance());
					bool isFromBase = TypeIsSubTypeOf(mCurTypeInstance->mBaseType, mCurMethodInstance->GetParamType(0)->ToTypeInstance());

					if ((mCurTypeInstance->IsObject()) && (isToBase || isFromBase))
						Fail("User-defined conversions to or from a base class are not allowed", paramErrorRefNode);
					else if ((mCurTypeInstance->IsStruct()) && (isToBase))
						Fail("User-defined conversions to a base struct are not allowed", paramErrorRefNode);
				}
			}
		}
	}

	bool wantsDIData = (mBfIRBuilder->DbgHasInfo()) && (!methodDef->IsEmptyPartial()) && (!mCurMethodInstance->mIsUnspecialized) && (mHasFullDebugInfo);
	if (methodDef->mMethodType == BfMethodType_Mixin)
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
		else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration, BfSrcPosFlag_NoSetDebugLoc);		
	}

	BfIRMDNode diFunction;
	
	if (wantsDIData)
	{
		BP_ZONE("BfModule::BfMethodDeclaration.DISetup");		

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
				if ((mCurTypeInstance->IsValuelessType()) || (mCurTypeInstance->IsSplattable()))
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
 				nullptr, flags, IsOptimized(), llvmFunction, genericArgs, genericConstValueArgs);

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
	
	if (methodDef->mBody != NULL)
	{
		UpdateSrcPos(methodDef->mBody, BfSrcPosFlag_NoSetDebugLoc);
	}
	else if ((mHasFullDebugInfo) && (diFunction) && (typeDef->mTypeDeclaration != NULL))
	{
		// We want to be able to step into delegate invokes -- we actually step over them
		if (methodDef->mName != "Invoke")
		{
			UpdateSrcPos(typeDef->mTypeDeclaration);
			mBfIRBuilder->DbgCreateAnnotation(diFunction, "StepOver", GetConstValue32(1));
		}
	}

	// Clear out DebugLoc - to mark the ".addr" code as part of prologue
	mBfIRBuilder->ClearDebugLocation();
	
	bool isTypedPrimitiveFunc = mCurTypeInstance->IsTypedPrimitive() && (methodDef->mMethodType != BfMethodType_Ctor);
	int irParamCount = methodInstance->GetIRFunctionParamCount(this);

	if (methodDef->mImportKind != BfImportKind_Dynamic)
	{
		int localIdx = 0;
		int argIdx = 0;				

		if (methodInstance->HasStructRet())		
			argIdx++;		

		//
		bool doDbgAgg = false;
		Array<BfIRValue> splatAddrValues;

		for ( ; argIdx < irParamCount; localIdx++)
		{
			bool isThis = ((localIdx == 0) && (!mCurMethodInstance->mMethodDef->mIsStatic));
			if ((isThis) && (thisType->IsValuelessType()))
				isThis = false;

			BfLocalVariable* paramVar = NULL;
			while (true)
			{
				BF_ASSERT(localIdx < methodState.mLocals.size());
				paramVar = methodState.mLocals[localIdx];
				if ((paramVar->mCompositeCount == -1) &&
                    ((!paramVar->mResolvedType->IsValuelessType()) || (paramVar->mResolvedType->IsMethodRef())))
					break;
				localIdx++;
			}

			if ((isThis) && (mCurTypeInstance->IsValueType()) && (methodDef->mMethodType != BfMethodType_Ctor) && (!methodDef->HasNoThisSplat()))
			{
				paramVar->mIsReadOnly = true;
			}
			
			bool wantsAddr = (wantsDIVariables) || (!paramVar->mIsReadOnly) || (paramVar->mResolvedType->GetLoweredType() != BfTypeCode_None);

			if (paramVar->mResolvedType->IsMethodRef())
				wantsAddr = false;			

// 			if ((methodDef->mHasAppend) && (argIdx == 1))
// 			{
// 				BF_ASSERT(paramVar->mName == "appendIdx");
// 				wantsAddr = true;
// 			}

			if ((!doDbgAgg) && (paramVar->mIsSplat))
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
						if (doDbgAgg)
						{
							auto allocaInst = mBfIRBuilder->CreateAlloca(thisAddrType);
							mBfIRBuilder->SetName(allocaInst, paramVar->mName + ".addr");
							mBfIRBuilder->SetAllocaAlignment(allocaInst, thisType->mAlign);
							paramVar->mAddr = allocaInst;
							if (WantsLifetimes())
								mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(allocaInst);
						}												
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
			else
			{
				argIdx++;
			}
		}
					
		if (methodDef->mBody != NULL)	
			UpdateSrcPos(methodDef->mBody);	
		else if (mCurTypeInstance->mTypeDef->mTypeDeclaration != NULL)
			UpdateSrcPos(mCurTypeInstance->mTypeDef->mTypeDeclaration);			

		int declArgIdx = 0;
		localIdx = 0;
		argIdx = 0;
		if (methodInstance->HasStructRet())
			argIdx++;				
	
		int splatAddrIdx = 0;		
		while (localIdx < (int)methodState.mLocals.size())
		{
			int curLocalIdx = localIdx++;
			BfLocalVariable* paramVar = methodState.mLocals[curLocalIdx];
			if (!paramVar->IsParam())
				continue;
			if (paramVar->mCompositeCount != -1)
				continue;			

			bool isThis = ((curLocalIdx == 0) && (!mCurMethodInstance->mMethodDef->mIsStatic));
			if ((isThis) && (thisType->IsValuelessType()))
				isThis = false;

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
				
				if ((!paramVar->mIsSplat) || (doDbgAgg))
				{
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
					if (doDbgAgg)
					{
						auto curArgIdx = argIdx;
						std::function<void(BfType*, BfIRValue)> checkTypeLambda = [&](BfType* checkType, BfIRValue curDestAddr)
						{
							if (checkType->IsStruct())
							{
								auto checkTypeInstance = checkType->ToTypeInstance();
								if (checkTypeInstance->mBaseType != NULL)
								{
									auto baseDestAddr = mBfIRBuilder->CreateBitCast(curDestAddr, mBfIRBuilder->MapTypeInstPtr(checkTypeInstance->mBaseType));
									checkTypeLambda(checkTypeInstance->mBaseType, baseDestAddr);
								}

								if (checkTypeInstance->mIsUnion)
								{
									auto unionInnerType = checkTypeInstance->GetUnionInnerType();
									if (!unionInnerType->IsValuelessType())
									{
										auto newDestAddr = mBfIRBuilder->CreateInBoundsGEP(curDestAddr, 0, 1);
										checkTypeLambda(unionInnerType, newDestAddr);
									}

									if (checkTypeInstance->IsEnum())
									{
										auto dscrType = checkTypeInstance->GetDiscriminatorType();
										auto newDestAddr = mBfIRBuilder->CreateInBoundsGEP(curDestAddr, 0, 2);
										checkTypeLambda(dscrType, newDestAddr);
									}
								}
								else
								{
									for (int fieldIdx = 0; fieldIdx < (int)checkTypeInstance->mFieldInstances.size(); fieldIdx++)
									{
										auto fieldInstance = (BfFieldInstance*)&checkTypeInstance->mFieldInstances[fieldIdx];
										if (fieldInstance->mDataIdx >= 0)
										{
											auto newDestAddr = mBfIRBuilder->CreateInBoundsGEP(curDestAddr, 0, fieldInstance->mDataIdx);
											checkTypeLambda(fieldInstance->GetResolvedType(), newDestAddr);
										}
									}
								}
							}
							else if (checkType->IsMethodRef())
							{
								BF_FATAL("Unimplemented");
// 								BfMethodRefType* methodRefType = (BfMethodRefType*)checkType;
// 								for (int dataIdx = 0; dataIdx < methodRefType->GetCaptureDataCount(); dataIdx++)
// 								{
// 									checkTypeLambda->
// 								}
							}
							else if (!checkType->IsValuelessType())
							{
								mBfIRBuilder->CreateAlignedStore(mBfIRBuilder->GetArgument(curArgIdx), curDestAddr, paramVar->mResolvedType->mAlign);
								curArgIdx++;
							}
						};
						
						checkTypeLambda(paramVar->mResolvedType, paramVar->mAddr);
					}					
				}
				else
				{
					bool handled = false;
					//if ((!isThis) || (!methodDef->mIsMutating && !methodDef->mNoSplat))

					if (paramVar->mIsLowered)
					{						
						auto loweredTypeCode = paramVar->mResolvedType->GetLoweredType();
						BF_ASSERT(loweredTypeCode != BfTypeCode_None);
						if (loweredTypeCode != BfTypeCode_None)
						{
							// We have a lowered type coming in, so we have to cast the .addr before storing
							auto primType = GetPrimitiveType(loweredTypeCode);
							auto primPtrType = CreatePointerType(primType);
							auto primPtrVal = mBfIRBuilder->CreateBitCast(paramVar->mAddr, mBfIRBuilder->MapType(primPtrType));
							mBfIRBuilder->CreateAlignedStore(paramVar->mValue, primPtrVal, paramVar->mResolvedType->mSize);
							// We don't want to allow directly using value
							paramVar->mValue = BfIRValue();
							handled = true;
						}						
					}

					if (!handled)
						mBfIRBuilder->CreateAlignedStore(paramVar->mValue, paramVar->mAddr, paramVar->mResolvedType->mAlign);
				}
			}
			
			if (!isThis)		
				declArgIdx++;						

			if (methodDef->mBody != NULL)
				UpdateSrcPos(methodDef->mBody);
			else if (methodDef->mDeclaringType->mTypeDeclaration != NULL)
				UpdateSrcPos(methodDef->mDeclaringType->mTypeDeclaration);
			else if (methodDeclaration == NULL)
				UseDefaultSrcPos();
			
			// Write our argument value into the .addr
			if ((!doDbgAgg) && (paramVar->mIsSplat))
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
				auto loadedThis = mBfIRBuilder->CreateLoad(paramVar->mAddr/*, "this"*/);
				mBfIRBuilder->ClearDebugLocation(loadedThis);				
				paramVar->mValue = loadedThis;
			}
		
			if ((wantsDIData) && (declareCall))
				mBfIRBuilder->UpdateDebugLocation(declareCall);				

			if (paramVar->mIsSplat)
			{
				BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramVar->mResolvedType);
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
			AddLocalVariableDef(localVar, true);
		}
	}

	bool wantsRemoveBody = false;
	bool skipBody = false;
	bool skipUpdateSrcPos = false;
	bool skipEndChecks = false;
	bool hasExternSpecifier = (methodDeclaration != NULL) && (methodDeclaration->mExternSpecifier != NULL) && (methodDef->mImportKind != BfImportKind_Dynamic);
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
		
		if (HasCompiledOutput())
		{	
			// Clear out DebugLoc - to mark the ".addr" code as part of prologue
			mBfIRBuilder->ClearDebugLocation();

			BfBoxedType* boxedType = (BfBoxedType*) mCurTypeInstance;
			BfTypeInstance* innerType = boxedType->mElementType;
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
			}
			else
			{
				BF_ASSERT(innerMethodInstance.mMethodInstance->mMethodDef == methodDef);

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
				else if (methodInstance->HasStructRet())
				{
					mBfIRBuilder->PopulateType(methodInstance->mReturnType);
					auto returnType = BfTypedValue(mBfIRBuilder->GetArgument(0), methodInstance->mReturnType, true);
					exprEvaluator.mReceivingValue = &returnType;
					auto retVal = exprEvaluator.CreateCall(innerMethodInstance.mMethodInstance, innerMethodInstance.mFunc, true, innerParams, NULL, true);					
					BF_ASSERT(exprEvaluator.mReceivingValue == NULL); // Ensure it was actually used
					mBfIRBuilder->CreateRetVoid();
				}
				else
				{
					mBfIRBuilder->PopulateType(methodInstance->mReturnType);
					auto retVal = exprEvaluator.CreateCall(innerMethodInstance.mMethodInstance, innerMethodInstance.mFunc, true, innerParams, NULL, true);
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
		int prevSize = mContext->mBfObjectType->mInstSize;		
		int curSize = mCurTypeInstance->mInstSize;
		if (curSize > prevSize)
		{
			auto int8PtrType = CreatePointerType(GetPrimitiveType(BfTypeCode_Int8));
			auto int8PtrVal = mBfIRBuilder->CreateBitCast(thisVal.mValue, mBfIRBuilder->MapType(int8PtrType));
			int8PtrVal = mBfIRBuilder->CreateInBoundsGEP(int8PtrVal, GetConstValue(prevSize));
			mBfIRBuilder->CreateMemSet(int8PtrVal, GetConstValue8(0), GetConstValue(curSize - prevSize), GetConstValue(mCurTypeInstance->mInstAlign));
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
	if ((customAttributes != NULL) && (customAttributes->Contains(mCompiler->mDisableObjectAccessChecksAttributeTypeDef)))
		mCurMethodState->mIgnoreObjectAccessCheck = true;
	
	if ((methodDef->mMethodType == BfMethodType_CtorNoBody) && (!methodDef->mIsStatic) &&
		((methodInstance->mChainType == BfMethodChainType_ChainHead) || (methodInstance->mChainType == BfMethodChainType_None)))
	{		
		// We chain even non-default ctors to the default ctors
		auto defaultCtor = methodInstance;
		if (defaultCtor->mChainType == BfMethodChainType_ChainHead)
			CallChainedMethods(defaultCtor, false);
	}

	auto bodyBlock = BfNodeDynCast<BfBlock>(methodDef->mBody);
	if (methodDef->mBody == NULL)
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
		if (methodDef->mImportKind == BfImportKind_Static)
		{
			for (auto customAttr : methodInstance->GetCustomAttributes()->mAttributes)
			{
				if (customAttr.mType->mTypeDef->mFullName.ToString() == "System.ImportAttribute")
				{
					if (customAttr.mCtorArgs.size() == 1)
					{
						auto fileNameArg = customAttr.mCtorArgs[0];
						int strNum = 0;
						auto constant = mCurTypeInstance->mConstHolder->GetConstant(fileNameArg);
						if (constant != NULL)
						{
							if (constant->IsNull())
								continue; // Invalid					
							strNum = constant->mInt32;
						}
						else
						{
							strNum = GetStringPoolIdx(fileNameArg, mCurTypeInstance->mConstHolder);
						}
						if (!mImportFileNames.Contains(strNum))
							mImportFileNames.Add(strNum);
					}
				}
			}

			//mImportFileNames
		}
		else if (methodDef->mImportKind == BfImportKind_Dynamic)
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
			if (HasCompiledOutput())
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
			if (HasCompiledOutput())
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
			CreateValueTypeEqualsMethod();
			skipBody = true;
			skipEndChecks = true;
		}
		else if ((methodDef->mName == BF_METHODNAME_EQUALS) && (typeDef == mCompiler->mValueTypeTypeDef))
		{
			CreateValueTypeEqualsMethod();
			skipBody = true;
			skipEndChecks = true;
		}
		else if (HasCompiledOutput())
		{
			if ((methodDef->mName == BF_METHODNAME_MARKMEMBERS) || (methodDef->mName == BF_METHODNAME_MARKMEMBERS_STATIC))
			{
				if (mCompiler->mOptions.mEnableRealtimeLeakCheck)
				{
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
					EmitGCFindTLSMembers();
				}
			}
		}
	}
	else if (!skipBody)
	{		
		bool isEmptyBodied = BfNodeDynCast<BfTokenNode>(methodDef->mBody) != NULL;

		if (isEmptyBodied) // Generate autoProperty things
		{
			auto propertyDeclaration = methodDef->GetPropertyDeclaration();
			if ((propertyDeclaration != NULL) && (!typeDef->HasAutoProperty(propertyDeclaration)))
			{
				if ((!mCurTypeInstance->IsInterface()) && (!hasExternSpecifier))
					Fail("Body expected", methodDef->mBody);
			}			
			else if (methodDef->mMethodType == BfMethodType_PropertyGetter)
			{
				if (methodInstance->mReturnType->IsValuelessType())
				{
					mBfIRBuilder->CreateRetVoid();
				}
				else if (HasCompiledOutput())
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
							lookupValue = ExtractValue(GetThis(), fieldInstance, fieldInstance->mFieldIdx);						
						if (!methodInstance->mReturnType->IsRef())
							lookupValue = LoadValue(lookupValue);
						mBfIRBuilder->CreateRet(lookupValue.mValue);
						EmitLifetimeEnds(&mCurMethodState->mHeadScope);
					}
					else
					{
						// This can happen if we have two properties with the same name but different types
						AssertErrorState();
						mBfIRBuilder->CreateRet(GetDefaultValue(methodInstance->mReturnType));
					}
				}
				else
				{
					// During autocomplete, the actual type may not have the proper field instance
					mBfIRBuilder->CreateRet(GetDefaultValue(methodInstance->mReturnType));
				}
				mCurMethodState->SetHadReturn(true);
			}
			else if (methodDef->mMethodType == BfMethodType_PropertySetter)
			{
				if (!mCompiler->IsAutocomplete())
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
								BF_FATAL("Should not happen");
							}
							if (mCurTypeInstance->IsObject())
								thisValue = LoadValue(thisValue);							
							lookupAddr = mBfIRBuilder->CreateInBoundsGEP(thisValue.mValue, 0, fieldInstance->mDataIdx);							
						}
						mBfIRBuilder->CreateStore(lastParam->mValue, lookupAddr);
					}
					else if (!fieldInstance->mResolvedType->IsValuelessType())
					{
						// This can happen if we have two properties with the same name but different types
						AssertErrorState();
					}
				}
			}
		}

		if ((!mCurMethodInstance->mReturnType->IsValuelessType()) && (!isEmptyBodied))
		{			
			mBfIRBuilder->PopulateType(mCurMethodInstance->mReturnType);
			
			if (mCurMethodInstance->HasStructRet())
			{			
				auto ptrType = CreatePointerType(mCurMethodInstance->mReturnType);
				auto allocaInst = AllocLocalVariable(ptrType, "__return.addr", false);				
				auto storeInst = mBfIRBuilder->CreateStore(mBfIRBuilder->GetArgument(0), allocaInst);
				mBfIRBuilder->ClearDebugLocation(storeInst);
				mCurMethodState->mRetValAddr = allocaInst;
			}
			else
			{
				auto allocaInst = AllocLocalVariable(mCurMethodInstance->mReturnType, "__return", false);
				mCurMethodState->mRetVal = BfTypedValue(allocaInst, mCurMethodInstance->mReturnType, true);
			}			
		}

		if (methodDef->mMethodType == BfMethodType_CtorCalcAppend)
		{
			mBfIRBuilder->CreateStore(GetConstValue(0), mCurMethodState->mRetVal.mValue);
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
				if ((methodDef->mMethodType != BfMethodType_Normal) && (propertyDeclaration == NULL))
				{
					BF_ASSERT(methodDeclaration->mFatArrowToken != NULL);
					Fail("Only normal methods can have expression bodies", methodDeclaration->mFatArrowToken);
				}

				auto expectingType = mCurMethodInstance->mReturnType;
// 				if ((expectingType->IsVoid()) && (IsInSpecializedSection()))
// 				{
// 					Warn(0, "Using a 'void' return with an expression-bodied method isn't needed. Consider removing '=>' token", methodDeclaration->mFatArrowToken);
// 				}
				
				//TODO: Why did we have all this stuff for handling void?
// 				if (expectingType->IsVoid())
// 				{
// 					bool wasReturnGenericParam = false;
// 					if ((mCurMethodState->mClosureState != NULL) && (mCurMethodState->mClosureState->mReturnType != NULL))
// 					{
// 						wasReturnGenericParam = mCurMethodState->mClosureState->mReturnType->IsGenericParam();
// 					}
// 					else
// 					{
// 						auto unspecializedMethodInstance = GetUnspecializedMethodInstance(mCurMethodInstance);
// 						if ((unspecializedMethodInstance != NULL) && (unspecializedMethodInstance->mReturnType->IsGenericParam()))
// 							wasReturnGenericParam = true;
// 					}
// 					
// 					// If we the void return was from a generic specialization, allow us to return a void result,
// 					//  otherwise treat expression as though it must be a statement
// 					bool isStatement = expressionBody->VerifyIsStatement(mCompiler->mPassInstance, wasReturnGenericParam);
// 					if (isStatement)
// 						expectingType = NULL;
// 				}

				UpdateSrcPos(expressionBody);
				auto retVal = CreateValueFromExpression(expressionBody, expectingType);
				if ((retVal) && (expectingType != NULL))
				{
					mCurMethodState->mHadReturn = true;
					retVal = LoadValue(retVal);
					EmitReturn(retVal.mValue);
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

	if (!mCurMethodState->mHadReturn)
	{
		// Clear off the stackallocs that have occurred after a scopeData break		
		EmitDeferredScopeCalls(false, &mCurMethodState->mHeadScope, mCurMethodState->mIRExitBlock);
	}
	
	if (mCurMethodState->mIRExitBlock)
	{
		for (auto preExitBlock : mCurMethodState->mHeadScope.mAtEndBlocks)
			mBfIRBuilder->MoveBlockToEnd(preExitBlock);

        mBfIRBuilder->MoveBlockToEnd(mCurMethodState->mIRExitBlock);
		mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRExitBlock);

		if (!mCurMethodState->mDIRetVal)
			CreateDIRetVal();
	}

	for (auto localVar : mCurMethodState->mLocals)
	{
		if ((skipEndChecks) || (bodyBlock == NULL))
			break;

		LocalVariableDone(localVar, true);
	}

	if (mCurMethodState->mIRExitBlock)
	{
		if ((mCurMethodState->mRetVal) && (!mCurMethodInstance->HasStructRet()))
		{			
			auto loadedVal = mBfIRBuilder->CreateLoad(mCurMethodState->mRetVal.mValue);
			
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
			if ((irParamCount == 0) && (!IsTargetingBeefBackend()) && (mCompiler->mOptions.mAllowHotSwapping))
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

	if ((mCurMethodInstance->mIsUnspecialized) /*|| (typeDef->mIsFunction)*/ || (mCurTypeInstance->IsUnspecializedType()))
	{
		// Don't keep instructions for unspecialized types
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction); 		
	}	
	
	if ((hasExternSpecifier) && (!skipBody))
	{
		// If we hot swap, we want to make sure at least one method refers to this extern method so it gets pulled in
		//  incase it gets called later by some hot-loaded coded
		if (mCompiler->mOptions.mAllowHotSwapping)
			CreateFakeCallerMethod(mangledName);
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);
	}
	else if ((methodDef->mImportKind == BfImportKind_Dynamic) && (!mCompiler->IsHotCompile()))
	{
		// We only need the DLL stub when we may be hot swapping
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);
	}
	else if (wantsRemoveBody)
		mBfIRBuilder->Func_DeleteBody(mCurMethodInstance->mIRFunction);

	// We don't want to hold on to pointers to LLVMFunctions of unspecialized types.
	//  This allows us to delete the mScratchModule LLVM module without rebuilding all
	//  unspecialized types
	if ((mCurTypeInstance->IsUnspecializedType()) || (mCurTypeInstance->IsInterface()))
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
	if (rootMethodState->mMethodInstance->mMethodInfoEx != NULL)
	{
		for (auto methodGenericArg : rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments)
		{
			StringT<128> genericTypeName;
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

	if (methodDeclaration != NULL)
	{
		BfDefBuilder defBuilder(mCompiler->mSystem);
		defBuilder.mPassInstance = mCompiler->mPassInstance;
		defBuilder.mCurTypeDef = mCurMethodInstance->mMethodDef->mDeclaringType;

		BfMethodDef* outerMethodDef = NULL;
		if (localMethod->mOuterLocalMethod != NULL)
			outerMethodDef = localMethod->mOuterLocalMethod->mMethodDef;
		else
			outerMethodDef = rootMethodState->mMethodInstance->mMethodDef;

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
	if (rootMethodState->mMethodInstance->mMethodInfoEx != NULL)
		dependentGenericStartIdx = (int)rootMethodState->mMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();
	
	BfMethodInstance* outerMethodInstance = mCurMethodInstance;
	
	if (methodGenericArguments.size() == 0)
	{
		if (rootMethodState->mMethodInstance->mMethodInfoEx != NULL)
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
			CreateValueFromExpression(bodyExpr);
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
	funcType = mBfIRBuilder->CreateFunctionType(mBfIRBuilder->MapType(voidType), paramTypes, false);	

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
	if (methodInstance->mIsUnspecializedVariation)
		wantsVisitBody = false;

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
		_VisitLambdaBody();
		RestoreScopeState();

		//ProcessMethod(methodInstance);

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

	//RestoreScopeState();

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
					if (localVar->mIsThis)
					{
						// We can only set mutating if our owning type is mutating
						if (localVar->mWrittenToId >= closureState.mCaptureStartAccessId)
						{
							if (mCurTypeInstance->IsValueType())
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

		 			if ((localVar->mConstValue) || (localVar->mResolvedType->IsValuelessType()))
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
	if ((!localMethod->mDeclOnly) && (!methodInstance->mIsUnspecializedVariation) && 
		(!mWantsIRIgnoreWrites) && (methodDef->mMethodType != BfMethodType_Mixin))
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
						auto bfError = Fail("Method already declared with the same parameter types", methodInstance->mMethodDef->GetRefNode(), true);
						if (bfError != NULL)
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
	
	if (methodInstance->GetCustomAttributes() != NULL)
		return;
		
	auto methodDeclaration = methodDef->GetMethodDeclaration();
	auto propertyMethodDeclaration = methodDef->GetPropertyMethodDeclaration();
	auto typeInstance = methodInstance->GetOwner();

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(mCurTypeInstance, typeInstance);
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(mCurMethodInstance, methodInstance);

	if ((methodDeclaration != NULL) && (methodDeclaration->mAttributes != NULL))
	{
		if (methodInstance->GetMethodInfoEx()->mMethodCustomAttributes == NULL)
			methodInstance->mMethodInfoEx->mMethodCustomAttributes = new BfMethodCustomAttributes();
		methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes = GetCustomAttributes(methodDeclaration->mAttributes, 
			((methodDef->mMethodType == BfMethodType_Ctor) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend)) ? BfAttributeTargets_Constructor : BfAttributeTargets_Method);
	}
	else if ((propertyMethodDeclaration != NULL) && (propertyMethodDeclaration->mAttributes != NULL))
	{		
		if (methodInstance->GetMethodInfoEx()->mMethodCustomAttributes == NULL)
			methodInstance->mMethodInfoEx->mMethodCustomAttributes = new BfMethodCustomAttributes();
		methodInstance->mMethodInfoEx->mMethodCustomAttributes->mCustomAttributes = GetCustomAttributes(propertyMethodDeclaration->mAttributes, BfAttributeTargets_Method);
	}	
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

	if (isTemporaryFunc)
	{
		BF_ASSERT((mCompiler->mIsResolveOnly) && (mCompiler->mResolvePassData->mAutoComplete != NULL));
		mangledName = "autocomplete_tmp";
	}

	if (!mBfIRBuilder->mIgnoreWrites)
	{
		BfIRFunction prevFunc;

		// Remove old version (if we have one)
		prevFunc = mBfIRBuilder->GetFunction(mangledName);
		if (prevFunc)
		{
			if (methodDef->mIsExtern)
			{
				// Allow this
				BfLogSysM("Function collision allowed multiple extern functions for %p: %d\n", methodInstance, prevFunc.mId);
			}
			else if (typeInstance->IsIRFuncUsed(prevFunc))
			{
				// We can have a collision of names when we have generic methods that differ only in
				//  their constraints, but they should only collide in their unspecialized form
				//  since only one will be chosen for a given concrete type		
				mCurMethodInstance->mMangleWithIdx = true;
				mangledName.Clear();
				BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), mCurMethodInstance);
				prevFunc = mBfIRBuilder->GetFunction(mangledName);

				BfLogSysM("Function collision forced mangleWithIdx for %p: %d\n", methodInstance, prevFunc.mId);
			}
			else
			{
				BfLogSysM("Function collision erased prevFunc %p: %d\n", methodInstance, prevFunc.mId);

				mBfIRBuilder->Func_EraseFromParent(prevFunc);
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
		bool wantsLLVMFunc = ((!typeInstance->IsUnspecializedType()) && (!methodDef->IsEmptyPartial())) && (funcType);

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
		methodInstance->mHotMethod = hotMethod;
	}
}

// methodDeclaration is NULL for default constructors
void BfModule::DoMethodDeclaration(BfMethodDeclaration* methodDeclaration, bool isTemporaryFunc, bool addToWorkList)
{
	BP_ZONE("BfModule::BfMethodDeclaration");	

	// We could trigger a DoMethodDeclaration from a const resolver or other location, so we reset it here
	//  to effectively make mIgnoreWrites method-scoped
	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, mWantsIRIgnoreWrites || mCurMethodInstance->mIsUnspecialized);
	SetAndRestoreValue<bool> prevIsCapturingMethodMatchInfo;
	SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, NULL);
	if (mCompiler->IsAutocomplete())	
		prevIsCapturingMethodMatchInfo.Init(mCompiler->mResolvePassData->mAutoComplete->mIsCapturingMethodMatchInfo, false);
	
	if (mCurMethodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_NoDecl_AwaitingReference)
		mCurMethodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingReference;

	bool ignoreWrites = mBfIRBuilder->mIgnoreWrites;

	if (mAwaitingInitFinish)
		FinishInit();

	auto typeInstance = mCurTypeInstance;
	auto typeDef = typeInstance->mTypeDef;		
	auto methodDef = mCurMethodInstance->mMethodDef;	
	
	BF_ASSERT(methodDef->mName != "__ASSERTNAME");

	if (typeInstance->IsClosure())
	{
		if (methodDef->mName == "Invoke")
			return;
	}

	auto methodInstance = mCurMethodInstance;	

	if ((methodInstance->IsSpecializedByAutoCompleteMethod()) || (mCurTypeInstance->IsFunction()))
		addToWorkList = false;

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
		auto rootMethodInstance = prevMethodState.mPrevVal->GetRootMethodState()->mMethodInstance;
		dependentGenericStartIdx = 0;
		if (rootMethodInstance->mMethodInfoEx != NULL)
			dependentGenericStartIdx = (int)rootMethodInstance->mMethodInfoEx->mMethodGenericArguments.size();

		methodInstance->mIsUnspecialized = rootMethodInstance->mIsUnspecialized;
		methodInstance->mIsUnspecializedVariation = rootMethodInstance->mIsUnspecializedVariation;
	}

	for (int genericArgIdx = dependentGenericStartIdx; genericArgIdx < (int) methodInstance->GetNumGenericArguments(); genericArgIdx++)
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
	
	methodInstance->mIsUnspecializedVariation |= typeInstance->IsUnspecializedTypeVariation();

	for (auto genericParamDef : methodDef->mGenericParams)
	{
		if (mCompiler->IsAutocomplete())
		{
			auto autoComplete = mCompiler->mResolvePassData->mAutoComplete;
			//autoComplete->CheckTypeRef()
		}
	}

	if (methodInstance->mIsUnspecializedVariation)
	{
		
	}

	if (methodInstance->mIsUnspecializedVariation)
		BF_ASSERT(methodInstance->mIsUnspecialized);

	if (methodDef->mMethodType == BfMethodType_Mixin)
		methodInstance->mIsUnspecialized = true;	

	BfAutoComplete* bfAutocomplete = NULL;
	if (mCompiler->mResolvePassData != NULL)
		bfAutocomplete = mCompiler->mResolvePassData->mAutoComplete;
	
	for (int genericParamIdx = 0; genericParamIdx < (int)methodInstance->GetNumGenericArguments(); genericParamIdx++)
	{			
		auto genericParamDef = methodDef->mGenericParams[genericParamIdx];
		ResolveGenericParamConstraints(methodInstance->mMethodInfoEx->mGenericParams[genericParamIdx], methodDef->mGenericParams, genericParamIdx);

		if (bfAutocomplete != NULL)
		{
			for (auto nameNode : genericParamDef->mNameNodes)
			{
				HandleMethodGenericParamRef(nameNode, typeDef, methodDef, genericParamIdx);
			}
		}
	}

	if (methodInstance->mMethodInfoEx != NULL)
	{
		for (auto genericParam : methodInstance->mMethodInfoEx->mGenericParams)
		{
			for (auto constraintTypeInst : genericParam->mInterfaceConstraints)
				AddDependency(constraintTypeInst, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
			if (genericParam->mTypeConstraint != NULL)
				AddDependency(genericParam->mTypeConstraint, mCurTypeInstance, BfDependencyMap::DependencyFlag_Constraint);
		}
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

		if (autoComplete->IsAutocompleteNode(nameNode))
		{
			autoComplete->mInsertStartIdx = nameNode->GetSrcStart();
			autoComplete->mInsertEndIdx = nameNode->GetSrcEnd();
			autoComplete->mDefType = typeDef;
			autoComplete->mDefMethod = methodDef;
			autoComplete->SetDefinitionLocation(nameNode);

			if (methodDef->mIsOverride)
			{
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
	if (((methodDef->mMethodType == BfMethodType_Normal) || (methodDef->mMethodType == BfMethodType_CtorCalcAppend) || (methodDef->mMethodType == BfMethodType_PropertyGetter) || (methodDef->mMethodType == BfMethodType_Operator)) &&
		(methodDef->mReturnTypeRef != NULL))
	{
		SetAndRestoreValue<bool> prevIngoreErrors(mIgnoreErrors, mIgnoreErrors || (methodDef->GetPropertyDeclaration() != NULL));
		resolvedReturnType = ResolveTypeRef(methodDef->mReturnTypeRef, BfPopulateType_Declaration, 
			(BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef | BfResolveTypeRefFlag_AllowRefGeneric));

		if ((resolvedReturnType != NULL) && (resolvedReturnType->IsVar()) && (methodDef->mMethodType != BfMethodType_Mixin))
		{
			Fail("Cannot declare var return types", methodDef->mReturnTypeRef);
			resolvedReturnType = GetPrimitiveType(BfTypeCode_Var);
		}
		
		if (resolvedReturnType == NULL)
			resolvedReturnType = GetPrimitiveType(BfTypeCode_Var);
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
		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
			mCompiler->mResolvePassData->mAutoComplete->CheckTypeRef(methodDef->mExplicitInterface, false);
		auto explicitInterface = ResolveTypeRef(methodDef->mExplicitInterface, BfPopulateType_Declaration);
		if (explicitInterface != NULL)
			mCurMethodInstance->GetMethodInfoEx()->mExplicitInterface = explicitInterface->ToTypeInstance();
		if ((mCurMethodInstance->mMethodInfoEx != NULL) && (mCurMethodInstance->mMethodInfoEx->mExplicitInterface != NULL))
		{
			bool interfaceFound = false;
			for (auto ifaceInst : typeInstance->mInterfaces)
				interfaceFound |= ifaceInst.mInterfaceType == mCurMethodInstance->mMethodInfoEx->mExplicitInterface;
			if (!interfaceFound)
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

		if ((paramDef != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) &&
			(paramDef->mParamKind != BfParamKind_AppendIdx))
			mCompiler->mResolvePassData->mAutoComplete->CheckTypeRef(paramDef->mTypeRef, false);

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
		else if (paramDef->mTypeRef->IsA<BfLetTypeReference>())
		{
			Fail("Cannot declare 'let' parameters", paramDef->mTypeRef);
			resolvedParamType = mContext->mBfObjectType;
		}
		else if (paramDef->mTypeRef->IsA<BfVarTypeReference>())
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
			SetAndRestoreValue<bool> prevIngoreErrors(mIgnoreErrors, mIgnoreErrors || (methodDef->GetPropertyDeclaration() != NULL));
			resolvedParamType = ResolveTypeRef(paramDef->mTypeRef, BfPopulateType_Declaration, 
				(BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef | BfResolveTypeRefFlag_AllowRefGeneric | BfResolveTypeRefFlag_AllowGenericMethodParamConstValue));
		}
		if (resolvedParamType == NULL)
		{
			resolvedParamType = GetPrimitiveType(BfTypeCode_Var);
			unresolvedParamType = resolvedParamType;
			if (mCurTypeInstance->IsBoxed())
			{
				auto boxedType = (BfBoxedType*)mCurTypeInstance;
				// If we failed a lookup here then we better have also failed it in the original type
				BF_ASSERT(boxedType->mElementType->mModule->mHadBuildError || mContext->mFailTypes.Contains(boxedType->mElementType));
			}
		}

		if (!methodInstance->IsSpecializedGenericMethod())
			AddDependency(resolvedParamType, typeInstance, BfDependencyMap::DependencyFlag_ParamOrReturnValue);				
		PopulateType(resolvedParamType, BfPopulateType_Declaration);		

		AddDependency(resolvedParamType, mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

		if ((paramDef != NULL) && (paramDef->mParamDeclaration != NULL) && (paramDef->mParamDeclaration->mInitializer != NULL) &&
			(!paramDef->mParamDeclaration->mInitializer->IsA<BfBlock>()))
		{
			BfMethodState methodState;
			SetAndRestoreValue<BfMethodState*> prevMethodState(mCurMethodState, &methodState);
			methodState.mTempKind = BfMethodState::TempKind_Static;

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
				BfConstResolver constResolver(this);
				defaultValue = constResolver.Resolve(paramDef->mParamDeclaration->mInitializer, resolvedParamType, BfConstResolveFlag_NoCast);
				if ((defaultValue) && (defaultValue.mType != resolvedParamType))
				{
					SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites, true);
					auto castedDefaultValue = Cast(paramDef->mParamDeclaration->mInitializer, defaultValue, resolvedParamType, (BfCastFlags)(BfCastFlags_SilentFail | BfCastFlags_NoConversionOperator));
					if ((castedDefaultValue) && (castedDefaultValue.mValue.IsConst()))
					{
						defaultValue = castedDefaultValue; // Good!
					}
					else if (!CanImplicitlyCast(defaultValue, resolvedParamType))
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
				BF_ASSERT(defaultValue.mValue.IsConst());
				while ((int)mCurMethodInstance->mDefaultValues.size() < paramDefIdx)
					mCurMethodInstance->mDefaultValues.Add(BfIRValue());

				CurrentAddToConstHolder(defaultValue.mValue);
				mCurMethodInstance->mDefaultValues.Add(defaultValue.mValue);
			}
		}		

		if ((paramDef != NULL) && (paramDef->mParamKind == BfParamKind_Params))
		{	
			bool addParams = true;
			bool isValid = false;

			auto resolvedParamTypeInst = resolvedParamType->ToTypeInstance();

			if ((resolvedParamTypeInst != NULL) && (resolvedParamTypeInst->mTypeDef == mCompiler->mSpanTypeDef))
			{
				// Span<T>
				isValid = true;
			}
			else if (resolvedParamType->IsArray())
			{
				// Array is the 'normal' params type				
				isValid = true;
			}
			else if ((resolvedParamType->IsDelegate()) || (resolvedParamType->IsFunction()))
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
					if (genericParamInstance->mTypeConstraint->IsArray())
					{
						BfMethodParam methodParam;						
						methodParam.mResolvedType = resolvedParamType;
						methodParam.mParamDefIdx = paramDefIdx;				
						mCurMethodInstance->mParams.Add(methodParam);
						isValid = true;
					}
					else if ((genericParamInstance->mTypeConstraint->IsDelegate()) || (genericParamInstance->mTypeConstraint->IsFunction()) ||
						((genericParamInstance != NULL) && (typeInstConstraint != NULL) &&
						 ((typeInstConstraint->mTypeDef == mCompiler->mDelegateTypeDef) || (typeInstConstraint->mTypeDef == mCompiler->mFunctionTypeDef))))
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
					((paramTypeInst->mTypeDef == mCompiler->mDelegateTypeDef) || (paramTypeInst->mTypeDef == mCompiler->mFunctionTypeDef)))
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

			if (addParams)
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
	if (methodInstance->HasStructRet())
		argIdx++;
	if (!methodDef->mIsStatic)
    {
		if (methodInstance->GetParamIsSplat(-1))
			argIdx += methodInstance->GetParamType(-1)->GetSplatCount();
		else if (!methodInstance->GetOwner()->IsValuelessType())
			argIdx++;
	}
	for (auto& methodParam : mCurMethodInstance->mParams)
	{
		if (methodParam.mResolvedType->IsMethodRef())
		{
			methodParam.mIsSplat = true;
		}
		else if (methodParam.mResolvedType->IsComposite())
		{
			PopulateType(methodParam.mResolvedType, BfPopulateType_Data);
			if (methodParam.mResolvedType->IsSplattable())
			{
				int splatCount = methodParam.mResolvedType->GetSplatCount();
				if (argIdx + splatCount <= mCompiler->mOptions.mMaxSplatRegs)
				{
					methodParam.mIsSplat = true;
					argIdx += splatCount;
					continue;
				}
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
				if (!defaultMethodInstance->mHadGenericDelegateParams)
					BF_ASSERT((isDone) && (isDefaultDone));

				break;
			}			

			methodInstance->mParams[paramIdx + implicitParamCount].mWasGenericParam = defaultMethodInstance->mParams[defaultParamIdx + defaultImplicitParamCount].mResolvedType->IsGenericParam();			
			paramIdx++;
			defaultParamIdx++;
		}		
	}

	StringT<128> mangledName;
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

	auto func = methodInstance->mIRFunction;

// 	if (methodInstance->mIsReified)
// 		CheckHotMethod(methodInstance, mangledName);

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

	if ((methodDeclaration != NULL) && (methodDeclaration->mMutSpecifier != NULL) && (!mCurTypeInstance->IsBoxed()) && (!methodInstance->mIsForeignMethodDef))
	{
		if (methodDef->mIsStatic)
			Warn(0, "Unnecessary 'mut' specifier, static methods have no implicit 'this' target to mutate", methodDeclaration->mMutSpecifier);
		else if ((!mCurTypeInstance->IsValueType()) && (!mCurTypeInstance->IsInterface()))
			Warn(0, "Unnecessary 'mut' specifier, methods of reference types are implicitly mutating", methodDeclaration->mMutSpecifier);
		else if (methodDef->mMethodType == BfMethodType_Ctor)
			Warn(0, "Unnecessary 'mut' specifier, constructors are implicitly mutating", methodDeclaration->mMutSpecifier);
		else if (methodDef->mMethodType == BfMethodType_Dtor)
			Warn(0, "Unnecessary 'mut' specifier, destructors are implicitly mutating", methodDeclaration->mMutSpecifier);
	}
	
	if (isTemporaryFunc)
	{
		// This handles temporary methods for autocomplete types
		BF_ASSERT(mIsScratchModule);
		BF_ASSERT(mCompiler->IsAutocomplete());
		BfLogSysM("DoMethodDeclaration autocomplete bailout\n");
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
			mFuncReferences[methodInstance] = func;	
	}
		
	if (addToWorkList)
	{
		bool wasAwaitingDecl = methodInstance->mMethodInstanceGroup->mOnDemandKind == BfMethodOnDemandKind_Decl_AwaitingDecl;
		if (wasAwaitingDecl)
			methodInstance->mMethodInstanceGroup->mOnDemandKind = BfMethodOnDemandKind_Decl_AwaitingReference;		

		if ((!methodDef->mIsAbstract) && (!methodInstance->mIgnoreBody))
		{
			if (!wasAwaitingDecl)
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
		
	if ((!methodInstance->IsSpecializedGenericMethodOrType()) && (!mCurTypeInstance->IsBoxed()) && 
		(!methodDef->mIsLocalMethod) &&
		(!CheckDefineMemberProtection(methodDef->mProtection, methodInstance->mReturnType)))
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
	
	if ((typeInstance->IsInterface()) && (methodDef->mMethodType == BfMethodType_Ctor))
	{
		Fail("Interfaces cannot contain constructors", methodDeclaration);
	}

	// Don't compare specialized generic methods against normal methods
	if ((((mCurMethodInstance->mIsUnspecialized) || (mCurMethodInstance->mMethodDef->mGenericParams.size() == 0))) &&
		(!methodDef->mIsLocalMethod))
	{
		if (!methodInstance->mIsForeignMethodDef)
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

				if ((checkMethod != methodDef) && (typeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault != NULL))
				{					
					auto checkMethodInstance = typeInstance->mMethodInstanceGroups[checkMethod->mIdx].mDefault;

					if (((checkMethodInstance->mChainType == BfMethodChainType_None) || (checkMethodInstance->mChainType == BfMethodChainType_ChainHead)) &&						
						(checkMethodInstance->GetExplicitInterface() == methodInstance->GetExplicitInterface()) &&
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
							CompareDeclTypes(checkMethodInstance->mMethodDef->mDeclaringType, methodInstance->mMethodDef->mDeclaringType, isBetter, isWorse);
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
						else if ((checkMethod->mBody == NULL) && (methodDef->mBody != NULL) && 
							(checkMethod->mDeclaringType != methodDef->mDeclaringType))
						{
							// We're allowed to override an empty-bodied method
							checkMethodInstance->mChainType = BfMethodChainType_ChainSkip;
						}
						else
						{
							if (!typeInstance->IsTypeMemberAccessible(checkMethod->mDeclaringType, methodDef->mDeclaringType))
								continue;
							
							bool silentlyAllow = false;							
							if (checkMethod->mDeclaringType != methodDef->mDeclaringType)
							{								
								if (typeInstance->IsInterface())
								{									
									if (methodDef->mIsOverride)
										silentlyAllow = true;
								}
								else
									silentlyAllow = true;
							}
							
							if (!silentlyAllow)
							{	
								if (!methodDef->mName.IsEmpty())
								{
									auto refNode = methodDef->GetRefNode();
									auto bfError = Fail("Method already declared with the same parameter types", refNode, true);
									if (bfError != NULL)
										mCompiler->mPassInstance->MoreInfo("First declaration", checkMethod->GetRefNode());
								}
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
			bool foundHiddenMethod = false;
			auto baseType = typeInstance->mBaseType;
			while (baseType != NULL)
			{					
				//for (int checkMethodIdx = 0; checkMethodIdx < (int) baseType->mTypeDef->mMethods.size(); checkMethodIdx++)

				auto baseTypeDef = baseType->mTypeDef;
				baseTypeDef->PopulateMemberSets();
				BfMethodDef* checkMethod = NULL;
				BfMemberSetEntry* entry = NULL;
				if (baseTypeDef->mMethodSet.TryGet(BfMemberSetEntry(methodDef), &entry))
					checkMethod = (BfMethodDef*)entry->mMemberDef;

				while (checkMethod != NULL)
				{					
					if (checkMethod->mMethodDeclaration == NULL)
						continue;
					
					if (baseType->mMethodInstanceGroups.size() == 0)
					{
						BF_ASSERT(baseType->IsIncomplete() && mCompiler->IsAutocomplete());
						break;
					}

					if (checkMethod == methodDef)
						continue;
					if (checkMethod->mName != methodDef->mName)
						continue;

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


			if ((methodDef->mIsNew) && (!foundHiddenMethod))
			{
				auto propertyDeclaration = methodDef->GetPropertyDeclaration();
				auto tokenNode = (propertyDeclaration != NULL) ? propertyDeclaration->mNewSpecifier :
					methodDeclaration->mNewSpecifier;
				Fail("Method does not hide an inherited member. The 'new' keyword is not required", tokenNode, true);				
			}
		}
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
			if (typeInstance->mBaseType != NULL)
				vTableStart = typeInstance->mBaseType->mVirtualMethodTableSize;

			StringT<128> mangledName;
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

void BfModule::CompareDeclTypes(BfTypeDef* newDeclType, BfTypeDef* prevDeclType, bool& isBetter, bool& isWorse)
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
		Fail("Static members cannot be marked as override, virtual, or abstract", virtualToken, true);
	}
	else if (methodDef->mIsVirtual)
	{
		if (mCompiler->IsHotCompile())
			mContext->EnsureHotMangledVirtualMethodName(methodInstance);

		if ((methodDef->mProtection == BfProtection_Private) && (virtualToken != NULL))
			Fail("Virtual or abstract members cannot be private", virtualToken, true);

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
				if (lookupType->mTypeDef->mMethodSet.TryGetWith(methodInstance->mMethodDef->mName, &entry))
					nextMethodDef = (BfMethodDef*)entry->mMemberDef;

				while (nextMethodDef != NULL)
				{
					auto checkMethodDef = nextMethodDef;
					nextMethodDef = nextMethodDef->mNextWithSameName;

					if ((!checkMethodDef->mIsVirtual) || (checkMethodDef->mIsOverride))
						continue;

					BfMethodInstance* lookupMethodInstance = lookupType->mMethodInstanceGroups[checkMethodDef->mIdx].mDefault;
					if ((lookupMethodInstance == NULL) || (lookupMethodInstance->mVirtualTableIdx == -1))
						continue;
				
					int checkMethodIdx = lookupMethodInstance->mVirtualTableIdx;
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

					auto& overridenRef = typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod;
					if (overridenRef.mKind != BfMethodRefKind_AmbiguousRef)
					{
						methodOverriden = overridenRef;

						BfMethodInstance* setMethodInstance = methodInstance;
						bool doOverride = true;
						if (methodOverriden->GetOwner() == methodInstance->GetOwner())
						{
							bool isBetter = false;
							bool isWorse = false;
							CompareDeclTypes(methodInstance->mMethodDef->mDeclaringType, methodOverriden->mMethodDef->mDeclaringType, isBetter, isWorse);
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

								doOverride = isBetter;
							}
						}

						if (doOverride)
						{							
							auto declMethodInstance = (BfMethodInstance*)typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mDeclaringMethod;
							_AddVirtualDecl(declMethodInstance);
							typeInstance->mVirtualMethodTable[virtualMethodMatchIdx].mImplementingMethod = setMethodInstance;							
						}
					}
				}

				if (methodOverriden != NULL)
				{
					auto prevProtection = methodOverriden->mMethodDef->mProtection;
					if ((methodDef->mProtection != prevProtection) && (methodDef->mMethodType != BfMethodType_Dtor))
					{
						const char* protectionNames[] = {"hidden", "private", "protected", "public"};
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
		bool hadNameMatch = false;		
		BfType* expectedReturnType = NULL;
		
		bool showedError = false;

		// We go through our VTable looking for NULL entries within the interface sections
		//  The only instance of finding a "better" method is when we have explicitly interface-dotted method
		//  because a normal method declared in this type could have matched earlier
		//for (int iMethodIdx = 0; iMethodIdx < iMethodCount; iMethodIdx++)

		ifaceInst->mTypeDef->PopulateMemberSets();
		BfMethodDef* checkMethodDef = NULL;
		BfMemberSetEntry* entry;
		if (ifaceInst->mTypeDef->mMethodSet.TryGetWith(methodInstance->mMethodDef->mName, &entry))
			checkMethodDef = (BfMethodDef*)entry->mMemberDef;

		while (checkMethodDef != NULL)
		{
			int iMethodIdx = checkMethodDef->mIdx;

			int iTableIdx = iMethodIdx + startIdx;
			BfTypeInterfaceMethodEntry* interfaceMethodEntry = &typeInstance->mInterfaceMethodTable[iTableIdx];
			auto iMethodPtr = &interfaceMethodEntry->mMethodRef;
			bool storeIFaceMethod = false;
						
			if ((mCompiler->mPassInstance->HasFailed()) && (iMethodIdx >= (int)ifaceInst->mMethodInstanceGroups.size()))
				continue;

			auto& iMethodGroup = ifaceInst->mMethodInstanceGroups[iMethodIdx];
			auto iMethodInst = iMethodGroup.mDefault;
			if (iMethodInst == NULL)
			{
				if ((!ifaceInst->IsGenericTypeInstance()) || (!ifaceInst->mTypeDef->mIsCombinedPartial))
					AssertErrorState();
				continue;
			}
			if (iMethodInst->mMethodDef->mName == methodInstance->mMethodDef->mName)
				hadNameMatch = true;								

			bool doesMethodSignatureMatch = CompareMethodSignatures(iMethodInst, methodInstance);
				
			if ((!doesMethodSignatureMatch) && (iMethodInst->GetNumGenericParams() == 0) && (interfaceMethodEntry->mMethodRef.IsNull()))
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
					if ((mCompiler->mIsResolveOnly) && (prevMethod == methodInstance))
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
						CompareDeclTypes(methodInstance->mMethodDef->mDeclaringType, prevMethod->mMethodDef->mDeclaringType, isBetter, isWorse);
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
				_AddVirtualDecl(iMethodInst);				
				*iMethodPtr = methodInstance;
			}

			checkMethodDef = checkMethodDef->mNextWithSameName;
		}

		if ((methodInstance->mMethodInfoEx != NULL) && (methodInstance->mMethodInfoEx->mExplicitInterface == ifaceInst) && (!hadMatch) && (!showedError))
		{	
			if (expectedReturnType != NULL)
				Fail(StrFormat("Wrong return type, expected '%s'", TypeToString(expectedReturnType).c_str()), declaringNode, true);
			else if (hadNameMatch)
				Fail("Method parameters don't match interface method", declaringNode, true);
			else
				Fail("Method name not found in interface", methodDef->GetRefNode(), true);
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

bool BfModule::SlotInterfaceMethod(BfMethodInstance* methodInstance)
{	
	auto typeInstance = mCurTypeInstance;
	auto methodDef = methodInstance->mMethodDef;

	if (methodDef->mMethodType == BfMethodType_Ctor)
		return true;
	
	bool foundOverride = false;

	if ((methodDef->mBody == NULL) && (methodDef->mProtection == BfProtection_Private))
	{
		Fail("Private interface methods must provide a body", methodDef->GetMethodDeclaration()->mProtectionSpecifier);
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
					(!methodInstGroup.mDefault->mMethodDef->mIsStatic) && (methodInstGroup.mDefault->mIsReified) &&
					((methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_AlwaysInclude) || (methodInstGroup.mOnDemandKind == BfMethodOnDemandKind_Referenced)))
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

	BF_ASSERT(mAddedToCount);
	mAddedToCount = false;
	mAwaitingFinish = false;
	

	mCompiler->mStats.mModulesFinished++;	

	if (HasCompiledOutput())
	{
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

		BF_ASSERT((int)mOutFileNames.size() >= mExtensionCount);

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
		codeGenOptions.mVirtualMethodOfs = 1 + mCompiler->GetDynCastVDataCount() + mCompiler->mMaxInterfaceSlots;
		codeGenOptions.mDynSlotOfs = mSystem->mPtrSize - mCompiler->GetDynCastVDataCount() * 4;

		mCompiler->mStats.mIRBytes += mBfIRBuilder->mStream.GetSize();
		mCompiler->mStats.mConstBytes += mBfIRBuilder->mTempAlloc.GetAllocSize();

		bool allowWriteToLib = true;
		if ((allowWriteToLib) && (codeGenOptions.mOptLevel == BfOptLevel_OgPlus) && (mModuleName != "vdata"))
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
			else if ((!mCompiler->mOptions.mGenerateObj) && (!mCompiler->mOptions.mWriteIR))
			{
				BF_FATAL("Neither obj nor IR specified");
			}

			moduleFileName.mModuleWritten = writeModule;
			moduleFileName.mWroteToLib = mWroteToLib;
			if (!mOutFileNames.Contains(moduleFileName))
				mOutFileNames.Add(moduleFileName);
		}
		
		if (mCompiler->IsHotCompile())
		{
			codeGenOptions.mIsHotCompile = true;
			//TODO: Why did we have this 'mWroteToLib' check? 'vdata' didn't get this flag set, which
			// is required for rebuilding vdata after we hot create a vdata
			//if (mWroteToLib)
			
			{
				if (mParentModule != NULL)
					mParentModule->mHadHotObjectWrites = true;
				mHadHotObjectWrites = true;
			}
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
			BF_ASSERT(!type->IsIncomplete());
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
void BfModule::ClearModuleData()
{
	BfLogSysM("ClearModuleData %p\n", this);

	if (mAddedToCount)
	{
		mCompiler->mStats.mModulesFinished++;
		mAddedToCount = false;
	}

	mDICompileUnit = BfIRMDNode();
	mIsModuleMutable = false;	
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

