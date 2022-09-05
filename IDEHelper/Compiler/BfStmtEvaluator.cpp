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

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/FileSystem.h"

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(pop)

USING_NS_BF;

bool BfModule::AddDeferredCallEntry(BfDeferredCallEntry* deferredCallEntry, BfScopeData* scopeData)
{
	if ((((mCompiler->mIsResolveOnly) && (!mIsComptimeModule)) ||
		(mBfIRBuilder->mIgnoreWrites)) && (deferredCallEntry->mDeferredBlock == NULL))
	{
		// For resolve entries, we only keep deferred blocks because we need to process them later so we can
		//  resolve inside of them.  This is also required for lambda bind scan-pass
		delete deferredCallEntry;
		return false;
	}

	if (mBfIRBuilder->mIgnoreWrites)
	{
		deferredCallEntry->mIgnored = true;
		scopeData->mDeferredCallEntries.PushBack(deferredCallEntry);
		return true;
	}

	// We don't need to do a "clear handlers" if we're just adding another dyn to an existing dyn list
	bool isDyn = mCurMethodState->mCurScope->IsDyn(scopeData);

	if (mCurMethodState->mPendingNullConditional != NULL)
		isDyn = true;

	deferredCallEntry->mOrigScopeArgs = deferredCallEntry->mScopeArgs;

	if (!isDyn)
	{
		mCurMethodState->mCurScope->ClearHandlers(scopeData);

		if (!mBfIRBuilder->mIgnoreWrites)
		{
			if ((IsTargetingBeefBackend()) && (deferredCallEntry->mModuleMethodInstance.mMethodInstance != NULL) &&
				(!mContext->IsSentinelMethod(deferredCallEntry->mModuleMethodInstance.mMethodInstance)))
			{
				SizedArray<BfIRType, 8> origParamTypes;
				BfIRType origReturnType;
				deferredCallEntry->mModuleMethodInstance.mMethodInstance->GetIRFunctionInfo(this, origReturnType, origParamTypes);

				int sretIdx = deferredCallEntry->mModuleMethodInstance.mMethodInstance->GetStructRetIdx();
				BF_ASSERT(origParamTypes.size() == deferredCallEntry->mScopeArgs.size() + ((sretIdx != -1) ? 1 : 0));

				int argIdx = 0;
				int paramIdx = 0;
				for (int argIdx = 0; argIdx < (int)deferredCallEntry->mScopeArgs.size(); argIdx++, paramIdx++)
				{
					if (argIdx == sretIdx)
						paramIdx++;

					auto scopeArg = deferredCallEntry->mScopeArgs[argIdx];
					if ((scopeArg.IsConst()) || (scopeArg.IsFake()))
						continue;

					auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
					mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
					auto allocaInst = mBfIRBuilder->CreateAlloca(origParamTypes[paramIdx]);
					mBfIRBuilder->ClearDebugLocation_Last();
					mBfIRBuilder->SetInsertPoint(prevInsertBlock);
					if (WantsLifetimes())
					{
						mBfIRBuilder->CreateLifetimeStart(allocaInst);
						mBfIRBuilder->ClearDebugLocation_Last();
					}
					mBfIRBuilder->CreateStore(scopeArg, allocaInst);
					mBfIRBuilder->ClearDebugLocation_Last();
					deferredCallEntry->mScopeArgs[argIdx] = allocaInst;
					if (WantsLifetimes())
						scopeData->mDeferredLifetimeEnds.push_back(allocaInst);
				}
				deferredCallEntry->mArgsNeedLoad = true;
			}
		}

		scopeData->mDeferredCallEntries.PushFront(deferredCallEntry);
		return true;
	}
	bool isLooped = mCurMethodState->mCurScope->IsLooped(scopeData);

	BfDeferredCallEntry* listEntry = NULL;
	if (!scopeData->mDeferredCallEntries.IsEmpty())
	{
		listEntry = scopeData->mDeferredCallEntries.mHead;
		if (!listEntry->IsDynList())
			listEntry = NULL;
	}

	auto deferredCallEntryType = ResolveTypeDef(mCompiler->mDeferredCallTypeDef);
	AddDependency(deferredCallEntryType, mCurTypeInstance, BfDependencyMap::DependencyFlag_Allocates);
	mBfIRBuilder->PopulateType(deferredCallEntryType);
	auto deferredCallEntryTypePtr = CreatePointerType(deferredCallEntryType);

	UpdateSrcPos(mCurMethodInstance->mMethodDef->GetRefNode(), BfSrcPosFlag_NoSetDebugLoc);

	if (listEntry == NULL)
	{
		listEntry = new BfDeferredCallEntry();

		if (!mBfIRBuilder->mIgnoreWrites)
		{
			listEntry->mDynCallTail = CreateAlloca(deferredCallEntryTypePtr, false, "deferredCallTail");
			if (WantsLifetimes())
				scopeData->mDeferredLifetimeEnds.push_back(listEntry->mDynCallTail);

			auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
			mBfIRBuilder->SaveDebugLocation();
			mBfIRBuilder->SetInsertPointAtStart(mCurMethodState->mIRInitBlock);

			auto scopeHead = &mCurMethodState->mHeadScope;
			if (scopeHead->mDIScope)
				mBfIRBuilder->SetCurrentDebugLocation(mCurFilePosition.mCurLine + 1, 0, scopeHead->mDIScope, BfIRMDNode());

			if (WantsLifetimes())
				mBfIRBuilder->CreateLifetimeStart(listEntry->mDynCallTail);
			auto storeInst = mBfIRBuilder->CreateStore(GetDefaultValue(deferredCallEntryTypePtr), listEntry->mDynCallTail);
			mBfIRBuilder->ClearDebugLocation(storeInst);

			if (WantsDebugInfo())
			{
				auto deferredCallEntryType = ResolveTypeDef(mCompiler->mDeferredCallTypeDef);
				auto deferredCallEntryTypePtr = CreatePointerType(deferredCallEntryType);

				String varName = StrFormat("__deferred%d", mCurMethodState->mDeferredLoopListCount);

				mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRInitBlock);

				if (scopeHead->mDIScope)
					mBfIRBuilder->SetCurrentDebugLocation(mCurFilePosition.mCurLine + 1, 0, scopeHead->mDIScope, BfIRMDNode());
				mBfIRBuilder->CreateStatementStart();

				//TODO: Make this work for LLVM - we need a proper debug location
				//if (IsTargetingBeefBackend())
				{
					auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(scopeHead->mDIScope, varName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(deferredCallEntryTypePtr));
					mBfIRBuilder->DbgInsertDeclare(listEntry->mDynCallTail, diVariable);
				}
				mCurMethodState->mDeferredLoopListCount++;
			}

			mBfIRBuilder->SetInsertPoint(prevInsertBlock);
			mBfIRBuilder->RestoreDebugLocation();
		}

		mCurMethodState->mCurScope->ClearHandlers(scopeData);
		scopeData->mDeferredCallEntries.PushFront(listEntry);
	}

	BfIRValue deferredAlloca;
	SizedArray<BfType*, 4> types;
	SizedArray<String, 8> memberNames;
	int instAlign = 1;
	int dataPos = 0;
	int instSize = 0;
	Array<int> memberPositions;
	String typeName;
	BfDeferredMethodCallData* deferredMethodCallData = NULL;

	if (deferredCallEntry->mDeferredBlock != NULL)
	{
		HashContext hashCtx;
		hashCtx.Mixin(deferredCallEntry->mDeferredBlock->GetSrcStart());

		auto parserData = deferredCallEntry->mDeferredBlock->GetParserData();
		if (parserData != NULL)
			hashCtx.MixinStr(parserData->mFileName);

		int64 blockId = BfDeferredMethodCallData::GenerateMethodId(this, hashCtx.Finish64());
		deferredCallEntry->mBlockId = blockId;

		auto deferType = deferredCallEntryType;

		BfIRType deferIRType;
		auto int64Type = GetPrimitiveType(BfTypeCode_Int64);

		types.push_back(int64Type);
		memberNames.push_back("__methodId");

		types.push_back(deferredCallEntryTypePtr);
		memberNames.push_back("__next");

		for (auto& capture : deferredCallEntry->mCaptures)
		{
			BfType* type = capture.mValue.mType;
			types.push_back(type);
			memberNames.push_back(capture.mName);
		}

		SizedArray<BfIRType, 4> llvmTypes;
		SizedArray<BfIRMDNode, 8> diFieldTypes;

		typeName = StrFormat("_BF_DeferredData_%s", BfTypeUtils::HashEncode64(blockId).c_str());

		auto valueType = ResolveTypeDef(mCompiler->mValueTypeTypeDef);
		llvmTypes.push_back(mBfIRBuilder->MapType(valueType));

		//int dataPos = 0;
		for (int i = 0; i < (int)memberNames.size(); i++)
		{
			auto type = types[i];
			auto memberName = memberNames[i];

			if (!type->IsValuelessType())
			{
				llvmTypes.push_back(mBfIRBuilder->MapType(type));

				instAlign = BF_MAX(instAlign, (int)type->mAlign);
				int alignSize = (int)type->mAlign;
				int dataSize = type->mSize;
				if (alignSize > 1)
					dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);

				memberPositions.push_back(dataPos);
				dataPos += type->mSize;
			}
		}
		instSize = dataPos;

		deferIRType = mBfIRBuilder->CreateStructType(typeName);
		mBfIRBuilder->StructSetBody(deferIRType, llvmTypes, instSize, instAlign, false);

		auto prevInsertPoint = mBfIRBuilder->GetInsertBlock();
		if (!isLooped)
			mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
		deferredAlloca = mBfIRBuilder->CreateAlloca(deferIRType);
		mBfIRBuilder->SetAllocaAlignment(deferredAlloca, instAlign);
		mBfIRBuilder->SetAllocaNoChkStkHint(deferredAlloca);
		if (!isLooped)
			mBfIRBuilder->SetInsertPoint(prevInsertPoint);

		auto gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, 2); // mNext
		auto prevVal = mBfIRBuilder->CreateLoad(listEntry->mDynCallTail);
		mBfIRBuilder->CreateStore(prevVal, gepInstance);
		gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, 1); // mMethodId
		mBfIRBuilder->CreateStore(GetConstValue64(blockId), gepInstance);

		if (!deferredCallEntry->mCaptures.empty())
		{
			int dataIdx = 3;
			for (int captureIdx = 0; captureIdx < (int)deferredCallEntry->mCaptures.size(); captureIdx++)
			{
				auto& capture = deferredCallEntry->mCaptures[captureIdx];
				if (!capture.mValue.mType->IsValuelessType())
				{
					auto gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, dataIdx);
					mBfIRBuilder->CreateStore(capture.mValue.mValue, gepInstance);
					dataIdx++;
				}
			}
		}

		mBfIRBuilder->CreateStore(mBfIRBuilder->CreateBitCast(deferredAlloca, mBfIRBuilder->MapType(deferredCallEntryTypePtr)), listEntry->mDynCallTail);

		deferredCallEntry->mDeferredAlloca = deferredAlloca;
		listEntry->mDynList.PushFront(deferredCallEntry);
	}
	else
	{
		auto& llvmArgs = deferredCallEntry->mScopeArgs;
		auto moduleMethodInstance = deferredCallEntry->mModuleMethodInstance;
		auto methodInstance = moduleMethodInstance.mMethodInstance;
		auto methodDef = methodInstance->mMethodDef;
		auto owningType = methodInstance->mMethodInstanceGroup->mOwner;

		auto voidType = GetPrimitiveType(BfTypeCode_None);
		auto voidPtrType = CreatePointerType(voidType);

		BfDeferredMethodCallData** deferredMethodCallDataPtr = NULL;
		if (mDeferredMethodCallData.TryGetValue(methodInstance, &deferredMethodCallDataPtr))
		{
			deferredMethodCallData = *deferredMethodCallDataPtr;
		}
		else
		{
			deferredMethodCallData = new BfDeferredMethodCallData();
			mDeferredMethodCallData[methodInstance] = deferredMethodCallData;
			deferredMethodCallData->mMethodId = BfDeferredMethodCallData::GenerateMethodId(this, methodInstance->mIdHash);

			auto int64Type = GetPrimitiveType(BfTypeCode_Int64);
			auto methodDef = moduleMethodInstance.mMethodInstance->mMethodDef;
			auto thisType = moduleMethodInstance.mMethodInstance->mMethodInstanceGroup->mOwner;

			types.push_back(int64Type);
			memberNames.push_back("__methodId");

			types.push_back(deferredCallEntryTypePtr);
			memberNames.push_back("__next");

			if (!methodDef->mIsStatic)
			{
				types.push_back(thisType);
				memberNames.push_back("__this");
			}

			for (int paramIdx = 0; paramIdx < (int)methodInstance->GetParamCount(); paramIdx++)
			{
				if (methodInstance->IsParamSkipped(paramIdx))
					paramIdx++;
				types.push_back(methodInstance->GetParamType(paramIdx));
				memberNames.push_back(methodInstance->GetParamName(paramIdx));
			}

			SizedArray<BfIRType, 4> llvmTypes;

			//String typeName;
			typeName += StrFormat("_BF_DeferredData_%s", BfTypeUtils::HashEncode64(deferredMethodCallData->mMethodId).c_str());
			BfLogSysM("Building type: %s from methodInstance:%p\n", typeName.c_str(), methodInstance);

			//Array<int> memberPositions;

			//int instAlign = 1;
			//int dataPos = 0;
			BF_ASSERT(types.size() == memberNames.size());
			for (int i = 0; i < (int)types.size(); i++)
			{
				auto type = types[i];
				auto memberName = memberNames[i];

				if (!type->IsValuelessType())
				{
					llvmTypes.push_back(mBfIRBuilder->MapType(type));

					instAlign = BF_MAX(instAlign, (int)type->mAlign);
					int alignSize = (int)type->mAlign;
					int dataSize = type->mSize;
					if (alignSize > 1)
						dataPos = (dataPos + (alignSize - 1)) & ~(alignSize - 1);

					memberPositions.push_back(dataPos);
					dataPos += type->mSize;
				}
			}
			instSize = dataPos;

			deferredMethodCallData->mAlign = instAlign;
			deferredMethodCallData->mSize = instSize;
			deferredMethodCallData->mDeferType = mBfIRBuilder->CreateStructType(typeName);
			mBfIRBuilder->StructSetBody(deferredMethodCallData->mDeferType, llvmTypes, instSize, instAlign, false);
			deferredMethodCallData->mDeferTypePtr = mBfIRBuilder->GetPointerTo(deferredMethodCallData->mDeferType);
		}

		auto deferType = deferredMethodCallData->mDeferType;

		auto prevInsertPoint = mBfIRBuilder->GetInsertBlock();
		if (!isLooped)
			mBfIRBuilder->SetInsertPoint(mCurMethodState->mIRHeadBlock);
		deferredAlloca = mBfIRBuilder->CreateAlloca(BfIRType(deferredMethodCallData->mDeferType));
		mBfIRBuilder->ClearDebugLocation(deferredAlloca);
		mBfIRBuilder->SetAllocaAlignment(deferredAlloca, deferredMethodCallData->mAlign);
		mBfIRBuilder->SetAllocaNoChkStkHint(deferredAlloca);
		if (!isLooped)
			mBfIRBuilder->SetInsertPoint(prevInsertPoint);

		auto gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, 1);
		auto prevVal = mBfIRBuilder->CreateLoad(listEntry->mDynCallTail);
		mBfIRBuilder->CreateStore(prevVal, gepInstance);
		gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, 0);
		mBfIRBuilder->CreateStore(GetConstValue64(deferredMethodCallData->mMethodId), gepInstance);

		int dataIdx = 2;
		int argIdx = 0;
		if (!methodDef->mIsStatic)
		{
			gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, 2);
			if (owningType->IsStruct())
			{
				if ((!methodDef->mIsMutating) && (owningType->IsSplattable()))
				{
					BfTypedValue splatVal(llvmArgs[0], owningType, BfTypedValueKind_ThisSplatHead);
					BfTypedValue aggVal = AggregateSplat(splatVal, &llvmArgs[0]);
					aggVal = LoadValue(aggVal);
					mBfIRBuilder->CreateStore(aggVal.mValue, gepInstance);
					BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, owningType);
				}
				else
				{
					auto thisArg = mBfIRBuilder->CreateLoad(llvmArgs[0]);
					mBfIRBuilder->CreateStore(thisArg, gepInstance);
					argIdx++;
				}
			}
			else
			{
				mBfIRBuilder->CreateStore(llvmArgs[0], gepInstance);
				argIdx++;
			}
			dataIdx++;
		}

		for (int paramIdx = 0; paramIdx < (int)methodInstance->GetParamCount(); paramIdx++, dataIdx++)
		{
			if (methodInstance->IsParamSkipped(paramIdx))
				paramIdx++;
			auto paramType = methodInstance->GetParamType(paramIdx);
			bool paramIsSplat = methodInstance->GetParamIsSplat(paramIdx);
			gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredAlloca, 0, dataIdx/*, methodDef->mParams[paramIdx]->mName*/);
			if (paramType->IsStruct())
			{
				if (paramIsSplat)
				{
					BfTypedValue splatVal(llvmArgs[argIdx], paramType, BfTypedValueKind_SplatHead);
					BfTypedValue aggVal = AggregateSplat(splatVal, &llvmArgs[argIdx]);
					aggVal = LoadValue(aggVal);
					mBfIRBuilder->CreateStore(aggVal.mValue, gepInstance);
					BfTypeUtils::SplatIterate([&](BfType* checkType) { argIdx++; }, paramType);
				}
				else
				{
					auto val = mBfIRBuilder->CreateLoad(llvmArgs[argIdx]);
					mBfIRBuilder->CreateStore(val, gepInstance);
					argIdx++;
				}
			}
			else
			{
				mBfIRBuilder->CreateStore(llvmArgs[argIdx], gepInstance);
				argIdx++;
			}
		}

		mBfIRBuilder->CreateStore(mBfIRBuilder->CreateBitCast(deferredAlloca, mBfIRBuilder->MapType(deferredCallEntryTypePtr)), listEntry->mDynCallTail);

		deferredCallEntry->mDeferredAlloca = deferredAlloca;
		listEntry->mDynList.PushFront(deferredCallEntry);
	}

	if ((mBfIRBuilder->DbgHasInfo()) && (mHasFullDebugInfo) && (mCompiler->mOptions.mEmitDebugInfo) && (mCurMethodState->mCurScope->mDIScope))
	{
		auto int64Type = GetPrimitiveType(BfTypeCode_Int64);
		auto moduleMethodInstance = deferredCallEntry->mModuleMethodInstance;
		auto methodInstance = moduleMethodInstance.mMethodInstance;
		BfIRMDNode deferDIType;

		if ((deferredMethodCallData != NULL) && (deferredMethodCallData->mDeferDIType))
		{
			deferDIType = deferredMethodCallData->mDeferDIType;
		}
		else
		{
			BfIRMDNode diForwardDecl;
			SizedArray<BfIRMDNode, 8> diFieldTypes;
			if ((mBfIRBuilder->DbgHasInfo()) && (mHasFullDebugInfo))
			{
				String dbgTypeName;
				if (mCompiler->mOptions.IsCodeView())
					dbgTypeName += "_bf::";
				dbgTypeName += typeName;
				diForwardDecl = mBfIRBuilder->DbgCreateReplaceableCompositeType(llvm::dwarf::DW_TAG_structure_type, dbgTypeName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mFileInstance->mDIFile,
					mCurFilePosition.mCurLine, instSize * 8, instAlign * 8);

				if (methodInstance != NULL)
				{
					// We make a fake member to get inserted into the DbgModule data so we can show what method this deferred call goes to
					StringT<128> mangledName;
					BfMangler::Mangle(mangledName, mCompiler->GetMangleKind(), methodInstance);
					auto memberType = mBfIRBuilder->DbgCreateMemberType(diForwardDecl, mangledName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine,
						0, 0, -1, 0, mBfIRBuilder->DbgGetType(int64Type));
					diFieldTypes.push_back(memberType);
				}
			}

			for (int i = 0; i < (int)types.size(); i++)
			{
				auto type = types[i];
				auto memberName = memberNames[i];
				if ((mBfIRBuilder->DbgHasInfo()) && (mHasFullDebugInfo))
				{
					int memberFlags = 0;
					auto memberType = mBfIRBuilder->DbgCreateMemberType(diForwardDecl, memberName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine,
						type->mSize * 8, type->mAlign * 8, memberPositions[i] * 8, memberFlags, mBfIRBuilder->DbgGetType(type));
					diFieldTypes.push_back(memberType);
				}
			}

			int diFlags = 0;
			mBfIRBuilder->DbgMakePermanent(diForwardDecl, BfIRMDNode(), diFieldTypes);
			deferDIType = mBfIRBuilder->DbgCreatePointerType(diForwardDecl);
			if (deferredMethodCallData != NULL)
				deferredMethodCallData->mDeferDIType = deferDIType;
		}

		// We don't actually want to see this, and it doesn't emit properly in LLVM CodeView anyway - it only accepts static allocs,
		//  not dynamic allocas
		String varName = StrFormat("$__deferredCall_%d", mCurMethodState->mDeferredLoopListEntryCount);
		mCurMethodState->mDeferredLoopListEntryCount++;
		auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
			varName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, deferDIType);
		mBfIRBuilder->DbgInsertDeclare(deferredAlloca, diVariable);
	}

	return true;
}

BfDeferredCallEntry* BfModule::AddDeferredBlock(BfBlock* block, BfScopeData* scopeData, Array<BfDeferredCapture>* captures)
{
	BfDeferredCallEntry* deferredCallEntry = new BfDeferredCallEntry();
	deferredCallEntry->mDeferredBlock = block;
	if (captures != NULL)
		deferredCallEntry->mCaptures = *captures;
	AddDeferredCallEntry(deferredCallEntry, scopeData);
	return deferredCallEntry;
}

BfDeferredCallEntry* BfModule::AddDeferredCall(const BfModuleMethodInstance& moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfScopeData* scopeData, BfAstNode* srcNode, bool bypassVirtual, bool doNullCheck)
{
	BfDeferredCallEntry* deferredCallEntry = new BfDeferredCallEntry();
	BF_ASSERT(moduleMethodInstance);
	deferredCallEntry->mModuleMethodInstance = moduleMethodInstance;

	for (auto arg : llvmArgs)
	{
		deferredCallEntry->mScopeArgs.push_back(arg);
	}
	deferredCallEntry->mSrcNode = srcNode;
	deferredCallEntry->mBypassVirtual = bypassVirtual;
	deferredCallEntry->mDoNullCheck = doNullCheck;
	if (!AddDeferredCallEntry(deferredCallEntry, scopeData))
		return NULL;
	return deferredCallEntry;
}

void BfModule::EmitDeferredCall(BfModuleMethodInstance moduleMethodInstance, SizedArrayImpl<BfIRValue>& llvmArgs, BfDeferredBlockFlags flags)
{
	if (moduleMethodInstance.mMethodInstance->GetOwner()->IsInstanceOf(mCompiler->mInternalTypeDef))
	{
		if (moduleMethodInstance.mMethodInstance->mMethodDef->mName.StartsWith("SetDeleted"))
		{
			intptr typeSize = 0;
			intptr typeAlign = 1;
			intptr clearSize = 0;
			bool isDynSize = false;

			bool mayBeZero = false;
			auto ptrValue = llvmArgs[0];
			BfIRValue arraySize;

			if ((moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted") ||
				(moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeletedArray"))
			{
				auto constant = mBfIRBuilder->GetConstant(llvmArgs[1]);
				if (constant != NULL)
					typeSize = constant->mInt64;
				constant = mBfIRBuilder->GetConstant(llvmArgs[2]);
				if (constant != NULL)
					typeAlign = constant->mInt64;
				if (llvmArgs.size() >= 4)
					arraySize = llvmArgs[3];

				intptr allocSize = typeSize;
				if (arraySize)
				{
					allocSize = BF_ALIGN(typeSize, typeAlign);
					auto constant = mBfIRBuilder->GetConstant(arraySize);
					if (constant != NULL)
						allocSize = allocSize * (intptr)constant->mInt64;
					else
					{
						isDynSize = true;
						mayBeZero = true;
					}
				}

				clearSize = BF_MIN(allocSize, mSystem->mPtrSize);
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeletedX")
			{
				// Note: this infers that mayBeZero is false still, because the deferred call would not have
				//  been added if the array size was zero
				typeSize = 1;
				clearSize = typeSize;
				arraySize = llvmArgs[1];
				isDynSize = true;
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted1")
			{
				clearSize = 1;
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted2")
			{
				clearSize = 2;
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted4")
			{
				clearSize = 4;
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted8")
			{
				clearSize = 8;
			}
			else if (moduleMethodInstance.mMethodInstance->mMethodDef->mName == "SetDeleted16")
			{
				clearSize = 16;
			}

			if (clearSize > 0)
			{
				BfTypeCode clearTypeCode = BfTypeCode_Int8;
				if (clearSize >= mSystem->mPtrSize)
					clearTypeCode = BfTypeCode_IntPtr;
				else if (clearSize >= 4)
					clearTypeCode = BfTypeCode_Int32;
				else if (clearSize >= 2)
					clearTypeCode = BfTypeCode_Int16;

				auto intType = GetPrimitiveType(clearTypeCode);
				auto intPtrType = CreatePointerType(intType);

				if (isDynSize)
				{
					if (clearSize >= mSystem->mPtrSize)
					{
						auto ddSize1Block = mBfIRBuilder->CreateBlock("DDSize1");
						auto ddDoneBlock = mBfIRBuilder->CreateBlock("DDDone");

						auto cmp = mBfIRBuilder->CreateCmpGT(arraySize, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), true);
						mBfIRBuilder->CreateCondBr(cmp, ddSize1Block, ddDoneBlock);

						mBfIRBuilder->AddBlock(ddSize1Block);
						mBfIRBuilder->SetInsertPoint(ddSize1Block);
						auto intPtrVal = mBfIRBuilder->CreateBitCast(ptrValue, mBfIRBuilder->MapType(intPtrType));
						mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(clearTypeCode, (uint64)0xDDDDDDDDDDDDDDDDULL), intPtrVal);
						mBfIRBuilder->CreateBr(ddDoneBlock);

						mBfIRBuilder->AddBlock(ddDoneBlock);
						mBfIRBuilder->SetInsertPoint(ddDoneBlock);

						if ((flags & BfDeferredBlockFlag_MoveNewBlocksToEnd) != 0)
						{
							mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddSize1Block);
							mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddDoneBlock);
						}
					}
					else
					{
						// If we allocate at least this many then we can do an IntPtr-sized marking, otherwise just one element's worth
						int intPtrCount = (int)((mSystem->mPtrSize + typeSize - 1) / typeSize);

						BfIRBlock ddSizePtrBlock = mBfIRBuilder->CreateBlock("DDSizePtr");
						BfIRBlock ddCheck1Block = mBfIRBuilder->CreateBlock("DDCheck1");
						BfIRBlock ddSize1Block;
						if (mayBeZero)
							ddSize1Block = mBfIRBuilder->CreateBlock("DDSize1");
						BfIRBlock ddDoneBlock = mBfIRBuilder->CreateBlock("DDDone");

						auto intptrType = GetPrimitiveType(BfTypeCode_IntPtr);
						auto intptrPtrType = CreatePointerType(intptrType);

						auto cmpPtr = mBfIRBuilder->CreateCmpGTE(arraySize, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, intPtrCount), true);
						mBfIRBuilder->CreateCondBr(cmpPtr, ddSizePtrBlock, ddCheck1Block);

						mBfIRBuilder->AddBlock(ddSizePtrBlock);
						mBfIRBuilder->SetInsertPoint(ddSizePtrBlock);
						auto intptrPtrVal = mBfIRBuilder->CreateBitCast(ptrValue, mBfIRBuilder->MapType(intptrPtrType));
						mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)0xDDDDDDDDDDDDDDDDULL), intptrPtrVal);
						mBfIRBuilder->CreateBr(ddDoneBlock);

						mBfIRBuilder->AddBlock(ddCheck1Block);
						mBfIRBuilder->SetInsertPoint(ddCheck1Block);
						if (mayBeZero)
						{
							auto cmp1 = mBfIRBuilder->CreateCmpGT(arraySize, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 0), true);
							mBfIRBuilder->CreateCondBr(cmp1, ddSize1Block, ddDoneBlock);

							mBfIRBuilder->AddBlock(ddSize1Block);
							mBfIRBuilder->SetInsertPoint(ddSize1Block);
						}
						auto intPtrVal = mBfIRBuilder->CreateBitCast(ptrValue, mBfIRBuilder->MapType(intPtrType));
						mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(clearTypeCode, (uint64)0xDDDDDDDDDDDDDDDDULL), intPtrVal);
						mBfIRBuilder->CreateBr(ddDoneBlock);

						mBfIRBuilder->AddBlock(ddDoneBlock);
						mBfIRBuilder->SetInsertPoint(ddDoneBlock);

						if ((flags & BfDeferredBlockFlag_MoveNewBlocksToEnd) != 0)
						{
							mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddSizePtrBlock);
							mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddCheck1Block);
							if (mayBeZero)
								mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddSize1Block);
							mCurMethodState->mCurScope->mAtEndBlocks.push_back(ddDoneBlock);
						}
					}
				}
				else
				{
					auto intPtrVal = mBfIRBuilder->CreateBitCast(ptrValue, mBfIRBuilder->MapType(intPtrType));
					mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(clearTypeCode, (uint64)0xDDDDDDDDDDDDDDDDULL), intPtrVal);
				}
			}
			return;
		}
	}

	if (moduleMethodInstance.mMethodInstance == mContext->mValueTypeDeinitSentinel)
	{
		BF_ASSERT(llvmArgs.size() == 3);
		auto sizeConstant = mBfIRBuilder->GetConstant(llvmArgs[1]);
		int clearSize = BF_MIN(sizeConstant->mInt32, 32);

		auto alignConstant = mBfIRBuilder->GetConstant(llvmArgs[2]);
		int clearAlign = alignConstant->mInt32;

		mBfIRBuilder->CreateMemSet(llvmArgs[0], GetConstValue8(0xDD), GetConstValue(clearSize), clearAlign);
		return;
	}

	auto methodInstance = moduleMethodInstance.mMethodInstance;
	auto methodOwner = methodInstance->mMethodInstanceGroup->mOwner;
	bool isDtor = methodInstance->mMethodDef->mMethodType == BfMethodType_Dtor;
	bool isScopeDtor = isDtor && ((flags & BfDeferredBlockFlag_BypassVirtual) != 0);

	if ((isDtor) && (methodInstance->GetParamCount() != 0))
	{
		// Dtor declared with params
		AssertErrorState();
		return;
	}

	BfIRBlock nullLabel;
	BfIRBlock notNullLabel;
	if ((flags & BfDeferredBlockFlag_DoNullChecks) != 0)
	{
		nullLabel = mBfIRBuilder->CreateBlock("deferred.isNull");
		notNullLabel = mBfIRBuilder->CreateBlock("deferred.notNull");

		auto notNullVal = mBfIRBuilder->CreateIsNotNull(llvmArgs[0]);
		mBfIRBuilder->CreateCondBr(notNullVal, notNullLabel, nullLabel);

		mBfIRBuilder->AddBlock(notNullLabel);
		mBfIRBuilder->SetInsertPoint(notNullLabel);
	}

	bool skipAccessCheck = false;
	if ((flags & BfDeferredBlockFlag_SkipObjectAccessCheck) != 0)
		skipAccessCheck = true;

	if ((!methodInstance->mMethodDef->mIsStatic) && (methodOwner->IsObjectOrInterface()) && (!isScopeDtor) &&
		(!skipAccessCheck))
	{
		EmitObjectAccessCheck(BfTypedValue(llvmArgs[0], methodOwner));
	}

	BfExprEvaluator expressionEvaluator(this);
	expressionEvaluator.CreateCall(NULL, moduleMethodInstance.mMethodInstance, moduleMethodInstance.mFunc, ((flags & BfDeferredBlockFlag_BypassVirtual) != 0), llvmArgs);

	if ((flags & BfDeferredBlockFlag_DoNullChecks) != 0)
	{
		mBfIRBuilder->CreateBr(nullLabel);

		mBfIRBuilder->AddBlock(nullLabel);
		mBfIRBuilder->SetInsertPoint(nullLabel);

		if (!mBfIRBuilder->mIgnoreWrites)
		{
			if ((flags & BfDeferredBlockFlag_MoveNewBlocksToEnd) != 0)
			{
				mCurMethodState->mCurScope->mAtEndBlocks.push_back(notNullLabel);
				mCurMethodState->mCurScope->mAtEndBlocks.push_back(nullLabel);
			}
		}
	}
}

void BfModule::EmitDeferredCallProcessorInstances(BfScopeData* scopeData)
{
	BF_ASSERT(scopeData->mDone);

	for (auto& deferredProcessor : scopeData->mDeferredCallProcessorInstances)
	{
		SetAndRestoreValue<bool> prevHadReturn(mCurMethodState->mHadReturn, false);
		SetAndRestoreValue<bool> prevInPostReturn(mCurMethodState->mInPostReturn, false);
		SetAndRestoreValue<bool> prevLeftBlockUncond(mCurMethodState->mLeftBlockUncond, false);

		auto prevBlock = mBfIRBuilder->GetInsertBlock();

		mBfIRBuilder->AddBlock(deferredProcessor.mProcessorBlock);
		mBfIRBuilder->SetInsertPoint(deferredProcessor.mProcessorBlock);
		EmitDeferredCallProcessor(scopeData, deferredProcessor.mDeferredCallEntry->mDynList, deferredProcessor.mDeferredCallEntry->mDynCallTail);
		mBfIRBuilder->CreateBr(deferredProcessor.mContinueBlock);

		mBfIRBuilder->SetInsertPoint(prevBlock);
	}
}

void BfModule::EmitDeferredCall(BfScopeData* scopeData, BfDeferredCallEntry& deferredCallEntry, bool moveBlocks)
{
	if ((mCompiler->mIsResolveOnly) && (!mIsComptimeModule) && (deferredCallEntry.mHandlerCount > 0))
	{
		// We only want to process deferred blocks once, otherwise it could significantly slow down autocompletion
		return;
	}

	deferredCallEntry.mHandlerCount++;

	if (deferredCallEntry.IsDynList())
	{
		if (scopeData->mDone)
		{
			EmitDeferredCallProcessor(scopeData, deferredCallEntry.mDynList, deferredCallEntry.mDynCallTail);
		}
		else
		{
			BfDeferredCallProcessorInstance deferredProcessor;
			deferredProcessor.mProcessorBlock = mBfIRBuilder->CreateBlock("dyn.processor");
			deferredProcessor.mContinueBlock = mBfIRBuilder->CreateBlock("dyn.continue");
			deferredProcessor.mDeferredCallEntry = &deferredCallEntry;
			scopeData->mDeferredCallProcessorInstances.Add(deferredProcessor);
			mBfIRBuilder->CreateBr(deferredProcessor.mProcessorBlock);

			mBfIRBuilder->AddBlock(deferredProcessor.mContinueBlock);
			mBfIRBuilder->SetInsertPoint(deferredProcessor.mContinueBlock);
			SetIllegalSrcPos();
			EmitEnsureInstructionAt();
		}

		return;
	}

	if (deferredCallEntry.mDeferredBlock != NULL)
	{
		// Only show warnings on the first pass
		// For errors, show on the first pass OR as long as we haven't gotten any errors within this method.  I'm not sure if there's a case
		//  where the first emission succeeds but a subsequent one would fail, but we leave this logic to handle that possibility
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, (deferredCallEntry.mHandlerCount > 1) && (mCurMethodInstance->mHasFailed));
		SetAndRestoreValue<bool> prevIgnoreWarnings(mIgnoreWarnings, (deferredCallEntry.mHandlerCount > 1));

		BfScopeData scopeData;
		mCurMethodState->AddScope(&scopeData);
		NewScopeState();
		for (auto& capture : deferredCallEntry.mCaptures)
		{
			BfLocalVariable* localVar = new BfLocalVariable();
			localVar->mIsReadOnly = true;
			localVar->mAssignedKind = BfLocalVarAssignKind_Unconditional;
			localVar->mReadFromId = 0;
			localVar->mName = capture.mName;
			localVar->mValue = capture.mValue.mValue;
			localVar->mResolvedType = capture.mValue.mType;
			if ((mBfIRBuilder->DbgHasInfo()) && (!localVar->mResolvedType->IsValuelessType()))
			{
				auto addr = CreateAlloca(localVar->mResolvedType);
				mBfIRBuilder->CreateAlignedStore(localVar->mValue, addr, localVar->mResolvedType->mAlign);
				localVar->mAddr = addr;
			}
			AddLocalVariableDef(localVar, true);
		}

		SetAndRestoreValue<BfAstNode*> prevCustomAttribute(mCurMethodState->mEmitRefNode, deferredCallEntry.mEmitRefNode);
		VisitEmbeddedStatement(deferredCallEntry.mDeferredBlock, NULL, BfEmbeddedStatementFlags_IsDeferredBlock);
		RestoreScopeState();
		return;
	}

	auto args = deferredCallEntry.mScopeArgs;
	if (deferredCallEntry.mArgsNeedLoad)
	{
		for (auto& arg : args)
		{
			if (!arg.IsConst())
				arg = mBfIRBuilder->CreateLoad(arg);
		}
	}
	if (deferredCallEntry.mCastThis)
	{
		args[0] = mBfIRBuilder->CreateBitCast(args[0], mBfIRBuilder->MapTypeInstPtr(deferredCallEntry.mModuleMethodInstance.mMethodInstance->GetOwner()));
	}

	BfDeferredBlockFlags flags = BfDeferredBlockFlag_None;
	if (deferredCallEntry.mBypassVirtual)
		flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_BypassVirtual);
	if (deferredCallEntry.mDoNullCheck)
		flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_DoNullChecks | BfDeferredBlockFlag_SkipObjectAccessCheck | BfDeferredBlockFlag_MoveNewBlocksToEnd);
	if (moveBlocks)
		flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_MoveNewBlocksToEnd);

	EmitDeferredCall(deferredCallEntry.mModuleMethodInstance, args, flags);
}

void BfModule::EmitDeferredCallProcessor(BfScopeData* scopeData, SLIList<BfDeferredCallEntry*>& callEntries, BfIRValue callTail)
{
	int64 collisionId = 0;

	struct _CallInfo
	{
		BfModuleMethodInstance mModuleMethodInstance;
		bool mBypassVirtual;
	};

	//typedef std::map<int64, _CallInfo> MapType;
	//MapType methodInstanceMap;
	Dictionary<int64, _CallInfo> methodInstanceMap;
	int blockCount = 0;

	HashSet<BfMethodInstance*> nullCheckMethodSet;

	BfDeferredCallEntry* deferredCallEntry = callEntries.mHead;
	while (deferredCallEntry != NULL)
	{
		BfModuleMethodInstance moduleMethodInstance = deferredCallEntry->mModuleMethodInstance;
		int64 methodId = 0;
		if (moduleMethodInstance.mMethodInstance != NULL)
		{
			int64 idHash = moduleMethodInstance.mMethodInstance->mIdHash;
			auto deferredMethodCallData = mDeferredMethodCallData[moduleMethodInstance.mMethodInstance];
			BF_ASSERT(deferredMethodCallData->mMethodId != 0);
			//methodInstanceMap[deferredMethodCallData->mMethodId] = moduleMethodInstance;
			_CallInfo* callInfo = NULL;
			if (methodInstanceMap.TryAdd(deferredMethodCallData->mMethodId, NULL, &callInfo))
			{
				callInfo->mModuleMethodInstance = moduleMethodInstance;
				callInfo->mBypassVirtual = deferredCallEntry->mBypassVirtual;
			}
			else
			{
				// Only bypass virtual if ALL these calls are devirtualized
				callInfo->mBypassVirtual &= deferredCallEntry->mBypassVirtual;
			}
		}
		else
			blockCount++;
		if (deferredCallEntry->mDoNullCheck)
			nullCheckMethodSet.Add(deferredCallEntry->mModuleMethodInstance.mMethodInstance);
		deferredCallEntry = deferredCallEntry->mNext;
	}

	bool moveBlocks = mCurMethodState->mCurScope != mCurMethodState->mTailScope;

	auto valueScopeStart = ValueScopeStart();
	if (valueScopeStart)
		mBfIRBuilder->SetName(valueScopeStart, "deferredScopeVal");

	BfIRBlock condBB = mBfIRBuilder->CreateBlock("deferCall.cond", true);
	if (moveBlocks)
		mCurMethodState->mCurScope->mAtEndBlocks.push_back(condBB);
	mBfIRBuilder->CreateBr(condBB);

	auto deferredCallEntryType = ResolveTypeDef(mCompiler->mDeferredCallTypeDef);
	auto deferredCallEntryTypePtr = CreatePointerType(deferredCallEntryType);

	BfIRBlock bodyBB = mBfIRBuilder->CreateBlock("deferCall.body");
	if (moveBlocks)
		mCurMethodState->mCurScope->mAtEndBlocks.push_back(bodyBB);
	BfIRBlock endBB = mBfIRBuilder->CreateBlock("deferCall.end");
	if (moveBlocks)
		mCurMethodState->mCurScope->mAtEndBlocks.push_back(endBB);

	BfIRBlock exitBB = endBB;

	BfIRValue deferredCallTail;

	mBfIRBuilder->SetInsertPoint(condBB);
	deferredCallTail = mBfIRBuilder->CreateLoad(callTail);
	auto isNotNull = mBfIRBuilder->CreateIsNotNull(deferredCallTail);
	ValueScopeEnd(valueScopeStart);
	mBfIRBuilder->CreateCondBr(isNotNull, bodyBB, exitBB);

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);

	BfIRValue switchInst;
	bool wantsSwitch = ((int)methodInstanceMap.size() + blockCount) > 1;
	if (blockCount > 0)
	{
		// A block may embed a switch so we need a switch whenever we have blocks
		wantsSwitch = true;
	}
	if (mCurMethodState->mCancelledDeferredCall)
		wantsSwitch = true;
	if (wantsSwitch)
	{
		if (IsTargetingBeefBackend())
			deferredCallTail = mBfIRBuilder->CreateLoad(callTail);
		auto idPtr = mBfIRBuilder->CreateInBoundsGEP(deferredCallTail, 0, 1); // mMethodId
		auto id = mBfIRBuilder->CreateLoad(idPtr);
		switchInst = mBfIRBuilder->CreateSwitch(id, exitBB, (int)methodInstanceMap.size());
		ValueScopeEnd(valueScopeStart);
	}

	BfDeferredCallEntry* prevHead = callEntries.mHead;
	BfDeferredCallEntry* prevCallEntry = NULL;

	HashSet<BfDeferredCallEntry*> handledSet;

	deferredCallEntry = callEntries.mHead;
	while (deferredCallEntry != NULL)
	{
		auto block = deferredCallEntry->mDeferredBlock;
		if (block == NULL)
		{
			deferredCallEntry = deferredCallEntry->mNext;
			continue;
		}
		int64 blockId = deferredCallEntry->mBlockId;

		//auto itr = handledSet.insert(deferredCallEntry);
		//if (!itr.second)

		if (!handledSet.Add(deferredCallEntry))
		{
			// Already handled, can happen if we defer again within the block
			deferredCallEntry = deferredCallEntry->mNext;
			continue;
		}

		if (switchInst)
		{
			String caseName = StrFormat("deferCall.%s", BfTypeUtils::HashEncode64(blockId).c_str());
			auto caseBB = mBfIRBuilder->CreateBlock(caseName, true);
			if (moveBlocks)
				mCurMethodState->mCurScope->mAtEndBlocks.push_back(caseBB);
			mBfIRBuilder->AddSwitchCase(switchInst, GetConstValue64(blockId), caseBB);
			mBfIRBuilder->SetInsertPoint(caseBB);
		}

		// Update .mDeferredAlloca to use the deferredCallTail
		if (IsTargetingBeefBackend())
			deferredCallTail = mBfIRBuilder->CreateLoad(callTail);
		auto nextPtr = mBfIRBuilder->CreateInBoundsGEP(deferredCallTail, 0, 2); // mNext
		auto next = mBfIRBuilder->CreateLoad(nextPtr);
		mBfIRBuilder->CreateStore(next, callTail);
		deferredCallEntry->mDeferredAlloca = mBfIRBuilder->CreateBitCast(deferredCallTail, mBfIRBuilder->GetType(deferredCallEntry->mDeferredAlloca));

		int dataIdx = 3;

		// Update .mCaptures to contain the stored values at the time the defer occurred
		for (int captureIdx = 0; captureIdx < (int)deferredCallEntry->mCaptures.size(); captureIdx++)
		{
			auto& capture = deferredCallEntry->mCaptures[captureIdx];
			if (!capture.mValue.mType->IsValuelessType())
			{
				auto gepInstance = mBfIRBuilder->CreateInBoundsGEP(deferredCallEntry->mDeferredAlloca, 0, dataIdx);
				capture.mValue.mValue = mBfIRBuilder->CreateLoad(gepInstance);
				dataIdx++;
			}
		}

		auto prevHead = callEntries.mHead;
		EmitDeferredCall(scopeData, *deferredCallEntry, moveBlocks);
		ValueScopeEnd(valueScopeStart);
		mBfIRBuilder->CreateBr(condBB);

		if (prevHead != callEntries.mHead)
		{
			// The list changed, start over and ignore anything we've already handled
			deferredCallEntry = callEntries.mHead;
		}
		else
			deferredCallEntry = deferredCallEntry->mNext;
	}

	// Blocks may have added new method types, so rebuild map
	if (blockCount > 0)
	{
		deferredCallEntry = callEntries.mHead;
		while (deferredCallEntry != NULL)
		{
			BfModuleMethodInstance moduleMethodInstance = deferredCallEntry->mModuleMethodInstance;
			if (moduleMethodInstance.mMethodInstance != NULL)
			{
				auto deferredMethodCallData = mDeferredMethodCallData[moduleMethodInstance.mMethodInstance];
				//methodInstanceMap.insert(MapType::value_type(deferredMethodCallData->mMethodId, moduleMethodInstance));
				_CallInfo* callInfo = NULL;
				if (methodInstanceMap.TryAdd(deferredMethodCallData->mMethodId, NULL, &callInfo))
				{
					callInfo->mModuleMethodInstance = moduleMethodInstance;
					callInfo->mBypassVirtual = deferredCallEntry->mBypassVirtual;
				}
			}
			deferredCallEntry = deferredCallEntry->mNext;
		}
	}

	BfExprEvaluator exprEvaluator(this);
	//for (auto itr = methodInstanceMap.begin(); itr != methodInstanceMap.end(); ++itr)
	for (auto& callInfoKV : methodInstanceMap)
	{
		auto moduleMethodInstance = callInfoKV.mValue.mModuleMethodInstance;
		bool bypassVirtual = callInfoKV.mValue.mBypassVirtual;
		auto methodInstance = moduleMethodInstance.mMethodInstance;
		auto methodDef = methodInstance->mMethodDef;
		auto methodOwner = methodInstance->mMethodInstanceGroup->mOwner;
		BfIRValue deferredCallInst = deferredCallTail;

		auto deferredMethodCallData = mDeferredMethodCallData[methodInstance];
		int64 methodId = deferredMethodCallData->mMethodId;

		if (switchInst)
		{
			String caseName = StrFormat("deferCall.%s", BfTypeUtils::HashEncode64(methodId).c_str());
			auto caseBB = mBfIRBuilder->CreateBlock(caseName, true);
			if (moveBlocks)
				mCurMethodState->mCurScope->mAtEndBlocks.push_back(caseBB);
			mBfIRBuilder->AddSwitchCase(switchInst, GetConstValue64(methodId), caseBB);
			mBfIRBuilder->SetInsertPoint(caseBB);
		}

		if (IsTargetingBeefBackend())
			deferredCallTail = mBfIRBuilder->CreateLoad(callTail);
		auto nextPtr = mBfIRBuilder->CreateInBoundsGEP(deferredCallTail, 0, 2); // mNext
		auto next = mBfIRBuilder->CreateLoad(nextPtr);
		mBfIRBuilder->CreateStore(next, callTail);
		deferredCallInst = mBfIRBuilder->CreateBitCast(deferredCallTail, deferredMethodCallData->mDeferTypePtr);

		int paramIdx = 0;
		if (!methodDef->mIsStatic)
			paramIdx = -1;

		SizedArray<BfIRValue, 8> llvmArgs;
		for (int argIdx = 0; paramIdx < methodInstance->GetParamCount(); argIdx++, paramIdx++)
		{
			auto argPtr = mBfIRBuilder->CreateInBoundsGEP(deferredCallInst, 0, argIdx + 2);
			bool isStruct = false;

			bool doSplat = methodInstance->GetParamIsSplat(paramIdx);;
			BfTypedValue typedVal;
			if (paramIdx == -1)
			{
				typedVal = BfTypedValue(argPtr, methodOwner, true);
			}
			else
			{
				auto paramType = methodInstance->GetParamType(paramIdx);
				typedVal = BfTypedValue(argPtr, paramType, true);
			}

			if (doSplat)
			{
				exprEvaluator.SplatArgs(typedVal, llvmArgs);
				continue;
			}

			if ((argIdx == 0) && (!methodDef->mIsStatic))
			{
				// 'this'
				isStruct = methodOwner->IsStruct();
			}
			else
			{
				while (methodInstance->IsParamSkipped(paramIdx))
					paramIdx++;
				if (paramIdx >= methodInstance->GetParamCount())
					break;
				auto paramType = methodInstance->GetParamType(paramIdx);
				isStruct = paramType->IsStruct();
			}

			if (isStruct)
			{
				llvmArgs.push_back(argPtr);
			}
			else
			{
				auto arg = mBfIRBuilder->CreateLoad(argPtr);
				llvmArgs.push_back(arg);
			}
		}

		BfDeferredBlockFlags flags = BfDeferredBlockFlag_None;
		if (moveBlocks)
			flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_MoveNewBlocksToEnd);
		if (nullCheckMethodSet.Contains(moduleMethodInstance.mMethodInstance))
			flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_DoNullChecks | BfDeferredBlockFlag_SkipObjectAccessCheck);
		if (bypassVirtual)
			flags = (BfDeferredBlockFlags)(flags | BfDeferredBlockFlag_BypassVirtual);
		EmitDeferredCall(moduleMethodInstance, llvmArgs, flags);
		ValueScopeEnd(valueScopeStart);
		mBfIRBuilder->CreateBr(condBB);
	}

	if (endBB)
	{
		mBfIRBuilder->AddBlock(endBB);
		mBfIRBuilder->SetInsertPoint(endBB);
	}
}

void BfModule::TryInitVar(BfAstNode* checkNode, BfLocalVariable* localVar, BfTypedValue initValue, BfTypedValue& checkResult)
{
	BF_ASSERT(!localVar->mAddr);
	localVar->mAddr = AllocLocalVariable(localVar->mResolvedType, localVar->mName);

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

	auto varType = localVar->mResolvedType;

	AddDependency(varType, mCurTypeInstance, BfDependencyMap::DependencyFlag_ExprTypeReference);

	if (!initValue)
	{
		AssertErrorState();
		checkResult = BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), boolType);
		return;
	}
	auto initType = initValue.mType;

	bool isDynamicCast = false;

	if (varType->IsGenericParam())
	{
		int pass = 0;
		while (varType->IsGenericParam())
		{
			auto genericParamType = (BfGenericParamType*)varType;

			auto genericParam = GetGenericParamInstance(genericParamType);
			auto typeConstraint = genericParam->mTypeConstraint;
			if ((typeConstraint == NULL) && (genericParam->mGenericParamFlags & (BfGenericParamFlag_Class | BfGenericParamFlag_Interface)))
				typeConstraint = mContext->mBfObjectType;
			if (typeConstraint != NULL)
				varType = typeConstraint;
			else
				break;
			if (++pass >= 100) // Sanity - but we should have caught circular error before
				break;
		}
		initValue = GetDefaultTypedValue(varType, false, BfDefaultValueKind_Undef);
	}

	BfTypeInstance* srcTypeInstance = initValue.mType->ToTypeInstance();
	BfTypeInstance* varTypeInstance = varType->ToTypeInstance();

	if (CanCast(initValue, varType))
	{
		if ((!varType->IsPointer()) && (!varType->IsObjectOrInterface()) && (!varType->IsVar()))
		{
			if (!IsInSpecializedSection())
			{
				if (initValue.mType != varType)
					Warn(BfWarning_CS0472_ValueTypeNullCompare, StrFormat("Variable declaration is always 'true' because static cast cannot fail and a value of type '%s' can never be null",
						TypeToString(varType).c_str()), checkNode);
				else
					Warn(BfWarning_CS0472_ValueTypeNullCompare, StrFormat("Variable declaration is always 'true' because a value of type '%s' can never be null",
						TypeToString(varType).c_str()), checkNode);
			}
		}
	}
// 	else if ((initType->IsInterface()) || (initType == mContext->mBfObjectType))
// 	{
// 		// Interface or System.Object -> *
// 		isDynamicCast = true;
// 	}
// 	else if ((srcTypeInstance != NULL) && (varTypeInstance != NULL) &&
// 		((srcTypeInstance->IsObject()) && (TypeIsSubTypeOf(varTypeInstance, srcTypeInstance))))
// 	{
// 		// Class downcast
// 		isDynamicCast = true;
// 	}
// 	else if ((!CanCast(GetFakeTypedValue(varType), initType)) && (!initType->IsGenericParam()))
// 	{
// 		if (!IsInSpecializedSection())
// 		{
// 			Fail(StrFormat("Cannot convert type '%s' to '%s' via any conversion",
// 				TypeToString(initValue.mType).c_str(), TypeToString(varType).c_str()), checkNode);
// 		}
// 	}

	if (!isDynamicCast)
	{
		//initValue = Cast(checkNode, initValue, varType, (BfCastFlags)(BfCastFlags_Explicit | BfCastFlags_SilentFail));
		initValue = Cast(checkNode, initValue, varType);

		if (!initValue)
		{
			checkResult = BfTypedValue(GetConstValue(0, boolType), boolType);
		}
		else
		{
			if (localVar->mAddr)
			{
				initValue = LoadValue(initValue);
				if (!initValue.mType->IsVar())
					mBfIRBuilder->CreateAlignedStore(initValue.mValue, localVar->mAddr, initValue.mType->mAlign);
			}

			if (varType->IsVar())
			{
				checkResult = GetDefaultTypedValue(boolType, false, BfDefaultValueKind_Undef);
			}
			else if ((varType->IsPointer()) || (varType->IsObjectOrInterface()))
			{
				checkResult = BfTypedValue(mBfIRBuilder->CreateIsNotNull(initValue.mValue), boolType);
			}
			else
			{
				checkResult = BfTypedValue(GetConstValue(1, boolType), boolType);
			}
		}

		return;
	}

	if (mCompiler->IsAutocomplete())
	{
		auto allocaResult = mBfIRBuilder->CreateAlloca(mBfIRBuilder->MapType(boolType));
		auto val = mBfIRBuilder->CreateLoad(allocaResult);
		checkResult = BfTypedValue(val, boolType);
		return;
	}

	int wantTypeId = 0;
	if (!varType->IsGenericParam())
		wantTypeId = varType->mTypeId;

	auto objectType = mContext->mBfObjectType;
	PopulateType(objectType, BfPopulateType_Full);

	initValue = LoadValue(initValue);

	auto prevBB = mBfIRBuilder->GetInsertBlock();
	auto matchBB = mBfIRBuilder->CreateBlock("is.match");
	auto endBB = mBfIRBuilder->CreateBlock("is.done");

	BfIRValue boolResult = CreateAlloca(boolType);
	mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), boolResult);

	EmitDynamicCastCheck(initValue, varType, matchBB, endBB);

	AddBasicBlock(matchBB);
	mBfIRBuilder->CreateStore(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), boolResult);
	mBfIRBuilder->CreateBr(endBB);

	AddBasicBlock(endBB);
	checkResult = BfTypedValue(mBfIRBuilder->CreateLoad(boolResult), boolType);
}

BfLocalVariable* BfModule::HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfExprEvaluator* exprEvaluator)
{
	if (mCurMethodState == NULL)
	{
		Fail("Invalid variable declaration", varDecl);
		return NULL;
	}

	BfAutoComplete* bfAutocomplete = NULL;

	// Just a check
	mBfIRBuilder->GetInsertBlock();

 	if (mCompiler->mResolvePassData != NULL)
		bfAutocomplete = mCompiler->mResolvePassData->mAutoComplete;
	if (bfAutocomplete != NULL)
		bfAutocomplete->CheckTypeRef(varDecl->mTypeRef, true, true);

	bool isConst = (varDecl->mModSpecifier != NULL) && (varDecl->mModSpecifier->GetToken() == BfToken_Const);
	bool isReadOnly = (varDecl->mModSpecifier != NULL) && (varDecl->mModSpecifier->GetToken() == BfToken_ReadOnly);
	bool isStatic = (varDecl->mModSpecifier != NULL) && (varDecl->mModSpecifier->GetToken() == BfToken_Static);

	BfLocalVariable* localDef = new BfLocalVariable();
	if (varDecl->mNameNode != NULL)
	{
		varDecl->mNameNode->ToString(localDef->mName);
		localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(varDecl->mNameNode);
	}
	else
	{
		localDef->mName = "val";
	}
	localDef->mIsStatic = isStatic;

	bool handledExprBoolResult = false;
	bool handledVarInit = false;
	bool handledVarStore = false;
	BfType* unresolvedType = NULL;
	BfType* resolvedType = NULL;
	BfTypedValue initValue;
	bool hadVarType = false;
	bool isLet = varDecl->mTypeRef->IsA<BfLetTypeReference>();
	bool initHandled = false;

	auto _DoConditionalInit = [&](BfType* expectedType)
	{
		auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

		auto _EmitCond = [&](BfIRValue condVal, BfTypedValue initValue)
		{
			initValue = Cast(varDecl->mInitializer, initValue, expectedType);
			if (!initValue)
				return;

			initHandled = true;

			if (localDef->mIsReadOnly)
			{
				if ((initValue.IsReadOnly()) ||
					(initValue.mKind == BfTypedValueKind_TempAddr))
				{
					localDef->mAddr = initValue.mValue;
					exprEvaluator->mResult = BfTypedValue(condVal, boolType);
					return;
				}
			}

			localDef->mAddr = AllocLocalVariable(resolvedType, localDef->mName);

			auto doAssignBlock = mBfIRBuilder->CreateBlock("assign");
			auto skipAssignBlock = mBfIRBuilder->CreateBlock("skipAssign");

			auto insertBlock = mBfIRBuilder->GetInsertBlock();
			mBfIRBuilder->CreateCondBr(condVal, doAssignBlock, skipAssignBlock);

			mBfIRBuilder->AddBlock(doAssignBlock);
			mBfIRBuilder->SetInsertPoint(doAssignBlock);

			initValue = LoadValue(initValue);
			mBfIRBuilder->CreateStore(initValue.mValue, localDef->mAddr);
			mBfIRBuilder->CreateBr(skipAssignBlock);

			mBfIRBuilder->AddBlock(skipAssignBlock);
			mBfIRBuilder->SetInsertPoint(skipAssignBlock);

			auto phiVal = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(boolType), 2);
			mBfIRBuilder->AddPhiIncoming(phiVal, mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), insertBlock);
			mBfIRBuilder->AddPhiIncoming(phiVal, mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), doAssignBlock);
			exprEvaluator->mResult = BfTypedValue(phiVal, boolType);
		};

		bool handled = false;
		if ((initValue) && (initValue.mType->IsPayloadEnum()))
		{
			auto typeInst = initValue.mType->ToTypeInstance();
			PopulateType(typeInst);
			BfType* outType = NULL;
			int tagId = -1;
			if (typeInst->GetResultInfo(outType, tagId))
			{
				int dscDataIdx = -1;
				auto dscType = typeInst->GetDiscriminatorType(&dscDataIdx);

				BfIRValue dscVal = ExtractValue(initValue, dscDataIdx);
				auto eqVal = mBfIRBuilder->CreateCmpEQ(dscVal, GetConstValue(tagId, dscType));
				exprEvaluator->mResult = BfTypedValue(eqVal, boolType);

				PopulateType(outType);
				if (!outType->IsValuelessType())
				{
					auto outPtrType = CreatePointerType(outType);

					initValue = MakeAddressable(initValue);
					auto payloadVal = mBfIRBuilder->CreateBitCast(initValue.mValue, mBfIRBuilder->MapType(outPtrType));
					auto payload = BfTypedValue(payloadVal, outType, true);
					if ((initValue.mKind == BfTypedValueKind_ReadOnlyAddr) ||
						(initValue.mKind == BfTypedValueKind_TempAddr) ||
						(initValue.mKind == BfTypedValueKind_ReadOnlyTempAddr))
						payload.mKind = initValue.mKind;
					_EmitCond(eqVal, payload);
				}

				handled = true;
			}
		}

		if (handled)
		{
			handledExprBoolResult = true;
			handledVarInit = true;
			handledVarStore = true;
		}
		else if ((initValue) && (initValue.mType->IsNullable()))
		{
			auto underlyingType = initValue.mType->GetUnderlyingType();
			exprEvaluator->mResult = BfTypedValue(ExtractValue(initValue, 2), boolType);
			handledExprBoolResult = true;

			if (!resolvedType->IsNullable())
			{
				if (initValue.IsAddr())
					initValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(initValue.mValue, 0, 1), initValue.mType->GetUnderlyingType(), true);
				else
					initValue = BfTypedValue(mBfIRBuilder->CreateExtractValue(initValue.mValue, 1), initValue.mType->GetUnderlyingType());
			}

			if ((initValue) && (!initValue.mType->IsValuelessType()))
			{
				_EmitCond(exprEvaluator->mResult.mValue, initValue);
				handledVarStore = true;
			}

			handledVarInit = true;
		}
		else if (initValue)
		{
			BfAstNode* refNode = varDecl;
			if (varDecl->mInitializer != NULL)
				refNode = varDecl->mInitializer;
			TryInitVar(refNode, localDef, initValue, exprEvaluator->mResult);
			handledExprBoolResult = true;
			handledVarInit = true;
			handledVarStore = true;
		}
	};

	if ((varDecl->mTypeRef->IsA<BfVarTypeReference>()) || (isLet))
	{
		hadVarType = true;
		if (varDecl->mInitializer == NULL)
		{
			if (!isLet)
			{
				BfLocalVarEntry* shadowEntry;
				if (mCurMethodState->mLocalVarSet.TryGet(BfLocalVarEntry(localDef), &shadowEntry))
				{
					auto prevLocal = shadowEntry->mLocalVar;
					if (prevLocal->mLocalVarIdx >= mCurMethodState->GetLocalStartIdx())
					{
						BfExprEvaluator exprEvaluator(this);
						initValue = exprEvaluator.LoadLocal(prevLocal);

						resolvedType = initValue.mType;
						unresolvedType = resolvedType;
						localDef->mLocalVarId = prevLocal->mLocalVarId;
						localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
						localDef->mIsShadow = true;

						exprEvaluator.mResultLocalVarRefNode = varDecl->mNameNode;
						exprEvaluator.mResultLocalVar = prevLocal;
						exprEvaluator.CheckResultForReading(initValue);

						if (bfAutocomplete != NULL)
							bfAutocomplete->CheckVarResolution(varDecl->mTypeRef, resolvedType);
					}
				}
			}

			if (!initValue)
			{
				Fail("Implicitly-typed variables must be initialized", varDecl);
				initValue = GetDefaultTypedValue(mContext->mBfObjectType);
			}
		}
		else
		{
			if (isConst)
			{
				BfConstResolver constResolver(this);
				initValue = constResolver.Resolve(varDecl->mInitializer);
			}
			else
			{
				BfExprEvaluator valExprEvaluator(this);
				valExprEvaluator.mAllowReadOnlyReference = isLet;
				initValue = CreateValueFromExpression(valExprEvaluator, varDecl->mInitializer, NULL, (BfEvalExprFlags)(BfEvalExprFlags_AllowSplat | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_VariableDeclaration));
				if ((exprEvaluator != NULL) && (initValue))
				{
					if (initValue.mType->IsNullable())
					{
						auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
						initValue = LoadValue(initValue);
						exprEvaluator->mResult = BfTypedValue(mBfIRBuilder->CreateExtractValue(initValue.mValue, 2), boolType);
						handledExprBoolResult = true;
						initValue = BfTypedValue(mBfIRBuilder->CreateExtractValue(initValue.mValue, 1), initValue.mType->GetUnderlyingType());
					}
 					else
 					{
 						auto typeInst = initValue.mType->ToTypeInstance();
 						if (typeInst != NULL)
 						{
 							PopulateType(typeInst);
 							BfType* outType = NULL;
 							int tagId = -1;
 							if (typeInst->GetResultInfo(outType, tagId))
 							{
  								handledExprBoolResult = true;
 								unresolvedType = outType;
								resolvedType = outType;
								isReadOnly = isLet;
								localDef->mIsReadOnly = isLet;
								_DoConditionalInit(outType);
 							}
 						}
 					}
				}
			}
		}
		if (!initValue)
		{
			initValue = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_Var));
		}
		if (initValue.mType->IsNull())
		{
			Fail("Implicitly-typed variables cannot be initialized to 'null'", varDecl->mInitializer);
			initValue = GetDefaultTypedValue(mContext->mBfObjectType);
		}
		if (unresolvedType == NULL)
			unresolvedType = initValue.mType;
		resolvedType = unresolvedType;

		if ((initValue.IsTempAddr()) && (!localDef->mAddr) && (initValue.mType == resolvedType))
		{
			// Take over value
			localDef->mAddr = initValue.mValue;
			handledVarInit = true;

			if (isLet)
			{
				localDef->mValue = mBfIRBuilder->CreateLoad(localDef->mAddr);
			}
		}

		if (bfAutocomplete != NULL)
			bfAutocomplete->CheckVarResolution(varDecl->mTypeRef, resolvedType);
	}
	else
	{
		BfTypeState typeState;
		typeState.mCurVarInitializer = varDecl->mInitializer;
		SetAndRestoreValue<BfTypeState*> prevTypeState(mContext->mCurTypeState, &typeState);

		BfResolveTypeRefFlags flags = (BfResolveTypeRefFlags)(BfResolveTypeRefFlag_NoResolveGenericParam | BfResolveTypeRefFlag_AllowRef);
		if (varDecl->mInitializer != NULL)
			flags = (BfResolveTypeRefFlags)(flags | BfResolveTypeRefFlag_AllowInferredSizedArray);

		unresolvedType = ResolveTypeRef(varDecl->mTypeRef, BfPopulateType_Data, flags);
		if (unresolvedType == NULL)
			unresolvedType = GetPrimitiveType(BfTypeCode_Var);
		resolvedType = unresolvedType;
	}

	auto _CheckConst = [&]
	{
		if (initValue.mValue.IsConst())
		{
			auto constant = mBfIRBuilder->GetConstant(initValue.mValue);

			// NullPtr is stand-in for GlobalVar during autocomplete
			if ((constant->mConstType == BfConstType_GlobalVar) ||
				(constant->mTypeCode == BfTypeCode_NullPtr))
			{
				// Not really constant
				// 			localNeedsAddr = false;
				// 			isConst = false;
				// 			initHandled = true;
				// 			localDef->mValue = initValue.mValue;
				if (GetStringPoolIdx(initValue.mValue, mBfIRBuilder) == -1)
					isConst = false;
			}
		}
	};

	localDef->mResolvedType = resolvedType;
	localDef->mIsReadOnly = isReadOnly;

	if (!initHandled)
	{
		if (isLet)
		{
			localDef->mIsReadOnly = true;
			if (initValue)
			{
				if ((initValue.mValue) && (initValue.mValue.IsConst()))
				{
					isConst = true;
				}
			}
		}
	}

	_CheckConst();

	bool localNeedsAddr = false;
	bool allowValueAccess = true;
	if (mHasFullDebugInfo)
	{
		//if (!IsTargetingBeefBackend())

		if (!isConst)
			localNeedsAddr = true;

		/*if (mCurMethodInstance->mMethodDef->mName != "Boop2")
			dbgNeedsAddr = true;*/
	}

	// This is required because of lifetime and LLVM domination rules for certain instances of variable declarations in binary conditionals.
	//   IE: if ((something) && (let a = somethingElse))
	if ((exprEvaluator != NULL) && (!isConst))
	{
		localNeedsAddr = true;
		allowValueAccess = false;
	}

	if ((varDecl->mEqualsNode != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) && (!initHandled))
	{
		mCompiler->mResolvePassData->mAutoComplete->CheckEmptyStart(varDecl->mEqualsNode, resolvedType);
	}

	BfIRInitType initType = BfIRInitType_NotSet;

	if (varDecl->mInitializer != NULL)
	{
		initType = BfIRInitType_NotNeeded_AliveOnDecl;

		if ((!initValue) && (!initHandled))
		{
			if ((isConst) || (isStatic))
			{
				BfConstResolver constResolver(this);
				initValue = constResolver.Resolve(varDecl->mInitializer, resolvedType, BfConstResolveFlag_ActualizeValues);
				if (!initValue)
					initValue = GetDefaultTypedValue(resolvedType);
			}
			else if (varDecl->mInitializer->IsA<BfUninitializedExpression>())
			{
				// Fake 'is assigned'
			}
			else
			{ //
				auto expectedType = resolvedType;
				if (expectedType->IsRef())
					expectedType = expectedType->GetUnderlyingType();

				BfExprEvaluator valExprEvaluator(this);
				valExprEvaluator.mAllowReadOnlyReference = isReadOnly;
				initValue = CreateValueFromExpression(valExprEvaluator, varDecl->mInitializer, expectedType, (BfEvalExprFlags)(BfEvalExprFlags_NoCast | BfEvalExprFlags_AllowRefExpr | BfEvalExprFlags_VariableDeclaration));
				
				if ((initValue) && (resolvedType->IsUndefSizedArray()))
				{
					int stringId = GetStringPoolIdx(initValue.mValue, mBfIRBuilder);
					if (stringId >= 0)
					{
						BfStringPoolEntry* entry = NULL;
						if (mContext->mStringObjectIdMap.TryGetValue(stringId, &entry))
							resolvedType = CreateSizedArrayType(((BfSizedArrayType*)resolvedType)->mElementType, entry->mString.mLength);
					}
				}

				auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

				if (exprEvaluator != NULL)
					_DoConditionalInit(expectedType);

				if ((!handledVarInit) && (initValue))
					initValue = Cast(varDecl->mInitializer, initValue, resolvedType, BfCastFlags_PreferAddr);
			}

			if ((initValue) && (resolvedType->IsUndefSizedArray()))
			{
				resolvedType = initValue.mType;
				unresolvedType = resolvedType;
			}
		}
		if ((!handledVarInit) && (!isConst))
		{
			if (initValue)
			{
				// Handled later
			}
			else if (varDecl->mInitializer->IsA<BfUninitializedExpression>())
			{
				// Fake 'is assigned'
				initType = BfIRInitType_Uninitialized;
			}
			else
			{
				AssertErrorState();
			}
		}

		localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
	}
	else
	{
		if (isConst)
		{
			Fail("Const locals must be initialized", varDecl->mModSpecifier);
			initValue = GetDefaultTypedValue(resolvedType, true);
		}
		else if (isReadOnly)
		{
			Fail("Readonly locals must be initialized", varDecl->mModSpecifier);
			initValue = GetDefaultTypedValue(resolvedType, true);
		}
		else if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(varDecl->mTypeRef))
		{
			Fail("Ref locals must be initialized", refTypeRef->mRefToken);
		}
		else
		{
			BF_ASSERT(!resolvedType->IsRef());
		}
	}

	PopulateType(resolvedType);
	AddDependency(resolvedType, mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

	localDef->mResolvedType = resolvedType;

	_CheckConst();

	if ((initValue.mKind == BfTypedValueKind_TempAddr) && (!initHandled))
	{
		BF_ASSERT(initValue.IsAddr());
		BF_ASSERT(initValue.mType->IsComposite());

		handledVarInit = true;
		handledVarStore = true;

		if (!localDef->mAddr)
		{
			localDef->mAddr = initValue.mValue;
			if (localDef->mIsReadOnly)
				localDef->mValue = mBfIRBuilder->CreateLoad(localDef->mAddr);
		}
		if (WantsLifetimes())
			mCurMethodState->mCurScope->mDeferredLifetimeEnds.push_back(localDef->mAddr);
	}

	if ((!localDef->mAddr) && (!isConst) && (!isStatic) && ((!localDef->mIsReadOnly) || (localNeedsAddr)))
	{
		if ((exprEvaluator != NULL) && (exprEvaluator->mResultIsTempComposite))
		{
			//TODO: Can we remove this one?
			BF_ASSERT(initValue.IsAddr());
			BF_ASSERT(initValue.mType->IsComposite());
			localDef->mAddr = initValue.mValue;
		}
		else
			localDef->mAddr = AllocLocalVariable(resolvedType, localDef->mName);
	}

	if (isStatic)
	{
		String name = mModuleName + "_" + mCurMethodInstance->mMethodDef->mName + "_" + localDef->mName;

		HashContext closureHashCtx;
		closureHashCtx.Mixin(varDecl->mSrcStart);
		uint64 closureHash = closureHashCtx.Finish64();
		name += "$";
		name += BfTypeUtils::HashEncode64(closureHash);

		initValue = LoadValue(initValue);
		if ((initValue) && (!initValue.mValue.IsConst()))
		{
			Fail("Static local variables can only be initialized with a const value", varDecl->mInitializer);
			initValue = BfTypedValue();
		}

		if (!initValue)
			initValue = GetDefaultTypedValue(localDef->mResolvedType);

		localDef->mAddr = mBfIRBuilder->CreateGlobalVariable(mBfIRBuilder->MapType(localDef->mResolvedType), false, BfIRLinkageType_Internal, initValue.mValue, name);;
		initHandled = true;
	}

	bool wantsStore = false;
	if ((initValue) && (!handledVarStore) && (!isConst) && (!initHandled))
	{
		initValue = LoadValue(initValue);
		if (initValue.IsSplat())
		{
			BF_ASSERT(!mIsComptimeModule);
			if (!localDef->mAddr)
				localDef->mAddr = AllocLocalVariable(resolvedType, localDef->mName);
			AggregateSplatIntoAddr(initValue, localDef->mAddr);
			initHandled = true;
		}
		else
		{
			initValue = AggregateSplat(initValue);
			localDef->mValue = initValue.mValue;
			if ((localDef->mAddr) && (!localDef->mResolvedType->IsValuelessType()))
			{
				if (!initValue.mType->IsVar())
					wantsStore = true;
			}
			else
			{
				BF_ASSERT(isReadOnly || isLet || initValue.mType->IsValuelessType() || (mBfIRBuilder->mIgnoreWrites));
			}
		}
	}

	if ((localDef->mIsReadOnly) && (!isConst) && (!localDef->mValue) && (!initHandled))
	{
		if (!resolvedType->IsValuelessType())
		{
			AssertErrorState();
			initValue = GetDefaultTypedValue(resolvedType, true, BfDefaultValueKind_Undef);
			localDef->mValue = initValue.mValue;
		}
	}

	if ((!localDef->mAddr) && (!isConst) && ((!localDef->mIsReadOnly) || (localNeedsAddr)))
	{
		localDef->mAddr = AllocLocalVariable(resolvedType, localDef->mName);
	}

	if ((exprEvaluator != NULL) && (!handledExprBoolResult))
	{
		auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
		if (!initValue)
		{
			AssertErrorState();
			exprEvaluator->mResult = BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), boolType);
		}
		else if ((resolvedType->IsPointer()) || (resolvedType->IsObjectOrInterface()))
		{
			exprEvaluator->mResult = BfTypedValue(mBfIRBuilder->CreateIsNotNull(initValue.mValue), boolType);
		}
		else if (resolvedType->IsVar())
		{
			exprEvaluator->mResult = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_Boolean), false, BfDefaultValueKind_Undef);
		}
		else
		{
			// Always true
			if ((!IsInSpecializedSection()) && (!resolvedType->IsGenericParam()))
				Warn(BfWarning_CS0472_ValueTypeNullCompare, StrFormat("Variable declaration is always 'true' since a value of type '%s' can never be null",
					TypeToString(initValue.mType).c_str()), varDecl);
			exprEvaluator->mResult = BfTypedValue(mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1), boolType);
		}
	}

	if ((unresolvedType->IsGenericParam()) && (resolvedType->IsValuelessType()) && (mHasFullDebugInfo))
	{
		// We do this in order to be able to bind to lines that contain valueless variable declarations in some generics
		//  We don't need to do this in non-generics because the breakpoint will just move to the next line that actually has instructions
		EmitEnsureInstructionAt();
	}

	if ((resolvedType->IsVoid()) && (!IsInSpecializedSection()))
	{
		Warn(0, StrFormat("Variable '%s' is declared as 'void'", localDef->mName.c_str()), varDecl->mTypeRef);
	}

	if ((!handledVarInit) && (isConst))
		localDef->mConstValue = initValue.mValue;

	if (!allowValueAccess)
		localDef->mValue = BfIRValue();

	if (!localDef->mIsShadow)
		CheckVariableDef(localDef);

	ValidateAllocation(localDef->mResolvedType, varDecl->mTypeRef);

	if ((exprEvaluator == NULL) && (varDecl->GetSourceData() != NULL))
		UpdateSrcPos(varDecl);
	localDef->Init();

	if (localDef->mConstValue)
		initType = BfIRInitType_NotNeeded;

	BfLocalVariable* localVar = AddLocalVariableDef(localDef, true, false, BfIRValue(), initType);
	if (wantsStore)
		mBfIRBuilder->CreateAlignedStore(initValue.mValue, localVar->mAddr, localVar->mResolvedType->mAlign);

	if ((mCurMethodState->mConstResolveState != NULL) && (mCurMethodState->mConstResolveState->mInCalcAppend))
	{
		if (localDef->mValue.IsConst())
			localDef->mConstValue = localDef->mValue;
	}

	return localVar;
}

BfLocalVariable* BfModule::HandleVariableDeclaration(BfType* type, BfAstNode* nameNode, BfTypedValue val, bool updateSrcLoc, bool forceAddr)
{
	BfLocalVariable* localDef = new BfLocalVariable();
	nameNode->ToString(localDef->mName);
	localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(nameNode);
	localDef->mResolvedType = type;
	localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
	localDef->mValue = val.mValue;

	if ((!localDef->mIsReadOnly) || (mHasFullDebugInfo))
	{
		localDef->mAddr = AllocLocalVariable(localDef->mResolvedType, localDef->mName);
		if ((val.mValue) && (!localDef->mResolvedType->IsValuelessType()) && (!localDef->mResolvedType->IsVar()))
		{
			if (localDef->mResolvedType->IsRef())
				val = MakeAddressable(val, true, true);

			if (val.IsSplat())
			{
				AggregateSplatIntoAddr(val, localDef->mAddr);
			}
			else
				mBfIRBuilder->CreateAlignedStore(val.mValue, localDef->mAddr, localDef->mResolvedType->mAlign);
		}
	}

	CheckVariableDef(localDef);

	if (nameNode->GetSourceData() != NULL)
		UpdateSrcPos(nameNode);
	localDef->Init();
	return AddLocalVariableDef(localDef, true);
}

BfLocalVariable* BfModule::HandleVariableDeclaration(BfVariableDeclaration* varDecl, BfTypedValue val, bool updateSrcLoc, bool forceAddr)
{
	if (varDecl->mEqualsNode != NULL)
		Fail("Unexpected initialization", varDecl->mEqualsNode);
	if (varDecl->mInitializer != NULL)
		CreateValueFromExpression(varDecl->mInitializer);

	auto isLet = varDecl->mTypeRef->IsA<BfLetTypeReference>();
	auto isVar = varDecl->mTypeRef->IsA<BfVarTypeReference>();
	bool isRef = false;

	if (auto varRefTypeReference = BfNodeDynCast<BfVarRefTypeReference>(varDecl->mTypeRef))
	{
		isRef = true;
		isLet = varRefTypeReference->mVarToken->GetToken() == BfToken_Let;
		isVar = varRefTypeReference->mVarToken->GetToken() == BfToken_Var;
	}
	else
	{
		BF_ASSERT(!val.IsAddr());
	}

	auto autoComplete = mCompiler->GetAutoComplete();
	if ((autoComplete != NULL) && ((isLet) || (isVar)))
		autoComplete->CheckVarResolution(varDecl->mTypeRef, val.mType);

// 	BfType* type = val.mType;
// 	if (type == NULL)
// 	{
// 		type = ResolveTypeRef(varDecl->mTypeRef);
// 		if (type == NULL)
// 			type = mContext->mBfObjectType;
// 	}

	BfType* type = NULL;
	if ((isLet) || (isVar))
	{
		type = val.mType;
	}
	else
	{
		type = ResolveTypeRef(varDecl->mTypeRef);
	}
	if (type == NULL)
	{
		type = GetPrimitiveType(BfTypeCode_Var);
		val = GetDefaultTypedValue(type);
	}

	if ((type->IsVar()) || (type->IsLet()))
	{
	}

	if (isRef)
	{
		type = CreateRefType(type);
	}

	BfLocalVariable* localDef = new BfLocalVariable();
	if (varDecl->mNameNode != NULL)
		varDecl->mNameNode->ToString(localDef->mName);
	localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(varDecl->mNameNode);
	localDef->mResolvedType = type;
	localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
	localDef->mValue = val.mValue;
	if (isLet)
	{
		localDef->mIsReadOnly = true;
	}

	if ((!localDef->mIsReadOnly) || (mHasFullDebugInfo) || (forceAddr))
	{
		localDef->mAddr = AllocLocalVariable(localDef->mResolvedType, localDef->mName);
		if ((val.mValue) && (!localDef->mResolvedType->IsValuelessType()) && (!localDef->mResolvedType->IsVar()))
		{
			if (localDef->mResolvedType->IsRef())
				val = MakeAddressable(val, true, true);

			if (val.IsSplat())
			{
				AggregateSplatIntoAddr(val, localDef->mAddr);
			}
			else
				mBfIRBuilder->CreateAlignedStore(val.mValue, localDef->mAddr, localDef->mResolvedType->mAlign);
		}
		if (forceAddr)
			localDef->mValue = BfIRValue();
	}

	CheckVariableDef(localDef);

	if ((updateSrcLoc) && (varDecl->GetSourceData() != NULL))
		UpdateSrcPos(varDecl);
	localDef->Init();
	return AddLocalVariableDef(localDef, true);
}

void BfModule::CheckTupleVariableDeclaration(BfTupleExpression* tupleExpr, BfType* initType)
{
	if (initType == NULL)
		return;

	BfTypeInstance* initTupleType = NULL;
	if ((initType != NULL) && (initType->IsTuple()))
		initTupleType = (BfTypeInstance*)initType;

	if (initTupleType != NULL)
	{
		mBfIRBuilder->PopulateType(initTupleType);
		AddDependency(initTupleType, mCurTypeInstance, BfDependencyMap::DependencyFlag_LocalUsage);

		int paramCountDiff = (int)tupleExpr->mValues.size() - (int)initTupleType->mFieldInstances.size();
		if (paramCountDiff > 0)
		{
			Fail(StrFormat("Too many variable names, expected %d fewer.", paramCountDiff), tupleExpr->mValues[(int)initTupleType->mFieldInstances.size()]);
		}
		else if (paramCountDiff < 0)
		{
			BfAstNode* refNode = tupleExpr->mCloseParen;
			if (refNode == NULL)
				refNode = tupleExpr;
			Fail(StrFormat("Too few variable names, expected %d more.", -paramCountDiff), refNode);
		}
	}
	else
	{
		Fail(StrFormat("Value result type '%s' must be a tuple type to be applicable for tuple decomposition", TypeToString(initType).c_str()), tupleExpr);
	}
}

void BfModule::HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl, BfTupleExpression* tupleExpr, BfTypedValue initTupleValue, bool isReadOnly, bool isConst, bool forceAddr, BfIRBlock* declBlock)
{
	BfTypeInstance* initTupleType = NULL;
	if ((initTupleValue) && (initTupleValue.mType->IsTuple()))
		initTupleType = (BfTypeInstance*)initTupleValue.mType;

	CheckTupleVariableDeclaration(tupleExpr, initTupleValue.mType);

	for (int varIdx = 0; varIdx < (int)tupleExpr->mValues.size(); varIdx++)
	{
		BfType* resolvedType = NULL;
		BfTypedValue initValue;
		if ((initTupleType != NULL) && (varIdx < (int)initTupleType->mFieldInstances.size()))
		{
			auto fieldInstance = &initTupleType->mFieldInstances[varIdx];
			auto fieldDef = fieldInstance->GetFieldDef();
			resolvedType = fieldInstance->GetResolvedType();
			if (fieldInstance->mDataIdx != -1)
			{
				if (initTupleValue.IsAddr())
				{
					initValue = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(initTupleValue.mValue, 0, fieldInstance->mDataIdx), resolvedType, true);
					initValue = LoadValue(initValue);
				}
				else
					initValue = BfTypedValue(mBfIRBuilder->CreateExtractValue(initTupleValue.mValue, fieldInstance->mDataIdx), resolvedType);
			}

			BfTupleNameNode* tupleNameNode = NULL;
			if (varIdx < (int)tupleExpr->mNames.size())
				tupleNameNode = tupleExpr->mNames[varIdx];

			if (!fieldDef->IsUnnamedTupleField())
			{
				if (tupleNameNode != NULL)
				{
					if (fieldDef->mName != tupleNameNode->mNameNode->ToString())
					{
						Fail(StrFormat("Mismatched tuple field name, expected '%s'", fieldDef->mName.c_str()), tupleNameNode->mNameNode);
					}
				}
			}
			else if ((tupleNameNode != NULL) && (tupleNameNode->mNameNode != NULL))
			{
				Fail(StrFormat("Unexpected tuple field name, expected unnamed tuple field", fieldDef->mName.c_str()), tupleNameNode->mNameNode);
			}
		}
		else
		{
			resolvedType = mContext->mBfObjectType;
			initValue = GetDefaultTypedValue(resolvedType);
		}

		BfExpression* varNameNode = tupleExpr->mValues[varIdx];
		if (!varNameNode->IsExact<BfIdentifierNode>())
		{
			if (BfTupleExpression* innerTupleExpr = BfNodeDynCast<BfTupleExpression>(varNameNode))
			{
				HandleTupleVariableDeclaration(varDecl, innerTupleExpr, initValue, isReadOnly, isConst, false, declBlock);
			}
			else if (!varNameNode->IsExact<BfUninitializedExpression>())
				Fail("Variable name expected", varNameNode);
			continue;
		}

		bool initHandled = false;

		BfLocalVariable* localDef = new BfLocalVariable();
		varNameNode->ToString(localDef->mName);
		localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(varNameNode);
		localDef->mResolvedType = resolvedType;
		localDef->mReadFromId = 0; // Don't give usage errors for binds
		if (isReadOnly)
		{
			localDef->mIsReadOnly = true;
			if ((initValue) && (initValue.mValue.IsConst()))
			{
				isConst = true;
			}
		}
		CheckVariableDef(localDef);

		if ((!isConst) && ((forceAddr) || (!localDef->mIsReadOnly) || (mHasFullDebugInfo)))
		{
			localDef->mAddr = AllocLocalVariable(resolvedType, localDef->mName);
		}

		if ((varDecl != NULL) && (varDecl->mEqualsNode != NULL) && (mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL) && (!initHandled))
		{
			mCompiler->mResolvePassData->mAutoComplete->CheckEmptyStart(varDecl->mEqualsNode, resolvedType);
		}

		if ((varDecl == NULL) || (varDecl->mInitializer != NULL))
		{
			if ((!isConst) && (!initHandled))
			{
				if (initValue)
				{
					if (!forceAddr)
						localDef->mValue = initValue.mValue;
					if (localDef->mAddr)
					{
						mBfIRBuilder->CreateStore(initValue.mValue, localDef->mAddr);
					}
				}
				else if ((varDecl == NULL) || (varDecl->mInitializer->IsA<BfUninitializedExpression>()))
				{
					// Fake 'is assigned'
				}
				else
				{
					AssertErrorState();
				}
			}

			localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		}
		else if ((varDecl != NULL) && (varDecl->mInitializer == NULL))
		{
			if (auto refTypeRef = BfNodeDynCast<BfRefTypeRef>(varDecl->mTypeRef))
			{
				Fail("Ref variables must be initialized", refTypeRef->mRefToken);
			}
			else
			{
				BF_ASSERT(!localDef->mResolvedType->IsRef());
			}
		}

		if (isConst)
			localDef->mConstValue = initValue.mValue;

		CheckVariableDef(localDef);

		if ((varDecl != NULL) && (varDecl->GetSourceData() != NULL))
			UpdateSrcPos(varDecl);
		localDef->Init();

		auto defBlock = mBfIRBuilder->GetInsertBlock();
		if (declBlock != NULL)
			mBfIRBuilder->SetInsertPoint(*declBlock);
		AddLocalVariableDef(localDef, true, false, BfIRValue(), BfIRInitType_NotNeeded);
		if (declBlock != NULL)
			mBfIRBuilder->SetInsertPoint(defBlock);
	}
}

void BfModule::HandleTupleVariableDeclaration(BfVariableDeclaration* varDecl)
{
	BfAutoComplete* bfAutocomplete = mCompiler->GetAutoComplete();
	if (bfAutocomplete != NULL)
		bfAutocomplete->CheckTypeRef(varDecl->mTypeRef, true);

	BfTupleExpression* tupleExpr = BfNodeDynCast<BfTupleExpression>(varDecl->mNameNode);

	bool isConst = (varDecl->mModSpecifier != NULL) && (varDecl->mModSpecifier->GetToken() == BfToken_Const);
	bool isReadOnly = (varDecl->mModSpecifier != NULL) && (varDecl->mModSpecifier->GetToken() == BfToken_ReadOnly);

	BfTypedValue initTupleValue;
	bool hadVarType = false;
	bool isLet = varDecl->mTypeRef->IsA<BfLetTypeReference>();
	bool isVar = varDecl->mTypeRef->IsA<BfVarTypeReference>();
	bool wasVarOrLet = isVar || isLet;

	if ((!isLet) && (!isVar))
	{
		ResolveTypeRef(varDecl->mTypeRef);
		Fail("'var' or 'let' expected", varDecl->mTypeRef);
		isVar = true;
	}

	if ((isVar) || (isLet))
	{
		hadVarType = true;
		if (varDecl->mInitializer == NULL)
		{
			Fail("Implicitly-typed variables must be initialized", varDecl);
			initTupleValue = GetDefaultTypedValue(mContext->mBfObjectType);
		}
		else
		{
			if (isConst)
			{
				BfConstResolver constResolver(this);
				initTupleValue = constResolver.Resolve(varDecl->mInitializer);
			}
			else
			{
				initTupleValue = CreateValueFromExpression(varDecl->mInitializer, NULL);
			}
		}

		initTupleValue = LoadValue(initTupleValue);

		if ((bfAutocomplete != NULL) && (wasVarOrLet))
			bfAutocomplete->CheckVarResolution(varDecl->mTypeRef, initTupleValue.mType);
	}

	bool isCompatible = false;
	if (initTupleValue)
		HandleTupleVariableDeclaration(varDecl, tupleExpr, initTupleValue, isLet || isReadOnly, isConst, false);
	else
		AssertErrorState();
}

void BfModule::HandleCaseEnumMatch_Tuple(BfTypedValue tupleVal, const BfSizedArray<BfExpression*>& arguments, BfAstNode* tooFewRef, BfIRValue phiVal, BfIRBlock& matchedBlockStart, BfIRBlock& matchedBlockEnd, BfIRBlock& falseBlockStart, BfIRBlock& falseBlockEnd, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough)
{
	SetAndRestoreValue<bool> prevInCondBlock(mCurMethodState->mInConditionalBlock);

	auto tupleType = tupleVal.mType->ToTypeInstance();

	struct DeferredAssign
	{
		BfExpression* mExpr;
		BfTypedValue mArgValue;
		BfTypedValue mTupleElement;
		int mFieldIdx;
	};
	Array<DeferredAssign> deferredAssigns;

	auto autoComplete = mCompiler->GetAutoComplete();

	for (int tupleFieldIdx = 0; tupleFieldIdx < (int)tupleType->mFieldInstances.size(); tupleFieldIdx++)
	{
		auto tupleFieldInstance = &tupleType->mFieldInstances[tupleFieldIdx];

		if (tupleFieldIdx >= arguments.size())
		{
			BfError* error = Fail(StrFormat("Not enough parameters specified, expected %d more.", tupleType->mFieldInstances.size() - (int)arguments.size()), tooFewRef);
			break;
		}

		BfTypedValue tupleElement;
		if (tupleFieldInstance->mDataIdx >= 0)
		{
			tupleElement = ExtractValue(tupleVal, tupleFieldInstance, tupleFieldInstance->mDataIdx);
		}
		else
			tupleElement = GetDefaultTypedValue(tupleFieldInstance->GetResolvedType());

		auto expr = BfNodeDynCast<BfExpression>(arguments[tupleFieldIdx]);
		if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(expr))
		{
			bool isVarOrLet = (varDecl->mTypeRef->IsExact<BfLetTypeReference>()) || (varDecl->mTypeRef->IsExact<BfVarTypeReference>());
			bool isRef = false;
			if (varDecl->mTypeRef->IsExact<BfVarRefTypeReference>())
			{
				isVarOrLet = true;
				isRef = true;
			}

			if (!isVarOrLet)
			{
				auto wantType = ResolveTypeRef(varDecl->mTypeRef);
				if (wantType == NULL)
					wantType = mContext->mBfObjectType;
				if (wantType != NULL)
					tupleElement = Cast(varDecl->mTypeRef, tupleElement, wantType);
				if (!tupleElement)
					tupleElement = GetDefaultTypedValue(wantType);
			}

			PopulateType(tupleElement.mType);
			if (!isRef)
				tupleElement = LoadValue(tupleElement);

			if (prevHadFallthrough)
				Fail("Destructuring cannot be used when the previous case contains a fallthrough", expr);

			auto localVar = HandleVariableDeclaration(varDecl, tupleElement, false, true);
			localVar->mReadFromId = 0; // Don't give usage errors for binds
			continue;
		}

		if (auto binOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>(expr))
		{
			if (binOpExpr->mOp == BfBinaryOp_Multiply)
			{
				SetAndRestoreValue<bool> prevIgnoreError(mIgnoreErrors, true);
				auto resolvedType = ResolveTypeRef(binOpExpr->mLeft, NULL);
				prevIgnoreError.Restore();
				if (resolvedType != NULL)
				{
					resolvedType = CreatePointerType(resolvedType);

					PopulateType(tupleElement.mType);
					tupleElement = LoadValue(tupleElement);
					tupleElement = Cast(binOpExpr->mLeft, tupleElement, resolvedType);

					if (prevHadFallthrough)
						Fail("Destructuring cannot be used when the previous case contains a fallthrough", expr);

					auto localVar = HandleVariableDeclaration(resolvedType, binOpExpr->mRight, tupleElement, false, true);
					localVar->mReadFromId = 0; // Don't give usage errors for binds
					continue;
				}
			}
		}

		if (auto uninitExpr = BfNodeDynCast<BfUninitializedExpression>(expr))
		{
			continue;
		}

		if (tupleFieldInstance->mDataIdx >= 0)
		{
			if (auto tupleExpr = BfNodeDynCast<BfTupleExpression>(expr))
			{
				if (tupleElement.mType->IsTuple())
				{
					BfAstNode* tooFewRef = tupleExpr->mCloseParen;
					if (tupleExpr->mValues.size() > 0)
						tooFewRef = tupleExpr->mValues[tupleExpr->mValues.size() - 1];
					if (tooFewRef == NULL)
						tooFewRef = tupleExpr->mOpenParen;
					HandleCaseEnumMatch_Tuple(tupleElement, tupleExpr->mValues, tooFewRef, phiVal, matchedBlockStart, matchedBlockEnd, falseBlockStart, falseBlockEnd, hadConditional, clearOutOnMismatch, prevHadFallthrough);
					continue;
				}
			}
		}

		if (expr == NULL)
		{
			// Error would have occured in the parser
			//AssertErrorState();
		}
		else
		{
			mCurMethodState->mInConditionalBlock = true;
			auto tupleElementAddr = tupleElement;

			BfTypedValue exprResult;

			if (auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(expr))
			{
				if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(invocationExpr->mTarget))
				{
					if (memberRefExpr->mTarget == NULL)
					{
						if (tupleElement.mType->IsPayloadEnum())
						{
							auto intType = GetPrimitiveType(BfTypeCode_Int32);
							BfTypedValue enumTagVal;
							if (tupleElement.IsAddr())
							{
								enumTagVal = BfTypedValue(mBfIRBuilder->CreateInBoundsGEP(tupleElement.mValue, 0, 2), intType, true);
								enumTagVal = LoadValue(enumTagVal);
							}
							else
								enumTagVal = BfTypedValue(mBfIRBuilder->CreateExtractValue(tupleElement.mValue, 2), intType, false);

							int uncondTagId = -1;
							bool hadConditional = false;
							exprResult = TryCaseEnumMatch(tupleElementAddr, enumTagVal, expr, NULL, NULL, NULL, uncondTagId, hadConditional, clearOutOnMismatch, prevHadFallthrough);
						}
					}
				}
			}

			if (!exprResult)
			{
				tupleElement = LoadValue(tupleElement);

				bool isMatchedBlockEnd = matchedBlockEnd == mBfIRBuilder->GetInsertBlock();
				bool isFalseBlockEnd = falseBlockEnd == mBfIRBuilder->GetInsertBlock();

				BfExprEvaluator exprEvaluator(this);
				exprEvaluator.mExpectingType = tupleFieldInstance->GetResolvedType();
				exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_AllowOutExpr;
				if (mCurMethodState->mDeferredLocalAssignData != NULL)
				{
					SetAndRestoreValue<bool> prevIsIfCondition(mCurMethodState->mDeferredLocalAssignData->mIsIfCondition, true);
					SetAndRestoreValue<bool> prevIfMayBeSkipped(mCurMethodState->mDeferredLocalAssignData->mIfMayBeSkipped, true);
					exprEvaluator.Evaluate(expr);
				}
				else
				{
					exprEvaluator.Evaluate(expr);
				}
				if (isMatchedBlockEnd)
					matchedBlockEnd = mBfIRBuilder->GetInsertBlock();
				if (isFalseBlockEnd)
					falseBlockEnd = mBfIRBuilder->GetInsertBlock();
				auto argValue = exprEvaluator.mResult;
				if (!argValue)
					continue;

				if (argValue.mType->IsRef())
				{
					auto refType = (BfRefType*)argValue.mType;
					if (refType->mRefKind != BfRefType::RefKind_Out)
					{
						BfAstNode* refNode = expr;
						if (auto unaryOperatorExpr = BfNodeDynCast<BfUnaryOperatorExpression>(expr))
							refNode = unaryOperatorExpr->mOpToken;
						Fail("Only 'out' refs can be used to assign to an existing value", refNode);
					}

					DeferredAssign deferredAssign = { expr, argValue, tupleElement, tupleFieldIdx };
					deferredAssigns.push_back(deferredAssign);
					if (mCurMethodState->mDeferredLocalAssignData != NULL)
					{
						SetAndRestoreValue<bool> prevIsIfCondition(mCurMethodState->mDeferredLocalAssignData->mIsIfCondition, true);
						SetAndRestoreValue<bool> prevIfMayBeSkipped(mCurMethodState->mDeferredLocalAssignData->mIfMayBeSkipped, true);
						exprEvaluator.MarkResultAssigned();
					}
					else
					{
						exprEvaluator.MarkResultAssigned();
					}
					continue;
				}

				if (!argValue.mType->IsValueType())
					argValue = LoadValue(argValue);
				argValue = Cast(expr, argValue, tupleFieldInstance->GetResolvedType());
				if (!argValue)
					continue;

				exprEvaluator.PerformBinaryOperation(expr, expr, BfBinaryOp_Equality, expr, BfBinOpFlag_NoClassify, tupleElement, argValue);
				exprResult = exprEvaluator.mResult;
			}

			if (exprResult)
			{
				hadConditional = true;
				if (phiVal)
				{
					auto insertBlock = mBfIRBuilder->GetInsertBlock();
					mBfIRBuilder->AddPhiIncoming(phiVal, mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), insertBlock);
				}

				matchedBlockStart = matchedBlockEnd = mBfIRBuilder->CreateBlock("match", false);

				mBfIRBuilder->CreateCondBr(exprResult.mValue, matchedBlockStart, falseBlockStart);
				mBfIRBuilder->AddBlock(matchedBlockStart);
				mBfIRBuilder->SetInsertPoint(matchedBlockStart);
			}
		}
	}

	if (!deferredAssigns.empty())
		mBfIRBuilder->SetInsertPoint(matchedBlockEnd);

	// We assign these only after the value checks succeed
	for (auto& deferredAssign : deferredAssigns)
	{
		auto argValue = RemoveRef(deferredAssign.mArgValue);
		auto tupleElement = Cast(deferredAssign.mExpr, deferredAssign.mTupleElement, argValue.mType);
		if (!tupleElement)
			continue;
		tupleElement = LoadValue(tupleElement);
		if (!tupleElement.mType->IsValuelessType())
			mBfIRBuilder->CreateStore(tupleElement.mValue, argValue.mValue);
	}

	if ((clearOutOnMismatch) && (!deferredAssigns.IsEmpty()))
	{
		auto curInsertPoint = mBfIRBuilder->GetInsertBlock();
		mBfIRBuilder->SetInsertPoint(falseBlockEnd);
		for (auto& deferredAssign : deferredAssigns)
		{
			auto tupleFieldInstance = &tupleType->mFieldInstances[deferredAssign.mFieldIdx];
			// We have to re-process the expr because we haven't done it in this branch, and then clear the result out
			SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, mHadBuildError); // Don't fail twice
			BfExprEvaluator exprEvaluator(this);
			exprEvaluator.mExpectingType = tupleFieldInstance->GetResolvedType();
			exprEvaluator.mBfEvalExprFlags = BfEvalExprFlags_AllowOutExpr;
			exprEvaluator.Evaluate(deferredAssign.mExpr);
			auto argValue = exprEvaluator.mResult;
			if (!argValue)
				continue;
 			mBfIRBuilder->CreateMemSet(argValue.mValue, GetConstValue8(0), GetConstValue(argValue.mType->mSize), GetConstValue(argValue.mType->mAlign));
		}
		falseBlockEnd = mBfIRBuilder->GetInsertBlock();
		mBfIRBuilder->SetInsertPoint(curInsertPoint);
	}

	if (arguments.size() > tupleType->mFieldInstances.size())
	{
		for (int i = (int)tupleType->mFieldInstances.size(); i < (int)arguments.size(); i++)
		{
			// For autocomplete and such
			auto expr = arguments[i];
			if (expr != NULL)
				CreateValueFromExpression(expr);
		}

		BfAstNode* errorRef = arguments[(int)tupleType->mFieldInstances.size()];
		BfError* error = Fail(StrFormat("Too many arguments, expected %d fewer.", arguments.size() - tupleType->mFieldInstances.size()), errorRef);
	}
}

BfTypedValue BfModule::TryCaseTupleMatch(BfTypedValue tupleVal, BfTupleExpression* tupleExpr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough)
{
	if (!tupleVal.mType->IsTuple())
		return BfTypedValue();

	auto tupleType = (BfTypeInstance*)tupleVal.mType;

	BfAstNode* tooFewRef = tupleExpr->mCloseParen;
	if ((tooFewRef == NULL) && (!tupleExpr->mCommas.IsEmpty()))
		tooFewRef = tupleExpr->mCommas[tupleExpr->mCommas.size() - 1];
	else if (tooFewRef == NULL)
		tooFewRef = tupleExpr->mOpenParen;

	///

	auto autoComplete = mCompiler->GetAutoComplete();

	bool wasCapturingMethodInfo = false;
	if (autoComplete != NULL)
	{
		wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;
		autoComplete->CheckInvocation(tupleExpr, tupleExpr->mOpenParen, tupleExpr->mCloseParen, tupleExpr->mCommas);

		if (autoComplete->mIsCapturingMethodMatchInfo)
		{
			autoComplete->mMethodMatchInfo->mInstanceList.Clear();

			auto methodMatchInfo = autoComplete->mMethodMatchInfo;

// 			auto methodDef = tupleType->mTypeDef->mMethods[0];
//
// 			BfAutoComplete::MethodMatchEntry methodMatchEntry;
// 			methodMatchEntry.mMethodDef = methodDef;
// 			methodMatchEntry.mTypeInstance = tupleType;
// 			methodMatchEntry.mCurMethodInstance = mCurMethodInstance;
// 			//methodMatchEntry.mPayloadEnumField = fieldInstance;
			//autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);

			methodMatchInfo->mBestIdx = 0;
			methodMatchInfo->mMostParamsMatched = 0;

			int cursorIdx = tupleExpr->GetParser()->mCursorIdx;
			if ((tupleExpr->mCloseParen == NULL) || (cursorIdx <= tupleExpr->mCloseParen->GetSrcStart()))
			{
				int paramIdx = 0;
				for (int commaIdx = 0; commaIdx < (int)tupleExpr->mCommas.size(); commaIdx++)
				{
					auto commaNode = tupleExpr->mCommas[commaIdx];
					if ((commaNode != NULL) && (cursorIdx >= commaNode->GetSrcStart()))
						paramIdx = commaIdx + 1;
				}

				bool isEmpty = true;

				if (paramIdx < (int)tupleExpr->mValues.size())
				{
					auto paramNode = tupleExpr->mValues[paramIdx];
					if (paramNode != NULL)
						isEmpty = false;
				}

				if (isEmpty)
				{
					if (paramIdx < (int)tupleType->mFieldInstances.size())
					{
						auto fieldDef = tupleType->mFieldInstances[paramIdx].GetFieldDef();
						String insertStr;
						if (fieldDef->IsUnnamedTupleField())
							insertStr = "p";
						insertStr += fieldDef->mName;

						insertStr.Insert(0, "let ");
						autoComplete->mEntriesSet.Clear();
						autoComplete->AddEntry(AutoCompleteEntry("paramName", insertStr));
						autoComplete->mInsertStartIdx = cursorIdx;
						autoComplete->mInsertEndIdx = cursorIdx;
					}
				}
			}
		}
	}

	defer
	(
		if (autoComplete != NULL)
			autoComplete->mIsCapturingMethodMatchInfo = (wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo);
	);

	///

	//BfIRValue phiVal;
	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
	//phiVal = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(boolType), 2);

	auto startBlock = mBfIRBuilder->GetInsertBlock();
	//auto dscrType = enumType->GetDiscriminatorType();
	//BfIRValue eqResult = mBfIRBuilder->CreateCmpEQ(tagVal.mValue, mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagId));

	BfIRBlock falseBlockStart;
	BfIRBlock falseBlockEnd;
	BfIRBlock doneBlockStart;
	if (notEqBlock != NULL)
		doneBlockStart = *notEqBlock;
	else
		doneBlockStart = mBfIRBuilder->CreateBlock("caseDone", false);
	BfIRBlock doneBlockEnd = doneBlockStart;

	if (clearOutOnMismatch)
	{
		falseBlockStart = falseBlockEnd = mBfIRBuilder->CreateBlock("caseNotEq", false);
		mBfIRBuilder->AddBlock(falseBlockStart);
	}

	BfIRBlock matchedBlockStart = mBfIRBuilder->CreateBlock("caseMatch", false);
	if (matchBlock != NULL)
		*matchBlock = matchedBlockStart;
	mBfIRBuilder->CreateBr(matchedBlockStart);
	mBfIRBuilder->AddBlock(matchedBlockStart);

	mBfIRBuilder->SetInsertPoint(doneBlockEnd);
	BfIRValue phiVal;
	if (eqBlock == NULL)
		phiVal = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(boolType), 2);

	mBfIRBuilder->SetInsertPoint(matchedBlockStart);
	BfIRBlock matchedBlockEnd = matchedBlockStart;
	HandleCaseEnumMatch_Tuple(tupleVal, tupleExpr->mValues, tooFewRef, falseBlockStart ? BfIRValue() : phiVal, matchedBlockStart, matchedBlockEnd,
		falseBlockStart ? falseBlockStart : doneBlockStart, falseBlockEnd ? falseBlockEnd : doneBlockEnd, hadConditional, clearOutOnMismatch, prevHadFallthrough);

	if (phiVal)
	{
		auto falseVal = mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0);
		if (falseBlockEnd)
			mBfIRBuilder->AddPhiIncoming(phiVal, falseVal, falseBlockEnd);

		auto trueVal = mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1);
		mBfIRBuilder->AddPhiIncoming(phiVal, trueVal, matchedBlockEnd);
	}

	if (eqBlock != NULL)
		mBfIRBuilder->CreateBr(*eqBlock);
	else
		mBfIRBuilder->CreateBr(doneBlockStart);

	if (falseBlockEnd)
	{
		mBfIRBuilder->SetInsertPoint(falseBlockEnd);
		mBfIRBuilder->CreateBr(doneBlockStart);
		//mBfIRBuilder->AddPhiIncoming(phiVal, mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), falseBlock);
	}

	mBfIRBuilder->AddBlock(doneBlockStart);
	mBfIRBuilder->SetInsertPoint(doneBlockEnd);

	if (phiVal)
		return BfTypedValue(phiVal, boolType);
	else
		return GetDefaultTypedValue(boolType);
}

BfTypedValue BfModule::TryCaseEnumMatch(BfTypedValue enumVal, BfTypedValue tagVal, BfExpression* expr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, int& tagId, bool& hadConditional, bool clearOutOnMismatch, bool prevHadFallthrough)
{
	auto invocationExpr = BfNodeDynCast<BfInvocationExpression>(expr);
	if (invocationExpr == NULL)
		return BfTypedValue();

	auto activeTypeDef = GetActiveTypeDef();

	BfType* targetType = NULL;
	BfIdentifierNode* nameNode = NULL;
	BfTokenNode* dotNode = NULL;
	if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(invocationExpr->mTarget))
	{
		if (memberRefExpr->mTarget == NULL)
		{
			targetType = enumVal.mType;
		}
		else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberRefExpr->mTarget))
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
			targetType = ResolveTypeRef(typeRef);
		}
		else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mTarget))
		{
			SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
			targetType = ResolveTypeRef(identifier, NULL);
		}
		if (auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(memberRefExpr->mMemberName))
		{
			dotNode = memberRefExpr->mDotToken;
			nameNode = nameIdentifier;
		}
		else
			return BfTypedValue();
	}
	else if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(invocationExpr->mTarget))
	{
		SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
		targetType = ResolveTypeRef(qualifiedNameNode->mLeft, NULL);
		nameNode = qualifiedNameNode->mRight;
	}
	else if (auto identiferNode = BfNodeDynCast<BfIdentifierNode>(invocationExpr->mTarget))
	{
		targetType = mCurTypeInstance;
		nameNode = identiferNode;
	}
	else
		return BfTypedValue();

	// These may have been colorized as methods, so change that
	SetElementType(nameNode, BfSourceElementType_Normal);

	if ((targetType == NULL) || (!targetType->IsPayloadEnum()))
		return BfTypedValue();

	auto enumType = targetType->ToTypeInstance();
	PopulateType(enumType);

	StringT<128> enumCaseName;
	if (nameNode != NULL)
		nameNode->ToString(enumCaseName);

	auto tagType = GetPrimitiveType(BfTypeCode_Int32);
	if (enumVal.mType != enumType)
	{
		Fail(StrFormat("Cannot match enum type '%s' with type '%s'",
			TypeToString(enumVal.mType).c_str(), TypeToString(enumType).c_str()));
		enumVal = GetDefaultTypedValue(enumType);
		tagVal = GetDefaultTypedValue(tagType);
	}

	for (int fieldIdx = 0; fieldIdx < (int)enumType->mFieldInstances.size(); fieldIdx++)
	{
		auto fieldInstance = &enumType->mFieldInstances[fieldIdx];
		auto fieldDef = fieldInstance->GetFieldDef();
		if (fieldDef == NULL)
			continue;
		if ((fieldInstance->mIsEnumPayloadCase) && (fieldDef->mName == enumCaseName))
		{
			if ((!enumType->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, this)) ||
				(!enumType->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
				continue;

			auto resolvePassData = mCompiler->mResolvePassData;
			if (resolvePassData != NULL)
			{
				if (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field)
					resolvePassData->HandleFieldReference(nameNode, enumType->mTypeDef, fieldDef);
				String filter;
				auto autoComplete = resolvePassData->mAutoComplete;
				if ((autoComplete != NULL) && (autoComplete->InitAutocomplete(dotNode, nameNode, filter)))
					autoComplete->AddEnumTypeMembers(enumType, enumCaseName, false, enumType == mCurTypeInstance);
			}

			BF_ASSERT(fieldInstance->mResolvedType->IsTuple());
			auto tupleType = (BfTypeInstance*)fieldInstance->mResolvedType;
			PopulateType(tupleType);
			mBfIRBuilder->PopulateType(tupleType);

			auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
			tagId = -fieldInstance->mDataIdx - 1;

			auto startBlock = mBfIRBuilder->GetInsertBlock();
			auto dscrType = enumType->GetDiscriminatorType();
			BfIRValue eqResult = mBfIRBuilder->CreateCmpEQ(tagVal.mValue, mBfIRBuilder->CreateConst(dscrType->mTypeDef->mTypeCode, tagId));

			BfIRBlock falseBlockStart;
			BfIRBlock falseBlockEnd;
			BfIRBlock doneBlockStart;
			BfIRBlock doneBlockEnd;
			if (notEqBlock != NULL)
				doneBlockStart = doneBlockEnd = *notEqBlock;
			else
 				doneBlockStart = doneBlockEnd = mBfIRBuilder->CreateBlock("caseDone", false);

			if (clearOutOnMismatch)
			{
				falseBlockStart = falseBlockEnd = mBfIRBuilder->CreateBlock("caseNotEq", false);
				mBfIRBuilder->AddBlock(falseBlockStart);
			}

			BfIRBlock matchedBlockStart = mBfIRBuilder->CreateBlock("caseMatch", false);
			BfIRBlock matchedBlockEnd = matchedBlockStart;
			if (matchBlock != NULL)
				*matchBlock = matchedBlockStart;
			mBfIRBuilder->CreateCondBr(eqResult, matchedBlockStart, falseBlockStart ? falseBlockStart : doneBlockStart);

			mBfIRBuilder->AddBlock(matchedBlockStart);

			mBfIRBuilder->SetInsertPoint(doneBlockEnd);
			BfIRValue phiVal;
			if (eqBlock == NULL)
				phiVal = mBfIRBuilder->CreatePhi(mBfIRBuilder->MapType(boolType), 1 + (int)tupleType->mFieldInstances.size());

			mBfIRBuilder->SetInsertPoint(matchedBlockEnd);

			BfTypedValue tupleVal;
			if (!enumVal.IsAddr())
			{
				auto unionInnerType = enumType->GetUnionInnerType();
				if (unionInnerType == tupleType)
				{
					tupleVal = ExtractValue(enumVal, NULL, 1);
				}
			}

			if (!tupleVal)
			{
				if (!tupleType->IsValuelessType())
				{
					tupleVal = ExtractValue(enumVal, NULL, 1);
					tupleVal = Cast(NULL, tupleVal, tupleType, BfCastFlags_Force);
				}
				else
					tupleVal = GetDefaultTypedValue(tupleType);
			}

			////

			BfAstNode* tooFewRef = invocationExpr->mCloseParen;
			if ((tooFewRef == NULL) && (!invocationExpr->mCommas.IsEmpty()))
				tooFewRef = invocationExpr->mCommas[invocationExpr->mCommas.size() - 1];
			else if (tooFewRef == NULL)
				tooFewRef = invocationExpr->mOpenParen;

			///

			auto autoComplete = mCompiler->GetAutoComplete();

			bool wasCapturingMethodInfo = false;
			if (autoComplete != NULL)
			{
				wasCapturingMethodInfo = autoComplete->mIsCapturingMethodMatchInfo;
				autoComplete->CheckInvocation(invocationExpr, invocationExpr->mOpenParen, invocationExpr->mCloseParen, invocationExpr->mCommas);

				if (autoComplete->mIsCapturingMethodMatchInfo)
				{
					autoComplete->mMethodMatchInfo->mInstanceList.Clear();

					auto methodMatchInfo = autoComplete->mMethodMatchInfo;

					BfAutoComplete::MethodMatchEntry methodMatchEntry;
					methodMatchEntry.mTypeInstance = enumType;
					methodMatchEntry.mCurMethodInstance = mCurMethodInstance;
					methodMatchEntry.mPayloadEnumField = fieldInstance;
					autoComplete->mMethodMatchInfo->mInstanceList.push_back(methodMatchEntry);

					methodMatchInfo->mBestIdx = 0;
					methodMatchInfo->mMostParamsMatched = 0;

					int cursorIdx = invocationExpr->GetParser()->mCursorIdx;
					if ((invocationExpr->mCloseParen == NULL) || (cursorIdx <= invocationExpr->mCloseParen->GetSrcStart()))
					{
						int paramIdx = 0;
						for (int commaIdx = 0; commaIdx < (int)invocationExpr->mCommas.size(); commaIdx++)
						{
							auto commaNode = invocationExpr->mCommas[commaIdx];
							if ((commaNode != NULL) && (cursorIdx >= commaNode->GetSrcStart()))
								paramIdx = commaIdx + 1;
						}

						bool isEmpty = true;

						if (paramIdx < (int)invocationExpr->mArguments.size())
						{
							auto paramNode = invocationExpr->mArguments[paramIdx];
							if (paramNode != NULL)
								isEmpty = false;
						}

						if (isEmpty)
						{
							if (paramIdx < (int)tupleType->mFieldInstances.size())
							{
								auto fieldDef = tupleType->mFieldInstances[paramIdx].GetFieldDef();
								String insertStr;
								if (fieldDef->IsUnnamedTupleField())
									insertStr = "p";
								insertStr += fieldDef->mName;

								insertStr.Insert(0, "let ");
								autoComplete->mEntriesSet.Clear();
								autoComplete->AddEntry(AutoCompleteEntry("paramName", insertStr));
								autoComplete->mInsertStartIdx = cursorIdx;
								autoComplete->mInsertEndIdx = cursorIdx;
							}
						}
					}
				}
			}

			defer
			(
				if (autoComplete != NULL)
					autoComplete->mIsCapturingMethodMatchInfo = (wasCapturingMethodInfo) && (!autoComplete->mIsCapturingMethodMatchInfo);
			);

			///

			HandleCaseEnumMatch_Tuple(tupleVal, invocationExpr->mArguments, tooFewRef, falseBlockStart ? BfIRValue() : phiVal, matchedBlockStart, matchedBlockEnd,
				falseBlockStart ? falseBlockStart : doneBlockStart, falseBlockEnd ? falseBlockEnd : doneBlockEnd,
				hadConditional, clearOutOnMismatch, prevHadFallthrough);

			///////

			if (phiVal)
			{
				auto falseVal = mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0);
				if (falseBlockEnd)
					mBfIRBuilder->AddPhiIncoming(phiVal, falseVal, falseBlockEnd);
				else
					mBfIRBuilder->AddPhiIncoming(phiVal, falseVal, startBlock);
				auto trueVal = mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 1);
				mBfIRBuilder->AddPhiIncoming(phiVal, trueVal, matchedBlockEnd);
			}

			if (eqBlock != NULL)
				mBfIRBuilder->CreateBr(*eqBlock);
			else
				mBfIRBuilder->CreateBr(doneBlockStart);

			if (falseBlockEnd)
			{
				mBfIRBuilder->SetInsertPoint(falseBlockEnd);
				mBfIRBuilder->CreateBr(doneBlockStart);
				//mBfIRBuilder->AddPhiIncoming(phiVal, mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0), falseBlock);
			}

			mBfIRBuilder->AddBlock(doneBlockStart);
			mBfIRBuilder->SetInsertPoint(doneBlockEnd);

			if (phiVal)
				return BfTypedValue(phiVal, boolType);
			else
				return GetDefaultTypedValue(boolType);
		}
	}

	return BfTypedValue();
}

BfTypedValue BfModule::HandleCaseBind(BfTypedValue enumVal, const BfTypedValue& tagVal, BfEnumCaseBindExpression* bindExpr, BfIRBlock* eqBlock, BfIRBlock* notEqBlock, BfIRBlock* matchBlock, int* outEnumIdx)
{
	BfTypeInstance* tupleType = NULL;

	auto activeTypeDef = GetActiveTypeDef();

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);
	BfIRValue eqResult;
	if (bindExpr->mEnumMemberExpr != NULL)
	{
		int enumIdx = -1;
		String findName;

		BfType* type = NULL;
		BfAstNode* targetNode = NULL;
		BfAstNode* nameNode = NULL;
		BfAstNode* dotNode = NULL;

		if (auto memberExpr = BfNodeDynCast<BfMemberReferenceExpression>(bindExpr->mEnumMemberExpr))
		{
			dotNode = memberExpr->mDotToken;
			if (memberExpr->mMemberName != NULL)
			{
				nameNode = memberExpr->mMemberName;
				findName = memberExpr->mMemberName->ToString();

				if (memberExpr->mTarget == NULL)
				{
					type = enumVal.mType;
				}
				else if (auto typeRef = BfNodeDynCast<BfTypeReference>(memberExpr->mTarget))
				{
					type = ResolveTypeRef(typeRef);
				}
			}

			targetNode = memberExpr->mTarget;
		}
		else if (auto identiferNode = BfNodeDynCast<BfIdentifierNode>(bindExpr->mEnumMemberExpr))
		{
			if (mCurTypeInstance->IsPayloadEnum())
			{
				nameNode = identiferNode;
				findName = nameNode->ToString();
				targetNode = identiferNode;
				type = mCurTypeInstance;
			}
			else
			{
				Fail("Expected a qualified enum case name. Consider prefixing name with a dot to infer enum type name.", bindExpr->mEnumMemberExpr);
			}
		}

		if (!findName.empty())
		{
			if (type != NULL)
			{
				if (type != enumVal.mType)
				{
					Fail(StrFormat("Enum case type '%s' does not match compared value of type '%s'",
						TypeToString(enumVal.mType).c_str(), TypeToString(type).c_str()), targetNode);
				}

				if (type->IsEnum())
				{
					auto enumType = (BfTypeInstance*)type;
					for (auto& fieldInstance : enumType->mFieldInstances)
					{
						auto fieldDef = fieldInstance.GetFieldDef();
						if ((fieldDef != NULL) && (fieldDef->IsEnumCaseEntry()) && (fieldDef->mName == findName))
						{
							if ((!enumType->IsTypeMemberIncluded(fieldDef->mDeclaringType, activeTypeDef, this)) ||
								(!enumType->IsTypeMemberAccessible(fieldDef->mDeclaringType, activeTypeDef)))
								continue;

							auto resolvePassData = mCompiler->mResolvePassData;
							if (resolvePassData != NULL)
							{
								if (resolvePassData->mGetSymbolReferenceKind == BfGetSymbolReferenceKind_Field)
									resolvePassData->HandleFieldReference(nameNode, enumType->mTypeDef, fieldDef);
								String filter;
								auto autoComplete = resolvePassData->mAutoComplete;
								if ((autoComplete != NULL) && (autoComplete->InitAutocomplete(dotNode, nameNode, filter)))
									autoComplete->AddEnumTypeMembers(enumType, findName, false, enumType == mCurTypeInstance);
							}

							enumIdx = -fieldInstance.mDataIdx - 1;
							if (outEnumIdx != NULL)
								*outEnumIdx = enumIdx;
							if (fieldInstance.mIsEnumPayloadCase)
								tupleType = fieldInstance.mResolvedType->ToTypeInstance();
						}
					}

					if (enumIdx == -1)
					{
						Fail("Enum case not found", nameNode);
					}
				}
				else
				{
					Fail(StrFormat("Type '%s' is not an enum type", TypeToString(type).c_str()), targetNode);
				}
			}
		}

		BF_ASSERT(tagVal.mType->IsPrimitiveType());
		eqResult = mBfIRBuilder->CreateCmpEQ(tagVal.mValue, mBfIRBuilder->CreateConst(((BfPrimitiveType*)tagVal.mType)->mTypeDef->mTypeCode, enumIdx));
	}
	else
	{
		eqResult = mBfIRBuilder->CreateConst(BfTypeCode_Boolean, 0);
	}

	BfIRBlock falseBlock;
	if (notEqBlock != NULL)
	{
		falseBlock = *notEqBlock;
	}
	else
	{
		falseBlock = mBfIRBuilder->CreateBlock("notEqBlock", false);
	}

	auto mainBlock = mBfIRBuilder->GetInsertBlock();

	BfIRBlock trueBlock = mBfIRBuilder->CreateBlock("eqBlock", false);
	if (matchBlock != NULL)
		*matchBlock = trueBlock;
	mBfIRBuilder->AddBlock(trueBlock);
	mBfIRBuilder->SetInsertPoint(trueBlock);

	if ((tupleType != NULL) && (bindExpr->mBindNames != NULL))
	{
		BfIRValue valueScopeStart;
		if (IsTargetingBeefBackend())
			valueScopeStart = mBfIRBuilder->CreateValueScopeStart();

		bool isVar = bindExpr->mBindToken->GetToken() == BfToken_Var;
		bool isLet = bindExpr->mBindToken->GetToken() == BfToken_Let;

		BfTypedValue tupleVal;
		if (enumVal.IsAddr())
		{
			auto ptrVal = mBfIRBuilder->CreateInBoundsGEP(enumVal.mValue, 0, 1);
			tupleVal = BfTypedValue(mBfIRBuilder->CreateBitCast(ptrVal, mBfIRBuilder->MapTypeInstPtr(tupleType)), tupleType, true);
		}
		else
		{
			auto unionInnerType = enumVal.mType->ToTypeInstance()->GetUnionInnerType();
			tupleVal = ExtractValue(enumVal, NULL, 1);
			if (unionInnerType != tupleType)
			{
				tupleVal = MakeAddressable(tupleVal);
				tupleVal = BfTypedValue(mBfIRBuilder->CreateBitCast(tupleVal.mValue, mBfIRBuilder->MapTypeInstPtr(tupleType)), tupleType, true);
			}
		}
		HandleTupleVariableDeclaration(NULL, bindExpr->mBindNames, tupleVal, isLet, false, true /*, &mainBlock*/);

		auto autoComplete = mCompiler->GetAutoComplete();
		if ((autoComplete != NULL) && ((isVar || isLet)))
			autoComplete->CheckVarResolution(bindExpr->mBindToken, tupleType);

		if (valueScopeStart)
			mBfIRBuilder->CreateValueScopeSoftEnd(valueScopeStart);
	}
	if (eqBlock != NULL)
		mBfIRBuilder->CreateBr(*eqBlock);
	else
		mBfIRBuilder->CreateBr(falseBlock);

	// Don't create the condBr until now, so HandleTupleVariableDeclaration can create the variable declarations in mainBlock--
	//  we need them there since the code that uses the new variables is created outside the eqBlock
	mBfIRBuilder->SetInsertPoint(mainBlock);
	mBfIRBuilder->CreateCondBr(eqResult, trueBlock, falseBlock);

	mBfIRBuilder->AddBlock(falseBlock);
	mBfIRBuilder->SetInsertPoint(falseBlock);

	return BfTypedValue(eqResult, boolType);
}

void BfModule::AddBasicBlock(BfIRBlock bb, bool activate)
{
	mBfIRBuilder->AddBlock(bb);

	if (activate)
		mBfIRBuilder->SetInsertPoint(bb);
}

void BfModule::VisitEmbeddedStatement(BfAstNode* stmt, BfExprEvaluator* exprEvaluator, BfEmbeddedStatementFlags flags)
{
	auto block = BfNodeDynCast<BfBlock>(stmt);
	BfLabelNode* labelNode = NULL;
	if (block == NULL)
	{
		auto labeledBlock = BfNodeDynCast<BfLabeledBlock>(stmt);
		if (labeledBlock != NULL)
		{
			block = labeledBlock->mBlock;
			labelNode = labeledBlock->mLabelNode;
		}
	}

	BfAstNode* openBrace = NULL;
	BfAstNode* closeBrace = NULL;

	if (block != NULL)
	{
		openBrace = block->mOpenBrace;
		closeBrace = block->mCloseBrace;

		if (openBrace == NULL)
		{
			auto checkScope = mCurMethodState->mCurScope;
			while ((checkScope != NULL) && (closeBrace == NULL))
			{
				closeBrace = checkScope->mCloseNode;
				checkScope = checkScope->mPrevScope;
			}
			BF_ASSERT(closeBrace != NULL);
		}
	}

	if ((block != NULL) && (openBrace != NULL))
		UpdateSrcPos(openBrace);

	if ((flags & BfEmbeddedStatementFlags_Unscoped) != 0)
	{
		SetAndRestoreValue<BfExprEvaluator*> prevExprEvaluator(mCurMethodState->mCurScope->mExprEvaluator, exprEvaluator);
		SetAndRestoreValue<bool> prevAllowReturn(mCurMethodState->mDisableReturns, true);
		VisitCodeBlock(block);
	}
	else if (mCurMethodState != NULL)
	{
		bool isIgnore = mBfIRBuilder->mIgnoreWrites;

		mCurMethodState->mInHeadScope = false;

		BfScopeData scopeData;
		if (IsTargetingBeefBackend())
			scopeData.mValueScopeStart = mBfIRBuilder->CreateValueScopeStart();

		mCurMethodState->AddScope(&scopeData);
		if (block != NULL)
		{
			mCurMethodState->mCurScope->mAstBlock = block;
			mCurMethodState->mCurScope->mCloseNode = closeBrace;
		}
		if (labelNode != NULL)
			scopeData.mLabelNode = labelNode->mLabel;
		NewScopeState(block != NULL);
		mCurMethodState->mCurScope->mOuterIsConditional = (flags & BfEmbeddedStatementFlags_IsConditional) != 0;
		mCurMethodState->mCurScope->mIsDeferredBlock = (flags & BfEmbeddedStatementFlags_IsDeferredBlock) != 0;
		mCurMethodState->mCurScope->mExprEvaluator = exprEvaluator;

		//
		{
			SetAndRestoreValue<bool> inDeferredBlock(mCurMethodState->mInDeferredBlock, mCurMethodState->mInDeferredBlock || mCurMethodState->mCurScope->mIsDeferredBlock);
			if (block != NULL)
			{
				if (labelNode != NULL)
					VisitCodeBlock(block, BfIRBlock(), BfIRBlock(), BfIRBlock(), false, NULL, labelNode);
				else
					VisitCodeBlock(block);
			}
			else
			{
				if (auto varDecl = BfNodeDynCast<BfVariableDeclaration>(stmt))
				{
					Fail("Variable declarations must be wrapped in a block statement", varDecl);
				}

				VisitChild(stmt);
			}
		}

		if ((block != NULL) && (closeBrace != NULL))
		{
			UpdateSrcPos(closeBrace);
			if (!mCurMethodState->mLeftBlockUncond)
				EmitEnsureInstructionAt();
		}

		if (block != NULL)
		{
			BfAutoParentNodeEntry autoParentNodeEntry(this, block);
			RestoreScopeState();
		}
		else
			RestoreScopeState();

		BF_ASSERT(isIgnore == mBfIRBuilder->mIgnoreWrites);
	}
	else
	{
		if (block != NULL)
			VisitCodeBlock(block);
		else
			VisitChild(stmt);
	}
}

void BfModule::VisitCodeBlock(BfBlock* block, BfIRBlock continueBlock, BfIRBlock breakBlock, BfIRBlock fallthroughBlock, bool defaultBreak, bool* hadReturn, BfLabelNode* labelNode, bool closeScope, BfEmbeddedStatementFlags flags)
{
	BfBreakData breakData;
	breakData.mIRContinueBlock = continueBlock;
	breakData.mIRBreakBlock = breakBlock;
	breakData.mIRFallthroughBlock = fallthroughBlock;
	breakData.mScope = mCurMethodState->mCurScope;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;

	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);
	VisitEmbeddedStatement(block, NULL, flags);

	if (closeScope)
		RestoreScopeState();

	if ((!mCurMethodState->mLeftBlockUncond) && (defaultBreak))
	{
		mBfIRBuilder->CreateBr(breakBlock);
	}
	if (hadReturn != NULL)
	{
		*hadReturn = mCurMethodState->mHadReturn;
		mCurMethodState->SetHadReturn(false);
		mCurMethodState->mLeftBlockUncond = false;
	}
}

void BfModule::VisitCodeBlock(BfBlock* block)
{
	//BP_ZONE("BfModule::VisitCodeBlock");

	BfAutoParentNodeEntry autoParentNodeEntry(this, block);

	BfIRBlock prevInsertBlock;
	bool hadReturn = false;

	int startLocalMethod = 0; // was -1
	auto rootMethodState = mCurMethodState->GetRootMethodState();

	BfIRBlock startInsertBlock = mBfIRBuilder->GetInsertBlock();

	bool allowLocalMethods = mCurMethodInstance != NULL;
	//int startDeferredLocalIdx = (int)rootMethodState->mDeferredLocalMethods.size();

 	int curLocalMethodIdx = -1;

	// Scan for any local method declarations
	if (allowLocalMethods)
	{
		startLocalMethod = (int)mCurMethodState->mLocalMethods.size();
		curLocalMethodIdx = startLocalMethod;
		auto itr = block->begin();
		while (itr != block->end())
		{
			BfAstNode* child = *itr;
			if (auto localMethodDecl = BfNodeDynCastExact<BfLocalMethodDeclaration>(child))
			{
				BfLocalMethod* localMethod;
				auto rootMethodState = mCurMethodState->GetRootMethodState();

				String methodName;
				if (localMethodDecl->mMethodDeclaration->mNameNode != NULL)
				{
					methodName = GetLocalMethodName(localMethodDecl->mMethodDeclaration->mNameNode->ToString(), localMethodDecl->mMethodDeclaration->mOpenParen, mCurMethodState, mCurMethodState->mMixinState);

					BfLocalMethod** localMethodPtr = NULL;
					if (rootMethodState->mLocalMethodCache.TryGetValue(methodName, &localMethodPtr))
					{
						localMethod = *localMethodPtr;
					}
					else
					{
						localMethod = new BfLocalMethod();
						localMethod->mSystem = mSystem;
						localMethod->mModule = this;
						localMethod->mMethodDeclaration = localMethodDecl->mMethodDeclaration;
						localMethod->mSource = mCurTypeInstance->mTypeDef->mSource;
						localMethod->mSource->mRefCount++;

						if (mCurMethodState->mClosureState != NULL)
							localMethod->mOuterLocalMethod = mCurMethodState->mClosureState->mLocalMethod;

						auto autoComplete = mCompiler->GetAutoComplete();
						if ((autoComplete != NULL) && (autoComplete->mResolveType == BfResolveType_Autocomplete))
						{
							auto autoComplete = mCompiler->mResolvePassData->mAutoComplete;
							if (!autoComplete->IsAutocompleteNode(localMethod->mMethodDeclaration))
								localMethod->mDeclOnly = true;
						}

						if (localMethod->mMethodDeclaration->mNameNode != NULL)
							localMethod->mMethodName = localMethod->mMethodDeclaration->mNameNode->ToString();
						localMethod->mExpectedFullName = methodName;
						rootMethodState->mLocalMethodCache[methodName] = localMethod;
						mContext->mLocalMethodGraveyard.push_back(localMethod);
					}
					BF_ASSERT(mCurMethodState->mCurScope != NULL);
					localMethod->mDeclDIScope = mCurMethodState->mCurScope->mDIScope;
					localMethod->mDeclMethodState = mCurMethodState;
					localMethod->mDeclMixinState = mCurMethodState->mMixinState;
					if (localMethod->mDeclMixinState != NULL)
						localMethod->mDeclMixinState->mHasDeferredUsage = true;
					mCurMethodState->mLocalMethods.push_back(localMethod);

					String* namePtr;
					if (!mCurMethodState->mLocalMethodMap.TryAdd(localMethod->mMethodName, &namePtr, &localMethodPtr))
					{
						BF_ASSERT(localMethod != *localMethodPtr);
						localMethod->mNextWithSameName = *localMethodPtr;
					}
					*localMethodPtr = localMethod;
				}
			}
			++itr;
		}
	}

	bool wantsAllLocalMethods = true;
	auto autoComplete = mCompiler->GetAutoComplete();
	if (autoComplete != NULL)
	{
		// If we only need reasoning "at the cursor" then we don't need all local methods
		if ((!autoComplete->mIsAutoComplete) ||
			(autoComplete->mResolveType == BfResolveType_GetCurrentLocation) ||
			(autoComplete->mResolveType == BfResolveType_GetFixits) ||
			(autoComplete->mResolveType == BfResolveType_GetResultString) ||
			(autoComplete->mResolveType == BfResolveType_GetSymbolInfo) ||
			(autoComplete->mResolveType == BfResolveType_ShowFileSymbolReferences))
			wantsAllLocalMethods = false;
	}

	/*if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mResolveType == BfResolveType_ShowFileSymbolReferences))
	{
		if (mCompiler->mResolvePassData->mSymbolReferenceLocalIdx != -1)
		{
			// We need to reproduce the behavior when we found the symbol - only process autocomplete nodes, otherwise local method ids will be wrong
			//  when we have local methods that were skipped the first time but not the second
			wantsAllLocalMethods = false;
		}
	}*/

	SetAndRestoreValue<bool> prevIgnoreWrite(mBfIRBuilder->mIgnoreWrites);
	bool hadUnreachableCode = false;

	// Handle statements
	auto itr = block->begin();
	while (itr != block->end())
	{
		BfAstNode* child = *itr;

		if (auto localMethodDecl = BfNodeDynCastExact<BfLocalMethodDeclaration>(child))
		{
			/*if ((mCurMethodInstance != NULL) && (mCurMethodInstance->mMethodDef->mMethodType == BfMethodType_Mixin))
				Fail("Mixins cannot contain local methods", child);*/

			if (!allowLocalMethods)
			{
				Fail("Invalid use of local methods", child);
			}
			else if (localMethodDecl->mMethodDeclaration->mNameNode != NULL)
			{
				BfLocalMethod* localMethod = mCurMethodState->mLocalMethods[curLocalMethodIdx];
				BF_ASSERT(localMethod->mMethodDeclaration == localMethodDecl->mMethodDeclaration);

				bool wantsLocalMethod = (wantsAllLocalMethods) || (autoComplete->IsAutocompleteNode(localMethod->mMethodDeclaration));

				if ((!wantsLocalMethod) && (mCurMethodInstance->mMethodDef->mIsLocalMethod))
					wantsLocalMethod = true;

				if (wantsLocalMethod)
				{
					if (!mCurMethodInstance->IsSpecializedGenericMethodOrType())
						GetLocalMethodInstance(localMethod, BfTypeVector(), NULL, true); // Only necessary on unspecialized pass
				}

				if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
					mCompiler->mResolvePassData->mAutoComplete->CheckMethod(localMethod->mMethodDeclaration, true);

				curLocalMethodIdx++;
			}

			++itr;
			continue;
		}

		if ((mCurMethodState != NULL) && (mCurMethodState->mLeftBlockUncond)) // mLeftBlock is cleared after conditional block is completed
		{
			if (mCurMethodState->mHadReturn)
				hadReturn = true;

			if ((!hadUnreachableCode) && (!mCurMethodState->mInPostReturn))
			{
				if ((mCurMethodState->mCurScope == NULL) || (!mCurMethodState->mCurScope->mSupressNextUnreachable))
					Warn(BfWarning_CS0162_UnreachableCode, "Unreachable code", child);

				hadUnreachableCode = true;
				prevInsertBlock = mBfIRBuilder->GetInsertBlock();

				mCurMethodState->mInPostReturn = true;
				mBfIRBuilder->mIgnoreWrites = true;
			}
		}
		if ((mCurMethodState != NULL) && (mCurMethodState->mCurScope != NULL))
			mCurMethodState->mCurScope->mSupressNextUnreachable = false;

		if (itr.IsLast())
		{
			if (auto expr = BfNodeDynCast<BfExpression>(child))
			{
				if (expr->IsExpression())
				{
					if (mCurMethodState != NULL)
					{
						if (mCurMethodState->mCurScope->mExprEvaluator != NULL)
						{
							if ((mAttributeState != NULL) &&
								((mAttributeState->mFlags & (BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)) == (BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)))
							{
								// Resolve as just 'false'
								mCurMethodState->mCurScope->mExprEvaluator->mResult = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_Boolean));
							}
							else
							{
								auto exprEvaluator = mCurMethodState->mCurScope->mExprEvaluator;

								// Evaluate last child as an expression
								exprEvaluator->VisitChild(expr);
								exprEvaluator->FinishExpressionResult();

								if ((exprEvaluator->mResult) && (!exprEvaluator->mResult.mType->IsValuelessType()) && (!exprEvaluator->mResult.mValue.IsConst()) &&
									(!exprEvaluator->mResult.IsAddr()) && (!exprEvaluator->mResult.mValue.IsFake()))
								{
									if ((mCurMethodState->mCurScope != NULL) && (mCurMethodState->mCurScope->mPrevScope != NULL))
									{
										// We need to make sure we don't retain any values through the scope's ValueScopeHardEnd - and extend alloca through previous scope

										bool wasReadOnly = exprEvaluator->mResult.IsReadOnly();
										FixIntUnknown(exprEvaluator->mResult, exprEvaluator->mExpectingType);
										auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();
										auto tempVar = CreateAlloca(exprEvaluator->mResult.mType, false, "blockExpr");
										mBfIRBuilder->SetInsertPointAtStart(startInsertBlock);
										auto lifetimeStart = mBfIRBuilder->CreateLifetimeStart(tempVar);
										mBfIRBuilder->ClearDebugLocation(lifetimeStart);

										if ((!mBfIRBuilder->mIgnoreWrites) && (IsTargetingBeefBackend()))
											mCurMethodState->mCurScope->mPrevScope->mDeferredLifetimeEnds.push_back(tempVar);
										mBfIRBuilder->SetInsertPoint(prevInsertBlock);
										if (exprEvaluator->mResult.IsSplat())
											AggregateSplatIntoAddr(exprEvaluator->mResult, tempVar);
										else
											mBfIRBuilder->CreateAlignedStore(exprEvaluator->mResult.mValue, tempVar, exprEvaluator->mResult.mType->mAlign);
										exprEvaluator->mResult = BfTypedValue(tempVar, exprEvaluator->mResult.mType,
											exprEvaluator->mResult.IsThis() ?
											(wasReadOnly ? BfTypedValueKind_ReadOnlyThisAddr : BfTypedValueKind_ThisAddr) :
											(wasReadOnly ? BfTypedValueKind_ReadOnlyAddr : BfTypedValueKind_Addr));
									}
								}

								if (exprEvaluator->mResult.IsAddr())
								{
									if (mCurMethodState->mCurScope->ExtendLifetime(exprEvaluator->mResult.mValue))
										mBfIRBuilder->CreateLifetimeSoftEnd(exprEvaluator->mResult.mValue);
								}
							}

							break;
						}
						else if (mCurMethodState->InMainMixinScope())
						{
							mCurMethodState->mMixinState->mResultExpr = expr;
							break;
						}
						else if ((mCurMethodInstance != NULL) && (mCurMethodInstance->IsMixin()) && (mCurMethodState->mCurScope == &mCurMethodState->mHeadScope))
						{
							// Only in mixin definition - result ignored
							CreateValueFromExpression(expr, NULL, BfEvalExprFlags_AllowRefExpr);
							break;
						}
						else
						{
 							FailAfter("Expression block cannot be used here. Consider adding semicolon if a statement was intended.", expr);
						}
					}
				}
			}
		}

		UpdateSrcPos(child);
		BfAutoParentNodeEntry autoParentNode(this, child);

		if ((mAttributeState != NULL) &&
			((mAttributeState->mFlags & (BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)) == (BfAttributeState::Flag_StopOnError | BfAttributeState::Flag_HadError)))
		{
			// Ignore child
		}
		else
			child->Accept(this);

		mContext->CheckLockYield();

		++itr;
	}

	if (mCurMethodState != NULL)
	{
		// Any local method that hasn't been called needs to be processed now
		for (int localMethodIdx = startLocalMethod; localMethodIdx < (int)mCurMethodState->mLocalMethods.size(); localMethodIdx++)
		{
			auto localMethod = mCurMethodState->mLocalMethods[localMethodIdx];
			if ((wantsAllLocalMethods) || (autoComplete->IsAutocompleteNode(localMethod->mMethodDeclaration)))
			{
				//??
				auto moduleMethodInstance = GetLocalMethodInstance(localMethod, BfTypeVector(), NULL, true);
			}
			mContext->CheckLockYield();
		}

		while ((int)mCurMethodState->mLocalMethods.size() > startLocalMethod)
		{
			auto localMethod = mCurMethodState->mLocalMethods.back();

#if _DEBUG
			BfLocalMethod** localMethodPtr = NULL;
			mCurMethodState->mLocalMethodMap.TryGetValue(localMethod->mMethodName, &localMethodPtr);
			BF_ASSERT(*localMethodPtr == localMethod);
#endif
			if (localMethod->mNextWithSameName == NULL)
				mCurMethodState->mLocalMethodMap.Remove(localMethod->mMethodName);
			else
			{
				mCurMethodState->mLocalMethodMap[localMethod->mMethodName] = localMethod->mNextWithSameName;
				localMethod->mNextWithSameName = NULL;
			}

			mCurMethodState->mLocalMethods.pop_back();
		}

		if (hadUnreachableCode)
		{
			if (hadReturn)
				mCurMethodState->SetHadReturn(true);
			mCurMethodState->mLeftBlockUncond = true;
			mCurMethodState->mInPostReturn = false;

			if (prevInsertBlock)
				mBfIRBuilder->SetInsertPoint(prevInsertBlock);
		}
	}
}

void BfModule::Visit(BfAstNode* astNode)
{
	AssertErrorState();
}

void BfModule::Visit(BfIdentifierNode* identifierNode)
{
	Visit((BfExpression*)identifierNode);
}

void BfModule::Visit(BfTypeReference* typeRef)
{
	Visit((BfAstNode*)typeRef);
	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		mCompiler->mResolvePassData->mAutoComplete->CheckTypeRef(typeRef, true);
}

void BfModule::Visit(BfEmptyStatement* astNode)
{
}

void BfModule::Visit(BfTryStatement* tryStmt)
{
	Fail("Exceptions not supported", tryStmt->mTryToken);
	VisitChild(tryStmt->mStatement);
}

void BfModule::Visit(BfCatchStatement* catchStmt)
{
	Fail("Exceptions not supported", catchStmt->mCatchToken);
}

void BfModule::Visit(BfFinallyStatement* finallyStmt)
{
	Fail("Exceptions not supported", finallyStmt->mFinallyToken);
	VisitChild(finallyStmt->mStatement);
}

void BfModule::Visit(BfCheckedStatement* checkedStmt)
{
	Fail("'checked' not supported", checkedStmt->mCheckedToken);
	VisitChild(checkedStmt->mStatement);
}

void BfModule::Visit(BfUncheckedStatement* uncheckedStmt)
{
	VisitChild(uncheckedStmt->mStatement);
}

void BfModule::DoIfStatement(BfIfStatement* ifStmt, bool includeTrueStmt, bool includeFalseStmt)
{
	auto autoComplete = mCompiler->GetAutoComplete();
	if (autoComplete != NULL)
		autoComplete->CheckIdentifier(ifStmt->mIfToken, true);

	if (ifStmt->mCondition == NULL)
	{
		AssertErrorState();
		return;
	}

	//TODO: Only conditionally create the scopeData here if we create a variable inside the condition statement

	UpdateSrcPos(ifStmt);

	BfScopeData newScope;
	newScope.mOuterIsConditional = true;
	newScope.mScopeKind = BfScopeKind_StatementTarget;
	if (ifStmt->mLabelNode != NULL)
		newScope.mLabelNode = ifStmt->mLabelNode->mLabel;
	mCurMethodState->AddScope(&newScope);
	NewScopeState();

	BfBreakData breakData;
	breakData.mScope = &newScope;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.mIsIfCondition = true;
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, true);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	BfAutoParentNodeEntry autoParentNodeEntry(this, ifStmt);
	BfTypedValue condValue = CreateValueFromExpression(ifStmt->mCondition, boolType);

	newScope.mScopeKind = BfScopeKind_Normal;

	deferredLocalAssignData.mIsIfCondition = false;

	// The "extend chain" is only valid for the conditional -- since that expression may contain unconditionally executed and
	//  conditionally executed code (in the case of "(GetVal(out a) && GetVal(out b))" for example
	mCurMethodState->mDeferredLocalAssignData->BreakExtendChain();

	if (!condValue)
	{
		AssertErrorState();
		condValue = BfTypedValue(GetDefaultValue(boolType), boolType);
	}

	BfIRBlock trueBB;
	BfIRBlock falseBB;
	bool isConstBranch = false;
	bool constResult = false;
	if (condValue.mValue.IsConst())
	{
		auto constant = mBfIRBuilder->GetConstant(condValue.mValue);
		if ((constant != NULL) && (constant->mTypeCode == BfTypeCode_Boolean))
		{
			isConstBranch = true;
			constResult = constant->mBool;
		}
	}

	if (!isConstBranch)
	{
		trueBB = mBfIRBuilder->CreateBlock("if.then", true);
		falseBB = (ifStmt->mFalseStatement == NULL) ? BfIRBlock() : mBfIRBuilder->CreateBlock("if.else");
	}
	else
		EmitEnsureInstructionAt();

	auto contBB = mBfIRBuilder->CreateBlock("if.end");

	if (!isConstBranch)
	{
		mBfIRBuilder->CreateCondBr(condValue.mValue, trueBB, (falseBB) ? falseBB : contBB);
	}

	// TRUE statement
	bool ignoredLastBlock = true;

	if (includeTrueStmt)
	{
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites);
		SetAndRestoreValue<bool> prevInConstIgnore(mCurMethodState->mCurScope->mInConstIgnore);

		if (trueBB)
			mBfIRBuilder->SetInsertPoint(trueBB);
		if ((isConstBranch) && (constResult != true))
		{
			mBfIRBuilder->mIgnoreWrites = true;
			mCurMethodState->mCurScope->mInConstIgnore = true;
		}
		else
			ignoredLastBlock = false;
		VisitEmbeddedStatement(ifStmt->mTrueStatement);
	}
	prevDLA.Restore();

	if (mCurMethodState->mDeferredLocalAssignData != NULL)
		mCurMethodState->mDeferredLocalAssignData->mHadBreak |= deferredLocalAssignData.mHadBreak;

	bool trueHadReturn = mCurMethodState->mHadReturn;

	// We restore the scopeData before the False block because we don't want variables created in the if condition to
	//   be visible in the false section
	//RestoreScopeState();
	RestoreScoreState_LocalVariables(mCurMethodState->mCurScope->mLocalVarStart);

	if ((!mCurMethodState->mLeftBlockUncond) && (!ignoredLastBlock))
		mBfIRBuilder->CreateBr_NoCollapse(contBB);

	if (mCurMethodState->mLeftBlockUncond)
	{
		deferredLocalAssignData.mLeftBlockUncond = true;
		mCurMethodState->mLeftBlockCond = true;
	}

	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->SetHadReturn(false);

	bool falseHadReturn = false;
	if (ifStmt->mFalseStatement != NULL)
	{
		BfDeferredLocalAssignData falseDeferredLocalAssignData(&newScope);
		falseDeferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
		if (falseBB)
		{
			mBfIRBuilder->AddBlock(falseBB);
			mBfIRBuilder->SetInsertPoint(falseBB);
		}

		ignoredLastBlock = true;
		//
		{
			SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites);
			SetAndRestoreValue<bool> prevInConstIgnore(mCurMethodState->mCurScope->mInConstIgnore);

			if ((isConstBranch) && (constResult != false))
			{
				mBfIRBuilder->mIgnoreWrites = true;
				mCurMethodState->mCurScope->mInConstIgnore = true;
			}
			else
				ignoredLastBlock = false;
			falseDeferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData);
			SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &falseDeferredLocalAssignData);
			if (includeFalseStmt)
				VisitEmbeddedStatement(ifStmt->mFalseStatement, NULL, BfEmbeddedStatementFlags_IsConditional);
		}
		if ((!mCurMethodState->mLeftBlockUncond) && (!ignoredLastBlock))
		{
 			if (IsTargetingBeefBackend())
 			{
				// If we don't do this, then with:
				//  if (a) { } else if (b) { }
				// Then we hit the closing second brace even if 'b' is false
				//SetIllegalSrcPos();
				//BfIRBuilder->ClearDebugLocation();
			}
			auto br = mBfIRBuilder->CreateBr_NoCollapse(contBB);
			//mBfIRBuilder->ClearDebugLocation(br);
		}
		falseHadReturn = mCurMethodState->mHadReturn;
		if (mCurMethodState->mLeftBlockUncond)
		{
			falseDeferredLocalAssignData.mLeftBlockUncond = true;
			mCurMethodState->mLeftBlockCond = true;
		}
		mCurMethodState->mLeftBlockUncond = false;
		mCurMethodState->SetHadReturn(false);

		deferredLocalAssignData.SetIntersection(falseDeferredLocalAssignData);
		mCurMethodState->ApplyDeferredLocalAssignData(deferredLocalAssignData);
	}
	else
	{
		// If we had a const-ignored if statement with no else
		if (ignoredLastBlock)
		{
			if (!mCurMethodState->mLeftBlockUncond)
				mBfIRBuilder->CreateBr_NoCollapse(contBB);
		}
	}

	mBfIRBuilder->AddBlock(contBB);
	mBfIRBuilder->SetInsertPoint(contBB);

	if (isConstBranch)
		mCurMethodState->SetHadReturn(constResult ? trueHadReturn : falseHadReturn);
	else
		mCurMethodState->SetHadReturn(trueHadReturn && falseHadReturn);
	mCurMethodState->mLeftBlockUncond = mCurMethodState->mHadReturn;

	if (mCurMethodState->mHadReturn)
	{
		mBfIRBuilder->EraseFromParent(contBB);
	}
	else
	{
		mBfIRBuilder->SetInsertPoint(contBB);
	}

	RestoreScopeState();
}

void BfModule::Visit(BfIfStatement* ifStmt)
{
	DoIfStatement(ifStmt, true, true);
}

void BfModule::Visit(BfVariableDeclaration* varDecl)
{
	//BP_ZONE("BfModule::Visit(BfVariableDeclaration)");

	UpdateSrcPos(varDecl);

	BfTupleExpression* tupleVariableDeclaration = BfNodeDynCast<BfTupleExpression>(varDecl->mNameNode);
	if (tupleVariableDeclaration != NULL)
	{
		HandleTupleVariableDeclaration(varDecl);
	}
	else
		HandleVariableDeclaration(varDecl);
}

void BfModule::Visit(BfLocalMethodDeclaration* methodDecl)
{
	Fail("Local method declarations must be wrapped in a block statement", methodDecl->mMethodDeclaration->mNameNode);
}

void BfModule::Visit(BfAttributedStatement* attribStmt)
{
	BfAttributeState attributeState;
	attributeState.mSrc = attribStmt->mAttributes;
	attributeState.mTarget = (BfAttributeTargets)(BfAttributeTargets_Invocation | BfAttributeTargets_MemberAccess);
	if (auto block = BfNodeDynCast<BfBlock>(attribStmt->mStatement))
		attributeState.mTarget = BfAttributeTargets_Block;

	attributeState.mCustomAttributes = GetCustomAttributes(attribStmt->mAttributes, attributeState.mTarget);

	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mAttributeState, &attributeState);

	if (auto ignoreErrorsAttrib = attributeState.mCustomAttributes->Get(mCompiler->mIgnoreErrorsAttributeTypeDef))
	{
		SetAndRestoreValue<bool> ignoreErrors(mIgnoreErrors, true);
		if (!ignoreErrorsAttrib->mCtorArgs.IsEmpty())
		{
			auto constant = mCurTypeInstance->mConstHolder->GetConstant(ignoreErrorsAttrib->mCtorArgs[0]);
			if (constant->mBool)
				attributeState.mFlags = BfAttributeState::Flag_StopOnError;
		}
		VisitChild(attribStmt->mStatement);
		attributeState.mUsed = true;
	}
	else if (attributeState.mCustomAttributes->Contains(mCompiler->mConstSkipAttributeTypeDef))
	{
		if ((mCurMethodState == NULL) || (mCurMethodState->mCurScope == NULL) || (!mCurMethodState->mCurScope->mInConstIgnore))
		{
			VisitChild(attribStmt->mStatement);
		}
		else
		{
			BF_ASSERT(mBfIRBuilder->mIgnoreWrites);
		}
		attributeState.mUsed = true;
	}
	else
	{
		VisitChild(attribStmt->mStatement);
	}

	FinishAttributeState(&attributeState);
}

void BfModule::Visit(BfExpression* expression)
{
	UpdateSrcPos(expression);
	BfExprEvaluator exprEvaluator(this);
	exprEvaluator.mUsedAsStatement = true;
	exprEvaluator.Evaluate(expression);
}

void BfModule::Visit(BfExpressionStatement* expressionStmt)
{
	expressionStmt->mExpression->Accept(this);
}

void BfModule::Visit(BfThrowStatement* throwStmt)
{
	if (throwStmt->mExpression == NULL)
	{
		AssertErrorState();
		return;
	}

	UpdateSrcPos(throwStmt->mThrowToken);
	auto throwValue = CreateValueFromExpression(throwStmt->mExpression);
	Fail("Exceptions are not supported", throwStmt->mThrowToken);

	if (mCurMethodInstance->mReturnType->IsVoid())
		EmitReturn(BfTypedValue());
	else
		EmitReturn(GetDefaultTypedValue(mCurMethodInstance->mReturnType));
}

void BfModule::Visit(BfDeleteStatement* deleteStmt)
{
	UpdateSrcPos(deleteStmt);

	auto autoComplete = mCompiler->GetAutoComplete();
	if (autoComplete != NULL)
		autoComplete->CheckIdentifier(deleteStmt->mDeleteToken, true);

	bool isAppendDelete = false;
	BfTypedValue customAllocator;
	if (deleteStmt->mAllocExpr != NULL)
	{
		if (auto expr = BfNodeDynCast<BfExpression>(deleteStmt->mAllocExpr))
			customAllocator = CreateValueFromExpression(expr);
		else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(deleteStmt->mAllocExpr))
		{
			if (tokenNode->mToken == BfToken_Append)
				isAppendDelete = true;
		}
	}

	BfAttributeState attributeState;
	attributeState.mTarget = BfAttributeTargets_Delete;
	SetAndRestoreValue<BfAttributeState*> prevAttributeState(mAttributeState, &attributeState);
	attributeState.mCustomAttributes = GetCustomAttributes(deleteStmt->mAttributes, attributeState.mTarget);

	if (deleteStmt->mExpression == NULL)
	{
		AssertErrorState();
		return;
	}

	auto val = CreateValueFromExpression(deleteStmt->mExpression);
	if (!val)
		return;

	if (val.mType->IsAllocType())
		val.mType = val.mType->GetUnderlyingType();

	BfGenericParamType* genericType = NULL;
	if (val.mType->IsGenericParam())
		genericType = (BfGenericParamType*)val.mType;
	if ((val.mType->IsPointer()) && (val.mType->GetUnderlyingType()->IsGenericParam()))
		genericType = (BfGenericParamType*)val.mType->GetUnderlyingType();

	auto checkType = val.mType;
	if (genericType != NULL)
	{
		BfGenericParamFlags genericParamFlags = BfGenericParamFlag_None;
		BfType* typeConstraint = NULL;
		auto genericParam = GetMergedGenericParamData(genericType, genericParamFlags, typeConstraint);

		if (typeConstraint != NULL)
			checkType = typeConstraint;
		bool canAlwaysDelete = checkType->IsDelegate() || checkType->IsFunction() || checkType->IsArray();
		if (auto checkTypeInst = checkType->ToTypeInstance())
		{
			if ((checkTypeInst->IsInstanceOf(mCompiler->mDelegateTypeDef)) ||
				(checkTypeInst->IsInstanceOf(mCompiler->mFunctionTypeDef)))
				canAlwaysDelete = true;
		}

		if (!canAlwaysDelete)
		{
			if (genericParamFlags & (BfGenericParamFlag_Delete | BfGenericParamFlag_Var))
				return;
			if (genericParamFlags & BfGenericParamFlag_StructPtr)
				return;
			if ((genericParamFlags & BfGenericParamFlag_Struct) && (checkType->IsPointer()))
				return;
			auto genericParamInst = GetGenericParamInstance(genericType);
			Fail(StrFormat("Must add 'where %s : delete' constraint to generic parameter to delete generic type '%s'",
				genericParamInst->GetGenericParamDef()->mName.c_str(), TypeToString(val.mType).c_str()), deleteStmt->mExpression);
			return;
		}
	}

	if (checkType->IsVar())
	{
		// Mixin or unconstrained generic
		return;
	}

	if ((!checkType->IsPointer()) && (!checkType->IsObjectOrInterface()))
	{
		Fail(StrFormat("Cannot delete a value of type '%s'", TypeToString(val.mType).c_str()), deleteStmt->mExpression);
		return;
	}

	if (val.mType->IsGenericParam())
		return;

	auto bodyBB = mBfIRBuilder->CreateBlock("delete.body");
	auto endBB = mBfIRBuilder->CreateBlock("delete.end");

	bool mayBeSentinel = false;

	if (checkType->IsPointer())
	{
		auto innerType = checkType->GetUnderlyingType();
		PopulateType(innerType);
		if (innerType->IsValuelessType())
			mayBeSentinel = true;
	}

	BfIRValue isNotNull;
	if (mayBeSentinel)
	{
		auto intVal = mBfIRBuilder->CreatePtrToInt(val.mValue, BfTypeCode_IntPtr);
		isNotNull = mBfIRBuilder->CreateCmpGT(intVal, mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, 1), false);
	}
	else
	{
		isNotNull = mBfIRBuilder->CreateIsNotNull(val.mValue);
	}
	mBfIRBuilder->CreateCondBr(isNotNull, bodyBB, endBB);

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);

	if (val.mType->IsObjectOrInterface())
	{
		EmitObjectAccessCheck(val);
	}

	SizedArray<BfIRValue, 4> llvmArgs;
	auto bitAddr = mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr));
	llvmArgs.push_back(bitAddr);
	if (val.mType->IsObjectOrInterface())
	{
		auto objectType = mContext->mBfObjectType;
		BfTypeInstance* checkTypeInst = val.mType->ToTypeInstance();

		bool allowPrivate = checkTypeInst == mCurTypeInstance;
		bool allowProtected = allowPrivate || TypeIsSubTypeOf(mCurTypeInstance, checkTypeInst);
		while (checkTypeInst != NULL)
		{
			auto dtorMethodDef = checkTypeInst->mTypeDef->GetMethodByName("~this");
			if (dtorMethodDef)
			{
				if (!CheckProtection(dtorMethodDef->mProtection, checkTypeInst->mTypeDef, allowProtected, allowPrivate))
				{
					auto error = Fail(StrFormat("'%s.~this()' is inaccessible due to its protection level", TypeToString(checkTypeInst).c_str()), deleteStmt->mExpression); // CS0122
				}
			}
			checkTypeInst = checkTypeInst->mBaseType;
			allowPrivate = false;
		}

		if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
		{
			auto preDelete = GetInternalMethod((deleteStmt->mTargetTypeToken != NULL) ? "Dbg_ObjectPreCustomDelete" : "Dbg_ObjectPreDelete");
			SizedArray<BfIRValue, 4> llvmArgs;
			llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(objectType)));
			mBfIRBuilder->CreateCall(preDelete.mFunc, llvmArgs);
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
			expressionEvaluator.CreateCall(deleteStmt->mDeleteToken, methodInstance, mBfIRBuilder->GetFakeVal(), false, llvmArgs);
		}

		if ((deleteStmt->mTargetTypeToken != NULL) && (!isAppendDelete))
		{
			if (deleteStmt->mAllocExpr != NULL)
			{
				if (customAllocator)
				{
					auto customAllocTypeInst = customAllocator.mType->ToTypeInstance();
					if (customAllocTypeInst != NULL)
					{
						if ((customAllocTypeInst != NULL) && (customAllocTypeInst->mTypeDef->GetMethodByName("FreeObject") != NULL))
						{
							BfTypedValueExpression typedValueExpr;
							typedValueExpr.Init(val);
							typedValueExpr.mRefNode = deleteStmt->mAllocExpr;
							BfExprEvaluator exprEvaluator(this);
							SizedArray<BfExpression*, 2> argExprs;
							argExprs.push_back(&typedValueExpr);
							BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
							BfResolvedArgs argValues(&sizedArgExprs);
							exprEvaluator.ResolveArgValues(argValues);
							exprEvaluator.mNoBind = true;
							exprEvaluator.MatchMethod(deleteStmt->mAllocExpr, NULL, customAllocator, false, true, "FreeObject", argValues, BfMethodGenericArguments());
							customAllocator = BfTypedValue();
						}
					}
				}
			}
		}
		else
		{
			if ((mCompiler->mOptions.mEnableRealtimeLeakCheck) && (!mIsComptimeModule))
			{
				SizedArray<BfIRValue, 4> llvmArgs;
				llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(objectType)));
				auto moduleMethodInstance = GetInternalMethod("Dbg_MarkObjectDeleted");
				mBfIRBuilder->CreateCall(moduleMethodInstance.mFunc, llvmArgs);
			}
			else if (!isAppendDelete)
			{
				mBfIRBuilder->CreateCall(GetBuiltInFunc(BfBuiltInFuncType_Free), llvmArgs);
			}
		}
	}
	else
	{
		if ((isAppendDelete) || (customAllocator))
		{
			// Do nothing
		}
		else
		{
			auto func = GetBuiltInFunc(BfBuiltInFuncType_Free);
			if (!func)
			{
				BF_ASSERT(mCompiler->mIsResolveOnly);
			}
			else
				mBfIRBuilder->CreateCall(func, llvmArgs);
		}
	}

	if (customAllocator.mType == GetPrimitiveType(BfTypeCode_NullPtr))
	{
		if (!checkType->IsObjectOrInterface())
			Warn(0, "Type '%' has no destructor, so delete:null has no effect", deleteStmt->mExpression);
	}
	else if (customAllocator)
	{
		auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
		auto ptrValue = BfTypedValue(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(voidPtrType)), voidPtrType);
		BfTypedValueExpression typedValueExpr;
		typedValueExpr.Init(ptrValue);
		BfExprEvaluator exprEvaluator(this);
		SizedArray<BfExpression*, 2> argExprs;
		argExprs.push_back(&typedValueExpr);
		BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
		BfResolvedArgs argValues(&sizedArgExprs);
		exprEvaluator.ResolveArgValues(argValues);
		exprEvaluator.mNoBind = true;
		exprEvaluator.MatchMethod(deleteStmt->mAllocExpr, NULL, customAllocator, false, false, "Free", argValues, BfMethodGenericArguments());
	}

	mBfIRBuilder->CreateBr(endBB);

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);
}

void BfModule::Visit(BfSwitchStatement* switchStmt)
{
	if (mModuleName == "BeefTest_TestProgram")
	{
		NOP;
	}

	BfScopeData outerScope;
	outerScope.mInnerIsConditional = false;
	outerScope.mCloseNode = switchStmt;
	if (switchStmt->mCloseBrace != NULL)
		outerScope.mCloseNode = switchStmt->mCloseBrace;
	mCurMethodState->AddScope(&outerScope);
	NewScopeState();

	auto valueScopeStartOuter = ValueScopeStart();

	BfTypedValue switchValue;
	if (switchStmt->mSwitchValue == NULL)
	{
		AssertErrorState();
		UpdateSrcPos(switchStmt->mSwitchToken);
	}
	else
	{
		UpdateExprSrcPos(switchStmt->mSwitchValue);

		BfEvalExprFlags flags = BfEvalExprFlags_None;
		flags = BfEvalExprFlags_AllowSplat;
		switchValue = CreateValueFromExpression(switchStmt->mSwitchValue, NULL, flags);
	}
	EmitEnsureInstructionAt();

	if (!switchValue)
	{
		AssertErrorState();
		switchValue = GetDefaultTypedValue(mContext->mBfObjectType);
	}

	if (switchValue.mType->IsPointer())
	{
		auto underlyingType = switchValue.mType->GetUnderlyingType();
		if (underlyingType->IsEnum())
		{
			switchValue = LoadValue(switchValue);
			switchValue = BfTypedValue(switchValue.mValue, underlyingType, true);
		}
	}

	// We make the switch value conditional, but all other uses of this scope is conditional since it's conditional on cases
	BfScopeData newScope;
	newScope.mInnerIsConditional = true;
	newScope.mCloseNode = switchStmt;
	if (switchStmt->mCloseBrace != NULL)
		newScope.mCloseNode = switchStmt->mCloseBrace;
	if (switchStmt->mLabelNode != NULL)
		newScope.mLabelNode = switchStmt->mLabelNode->mLabel;
	mCurMethodState->AddScope(&newScope);
	NewScopeState();

	BfTypedValue switchValueAddr = switchValue;

	BfLocalVariable* localDef = new BfLocalVariable();
	localDef->mName = "_";
	localDef->mResolvedType = switchValueAddr.mType;
	localDef->mIsReadOnly = true;
	localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
	if (switchValue.IsAddr())
	{
		localDef->mAddr = switchValue.mValue;
	}
	else
	{
		localDef->mValue = switchValue.mValue;
		localDef->mIsSplat = switchValue.IsSplat();
	}

	bool wantsDebugInfo = mHasFullDebugInfo && !mBfIRBuilder->mIgnoreWrites;

	bool tryExtendValue = false;
	bool addDebugInfo = true;
	if ((wantsDebugInfo) && (!switchValue.mType->IsValuelessType()) && (!switchValue.mType->IsVar()))
	{
		if (IsTargetingBeefBackend())
		{
			// We don't need to make a copy
			if (switchValue.IsSplat())
			{
				localDef->mIsSplat = true;

				if (WantsDebugInfo())
				{
					bool found = false;

					String varName = "_";
					for (auto dbgVar : mCurMethodState->mLocals)
					{
						if (dbgVar->mAddr == switchValue.mValue)
						{
							varName += "$a$" + dbgVar->mName;
							found = true;
							break;
						}
					}

					if (found)
					{
						auto fakeVal = CreateAlloca(GetPrimitiveType(BfTypeCode_Int32), true, "_fake");
						addDebugInfo = false;
						auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope, varName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, mBfIRBuilder->DbgGetType(localDef->mResolvedType), BfIRInitType_NotNeeded_AliveOnDecl);
						mBfIRBuilder->DbgInsertDeclare(fakeVal, diVariable);
					}
				}
			}
			else
			{
// 				if (!localDef->mAddr)
// 				{
// 					BfIRValue value = localDef->mValue;
// 					if (newLocalVar->mConstValue)
// 						value = localDef->mConstValue;
// 					auto aliasValue = mBfIRBuilder->CreateAliasValue(value);
// 					mBfIRBuilder->DbgInsertValueIntrinsic(aliasValue, diVariable);
// 					scopeData.mDeferredLifetimeEnds.push_back(aliasValue);
// 				}

				tryExtendValue = true;
			}
		}
		else if ((switchValue.mType->IsComposite()) && (switchValue.IsAddr()))
		{
			auto refType = CreateRefType(switchValue.mType);
			auto allocaVal = CreateAlloca(refType);
			mBfIRBuilder->CreateStore(switchValue.mValue, allocaVal);

			auto diType = mBfIRBuilder->DbgGetType(refType);
			auto diVariable = mBfIRBuilder->DbgCreateAutoVariable(mCurMethodState->mCurScope->mDIScope,
				localDef->mName, mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType);
			mBfIRBuilder->DbgInsertDeclare(allocaVal, diVariable);
			addDebugInfo = false;
		}
		else
		{
			if (switchValueAddr.IsSplat())
			{
				auto addr = CreateAlloca(switchValue.mType);
				if (switchValue.IsSplat())
					AggregateSplatIntoAddr(switchValue, addr);
				else
					mBfIRBuilder->CreateStore(switchValue.mValue, addr);
				localDef->mAddr = addr;
				localDef->mValue = BfIRValue();
				localDef->mIsSplat = false;
			}
		}
	}

	if (!localDef->mResolvedType->IsVar())
		AddLocalVariableDef(localDef, addDebugInfo, true);

	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	int numExpressions = 0;

	SizedArray<BfIRBlock, 8> blocks;
	SizedArray<BfWhenExpression*, 8> whenExprs;
	SizedArray<BfIRBlock, 8> whenFailBlocks;
	BfIRBlock defaultBlock;
	auto endBlock = mBfIRBuilder->CreateBlock("switch.end");

	for (BfSwitchCase* switchCase : switchStmt->mSwitchCases)
	{
		auto caseBlock = mBfIRBuilder->CreateBlock(StrFormat("switch.%d", blocks.size()));
		blocks.push_back(caseBlock);
		numExpressions += (int)switchCase->mCaseExpressions.size();
	}

	defaultBlock = mBfIRBuilder->CreateBlock("default");
	bool hasDefaultCase = switchStmt->mDefaultCase != NULL;
	if (hasDefaultCase)
		blocks.push_back(defaultBlock);

	SizedArray<BfDeferredLocalAssignData, 8> deferredLocalAssignDataVec;
	deferredLocalAssignDataVec.resize(blocks.size());

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

	BfTypedValue enumTagVal;

	// Declare cases
	int blockIdx = 0;
	bool hadConstIntVals = false;
	bool hadWhen = false;
	BfIRValue switchStatement;
	auto switchBlock = mBfIRBuilder->GetInsertBlock();
	BfIRBlock noSwitchBlock = mBfIRBuilder->CreateBlock("noSwitch", true);

	BfPrimitiveType* intCoercibleType = GetIntCoercibleType(switchValue.mType);

	bool isConstSwitch = false;
	if ((switchValue.mValue.IsConst()) || (switchValue.mType->IsValuelessType()))
	{
		isConstSwitch = true;
	}

	if (switchValue.mValue)
	{
		mBfIRBuilder->PopulateType(switchValue.mType);

		if (intCoercibleType != NULL)
		{
			auto intValue = GetIntCoercible(switchValue);
			switchStatement = mBfIRBuilder->CreateSwitch(intValue.mValue, noSwitchBlock, numExpressions);
		}
		else if (switchValue.mType->IsPayloadEnum())
		{
			enumTagVal = ExtractValue(switchValue, NULL, 2);
			enumTagVal = LoadValue(enumTagVal);
			switchStatement = mBfIRBuilder->CreateSwitch(enumTagVal.mValue, noSwitchBlock, numExpressions);
		}
		else if ((!isConstSwitch) && (!switchValue.mType->IsVar()))
			switchStatement = mBfIRBuilder->CreateSwitch(switchValue.mValue, noSwitchBlock, numExpressions);
	}

	auto valueScopeStartInner = ValueScopeStart();

	mBfIRBuilder->SetInsertPoint(noSwitchBlock);

	bool isPayloadEnum = switchValue.mType->IsPayloadEnum();
	bool isTuple = switchValue.mType->IsTuple();
	bool isIntegralSwitch = switchValue.mType->IsIntegral() || (intCoercibleType != NULL) || ((switchValue.mType->IsEnum()) && (!isPayloadEnum));

	auto _ShowCaseError = [&] (int64 id, BfAstNode* errNode)
	{
		if (isPayloadEnum)
		{
			auto enumType = switchValue.mType->ToTypeInstance();
			for (auto fieldInstance : enumType->mFieldInstances)
			{
				auto fieldDef = fieldInstance.GetFieldDef();
				if (fieldDef->IsEnumCaseEntry())
				{
					int enumIdx = -fieldInstance.mDataIdx - 1;
					if (enumIdx == id)
					{
						Fail(StrFormat("The switch statement already contains a case for the the value '%s'", fieldDef->mName.c_str()), errNode);
						return;
					}
				}
			}
		}

		Fail(StrFormat("The switch statement already contains a case for the the value '%lld'", id), errNode);
	};

	int caseCount = 0;
	bool allHadReturns = true;
	bool hadCondCase = false;
	BfIRBlock lastDefaultBlock;

	struct _CaseState
	{
		BfIRBlock mCondBlock;
		BfIRBlock mUncondBlock;
	};

	bool hadConstMatch = false;

	auto startingLocalVarId = mCurMethodState->GetRootMethodState()->mCurLocalVarId;

	bool prevHadFallthrough = false;

	Dictionary<int64, _CaseState> handledCases;
	for (BfSwitchCase* switchCase : switchStmt->mSwitchCases)
	{
		deferredLocalAssignDataVec[blockIdx].mScopeData = mCurMethodState->mCurScope;
		deferredLocalAssignDataVec[blockIdx].ExtendFrom(mCurMethodState->mDeferredLocalAssignData);
		SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignDataVec[blockIdx]);
		mCurMethodState->mDeferredLocalAssignData->mVarIdBarrier = startingLocalVarId;

		SetIllegalSrcPos();
		auto caseBlock = blocks[blockIdx];

		BfScopeData caseScopeData;
		bool openedScope = false;

		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
		{
			// This does the enum autocomplete popup
			BfAstNode* checkNode = NULL;
			int caseExprIdx = (int)switchCase->mCaseExpressions.size();
			if (caseExprIdx == 0)
				checkNode = switchCase->mCaseToken;
			else if (caseExprIdx - 1 < (int)switchCase->mCaseCommas.size())
				checkNode = switchCase->mCaseCommas[caseExprIdx - 1];
			if (checkNode != NULL)
				mCompiler->mResolvePassData->mAutoComplete->CheckEmptyStart(checkNode, switchValue.mType);
		}

		bool mayHaveMatch = false;

		BfWhenExpression* whenExpr = NULL;

		for (BfExpression* caseExpr : switchCase->mCaseExpressions)
		{
			if (auto checkWhenExpr = BfNodeDynCast<BfWhenExpression>(caseExpr))
			{
				hadWhen = true;
				whenExpr = checkWhenExpr;
			}
		}

		bool wantsOpenedScope = isPayloadEnum || isTuple;

		BfIRBlock lastNotEqBlock;
		for (BfExpression* caseExpr : switchCase->mCaseExpressions)
		{
			BfConstant* constantInt = NULL;

			if (auto checkWhenExpr = BfNodeDynCast<BfWhenExpression>(caseExpr))
				continue;

			if ((!openedScope) && (wantsOpenedScope))
			{
				openedScope = true;

				caseScopeData.mOuterIsConditional = true;
				caseScopeData.mIsSharedTempBlock = true;
				mCurMethodState->AddScope(&caseScopeData);
				NewScopeState();
				UpdateSrcPos(caseExpr);
				SetIllegalSrcPos();
			}

			BfIRValue eqResult;
			BfIRBlock notEqBB;
			bool handled = false;
			BfTypedValue caseValue;
			BfIRBlock doBlock = caseBlock;
			bool hadConditional = false;
			if (isPayloadEnum)
			{
				auto dscrType = switchValue.mType->ToTypeInstance()->GetDiscriminatorType();
				if (!enumTagVal)
				{
					enumTagVal = ExtractValue(switchValue, NULL, 2);
					enumTagVal = LoadValue(enumTagVal);
				}

				notEqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.notEq.%d", blockIdx), false);
				int tagId = -1;
				BfIRBlock matchBlock;
				BfTypedValue eqTypedResult;
				if (auto bindExpr = BfNodeDynCast<BfEnumCaseBindExpression>(caseExpr))
				{
					eqTypedResult = HandleCaseBind(switchValueAddr, enumTagVal, bindExpr, &caseBlock, &notEqBB, &matchBlock, &tagId);
				}
				else
				{
					eqTypedResult = TryCaseEnumMatch(switchValueAddr, enumTagVal, caseExpr, &caseBlock, &notEqBB, &matchBlock, tagId, hadConditional, false, prevHadFallthrough);
					if (hadConditional)
						hadCondCase = true;
				}

				if (tagId != -1)
				{
					doBlock = matchBlock; // Jump to binds rather than just the code
					caseValue = BfTypedValue(GetConstValue(tagId, GetPrimitiveType(dscrType->mTypeDef->mTypeCode)), dscrType);
				}
				else
					hadCondCase = true;

				if (eqTypedResult)
				{
					handled = true;
					eqResult = eqTypedResult.mValue;
				}
			}
			else if (auto tupleExpr = BfNodeDynCast<BfTupleExpression>(caseExpr))
			{
				notEqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.notEq.%d", blockIdx), false);

				BfIRBlock matchBlock;
				BfTypedValue eqTypedResult = TryCaseTupleMatch(switchValue, tupleExpr, &caseBlock, &notEqBB, &matchBlock, hadConditional, false, prevHadFallthrough);
				if (hadConditional)
					hadCondCase = true;
				if (eqTypedResult)
				{
					mayHaveMatch = true;
					handled = true;
					eqResult = eqTypedResult.mValue;
				}
			}

			if (!eqResult)
			{
				caseValue = CreateValueFromExpression(caseExpr, switchValue.mType, (BfEvalExprFlags)(BfEvalExprFlags_AllowEnumId | BfEvalExprFlags_NoCast));
				if (!caseValue)
					continue;
			}

			BfTypedValue caseIntVal = caseValue;
			if ((isIntegralSwitch) || (isPayloadEnum))
			{
				if ((intCoercibleType != NULL) &&
					(caseValue.mType == switchValue.mType) &&
					(caseValue.mValue.IsConst()))
				{
					caseIntVal = GetIntCoercible(caseValue);
					constantInt = mBfIRBuilder->GetConstant(caseIntVal.mValue);
				}
				else
				{
					// For a non-const case, allow for conversion operators, otherwise cast now
					if ((isIntegralSwitch) && (caseValue.mValue.IsConst()))
					{
						if (caseValue.mType != switchValue.mType)
						{
							caseValue = Cast(caseExpr, caseValue, switchValue.mType);
							if (!caseValue)
								continue;
							caseIntVal = caseValue;
						}
					}

					if ((caseValue.mType == switchValue.mType) || (eqResult))
					{
						constantInt = mBfIRBuilder->GetConstant(caseValue.mValue);
						if ((constantInt != NULL) && (!mBfIRBuilder->IsInt(constantInt->mTypeCode)))
							constantInt = NULL;
					}
				}
			}

			if ((!switchStatement) && (!isConstSwitch))
			{
				// Do nothing
				mayHaveMatch = true;
			}
			else if ((constantInt != NULL) && (!hadWhen) && (!isConstSwitch))
			{
				if (!hadConditional)
				{
					_CaseState* caseState = NULL;
					handledCases.TryAdd(constantInt->mInt64, NULL, &caseState);

					if (caseState->mUncondBlock)
					{
						_ShowCaseError(constantInt->mInt64, caseExpr);
					}
					else
					{
						caseState->mUncondBlock = doBlock;
						mBfIRBuilder->AddSwitchCase(switchStatement, caseIntVal.mValue, doBlock);
						hadConstIntVals = true;
					}
				}
				mayHaveMatch = true;
			}
			else if (!handled)
			{
				hadCondCase = true;

				if (!eqResult)
				{
					BfExprEvaluator exprEvaluator(this);
					BfAstNode* refNode = switchCase->mColonToken;

					if ((caseValue.mType->IsPayloadEnum()) && (caseValue.mValue.IsConst()) && (switchValue.mType == caseValue.mType))
					{
						if (!enumTagVal)
						{
							enumTagVal = ExtractValue(switchValue, NULL, 2);
							enumTagVal = LoadValue(enumTagVal);
						}
						eqResult = mBfIRBuilder->CreateCmpEQ(enumTagVal.mValue, caseValue.mValue);
					}
					else
					{
						exprEvaluator.PerformBinaryOperation(switchStmt->mSwitchValue, caseExpr, BfBinaryOp_Equality, refNode, (BfBinOpFlags)(BfBinOpFlag_ForceLeftType), switchValue, caseValue);

						if (switchStmt->mSwitchValue != NULL)
							UpdateSrcPos(switchStmt->mSwitchValue);
						SetIllegalSrcPos();

						eqResult = exprEvaluator.mResult.mValue;
						if (!eqResult)
							eqResult = GetConstValue(0, boolType);
					}
				}

				ValueScopeEnd(valueScopeStartInner);

				bool isConstResult = false;
				bool constResult = false;
				if (eqResult.IsConst())
				{
					auto constant = mBfIRBuilder->GetConstant(eqResult);
					if (constant->mTypeCode == BfTypeCode_Boolean)
					{
						isConstResult = true;
						constResult = constant->mBool;
					}
				}

				if (isConstResult)
				{
					if (constResult)
					{
						mBfIRBuilder->CreateBr(caseBlock);
						mayHaveMatch = true;

						if (whenExpr == NULL)
						{
							hadConstMatch = true;
						}
						else
						{
							notEqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.notEq.%d", blockIdx));

							mBfIRBuilder->AddBlock(notEqBB);
							mBfIRBuilder->SetInsertPoint(notEqBB);
						}
					}
				}
				else
				{
					notEqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.notEq.%d", blockIdx));

					mayHaveMatch = true;
					mBfIRBuilder->CreateCondBr(eqResult, caseBlock, notEqBB);

					mBfIRBuilder->AddBlock(notEqBB);
					mBfIRBuilder->SetInsertPoint(notEqBB);
				}
			}

			if (notEqBB)
				lastNotEqBlock = notEqBB;

			if ((!hadCondCase) && (notEqBB))
				lastDefaultBlock = notEqBB;
		}

		if ((whenExpr != NULL) && (switchCase->mCaseExpressions.size() == 1))
		{
			// This was a "case when" expression, always matches
			mayHaveMatch = true;

			auto notEqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.notEq_when.%d", blockIdx));

			mBfIRBuilder->CreateBr(caseBlock);
			mBfIRBuilder->AddBlock(notEqBB);
			mBfIRBuilder->SetInsertPoint(notEqBB);

			lastNotEqBlock = notEqBB;
		}

		if ((lastDefaultBlock) && (switchStatement))
			mBfIRBuilder->SetSwitchDefaultDest(switchStatement, lastDefaultBlock);

		auto prevInsertBlock = mBfIRBuilder->GetInsertBlock();

		bool isConstIgnore = !mayHaveMatch && !prevHadFallthrough;
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true, isConstIgnore);
		SetAndRestoreValue<bool> prevInConstIgnore(mCurMethodState->mCurScope->mInConstIgnore, true, isConstIgnore);

		mBfIRBuilder->AddBlock(caseBlock);
		mBfIRBuilder->SetInsertPoint(caseBlock);

		if (whenExpr != NULL)
		{
			UpdateSrcPos(whenExpr);

			BfTypedValue whenValue;
			if (whenExpr->mExpression != NULL)
				whenValue = CreateValueFromExpression(whenExpr->mExpression, boolType, BfEvalExprFlags_AllowEnumId);
			if (!whenValue)
			{
				AssertErrorState();
				whenValue = GetDefaultTypedValue(boolType);
			}

			bool constResult = false;
			if (mBfIRBuilder->TryGetBool(whenValue.mValue, constResult))
			{
				if (!constResult)
					prevIgnoreWrites.Set();
			}
			else
			{
				BfIRBlock eqBB = mBfIRBuilder->CreateBlock(StrFormat("switch.when.%d", blockIdx));

				mBfIRBuilder->CreateCondBr(whenValue.mValue, eqBB, lastNotEqBlock);
				mBfIRBuilder->AddBlock(eqBB);
				mBfIRBuilder->SetInsertPoint(eqBB);
			}
		}

		BfIRBlock fallthroughBlock;
		if (blockIdx < (int) blocks.size() - 1)
			fallthroughBlock = blocks[blockIdx + 1];
		else
			fallthroughBlock = defaultBlock;

		bool hadReturn = false;
		if ((switchCase->mCodeBlock != NULL) && (!switchCase->mCodeBlock->mChildArr.IsEmpty()))
		{
			UpdateSrcPos(switchCase->mCodeBlock);

			VisitCodeBlock(switchCase->mCodeBlock, BfIRBlock(), endBlock, fallthroughBlock, true, &hadReturn, switchStmt->mLabelNode, openedScope /*, BfEmbeddedStatementFlags_RescopeDLA*/);

			openedScope = false;
			deferredLocalAssignDataVec[blockIdx].mHadReturn = hadReturn;
			caseCount++;
			if ((!hadReturn) &&
				((!mCurMethodState->mDeferredLocalAssignData->mHadFallthrough) || (mCurMethodState->mDeferredLocalAssignData->mHadBreak)))
				allHadReturns = false;

			if (auto block = BfNodeDynCast<BfBlock>(switchCase->mCodeBlock))
			{
				//
			}
			else
			{
				if (switchStmt->mCloseBrace != NULL)
				{
					UpdateSrcPos(switchStmt->mCloseBrace);
				}
				EmitEnsureInstructionAt();
			}

			//UpdateSrcPos(switchCase->mCodeBlock);
			//SetIllegalSrcPos();

			mBfIRBuilder->ClearDebugLocation();
		}
		else
		{
			if (openedScope)
				RestoreScopeState();
			mBfIRBuilder->CreateBr(endBlock);
			allHadReturns = false;
		}

		prevIgnoreWrites.Restore();

		mBfIRBuilder->SetInsertPoint(prevInsertBlock);

		prevHadFallthrough = mCurMethodState->mDeferredLocalAssignData->mHadFallthrough;

		blockIdx++;
	}

	// Check for comprehensiveness
	bool isComprehensive = true;
	if ((switchValue) && (switchStmt->mDefaultCase == NULL))
	{
		if (switchValue.mType->IsEnum())
		{
			if (hadConstMatch)
			{
				// Already handled
			}
			else
			{
				auto enumType = switchValue.mType->ToTypeInstance();
				if (enumType->IsPayloadEnum())
				{
					int lastTagId = -1;
					for (auto& field : enumType->mFieldInstances)
					{
						auto fieldDef = field.GetFieldDef();
						if (fieldDef == NULL)
							continue;
						if (!fieldDef->IsEnumCaseEntry())
							continue;
						if (field.mDataIdx < 0)
							lastTagId = -field.mDataIdx - 1;
					}
					isComprehensive = lastTagId == (int)handledCases.size() - 1;
				}
				else
				{
					for (auto& field : enumType->mFieldInstances)
					{
						auto fieldDef = field.GetFieldDef();
						if ((fieldDef != NULL) && (fieldDef->IsEnumCaseEntry()))
						{
							if (field.mConstIdx != -1)
							{
								auto constant = enumType->mConstHolder->GetConstantById(field.mConstIdx);
								isComprehensive &= handledCases.ContainsKey(constant->mInt64);
							}
						}
					}
				}
			}

			if (!isComprehensive)
			{
				BfAstNode* refNode = switchStmt->mSwitchToken;
				Fail("Switch must be exhaustive, consider adding a default clause", switchStmt->mSwitchToken);
				if ((switchStmt->mCloseBrace) && (mCompiler->IsAutocomplete()) && (mCompiler->mResolvePassData->mAutoComplete->CheckFixit((refNode))))
				{
					BfParserData* parser = refNode->GetSourceData()->ToParserData();
					if (parser != NULL)
					{
						int fileLoc = switchStmt->mCloseBrace->GetSrcStart();
						mCompiler->mResolvePassData->mAutoComplete->AddEntry(AutoCompleteEntry("fixit", StrFormat("default:\tdefault:|%s|%d||default:", parser->mFileName.c_str(), fileLoc).c_str()));
					}
				}
			}
		}
		else
			isComprehensive = false;
	}

	if (!hadConstMatch)
		mBfIRBuilder->CreateBr(defaultBlock);

	mBfIRBuilder->SetInsertPoint(switchBlock);
	if (!hadConstIntVals)
	{
		if (switchStatement)
			mBfIRBuilder->EraseInstFromParent(switchStatement);
		mBfIRBuilder->CreateBr(noSwitchBlock);
	}

	if (switchStmt->mDefaultCase != NULL)
	{
		SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites, true, hadConstMatch);
		SetAndRestoreValue<bool> prevInConstIgnore(mCurMethodState->mCurScope->mInConstIgnore, true, hadConstMatch);

		mBfIRBuilder->AddBlock(defaultBlock);
		mBfIRBuilder->SetInsertPoint(defaultBlock);

		auto switchCase = switchStmt->mDefaultCase;
		if (switchCase->mCodeBlock != NULL)
		{
			isComprehensive = true;
			UpdateSrcPos(switchCase->mCodeBlock);

			deferredLocalAssignDataVec[blockIdx].mScopeData = mCurMethodState->mCurScope;
			deferredLocalAssignDataVec[blockIdx].ExtendFrom(mCurMethodState->mDeferredLocalAssignData);
			SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignDataVec[blockIdx]);
			mCurMethodState->mDeferredLocalAssignData->mVarIdBarrier = startingLocalVarId;

			BfScopeData caseScopeData;
			caseScopeData.mOuterIsConditional = true;
			caseScopeData.mIsSharedTempBlock = true;
			mCurMethodState->AddScope(&caseScopeData);
			NewScopeState();

			bool hadReturn = false;
			VisitCodeBlock(switchCase->mCodeBlock, BfIRBlock(), endBlock, BfIRBlock(), true, &hadReturn, switchStmt->mLabelNode);
			deferredLocalAssignDataVec[blockIdx].mHadReturn = hadReturn;
			caseCount++;
			if (!hadReturn)
				allHadReturns = false;

			RestoreScopeState();
		}
	}
	else
	{
		mBfIRBuilder->AddBlock(defaultBlock);
		mBfIRBuilder->SetInsertPoint(defaultBlock);
		if (isComprehensive)
		{
			mBfIRBuilder->CreateUnreachable();
			//TODO: This masks a bug in our backend
			if (IsTargetingBeefBackend())
				mBfIRBuilder->CreateBr(endBlock);
		}
		else
			mBfIRBuilder->CreateBr(endBlock);
	}

	if (isComprehensive)
	{
		// Merge and apply deferred local assign data
		//  We only do this if there's a default case, otherwise we assume we may have missed a case
		//  that by definition had no local assigns
		BfDeferredLocalAssignData* mergedDeferredLocalAssignData = NULL;
		for (blockIdx = 0; blockIdx < (int)blocks.size(); blockIdx++)
		{
			auto deferredLocalAssignData = &deferredLocalAssignDataVec[blockIdx];
			if (deferredLocalAssignData->mHadFallthrough)
				continue;

			if (mergedDeferredLocalAssignData == NULL)
				mergedDeferredLocalAssignData = deferredLocalAssignData;
			else
				mergedDeferredLocalAssignData->SetIntersection(*deferredLocalAssignData);
		}

		if (mergedDeferredLocalAssignData != NULL)
			mCurMethodState->ApplyDeferredLocalAssignData(*mergedDeferredLocalAssignData);
	}

	if ((caseCount > 0) && (allHadReturns) &&
		((hasDefaultCase) || (isComprehensive)))
	{
		mCurMethodState->SetHadReturn(true);
		mCurMethodState->mLeftBlockUncond = true;

		if ((!hasDefaultCase) && (!isComprehensive))
			mBfIRBuilder->DeleteBlock(endBlock);
		else
		{
			if (switchStmt->mCloseBrace != NULL)
				UpdateSrcPos(switchStmt->mCloseBrace);
			mBfIRBuilder->AddBlock(endBlock);
			mBfIRBuilder->SetInsertPoint(endBlock);
			mBfIRBuilder->CreateUnreachable();
		}
	}
	else
	{
		if (switchStmt->mCloseBrace != NULL)
			UpdateSrcPos(switchStmt->mCloseBrace);

		mBfIRBuilder->AddBlock(endBlock);
		mBfIRBuilder->SetInsertPoint(endBlock);
	}

	BfIRValue lifetimeExtendVal;
	if (tryExtendValue)
	{
		if (localDef->mAddr)
			lifetimeExtendVal = localDef->mAddr;
		else
			lifetimeExtendVal = localDef->mValue;
	}

	RestoreScopeState(); // newScope
	RestoreScopeState(); // outerScope

	if (lifetimeExtendVal)
		mBfIRBuilder->CreateLifetimeExtend(lifetimeExtendVal);

	ValueScopeEnd(valueScopeStartOuter);
}

static int gRetIdx = 0;

void BfModule::Visit(BfReturnStatement* returnStmt)
{
	if ((mCurMethodInstance == NULL) || (mCurMethodState->mDisableReturns))
	{
		Fail("Unexpected return", returnStmt);
		if (returnStmt->mExpression != NULL)
		{
			BfExprEvaluator exprEvaluator(this);
			CreateValueFromExpression(exprEvaluator, returnStmt->mExpression);
		}
		return;
	}

	UpdateSrcPos(returnStmt);
	EmitEnsureInstructionAt();

	auto retType = mCurMethodInstance->mReturnType;
	if (mCurMethodInstance->IsMixin())
		retType = NULL;

	bool inferReturnType = false;
	if (mCurMethodState->mClosureState != NULL)
	{
		retType = mCurMethodState->mClosureState->mReturnType;
		inferReturnType = (mCurMethodState->mClosureState->mReturnTypeInferState != BfReturnTypeInferState_None);
	}

	auto checkScope = mCurMethodState->mCurScope;
	while (checkScope != NULL)
	{
		if (checkScope->mIsDeferredBlock)
		{
			Fail("Deferred blocks cannot contain 'return' statements", returnStmt);
			if (returnStmt->mExpression != NULL)
			{
				BfExprEvaluator exprEvaluator(this);
				CreateValueFromExpression(exprEvaluator, returnStmt->mExpression, GetPrimitiveType(BfTypeCode_Var), BfEvalExprFlags_None);
			}
			return;
		}
		if (checkScope->mInInitBlock)
		{
			Fail("Initialization blocks cannot contain 'return' statements", returnStmt);
		}
		checkScope = checkScope->mPrevScope;
	}

	auto checkLocalAssignData = mCurMethodState->mDeferredLocalAssignData;
	while (checkLocalAssignData != NULL)
	{
		if (checkLocalAssignData->mScopeData != NULL)
			checkLocalAssignData->mLeftBlock = true;
		checkLocalAssignData = checkLocalAssignData->mChainedAssignData;
	}

	if ((retType == NULL) && (!inferReturnType))
	{
		if (returnStmt->mExpression != NULL)
		{
			BfExprEvaluator exprEvaluator(this);
			CreateValueFromExpression(exprEvaluator, returnStmt->mExpression, GetPrimitiveType(BfTypeCode_Var), BfEvalExprFlags_None);
		}
		MarkScopeLeft(&mCurMethodState->mHeadScope);
		return;
	}

	if (returnStmt->mExpression == NULL)
	{
		MarkScopeLeft(&mCurMethodState->mHeadScope);

		if ((retType != NULL) && (retType->IsVoid()))
		{
			EmitReturn(BfTypedValue());
			return;
		}

		Fail("Expected return value", returnStmt);
		if (retType != NULL)
			EmitReturn(GetDefaultTypedValue(retType));
		else
			EmitReturn(BfTypedValue());
		return;
	}

	BfType* expectingReturnType = retType;
	BfType* origType;
	BfExprEvaluator exprEvaluator(this);
	bool alreadyWritten = false;
	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
		exprEvaluator.mReceivingValue = &mCurMethodState->mRetVal;
	if (mCurMethodInstance->mMethodDef->mIsReadOnly)
		exprEvaluator.mAllowReadOnlyReference = true;

	if (inferReturnType)
		expectingReturnType = NULL;

	auto retValue = CreateValueFromExpression(exprEvaluator, returnStmt->mExpression, expectingReturnType, BfEvalExprFlags_AllowRefExpr, &origType);

	if ((retValue) && (inferReturnType))
	{
		if (mCurMethodState->mClosureState->mReturnType == NULL)
			mCurMethodState->mClosureState->mReturnType = retValue.mType;
		else
		{
			if ((retValue.mType == mCurMethodState->mClosureState->mReturnType) ||
				(CanCast(retValue, mCurMethodState->mClosureState->mReturnType)))
			{
				// Leave as-is
			}
			else if (CanCast(GetFakeTypedValue(mCurMethodState->mClosureState->mReturnType), retValue.mType))
			{
				mCurMethodState->mClosureState->mReturnType = retValue.mType;
			}
			else
			{
				mCurMethodState->mClosureState->mReturnTypeInferState = BfReturnTypeInferState_Fail;
			}
		}
	}
	if ((retType == NULL) && (inferReturnType))
		retType = mCurMethodState->mClosureState->mReturnType;
	if (retType == NULL)
		retType = GetPrimitiveType(BfTypeCode_None);

	if ((!mIsComptimeModule) && (mCurMethodInstance->GetStructRetIdx() != -1))
		alreadyWritten = exprEvaluator.mReceivingValue == NULL;
	MarkScopeLeft(&mCurMethodState->mHeadScope);

	if (!retValue)
	{
		AssertErrorState();
		if ((expectingReturnType != NULL) && (!expectingReturnType->IsVoid()))
		{
			retValue = GetDefaultTypedValue(expectingReturnType, true);
		}
		else
		{
			EmitReturn(BfTypedValue());
			return;
		}
	}

	if (retValue.mType->IsVar())
	{
		EmitReturn(BfTypedValue());
	}
	else if (retValue.mType->IsVoid())
	{
		if (retType->IsVoid())
		{
			Warn(0, "Returning void value", returnStmt->mReturnToken);
			EmitReturn(BfTypedValue());
		}
	}
	else
	{
		if (retType->IsVoid())
		{
			expectingReturnType = NULL;
			Fail("Attempting to return value from void method", returnStmt->mExpression);
			EmitReturn(BfTypedValue());
			return;
		}

		if ((origType != NULL) && (origType->IsStructOrStructPtr()) && (retValue.mType->IsObjectOrInterface()))
		{
			Fail(StrFormat("Stack boxing of type '%s' is not allowed on return statements. Use 'new box' to box on the heap.", TypeToString(origType).c_str()), returnStmt->mExpression);
		}

		if (!alreadyWritten)
			EmitReturn(LoadOrAggregateValue(retValue));
		else
			EmitReturn(BfTypedValue());
	}
}

void BfModule::Visit(BfYieldStatement* yieldStmt)
{
	Fail("Yield not supported", yieldStmt);
}

void BfModule::Visit(BfBreakStatement* breakStmt)
{
	bool inMixinDecl = (mCurMethodInstance != NULL) && (mCurMethodInstance->IsMixin());

	UpdateSrcPos(breakStmt);
	EmitEnsureInstructionAt();

	BfBreakData* breakData = mCurMethodState->mBreakData;
	if (breakStmt->mLabel != NULL)
	{
		breakData = FindBreakData(breakStmt->mLabel);
	}
	else
	{
		while (breakData != NULL)
		{
			if (breakData->mIRBreakBlock)
				break;
			breakData = breakData->mPrevBreakData;
		}
	}

	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		BfScopeData* scope = NULL;
		if (breakData != NULL)
			scope = breakData->mScope;
		if (auto identifer = BfNodeDynCast<BfIdentifierNode>(breakStmt->mLabel))
			mCompiler->mResolvePassData->mAutoComplete->CheckLabel(identifer, breakStmt->mBreakNode, scope);
	}

	if ((breakData == NULL) || (!breakData->mIRBreakBlock))
	{
		if (inMixinDecl)
		{
			// Our mixin may just require that we're injected into a breakable scope
		}
		else
			Fail("'break' not applicable in this block", breakStmt);
		return;
	}

	if (mCurMethodState->mInDeferredBlock)
	{
		auto checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			if (checkScope == breakData->mScope)
				break;
			if (checkScope->mIsDeferredBlock)
			{
 				Fail("The break target crosses a deferred block boundary", breakStmt);
 				return;
			}
			checkScope = checkScope->mPrevScope;
		}
	}

	if (HasDeferredScopeCalls(breakData->mScope))
	{
		EmitDeferredScopeCalls(true, breakData->mScope, breakData->mIRBreakBlock);
	}
	else
	{
		mBfIRBuilder->CreateBr(breakData->mIRBreakBlock);
	}
	mCurMethodState->mLeftBlockUncond = true;

	bool isCond = false;
	int uncondScopeDepth = 0;
	if (mCurMethodState->mCurScope != NULL)
		uncondScopeDepth = mCurMethodState->mCurScope->mScopeDepth + 1;

	BfIRValue earliestValueScopeStart;
	auto checkScope = mCurMethodState->mCurScope;
	while (checkScope != NULL)
	{
		if (!isCond)
			uncondScopeDepth = checkScope->mScopeDepth;
		if ((checkScope->mOuterIsConditional) && (!checkScope->mIsSharedTempBlock))
			isCond = true;

		if (checkScope->mValueScopeStart)
			earliestValueScopeStart = checkScope->mValueScopeStart;
		if (checkScope == breakData->mScope)
			break;

		checkScope = checkScope->mPrevScope;
	}

	auto checkLocalAssignData = mCurMethodState->mDeferredLocalAssignData;
	while (checkLocalAssignData != NULL)
	{
		if ((checkLocalAssignData->mScopeData != NULL) && (checkLocalAssignData->mScopeData->mScopeDepth >= breakData->mScope->mScopeDepth))
		{
			if (checkLocalAssignData->mScopeData->mScopeDepth >= uncondScopeDepth)
				checkLocalAssignData->mLeftBlockUncond = true;
			checkLocalAssignData->mLeftBlock = true;
			checkLocalAssignData->mHadBreak = true;
		}
		checkLocalAssignData = checkLocalAssignData->mChainedAssignData;
	}

	MarkScopeLeft(breakData->mScope);
	ValueScopeEnd(earliestValueScopeStart);

	auto checkBreakData = mCurMethodState->mBreakData;
	while (true)
	{
		checkBreakData->mHadBreak = true;
		if (checkBreakData == breakData)
			break;
		checkBreakData = checkBreakData->mPrevBreakData;
	}
}

void BfModule::Visit(BfContinueStatement* continueStmt)
{
	bool inMixinDecl = (mCurMethodInstance != NULL) && (mCurMethodInstance->IsMixin());

	UpdateSrcPos(continueStmt);
	EmitEnsureInstructionAt();

	// If we're in a switch, 'break' is valid but we need to continue looking outward for a 'continue' target
	BfBreakData* breakData = mCurMethodState->mBreakData;
	if (continueStmt->mLabel != NULL)
	{
		breakData = FindBreakData(continueStmt->mLabel);
		if ((breakData != NULL) && (!breakData->mIRContinueBlock))
		{
			Fail(StrFormat("'continue' not applicable in '%s", continueStmt->mLabel->ToString().c_str()), continueStmt);
			return;
		}
	}
	else
	{
		while (breakData != NULL)
		{
			if (breakData->mIRContinueBlock)
				break;
			breakData = breakData->mPrevBreakData;
		}
	}

	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		BfScopeData* scope = NULL;
		if (breakData != NULL)
			scope = breakData->mScope;
		if (auto identifer = BfNodeDynCast<BfIdentifierNode>(continueStmt->mLabel))
			mCompiler->mResolvePassData->mAutoComplete->CheckLabel(identifer, continueStmt->mContinueNode, scope);
	}

	if ((breakData == NULL) || (!breakData->mIRContinueBlock))
	{
		if (inMixinDecl)
		{
			// Our mixin may just require that we're injected into a breakable scope
		}
		else
			Fail("'continue' not applicable in this block", continueStmt);
		return;
	}

	BfIRValue earliestValueScopeStart;

	// We don't want to close out our own scope, we want to close out any scopes that were opened after us
	auto nextScope = mCurMethodState->mCurScope;
	while (nextScope != NULL)
	{
		if (nextScope->mValueScopeStart)
			earliestValueScopeStart = nextScope->mValueScopeStart;
		if (nextScope->mPrevScope == breakData->mScope)
			break;
		nextScope = nextScope->mPrevScope;
	}

	if (breakData->mInnerValueScopeStart)
		earliestValueScopeStart = breakData->mInnerValueScopeStart;

	if (mCurMethodState->mInDeferredBlock)
	{
		auto checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			if (checkScope == breakData->mScope)
				break;
			if (checkScope->mIsDeferredBlock)
			{
				Fail("The continue target crosses a deferred block boundary", continueStmt);
				return;
			}
			checkScope = checkScope->mPrevScope;
		}
	}

	if ((nextScope != NULL) && (HasDeferredScopeCalls(nextScope)))
	{
		EmitDeferredScopeCalls(true, nextScope, breakData->mIRContinueBlock);
	}
	else
	{
		mBfIRBuilder->CreateBr(breakData->mIRContinueBlock);
	}
	MarkScopeLeft(breakData->mScope);
	ValueScopeEnd(earliestValueScopeStart);

	mCurMethodState->mLeftBlockUncond = true;
	if (!mCurMethodState->mInPostReturn)
		mCurMethodState->mHadContinue = true;
}

void BfModule::Visit(BfFallthroughStatement* fallthroughStmt)
{
	UpdateSrcPos(fallthroughStmt);
	BfBreakData* breakData = mCurMethodState->mBreakData;
	while (breakData != NULL)
	{
		if (breakData->mIRFallthroughBlock)
			break;
		breakData = breakData->mPrevBreakData;
	}

	if (mCurMethodState->mInDeferredBlock)
	{
		auto checkScope = mCurMethodState->mCurScope;
		while (checkScope != NULL)
		{
			if (checkScope == breakData->mScope)
				break;
			if (checkScope->mIsDeferredBlock)
			{
				Fail("The fallthrough crosses a deferred block boundary", fallthroughStmt);
				return;
			}
			checkScope = checkScope->mPrevScope;
		}
	}

	if (breakData == NULL)
	{
		Fail("'fallthrough' not applicable in this block", fallthroughStmt);
		return;
	}
	EmitDeferredScopeCalls(true, breakData->mScope, breakData->mIRFallthroughBlock);
	mCurMethodState->mLeftBlockUncond = true; // Not really a return, but handled the same way
	if (mCurMethodState->mDeferredLocalAssignData != NULL)
		mCurMethodState->mDeferredLocalAssignData->mHadFallthrough = true;
}

void BfModule::Visit(BfUsingStatement* usingStmt)
{
	UpdateSrcPos(usingStmt);

	mCurMethodState->mInHeadScope = false;
	BfScopeData newScope;
	mCurMethodState->AddScope(&newScope);
	NewScopeState();
	if (usingStmt->mVariableDeclaration != NULL)
		UpdateSrcPos(usingStmt->mVariableDeclaration);

	BfTypedValue embeddedValue;
	SizedArray<BfIRValue, 1> llvmArgs;
	BfModuleMethodInstance moduleMethodInstance;

	BfFunctionBindResult functionBindResult;
	BfExprEvaluator exprEvaluator(this);

	bool failed = false;

	if (usingStmt->mVariableDeclaration == NULL)
	{
		AssertErrorState();
		failed = true;
	}
	else if (usingStmt->mVariableDeclaration->mNameNode != NULL)
	{
		BfLocalVariable* localVar = HandleVariableDeclaration(usingStmt->mVariableDeclaration);
		if (localVar == NULL)
		{
			AssertErrorState();
			failed = true;
		}
		else
		{
			embeddedValue = exprEvaluator.LoadLocal(localVar);
		}
		//exprEvaluator.CheckModifyResult(embeddedValue, usingStmt->mVariableDeclaration->mNameNode,);
	}
	else
	{
		embeddedValue = CreateValueFromExpression(usingStmt->mVariableDeclaration->mInitializer);
		if (!embeddedValue)
			failed = true;
	}

	if (!failed)
	{
		auto iDisposableType = ResolveTypeDef(mCompiler->mIDisposableTypeDef)->ToTypeInstance();
		auto dispMethod = GetMethodByName(iDisposableType, "Dispose");

		if ((!dispMethod) || (!CanCast(embeddedValue, iDisposableType)))
		{
			Fail(StrFormat("Type '%s' must be implicitly convertible to 'System.IDisposable' for use in 'using' statement", TypeToString(embeddedValue.mType).c_str()), usingStmt->mVariableDeclaration);
			failed = true;
		}
		else
		{
			bool mayBeNull = true;
			if (embeddedValue.mType->IsStruct())
			{
				// It's possible that a struct can convert to an IDisposable through a conversion operator that CAN
				//  return null, so the only way we can know we are not null is if we are a struct that directly
				//  implements the interface
				if (TypeIsSubTypeOf(embeddedValue.mType->ToTypeInstance(), iDisposableType))
					mayBeNull = false;
			}

			exprEvaluator.mFunctionBindResult = &functionBindResult;
			SizedArray<BfResolvedArg, 0> resolvedArgs;
			BfMethodMatcher methodMatcher(usingStmt->mVariableDeclaration, this, dispMethod.mMethodInstance, resolvedArgs, BfMethodGenericArguments());
			methodMatcher.CheckType(iDisposableType, embeddedValue, false);
			methodMatcher.TryDevirtualizeCall(embeddedValue);
			auto retVal = exprEvaluator.CreateCall(&methodMatcher, embeddedValue);
			if (functionBindResult.mMethodInstance != NULL)
			{
				moduleMethodInstance = BfModuleMethodInstance(functionBindResult.mMethodInstance, functionBindResult.mFunc);
				AddDeferredCall(moduleMethodInstance, functionBindResult.mIRArgs, mCurMethodState->mCurScope, NULL, false, mayBeNull);
			}
		}
	}

	if (usingStmt->mEmbeddedStatement == NULL)
	{
		AssertErrorState();
	}
	else
	{
		VisitEmbeddedStatement(usingStmt->mEmbeddedStatement);
	}

	RestoreScopeState();
}

void BfModule::Visit(BfDoStatement* doStmt)
{
	UpdateSrcPos(doStmt);

	auto bodyBB = mBfIRBuilder->CreateBlock("do.body", true);
	auto endBB = mBfIRBuilder->CreateBlock("do.end");

	BfScopeData scopeData;
	if (doStmt->mLabelNode != NULL)
		scopeData.mLabelNode = doStmt->mLabelNode->mLabel;
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	BfBreakData breakData;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &scopeData;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	mBfIRBuilder->CreateBr(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);
	VisitEmbeddedStatement(doStmt->mEmbeddedStatement);

	prevDLA.Restore();
	mCurMethodState->ApplyDeferredLocalAssignData(deferredLocalAssignData);

	RestoreScopeState();

	if (!mCurMethodState->mLeftBlockUncond)
		mBfIRBuilder->CreateBr(endBB);
	mCurMethodState->SetHadReturn(false);
	mCurMethodState->mLeftBlockUncond = false;

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);
}

void BfModule::Visit(BfRepeatStatement* repeatStmt)
{
// 	if (repeatStmt->mCondition != NULL)
// 		UpdateSrcPos(repeatStmt->mCondition);
// 	else
		UpdateSrcPos(repeatStmt);

	if (repeatStmt->mRepeatToken->mToken == BfToken_Do)
	{
		Fail("Repeat block requires 'repeat' token", repeatStmt->mRepeatToken);
	}

	auto bodyBB = mBfIRBuilder->CreateBlock("repeat.body", true);
	auto condBB = mBfIRBuilder->CreateBlock("repeat.cond");
	auto endBB = mBfIRBuilder->CreateBlock("repeat.end");

	BfScopeData scopeData;
	// We set mIsLoop later
	if (repeatStmt->mLabelNode != NULL)
		scopeData.mLabelNode = repeatStmt->mLabelNode->mLabel;
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	BfBreakData breakData;
	breakData.mIRContinueBlock = condBB;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &scopeData;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	mBfIRBuilder->CreateBr(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);
	scopeData.mIsLoop = true;

	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	VisitEmbeddedStatement(repeatStmt->mEmbeddedStatement);

	if (!mCurMethodState->mLeftBlockUncond)
		mBfIRBuilder->CreateBr(condBB);
	mCurMethodState->SetHadReturn(false);
	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->mLeftBlockCond = false;

	mBfIRBuilder->AddBlock(condBB);
	mBfIRBuilder->SetInsertPoint(condBB);

	bool isInfiniteLoop = false;

	if (repeatStmt->mCondition != NULL)
	{
		UpdateSrcPos(repeatStmt->mCondition);
		auto checkVal = CreateValueFromExpression(repeatStmt->mCondition, GetPrimitiveType(BfTypeCode_Boolean));
		if (checkVal)
		{
			if ((!breakData.mHadBreak) && (checkVal.mValue.IsConst()))
			{
				auto constVal = mBfIRBuilder->GetConstantById(checkVal.mValue.mId);
				if (constVal->mTypeCode == BfTypeCode_Boolean)
					isInfiniteLoop = constVal->mBool;
			}

			mBfIRBuilder->CreateCondBr(checkVal.mValue, bodyBB, endBB);
			mBfIRBuilder->AddBlock(endBB);
			mBfIRBuilder->SetInsertPoint(endBB);
		}
	}

	RestoreScopeState();

	prevDLA.Restore();
	mCurMethodState->ApplyDeferredLocalAssignData(deferredLocalAssignData);

	if (isInfiniteLoop)
		EmitDefaultReturn();
}

void BfModule::Visit(BfWhileStatement* whileStmt)
{
	UpdateSrcPos(whileStmt);

	bool prevHadReturn = mCurMethodState->mHadReturn;

	auto condBB = mBfIRBuilder->CreateBlock("while.cond");
	auto bodyBB = mBfIRBuilder->CreateBlock("while.body");
	auto endBB = mBfIRBuilder->CreateBlock("while.end");

	mCurMethodState->mInHeadScope = false;
	BfScopeData scopeData;
	scopeData.mScopeKind = BfScopeKind_StatementTarget_Conditional;
	scopeData.mIsLoop = true;
	if (whileStmt->mLabelNode != NULL)
		scopeData.mLabelNode = whileStmt->mLabelNode->mLabel;
	scopeData.mValueScopeStart = ValueScopeStart();
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	BfBreakData breakData;
	breakData.mIRContinueBlock = condBB;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &scopeData;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	breakData.mInnerValueScopeStart = scopeData.mValueScopeStart;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	mBfIRBuilder->AddBlock(condBB);
	mBfIRBuilder->CreateBr(condBB);
	mBfIRBuilder->SetInsertPoint(condBB);

	BfTypedValue checkVal;
	if (whileStmt->mCondition != NULL)
	{
		UpdateSrcPos(whileStmt->mCondition);
		checkVal = CreateValueFromExpression(whileStmt->mCondition, GetPrimitiveType(BfTypeCode_Boolean));
	}

	if (!checkVal)
	{
		AssertErrorState();
		checkVal = GetDefaultTypedValue(GetPrimitiveType(BfTypeCode_Boolean));
	}

	bool isInfiniteLoop = false;
	bool isFalseLoop = false;

	if (checkVal.mValue.IsConst())
	{
		EmitEnsureInstructionAt();

		auto constVal = mBfIRBuilder->GetConstantById(checkVal.mValue.mId);
		if (constVal->mTypeCode == BfTypeCode_Boolean)
		{
			isInfiniteLoop = constVal->mBool;
			isFalseLoop = !isInfiniteLoop;
		}
	}

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	// For BeefBackend we continue to do CondBr because it helps our flow analysis and we optimize it anyway
	if ((isInfiniteLoop) && (!IsTargetingBeefBackend()))
		mBfIRBuilder->CreateBr(bodyBB);
	else if (isFalseLoop)
		mBfIRBuilder->CreateBr(endBB);
	else
		mBfIRBuilder->CreateCondBr(checkVal.mValue, bodyBB, endBB);

	// Apply deferred local assign to mEmbeddedStatement and itrStmt
	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);
	deferredLocalAssignData.mIsUnconditional = isInfiniteLoop;

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);
	if (whileStmt->mEmbeddedStatement != NULL)
	{
		if (isFalseLoop)
		{
			SetAndRestoreValue<bool> ignoreWrites(mBfIRBuilder->mIgnoreWrites, true);
			SetAndRestoreValue<bool> prevInConstIgnore(mCurMethodState->mCurScope->mInConstIgnore, true);
			VisitEmbeddedStatement(whileStmt->mEmbeddedStatement);
		}
		else
			VisitEmbeddedStatement(whileStmt->mEmbeddedStatement);
	}
	else
	{
		AssertErrorState();
	}
	if (breakData.mHadBreak)
	{
		isInfiniteLoop = false;
	}

	if ((!mCurMethodState->mLeftBlockUncond) && (!isFalseLoop))
	{
		mBfIRBuilder->CreateBr(condBB);
	}

	if (!isInfiniteLoop)
		mCurMethodState->mHadReturn = prevHadReturn;

	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->mLeftBlockCond = false;

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);

	if (isFalseLoop)
	{
		mBfIRBuilder->EraseFromParent(bodyBB);
	}

	RestoreScopeState();

	if ((isInfiniteLoop) && (mCurMethodInstance != NULL))
		EmitDefaultReturn();
}

void BfModule::Visit(BfForStatement* forStmt)
{
	auto autoComplete = mCompiler->GetAutoComplete();
	if (autoComplete != NULL)
		autoComplete->CheckIdentifier(forStmt->mForToken, true);

	UpdateSrcPos(forStmt);

	auto startBB = mBfIRBuilder->CreateBlock("for.start", true);
	mBfIRBuilder->CreateBr(startBB);
	mBfIRBuilder->SetInsertPoint(startBB);

	BfScopeData scopeData;
	scopeData.mIsLoop = true;
	if (forStmt->mLabelNode != NULL)
		scopeData.mLabelNode = forStmt->mLabelNode->mLabel;
	scopeData.mCloseNode = forStmt;
	scopeData.mValueScopeStart = ValueScopeStart();
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	for (auto initializer : forStmt->mInitializers)
	{
		VisitChild(initializer);
	}

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	auto boolType = GetPrimitiveType(BfTypeCode_Boolean);

	auto condBB = mBfIRBuilder->CreateBlock("for.cond", true);
	auto bodyBB = mBfIRBuilder->CreateBlock("for.body");
	auto incBB = mBfIRBuilder->CreateBlock("for.inc");
	auto endBB = mBfIRBuilder->CreateBlock("for.end");

	BfBreakData breakData;
	breakData.mIRContinueBlock = incBB;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &scopeData;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	mBfIRBuilder->CreateBr(condBB);

	bool isInfiniteLoop = false;
	mBfIRBuilder->SetInsertPoint(condBB);
	if (forStmt->mCondition != NULL)
	{
		auto conditionValue = CreateValueFromExpression(forStmt->mCondition, boolType);
		if (!conditionValue)
			conditionValue = GetDefaultTypedValue(boolType);
		auto constant = mBfIRBuilder->GetConstant(conditionValue.mValue);
		if ((constant != NULL) && (constant->mTypeCode == BfTypeCode_Boolean))
			isInfiniteLoop = constant->mBool;
		ValueScopeEnd(scopeData.mValueScopeStart);
		mBfIRBuilder->CreateCondBr(conditionValue.mValue, bodyBB, endBB);
	}
	else
	{
		isInfiniteLoop = true;
		mBfIRBuilder->CreateBr(bodyBB);
	}

	// Apply deferred local assign to mEmbeddedStatement and itrStmt
	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);
	if (forStmt->mEmbeddedStatement != NULL)
	{
		VisitEmbeddedStatement(forStmt->mEmbeddedStatement);
	}
	if (!mCurMethodState->mLeftBlockUncond)
		mBfIRBuilder->CreateBr(incBB);

	mBfIRBuilder->AddBlock(incBB);
	mBfIRBuilder->SetInsertPoint(incBB);
	for (auto itrStmt : forStmt->mIterators)
	{
		VisitChild(itrStmt);
		if ((mCurMethodState->mLeftBlockUncond) && (!mCurMethodState->mHadContinue) && (!mCurMethodState->mInPostReturn))
			Warn(BfWarning_CS0162_UnreachableCode, "Unreachable code", itrStmt);
	}
	ValueScopeEnd(scopeData.mValueScopeStart);
	mBfIRBuilder->CreateBr(condBB);

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);

	// The 'return' may have been inside the block, which may not have been entered if preconditions were not met
	mCurMethodState->SetHadReturn(false);
	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->mLeftBlockCond = false;

	if (breakData.mHadBreak)
		isInfiniteLoop = false;

	RestoreScopeState();

	if (isInfiniteLoop)
		EmitDefaultReturn();
}

void BfModule::DoForLess(BfForEachStatement* forEachStmt)
{
	UpdateSrcPos(forEachStmt);

	auto startBB = mBfIRBuilder->GetInsertBlock();
	auto condBB = mBfIRBuilder->CreateBlock("forless.cond", true);
	mBfIRBuilder->SetInsertPoint(condBB);

	BfScopeData scopeData;
	// We set mIsLoop later
	if (forEachStmt->mLabelNode != NULL)
		scopeData.mLabelNode = forEachStmt->mLabelNode->mLabel;
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	auto autoComplete = mCompiler->GetAutoComplete();

	auto isLet = BfNodeDynCast<BfLetTypeReference>(forEachStmt->mVariableTypeRef) != 0;
	auto isVar = BfNodeDynCast<BfVarTypeReference>(forEachStmt->mVariableTypeRef) != 0;

	BfTypedValue target;
	BfType* varType = NULL;
	bool didInference = false;
	if (isLet || isVar)
	{
		if (forEachStmt->mCollectionExpression != NULL)
			target = CreateValueFromExpression(forEachStmt->mCollectionExpression);
		if (target)
		{
			FixIntUnknown(target);
			varType = target.mType;
		}

		if (autoComplete != NULL)
			autoComplete->CheckVarResolution(forEachStmt->mVariableTypeRef, varType);
		didInference = true;
	}
	else
	{
		varType = ResolveTypeRef(forEachStmt->mVariableTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);
		if (forEachStmt->mCollectionExpression != NULL)
			target = CreateValueFromExpression(forEachStmt->mCollectionExpression, varType);
	}
	if (varType == NULL)
		varType = GetPrimitiveType(BfTypeCode_IntPtr);

	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.mIsIfCondition = true;
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, true);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	deferredLocalAssignData.mIsIfCondition = false;

	// The "extend chain" is only valid for the conditional -- since that expression may contain unconditionally executed and
	//  conditionally executed code (in the case of "(GetVal(out a) && GetVal(out b))" for example
	mCurMethodState->mDeferredLocalAssignData->BreakExtendChain();

	BfType* checkType = varType;
	if (checkType->IsTypedPrimitive())
		checkType = checkType->GetUnderlyingType();
	if (!checkType->IsIntegral())
	{
		Fail(StrFormat("Cannot iterate over '%s' in for-less statements, only integer types are allowed", TypeToString(varType).c_str()), forEachStmt->mVariableTypeRef);
		varType = GetPrimitiveType(BfTypeCode_IntPtr);
		if (didInference)
			target = GetDefaultTypedValue(varType);
	}
	PopulateType(varType, BfPopulateType_Data);

	auto condEndBB = mBfIRBuilder->GetInsertBlock();
	mBfIRBuilder->SetInsertPoint(startBB);

	BfLocalVariable* localDef = new BfLocalVariable();
	localDef->mNameNode = BfNodeDynCast<BfIdentifierNode>(forEachStmt->mVariableName);
	localDef->mName = localDef->mNameNode->ToString();
	localDef->mResolvedType = varType;
	BfIRValue varInst;
	if (!varType->IsValuelessType())
	{
		varInst = CreateAlloca(varType);
	}
	localDef->mAddr = varInst;
	localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
	localDef->mReadFromId = 0;
	localDef->mIsReadOnly = isLet || (forEachStmt->mReadOnlyToken != NULL);

	CheckVariableDef(localDef);

	mBfIRBuilder->CreateAlignedStore(GetDefaultValue(varType), localDef->mAddr, varType->mAlign);

	localDef->Init();
	UpdateExprSrcPos(forEachStmt->mVariableName);
	AddLocalVariableDef(localDef, true);

	auto bodyBB = mBfIRBuilder->CreateBlock("forless.body");
	auto incBB = mBfIRBuilder->CreateBlock("forless.inc");
	auto endBB = mBfIRBuilder->CreateBlock("forless.end");

	BfBreakData breakData;
	breakData.mIRContinueBlock = incBB;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &scopeData;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;

	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	mBfIRBuilder->CreateBr(condBB);
	mBfIRBuilder->SetInsertPoint(condEndBB);
	if (forEachStmt->mCollectionExpression != NULL)
		UpdateExprSrcPos(forEachStmt->mCollectionExpression);
	BfIRValue conditionValue;

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	// Cond
	auto valueScopeStart = ValueScopeStart();
	auto localVal = mBfIRBuilder->CreateAlignedLoad(localDef->mAddr, localDef->mResolvedType->mAlign);
	if (!target)
	{
		// Soldier on
		target = GetDefaultTypedValue(varType);
	}
	if (forEachStmt->mInToken->mToken == BfToken_LessEquals)
		conditionValue = mBfIRBuilder->CreateCmpLTE(localVal, target.mValue, varType->IsSigned());
	else
		conditionValue = mBfIRBuilder->CreateCmpLT(localVal, target.mValue, varType->IsSigned());
	mBfIRBuilder->CreateCondBr(conditionValue, bodyBB, endBB);
	ValueScopeEnd(valueScopeStart);

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);

	// Body
	scopeData.mIsLoop = true;
	if (forEachStmt->mEmbeddedStatement != NULL)
	{
		VisitEmbeddedStatement(forEachStmt->mEmbeddedStatement);
	}

	// Inc

	if (!mCurMethodState->mLeftBlockUncond)
	{
		mBfIRBuilder->CreateBr(incBB);
	}

	mBfIRBuilder->AddBlock(incBB);
	mBfIRBuilder->SetInsertPoint(incBB);
	if (forEachStmt->mCollectionExpression != NULL)
		UpdateExprSrcPos(forEachStmt->mCollectionExpression);

	auto one = GetConstValue(1, localDef->mResolvedType);
	// We have to reload localVal before the inc, user logic could have changed it
	localVal = mBfIRBuilder->CreateAlignedLoad(localDef->mAddr, localDef->mResolvedType->mAlign);
	auto result = mBfIRBuilder->CreateAdd(localVal, one);
	mBfIRBuilder->CreateAlignedStore(result, localDef->mAddr, localDef->mResolvedType->mAlign);
	ValueScopeEnd(valueScopeStart);

	mBfIRBuilder->CreateBr(condBB);

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);

	// The 'return' may have been inside the block, which may not have been entered if preconditions were not met
	mCurMethodState->SetHadReturn(false);
	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->mLeftBlockCond = false;

	bool definitelyExecuted = false;
	if (target.mValue.IsConst())
	{
		auto constant = mBfIRBuilder->GetConstant(target.mValue);
		if (constant->mInt32 > 0)
			definitelyExecuted = true;
	}

	prevDLA.Restore();
	if (definitelyExecuted)
		mCurMethodState->ApplyDeferredLocalAssignData(deferredLocalAssignData);

	RestoreScopeState();
}

void BfModule::Visit(BfForEachStatement* forEachStmt)
{
	if ((forEachStmt->mInToken != NULL) &&
		((forEachStmt->mInToken->GetToken() == BfToken_LChevron) || (forEachStmt->mInToken->GetToken() == BfToken_LessEquals)))
	{
		DoForLess(forEachStmt);
		return;
	}

	auto autoComplete = mCompiler->GetAutoComplete();
	UpdateSrcPos(forEachStmt);

	BfScopeData scopeData;
	// We set mIsLoop after the non-looped initializations
	scopeData.mValueScopeStart = ValueScopeStart();
	mCurMethodState->AddScope(&scopeData);
	NewScopeState();

	bool isRefExpression = false;
	BfExpression* collectionExpr = forEachStmt->mCollectionExpression;
	if (auto unaryOpExpr = BfNodeDynCast<BfUnaryOperatorExpression>(collectionExpr))
	{
		if ((unaryOpExpr->mOp == BfUnaryOp_Ref) || (unaryOpExpr->mOp == BfUnaryOp_Mut))
		{
			isRefExpression = true;
			collectionExpr = unaryOpExpr->mExpression;
		}
	}

	BfTypedValue target;
	if (collectionExpr != NULL)
		target = CreateValueFromExpression(collectionExpr);
	if (!target)
	{
		// Soldier on
		target = BfTypedValue(GetDefaultValue(mContext->mBfObjectType), mContext->mBfObjectType);
	}

	bool isLet = (forEachStmt->mVariableTypeRef != NULL) && (forEachStmt->mVariableTypeRef->IsA<BfLetTypeReference>());

	BfType* varType = NULL;
	bool inferVarType = false;
	if ((forEachStmt->mVariableTypeRef == NULL) || (forEachStmt->mVariableTypeRef->IsA<BfVarTypeReference>()) || (isLet))
	{
		if (target.mType->IsSizedArray())
		{
			varType = target.mType->GetUnderlyingType();
			if (isRefExpression)
				varType = CreateRefType(varType);
		}
		else if (target.mType->IsArray())
		{
			varType = target.mType->GetUnderlyingType();
			if (isRefExpression)
				varType = CreateRefType(varType);
		}
		else
			inferVarType = true;
	}
	else
	{
		if (autoComplete != NULL)
			autoComplete->CheckTypeRef(forEachStmt->mVariableTypeRef, false);
		varType = ResolveTypeRef(forEachStmt->mVariableTypeRef, BfPopulateType_Data, BfResolveTypeRefFlag_AllowRef);
	}

	if (varType == NULL)
		varType = GetPrimitiveType(BfTypeCode_Var);
	bool isArray = target.mType->IsArray();
	bool isSizedArray = target.mType->IsSizedArray();
	bool isVarEnumerator = target.mType->IsVar();

	// Array
	BfType* itrType;
	BfTypedValue itr;
	BfTypeInstance* itrInterface = NULL;
	BfTypeInstance* refItrInterface = NULL;

	if (isVarEnumerator)
		varType = GetPrimitiveType(BfTypeCode_Var);

	BfGenericParamInstance* genericParamInst = NULL;

	if (target.mType->IsGenericParam())
	{
		genericParamInst = GetGenericParamInstance((BfGenericParamType*)target.mType);
		if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Var) != 0)
		{
			varType = GetPrimitiveType(BfTypeCode_Var);
			isVarEnumerator = true;
		}

		if (genericParamInst->mTypeConstraint != NULL)
		{
			if (genericParamInst->mTypeConstraint->IsVar())
			{
				varType = GetPrimitiveType(BfTypeCode_Var);
				isVarEnumerator = true;
			}

			if (genericParamInst->mTypeConstraint->IsGenericTypeInstance())
			{
				auto genericConstraintType = (BfTypeInstance*)genericParamInst->mTypeConstraint;
				if (genericConstraintType->IsInstanceOf(mCompiler->mSizedArrayTypeDef))
				{
					varType = genericConstraintType->mGenericTypeInfo->mTypeGenericArguments[0];
					isVarEnumerator = true;
				}
			}
		}
	}

	if (target.mType->IsConcreteInterfaceType())
		target.mType = target.mType->GetUnderlyingType();

	if (isArray || isSizedArray)
	{
		itrType = GetPrimitiveType(BfTypeCode_IntPtr);
		BfIRValue itrInst = CreateAlloca(itrType);
		itr = BfTypedValue(itrInst, itrType, true);
	}
	else if (isVarEnumerator)
	{
		// Generic method or mixin decl
	}
	else if ((!target.mType->IsTypeInstance()) && (genericParamInst == NULL))
	{
		Fail(StrFormat("Type '%s' cannot be used in enumeration", TypeToString(target.mType).c_str()), forEachStmt->mCollectionExpression);
	}
	else if (forEachStmt->mCollectionExpression != NULL)
	{
		auto targetTypeInstance = target.mType->ToTypeInstance();

		itr = target;
		bool hadGetEnumeratorType = false;

		if (genericParamInst != NULL)
		{
			for (auto ifaceConstraint : genericParamInst->mInterfaceConstraints)
			{
				if (ifaceConstraint->IsInstanceOf(mCompiler->mGenericIEnumerableTypeDef))
				{
					if (targetTypeInstance != NULL)
					{
						targetTypeInstance = NULL;
						break;
					}
					targetTypeInstance = ifaceConstraint->ToTypeInstance();
				}
			}
		}

		if (targetTypeInstance != NULL)
		{
			PopulateType(targetTypeInstance, BfPopulateType_DataAndMethods);

			auto getEnumeratorMethod = GetMethodByName(targetTypeInstance, "GetEnumerator", 0, true);
			if (!getEnumeratorMethod)
			{
				hadGetEnumeratorType = false;
			}
			else if (getEnumeratorMethod.mMethodInstance->mMethodDef->mIsStatic)
			{
				hadGetEnumeratorType = true;
				Fail(StrFormat("Type '%s' does not contain a non-static 'GetEnumerator' method", TypeToString(targetTypeInstance).c_str()), forEachStmt->mCollectionExpression);
			}
			else
			{
				if (getEnumeratorMethod.mMethodInstance->mMethodDef->mIsConcrete)
				{
					hadGetEnumeratorType = true;
					if (genericParamInst != NULL)
					{
						if ((genericParamInst->mGenericParamFlags & BfGenericParamFlag_Concrete) == 0)
							Fail(StrFormat("Iteration requires a concrete implementation of '%s', consider adding 'concrete' constraint to '%s'", TypeToString(targetTypeInstance).c_str(), genericParamInst->GetName().c_str()), forEachStmt->mCollectionExpression);
					}
					else
						Fail(StrFormat("Iteration requires a concrete implementation of '%s'", TypeToString(targetTypeInstance).c_str()), forEachStmt->mCollectionExpression);
				}

				hadGetEnumeratorType = true;
				BfExprEvaluator exprEvaluator(this);
				SizedArray<BfIRValue, 1> args;
				auto castedTarget = Cast(forEachStmt->mCollectionExpression, target, getEnumeratorMethod.mMethodInstance->GetOwner());
				exprEvaluator.PushThis(forEachStmt->mCollectionExpression, castedTarget, getEnumeratorMethod.mMethodInstance, args);
				itr = exprEvaluator.CreateCall(forEachStmt->mCollectionExpression, getEnumeratorMethod.mMethodInstance, IsSkippingExtraResolveChecks() ? BfIRValue() : getEnumeratorMethod.mFunc, false, args);

				if (itr.mType->IsConcreteInterfaceType())
					itr.mType = itr.mType->GetUnderlyingType();
			}
		}

		if (itr)
		{
			PopulateType(itr.mType, BfPopulateType_DataAndMethods);

			BfTypeInstance* genericItrInterface = NULL;

			auto _CheckInterface = [&](BfTypeInstance* interface)
			{
				if (interface->IsInstanceOf(isRefExpression ? mCompiler->mGenericIRefEnumeratorTypeDef : mCompiler->mGenericIEnumeratorTypeDef))
				{
					if (genericItrInterface != NULL)
					{
						Fail(StrFormat("Type '%s' implements multiple %s<T> interfaces", TypeToString(itr.mType).c_str(), isRefExpression ? "IRefEnumerator" : "IEnumerator"), forEachStmt->mCollectionExpression);
					}

					itrInterface = interface;
					genericItrInterface = itrInterface->ToGenericTypeInstance();
					if (inferVarType)
					{
						varType = genericItrInterface->mGenericTypeInfo->mTypeGenericArguments[0];
						if (isRefExpression)
						{
							if (varType->IsPointer())
								varType = CreateRefType(varType->GetUnderlyingType());
						}
					}
				}
			};

			auto enumeratorTypeInst = itr.mType->ToTypeInstance();
			while (enumeratorTypeInst != NULL)
			{
				PopulateType(enumeratorTypeInst, Beefy::BfPopulateType_Interfaces_All);

				for (auto& interfaceRef : enumeratorTypeInst->mInterfaces)
				{
					BfTypeInstance* interface = interfaceRef.mInterfaceType;
					_CheckInterface(interface);
				}

				if (enumeratorTypeInst->IsInstanceOf(isRefExpression ? mCompiler->mGenericIRefEnumeratorTypeDef : mCompiler->mGenericIEnumeratorTypeDef))
				{
					itrInterface = enumeratorTypeInst;
					genericItrInterface = itrInterface->ToGenericTypeInstance();
					if (inferVarType)
					{
						varType = genericItrInterface->mGenericTypeInfo->mTypeGenericArguments[0];
						if (isRefExpression)
						{
							if (varType->IsPointer())
								varType = CreateRefType(varType);
						}
					}
					break;
				}

				if (itrInterface != NULL)
					break;

				enumeratorTypeInst = enumeratorTypeInst->mBaseType;
			}

			if ((genericItrInterface == NULL) && (genericParamInst != NULL))
			{
				for (auto interface : genericParamInst->mInterfaceConstraints)
					_CheckInterface(interface);
			}

			if (genericItrInterface == NULL)
			{
				if (!hadGetEnumeratorType)
				{
					Fail(StrFormat("Type '%s' must contain a 'GetEnumerator' method or implement an IEnumerator<T> interface", TypeToString(target.mType).c_str()), forEachStmt->mCollectionExpression);
				}
				else
					Fail(StrFormat("Enumerator type '%s' must implement an %s<T> interface", TypeToString(itr.mType).c_str(), isRefExpression ? "IRefEnumerator" : "IEnumerator"), forEachStmt->mCollectionExpression);
				itrInterface = NULL;
				itr = BfTypedValue();
			}
			else
			{
				itrInterface = genericItrInterface;
				if (isRefExpression)
				{
					refItrInterface = itrInterface;
					PopulateType(refItrInterface);
					// Must IRefEnumeratorf<T> must include only IEnumerator<T>
// 					BF_ASSERT(refItrInterface->mInterfaces.size() == 1);
// 					if (refItrInterface->mInterfaces.size() == 1)
// 						itrInterface = refItrInterface->mInterfaces[0].mInterfaceType;
				}
				itr = MakeAddressable(itr);
				itr = RemoveReadOnly(itr);
			}
		}
	}
	else
	{
		AssertErrorState();
	}

	PopulateType(varType, BfPopulateType_Data);

	// Apply deferred local assign to mEmbeddedStatement and itrStmt
	BfDeferredLocalAssignData deferredLocalAssignData(mCurMethodState->mCurScope);
	deferredLocalAssignData.ExtendFrom(mCurMethodState->mDeferredLocalAssignData, false);
	deferredLocalAssignData.mVarIdBarrier = mCurMethodState->GetRootMethodState()->mCurLocalVarId;
	SetAndRestoreValue<BfDeferredLocalAssignData*> prevDLA(mCurMethodState->mDeferredLocalAssignData, &deferredLocalAssignData);

	SetAndRestoreValue<bool> prevIgnoreWrites(mBfIRBuilder->mIgnoreWrites);

	if ((target.mType->IsSizedArray()) && (((BfSizedArrayType*)target.mType)->mElementCount == 0))
	{
		EmitEnsureInstructionAt();
		mBfIRBuilder->mIgnoreWrites = true;
	}

	BfIdentifierNode* nameNode = NULL;
	String variableName;

	struct _TupleBind
	{
		BfIdentifierNode* mNameNode;
		String mName;
		BfType* mType;
		BfLocalVariable* mVariable;
	};

	Array<_TupleBind> tupleBinds;

	if (forEachStmt->mVariableName != NULL)
	{
		if (auto tupleExpr = BfNodeDynCast<BfTupleExpression>(forEachStmt->mVariableName))
		{
			CheckTupleVariableDeclaration(tupleExpr, varType);

			if (varType->IsTuple())
			{
				auto tupleType = (BfTypeInstance*)varType;

				for (int idx = 0; idx < BF_MIN((int)tupleExpr->mValues.size(), (int)tupleType->mFieldInstances.size()); idx++)
				{
					auto nameNode = tupleExpr->mValues[idx];

					_TupleBind tupleBind;
					tupleBind.mNameNode = BfNodeDynCast<BfIdentifierNode>(nameNode);
					if ((tupleBind.mNameNode == NULL) && (nameNode != NULL))
					{
						Fail("Variable name expected", nameNode);
					}
					tupleBind.mName = nameNode->ToString();
					tupleBind.mType = tupleType->mFieldInstances[idx].mResolvedType;
					tupleBind.mVariable = NULL;

					tupleBinds.Add(tupleBind);
					if (idx == 0)
						variableName = tupleBind.mName;
				}
			}
		}
		else
		{
			nameNode = BfNodeDynCast<BfIdentifierNode>(forEachStmt->mVariableName);
			if (nameNode != NULL)
				variableName = nameNode->ToString();
		}
	}

	if (variableName.IsEmpty())
		variableName = "_";

	BfModuleMethodInstance getNextMethodInst;

	BfType* nextEmbeddedType = NULL;
	BfTypedValue nextResult;
	if ((refItrInterface) || (itrInterface))
	{
		if (isRefExpression)
		{
			PopulateType(refItrInterface, BfPopulateType_Full_Force);
			getNextMethodInst = GetMethodByName(refItrInterface, "GetNextRef");
		}
		else
		{
			PopulateType(itrInterface, BfPopulateType_Full_Force);
			getNextMethodInst = GetMethodByName(itrInterface, "GetNext");
		}
		BF_ASSERT(getNextMethodInst);
		nextResult = BfTypedValue(CreateAlloca(getNextMethodInst.mMethodInstance->mReturnType), getNextMethodInst.mMethodInstance->mReturnType, true);

		if (nextResult.mType->IsGenericTypeInstance())
		{
			nextEmbeddedType = ((BfTypeInstance*)nextResult.mType)->mGenericTypeInfo->mTypeGenericArguments[0];
		}
	}
	if (nextEmbeddedType == NULL)
		nextEmbeddedType = GetPrimitiveType(BfTypeCode_Var);

	BfLocalVariable* itrLocalDef = NULL;

	// Iterator local def
	if (itr)
	{
		BfLocalVariable* localDef = new BfLocalVariable();
		itrLocalDef = localDef;
		localDef->mNameNode = nameNode;
		localDef->mName = variableName;
		localDef->mResolvedType = itr.mType;
		localDef->mAddr = itr.mValue;
		localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
		localDef->mReadFromId = 0;
		localDef->Init();
		UpdateSrcPos(forEachStmt);
		CheckVariableDef(localDef);
		AddLocalVariableDef(localDef, true);
	}

	BfIRValue varInst;
	BfTypedValue varTypedVal;
	bool needsValCopy = true;

	// Local variable
	{
		if (!tupleBinds.IsEmpty())
		{
			BF_ASSERT(varType->IsTuple());
			auto tupleType = (BfTypeInstance*)varType;

			// Tuple binds
			needsValCopy = false;
			if (!nextResult)
			{
				varInst = CreateAlloca(varType);
				varTypedVal = BfTypedValue(varInst, varType, true);
			}

			// Local
			for (int idx = 0; idx < (int)tupleBinds.size(); idx++)
			{
				auto& tupleBind = tupleBinds[idx];
				BfLocalVariable* localDef = new BfLocalVariable();
				localDef->mNameNode = tupleBind.mNameNode;
				localDef->mName = tupleBind.mName;
				localDef->mResolvedType = tupleBind.mType;
				if (!needsValCopy)
					localDef->mResolvedType = CreateRefType(localDef->mResolvedType);
				localDef->mAddr = CreateAlloca(localDef->mResolvedType);
				localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
				localDef->mReadFromId = 0;
				if ((isLet) || (forEachStmt->mReadOnlyToken != NULL))
					localDef->mIsReadOnly = true;
				localDef->Init();

				auto fieldInstance = &tupleType->mFieldInstances[idx];
				if (fieldInstance->mDataIdx >= 0)
				{
					auto tuplePtrType = CreatePointerType(varType);
					BfIRValue tuplePtr;
					if (nextResult)
						tuplePtr = mBfIRBuilder->CreateBitCast(nextResult.mValue, mBfIRBuilder->MapType(tuplePtrType));
					else
						tuplePtr = mBfIRBuilder->CreateBitCast(varInst, mBfIRBuilder->MapType(tuplePtrType));
					auto valAddr = mBfIRBuilder->CreateInBoundsGEP(tuplePtr, 0, fieldInstance->mDataIdx);
					mBfIRBuilder->CreateAlignedStore(valAddr, localDef->mAddr, localDef->mResolvedType->mAlign);
				}

				UpdateSrcPos(forEachStmt);
				if ((itrLocalDef != NULL) && (idx == 0))
				{
					localDef->mLocalVarId = itrLocalDef->mLocalVarId;
					localDef->mIsShadow = true;
				}
				else
				{
					CheckVariableDef(localDef);
				}

				AddLocalVariableDef(localDef, true, false, BfIRValue(), BfIRInitType_NotNeeded_AliveOnDecl);
			}
		}
		else
		{
			// Normal case
			if ((nextResult) && (varType->IsComposite()) && (!isRefExpression))
			{
				needsValCopy = false;
				varType = CreateRefType(varType);
			}

			// Local
			BfLocalVariable* localDef = new BfLocalVariable();
			localDef->mNameNode = nameNode;
			localDef->mName = variableName;
			localDef->mResolvedType = varType;
			varInst = CreateAlloca(varType);
			localDef->mAddr = varInst;
			localDef->mAssignedKind = BfLocalVarAssignKind_Unconditional;
			localDef->mReadFromId = 0;
			if ((isLet) || (forEachStmt->mReadOnlyToken != NULL))
				localDef->mIsReadOnly = true;
			localDef->Init();

			if (!needsValCopy)
			{
				auto valAddr = mBfIRBuilder->CreateBitCast(nextResult.mValue, mBfIRBuilder->MapType(varType));
				mBfIRBuilder->CreateAlignedStore(valAddr, varInst, varType->mAlign);
			}

			UpdateSrcPos(forEachStmt);
			if (itrLocalDef != NULL)
			{
				localDef->mLocalVarId = itrLocalDef->mLocalVarId;
				localDef->mIsShadow = true;
			}
			else
			{
				CheckVariableDef(localDef);
			}

			AddLocalVariableDef(localDef, true, false, BfIRValue(), BfIRInitType_NotNeeded_AliveOnDecl);

			varTypedVal = BfTypedValue(varInst, varType, true);
		}
	}

	// Iterator
	if (itr)
	{
		if ((!isArray) && (!isSizedArray))
		{
			BfFunctionBindResult functionBindResult;
			BfExprEvaluator exprEvaluator(this);
			exprEvaluator.mFunctionBindResult = &functionBindResult;

			// Allow for "Dispose" not to exist
			SetAndRestoreValue<bool> prevIgnoreErrors(mIgnoreErrors, true);
			BfResolvedArgs resolvedArgs;
			exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_NoAutoComplete);
			exprEvaluator.MatchMethod(forEachStmt->mCollectionExpression, NULL, itr, false, false, "Dispose", resolvedArgs, BfMethodGenericArguments());
			if (functionBindResult.mMethodInstance != NULL)
			{
				BfModuleMethodInstance moduleMethodInstance;
				moduleMethodInstance = BfModuleMethodInstance(functionBindResult.mMethodInstance, functionBindResult.mFunc);
				AddDeferredCall(moduleMethodInstance, functionBindResult.mIRArgs, mCurMethodState->mCurScope);
			}
		}
	}

	BfScopeData innerScopeData;
	if (forEachStmt->mLabelNode != NULL)
		innerScopeData.mLabelNode = forEachStmt->mLabelNode->mLabel;
	innerScopeData.mValueScopeStart = ValueScopeStart();
	mCurMethodState->AddScope(&innerScopeData);
	NewScopeState(true, false);
	innerScopeData.mIsLoop = true;

	if ((autoComplete != NULL) && (forEachStmt->mVariableTypeRef != NULL))
		autoComplete->CheckVarResolution(forEachStmt->mVariableTypeRef, varType);

	if (isArray || isSizedArray)
		mBfIRBuilder->CreateAlignedStore(GetConstValue(0), itr.mValue, itr.mType->mAlign);

	auto valueScopeStartInner = ValueScopeStart();

	// We may have a call in the loop body
	mCurMethodState->mMayNeedThisAccessCheck = true;

	auto condBB = mBfIRBuilder->CreateBlock("foreach.cond", true);
	auto bodyBB = mBfIRBuilder->CreateBlock("foreach.body");
	auto incBB = mBfIRBuilder->CreateBlock("foreach.inc");
	auto endBB = mBfIRBuilder->CreateBlock("foreach.end");

	BfBreakData breakData;
	breakData.mIRContinueBlock = incBB;
	breakData.mIRBreakBlock = endBB;
	breakData.mScope = &innerScopeData;
	breakData.mInnerValueScopeStart = valueScopeStartInner;
	breakData.mPrevBreakData = mCurMethodState->mBreakData;
	SetAndRestoreValue<BfBreakData*> prevBreakData(mCurMethodState->mBreakData, &breakData);

	mBfIRBuilder->CreateBr(condBB);
	mBfIRBuilder->SetInsertPoint(condBB);
	if (forEachStmt->mCollectionExpression != NULL)
		UpdateExprSrcPos(forEachStmt->mCollectionExpression);
	BfIRValue conditionValue;
	if (isSizedArray) // if (i < lengthof(array)
	{
		auto itrVal = mBfIRBuilder->CreateLoad(itr.mValue);
		auto arrayType = (BfSizedArrayType*)target.mType;
		PopulateType(arrayType, BfPopulateType_DataAndMethods);
		BfIRValue lengthVal = GetConstValue(arrayType->mElementCount);
		conditionValue = mBfIRBuilder->CreateCmpLT(itrVal, lengthVal, true);
		mBfIRBuilder->CreateCondBr(conditionValue, bodyBB, endBB);
		ValueScopeEnd(valueScopeStartInner);
	}
	else if (isArray) // if (i < array.mLength)
	{
		auto itrVal = mBfIRBuilder->CreateAlignedLoad(itr.mValue, itr.mType->mAlign);
		auto arrayType = (BfArrayType*)target.mType;
		PopulateType(arrayType);
		auto arrayBaseValue = mBfIRBuilder->CreateBitCast(target.mValue, mBfIRBuilder->MapType(arrayType->mBaseType, BfIRPopulateType_Full));
		int getLengthBitCount = arrayType->GetLengthBitCount();

		BfIRValue lengthVal;
		if (arrayType->mBaseType->mTypeFailed)
		{
			AssertErrorState();
			if (getLengthBitCount == 64)
				lengthVal = GetConstValue64(0);
			else
				lengthVal = GetConstValue32(0);
		}
		else
		{
			auto fieldInst = GetFieldInstance(arrayType->mBaseType, 0, "mLength");
			if (fieldInst != NULL)
			{
				auto lengthValAddr = mBfIRBuilder->CreateInBoundsGEP(arrayBaseValue, 0, fieldInst->mDataIdx);
				lengthVal = mBfIRBuilder->CreateAlignedLoad(lengthValAddr, fieldInst->mResolvedType->mAlign);
			}
		}
		lengthVal = mBfIRBuilder->CreateNumericCast(lengthVal, true, BfTypeCode_IntPtr);
		conditionValue = mBfIRBuilder->CreateCmpLT(itrVal, lengthVal, true);
		mBfIRBuilder->CreateCondBr(conditionValue, bodyBB, endBB);
		ValueScopeEnd(valueScopeStartInner);
	}
	else // if (itr.MoveNext())
	{
		if (!itr)
		{
			if (!isVarEnumerator)
				AssertErrorState();

			mBfIRBuilder->CreateBr(endBB);
		}
		else
		{
			BfExprEvaluator exprEvaluator(this);
			auto itrTypeInstance = itr.mType->ToTypeInstance();
			SizedArray<BfResolvedArg, 0> resolvedArgs;
			BfMethodMatcher methodMatcher(forEachStmt->mCollectionExpression, this, getNextMethodInst.mMethodInstance, resolvedArgs, BfMethodGenericArguments());
			if (isRefExpression)
				methodMatcher.CheckType(refItrInterface, itr, false);
			else
				methodMatcher.CheckType(itrInterface, itr, false);
			methodMatcher.TryDevirtualizeCall(itr);
			exprEvaluator.mReceivingValue = &nextResult;
			auto retVal = exprEvaluator.CreateCall(&methodMatcher, itr);
			if (exprEvaluator.mReceivingValue != NULL)
			{
				if (mIsComptimeModule)
				{
					retVal = LoadValue(retVal);
					mBfIRBuilder->CreateStore(retVal.mValue, nextResult.mValue);
				}
				else
					AssertErrorState();
			}
			if ((retVal) && (!retVal.mType->IsVar()))
			{
				auto i8Result = ExtractValue(nextResult, NULL, 2);
				i8Result = LoadValue(i8Result);
				BF_ASSERT(i8Result.mType == GetPrimitiveType(BfTypeCode_Int8));
				conditionValue = mBfIRBuilder->CreateCmpEQ(i8Result.mValue, GetConstValue8(0));
			}
			else
				conditionValue = GetDefaultValue(GetPrimitiveType(BfTypeCode_Boolean));
			mBfIRBuilder->CreateCondBr(conditionValue, bodyBB, endBB);
			ValueScopeEnd(valueScopeStartInner);
		}
	}

	mBfIRBuilder->AddBlock(bodyBB);
	mBfIRBuilder->SetInsertPoint(bodyBB);
	if (!varTypedVal)
	{
		// Nothing to do...
	}
	else if (isSizedArray) // val = array[i]
	{
		auto itrVal = mBfIRBuilder->CreateLoad(itr.mValue);
		auto arrayType = (BfSizedArrayType*)target.mType;
		BfType* ptrType = CreatePointerType(arrayType->mElementType);
		BfTypedValue arrayItem;

		if (arrayType->mElementType->IsValuelessType())
		{
			arrayItem = GetDefaultTypedValue(arrayType->mElementType);
		}
		else
		{
			target = MakeAddressable(target);
			arrayItem = BfTypedValue(CreateIndexedValue(arrayType->mElementType, target.mValue, itrVal, true), arrayType->mElementType, true);
			if (isRefExpression)
				arrayItem = BfTypedValue(arrayItem.mValue, CreateRefType(arrayItem.mType));
		}
		arrayItem = Cast(forEachStmt->mCollectionExpression, arrayItem, varType, BfCastFlags_Explicit);
		if ((arrayItem) && (!arrayItem.mValue.IsFake()))
		{
			arrayItem = LoadValue(arrayItem);
			if (arrayItem)
				mBfIRBuilder->CreateStore(arrayItem.mValue, varInst);
		}
	}
	else if (isArray) // val = array[i]
	{
		auto itrVal = mBfIRBuilder->CreateLoad(itr.mValue);
		BfTypedValueExpression typedValueExpr;
		typedValueExpr.Init(BfTypedValue(itrVal, itrType));
		BfExprEvaluator exprEvaluator(this);
		SizedArray<BfExpression*, 1> indices;
		indices.push_back(&typedValueExpr);
		BfSizedArray<BfExpression*> sizedArgExprs(indices);
		BfResolvedArgs argValues(&sizedArgExprs);
		exprEvaluator.ResolveArgValues(argValues);
		bool boundsCheck = mCompiler->mOptions.mRuntimeChecks;
		auto typeOptions = GetTypeOptions();
		if (typeOptions != NULL)
			boundsCheck = typeOptions->Apply(boundsCheck, BfOptionFlags_RuntimeChecks);

		BfMethodMatcher methodMatcher(forEachStmt->mVariableName, this, "get__", argValues.mResolvedArgs, BfMethodGenericArguments());
		methodMatcher.mMethodType = BfMethodType_PropertyGetter;
		methodMatcher.CheckType(target.mType->ToTypeInstance(), target, false);
		if (methodMatcher.mBestMethodDef == NULL)
		{
			Fail("Failed to find indexer method in array", forEachStmt);
		}
		else
		{
			methodMatcher.mCheckedKind = boundsCheck ? BfCheckedKind_Checked : BfCheckedKind_Unchecked;
			BfTypedValue arrayItem = exprEvaluator.CreateCall(&methodMatcher, target);

			if ((varInst) && (arrayItem))
			{
				if (isRefExpression)
					arrayItem = BfTypedValue(arrayItem.mValue, CreateRefType(arrayItem.mType));
				else if (!arrayItem.mType->IsComposite())
					arrayItem = LoadValue(arrayItem);
				arrayItem = Cast(forEachStmt->mCollectionExpression, arrayItem, varType, BfCastFlags_Explicit);
				arrayItem = LoadValue(arrayItem);
				if (arrayItem)
					mBfIRBuilder->CreateStore(arrayItem.mValue, varInst);
			}
		}
	}
	else
	{
		if (!itr)
		{
			if (!isVarEnumerator)
				AssertErrorState();
		}
		else if (mCompiler->IsAutocomplete())
		{
			// If we don't do this shortcut, we can end up creating temporary "boxed" objects
		}
		else
		{
			if (needsValCopy)
			{
				auto nextVal = BfTypedValue(mBfIRBuilder->CreateBitCast(nextResult.mValue, mBfIRBuilder->MapType(CreatePointerType(nextEmbeddedType))), nextEmbeddedType, true);
				if (isRefExpression)
				{
					if (nextVal.mType->IsPointer())
						nextVal = BfTypedValue(nextVal.mValue, CreateRefType(nextVal.mType->GetUnderlyingType()), true);
				}
				nextVal = Cast(forEachStmt->mCollectionExpression, nextVal, varType, BfCastFlags_Explicit);
				nextVal = LoadValue(nextVal);
				if ((nextVal) && (!nextVal.mType->IsValuelessType()))
					mBfIRBuilder->CreateAlignedStore(nextVal.mValue, varInst, nextVal.mType->mAlign);
			}
		}
	}

	if (forEachStmt->mEmbeddedStatement != NULL)
	{
		VisitEmbeddedStatement(forEachStmt->mEmbeddedStatement);
	}

	if (!mCurMethodState->mLeftBlockUncond)
	{
		ValueScopeEnd(valueScopeStartInner);
		mBfIRBuilder->CreateBr(incBB);
	}

	mBfIRBuilder->AddBlock(incBB);
	mBfIRBuilder->SetInsertPoint(incBB);

	if (isArray || isSizedArray)
	{
		auto val = mBfIRBuilder->CreateLoad(itr.mValue);
		auto result = mBfIRBuilder->CreateAdd(val, GetConstValue(1));
		mBfIRBuilder->CreateStore(result, itr.mValue);
	}
	else
	{
		// Nothing to do
	}

	mBfIRBuilder->CreateBr(condBB);

	mBfIRBuilder->AddBlock(endBB);
	mBfIRBuilder->SetInsertPoint(endBB);
	if ((itrLocalDef != NULL) && (itrLocalDef->mDbgVarInst) && (IsTargetingBeefBackend()))
	{
		// If this shadows another enumerator variable then we need to explicitly mark the end of this one
		mBfIRBuilder->DbgLifetimeEnd(itrLocalDef->mDbgVarInst);
	}

	// The 'return' may have been inside the block, which may not have been entered if preconditions were not met
	mCurMethodState->SetHadReturn(false);
	mCurMethodState->mLeftBlockUncond = false;
	mCurMethodState->mLeftBlockCond = false;

	RestoreScopeState();
	RestoreScopeState();
}

void BfModule::Visit(BfDeferStatement* deferStmt)
{
	if (deferStmt->mTargetNode == NULL)
	{
		AssertErrorState();
		return;
	}

	//TODO: Why in the world didn't we want to be able to step onto a defer statement?
	// We only want the breakpoint to hit on execution of the defer, not on insertion of it
	//SetAndRestoreValue<bool> prevSetIllegalSrcPos(mSetIllegalSrcPosition, true);

	UpdateSrcPos(deferStmt);
	EmitEnsureInstructionAt();

	BfScopeData* scope = NULL;

	if (deferStmt->mScopeToken != NULL)
	{
		if (deferStmt->mScopeToken->GetToken() == BfToken_Scope)
			scope = mCurMethodState->mCurScope->GetTargetable();
		else
			scope = &mCurMethodState->mHeadScope;
	}
	else if (deferStmt->mScopeName != NULL)
		scope = FindScope(deferStmt->mScopeName, true);
	else
		scope = mCurMethodState->mCurScope;

	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mAutoComplete != NULL))
	{
		auto targetIdentifier = BfNodeDynCast<BfIdentifierNode>(deferStmt->mScopeName);
		if ((deferStmt->mScopeName == NULL) || (targetIdentifier != NULL))
			mCompiler->mResolvePassData->mAutoComplete->CheckLabel(targetIdentifier, deferStmt->mColonToken, scope);
	}

	if ((scope == mCurMethodState->mCurScope) && (scope->mCloseNode == NULL))
	{
		auto parser = deferStmt->GetParser();
		if ((parser == NULL) || (!parser->mIsEmitted))
			Warn(0, "This defer will immediately execute. Consider specifying a wider scope target such as 'defer::'", deferStmt->mDeferToken);
	}

	if (auto block = BfNodeDynCast<BfBlock>(deferStmt->mTargetNode))
	{
		if (deferStmt->mBind != NULL)
		{
			Array<BfDeferredCapture> captures;
			for (auto identifier : deferStmt->mBind->mParams)
			{
				BfDeferredCapture deferredCapture;
				deferredCapture.mName = identifier->ToString();
				deferredCapture.mValue = CreateValueFromExpression(identifier);
				if (deferredCapture.mValue)
				{
					captures.push_back(deferredCapture);
				}
			}
			AddDeferredBlock(block, scope, &captures);
		}
		else
			AddDeferredBlock(block, scope);
	}
	else if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(deferStmt->mTargetNode))
	{
		BfExprEvaluator expressionEvaluator(this);
		expressionEvaluator.mDeferCallRef = exprStmt->mExpression;
		expressionEvaluator.mDeferScopeAlloc = scope;
		expressionEvaluator.VisitChild(exprStmt->mExpression);
		if (mCurMethodState->mPendingNullConditional != NULL)
			FlushNullConditional(expressionEvaluator.mResult, true);
	}
	else if (auto deleteStmt = BfNodeDynCast<BfDeleteStatement>(deferStmt->mTargetNode))
	{
		if (deleteStmt->mExpression == NULL)
		{
			AssertErrorState();
			return;
		}

		auto val = CreateValueFromExpression(deleteStmt->mExpression);
		if (!val)
			return;

		if (mCompiler->IsAutocomplete())
			return;

		bool isGenericParam = false;
		auto checkType = val.mType;
		if (val.mType->IsGenericParam())
		{
			isGenericParam = true;
			auto genericParamInst = GetGenericParamInstance((BfGenericParamType*)val.mType);
			if (genericParamInst->mGenericParamFlags & BfGenericParamFlag_Delete)
				return;
			if (genericParamInst->mTypeConstraint != NULL)
				checkType = genericParamInst->mTypeConstraint;
		}

		bool isAppendDelete = false;
		BfTypedValue customAllocator;
		if (deleteStmt->mAllocExpr != NULL)
		{
			if (auto expr = BfNodeDynCast<BfExpression>(deleteStmt->mAllocExpr))
				customAllocator = CreateValueFromExpression(expr);
			else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(deleteStmt->mAllocExpr))
			{
				if (tokenNode->mToken == BfToken_Append)
					isAppendDelete = true;
			}
		}

		auto internalType = ResolveTypeDef(mCompiler->mInternalTypeDef);
		PopulateType(checkType);
		if (checkType->IsVar())
			return;

		if ((!checkType->IsObjectOrInterface()) && (!checkType->IsPointer()))
		{
			VisitChild(deferStmt->mTargetNode);
			Fail(StrFormat("Cannot delete a value of type '%s'", TypeToString(val.mType).c_str()), deferStmt->mTargetNode);
			return;
		}

		if (isGenericParam)
			return;

		bool isDtorOnly = false;
		if (customAllocator.mType == GetPrimitiveType(BfTypeCode_NullPtr))
		{
			if (!checkType->IsObjectOrInterface())
				Warn(0, "Type '%' has no destructor, so delete:null has no effect", deleteStmt->mExpression);
		}
		else if (customAllocator)
		{
			BfFunctionBindResult functionBindResult;
			functionBindResult.mWantsArgs = true;
			auto customAllocTypeInst = customAllocator.mType->ToTypeInstance();
			if ((checkType->IsObjectOrInterface()) && (customAllocTypeInst != NULL) && (customAllocTypeInst->mTypeDef->GetMethodByName("FreeObject") != NULL))
			{
				BfTypedValueExpression typedValueExpr;
				typedValueExpr.Init(val);
				typedValueExpr.mRefNode = deleteStmt->mAllocExpr;
				BfExprEvaluator exprEvaluator(this);
				SizedArray<BfExpression*, 2> argExprs;
				argExprs.push_back(&typedValueExpr);
				BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
				BfResolvedArgs argValues(&sizedArgExprs);
				exprEvaluator.ResolveArgValues(argValues);
				exprEvaluator.mNoBind = true;
				exprEvaluator.mFunctionBindResult = &functionBindResult;
				exprEvaluator.MatchMethod(deleteStmt->mAllocExpr, NULL, customAllocator, false, false, "FreeObject", argValues, BfMethodGenericArguments());
			}
			else
			{
				auto voidPtrType = GetPrimitiveType(BfTypeCode_NullPtr);
				auto ptrValue = BfTypedValue(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(voidPtrType)), voidPtrType);
				BfTypedValueExpression typedValueExpr;
				typedValueExpr.Init(ptrValue);
				BfExprEvaluator exprEvaluator(this);
				SizedArray<BfExpression*, 2> argExprs;
				argExprs.push_back(&typedValueExpr);
				BfSizedArray<BfExpression*> sizedArgExprs(argExprs);
				BfResolvedArgs argValues(&sizedArgExprs);
				exprEvaluator.ResolveArgValues(argValues);
				exprEvaluator.mNoBind = true;
				exprEvaluator.mFunctionBindResult = &functionBindResult;
				exprEvaluator.MatchMethod(deleteStmt->mAllocExpr, NULL, customAllocator, false, false, "Free", argValues, BfMethodGenericArguments());
			}

			if (functionBindResult.mMethodInstance != NULL)
			{
				AddDeferredCall(BfModuleMethodInstance(functionBindResult.mMethodInstance, functionBindResult.mFunc), functionBindResult.mIRArgs, scope, deleteStmt, true);
			}
		}

		if (checkType->IsObjectOrInterface())
		{
			auto objectType = mContext->mBfObjectType;
			PopulateType(objectType);
			BfMethodInstance* methodInstance = objectType->mVirtualMethodTable[mCompiler->GetVTableMethodOffset() + 0].mImplementingMethod;
			BF_ASSERT(methodInstance->mMethodDef->mName == "~this");
			SizedArray<BfIRValue, 1> llvmArgs;
			llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->MapType(objectType)));

			if (!customAllocator)
			{
				if ((mCompiler->mOptions.mEnableRealtimeLeakCheck) && (!mIsComptimeModule))
				{
					auto moduleMethodInstance = GetInternalMethod("Dbg_MarkObjectDeleted");
					AddDeferredCall(moduleMethodInstance, llvmArgs, scope, deleteStmt, false, true);
				}
				else
				{
					auto moduleMethodInstance = GetInternalMethod("Free");
					SizedArray<BfIRValue, 1> llvmArgs;
					llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr)));
					AddDeferredCall(moduleMethodInstance, llvmArgs, scope, deleteStmt, false, true);
				}
			}

			auto moduleMethodInstance = GetMethodInstance(objectType, methodInstance->mMethodDef, BfTypeVector());
			AddDeferredCall(moduleMethodInstance, llvmArgs, scope, deleteStmt, false, true);

			if ((mCompiler->mOptions.mObjectHasDebugFlags) && (!mIsComptimeModule))
			{
				auto moduleMethodInstance = GetMethodByName(internalType->ToTypeInstance(), (deleteStmt->mTargetTypeToken != NULL) ? "Dbg_ObjectPreCustomDelete" : "Dbg_ObjectPreDelete");
				AddDeferredCall(moduleMethodInstance, llvmArgs, scope, deleteStmt, false, true);
			}
		}
		else
		{
			if ((!customAllocator) && (!isAppendDelete))
			{
				val = LoadValue(val);
				BfModuleMethodInstance moduleMethodInstance;
				if ((mCompiler->mOptions.mDebugAlloc) && (!mIsComptimeModule))
					moduleMethodInstance = GetMethodByName(internalType->ToTypeInstance(), "Dbg_RawFree");
				else
					moduleMethodInstance = GetMethodByName(internalType->ToTypeInstance(), "Free");
				SizedArray<BfIRValue, 1> llvmArgs;
				llvmArgs.push_back(mBfIRBuilder->CreateBitCast(val.mValue, mBfIRBuilder->GetPrimitiveType(BfTypeCode_NullPtr)));
				AddDeferredCall(moduleMethodInstance, llvmArgs, scope, deleteStmt, false, true);
			}
		}
	}
	else
	{
		AssertErrorState();
		VisitChild(deferStmt->mTargetNode);
	}
}

void BfModule::Visit(BfBlock* block)
{
	VisitEmbeddedStatement(block);
}

void BfModule::Visit(BfUnscopedBlock* block)
{
	VisitEmbeddedStatement(block, NULL, BfEmbeddedStatementFlags_Unscoped);
}

void BfModule::Visit(BfLabeledBlock* labeledBlock)
{
	VisitEmbeddedStatement(labeledBlock);
}

void BfModule::Visit(BfRootNode* rootNode)
{
	VisitMembers(rootNode);
}

void BfModule::Visit(BfInlineAsmStatement* asmStmt)
{
#if 0
	enum RegClobberFlags //CDH TODO add support for mmx/xmm/fpst etc (how are these signified in LLVM? check LangRef inline asm docs clobber list info)
	{
		// please keep eax through edx in alphabetical order (grep $BYTEREGS for why)
		REGCLOBBERF_EAX		= (1 << 0),
		REGCLOBBERF_EBX		= (1 << 1),
		REGCLOBBERF_ECX		= (1 << 2),
		REGCLOBBERF_EDX		= (1 << 3),
		REGCLOBBERF_ESI		= (1 << 4),
		REGCLOBBERF_EDI		= (1 << 5),
		REGCLOBBERF_ESP		= (1 << 6),
		REGCLOBBERF_EBP		= (1 << 7),

		REGCLOBBERF_XMM0	= (1 << 8),
		REGCLOBBERF_XMM1	= (1 << 9),
		REGCLOBBERF_XMM2	= (1 << 10),
		REGCLOBBERF_XMM3	= (1 << 11),
		REGCLOBBERF_XMM4	= (1 << 12),
		REGCLOBBERF_XMM5	= (1 << 13),
		REGCLOBBERF_XMM6	= (1 << 14),
		REGCLOBBERF_XMM7	= (1 << 15),

		REGCLOBBERF_FPST0	= (1 << 16),
		REGCLOBBERF_FPST1	= (1 << 17),
		REGCLOBBERF_FPST2	= (1 << 18),
		REGCLOBBERF_FPST3	= (1 << 19),
		REGCLOBBERF_FPST4	= (1 << 20),
		REGCLOBBERF_FPST5	= (1 << 21),
		REGCLOBBERF_FPST6	= (1 << 22),
		REGCLOBBERF_FPST7	= (1 << 23),

		REGCLOBBERF_MM0		= (1 << 24),
		REGCLOBBERF_MM1		= (1 << 25),
		REGCLOBBERF_MM2		= (1 << 26),
		REGCLOBBERF_MM3		= (1 << 27),
		REGCLOBBERF_MM4		= (1 << 28),
		REGCLOBBERF_MM5		= (1 << 29),
		REGCLOBBERF_MM6		= (1 << 30),
		REGCLOBBERF_MM7		= (1 << 31),
	};

	const char* regClobberNames[] = { "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", nullptr }; // must be in same order as flags
	unsigned long regClobberFlags = 0;

	std::function<bool(const StringImpl&, bool)> matchRegFunc = [this, &regClobberNames, &regClobberFlags](const StringImpl& name, bool isClobber)
	{
		bool found = false;
		int nameLen = name.length();
		if (nameLen == 3)
		{
			if ((name[0] == 's') && (name[1] == 't') && (name[2] >= '0') && (name[2] <= '7'))
			{
				// st# regs (unparenthesized at this point)
				if (isClobber)
					regClobberFlags |= (REGCLOBBERF_FPST0 << (name[2] - '0'));
				found = true;
			}
			else if ((name[0] == 'm') && (name[1] == 'm') && (name[2] >= '0') && (name[2] <= '7'))
			{
				// mm regs
				if (isClobber)
					regClobberFlags |= (REGCLOBBERF_MM0 << (name[2] - '0'));
				found = true;
			}
			else
			{
				// dword regs
				for (int iRegCheck = 0; regClobberNames[iRegCheck] != nullptr; ++iRegCheck)
				{
					if (!strcmp(name.c_str(), regClobberNames[iRegCheck]))
					{
						if (isClobber)
							regClobberFlags |= (1 << iRegCheck);
						found = true;
						break;
					}
				}
			}
		}
		else if (nameLen == 2)
		{
			// word & byte regs
			for (int iRegCheck = 0; regClobberNames[iRegCheck] != nullptr; ++iRegCheck)
			{
				if (!strcmp(name.c_str(), regClobberNames[iRegCheck] + 1)) // skip leading 'e'
				{
					if (isClobber)
						regClobberFlags |= (1 << iRegCheck);
					found = true;
					break;
				}
			}
			if (!found)
			{
				// check for byte regs for eax through edx (e.g. al, ah, bl, bh....)
				if ((nameLen == 2) && (name[0] >= 'a') && (name[0] <= 'd') && ((name[1] == 'l') || (name[1] == 'h')))
				{
					if (isClobber)
						regClobberFlags |= (1 << (name[0] - 'a'));// $BYTEREGS this is why we want alphabetical order
					found = true;
				}
			}
		}
		else if ((nameLen == 4) && (name[0] == 'x') && (name[1] == 'm') && (name[2] == 'm') && (name[3] >= '0') && (name[3] <= '7'))
		{
			// xmm regs
			if (isClobber)
				regClobberFlags |= (REGCLOBBERF_XMM0 << (name[3] - '0'));
			found = true;
		}
		return found;
	};

	int asmInstCount = (int)asmStmt->mInstructions.size();
	typedef std::map<String, int> StrToVarIndexMap;
	StrToVarIndexMap strToVarIndexMap;

	if (mCompiler->IsAutocomplete())
	{
		// auto-complete "fast-pass" just to eliminate unused/unassigned variable yellow warning flashes

		for (int iInst=0; iInst<asmInstCount; ++iInst)
		{
			auto instNode = asmStmt->mInstructions[iInst];
			BfInlineAsmInstruction::AsmInst& asmInst = instNode->mAsmInst;

			bool hasLabel = !asmInst.mLabel.empty();
			bool hasOpCode = !asmInst.mOpCode.empty();

			if (hasLabel || hasOpCode) // check against blank lines
			{
				if (hasOpCode) // check against label-only lines (which still get written out, but don't do any other processing)
				{
					int argCount = (int)asmInst.mArgs.size();

					for (int iArg=0; iArg<argCount; ++iArg)
					{
						BfInlineAsmInstruction::AsmArg* arg = &asmInst.mArgs[iArg];
						if (arg->mType == BfInlineAsmInstruction::AsmArg::ARGTYPE_IntReg)
						{
							bool found = matchRegFunc(arg->mReg, false);

							if (!found)
							{
								StrToVarIndexMap::iterator it = strToVarIndexMap.find(arg->mReg);
								if (it == strToVarIndexMap.end())
								{
									for (int i = 0; i < (int) mCurMethodState->mLocals.size(); i++)
									{
										auto& checkLocal = mCurMethodState->mLocals[i];
										if (checkLocal.mName == arg->mReg)
										{
											// if you access a variable in asm, we suppress any warnings related to used or assigned, regardless of usage
											checkLocal.mIsReadFrom = true;
											checkLocal.mAssignedKind = BfLocalVarAssignKind_Unconditional;

											found = true;
											break;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		return;
	}

	int debugLocOffset = 0;
	if (!asmStmt->mInstructions.empty())
		debugLocOffset = asmStmt->mInstructions.front()->GetSrcStart() - asmStmt->GetSrcStart();
	UpdateSrcPos(asmStmt, true, debugLocOffset);

	mCurMethodState->mInHeadScope = false;
	BfScopeData prevScope = mCurMethodState->mCurScope;
	mCurMethodState->mCurScope->mPrevScope = &prevScope;
	NewScopeState();

	bool failed = false;

	if (!mCpu)
		mCpu = new Beefy::X86Cpu();

	//const char* srcAsmText = "nop\nnop\n\n\nmov eax, i\ninc eax\nmov i, eax\n_emit 0x0F\n_emit 0xC7\n_emit 0xF0\n\n\nnop\nnop"; //CDH TODO extract from actual lexical text block
	//const char* srcAsmText = "nop\nnop\n\n\nmov eax, i\ninc eax\nmov i, eax\nrdrand eax\n\n\nnop\nnop"; //CDH TODO extract from actual lexical text block
	//String srcAsmTextStr(&asmStmt->mParser->mSrc[asmStmt->mSrcStart], asmStmt->mSrcEnd - asmStmt->mSrcStart);
	//const char* srcAsmText = srcAsmTextStr.c_str();

	String dstAsmText;
	String constraintStr, clobberStr;
	int constraintCount = 0;
	bool hasMemoryClobber = false;
	Array<Type*> paramTypes;
	SizedArray<Value*, 1> llvmArgs;

	int lastDebugLine = -1;
	int curDebugLineDeltaValue = 0, curDebugLineDeltaRunCount = 0;
	String debugLineSequenceStr;
	int isFirstDebugLine = 1;

	auto maybeEmitDebugLineRun = [&curDebugLineDeltaValue, &curDebugLineDeltaRunCount, &debugLineSequenceStr, &isFirstDebugLine]()
	{
		if (curDebugLineDeltaRunCount > 0)
		{
			for (int i=isFirstDebugLine; i<2; ++i)
			{
				int value = i ? curDebugLineDeltaValue : curDebugLineDeltaRunCount;
				String encodedValue;
				EncodeULEB32(value, encodedValue);
				if (encodedValue.length() > 1)
					debugLineSequenceStr += String("$") + encodedValue + "$";
				else
					debugLineSequenceStr += encodedValue;
			}

			curDebugLineDeltaRunCount = 0;
			isFirstDebugLine = 0;
		}
	};
	auto mangledLabelName = [&asmStmt](const StringImpl& labelName, int numericLabel) -> String
	{
		return StrFormat("%d", numericLabel);
		//return String(".") + labelName;
		//return StrFormat("%s_%p_%d", labelName.c_str(), asmStmt->mSource, asmStmt->mSrcStart); // suffix label name with location information to make it block-specific (since labels get external linkage)
	};

	typedef std::pair<String, int> LabelPair;
	std::unordered_map<String, LabelPair> labelNames;

	// pre-scan instructions for label names
	for (int iInst=0; iInst<asmInstCount; ++iInst)
	{
		auto instNode = asmStmt->mInstructions[iInst];
		BfInlineAsmInstruction::AsmInst& asmInst = instNode->mAsmInst;
		if (!asmInst.mLabel.empty())
		{
			if (labelNames.find(asmInst.mLabel) != labelNames.end())
			{
				Fail(StrFormat("Label \"%s\" already defined in asm block", asmInst.mLabel.c_str()), instNode, true);
				failed = true;
			}
			else
			{
				String mangledLabel(mangledLabelName(asmInst.mLabel, instNode->GetSrcStart()));
				labelNames[asmInst.mLabel] = LabelPair(mangledLabel, instNode->GetSrcStart());
				asmInst.mLabel = mangledLabel;
			}
		}
	}

	for (int iInst=0; iInst<asmInstCount; ++iInst)
	{
		auto instNode = asmStmt->mInstructions[iInst];
		BfInlineAsmInstruction::AsmInst& asmInst = instNode->mAsmInst;

		bool hasLabel = !asmInst.mLabel.empty();
		bool hasOpCode = !asmInst.mOpCode.empty();

		if (hasLabel || hasOpCode) // check against blank lines
		{
			if (hasOpCode) // check against label-only lines (which still get written out, but don't do any other processing)
			{
				int argCount = (int)asmInst.mArgs.size();

				// reasonable defaults for clobber info
				int clobberCount = 1; // destination is usually first arg in Intel syntax
				bool mayClobberMem = true; // we only care about this when it gets turned to false by GetClobbersForMnemonic (no operand form of the instruction clobbers mem)

										   // pseudo-ops
				if (asmInst.mOpCode == "_emit")
				{
					asmInst.mOpCode = ".byte";
				}
				else
				{
					Array<int> opcodes;
					if (!mCpu->GetOpcodesForMnemonic(asmInst.mOpCode, opcodes))
					{
						Fail(StrFormat("Unrecognized instruction mnemonic \"%s\"", asmInst.mOpCode.c_str()), instNode, true);
						failed = true;
					}
					else
					{
						Array<int> implicitClobbers;
						mCpu->GetClobbersForMnemonic(asmInst.mOpCode, argCount, implicitClobbers, clobberCount, mayClobberMem);
						for (int iClobberReg : implicitClobbers)
						{
							String regName = CPURegisters::GetRegisterName(iClobberReg);
							std::transform(regName.begin(), regName.end(), regName.begin(), ::tolower);

							matchRegFunc(regName, true);
						}
					}
				}

				String fakeLabel; // used when running label-using instructions through LLVM semantic pre-check
				for (int iArg=0; iArg<argCount; ++iArg)
				{
					BfInlineAsmInstruction::AsmArg* arg = &asmInst.mArgs[iArg];
					if (arg->mType == BfInlineAsmInstruction::AsmArg::ARGTYPE_IntReg)
					{
						bool isClobber = (iArg < clobberCount);
						bool found = matchRegFunc(arg->mReg, isClobber);

						if (!found)
						{
							StrToVarIndexMap::iterator it = strToVarIndexMap.find(arg->mReg);
							if (it != strToVarIndexMap.end())
							{
								arg->mReg = StrFormat("$%d", it->second);

								if (isClobber)
									mayClobberMem = true;

								found = true;
							}
							else
							{
								for (int i = 0; i < (int) mCurMethodState->mLocals.size(); i++)
								{
									auto& checkLocal = mCurMethodState->mLocals[i];
									if (checkLocal.mName == arg->mReg)
									{
										BfIRValue testValue = checkLocal.mAddr;

										llvmArgs.push_back(testValue);
										paramTypes.push_back(testValue->getType());

										arg->mReg = StrFormat("$%d", constraintCount);//CDH TODO does this need size qualifiers for "dword ptr $0" or whatever?
										strToVarIndexMap[checkLocal.mName] = constraintCount;

										constraintStr += "=*m,";
										++constraintCount;
										if (isClobber)
											mayClobberMem = true;

										// if you access a variable in asm, we suppress any warnings related to used or assigned, regardless of usage
										checkLocal.mIsReadFrom = true;
										checkLocal.mAssignedKind = BfLocalVarAssignKind_Unconditional;

										found = true;
										break;
									}
								}
							}
						}
						if (!found)
						{
							auto labelIt = labelNames.find(arg->mReg);
							if (labelIt != labelNames.end())
							{
								arg->mReg = labelIt->second.first;
								if (labelIt->second.second <= instNode->GetSrcStart())
								{
									fakeLabel = arg->mReg;
									arg->mReg += "b";
								}
								else
									arg->mReg += "f";
							}
							else
							{
								Fail(StrFormat("Unrecognized variable \"%s\"", arg->mReg.c_str()), instNode, true);
								failed = true;
							}
						}
					}
					else if (arg->mType == BfInlineAsmInstruction::AsmArg::ARGTYPE_FloatReg)
					{
						//CDH individual reg clobber is probably insufficient for fp regs since it's stack-based; without deeper knowledge of how individual instructions
						// manipulate the FP stack, the safest approach is to clobber all FP regs as soon as one of them is involved
						//bool isClobber = (iArg == 0); // destination is first arg in Intel syntax
						//bool found = matchRegFunc(StrFormat("st%d", arg->mInt), isClobber);
						//BF_ASSERT(found);
						for (int iRegCheck=0; iRegCheck<8; ++iRegCheck)
							regClobberFlags |= (REGCLOBBERF_FPST0 << iRegCheck);
					}
					else if (arg->mType == BfInlineAsmInstruction::AsmArg::ARGTYPE_Memory)
					{
						bool isClobber = (iArg < clobberCount);

						// check regs for clobber flags
						/*
						//CDH TODO do we need to set clobber flags for regs that are used *indirectly* like this?  Actually I don't think so; commenting out for now
						if (!arg->mReg.empty())
						matchRegFunc(arg->mReg, isClobber);
						if (!arg->mAdjReg.empty())
						matchRegFunc(arg->mAdjReg, isClobber);
						*/

						if (isClobber)
							mayClobberMem = true;

						if (!arg->mMemberSuffix.empty())
						{
							//CDH TODO add member support once I know the right way to look up struct member offsets.  Once we know the offset,
							//  add it to arg->mInt, and set ARGMEMF_ImmediateDisp if it's not set already (member support is just used as an offset)
							Fail("Member suffix syntax is not yet supported", instNode, true);
							failed = true;
						}
					}
				}

				if (mayClobberMem)
					hasMemoryClobber = true;

				//debugLineSequenceStr += StrFormat("%d_", asmInst.mDebugLine);
				int curDebugLine = asmInst.mDebugLine;
				int debugLineDelta = (lastDebugLine > 0) ? curDebugLine - lastDebugLine : curDebugLine;
				lastDebugLine = curDebugLine;

				//String encodedDebugLineDelta;
				//EncodeULEB32(debugLineDelta, encodedDebugLineDelta);
				//debugLineSequenceStr += encodedDebugLineDelta + "_";
				if (curDebugLineDeltaValue != debugLineDelta)
				{
					maybeEmitDebugLineRun();
					curDebugLineDeltaValue = debugLineDelta;
				}
				++curDebugLineDeltaRunCount;

				// run instruction through LLVM for better semantic errors (can be slow in debug; feel free to comment out this scopeData if it's intolerable; the errors will still be caught at compile time)
				//if (false)
				{
					BfInlineAsmInstruction::AsmInst tempAsmInst(asmInst);
					tempAsmInst.mLabel = fakeLabel;
					for (auto & arg : tempAsmInst.mArgs)
					{
						if ((arg.mType == BfInlineAsmInstruction::AsmArg::ARGTYPE_IntReg) && !arg.mReg.empty() && (arg.mReg[0] == '$'))
						{
							// if we've rewritten a local variable instruction arg to use a $-prefixed input, we can't pass that to LLVM
							//  at this stage as it won't recognize it; the actual compilation would have changed all these to use actual
							//  memory operand syntax first.  However, those changes all work down in LLVM to printIntelMemReference inside
							//  of X86AsmPrinter.cpp, and that always results in a [bracketed] memory access string no matter what, which
							//  means for our semantic checking purposes here it's sufficient to just use "[eax]" for all such cases,
							//  rather than go through an even more expensive setup & teardown process to use the AsmPrinter itself.
							arg.mType = BfInlineAsmInstruction::AsmArg::ARGTYPE_Memory;
							arg.mMemFlags = BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg;
							arg.mReg = "eax";
						}
					}

					String llvmError;
					if (!mCpu->ParseInlineAsmInstructionLLVM(tempAsmInst.ToString(), llvmError))
					{
						Fail(StrFormat("Inline asm error: %s", llvmError.c_str()), instNode, true);
						failed = true;
					}
				}
			}

			dstAsmText += asmInst.ToString();
			dstAsmText += "\n";
		}
	}

	maybeEmitDebugLineRun(); // leftovers

	if (failed)
	{
		RestoreScopeState(&prevScope);
		return;
	}

	// prepare constraints/clobbers
	{
		for (int iRegCheck = 0; regClobberNames[iRegCheck] != nullptr; ++iRegCheck)
		{
			if (regClobberFlags & (1 << iRegCheck))
				clobberStr += StrFormat("~{%s},", regClobberNames[iRegCheck]);
		}
		for (int iRegCheck=0; iRegCheck<8; ++iRegCheck)
		{
			if (regClobberFlags & (REGCLOBBERF_XMM0 << iRegCheck))
				clobberStr += StrFormat("~{xmm%d},", iRegCheck);
			if (regClobberFlags & (REGCLOBBERF_FPST0 << iRegCheck))
				clobberStr += StrFormat("~{fp%d},~{st(%d)},", iRegCheck, iRegCheck); // both fp# and st(#) are listed in X86RegisterInfo.td
			if (regClobberFlags & (REGCLOBBERF_MM0 << iRegCheck))
				clobberStr += StrFormat("~{mm%d},", iRegCheck);
		}

		// add wrapping instructions to preserve certain regs (e.g. ESI), due to LLVM bypassing register allocator when choosing a base register
		//CDH TODO currently I'm only shielding against ESI stompage; people generally know not to mess with ESP & EBP, but ESI is still a "general" reg and should be allowed to be clobbered
		//if (regClobberFlags & REGCLOBBERF_ESI)
		//{
		//CDH TODO Bah! This doesn't actually work, because if you do any local variable access after mutating ESI, the variable substitution could have generated a base address dependency
		//  on ESI which will not expect it to have changed, e.g. "mov esi, var\ninc esi\nmov var, esi\n" dies if "var" gets internally rewritten to "[esi + displacement]".  What to do?  Hmm.
		//dstAsmText = String("push esi\n") + dstAsmText + String("pop esi\n");
		//}

		if (hasMemoryClobber)
			clobberStr += "~{memory},";
		clobberStr += "~{dirflag},~{fpsr},~{flags}";

		constraintStr += clobberStr;
	}

	bool wantsDIData = (mBfIRBuilder->DbgHasInfo()) && (!mCurMethodInstance->mIsUnspecialized) && (mHasFullDebugInfo);
	if (wantsDIData)
	{
		static int sVarNum = 0;
		String varName(StrFormat("__asmLines_%d.%s", ++sVarNum, debugLineSequenceStr.c_str()));

		auto varType = GetPrimitiveType(BfTypeCode_Int32);

		auto allocaInst = mBfIRBuilder->CreateAlloca(varType->mLLVMType, 0, varName + ".addr");
		allocaInst->setAlignment(varType->mAlign);
		//paramVar->mAddr = allocaInst;

		auto varValue = GetConstValue(0, varType);

		auto diVariable = mDIBuilder->createAutoVariable(mCurMethodState->mCurScope->mDIScope,
			varName.c_str(), mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, varType->mDIType/*, true*/);

		//auto varValue = llvm::ConstantInt::getTrue(*mLLVMContext);
		//auto varValue = GetDefaultValue(varType);
		//auto varValue = CreateGlobalConstValue(varName, llvm::ConstantInt::getTrue(*mLLVMContext), true);
		//auto varValue = AllocGlobalVariable(*mIRModule, diType->getType(), false, GlobalValue::ExternalLinkage, llvm::ConstantInt::getTrue(*mLLVMContext), varName.c_str());

		//auto diVariable = mDIBuilder->createGlobalVariable(mCurMethodState->mCurScope->mDIScope, varName.c_str(), "", mCurFilePosition.mFileInstance->mDIFile, mCurFilePosition.mCurLine, diType, false, varValue);

		//BasicBlock* block = mBfIRBuilder->GetInsertBlock();
		//auto declareVar = mDIBuilder->insertDeclare(varValue, diVariable, mBfIRBuilder->GetInsertBlock());
		auto declareVar = mDIBuilder->insertDeclare(allocaInst, diVariable, mDIBuilder->createExpression(),
			mIRBuilder->getCurrentDebugLocation(), mBfIRBuilder->GetInsertBlock());
		//auto declareVar = mDIBuilder->insertDbgValueIntrinsic(varValue, 0, diVariable, mBfIRBuilder->GetInsertBlock());
		declareVar->setDebugLoc(mIRBuilder->getCurrentDebugLocation());
	}

	/*
	BfIRValue testValue = NULL;
	for (int i = 0; i < (int) mCurMethodState->mLocals.size(); i++)
	{
	auto& checkLocal = mCurMethodState->mLocals[i];
	if (checkLocal.mName == "i")
	{
	testValue = checkLocal.mAddr;
	break;
	}
	}
	*/
	//BF_ASSERT((testValue != NULL) && "Need local variable \"i\"");
	//if (testValue != NULL)
	{
		//Type* voidPtrType = Type::getInt8PtrTy(*mLLVMContext);
		//if (mContext->mAsmObjectCheckFuncType == NULL)
		//{
		//Array<Type*> paramTypes;
		//paramTypes.push_back(voidPtrType);
		//mContext->mAsmObjectCheckFuncType = FunctionType::get(Type::getVoidTy(*mLLVMContext), paramTypes, false);
		//}
		FunctionType* funcType = FunctionType::get(Type::getVoidTy(*mLLVMContext), paramTypes, false);

		//SizedArray<Value*, 1> llvmArgs;
		//llvmArgs.push_back(testValue);

		//CDH REMOVE NOTE
		//generates IR (e.g.):
		//  call void asm sideeffect "#4\0Anop\0Anop\0Amovl %eax, %eax\0Anop\0Anop", "~{cc},~{dirflag},~{fpsr},~{flags},~{eax}"() #0, !dbg !492

		static int asmIdx = 0;
		asmIdx++;
		String asmStr = StrFormat("#%d\n", asmIdx) +
			//"nop\nnop\nmovl $0, %eax\nincl %eax\nmovl %eax, $0\nmovl $$0, %ecx\nmovl $$0, %esp\nnop\nnop";
			//"nop\nnop\nmovl ($0), %eax\nincl %eax\nmovl %eax, ($0)\nmovl $$0, %ecx\nmovl $$0, %esp\nnop\nnop";
			//"nop\nnop\nmovl %eax, %eax\nmovl $$0, %ecx\nmovl $$0, %esp\nnop\nnop";
			//"nop\nnop\nmovl %eax, %eax\nnop\nnop";
			//"nop\nnop\n.byte 0x0F\n.byte 0xC7\n.byte 0xF0\nmov eax, 7\nmov ecx, 0\ncpuid\nmov eax, dword ptr $0\ninc eax\nmov dword ptr $0, eax\nmov ecx, 0\nmov esp, 0\nnop\nnop"; // rdrand test
			dstAsmText;

		llvm::InlineAsm* inlineAsm = llvm::InlineAsm::get(funcType,
			//asmStr.c_str(), "~{r},~{cc},~{dirflag},~{fpsr},~{flags},~{eax},~{memory},~{esi},~{esp}", true,
			//asmStr.c_str(), "~{cc},~{dirflag},~{fpsr},~{flags},~{memory},~{ecx},~{esp}", true,
			//DOES NOT WORK (mem not written back to from reg:
			//asmStr.c_str(), "+r,~{cc},~{dirflag},~{fpsr},~{flags},~{memory},~{eax},~{ecx},~{esp}", true,
			//asmStr.c_str(), "+rm,~{cc},~{dirflag},~{fpsr},~{flags},~{memory},~{eax},~{ecx},~{esp}", true,
			asmStr.c_str(), constraintStr.c_str(), true,
			false, /*llvm::InlineAsm::AD_ATT*/llvm::InlineAsm::AD_Intel);

		llvm::CallInst* callInst = mIRBuilder->CreateCall(inlineAsm, llvmArgs);
		//llvm::CallInst* callInst = mIRBuilder->CreateCall(inlineAsm);

		callInst->addAttribute(llvm::AttributeSet::FunctionIndex, llvm::Attribute::NoUnwind);
	}

	RestoreScopeState(&prevScope);

#endif
}