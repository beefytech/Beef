#include "BeModule.h"
#include "../Beef/BfCommon.h"
#include "../Compiler/BfUtil.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "../Compiler/BfIRCodeGen.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

void bpt(Beefy::BeType* t)
{
	Beefy::String str;
	if (t->IsStruct())
		BeModule::StructToString(str, (BeStructType*)t);
	else
	{
		BeModule::ToString(str, t);
		str += "\n";
	}
	OutputDebugStrF("%s", str.c_str());

	if (t->IsPointer())
	{
		bpt(((BePointerType*)t)->mElementType);
	}
}

void bpv(Beefy::BeValue* val)
{
	BeDumpContext dumpCtx;
	String str;
	dumpCtx.ToString(str, val);
	str += "\n";
	OutputDebugStr(str);

	auto type = val->GetType();
	if (type != NULL)
		bpt(type);
}

//////////////////////////////////////////////////////////////////////////

void BeValueVisitor::VisitChild(BeValue* value)
{
	if (value != NULL)
		value->Accept(this);
}

BeValue* BeInliner::Remap(BeValue* srcValue)
{
	if (srcValue == NULL)
		return NULL;

	// Try to unwrap mdnode
	if (auto inlinedScope = BeValueDynCast<BeDbgInlinedScope>(srcValue))
		srcValue = inlinedScope->mScope;

	BeValue** valuePtr = NULL;
	/*auto itr = mValueMap.find(srcValue);
	if (itr != mValueMap.end())
		return itr->second;*/
	if (mValueMap.TryGetValue(srcValue, &valuePtr))
		return *valuePtr;
	
	BeMDNode* wrapMDNode = NULL;	

	if (auto dbgFunction = BeValueDynCast<BeDbgFunction>(srcValue))
	{		
		wrapMDNode = dbgFunction;		
	}
	if (auto dbgLexBlock = BeValueDynCast<BeDbgLexicalBlock>(srcValue))
	{
		wrapMDNode = dbgLexBlock;		
	}

	if (auto callInst = BeValueDynCast<BeCallInst>(srcValue))
	{
		if (callInst->mInlineResult != NULL)
			return Remap(callInst->mInlineResult);
	}

	if (wrapMDNode != NULL)
	{		
		auto destMDNode = mOwnedValueVec->Alloc<BeDbgInlinedScope>();
		destMDNode->mScope = wrapMDNode;		
		mValueMap[srcValue] = destMDNode;
		return destMDNode;
	}

	return srcValue;
}

// Insert 'mCallInst->mDbgLoc' at the head of the inline chain
BeDbgLoc* BeInliner::ExtendInlineDbgLoc(BeDbgLoc* srcInlineAt)
{
	if (srcInlineAt == NULL)
		return mCallInst->mDbgLoc;

	/*auto itr = mInlinedAtMap.find(srcInlineAt);
	if (itr != mInlinedAtMap.end())
	{
		return itr->second;
	}*/
	BeDbgLoc** dbgLocPtr = NULL;
	if (mInlinedAtMap.TryGetValue(srcInlineAt, &dbgLocPtr))
		return *dbgLocPtr;
	
	auto dbgLoc = mModule->mAlloc.Alloc<BeDbgLoc>();
	dbgLoc->mLine = srcInlineAt->mLine;
	dbgLoc->mColumn = srcInlineAt->mColumn;
	dbgLoc->mDbgScope = (BeMDNode*)Remap(srcInlineAt->mDbgScope);
	dbgLoc->mIdx = mModule->mCurDbgLocIdx++;
	dbgLoc->mDbgInlinedAt = ExtendInlineDbgLoc(srcInlineAt->mDbgInlinedAt);	
	mInlinedAtMap[srcInlineAt] = dbgLoc;

	return dbgLoc;
}

void BeInliner::AddInst(BeInst* destInst, BeInst* srcInst)
{
	if ((srcInst != NULL) && (srcInst->mDbgLoc != mSrcDbgLoc))
	{
		mSrcDbgLoc = srcInst->mDbgLoc;
		if (mSrcDbgLoc == NULL)
		{
			mDestDbgLoc = NULL;
		}
		else
		{
			BeDbgLoc* inlinedAt = ExtendInlineDbgLoc(mSrcDbgLoc->mDbgInlinedAt);						
			mModule->SetCurrentDebugLocation(mSrcDbgLoc->mLine, mSrcDbgLoc->mColumn, (BeMDNode*)Remap(mSrcDbgLoc->mDbgScope), inlinedAt);
			mDestDbgLoc = mModule->mCurDbgLoc;
		}
	}

	if (srcInst != NULL)
		destInst->mName = srcInst->mName;
	
	destInst->mDbgLoc = mDestDbgLoc;
	destInst->mParentBlock = mDestBlock;
	mDestBlock->mInstructions.push_back(destInst);
	if (srcInst != NULL)
		mValueMap[srcInst] = destInst;
}

void BeInliner::Visit(BeValue* beValue)
{
}

void BeInliner::Visit(BeBlock* beBlock)
{
}

void BeInliner::Visit(BeArgument* beArgument)
{
	
}

void BeInliner::Visit(BeInst* beInst)
{
	BF_FATAL("Not handled");
}

void BeInliner::Visit(BeNopInst* nopInst)
{
	auto destNopInst = AllocInst(nopInst);	
}

void BeInliner::Visit(BeUnreachableInst* unreachableInst)
{
	auto destUnreachableInst = AllocInst(unreachableInst);
}

void BeInliner::Visit(BeEnsureInstructionAtInst* ensureCodeAtInst)
{
	auto destEnsureCodeAtInst = AllocInst(ensureCodeAtInst);
}

void BeInliner::Visit(BeUndefValueInst* undefValue)
{
	auto destUndefValue = AllocInst(undefValue);
	destUndefValue->mType = undefValue->mType;
}

void BeInliner::Visit(BeExtractValueInst* extractValue)
{
	auto destExtractValue = AllocInst(extractValue);
	destExtractValue->mAggVal = Remap(extractValue->mAggVal);	
	destExtractValue->mIdx = extractValue->mIdx;
}

void BeInliner::Visit(BeInsertValueInst* insertValue)
{
	auto destInsertValue = AllocInst(insertValue);
	destInsertValue->mAggVal = Remap(insertValue->mAggVal);
	destInsertValue->mMemberVal = Remap(insertValue->mMemberVal);
	destInsertValue->mIdx = insertValue->mIdx;
}

void BeInliner::Visit(BeNumericCastInst* castInst)
{
	auto destCastInset = AllocInst(castInst);
	destCastInset->mValue = Remap(castInst->mValue);
	destCastInset->mToType = castInst->mToType;
	destCastInset->mValSigned = castInst->mValSigned;
	destCastInset->mToSigned = castInst->mToSigned;
}

void BeInliner::Visit(BeBitCastInst* castInst)
{
	auto destCastInst = AllocInst(castInst);
	destCastInst->mValue = Remap(castInst->mValue);
	destCastInst->mToType = castInst->mToType;
}

void BeInliner::Visit(BeNegInst* negInst)
{
	auto destNegInst = AllocInst(negInst);
	destNegInst->mValue = Remap(negInst->mValue);
}

void BeInliner::Visit(BeNotInst* notInst)
{
	auto destNotInst = AllocInst(notInst);
	destNotInst->mValue = Remap(notInst->mValue);
}

void BeInliner::Visit(BeBinaryOpInst* binaryOpInst)
{
	auto destBinaryOp = AllocInst(binaryOpInst);
	destBinaryOp->mOpKind = binaryOpInst->mOpKind;
	destBinaryOp->mLHS = Remap(binaryOpInst->mLHS);
	destBinaryOp->mRHS = Remap(binaryOpInst->mRHS);	
}

void BeInliner::Visit(BeCmpInst* cmpInst)
{
	auto destCmpInst = AllocInst(cmpInst);
	destCmpInst->mCmpKind = cmpInst->mCmpKind;
	destCmpInst->mLHS = Remap(cmpInst->mLHS);
	destCmpInst->mRHS = Remap(cmpInst->mRHS);
}

void BeInliner::Visit(BeFenceInst* fenceInst)
{
	auto destFenceInst = AllocInst(fenceInst);
}

void BeInliner::Visit(BeStackSaveInst* stackSaveInst)
{
	auto destStackSaveInst = AllocInst(stackSaveInst);
}

void BeInliner::Visit(BeStackRestoreInst* stackRestoreInst)
{
	auto destStackRestoreInst = AllocInst(stackRestoreInst);
	destStackRestoreInst->mStackVal = Remap(stackRestoreInst);
}

void BeInliner::Visit(BeObjectAccessCheckInst* objectAccessCheckInst)
{
	auto destObjectAccessCheckInst = AllocInst(objectAccessCheckInst);
	destObjectAccessCheckInst->mValue = Remap(objectAccessCheckInst->mValue);
}

void BeInliner::Visit(BeAllocaInst* allocaInst)
{
	auto destAllocInst = AllocInst(allocaInst);
	destAllocInst->mType = allocaInst->mType;
	destAllocInst->mArraySize = Remap(allocaInst->mArraySize);
	destAllocInst->mAlign = allocaInst->mAlign;
	destAllocInst->mNoChkStk = allocaInst->mNoChkStk;
	destAllocInst->mForceMem = allocaInst->mForceMem;
}

void BeInliner::Visit(BeAliasValueInst* aliasValueInst)
{
	auto destlifetimeStartInst = AllocInst(aliasValueInst);
	destlifetimeStartInst->mPtr = Remap(aliasValueInst->mPtr);
}

void BeInliner::Visit(BeLifetimeExtendInst* lifetimeExtendInst)
{
	auto destlifetimeExtendInst = AllocInst(lifetimeExtendInst);
	destlifetimeExtendInst->mPtr = Remap(lifetimeExtendInst->mPtr);	
}

void BeInliner::Visit(BeLifetimeStartInst* lifetimeStartInst)
{
	auto destlifetimeStartInst = AllocInst(lifetimeStartInst);
	destlifetimeStartInst->mPtr = Remap(lifetimeStartInst->mPtr);	
}

void BeInliner::Visit(BeLifetimeEndInst* lifetimeEndInst)
{
	auto destlifetimeEndInst = AllocInst(lifetimeEndInst);
	destlifetimeEndInst->mPtr = Remap(lifetimeEndInst->mPtr);	
}

void BeInliner::Visit(BeLifetimeFenceInst* lifetimeFenceInst)
{
	auto destlifetimeFenceInst = AllocInst(lifetimeFenceInst);
	destlifetimeFenceInst->mFenceBlock = (BeBlock*)Remap(lifetimeFenceInst->mFenceBlock);
	destlifetimeFenceInst->mPtr = Remap(lifetimeFenceInst->mPtr);	
}

void BeInliner::Visit(BeValueScopeStartInst* valueScopeStartInst)
{
	auto destValueScopeStartInst = AllocInst(valueScopeStartInst);
}

void BeInliner::Visit(BeValueScopeRetainInst* valueScopeRetainInst)
{
	auto destValueScopeRetainInst = AllocInst(valueScopeRetainInst);
	destValueScopeRetainInst->mValue = Remap(valueScopeRetainInst->mValue);
}

void BeInliner::Visit(BeValueScopeEndInst* valueScopeEndInst)
{
	auto destValueScopeEndInst = AllocInst(valueScopeEndInst);
	destValueScopeEndInst->mScopeStart = (BeValueScopeStartInst*)Remap(valueScopeEndInst->mScopeStart);
	destValueScopeEndInst->mIsSoft = valueScopeEndInst->mIsSoft;
}

void BeInliner::Visit(BeLoadInst* loadInst)
{
	auto destLoadInst = AllocInst(loadInst);
	destLoadInst->mTarget = Remap(loadInst->mTarget);	
}

void BeInliner::Visit(BeStoreInst* storeInst)
{
	auto destStoreInst = AllocInst(storeInst);
	destStoreInst->mPtr = Remap(storeInst->mPtr);
	destStoreInst->mVal = Remap(storeInst->mVal);	
}

void BeInliner::Visit(BeSetCanMergeInst* setCanMergeInst)
{
	auto destSetCanMergeInst = AllocInst(setCanMergeInst);
	destSetCanMergeInst->mVal = Remap(setCanMergeInst->mVal);
}

void BeInliner::Visit(BeMemSetInst* memSetInst)
{
	auto destMemSetInst = AllocInst(memSetInst);
	destMemSetInst->mAddr = Remap(memSetInst->mAddr);
	destMemSetInst->mVal = Remap(memSetInst->mVal);
	destMemSetInst->mSize = Remap(memSetInst->mSize);
	destMemSetInst->mAlignment = memSetInst->mAlignment;
}

void BeInliner::Visit(BeGEPInst* gepInst)
{
	auto destGEPInst = AllocInst(gepInst);
	destGEPInst->mPtr = Remap(gepInst->mPtr);
	destGEPInst->mIdx0 = Remap(gepInst->mIdx0);
	destGEPInst->mIdx1 = Remap(gepInst->mIdx1);
}

void BeInliner::Visit(BeBrInst* brInst)
{
	auto destBrInst = AllocInst(brInst);
	destBrInst->mTargetBlock = (BeBlock*)Remap(brInst->mTargetBlock);
	destBrInst->mNoCollapse = brInst->mNoCollapse;
	destBrInst->mIsFake = brInst->mIsFake;
}

void BeInliner::Visit(BeCondBrInst* condBrInst)
{
	auto destCondBrInst = AllocInst(condBrInst);
	destCondBrInst->mCond = Remap(condBrInst->mCond);
	destCondBrInst->mTrueBlock = (BeBlock*)Remap(condBrInst->mTrueBlock);
	destCondBrInst->mFalseBlock = (BeBlock*)Remap(condBrInst->mFalseBlock);	
}

void BeInliner::Visit(BePhiIncoming* phiIncomingInst)
{
	
}

void BeInliner::Visit(BePhiInst* phiInst)
{	
	auto destPhiInst = AllocInst(phiInst);
	for (auto incoming : phiInst->mIncoming)
	{
		auto destPhiIncoming = mAlloc->Alloc<BePhiIncoming>();
		destPhiIncoming->mValue = Remap(incoming->mValue);		
		destPhiIncoming->mBlock = (BeBlock*)Remap(incoming->mBlock);
		destPhiInst->mIncoming.push_back(destPhiIncoming);
	}

	
	destPhiInst->mType = phiInst->mType;
}

void BeInliner::Visit(BeSwitchInst* switchInst)
{
	auto destSwitchInst = AllocInstOwned(switchInst);
	destSwitchInst->mValue = Remap(switchInst->mValue);
	destSwitchInst->mDefaultBlock = (BeBlock*)Remap(switchInst->mDefaultBlock);
	for (auto& switchCase : switchInst->mCases)
	{
		BeSwitchCase destSwitchCase;
		destSwitchCase.mBlock = (BeBlock*)Remap(switchCase.mBlock);
		destSwitchCase.mValue = switchCase.mValue;
		destSwitchInst->mCases.push_back(destSwitchCase);
	}
}

void BeInliner::Visit(BeRetInst* retInst)
{
	auto destRetInst = AllocInst(retInst);
	destRetInst->mRetValue = Remap(retInst->mRetValue);
}

void BeInliner::Visit(BeCallInst* callInst)
{
	auto destCallInst = AllocInstOwned(callInst);
	destCallInst->mFunc = Remap(callInst->mFunc);
	for (auto& arg : callInst->mArgs)
	{
		auto copiedArg = arg;
		copiedArg.mValue = Remap(arg.mValue);
		destCallInst->mArgs.push_back(copiedArg);
	}
	destCallInst->mNoReturn = callInst->mNoReturn;
	destCallInst->mTailCall = callInst->mTailCall;	
}

void BeInliner::Visit(BeDbgDeclareInst* dbgDeclareInst)
{
	auto destDbgDeclareInst = AllocInst(dbgDeclareInst);
	destDbgDeclareInst->mDbgVar = (BeDbgVariable*)Remap(dbgDeclareInst->mDbgVar);
	destDbgDeclareInst->mValue = Remap(dbgDeclareInst->mValue);
}

//////////////////////////////////////////////////////////////////////////

BeType* BeConstant::GetType()
{
	return mType;
}

void BeConstant::GetData(BeConstData& data)
{
	auto type = GetType();
	while ((((int)data.mData.size()) % type->mAlign) != 0)
		data.mData.push_back(0);

	if (type->IsComposite())
	{
		for (int i = 0; i < type->mSize; i++)
			data.mData.push_back(0); // Aggregate
	}
	else if (type->mTypeCode == BeTypeCode_Float)
	{
		float f = mDouble;
		data.mData.Insert(data.mData.mSize, (uint8*)&f, sizeof(float));
	}
	else
	{		
		data.mData.Insert(data.mData.mSize, &mUInt8, type->mSize);
	}
}

void BeConstant::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	mType->HashReference(hashCtx);
	if (mType->mTypeCode < BeTypeCode_Struct)
	{		
		hashCtx.Mixin(mUInt64);
	}
	else if (mType->IsPointer())
	{
		if (mTarget != NULL)
			mTarget->HashReference(hashCtx);
		else
			hashCtx.Mixin(-1);
	}
	else if (mType->IsComposite())
	{
		// Zero-init
		BF_ASSERT(mTarget == NULL);
	}
	else
		BF_FATAL("NotImpl");
}

void BeStructConstant::GetData(BeConstData& data)
{
	for (auto val : mMemberValues)
		val->GetData(data);
}

BeType* BeGEPConstant::GetType()
{	
	BePointerType* ptrType = (BePointerType*)mTarget->GetType();
	BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
	if (ptrType->mElementType->mTypeCode == BeTypeCode_SizedArray)
	{
		BeSizedArrayType* arrayType = (BeSizedArrayType*)ptrType->mElementType;
		BF_ASSERT(arrayType->mTypeCode == BeTypeCode_SizedArray);
		return arrayType->mContext->GetPointerTo(arrayType->mElementType);
	}
	else if (ptrType->mElementType->mTypeCode == BeTypeCode_Vector)
	{
		BeVectorType* arrayType = (BeVectorType*)ptrType->mElementType;
		BF_ASSERT(arrayType->mTypeCode == BeTypeCode_Vector);
		return arrayType->mContext->GetPointerTo(arrayType->mElementType);
	}
	/*else if (ptrType->mElementType->IsPointer())
	{
		return ptrType->mElementType;
	}*/
	else
	{
		BeStructType* structType = (BeStructType*)ptrType->mElementType;
		BF_ASSERT(structType->mTypeCode == BeTypeCode_Struct);
		return structType->mContext->GetPointerTo(structType->mMembers[mIdx1].mType);
	}
}

BeType* BeExtractValueConstant::GetType()
{
	BeType* type = mTarget->GetType();	
	if (type->mTypeCode == BeTypeCode_SizedArray)
	{
		BeSizedArrayType* arrayType = (BeSizedArrayType*)type;
		BF_ASSERT(arrayType->mTypeCode == BeTypeCode_SizedArray);
		return arrayType->mElementType;
	}
	else if (type->mTypeCode == BeTypeCode_Vector)
	{
		BeVectorType* arrayType = (BeVectorType*)type;
		BF_ASSERT(arrayType->mTypeCode == BeTypeCode_Vector);
		return arrayType->mElementType;
	}
	/*else if (ptrType->mElementType->IsPointer())
	{
		return ptrType->mElementType;
	}*/
	else
	{
		BeStructType* structType = (BeStructType*)type;
		BF_ASSERT(structType->mTypeCode == BeTypeCode_Struct);
		return structType->mMembers[mIdx0].mType;
	}
}

BeType* BeGlobalVariable::GetType()
{
	//if (mIsConstant)
		//return mType;
	return mModule->mContext->GetPointerTo(mType);
}

void BeFunction::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	hashCtx.MixinStr(mName);
	hashCtx.Mixin(mLinkageType);
	hashCtx.Mixin(mAlwaysInline);	
	hashCtx.Mixin(mCallingConv);

	for (auto block : mBlocks)
		block->HashReference(hashCtx);

	for (auto& param : mParams)
	{
		hashCtx.MixinStr(param.mName);
		hashCtx.Mixin(param.mNoAlias);
		hashCtx.Mixin(param.mNoCapture);
		hashCtx.Mixin(param.mStructRet);
		hashCtx.Mixin(param.mZExt);
		hashCtx.Mixin(param.mDereferenceableSize);
		hashCtx.Mixin(param.mByValSize);
	}
	if (mDbgFunction != NULL)
		mDbgFunction->HashReference(hashCtx);
	if (mRemapBindVar != NULL)
		mRemapBindVar->HashReference(hashCtx);
}

void BeBlock::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	hashCtx.MixinStr(mName);
	hashCtx.Mixin(mInstructions.size());
	for (auto inst : mInstructions)
		inst->HashReference(hashCtx);
}

void BeInst::HashContent(BeHashContext& hashCtx)
{
	HashInst(hashCtx);
	if (mName != NULL)
		hashCtx.MixinStr(mName);
	if (mDbgLoc != NULL)
		mDbgLoc->HashReference(hashCtx);
}

BeType* BeNumericCastInst::GetType()
{
	return mToType;
}

BeType* BeBitCastInst::GetType()
{
	return mToType;
}

BeType* BeNegInst::GetType()
{
	return mValue->GetType();
}

BeType* BeNotInst::GetType()
{
	return mValue->GetType();
}

BeType* BeBinaryOpInst::GetType()
{
	return mLHS->GetType();
}

BeContext* BeInst::GetContext()
{
	return mParentBlock->mFunction->mModule->mContext;
}

BeModule* BeInst::GetModule()
{
	return mParentBlock->mFunction->mModule;
}

void BeInst::SetName(const StringImpl& name)
{	
	char* nameStr = (char*)GetModule()->mAlloc.AllocBytes((int)name.length() + 1);
	strcpy(nameStr, name.c_str());
	mName = nameStr;	
}

BeType* BeLoadInst::GetType()
{
	auto type = mTarget->GetType();
	BF_ASSERT(type->mTypeCode == BeTypeCode_Pointer);
	BePointerType* pointerType = (BePointerType*)type;
	return pointerType->mElementType;
}

BeType* BeUndefValueInst::GetType()
{
	return mType;
}

BeType* BeExtractValueInst::GetType()
{
	auto aggType = mAggVal->GetType();
	if (aggType->mTypeCode == BeTypeCode_SizedArray)
	{
		BeSizedArrayType* arrayType = (BeSizedArrayType*)aggType;
		return arrayType->mElementType;
	}
	if (aggType->mTypeCode == BeTypeCode_Vector)
	{
		BeVectorType* arrayType = (BeVectorType*)aggType;
		return arrayType->mElementType;
	}
	BF_ASSERT(aggType->mTypeCode == BeTypeCode_Struct);
	BeStructType* structType = (BeStructType*)aggType;
	return structType->mMembers[mIdx].mType;	
}

BeType* BeInsertValueInst::GetType()
{
	return mAggVal->GetType();
}

BeType* BeCmpInst::GetType()
{
	return GetContext()->GetPrimitiveType(BeTypeCode_Boolean);
}

BeType* BeAllocaInst::GetType()
{
	auto context = GetContext();
	return context->GetPointerTo(mType);
}

BeType * BeValueScopeStartInst::GetType()
{
	auto context = GetContext();
	return context->GetPrimitiveType(BeTypeCode_Int32);
}


BeType* BeGEPInst::GetType()
{
	if (mIdx1 == NULL)		
		return mPtr->GetType();

	BePointerType* ptrType = (BePointerType*)mPtr->GetType();
	BF_ASSERT(ptrType->mTypeCode == BeTypeCode_Pointer);
	auto elementType = ptrType->mElementType;
	if (elementType->IsStruct())
	{
		BeStructType* structType = (BeStructType*)ptrType->mElementType;
		BF_ASSERT(structType->mTypeCode == BeTypeCode_Struct);
		auto constIdx1 = BeValueDynCast<BeConstant>(mIdx1);
		return GetContext()->GetPointerTo(structType->mMembers[constIdx1->mInt64].mType);
	}
	else if (elementType->IsSizedArray())
	{
		BeSizedArrayType* arrayType = (BeSizedArrayType*)ptrType->mElementType;
		return GetContext()->GetPointerTo(arrayType->mElementType);
	}
	else if (elementType->IsVector())
	{
		BeVectorType* arrayType = (BeVectorType*)ptrType->mElementType;
		return GetContext()->GetPointerTo(arrayType->mElementType);
	}
	else
	{
		BF_FATAL("Bad type");
		return NULL;
	}
}

bool BeBlock::IsEmpty()
{
	return mInstructions.size() == 0;
}

BeType* BeCallInst::GetType()
{
	if (mInlineResult != NULL)
		return mInlineResult->GetType();

	auto type = mFunc->GetType();
	if (type == NULL)
	{
		if (auto intrin = BeValueDynCast<BeIntrinsic>(mFunc))		
			return intrin->mReturnType;		
		return type;
	}
	while (type->mTypeCode == BeTypeCode_Pointer)
		type = ((BePointerType*)type)->mElementType;
	auto funcType = (BeFunctionType*)(type);
	if (funcType == NULL)
		return NULL;
	BF_ASSERT(funcType->mTypeCode == BeTypeCode_Function);
	return funcType->mReturnType;
}

BeType* BePhiInst::GetType()
{
	return mType;
}

BeType* BeArgument::GetType()
{
	return mModule->mActiveFunction->GetFuncType()->mParams[mArgIdx].mType;
}

void BeDbgDeclareInst::HashInst(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	mDbgVar->HashReference(hashCtx);
	mValue->HashReference(hashCtx);
	hashCtx.Mixin(mIsValue);
}

void BeDbgStructType::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	hashCtx.MixinStr(mName);
	if (mDerivedFrom != NULL)
		mDerivedFrom->HashReference(hashCtx);
	for (auto member : mMembers)
		member->HashReference(hashCtx);
	for (auto method : mMethods)
		method->HashReference(hashCtx);
	hashCtx.Mixin(mIsFullyDefined);
	hashCtx.Mixin(mIsStatic);
	mDefFile->HashReference(hashCtx);
	hashCtx.Mixin(mDefLine);
}

//////////////////////////////////////////////////////////////////////////

void BeDbgLexicalBlock::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	mFile->HashReference(hashCtx);
	mScope->HashReference(hashCtx);
}

//////////////////////////////////////////////////////////////////////////

BeDbgFunction* BeDbgLoc::GetDbgFunc()
{
	auto checkScope = mDbgScope;
	while (checkScope != NULL)
	{
		if (auto inlinedScope = BeValueDynCast<BeDbgInlinedScope>(checkScope))
		{
			checkScope = inlinedScope->mScope;
		}
		if (auto dbgFunc = BeValueDynCast<BeDbgFunction>(checkScope))
		{
			return dbgFunc;
		}
		else if (auto dbgLexBlock = BeValueDynCast<BeDbgLexicalBlock>(checkScope))
		{
			checkScope = dbgLexBlock->mScope;
		}
		else
			return NULL;
	}
	return NULL;
}

BeDbgFile* BeDbgLoc::GetDbgFile()
{
	auto checkScope = mDbgScope;
	while (checkScope != NULL)
	{
		if (auto inlinedScope = BeValueDynCast<BeDbgInlinedScope>(checkScope))
		{
			checkScope = inlinedScope->mScope;
		}
		if (auto dbgFile = BeValueDynCast<BeDbgFile>(checkScope))
		{
			return dbgFile;
		}
		else if (auto dbgStruct = BeValueDynCast<BeDbgStructType>(checkScope))
		{
			checkScope = dbgStruct->mScope;
		}
		else if (auto dbgEnum = BeValueDynCast<BeDbgEnumType>(checkScope))
		{
			checkScope = dbgEnum->mScope;
		}
		else if (auto dbgNamespace = BeValueDynCast<BeDbgNamespace>(checkScope))
		{
			checkScope = dbgNamespace->mScope;
		}
		else if (auto dbgFunc = BeValueDynCast<BeDbgFunction>(checkScope))
		{
			return dbgFunc->mFile;			
		}
		else if (auto dbgLexBlock = BeValueDynCast<BeDbgLexicalBlock>(checkScope))
		{
			return dbgLexBlock->mFile;
		}
		else
			return NULL;
	}
	return NULL;
}

BeDbgLoc* BeDbgLoc::GetInlinedAt(int idx)
{
	if (idx == -1)
		return this;
	auto checkDbgLoc = mDbgInlinedAt;
	for (int i = 0; i < idx; i++)
		checkDbgLoc = checkDbgLoc->mDbgInlinedAt;
	return checkDbgLoc;
}

BeDbgLoc* BeDbgLoc::GetRoot()
{	
	auto checkDbgLoc = this;
	while (checkDbgLoc->mDbgInlinedAt != NULL)	
		checkDbgLoc = checkDbgLoc->mDbgInlinedAt;
	return checkDbgLoc;
}

int BeDbgLoc::GetInlineDepth()
{
	int inlineDepth = 0;
	auto checkDbgLoc = mDbgInlinedAt;
	while (checkDbgLoc != NULL)
	{
		inlineDepth++;
		checkDbgLoc = checkDbgLoc->mDbgInlinedAt;
	}
	return inlineDepth;
}

int BeDbgLoc::GetInlineMatchDepth(BeDbgLoc* other)
{
	int inlineDepth = GetInlineDepth();
	int otherInlineDepth = other->GetInlineDepth();

	int matchDepth = 0;
	while (true)
	{
		if ((matchDepth >= inlineDepth) || (matchDepth >= otherInlineDepth))
			break;
		int inlineIdx = inlineDepth - matchDepth - 1;
		int otherInlineIdx = otherInlineDepth - matchDepth - 1;
		auto inlinedAt = GetInlinedAt(inlineIdx);
		auto otherInlinedAt = other->GetInlinedAt(otherInlineIdx);
		if (inlinedAt != otherInlinedAt)
			break;
		if ((otherInlineIdx == 0) || (inlineIdx == 0))
		{
			// At the current scope, make sure we're refererring to the same method...
			auto funcScope = GetInlinedAt(inlineIdx - 1);
			auto otherFuncScope = other->GetInlinedAt(otherInlineIdx - 1);

			auto dbgFunc = funcScope->GetDbgFunc();
			auto otherDbgFunc = otherFuncScope->GetDbgFunc();
			if (dbgFunc != otherDbgFunc)
			{
				// Same invocation position but different method...
				break;
			}
		}
		matchDepth++;
	}

	/*int matchDepth = 0;
	while (true)
	{
		if ((matchDepth >= inlineDepth) || (matchDepth >= otherInlineDepth))
			break;
		if (GetInlinedAt(inlineDepth - matchDepth - 1) != other->GetInlinedAt(otherInlineDepth - matchDepth - 1))
			break;
		matchDepth++;
	}*/

	return matchDepth;
}

void BeDbgFunction::HashContent(BeHashContext& hashCtx)
{
	hashCtx.Mixin(TypeId);
	if (mFile != NULL)
		mFile->HashReference(hashCtx);
	hashCtx.Mixin(mLine);
	hashCtx.MixinStr(mName);
	hashCtx.MixinStr(mLinkageName);
	mType->HashReference(hashCtx);
	for (auto genericArg : mGenericArgs)
		genericArg->HashReference(hashCtx);
	for (auto genericConstValueArgs : mGenericArgs)
		genericConstValueArgs->HashReference(hashCtx);
	if (mValue != NULL)
		mValue->HashReference(hashCtx);
	hashCtx.Mixin(mIsLocalToUnit);
	hashCtx.Mixin(mIsStaticMethod);
	hashCtx.Mixin(mFlags);
	hashCtx.Mixin(mVK);
	hashCtx.Mixin(mVIndex);
	hashCtx.Mixin(mVariables.size());
	for (auto& variable : mVariables)
	{
		if (variable == NULL)
			hashCtx.Mixin(-1);
		else
			variable->HashReference(hashCtx);
	}
	hashCtx.Mixin(mPrologSize);
	hashCtx.Mixin(mCodeLen);
}

void BeDbgStructType::SetMembers(SizedArrayImpl<BeMDNode*>& members)
{
	mIsFullyDefined = true;

	BF_ASSERT(mMembers.size() == 0);
	for (auto member : members)
	{
		if (auto inheritance = BeValueDynCast<BeDbgInheritance>(member))
		{
			BF_ASSERT(mDerivedFrom == NULL);
			mDerivedFrom = inheritance->mBaseType;
		}
		else if (auto structMember = BeValueDynCast<BeDbgStructMember>(member))
		{
			mMembers.push_back(structMember);
		}
		else if (auto dbgMethod = BeValueDynCast<BeDbgFunction>(member))
		{
			dbgMethod->mIncludedAsMember = true;
			mMethods.push_back(dbgMethod);
		}
		else
			BF_FATAL("bad");
	}
}

void BeDbgEnumType::SetMembers(SizedArrayImpl<BeMDNode*>& members)
{
	mIsFullyDefined = true;

	BF_ASSERT(mMembers.size() == 0);
	for (auto member : members)
	{
		if (auto enumMember = BeValueDynCast<BeDbgEnumMember>(member))
		{
			mMembers.push_back(enumMember);
		}		
		else
			BF_FATAL("bad");
	}
}

//////////////////////////////////////////////////////////////////////////

void BeDumpContext::ToString(StringImpl& str, BeValue* value, bool showType, bool mdDrillDown)
{
	if (value == NULL)
	{
		str += "<null>";
		return;
	}

	if (auto mdNode = BeValueDynCast<BeMDNode>(value))
	{
		if (auto dbgInlinedScope = BeValueDynCast<BeDbgInlinedScope>(mdNode))
		{
			str += "Inlined:";
			ToString(str, dbgInlinedScope->mScope);
			return;
		}

		if (auto dbgVar = BeValueDynCast<BeDbgVariable>(mdNode))
		{
			ToString(str, dbgVar->mType);
			str += " ";
			str += dbgVar->mName;
			return;
		}

		if (auto dbgVar = BeValueDynCast<BeDbgVariable>(mdNode))
		{
			ToString(str, dbgVar->mType);
			str += " ";
			str += dbgVar->mName;
			return;
		}

		if (auto dbgFile = BeValueDynCast<BeDbgFile>(mdNode))
		{
			str += dbgFile->mFileName;
			return;
		}

		if (auto dbgFunc = BeValueDynCast<BeDbgFunction>(mdNode))
		{			
			if (auto parentType = BeValueDynCast<BeDbgType>(dbgFunc->mScope))
			{
				ToString(str, dbgFunc->mScope);
				str += ".";
			}
			else if (auto dbgNamespace = BeValueDynCast<BeDbgNamespace>(dbgFunc->mScope))
			{
				ToString(str, dbgNamespace->mScope);
				str += ".";
			}
			str += dbgFunc->mName;

			if (mdDrillDown)
			{				
				str += ":";
				ToString(str, dbgFunc->mFile, true, true);
			}

			return;
		}

		if (auto lexBlock = BeValueDynCast<BeDbgLexicalBlock>(mdNode))
		{
			str += "{";
			str += StrFormat("%d@", lexBlock->mId);
			ToString(str, lexBlock->mFile);
			str += ":";
			ToString(str, lexBlock->mScope);
			return;
		}
		
		if (auto dbgType = BeValueDynCast<BeDbgBasicType>(mdNode))
		{			
			if (dbgType->mEncoding == llvm::dwarf::DW_ATE_address)
			{
				if (dbgType->mSize == 0)
					str += "void";
				else
					str += "addr";
				return;
			}
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_signed)
			{
				str += "int";
			}
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_unsigned)
			{
				str += "uint";
			}
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_float)
			{
				if (dbgType->mSize == 4)
					str += "float";
				else
					str += "double";
				return;
			}
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_unsigned_char)
				str += "uchar";
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_signed_char)
				str += "char";
			else if (dbgType->mEncoding == llvm::dwarf::DW_ATE_boolean)
			{
				if (dbgType->mSize == 1)
				{
					str += "bool";
					return;
				}
				str += "bool";
			}
			else
				str += "???";
			str += StrFormat("%d", dbgType->mSize * 8);
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgPointerType>(mdNode))
		{
			ToString(str, dbgType->mElement);
			str += "*";
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgReferenceType>(mdNode))
		{
			ToString(str, dbgType->mElement);
			str += "&";
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgConstType>(mdNode))
		{
			str += "const ";
			ToString(str, dbgType->mElement);
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgArtificialType>(mdNode))
		{
			str += "artificial ";
			ToString(str, dbgType->mElement);
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgNamespace>(mdNode))
		{			
			if ((BeValueDynCast<BeDbgStructType>(dbgType->mScope) != NULL) ||
				(BeValueDynCast<BeDbgNamespace>(dbgType->mScope) != NULL))
			{
				ToString(str, dbgType->mScope);
				str += ".";
				str += dbgType->mName;
				return;
			}
			else
				str += dbgType->mName;
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgStructType>(mdNode))
		{			
			if ((BeValueDynCast<BeDbgStructType>(dbgType->mScope) != NULL) ||
				(BeValueDynCast<BeDbgNamespace>(dbgType->mScope) != NULL))
			{
				ToString(str, dbgType->mScope);
				str += ".";
				str += dbgType->mName;
				return;
			}
			else
				str += dbgType->mName;
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgEnumType>(mdNode))
		{			
			if ((BeValueDynCast<BeDbgStructType>(dbgType->mScope) != NULL) ||
				(BeValueDynCast<BeDbgNamespace>(dbgType->mScope) != NULL))
			{
				ToString(str, dbgType->mScope);
				str += ".";
				str += dbgType->mName;
				return;
			}
			else
				str += dbgType->mName;
			return;
		}

		if (auto dbgType = BeValueDynCast<BeDbgArrayType>(mdNode))
		{			
			ToString(str, dbgType->mElement);
			str += "[";
			str += StrFormat("%d", dbgType->mNumElements);
			str += "]";
			return;
		}

		if (auto dbgLoc = BeValueDynCast<BeDbgLoc>(mdNode))
		{
			str += StrFormat("@%d ", dbgLoc->mIdx);
			ToString(str, dbgLoc->mDbgScope, true, true);
			str += StrFormat(":%d:%d", dbgLoc->mLine + 1, dbgLoc->mColumn + 1);

			if (dbgLoc->mDbgInlinedAt)
			{
				str += " inlined at ";
				ToString(str, dbgLoc->mDbgInlinedAt, true, true);
			}
			return;
		}

		if (auto inheritance = BeValueDynCast<BeDbgInheritance>(mdNode))
		{
			str += "inherit ";
			ToString(str, inheritance->mBaseType);
			return;
		}

		if (auto dbgMember = BeValueDynCast<BeDbgStructMember>(mdNode))
		{			
			if (dbgMember->mIsStatic)
				str += "static ";
			ToString(str, dbgMember->mType);
			str += " ";
			str += dbgMember->mName;
			if (!dbgMember->mIsStatic)
			{
				str += " offset:";
				str += StrFormat("%d", dbgMember->mOffset);
			}
			
			if (dbgMember->mStaticValue != NULL)
				str += " " + ToString(dbgMember->mStaticValue);
			return;
		}

		if (auto dbgMember = BeValueDynCast<BeDbgEnumMember>(mdNode))
		{			
			str += dbgMember->mName;
			str += " ";
			str += StrFormat("%lld", dbgMember->mValue);
			return;
		}

		str += "?MDNode?";
		return;
	}

	if (auto globalVar = BeValueDynCast<BeGlobalVariable>(value))
	{
		ToString(str, globalVar->GetType());
		str += " ";
		str += globalVar->mName;
		return;
	}

	if (auto constantGEP = BeValueDynCast<BeGEPConstant>(value))
	{	
		str += "ConstGep ";
		ToString(str, constantGEP->mTarget);
		str += StrFormat(" %d %d", constantGEP->mIdx0, constantGEP->mIdx1);
		return;
	}
	
	if (auto constantExtract = BeValueDynCast<BeExtractValueConstant>(value))
	{
		str += "ConstExtract ";
		ToString(str, constantExtract->mTarget);
		str += StrFormat(" %d", constantExtract->mIdx0);
		return;
	}

	if (auto arg = BeValueDynCast<BeArgument>(value))
	{
		auto activeFunction = arg->mModule->mActiveFunction;
		auto& typeParam = activeFunction->GetFuncType()->mParams[arg->mArgIdx];
		auto& param = activeFunction->mParams[arg->mArgIdx];
		if (showType)
		{
			BeModule::ToString(str, typeParam.mType);
			str += " %";
			str += param.mName;
		}
		else
		{
			str += "%";
			str += param.mName;
		}
		
		return;
	}
	
	if (auto func = BeValueDynCast<BeFunction>(value))
	{
		str += func->mName;
		return;
	}

	if (auto constant = BeValueDynCast<BeStructConstant>(value))
	{		
		if (showType)
		{
			BeModule::ToString(str, constant->mType);
			str += " ";
		}

		str += "(";

		switch (constant->mType->mTypeCode)
		{
		case BeTypeCode_Struct:			
		case BeTypeCode_SizedArray:
		case BeTypeCode_Vector:
			for (int valIdx = 0; valIdx < (int)constant->mMemberValues.size(); valIdx++)
			{
				if (valIdx > 0)
					str += ", ";
				ToString(str, constant->mMemberValues[valIdx], false);
			}			
			break;
		default:
			BF_FATAL("NotImpl");
			break;
		}

		str += ")";
		return;
	}

	if (auto constant = BeValueDynCast<BeUndefConstant>(value))
	{
		if (showType)
		{
			BeModule::ToString(str, constant->mType);
			str += " ";
		}
		str += "undef";
	}

	if (auto constant = BeValueDynCast<BeCastConstant>(value))
	{ 		
		ToString(str, constant->mType);
		str += " cast ";
		ToString(str, constant->mTarget);
		return;
	}

	if (auto constant = BeValueDynCast<BeGEPConstant>(value))
	{		
		ToString(str, constant->GetType());
		str += " gep (";
		ToString(str, constant->mTarget);
		str += StrFormat(", %d, %d)", constant->mIdx0, constant->mIdx1);
		return;
	}

	if (auto constant = BeValueDynCast<BeExtractValueConstant>(value))
	{
		ToString(str, constant->GetType());
		str += " extract (";
		ToString(str, constant->mTarget);
		str += StrFormat(", %d)", constant->mIdx0);
		return;
	}

	if (auto constant = BeValueDynCast<BeStructConstant>(value))
	{		
		ToString(str, constant->GetType());
		str += " (";
		for (int i = 0; i < constant->mMemberValues.size(); i++)
		{
			if (i > 0)
				str += ", ";
			ToString(str, constant->mMemberValues[i]);
		}
		str += ")";
		return;
	}

	if (auto constant = BeValueDynCast<BeStringConstant>(value))
	{		
		ToString(str, constant->GetType());
		str += " \"";
		str += SlashString(constant->mString, true, true);
		str += "\"";
		return;
	}

	if (auto constant = BeValueDynCast<BeConstant>(value))
	{
		BeModule::ToString(str, constant->mType);
		str += " ";

		switch (constant->mType->mTypeCode)
		{
		case BeTypeCode_None:
			return;
		case BeTypeCode_NullPtr:
			return;
		case BeTypeCode_Boolean:
			str += constant->mBool ? "true" : "false";
			return;
		case BeTypeCode_Int8:
		case BeTypeCode_Int16:
		case BeTypeCode_Int32:
		case BeTypeCode_Int64:
			str += StrFormat("%lld", constant->mInt64);
			return;
		case BeTypeCode_Float:
		case BeTypeCode_Double:
			str += StrFormat("%f", constant->mDouble);
			return;
		case BeTypeCode_Pointer:
			if (constant->mTarget == NULL)
			{
				str += "null";
				return;
			}
			else
			{
				str += "(";
				ToString(str, constant->mTarget);
				str += ")";
				return;
			}
		case BeTypeCode_Struct:
		case BeTypeCode_SizedArray:
		case BeTypeCode_Vector:
			str += "zeroinitializer";		
			return;
		case BeTypeCode_Function:
			BF_FATAL("Notimpl");
			str += "<InvalidConstant>";
			return;
		default:
			BF_FATAL("NotImpl");
		}
	}

	if (auto intrin = BeValueDynCast<BeIntrinsic>(value))
	{
		str += "intrin:";
		str += BfIRCodeGen::GetIntrinsicName((int)intrin->mKind);;
		return;
	}

	if (auto callInst = BeValueDynCast<BeCallInst>(value))
	{
		if (callInst->mInlineResult != NULL)
		{
			str += "InlineResult: ";
			ToString(str, callInst->mInlineResult);
			return;
		}
	}	


	BeType* resultType = NULL;	
	const char* wantNamePtr = NULL;
	if (auto instVal = BeValueDynCast<BeInst>(value))
	{
	 	resultType = instVal->GetType();
	 	if ((instVal->mName != NULL) && (instVal->mName[0] != 0))
	 		wantNamePtr = instVal->mName;
	}
	 
	String* valueNamePtr = NULL;
	if (mValueNameMap.TryGetValue(value, &valueNamePtr))
	{
		if (resultType != NULL)
		{
			BeModule::ToString(str, resultType);
			str += " %";
		}
		else
			str += "%";
		str += *valueNamePtr;
		return;
	}
	 
	if (auto beBlock = BeValueDynCast<BeBlock>(value))
	{
		if (!beBlock->mName.IsEmpty())
			wantNamePtr = beBlock->mName.c_str();
	}
	 
	StringT<64> useName;
	if (wantNamePtr != NULL)
		useName += wantNamePtr;		
	while (true)
	{
		int* idxPtr = NULL;
		if ((mSeenNames.TryAdd(useName, NULL, &idxPtr)) && (!useName.IsEmpty()))
			break;

		int checkIdx = (*idxPtr)++;
	 			
	 	char str[32];
	 	sprintf(str, "%d", checkIdx);		
	 	useName += str;
	}

	mValueNameMap[value] = useName;	
	if ((showType) && (resultType != NULL))
	{
		BeModule::ToString(str, resultType);
		str += " %";
		str += useName;
		return;
	}
	else
	{
		str += "%";
		str += useName;
		return;
	}
}

String BeDumpContext::ToString(BeValue* value, bool showType, bool mdDrillDown)
{
	String str;
	ToString(str, value, showType, mdDrillDown);
	return str;
}

String BeDumpContext::ToString(BeType* type)
{
	String str;
	BeModule::ToString(str, type);
	return str;
}

void BeDumpContext::ToString(StringImpl& str, BeType* type)
{
	BeModule::ToString(str, type);
}

void BeDumpContext::ToString(StringImpl& str, BeDbgFunction* dbgFunction, bool showScope)
{	
	if (dbgFunction->mIsStaticMethod)
		str += "static ";
	if (dbgFunction->mIsLocalToUnit)
		str += "internal ";
	if (dbgFunction->mValue == NULL)
		str += "external ";
	if (dbgFunction->mVK > 0)
		str += StrFormat("virtual(%d) ", dbgFunction->mVIndex);

	str += ToString(dbgFunction->mType->mReturnType);
	str += " ";	

	if ((showScope) && (dbgFunction->mScope != NULL))
	{
		if (auto parentType = BeValueDynCast<BeDbgType>(dbgFunction->mScope))
		{
			ToString(str, parentType);
			str += ".";
		}
	}

	bool needsQuote = false;
	for (char c : dbgFunction->mName)
	{
		if ((c == '.') || (c == '<') || (c == '('))
			needsQuote = true;
	}
	if (needsQuote)
		str += "\"";
	str += dbgFunction->mName;
	if (needsQuote)
		str += "\"";
	
	if (!dbgFunction->mGenericArgs.IsEmpty())
	{
		str += "<";
		for (int genericIdx = 0; genericIdx < dbgFunction->mGenericArgs.Count(); genericIdx++)
		{
			if (genericIdx > 0)
				str += ", ";
			str += ToString(dbgFunction->mGenericArgs[genericIdx]);
		}
		str += ">";
	}
	if (!dbgFunction->mGenericConstValueArgs.IsEmpty())
	{
		str += "<";
		for (int genericIdx = 0; genericIdx < dbgFunction->mGenericConstValueArgs.Count(); genericIdx++)
		{
			if (genericIdx > 0)
				str += ", ";
			str += ToString(dbgFunction->mGenericConstValueArgs[genericIdx]);
		}
		str += ">";
	}

	str += "(";
	for (int paramIdx = 0; paramIdx < (int)dbgFunction->mType->mParams.size(); paramIdx++)
	{
		if (paramIdx > 0)
			str += ", ";
		str += ToString(dbgFunction->mType->mParams[paramIdx]);

		BeDbgVariable* variable = NULL;
		if (paramIdx < dbgFunction->mVariables.Count())
			variable = dbgFunction->mVariables[paramIdx];
		if (variable != NULL)
		{
			if (variable->mParamNum == paramIdx)
			{
				str += " ";
				str += variable->mName;
			}
		}
	}

	str += ")";

	if (!dbgFunction->mLinkageName.IsEmpty())
	{
		str += " Link:";
		str += dbgFunction->mLinkageName;
	}	
}

String BeDumpContext::ToString(BeDbgFunction* dbgFunction)
{
	String str;
	ToString(str, dbgFunction, false);
	return str;
}

void BeDumpContext::ToString(StringImpl& str, int val)
{
	char iStr[32];
	sprintf(iStr, "%d", val);
	str += iStr;
}

String BeDumpContext::ToString(int val)
{
	return StrFormat("%d", val);
}

void BeDumpContext::ToString(StringImpl& str, BeCmpKind cmpKind)
{
	switch (cmpKind)
	{
	case BeCmpKind_SLT: str += "slt"; return;
	case BeCmpKind_ULT: str += "ult"; return;
	case BeCmpKind_SLE: str += "sle"; return;
	case BeCmpKind_ULE: str += "ule"; return;
	case BeCmpKind_EQ: str += "eq"; return;
	case BeCmpKind_NE: str += "ne"; return;
	case BeCmpKind_SGT: str += "sgt"; return;
	case BeCmpKind_UGT: str += "ugt"; return;
	case BeCmpKind_SGE: str += "sge"; return;
	case BeCmpKind_UGE: str += "uge"; return;
	default:
		str += "???";
	}		
}

String BeDumpContext::ToString(BeCmpKind cmpKind)
{
	String str;
	ToString(str, cmpKind);
	return str;
}

void BeDumpContext::ToString(StringImpl& str, BeBinaryOpKind opKind)
{
	switch (opKind)
	{	
	case BeBinaryOpKind_Add: str += "+"; return;
	case BeBinaryOpKind_Subtract: str += "-"; return;
	case BeBinaryOpKind_Multiply: str += "*"; return;
	case BeBinaryOpKind_SDivide: str += "s/"; return;
	case BeBinaryOpKind_UDivide: str += "u/"; return;
	case BeBinaryOpKind_SModulus: str += "%"; return;
	case BeBinaryOpKind_UModulus: str += "%"; return;
	case BeBinaryOpKind_BitwiseAnd: str += "&"; return;
	case BeBinaryOpKind_BitwiseOr: str += "|"; return;
	case BeBinaryOpKind_ExclusiveOr: str += "^"; return;
	case BeBinaryOpKind_LeftShift: str += "<<"; return;
	case BeBinaryOpKind_RightShift: str += ">>"; return;
	case BeBinaryOpKind_ARightShift: str += "A>>"; return;
	default:
		str += "???";
	}	
}

String BeDumpContext::ToString(BeBinaryOpKind opKind)
{
	String str;
	ToString(str, opKind);
	return str;
}

//////////////////////////////////////////////////////////////////////////

void BeDbgFile::ToString(String& str)
{
	str += mDirectory;
	if (str.length() > 0)
	{
		if ((str[str.length() - 1] != '\\') && (str[str.length() - 1] != '/'))
			str += "\\";
	}
	str += mFileName;
	for (int i = 0; i < str.length(); i++)
		if (str[i] == '/')
			str = '\\';
}

void BeDbgFile::GetFilePath(String& outStr)
{
	outStr.Append(mDirectory);
	outStr.Append(DIR_SEP_CHAR);
	outStr.Append(mFileName);
}

//////////////////////////////////////////////////////////////////////////

BeModule::BeModule(const StringImpl& moduleName, BeContext* context)
{
	mBeIRCodeGen = NULL;
	mModuleName = moduleName;
	mContext = context;
	mActiveBlock = NULL;
	mInsertPos = -1;
	mCurDbgLoc = NULL;
	mLastDbgLoc = NULL;
	mActiveFunction = NULL;
	mDbgModule = NULL;
	mPrevDbgLocInline = NULL;
	mCurDbgLocIdx = 0;
	mCurLexBlockId = 0;	
}

void BeModule::Hash(BeHashContext& hashCtx)
{	
	hashCtx.Mixin(mConfigConsts64.size());
	for (auto configConst : mConfigConsts64)
		configConst->HashContent(hashCtx);

	if (mDbgModule != NULL)
		mDbgModule->HashReference(hashCtx);	

	if (!mFunctions.IsEmpty())
	{
		std::sort(mFunctions.begin(), mFunctions.end(), [](BeFunction* lhs, BeFunction* rhs)
		{
			return (lhs->mName < rhs->mName);
		});

		for (auto& beFunction : mFunctions)
		{
			if (!beFunction->mBlocks.IsEmpty())
				beFunction->HashReference(hashCtx);
		}
	}
	
	if (!mGlobalVariables.IsEmpty())
	{
		std::sort(mGlobalVariables.begin(), mGlobalVariables.end(), [](BeGlobalVariable* lhs, BeGlobalVariable* rhs)
		{
			return (lhs->mName < rhs->mName);
		});
	
		for (auto& beGlobalVar : mGlobalVariables)
		{
			if (beGlobalVar->mInitializer != NULL)
				beGlobalVar->HashReference(hashCtx);
		}
	}	
}

#define DELETE_ENTRY(i) delete mOwnedValues[i]; mOwnedValues[i] = NULL

BeModule::~BeModule()
{	
	delete mDbgModule;
}

void BeModule::StructToString(StringImpl& str, BeStructType* structType)
{
	str += structType->mName;
	str += " = type ";
	if (!structType->mMembers.IsEmpty())
	{
		if (structType->mIsPacked)
			str += "<";
		str += "{";
		for (int memberIdx = 0; memberIdx < (int)structType->mMembers.size(); memberIdx++)
		{
			if (memberIdx > 0)
				str += ", ";
			ToString(str, structType->mMembers[memberIdx].mType);
		}
		str += "}";
		if (structType->mIsPacked)
			str += ">";
	}
	else
	{
		str += "opaque";

		if (structType->mSize > 0)
		{
			str += " size ";
			str += StrFormat("%d", structType->mSize);
		}
		if (structType->mAlign > 0)
		{
			str += " align ";
			str += StrFormat("%d", structType->mAlign);
		}
	}
	str += "\n";
}

String BeModule::ToString(BeFunction* wantFunc)
{
	Dictionary<int, BeDbgLoc*> dbgLocs;

	String str;
	
	SetAndRestoreValue<BeFunction*> prevActiveFunc(mActiveFunction, NULL);

	BeDumpContext dc;
	
	if (wantFunc == NULL)
	{
		str += "Module: "; str += mModuleName; str += "\n";
		str += "Target: "; str += mTargetTriple; str += "\n";

		if (mDbgModule != NULL)
		{			
			str += "FileName: "; str += mDbgModule->mFileName; str += "\n";
			str += "Directory: "; str += mDbgModule->mDirectory; str += "\n";
			str += "Producer: "; str += mDbgModule->mProducer; str += "\n";			
		}
		
		for (int i = 0; i < (int)mConfigConsts64.size(); i++)
		{
			if (i == 0)
				str += "VirtualMethodOfs: ";
			else if (i == 1)
				str += "DynSlotOfs: ";
			dc.ToString(str, mConfigConsts64[i]);
			str += "\n";
		}

		str += "\n";
		str += "; Types\n";
		for (auto type : mContext->mTypes)
		{
			if (type->mTypeCode == BeTypeCode_Struct)
			{
				auto structType = (BeStructType*)type;
				StructToString(str, structType);
			}
		}
		str += "\n";

		str += "; Global variables\n";		
		for (int gvIdx = 0; gvIdx < (int)mGlobalVariables.size(); gvIdx++)
		{
			auto gv = mGlobalVariables[gvIdx];
			str += gv->mName;
			str += " =";
			if (gv->mInitializer == NULL)
				str += " external";
			if (gv->mLinkageType == BfIRLinkageType_Internal)
				str += " internal";
			if (gv->mIsConstant)
				str += " constant";
			if (gv->mIsTLS)
				str += " tls";
			if (gv->mInitializer != NULL)
			{
				str += " ";
				str += dc.ToString(gv->mInitializer);
			}
			else
			{
				str += " ";
				str += dc.ToString(gv->mType);
			}
			if (gv->mAlign != -1)
			{
				str += " align ";
				str += StrFormat("%d", gv->mAlign);
			}
			str += "\n";
		}
		str += "\n";

		if (mDbgModule != NULL)
		{
			if (!mDbgModule->mGlobalVariables.IsEmpty())
			{
				str += "; Global variable debug info\n";
				for (auto dbgGlobalVar : mDbgModule->mGlobalVariables)
				{
					str += dbgGlobalVar->mName;
					str += " = ";
					if (dbgGlobalVar->mIsLocalToUnit)
						str += "internal ";
					dc.ToString(str, dbgGlobalVar->mType);
					
					if (dbgGlobalVar->mValue != NULL)
					{
						str += " ";
						dc.ToString(str, dbgGlobalVar->mValue);
					}

					if (dbgGlobalVar->mFile != NULL)
					{
						str += " @";
						dc.ToString(str, dbgGlobalVar->mFile);
						str += StrFormat(":%d", dbgGlobalVar->mLineNum);
					}

					if (!dbgGlobalVar->mLinkageName.IsEmpty())
					{
						str += " Link:";
						str += dbgGlobalVar->mLinkageName;
					}
					str += "\n";
				}				
				
				str += "\n";
			}

			str += "; Debug types\n";
			for (auto dbgType : mDbgModule->mTypes)
			{
				if (auto dbgStructType = BeValueDynCast<BeDbgStructType>(dbgType))
				{
					dc.ToString(str, dbgStructType);					
					str += " = {";
					if (dbgStructType->mSize != -1)
					{
						str += StrFormat("\n  Size: %d", dbgStructType->mSize);
						str += StrFormat("\n  Align: %d", dbgStructType->mAlign);
					}
					if (dbgStructType->mDerivedFrom != NULL)
					{
						str += "\n  Base: "; str += dc.ToString(dbgStructType->mDerivedFrom);
					}
					if (!dbgStructType->mMembers.IsEmpty())
					{
						str += "\n  Members: {";
						for (int memberIdx = 0; memberIdx < dbgStructType->mMembers.Count(); memberIdx++)
						{
							if (memberIdx > 0)
								str += ", ";
							str += "\n    ";
							dc.ToString(str, dbgStructType->mMembers[memberIdx]);
						}
						str += "}";
					}
					if (!dbgStructType->mMethods.IsEmpty())
					{
						str += "\n  Methods: {";
						for (int methodIdx = 0; methodIdx < dbgStructType->mMethods.Count(); methodIdx++)
						{
							if (methodIdx > 0)
								str += ",";

							str += "\n    ";
							dc.ToString(str, dbgStructType->mMethods[methodIdx], false);																																
						}
						str += "}";
					}
					str += "}\n";
				}
				else if (auto dbgEnumType = BeValueDynCast<BeDbgEnumType>(dbgType))
				{
					dc.ToString(str, dbgEnumType);
					str += " = enum {";
					if (dbgEnumType->mSize != -1)
					{
						str += StrFormat("\n  Size: %d", dbgEnumType->mSize);
						str += StrFormat("\n  Align: %d", dbgEnumType->mAlign);
					}
					if (dbgEnumType->mElementType != NULL)
					{
						str += "\n  Underlying: "; str += dc.ToString(dbgEnumType->mElementType);
					}
					if (!dbgEnumType->mMembers.IsEmpty())
					{
						str += "\n  Members: {";
						for (int memberIdx = 0; memberIdx < dbgEnumType->mMembers.Count(); memberIdx++)
						{
							if (memberIdx > 0)
								str += ", ";
							str += "\n    ";
							dc.ToString(str, dbgEnumType->mMembers[memberIdx]);
						}
						str += "}";
					}
					str += "}\n";
				}
			}
			str += "\n";

			str += "; Debug functions\n";
			for (auto dbgFunc : mDbgModule->mFuncs)
			{
				if (!dbgFunc->mIncludedAsMember)
				{
					dc.ToString(str, dbgFunc, true);
					str += "\n";
				}
			}

			str += "\n";
		}

		str += "; Functions\n";
	}

	for (auto func : mFunctions)
	{
		if ((wantFunc != NULL) && (wantFunc != func))
			continue;

		mActiveFunction = func;
 		
		Dictionary<BeValue*, String> valueNameMap;
		HashSet<String> seenNames;

		auto funcType = func->GetFuncType();
		if (func->mBlocks.size() == 0)
			str += "declare ";
		else
			str += "define ";
		ToString(str, func->GetFuncType()->mReturnType);
		str += " ";
		str += func->mName;
		str += "(";
		for (int paramIdx = 0; paramIdx < (int)funcType->mParams.size(); paramIdx++)
		{	
			auto& typeParam = funcType->mParams[paramIdx];
			auto& param = func->mParams[paramIdx];
			if (paramIdx > 0)
				str += ", ";
			ToString(str, typeParam.mType);
			
			if (param.mStructRet)
				str += " sret";
			if (param.mNoAlias)
				str += " noalias";
			if (param.mNoCapture)
				str += " nocapture";
			if (param.mZExt)
				str += " zext";
			if (param.mDereferenceableSize != -1)
				str += StrFormat(" dereferenceable(%d)", param.mDereferenceableSize);

			str += " ";
			if (param.mName.empty())
				param.mName = StrFormat("p%d", paramIdx);
			dc.mSeenNames[param.mName] = 0;
			str += "%" + param.mName;
		}

		if (funcType->mIsVarArg)
		{
			if (!funcType->mParams.IsEmpty())
				str += ", ";
			str += "...";
		}

		str += ")";

		if (func->mAlwaysInline)
			str += " AlwaysInline";
		if (func->mNoUnwind)
			str += " nounwind";
		if (func->mUWTable)
			str += " uwtable";
		if (func->mNoReturn)
			str += " noreturn";
		if (func->mNoFramePointerElim)
			str += " noframepointerelim";
		if (func->mIsDLLExport)
			str += " dllexport";

		if (func->mBlocks.size() == 0)
		{
			str += "\n\n";
			continue;
		}

		str += " {\n";

#define DISPLAY_INST0(typeName, name) \
			case typeName::TypeId: { \
			auto castedInst = (typeName*)inst; \
			str += name; \
		} \
		break;

#define DISPLAY_INST1(typeName, name, member1) \
			case typeName::TypeId: { \
			auto castedInst = (typeName*)inst; \
			str += name; \
			str += " "; \
			dc.ToString(str, castedInst->member1); \
		} \
		break;

#define DISPLAY_INST2(typeName, name, member1, member2) \
			case typeName::TypeId: { \
			auto castedInst = (typeName*)inst;\
			str += name; \
			str += " "; \
			dc.ToString(str, castedInst->member1); \
			str += ", "; \
			dc.ToString(str, castedInst->member2); \
		} \
		break;

#define DISPLAY_INST2_OPEN(typeName, name, member1, member2) \
			case typeName::TypeId: { \
			auto castedInst = (typeName*)inst;\
			str += name; \
			str += " "; \
			dc.ToString(str, castedInst->member1); \
			str += ", "; \
			dc.ToString(str, castedInst->member2); \
			}

#define DISPLAY_INST3(typeName, name, member1, member2, member3) \
		case typeName::TypeId: { \
			auto castedInst = (typeName*)inst;\
			str += name; \
			str += " "; \
			dc.ToString(str, castedInst->member1); \
			str += ", "; \
			dc.ToString(str, castedInst->member2); \
			if ((std::is_pointer<decltype(castedInst->member3)>::value) && (castedInst->member3 != NULL)) \
			{ \
				str += ", "; \
				str += dc.ToString(castedInst->member3); \
			} \
		} \
		break;

#define DISPLAY_INST4(typeName, name, member1, member2, member3, member4) \
		case typeName::TypeId: { \
			auto castedInst = (typeName*)inst;\
			str += name; \
			str += " "; \
			dc.ToString(str, castedInst->member1); \
			str += ", "; \
			dc.ToString(str, castedInst->member2); \
			if ((std::is_pointer<decltype(castedInst->member3)>::value) && (castedInst->member3 != NULL)) \
			{ \
				str += ", "; \
				dc.ToString(str, castedInst->member3); \
				if ((std::is_pointer<decltype(castedInst->member4)>::value) && (castedInst->member4 != NULL)) \
				{ \
					str += ", "; \
					dc.ToString(str, castedInst->member4); \
				} \
			} \
		} \
		break;

		HashSet<BeDbgLoc*> seenInlinedAt;
		BeDbgLoc* lastDbgLoc = NULL;
		HashSet<BeDbgLoc*> prevDbgLocs;

		for (int blockIdx = 0; blockIdx < (int)func->mBlocks.size(); blockIdx++)
		{
			auto beBlock = func->mBlocks[blockIdx];
			if (blockIdx > 0)
				str += "\n";
			dc.ToString(str, beBlock);
			str += ":\n";

			for (auto inst : beBlock->mInstructions)
			{	
				if (inst->mDbgLoc != NULL)
				{
					if ((inst->mDbgLoc != lastDbgLoc) && (lastDbgLoc != NULL))
					{
// 						if (inst->mDbgLoc->mIdx < lastDbgLoc->mIdx)
// 						{
// 							str += "WARNING: Out-of-order debug locations:\n";
// 						}

						if ((inst->mDbgLoc->mDbgInlinedAt != lastDbgLoc->mDbgInlinedAt) && (inst->mDbgLoc->mDbgInlinedAt != NULL))
						{														
							prevDbgLocs.Clear();
							auto prevInlinedAt = lastDbgLoc->mDbgInlinedAt;
							while (prevInlinedAt != NULL)
							{
								prevDbgLocs.Add(prevInlinedAt);
								prevInlinedAt = prevInlinedAt->mDbgInlinedAt;
							}

							auto curInlinedAt = inst->mDbgLoc->mDbgInlinedAt;
							if (!prevDbgLocs.Contains(curInlinedAt))
							{
								if (!seenInlinedAt.Add(curInlinedAt))
								{
									str += "WARNING: Adding new span of already-seen inlined location:\n";
								}
							}
						}
					}
					lastDbgLoc = inst->mDbgLoc;
				}

				str += "  ";
				if (inst->CanBeReferenced())
				{					
					str += dc.ToString(inst, false);
					str += " = ";
				}

				switch (inst->GetTypeId())
				{
				DISPLAY_INST0(BeNopInst, "nop");
				DISPLAY_INST0(BeUnreachableInst, "unreachable");
				DISPLAY_INST0(BeEnsureInstructionAtInst, "ensureCodeAt");
				DISPLAY_INST1(BeUndefValueInst, "undef", mType);
				DISPLAY_INST2(BeExtractValueInst, "extractValue", mAggVal, mIdx);
				DISPLAY_INST3(BeInsertValueInst, "insertValue", mAggVal, mMemberVal, mIdx);
				DISPLAY_INST2_OPEN(BeNumericCastInst, "numericCast", mValue, mToType)
					{
						auto castedInst = (BeNumericCastInst*)inst;
						if (castedInst->mValSigned)
							str += " s->";
						else
							str += " u->";
						if (castedInst->mToSigned)
							str += "s";
						else
							str += "u";
					}
					break;
				DISPLAY_INST2(BeBitCastInst, "bitCast", mValue, mToType);
				DISPLAY_INST1(BeNegInst, "neg", mValue);
				DISPLAY_INST1(BeNotInst, "not", mValue);
				DISPLAY_INST3(BeBinaryOpInst, "binOp", mLHS, mOpKind, mRHS);
					/*{
						auto castedInst = (BeAddInst*)inst;
						str += "add ";
						str += dc.ToString(castedInst->mLHS);
						str += ", ";
						str += dc.ToString(castedInst->mRHS, false);
					}
					break;*/
				DISPLAY_INST3(BeCmpInst, "cmp", mCmpKind, mLHS, mRHS);				
				DISPLAY_INST1(BeObjectAccessCheckInst, "objectAccessCheck", mValue);
				case BeAllocaInst::TypeId:
					{
						auto castedInst = (BeAllocaInst*)inst;
						str += "alloca ";
						ToString(str, castedInst->mType);
						if (castedInst->mArraySize != NULL)
						{
							str += ", ";
							dc.ToString(str, castedInst->mArraySize);
						}
						str += ", align ";
						dc.ToString(str, castedInst->mAlign);
					}
					break;
				DISPLAY_INST1(BeAliasValueInst, "aliasvalue", mPtr);
				DISPLAY_INST1(BeLifetimeStartInst, "lifetime.start", mPtr);				
				DISPLAY_INST1(BeLifetimeEndInst, "lifetime.end", mPtr);
				DISPLAY_INST2(BeLifetimeFenceInst, "lifetime.fence", mFenceBlock, mPtr);
				DISPLAY_INST0(BeValueScopeStartInst, "valueScope.start");
				DISPLAY_INST1(BeValueScopeRetainInst, "valueScope.retain", mValue);
				DISPLAY_INST1(BeValueScopeEndInst, ((BeValueScopeEndInst*)inst)->mIsSoft ? "valueScope.softEnd" : "valueScope.hardEnd", mScopeStart);
				DISPLAY_INST1(BeLifetimeExtendInst, "lifetime.extend", mPtr);
				case BeLoadInst::TypeId:
					{
						auto castedInst = (BeLoadInst*)inst;
						str += "load ";
						if (castedInst->mIsVolatile)
							str += "volatile ";
						ToString(str, inst->GetType());
						str += ", ";
						dc.ToString(str, castedInst->mTarget);
					}
					break;
				case BeStoreInst::TypeId:
					{
						auto castedInst = (BeStoreInst*)inst;
						str += "store ";
						if (castedInst->mIsVolatile)
							str += "volatile ";
						dc.ToString(str, castedInst->mVal);
						str += ", ";
						dc.ToString(str, castedInst->mPtr);
					}
					break;
				DISPLAY_INST1(BeSetCanMergeInst, "setCanMerge", mVal);
				DISPLAY_INST4(BeMemSetInst, "memset", mAddr, mVal, mSize, mAlignment);
				DISPLAY_INST0(BeFenceInst, "fence");
				DISPLAY_INST0(BeStackSaveInst, "stackSave");
				DISPLAY_INST1(BeStackRestoreInst, "stackRestore", mStackVal);
				DISPLAY_INST3(BeGEPInst, "gep", mPtr, mIdx0, mIdx1);				
				//DISPLAY_INST1(BeBrInst, "br", mTargetBlock);
				case BeBrInst::TypeId:
					{
						auto castedInst = (BeBrInst*)inst;
						if (castedInst->mNoCollapse)
							str += "br NoCollapse ";
						else if (castedInst->mIsFake)
							str += "br Fake ";
						else
							str += "br ";
						dc.ToString(str, castedInst->mTargetBlock);
					}
					break;

				DISPLAY_INST3(BeCondBrInst, "condbr", mCond, mTrueBlock, mFalseBlock);
				
				case BeRetInst::TypeId:
					{
						auto castedInst = (BeRetInst*)inst;
						str += "ret";
						if (castedInst->mRetValue != NULL)
						{
							str += " ";
							dc.ToString(str, castedInst->mRetValue);
						}
						else
							str += " void";
					}
					break;
				DISPLAY_INST2(BeSetRetInst, "setret", mRetValue, mReturnTypeId);
				case BeCallInst::TypeId:
					{
						auto castedInst = (BeCallInst*)inst;
						if (castedInst->mInlineResult != NULL)
						{
							str += "InlineResult: ";
							dc.ToString(str, castedInst->mInlineResult);
							break;
						}

						if (castedInst->mTailCall)
							str += "tail ";

						str += "call ";
						dc.ToString(str, castedInst->mFunc);
						str += "(";
						for (int argIdx = 0; argIdx < (int)castedInst->mArgs.size(); argIdx++)
						{
							auto& arg = castedInst->mArgs[argIdx];

							if (argIdx > 0)
								str += ", ";
							str += dc.ToString(arg.mValue);

							if (arg.mStructRet)
								str += " sret";
							if (arg.mZExt)
								str += " zext";
							if (arg.mNoAlias)
								str += " noalias";
							if (arg.mNoCapture)
								str += " nocapture";							
							if (arg.mDereferenceableSize != -1)
								str += StrFormat(" dereferenceable(%d)", arg.mDereferenceableSize);
						}
						str += ")";
						if (castedInst->mNoReturn)
							str += " noreturn";
					}
					break;
				case BePhiInst::TypeId:
					{
						auto castedInst = (BePhiInst*)inst;
						str += "phi ";
						dc.ToString(str, castedInst->mType);
						str += " ";						
						for (int argIdx = 0; argIdx < (int)castedInst->mIncoming.size(); argIdx++)
						{
							if (argIdx > 0)
								str += ", ";
							str += "[";							
							dc.ToString(str, castedInst->mIncoming[argIdx]->mValue);
							str += ", ";
							dc.ToString(str, castedInst->mIncoming[argIdx]->mBlock);
							str += "]";
						}						
					}
					break;
				case BeSwitchInst::TypeId:
					{
						auto castedInst = (BeSwitchInst*)inst;
						str += "switch ";
						dc.ToString(str, castedInst->mValue);
						str += " ";
						dc.ToString(str, castedInst->mDefaultBlock);
						str += " [";
						for (int argIdx = 0; argIdx < (int)castedInst->mCases.size(); argIdx++)
						{
							str += "\n   ";
							dc.ToString(str, castedInst->mCases[argIdx].mValue);
							str += ", ";
							dc.ToString(str, castedInst->mCases[argIdx].mBlock);
						}
						str += "]";
					}
					break;
				DISPLAY_INST2_OPEN(BeDbgDeclareInst, "DbgDeclare", mDbgVar, mValue);
					{
						auto castedInst = (BeDbgDeclareInst*)inst;
						if (castedInst->mIsValue)
							str += " <val>";
						else
							str += " <addr>";

						if (auto dbgVariable = castedInst->mDbgVar)
						{							
							switch (dbgVariable->mInitType)
							{
							case BfIRInitType_NotNeeded: str += " noinit"; break;
							case BfIRInitType_NotNeeded_AliveOnDecl: str += " noinit_aliveondecl"; break;
							case BfIRInitType_Uninitialized: str += " uninit"; break;
							case BfIRInitType_Zero: str += " zero"; break;
							}
							if (dbgVariable->mScope != NULL)
							{
								str += " Scope:";
								dc.ToString(str, dbgVariable->mScope);
							}
						}
					}
					break;
				DISPLAY_INST1(BeComptimeError, "ComptimeError", mError);
				DISPLAY_INST1(BeComptimeGetType, "ComptimeGetType", mTypeId);
				DISPLAY_INST1(BeComptimeGetReflectType, "ComptimeGetReflectType", mTypeId);
				DISPLAY_INST2(BeComptimeDynamicCastCheck, "ComptimeDynamicCastCheck", mValue, mTypeId);
				DISPLAY_INST2(BeComptimeGetVirtualFunc, "ComptimeGetVirtualFunc", mValue, mVirtualTableIdx);
				DISPLAY_INST3(BeComptimeGetInterfaceFunc, "ComptimeGetInterfaceFunc", mValue, mIFaceTypeId, mMethodIdx);
				default:
					BF_FATAL("Notimpl");
					str += "<UNKNOWN INST>";
					break;
				}

				if (inst->mDbgLoc != NULL)
				{
					dbgLocs[inst->mDbgLoc->mIdx] = inst->mDbgLoc;
					str += StrFormat(" @%d", inst->mDbgLoc->mIdx);
					auto dbgFile = inst->mDbgLoc->GetDbgFile();
					if (dbgFile != NULL)
					{
						str += "[";
						str += dbgFile->mFileName;
						str += StrFormat(":%d", inst->mDbgLoc->mLine + 1);
						if (inst->mDbgLoc->mDbgInlinedAt != NULL)
						{
							str += ",inl";

							BeDbgLoc* inlinedAt = inst->mDbgLoc->mDbgInlinedAt;
							while (inlinedAt != NULL)
							{	
								str += StrFormat("#%d", inlinedAt->mIdx);
								inlinedAt = inlinedAt->mDbgInlinedAt;
							}
						}
						str += "]";
					}					
				}

				str += "\n";
			}
		}

		str += "}\n";


		if (func->mDbgFunction != NULL)
		{
			str += " DbgFunc: ";
			dc.ToString(str, func->mDbgFunction);
			str += "\n";

			for (auto dbgVar : func->mDbgFunction->mVariables)
			{
				if (dbgVar == NULL)
					continue;
				str += StrFormat(" Var: ");
				str += dbgVar->mName;
				str += " ";
				dc.ToString(str, dbgVar->mScope);
				str += "\n";
			}
		}

		str += "\n";
	}
		
	for (auto& dbgLocPair : dbgLocs)
	{
		auto dbgLoc = dbgLocPair.mValue;		
		dc.ToString(str, dbgLocPair.mValue);
		str += "\n";
	}
	str += "\n";	

	return str;
}

void BeModule::Print()
{
	OutputDebugStr(ToString());
}

void BeModule::Print(BeFunction* func)
{
	OutputDebugStr(ToString(func));
}

void BeModule::PrintValue(BeValue* val)
{
	BeDumpContext dumpCtx;	
	String str;
	dumpCtx.ToString(str, val);
	str += "\n";
	OutputDebugStr(str);

	auto type = val->GetType();
	if (type != NULL)
		bpt(type);
}

void BeModule::DoInlining(BeFunction* func)
{
	//bool debugging = func->mName == "?Test@Program@bf@@CAXXZ";
	//bool debugging = func->mName == "?TestA@TestClass@Bro@Dude@Hey@@SAX_J@Z";
	//debugging |= func->mName == "?TestB@TestClass@Bro@Dude@Hey@@SAX_J@Z";
	//debugging |= func->mName == "?TestC@TestClass@Bro@Dude@Hey@@SAX_J@Z";
// 	if (debugging)
// 	{
// 		Print(func);
// 	}

	if (func->mDidInlinePass)
		return;
	// Set this true here so we don't recurse on the same function	
	func->mDidInlinePass = true;

	int numHeadAllocas = 0;
	bool inHeadAllocas = true;

	int blockIdx = 0;

	// From head to resume
	std::unordered_multimap<BeBlock*, BeBlock*> inlineResumesMap;
	
	// From resume to head
	std::unordered_multimap<BeBlock*, BeBlock*> inlineHeadMap; 

	bool hadInlining = false;		

	std::function<void(int& blockIdx, BeBlock* endBlock, std::unordered_set<BeFunction*>& funcInlined)> _DoInlining;
	_DoInlining = [&](int& blockIdx, BeBlock* endBlock, std::unordered_set<BeFunction*>& funcInlined)
	{
		for (; blockIdx < (int)func->mBlocks.size(); blockIdx++)
		{
			auto beBlock = func->mBlocks[blockIdx];
			if (beBlock == endBlock)
			{
				// Let previous handler deal with this
				--blockIdx;
				return;
			}

			for (int instIdx = 0; instIdx < (int)beBlock->mInstructions.size(); instIdx++)
			{
				auto inst = beBlock->mInstructions[instIdx];
				if (inHeadAllocas)
				{
					switch (inst->GetTypeId())
					{
					case BeAllocaInst::TypeId:
					case BeNumericCastInst::TypeId:
					case BeBitCastInst::TypeId:
						numHeadAllocas++;
					default:
						inHeadAllocas = false;
					}
				}

				if (auto phiInst = BeValueDynCast<BePhiInst>(inst))
				{
					for (auto incoming : phiInst->mIncoming)
					{
						bool found = false;

						auto _CheckBlock = [&](BeBlock* checkBlock)
						{
							for (auto inst : checkBlock->mInstructions)
							{								
								switch (inst->GetTypeId())
								{
								case BeBrInst::TypeId:
									{
										auto castedInst = (BeBrInst*)inst;
										if (castedInst->mTargetBlock == beBlock)
										{											
											found = true;
										}
									}
									break;
								case BeCondBrInst::TypeId:
									{
										auto castedInst = (BeCondBrInst*)inst;
										if ((castedInst->mTrueBlock == beBlock) ||
											(castedInst->mFalseBlock == beBlock))
										{											
											found = true;
										}
									}
									break;
								case BeSwitchInst::TypeId:
									{
										auto castedInst = (BeSwitchInst*)inst;
										if (castedInst->mDefaultBlock == beBlock)
											found = true;
										for (auto& caseVal : castedInst->mCases)
											if (caseVal.mBlock == beBlock)
												found = true;
									}
									break;
								};
							};
							if (found)
							{
								incoming->mBlock = checkBlock;
								return;
							}
						};

						_CheckBlock(incoming->mBlock);
						auto itr = inlineResumesMap.find(incoming->mBlock);
						while (itr != inlineResumesMap.end())
						{
							if (found)
								break;
							if (itr->first != incoming->mBlock)
								break;

							auto checkBlock = itr->second;
							_CheckBlock(checkBlock);
							++itr;
						}
						BF_ASSERT(found);
					}
				}

				auto callInst = BeValueDynCast<BeCallInst>(inst);
				if (callInst == NULL)
					continue;

				auto inlineFunc = BeValueDynCast<BeFunction>(callInst->mFunc);
				if (inlineFunc == NULL)
					continue;
				if (inlineFunc == func)
					continue;
				if (!inlineFunc->mAlwaysInline)
					continue;

				if (inlineFunc->mBlocks.empty())
				{
					BF_FATAL("No content?");
					continue;
				}

				// It's more efficient to do depth-first inlining so nested inlines will be pre-expanded
				DoInlining(inlineFunc);

				//TODO: Not needed anymore, right?
				if (funcInlined.find(inlineFunc) != funcInlined.end())
					continue; // Don't recursively inline

				// Incase we have multiple inlines from the same location, those need to have unique dbgLocs
				callInst->mDbgLoc = DupDebugLocation(callInst->mDbgLoc);

				hadInlining = true;

				BeInliner inliner;
				inliner.mAlloc = &mAlloc;
				inliner.mOwnedValueVec = &mOwnedValues;
				inliner.mModule = this;
				inliner.mSrcFunc = inlineFunc;
				inliner.mDestFunc = func;
				inliner.mCallInst = callInst;				

				if ((func->mDbgFunction != NULL) && (inlineFunc->mDbgFunction != NULL))
				{
					//BeDbgLexicalBlock

					for (int srcVarIdx = 0; srcVarIdx < (int)inlineFunc->mDbgFunction->mVariables.size(); srcVarIdx++)
					{
						auto dbgGlobalVar = inlineFunc->mDbgFunction->mVariables[srcVarIdx];
						if (dbgGlobalVar == NULL)
							continue;
						auto destDbgGlobalVar = mOwnedValues.Alloc<BeDbgVariable>();
						destDbgGlobalVar->mName = dbgGlobalVar->mName;
						destDbgGlobalVar->mType = dbgGlobalVar->mType;
						destDbgGlobalVar->mInitType = dbgGlobalVar->mInitType;
						if (dbgGlobalVar->mValue != NULL)
						{
							BF_ASSERT(BeValueDynCast<BeConstant>(dbgGlobalVar->mValue) != NULL);							
							destDbgGlobalVar->mValue = dbgGlobalVar->mValue;
						}
						else
							BF_ASSERT(dbgGlobalVar->mValue == NULL);
						destDbgGlobalVar->mScope = (BeMDNode*)inliner.Remap(dbgGlobalVar->mScope);
						inliner.mValueMap[dbgGlobalVar] = destDbgGlobalVar;
						func->mDbgFunction->mVariables.push_back(destDbgGlobalVar);
					}
				}

				//int prevBlockSize = func->mBlocks.size();

				// Split block, with calls that come after the call going into inlineResume
				BeBlock* returnBlock = mOwnedValues.Alloc<BeBlock>();
				returnBlock->mName = "inlineResume";
				returnBlock->mFunction = func;
				func->mBlocks.Insert(blockIdx + 1, returnBlock);
				for (int srcIdx = instIdx + 1; srcIdx < (int)beBlock->mInstructions.size(); srcIdx++)
					returnBlock->mInstructions.push_back(beBlock->mInstructions[srcIdx]);
				beBlock->mInstructions.RemoveRange(instIdx, beBlock->mInstructions.size() - instIdx);

				/*auto _InsertResume = (BeBlock* beBlock, BeBlock* returnBlock)[&]
				{
					inlineResumesMap.insert(std::make_pair(beBlock, returnBlock));
					inlineHeadMap.insert(std::make_pair(returnBlock, beBlock));

					auto prevHeadItr = inlineHeadMap.find(beBlock);
				};
				_InsertResume(beBlock, returnBlock);*/

				auto headBlock = beBlock;
				while (true)
				{
					auto itr = inlineHeadMap.find(headBlock);
					if (itr == inlineHeadMap.end())
						break;
					headBlock = itr->second;
				}
				inlineResumesMap.insert(std::make_pair(headBlock, returnBlock));
				inlineHeadMap.insert(std::make_pair(returnBlock, headBlock));

				std::vector<BeBlock*> destBlocks;				

				for (int argIdx = 0; argIdx < (int)callInst->mArgs.size(); argIdx++)
				{
					auto& argVal = callInst->mArgs[argIdx];
					inliner.mValueMap[GetArgument(argIdx)] = argVal.mValue;
				}

				for (int inlineBlockIdx = 0; inlineBlockIdx < (int)inlineFunc->mBlocks.size(); inlineBlockIdx++)
				{
					auto srcBlock = inlineFunc->mBlocks[inlineBlockIdx];
					auto destBlock = mOwnedValues.Alloc<BeBlock>();
					destBlock->mFunction = func;
					destBlock->mName = inlineFunc->mName;
					destBlock->mName += "_";
					destBlock->mName += srcBlock->mName;
					if (inlineBlockIdx == 0)
					{
						auto brInst = mAlloc.Alloc<BeBrInst>();
						brInst->mDbgLoc = inst->mDbgLoc;
						brInst->mTargetBlock = destBlock;
						beBlock->mInstructions.push_back(brInst);
					}
					func->mBlocks.Insert(blockIdx + 1 + inlineBlockIdx, destBlock);
					destBlocks.push_back(destBlock);
					inliner.mValueMap[srcBlock] = destBlock;
				}

				bool inlineInHeadAllocas = true;

				for (int inlineBlockIdx = 0; inlineBlockIdx < (int)inlineFunc->mBlocks.size(); inlineBlockIdx++)
				{
					auto srcBlock = inlineFunc->mBlocks[inlineBlockIdx];
					auto destBlock = destBlocks[inlineBlockIdx];
					inliner.mDestBlock = destBlock;

					for (int srcInstIdx = 0; srcInstIdx < (int)srcBlock->mInstructions.size(); srcInstIdx++)
					{
						auto srcInst = srcBlock->mInstructions[srcInstIdx];

						if (inlineInHeadAllocas)
						{
							if (srcInst->GetTypeId() == BeAllocaInst::TypeId)
							{
								BeAllocaInst* allocaInst = (BeAllocaInst*)srcInst;
								auto destAlloca = mAlloc.Alloc<BeAllocaInst>();
								destAlloca->mType = allocaInst->mType;
								destAlloca->mArraySize = allocaInst->mArraySize;
								destAlloca->mAlign = allocaInst->mAlign;
								destAlloca->mNoChkStk = allocaInst->mNoChkStk;
								destAlloca->mForceMem = allocaInst->mForceMem;								
								destAlloca->mName = allocaInst->mName;

								auto destBlock = func->mBlocks[0];
								destAlloca->mParentBlock = destBlock;
								destBlock->mInstructions.Insert(numHeadAllocas, destAlloca);
								numHeadAllocas++;
								inliner.mValueMap[allocaInst] = destAlloca;
								continue;
							}
							else
								inlineInHeadAllocas = false;
						}

						if (auto storeInst = BeValueDynCast<BeStoreInst>(srcInst))
						{
							if (auto argVal = BeValueDynCast<BeArgument>(storeInst->mVal))
							{
								// This doesn't solve the 'SRET' issue of allowing a single-value return
								//  in a SRET function to directly map the returned value to the incoming
								//  SRET pointer, since that relies on setting a function-wide
								//  mCompositeRetVRegIdx value.  Possible future optimization.
								auto setCanMergeInst = mAlloc.Alloc<BeSetCanMergeInst>();
								setCanMergeInst->mVal = inliner.Remap(storeInst->mPtr);
								inliner.AddInst(setCanMergeInst, NULL);
							}
						}

						if (auto retInst = BeValueDynCast<BeRetInst>(srcInst))
						{
							callInst->mInlineResult = inliner.Remap(retInst->mRetValue);
							callInst->mFunc = NULL;
							callInst->mArgs.clear();
							callInst->mNoReturn = false;
							callInst->mTailCall = false;

							if (retInst->mRetValue != NULL)
							{
								// We want to ensure that we can step onto the closing brace to see the __return value
								auto brInst = mAlloc.Alloc<BeEnsureInstructionAtInst>();
								inliner.AddInst(brInst, retInst);

								auto fenceInst = mAlloc.Alloc<BeLifetimeFenceInst>();					
								fenceInst->mFenceBlock = beBlock;
								fenceInst->mPtr = callInst->mInlineResult;
								inliner.AddInst(fenceInst, retInst);
							}

							auto brInst = mAlloc.Alloc<BeBrInst>();
							brInst->mTargetBlock = returnBlock;
							inliner.AddInst(brInst, retInst);
						}
						else
							inliner.VisitChild(srcInst);
					}
				}

				/*if (callInst->mInlineResult != NULL)
				{
					auto fenceInst = mAlloc.Alloc<BeLifetimeFenceInst>();					
					fenceInst->mPtr = callInst->mInlineResult;
					beBlock->mInstructions.push_back(fenceInst);
				}*/

				auto inlinedFuncInlined = funcInlined;
				inlinedFuncInlined.insert(inlineFunc);
				_DoInlining(blockIdx, returnBlock, inlinedFuncInlined);
			}
		}
	};

	/*int prevDbgVars = 0;
	if (func->mDbgFunction != NULL)
		prevDbgVars = (int)func->mDbgFunction->mVariables.size();*/

	std::unordered_set<BeFunction*> newFuncSet;
	_DoInlining(blockIdx, NULL, newFuncSet);

	/*if ((func->mDbgFunction != NULL) && (prevDbgVars != (int)func->mDbgFunction->mVariables.size()))
	{
		std::stable_sort(func->mDbgFunction->mVariables.begin(), func->mDbgFunction->mVariables.end(), [] (BeDbgVariable* lhs, BeDbgVariable* rhs)
		{
			BeDbgLoc* lhsInlinePos = NULL;
			if (lhs->mDeclDbgLoc != NULL)
				lhsInlinePos = lhs->mDeclDbgLoc->mDbgInlinedAt;
			BeDbgLoc* rhsInlinePos = NULL;
			if (rhs->mDeclDbgLoc != NULL)
				rhsInlinePos = rhs->mDeclDbgLoc->mDbgInlinedAt;

			if ((lhsInlinePos == NULL) || (rhsInlinePos == NULL))
			{
				if ((lhsInlinePos == NULL) && (rhsInlinePos != NULL))
					return true;
				return false;
			}

			return lhsInlinePos->mIdx < rhsInlinePos->mIdx;
		});
	}*/
	
}

void BeModule::DoInlining()
{
	BP_ZONE("BeModule::DoInlining");
	for (auto func : mFunctions)
	{
		DoInlining(func);
	}
}

BeCmpKind BeModule::InvertCmp(BeCmpKind cmpKind)
{
	switch (cmpKind)
	{
	case BeCmpKind_SLT:
		return BeCmpKind_SGE;
	case BeCmpKind_ULT:
		return BeCmpKind_UGE;
	case BeCmpKind_SLE:
		return BeCmpKind_SGT;
	case BeCmpKind_ULE:
		return BeCmpKind_UGT;
	case BeCmpKind_EQ:
		return BeCmpKind_NE;
	case BeCmpKind_NE:
		return BeCmpKind_EQ;
	case BeCmpKind_SGT:
		return BeCmpKind_SLE;
	case BeCmpKind_UGT:
		return BeCmpKind_ULE;
	case BeCmpKind_SGE:
		return BeCmpKind_SLT;
	case BeCmpKind_UGE:
		return BeCmpKind_ULT;
	}
	return cmpKind;
}

BeCmpKind BeModule::SwapCmpSides(BeCmpKind cmpKind)
{
	switch (cmpKind)
	{
	case BeCmpKind_SLT:
		return BeCmpKind_SGT;
	case BeCmpKind_ULT:
		return BeCmpKind_UGT;
	case BeCmpKind_SLE:
		return BeCmpKind_SGE;
	case BeCmpKind_ULE:
		return BeCmpKind_UGE;
	case BeCmpKind_EQ:
		return BeCmpKind_EQ;
	case BeCmpKind_NE:
		return BeCmpKind_NE;
	case BeCmpKind_SGT:
		return BeCmpKind_SLT;
	case BeCmpKind_UGT:
		return BeCmpKind_ULT;
	case BeCmpKind_SGE:
		return BeCmpKind_SLE;
	case BeCmpKind_UGE:
		return BeCmpKind_ULE;
	}
	return cmpKind;
}

void BeModule::AddInst(BeInst* inst)
{
	inst->mDbgLoc = mCurDbgLoc;
	inst->mParentBlock = mActiveBlock;
	if (mInsertPos == -1)
	{
		mActiveBlock->mInstructions.push_back(inst);
	}
	else
	{
		mActiveBlock->mInstructions.Insert(mInsertPos, inst);
		mInsertPos++;
	}

	//inst->mFuncRelId = mActiveBlock->mFunction->mCurElementId++;
}

void BeModule::ToString(StringImpl& str, BeType* type)
{
	if (type == NULL)
	{
		str += "<MissingType>";
		return;
	}

	switch (type->mTypeCode)
	{
	case BeTypeCode_None:
		str += "void";
		return;
	case BeTypeCode_NullPtr:
		str += "null";
		return;
	case BeTypeCode_Boolean:
		str += "bool";
		return;
	case BeTypeCode_Int8:
		str += "i8";
		return;
	case BeTypeCode_Int16:
		str += "i16";
		return;
	case BeTypeCode_Int32:
		str += "i32";
		return;
	case BeTypeCode_Int64:
		str += "i64";
		return;
	case BeTypeCode_Float:
		str += "float";
		return;
	case BeTypeCode_Double:
		str += "double";
		return;
	case BeTypeCode_Pointer:
		ToString(str, ((BePointerType*)type)->mElementType);
		str += "*";
		return;
	case BeTypeCode_Struct:
		str += ((BeStructType*)type)->mName;
		return;
	case BeTypeCode_Function:
		{
			auto funcType = (BeFunctionType*)type;			
			ToString(str, funcType->mReturnType);
			str += "(";
			for (int paramIdx = 0; paramIdx < (int)funcType->mParams.size(); paramIdx++)
			{
				if (paramIdx > 0)
					str += ", ";
				ToString(str, funcType->mParams[paramIdx].mType);
			}

			if (funcType->mIsVarArg)
			{
				if (!funcType->mParams.IsEmpty())
					str += ", ";
				str += "...";
			}

			str += ")";
			return;
		}
	case BeTypeCode_SizedArray:
		{
			auto arrayType = (BeSizedArrayType*)type;
			ToString(str, arrayType->mElementType);
			str += "[";
			str += StrFormat("%d", arrayType->mLength);
			str += "]";
			return;
		}
	case BeTypeCode_Vector:
		{
			auto arrayType = (BeSizedArrayType*)type;
			ToString(str, arrayType->mElementType);
			str += "<";
			str += StrFormat("%d", arrayType->mLength);
			str += ">";
			return;
		}
	}
	str += "<UnknownType>";
}

void BeModule::SetActiveFunction(BeFunction* function)
{
	mActiveFunction = function;	
}

BeArgument* BeModule::GetArgument(int argIdx)
{
	while ((int)argIdx >= mArgs.size())		
	{
		auto arg = mAlloc.Alloc<BeArgument>();
		arg->mModule = this;
		arg->mArgIdx = (int)mArgs.size();
		mArgs.push_back(arg);
	}
	
	return mArgs[argIdx];
}

BeBlock* BeModule::CreateBlock(const StringImpl& name)
{
	auto block = mOwnedValues.Alloc<BeBlock>();
	block->mName = name;
	return block;
}

void BeModule::AddBlock(BeFunction* function, BeBlock* block)
{
	block->mFunction = function;
	function->mBlocks.push_back(block);	
}

void BeModule::RemoveBlock(BeFunction* function, BeBlock* block)
{
	bool didRemove = function->mBlocks.Remove(block);	
	BF_ASSERT(didRemove);
#ifdef _DEBUG
	for (auto inst : block->mInstructions)
		inst->mWasRemoved = true;
#endif
}

BeBlock* BeModule::GetInsertBlock()
{
	return mActiveBlock;
}

void BeModule::SetInsertPoint(BeBlock* block)
{
	mActiveBlock = block;
	mInsertPos = -1;
}

void BeModule::SetInsertPointAtStart(BeBlock* block)
{
	mActiveBlock = block;
	mInsertPos = 0;
}

BeFunction* BeModule::CreateFunction(BeFunctionType* funcType, BfIRLinkageType linkageType, const StringImpl& name)
{
	auto func = mOwnedValues.Alloc<BeFunction>();
	func->mName = name;
	func->mModule = this;
	func->mType = mContext->GetPointerTo(funcType);
	func->mLinkageType = linkageType;
	func->mParams.Resize(funcType->mParams.size());
	mFunctions.push_back(func);
	
#ifdef _DEBUG
	// It IS possible hit this, especially if we have multiple intrinsics mapping to 'malloc' for example
	//BF_ASSERT(mFunctionMap.TryAdd(name, func));
#endif
	return func;
}

BeDbgLoc* BeModule::GetCurrentDebugLocation()
{
	return mCurDbgLoc;
}

void BeModule::SetCurrentDebugLocation(BeDbgLoc* debugLoc)
{
	mCurDbgLoc = debugLoc;
}

void BeModule::SetCurrentDebugLocation(int line, int column, BeMDNode* dbgScope, BeDbgLoc* dbgInlinedAt)
{
	if (mCurDbgLoc == NULL)
		mCurDbgLoc = mLastDbgLoc;

	if ((mCurDbgLoc != NULL) &&
		(mCurDbgLoc->mLine == line) &&
		(mCurDbgLoc->mColumn == column) &&
		(mCurDbgLoc->mDbgScope == dbgScope) &&
		(mCurDbgLoc->mDbgInlinedAt == dbgInlinedAt))
		return;

	mCurDbgLoc = mAlloc.Alloc<BeDbgLoc>();
	mCurDbgLoc->mLine = line;
	mCurDbgLoc->mColumn = column;
	mCurDbgLoc->mDbgScope = dbgScope;
	mCurDbgLoc->mDbgInlinedAt = dbgInlinedAt;	
	mCurDbgLoc->mIdx = mCurDbgLocIdx++;

	if ((dbgInlinedAt != NULL) && (!dbgInlinedAt->mHadInline))
	{
		dbgInlinedAt->mHadInline = true;		
	}

	mLastDbgLoc = mCurDbgLoc;
}

BeDbgLoc* BeModule::DupDebugLocation(BeDbgLoc* dbgLoc)
{
	if (dbgLoc == NULL)
		return dbgLoc;
	
	auto newDbgLoc = mAlloc.Alloc<BeDbgLoc>();
	newDbgLoc->mLine = dbgLoc->mLine;
	newDbgLoc->mColumn = dbgLoc->mColumn;
	newDbgLoc->mDbgScope = dbgLoc->mDbgScope;
	newDbgLoc->mDbgInlinedAt = dbgLoc->mDbgInlinedAt;
	newDbgLoc->mIdx = mCurDbgLocIdx++;

	if ((newDbgLoc->mDbgInlinedAt != NULL) && (!newDbgLoc->mDbgInlinedAt->mHadInline))
	{
		newDbgLoc->mDbgInlinedAt->mHadInline = true;
	}

	return newDbgLoc;
}

void BeModule::DupCurrentDebugLocation()
{
	mCurDbgLoc = DupDebugLocation(mCurDbgLoc);
	mLastDbgLoc = mCurDbgLoc;
}

BeNopInst* BeModule::CreateNop()
{
	auto inst = mAlloc.Alloc<BeNopInst>();
	AddInst(inst);
	return inst;
}

BeUndefValueInst* BeModule::CreateUndefValue(BeType* type)
{
	auto undefValue = AllocInst<BeUndefValueInst>();
	undefValue->mType = type;
	return undefValue;
}

BeNumericCastInst* BeModule::CreateNumericCast(BeValue* value, BeType* toType, bool valSigned, bool toSigned)
{
	auto inst = mAlloc.Alloc<BeNumericCastInst>();
	inst->mValue = value;
	inst->mToType = toType;
	inst->mValSigned = valSigned;
	inst->mToSigned = toSigned;
	BF_ASSERT(toType != NULL);
	AddInst(inst);
	return inst;
}

BeBitCastInst * BeModule::CreateBitCast(BeValue* value, BeType* toType)
{
	auto inst = mAlloc.Alloc<BeBitCastInst>();
	inst->mValue = value;
	inst->mToType = toType;	
	AddInst(inst);
	return inst;
}

BeCmpInst* BeModule::CreateCmp(BeCmpKind cmpKind, BeValue* lhs, BeValue* rhs)
{
	auto inst = mAlloc.Alloc<BeCmpInst>();
	inst->mCmpKind = cmpKind;
	inst->mLHS = lhs;
	inst->mRHS = rhs;
	AddInst(inst);
	return inst;
}

BeBinaryOpInst* BeModule::CreateBinaryOp(BeBinaryOpKind opKind, BeValue* lhs, BeValue* rhs)
{
#ifdef _DEBUG
	auto leftType = lhs->GetType();
	auto rightType = rhs->GetType();
	BF_ASSERT(leftType == rightType);
#endif
	auto inst = mAlloc.Alloc<BeBinaryOpInst>();
	inst->mOpKind = opKind;
	inst->mLHS = lhs;
	inst->mRHS = rhs;
	AddInst(inst);
	return inst;
}

BeAllocaInst* BeModule::CreateAlloca(BeType* type)
{
	auto inst = mAlloc.Alloc<BeAllocaInst>();
	inst->mType = type;
	AddInst(inst);
	return inst;
}

BeLoadInst* BeModule::CreateLoad(BeValue* value, bool isVolatile)
{	
	auto inst = mAlloc.Alloc<BeLoadInst>();
	inst->mTarget = value;
	inst->mIsVolatile = isVolatile;
	AddInst(inst);
	return inst;
}

BeLoadInst* BeModule::CreateAlignedLoad(BeValue* value, int alignment, bool isVolatile)
{
	BF_ASSERT(value->GetType()->IsPointer());

	auto inst = mAlloc.Alloc<BeLoadInst>();
	inst->mTarget = value;
	inst->mIsVolatile = isVolatile;
	AddInst(inst);
	return inst;
}

BeStoreInst* BeModule::CreateStore(BeValue* val, BeValue* ptr, bool isVolatile)
{
	BF_ASSERT(ptr->GetType()->IsPointer());

	auto inst = mAlloc.Alloc<BeStoreInst>();
	inst->mVal = val;
	inst->mPtr = ptr;
	inst->mIsVolatile = isVolatile;
	AddInst(inst);
	return inst;
}

BeStoreInst* BeModule::CreateAlignedStore(BeValue* val, BeValue* ptr, int alignment, bool isVolatile)
{
	BF_ASSERT(ptr->GetType()->IsPointer());

	auto inst = mAlloc.Alloc<BeStoreInst>();
	inst->mVal = val;
	inst->mPtr = ptr;
	inst->mIsVolatile = isVolatile;
	AddInst(inst);
	return inst;
}

BeGEPInst* BeModule::CreateGEP(BeValue* ptr, BeValue* idx0, BeValue* idx1)
{	
	auto inst = mAlloc.Alloc<BeGEPInst>();
	inst->mPtr = ptr;
	inst->mIdx0 = idx0;
	inst->mIdx1 = idx1;		
	AddInst(inst);	
	
#ifdef _DEBUG
	BF_ASSERT(ptr->GetType()->IsPointer());
	inst->GetType();
#endif

	return inst;
}

BeBrInst* BeModule::CreateBr(BeBlock* block)
{
	auto inst = mAlloc.Alloc<BeBrInst>();
	inst->mTargetBlock = block;
	AddInst(inst);
	return inst;
}

BeCondBrInst* BeModule::CreateCondBr(BeValue* cond, BeBlock* trueBlock, BeBlock* falseBlock)
{
	auto inst = mAlloc.Alloc<BeCondBrInst>();
	inst->mCond = cond;
	inst->mTrueBlock = trueBlock;
	inst->mFalseBlock = falseBlock;
	AddInst(inst);
	return inst;
}

BeRetInst* BeModule::CreateRetVoid()
{
	auto inst = mAlloc.Alloc<BeRetInst>();
	AddInst(inst);
	return inst;
}

BeRetInst* BeModule::CreateRet(BeValue* value)
{
	auto inst = mAlloc.Alloc<BeRetInst>();
	inst->mRetValue = value;
	AddInst(inst);
	return inst;
}

BeSetRetInst* BeModule::CreateSetRet(BeValue* value, int returnTypeId)
{
	auto inst = mAlloc.Alloc<BeSetRetInst>();
	inst->mRetValue = value;
	inst->mReturnTypeId = returnTypeId;
	AddInst(inst);
	return inst;
}

BeCallInst* BeModule::CreateCall(BeValue* func, const SizedArrayImpl<BeValue*>& args)
{	
	auto inst = mOwnedValues.Alloc<BeCallInst>();
	inst->mFunc = func;
	if (!args.IsEmpty())
	{	
		inst->mArgs.resize(args.size());
		for (int i = 0; i < (int)args.size(); i++)
			inst->mArgs[i].mValue = args[i];
	}
	AddInst(inst);
	return inst;
}

BeConstant* BeModule::GetConstant(BeType* type, double floatVal)
{
	auto constant = mAlloc.Alloc<BeConstant>();
	constant->mType = type;
	constant->mDouble = floatVal;
	return constant;
}

BeConstant* BeModule::GetConstant(BeType* type, int64 intVal)
{
	auto constant = mAlloc.Alloc<BeConstant>();
	constant->mType = type;

	// Sign extend. One reason this is required is to for binary searching on switches
	switch (type->mTypeCode)
	{
	case BeTypeCode_Int8:
		constant->mInt64 = (int8)intVal;
		break;
	case BeTypeCode_Int16:
		constant->mInt64 = (int16)intVal;
		break;
	case BeTypeCode_Int32:
		constant->mInt64 = (int32)intVal;
		break;
	default:
		constant->mInt64 = intVal;
	}		
	
	return constant;
}

BeConstant* BeModule::GetConstant(BeType* type, bool boolVal)
{
	auto constant = mAlloc.Alloc<BeConstant>();
	constant->mType = type;
	constant->mBool = boolVal;
	return constant;
}

BeConstant* BeModule::GetConstantNull(BePointerType* type)
{
	auto constant = mAlloc.Alloc<BeConstant>();	
	if (type == NULL)
		constant->mType = mContext->GetPrimitiveType(BeTypeCode_NullPtr);
	else
		constant->mType = type;	
	return constant;
}

BeDbgReferenceType * BeDbgModule::CreateReferenceType(BeDbgType* elementType)
{	
	BeDbgType* useType = elementType->FindDerivedType(BeDbgReferenceType::TypeId);
	if (useType == NULL)
	{
		auto dbgType = mTypes.Alloc<BeDbgReferenceType>();
		dbgType->mElement = elementType;
		elementType->mDerivedTypes.PushFront(dbgType, &mBeModule->mAlloc);
		useType = dbgType;
	}
	return (BeDbgReferenceType*)useType;
}

void BeDbgModule::HashContent(BeHashContext & hashCtx)
{
	hashCtx.Mixin(TypeId);
	hashCtx.MixinStr(mFileName);
	hashCtx.MixinStr(mDirectory);
	hashCtx.MixinStr(mProducer);

	BeDumpContext dc;	
	String lhsName;
	String rhsName;

	if (!mFuncs.IsEmpty())
	{
		auto _GetName = [&](BeDbgFunction* func, String& str)
		{
			if (!func->mLinkageName.IsEmpty())			
				str.Append(func->mLinkageName);			
			else			
				dc.ToString(str, func);			
		};
		
		Array<BeDbgFunction*> unrefFuncs;
		for (auto dbgFunc : mFuncs)
		{
			if ((!dbgFunc->mIncludedAsMember) && (dbgFunc->mHashId == -1))
				unrefFuncs.Add(dbgFunc);
		}

		std::sort(unrefFuncs.begin(), unrefFuncs.end(), [&](BeDbgFunction* lhs, BeDbgFunction* rhs)
		{			
			lhsName.Clear();
			_GetName(lhs, lhsName);

			rhsName.Clear();
			_GetName(rhs, rhsName);			

			int cmp = String::Compare(lhsName, rhsName, false);
			if (cmp != 0)
				return cmp < 0;

			if (lhs->mFile != rhs->mFile)
			{
				lhsName.Clear();
				rhsName.Clear();
				if (lhs->mFile != NULL)
					lhs->mFile->ToString(lhsName);
				if (rhs->mFile != NULL)
					rhs->mFile->ToString(rhsName);
				cmp = String::Compare(lhsName, rhsName, false);
				if (cmp != 0)
					return cmp < 0;
			}

			if (lhs->mLine != rhs->mLine)
				return lhs->mLine < rhs->mLine;
			
			return lhs->mIdx < rhs->mIdx;
		});

		hashCtx.Mixin(unrefFuncs.size());
		for (auto dbgFunc : unrefFuncs)
		{
			if (dbgFunc->mHashId == -1)
				dbgFunc->HashReference(hashCtx);
		}
	}

	if (!mGlobalVariables.IsEmpty())
	{
		auto _GetName = [&](BeDbgGlobalVariable* func, String& str)
		{
			if (!func->mLinkageName.IsEmpty())
				str.Append(func->mLinkageName);
			else
				dc.ToString(str, func);
		};

		std::sort(mGlobalVariables.begin(), mGlobalVariables.end(), [&](BeDbgGlobalVariable* lhs, BeDbgGlobalVariable* rhs)
		{
			lhsName.Clear();
			_GetName(lhs, lhsName);

			rhsName.Clear();
			_GetName(rhs, rhsName);

			return (lhsName < rhsName);
		});

		for (auto globalVar : mGlobalVariables)
			globalVar->HashReference(hashCtx);
	}
}
