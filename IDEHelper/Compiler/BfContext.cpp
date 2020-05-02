#pragma warning(push)
#pragma warning(disable:4800)
#pragma warning(disable:4244)
#pragma warning(disable:4141)
#pragma warning(disable:4624)
#pragma warning(disable:4146)
#pragma warning(disable:4267)
#pragma warning(disable:4291)

#include "BfCompiler.h"
#include "BfSystem.h"
#include "BfParser.h"
#include "BfExprEvaluator.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/FileSystem.h"
//#include "llvm/Support/Dwarf.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <fcntl.h>
#include "BfConstResolver.h"
#include "BfMangler.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BfSourceClassifier.h"
#include "BfAutoComplete.h"
#include "BfResolvePass.h"

#pragma warning(pop)

USING_NS_BF;
using namespace llvm;

BfContext::BfContext(BfCompiler* compiler) :
	mTypeDefTypeRefPool(true),
	mNamedTypeRefPool(true)
{
	mCompiler = compiler;
	mSystem = compiler->mSystem;	
	mBfTypeType = NULL;	
	mBfClassVDataPtrType = NULL;	
	mBfObjectType = NULL;				
	mMappedObjectRevision = 0;
	mDeleting = false;
	mLockModules = false;

	mCurTypeState = NULL;
	mCurConstraintState = NULL;
	mResolvingVarField = false;
	
	for (int i = 0; i < BfTypeCode_Length; i++)
	{
		mPrimitiveTypes[i] = NULL;
		mPrimitiveStructTypes[i] = NULL;
	}

	mScratchModule = new BfModule(this, "");
	mScratchModule->mIsSpecialModule = true;
	mScratchModule->mIsScratchModule = true;
	mScratchModule->mIsReified = true;
	mScratchModule->Init();	

	mUnreifiedModule = new BfModule(this, "");
	mUnreifiedModule->mIsSpecialModule = true;	
	mUnreifiedModule->mIsScratchModule = true;
	mUnreifiedModule->mIsReified = false;
	mUnreifiedModule->Init();	

	mValueTypeDeinitSentinel = (BfMethodInstance*)1;

	mCurStringObjectPoolId = 0;		
}

void BfReportMemory();

BfContext::~BfContext()
{		
	BfLogSysM("Deleting Context...\n");

	mDeleting = true;

	for (auto& kv : mSavedTypeDataMap)
		delete kv.mValue;

	for (auto localMethod : mLocalMethodGraveyard)
		delete localMethod;

	int numTypesDeleted = 0;
	for (auto type : mResolvedTypes)
	{		
		//_CrtCheckMemory();
		delete type;
	}		
	
	delete mScratchModule;
	delete mUnreifiedModule;
	for (auto module : mModules)
		delete module;
		
	BfReportMemory();
}

void BfContext::ReportMemory(MemReporter* memReporter)
{	
	memReporter->Add(sizeof(BfContext));
}

void BfContext::ProcessMethod(BfMethodInstance* methodInstance)
{
	// When we are doing as resolveOnly pass over unused methods in the compiler,
	//  we use the scratch module to ensure mIsResolveOnly flag is set when we
	//  process the method
	auto defModule = methodInstance->mDeclModule;
	if ((!methodInstance->mIsReified) && (!mCompiler->mIsResolveOnly))
		defModule = mUnreifiedModule;

	auto typeInst = methodInstance->GetOwner();
	defModule->ProcessMethod(methodInstance);	
	mCompiler->mStats.mMethodsProcessed++;
	if (!methodInstance->mIsReified)
		mCompiler->mStats.mUnreifiedMethodsProcessed++;
	mCompiler->UpdateCompletion();
}

int BfContext::GetStringLiteralId(const StringImpl& str)
{
	// Note: We do need string pooling in resolve, for intrinsic names and such		
	int* idPtr = NULL;

	if (mStringObjectPool.TryGetValue(str, &idPtr))
		return *idPtr;

	mCurStringObjectPoolId++;
	mStringObjectPool[str] = mCurStringObjectPoolId;

	BfStringPoolEntry stringPoolEntry;
	stringPoolEntry.mString = str;
	stringPoolEntry.mFirstUsedRevision = mCompiler->mRevision;
	stringPoolEntry.mLastUsedRevision = mCompiler->mRevision;
	mStringObjectIdMap[mCurStringObjectPoolId] = stringPoolEntry;
	return mCurStringObjectPoolId;
}

void BfContext::AssignModule(BfType* type)
{
	auto typeInst = type->ToTypeInstance();
	if (typeInst->mModule != NULL)
	{
		BF_ASSERT(!typeInst->mModule->mIsReified);
	}

	BfModule* module = NULL;
	bool needsModuleInit = false;

	// We used to have this "IsReified" check, but we DO want to create modules for unreified types even if they remain unused.
	//  What was that IsReified check catching?
	//  It screwed up the reification of generic types- they just got switched to mScratchModule from mUnreifiedModule, but didn't ever generate code.
	if (/*(!type->IsReified()) ||*/ (type->IsUnspecializedType()) || (type->IsInterface()) || (type->IsVar()) || (type->IsTypeAlias()) || (type->IsFunction()))
	{
		if (typeInst->mIsReified)
			module = mScratchModule;
		else
			module = mUnreifiedModule;
		typeInst->mModule = module;
		BfTypeProcessRequest* typeProcessEntry = mPopulateTypeWorkList.Alloc();
		typeProcessEntry->mType = type;
		BF_ASSERT(typeProcessEntry->mType->mContext == this);
		BfLogSysM("HandleTypeWorkItem: %p -> %p\n", type, typeProcessEntry->mType);				
		mCompiler->mStats.mTypesQueued++;
		mCompiler->UpdateCompletion();		
	}
	else
	{
		auto typeInst = type->ToTypeInstance();
		BF_ASSERT(typeInst != NULL);
		
		auto project = typeInst->mTypeDef->mProject;
		if ((project->mSingleModule) && (typeInst->mIsReified))
		{
			BfModule** modulePtr = NULL;
			if (mProjectModule.TryAdd(project, NULL, &modulePtr))
			{
				String moduleName = project->mName;
				module = new BfModule(this, moduleName);
				module->mIsReified = true;
				module->mProject = project;
				typeInst->mModule = module;
				BF_ASSERT(!mLockModules);
				mModules.push_back(module);
				*modulePtr = module;
				needsModuleInit = true;
			}
			else
			{				
				module = *modulePtr;
				typeInst->mModule = module;
			}
		}
		else
		{
			String moduleName = GenerateModuleName(typeInst);
			module = new BfModule(this, moduleName);
			module->mIsReified = typeInst->mIsReified;
			module->mProject = project;
			typeInst->mModule = module;
			BF_ASSERT(!mLockModules);
			mModules.push_back(module);
			needsModuleInit = true;
		}
	}

	auto localTypeInst = type->ToTypeInstance();
	BF_ASSERT((localTypeInst != NULL) || (mCompiler->mPassInstance->HasFailed()));

	if ((localTypeInst != NULL) && (!module->mIsScratchModule))
	{
		BF_ASSERT(localTypeInst->mContext == this);
		module->mOwnedTypeInstances.push_back(localTypeInst);
	}

	if (needsModuleInit)
		module->Init();
}

void BfContext::HandleTypeWorkItem(BfType* type)
{	
	AssignModule(type);
}

void BfContext::EnsureHotMangledVirtualMethodName(BfMethodInstance* methodInstance)
{
	BP_ZONE("BfContext::EnsureHotMangledVirtualMethodName");
	if ((methodInstance != NULL) && (methodInstance->GetMethodInfoEx()->mMangledName.IsEmpty()))
		BfMangler::Mangle(methodInstance->GetMethodInfoEx()->mMangledName, mCompiler->GetMangleKind(), methodInstance);
}

void BfContext::EnsureHotMangledVirtualMethodNames()
{
	BP_ZONE("BfContext::EnsureHotMangledVirtualMethodNames");

	for (auto type : mResolvedTypes)
	{		
		auto typeInst = type->ToTypeInstance();
		if (typeInst == NULL)
			continue;

		for (auto& methodEntry : typeInst->mVirtualMethodTable)
		{
			BfMethodInstance* methodInstance = methodEntry.mImplementingMethod;
			if ((methodInstance != NULL) && (methodInstance->GetMethodInfoEx()->mMangledName.IsEmpty()))
				BfMangler::Mangle(methodInstance->GetMethodInfoEx()->mMangledName, mCompiler->GetMangleKind(), methodInstance);
		}
	}
}

bool BfContext::IsCancellingAndYield()
{
	mSystem->CheckLockYield();
	return mCompiler->mCanceling;
}

void BfContext::QueueFinishModule(BfModule* module)
{
	bool needsDefer = false;

	if (mCompiler->mMaxInterfaceSlots == -1)
	{
		if (module->mUsedSlotCount == 0)
			needsDefer = true;
		else if (module->mHasFullDebugInfo)
		{
			// The virtual index for methods requires waiting
			for (auto ownedTypeInst : module->mOwnedTypeInstances)
			{
				if (ownedTypeInst->IsInterface())
					needsDefer = true;
				else if (ownedTypeInst->IsObject())
				{
					for (auto& methodGroup : ownedTypeInst->mMethodInstanceGroups)
					{
						auto methodInstance = methodGroup.mDefault;
						if (methodInstance == NULL)
							continue;
						if ((methodInstance->mVirtualTableIdx != -1) && (!methodInstance->mMethodDef->mIsOverride))
							needsDefer = true;
					}
				}
			}
		}
	}

	if (!needsDefer)
		mFinishedModuleWorkList.push_back(module);
	module->mAwaitingFinish = true;
}

// For simplicity - if we're canceling then we just rebuild modules that had certain types of pending work items
void BfContext::CancelWorkItems()
{
	/*return;

	BfLogSysM("BfContext::CancelWorkItems\n");
	for (int workIdx = 0; workIdx < (int)mMethodSpecializationWorkList.size(); workIdx++)
	{		
		auto workItemRef = mMethodSpecializationWorkList[workIdx];
		if (workItemRef != NULL)		
			workItemRef->mFromModule->mHadBuildError = true;		
		workIdx = mMethodSpecializationWorkList.RemoveAt(workIdx);		
	}
	mMethodSpecializationWorkList.Clear();

	for (int workIdx = 0; workIdx < (int)mInlineMethodWorkList.size(); workIdx++)
	{
		auto workItemRef = mInlineMethodWorkList[workIdx];
		if (workItemRef != NULL)
			workItemRef->mFromModule->mHadBuildError = true;
		workIdx = mInlineMethodWorkList.RemoveAt(workIdx);
	}
	mInlineMethodWorkList.Clear();

	for (int workIdx = 0; workIdx < (int)mMethodWorkList.size(); workIdx++)
	{
		auto workItemRef = mMethodWorkList[workIdx];
		if (workItemRef != NULL)
			workItemRef->mFromModule->mHadBuildError = true;
		workIdx = mMethodWorkList.RemoveAt(workIdx);
	}
	mMethodWorkList.Clear();*/
}

bool BfContext::ProcessWorkList(bool onlyReifiedTypes, bool onlyReifiedMethods)
{	
	bool didAnyWork = false;		

	while (!mCompiler->mCanceling)
	{		
		BfParser* resolveParser = NULL;
		if (mCompiler->mResolvePassData != NULL)
			resolveParser = mCompiler->mResolvePassData->mParser;

		bool didWork = false;				
		
		//for (auto itr = mReifyModuleWorkList.begin(); itr != mReifyModuleWorkList.end(); )
		for (int workIdx = 0; workIdx < mReifyModuleWorkList.size(); workIdx++)
		{
			BP_ZONE("PWL_ReifyModule");			
			if (IsCancellingAndYield())
				break; 
			
			BfModule* module = mReifyModuleWorkList[workIdx];
			if (module == NULL)
			{
				workIdx = mReifyModuleWorkList.RemoveAt(workIdx);
				continue;
			}

			if (!module->mIsReified)
				module->ReifyModule();

			workIdx = mReifyModuleWorkList.RemoveAt(workIdx);

			didWork = true;
		}
		
		for (int workIdx = 0; workIdx < (int)mPopulateTypeWorkList.size(); workIdx++)
		{
			//BP_ZONE("PWL_PopulateType");			
			if (IsCancellingAndYield())
				break; 

			auto workItemRef = mPopulateTypeWorkList[workIdx];
			if (workItemRef == NULL)
			{
				workIdx = mPopulateTypeWorkList.RemoveAt(workIdx);
				continue;
			}

			BfType* type = workItemRef->mType;
			bool rebuildType = workItemRef->mRebuildType;

			if ((onlyReifiedTypes) && (!type->IsReified()))
			{				
				continue;
			}

			auto typeInst = type->ToTypeInstance();
			if ((typeInst != NULL) && (resolveParser != NULL))
			{				
				if (!typeInst->mTypeDef->GetLatest()->HasSource(resolveParser))
				{					
					continue;
				}
			}			
			
			workIdx = mPopulateTypeWorkList.RemoveAt(workIdx);
			
			if (rebuildType)
				RebuildType(type);

			BF_ASSERT(this == type->mContext);						
			auto useModule = type->GetModule();
			if (useModule == NULL)
			{
				if (mCompiler->mOptions.mCompileOnDemandKind == BfCompileOnDemandKind_AlwaysInclude)
					useModule = mScratchModule;
				else
					useModule = mUnreifiedModule;
			}
			useModule->PopulateType(type, BfPopulateType_Full);			
			mCompiler->mStats.mQueuedTypesProcessed++;
			mCompiler->UpdateCompletion();
			didWork = true;						
		}				

		//while (mMethodSpecializationWorkList.size() != 0)

		// For the first pass, we want to handle the reified requests first. This helps rebuilds require
		//  fewer reifications of methods
		for (int methodSpecializationPass = 0; methodSpecializationPass < 2; methodSpecializationPass++)
		{
			bool wantsReified = methodSpecializationPass == 0;

			for (int workIdx = 0; workIdx < (int)mMethodSpecializationWorkList.size(); workIdx++)
			{
				if (IsCancellingAndYield())
					break;
				
				auto workItemRef = mMethodSpecializationWorkList[workIdx];
				if (workItemRef == NULL)
				{
					workIdx = mMethodSpecializationWorkList.RemoveAt(workIdx);
					continue;
				}
				
				if (wantsReified != workItemRef->mFromModule->mIsReified)
					continue;

				auto methodSpecializationRequest = *workItemRef;

				auto module = workItemRef->mFromModule;
				workIdx = mMethodSpecializationWorkList.RemoveAt(workIdx);

				auto typeInst = methodSpecializationRequest.mType->ToTypeInstance();
				module->GetMethodInstance(methodSpecializationRequest.mType->ToTypeInstance(), methodSpecializationRequest.mMethodDef, methodSpecializationRequest.mMethodGenericArguments, 
					(BfGetMethodInstanceFlags)(methodSpecializationRequest.mFlags | BfGetMethodInstanceFlag_ResultNotUsed));
				didWork = true;
			}
		}

		for (int workIdx = 0; workIdx < mMethodWorkList.size(); workIdx++)
		{
			BP_ZONE("PWL_ProcessMethod");			
								
			mSystem->CheckLockYield();
			// Don't allow canceling out of the first pass - otherwise we'll just keep reprocessing the 
			//  head of the file over and over
			if ((resolveParser == NULL) && (mCompiler->mCanceling))
				break;			
			
			auto workItem = mMethodWorkList[workIdx];
			if (workItem == NULL)
			{
				workIdx = mMethodWorkList.RemoveAt(workIdx);
				continue;
			}

			intptr prevPopulateTypeWorkListSize = mPopulateTypeWorkList.size();
			intptr prevInlineMethodWorkListSize = mInlineMethodWorkList.size();

			auto module = workItem->mFromModule;		
			auto methodInstance = workItem->mMethodInstance;

			bool wantProcessMethod = methodInstance != NULL;
			if ((workItem->mFromModuleRebuildIdx != -1) && (workItem->mFromModuleRebuildIdx != module->mRebuildIdx))
				wantProcessMethod = false;						

			if (methodInstance != NULL)
				BF_ASSERT(methodInstance->mMethodProcessRequest == workItem);
			
			bool hasBeenProcessed = true;
			if (wantProcessMethod)
			{
				if ((onlyReifiedMethods) && (!methodInstance->mIsReified))
				{
					continue;
				}

				auto owner = methodInstance->mMethodInstanceGroup->mOwner;

				BF_ASSERT(!module->mAwaitingFinish);
				if ((resolveParser != NULL) && (methodInstance->mMethodDef->mDeclaringType != NULL) && (methodInstance->mMethodDef->mDeclaringType->mSource != resolveParser))
				{					
					continue;
				}
				
				hasBeenProcessed = methodInstance->mHasBeenProcessed;
				BF_ASSERT(module->mContext == this);

				if (owner->IsIncomplete())
					module->PopulateType(owner, BfPopulateType_Full);

				if (methodInstance->mDeclModule != NULL)
					ProcessMethod(methodInstance);
			}
			
			workIdx = mMethodWorkList.RemoveAt(workIdx);
			
			if (methodInstance != NULL)
				methodInstance->mMethodProcessRequest = NULL;

			if ((!module->mAwaitingFinish) && (module->WantsFinishModule()))
			{								
				BfLogSysM("Module finished: %p %s HadBuildErrors:%d\n", module, module->mModuleName.c_str(), module->mHadBuildError);				
				QueueFinishModule(module);				
			}

			didWork = true;			
		}

// 		for (int workIdx = 0; workIdx < (int)mFinishedSlotAwaitModuleWorkList.size(); workIdx++)
// 		{
// 			auto& moduleRef = mFinishedSlotAwaitModuleWorkList[workIdx];
// 			if (moduleRef == NULL)
// 			{
// 				workIdx = mFinishedSlotAwaitModuleWorkList.RemoveAt(workIdx);
// 				continue;
// 			}
// 
// 			auto module = moduleRef;
// 			if (mCompiler->mMaxInterfaceSlots >= 0)
// 			{				
// 				mFinishedModuleWorkList.Add(module);
// 			}
// 
// 			workIdx = mFinishedSlotAwaitModuleWorkList.RemoveAt(workIdx);
// 			didWork = true;
// 		}

		for (int workIdx = 0; workIdx < (int)mFinishedModuleWorkList.size(); workIdx++)
		{
			//auto module = *moduleItr;
			auto& moduleRef = mFinishedModuleWorkList[workIdx];

			if (moduleRef == NULL)
			{
				workIdx = mFinishedModuleWorkList.RemoveAt(workIdx);
				continue;
			}

			auto module = moduleRef;
			if (!module->mAwaitingFinish)
			{
				BfLogSysM("mFinishedModuleWorkList removing old:%p\n", module);
				workIdx = mFinishedModuleWorkList.RemoveAt(workIdx);				
				continue;
			}

			//if (module->mAwaitingFinish)

			BfLogSysM("mFinishedModuleWorkList handling:%p\n", module);

			mSystem->CheckLockYield();

			if (mPopulateTypeWorkList.size() > 0)
			{
				// We can't finish modules unless all DI forward references have been replaced
				break;
			}

			BP_ZONE("PWL_ProcessFinishedModule");			
			
			bool hasUnfinishedSpecModule = false;
			for (auto& specModulePair : module->mSpecializedMethodModules)
			{
				auto specModule = specModulePair.mValue;
				if ((specModule->mAwaitingFinish) || (specModule->mIsModuleMutable))
					hasUnfinishedSpecModule = true;
			}

			if (hasUnfinishedSpecModule)
			{				
				continue;
			}
			
			if (!module->mIsSpecialModule)
			{
				module->Finish();
				if (mCompiler->mIsResolveOnly)
					module->ClearModuleData();
			}

			mCompiler->UpdateCompletion();
			workIdx = mFinishedModuleWorkList.RemoveAt(workIdx);
			didWork = true;
		}

		for (int workIdx = 0; workIdx < (int)mInlineMethodWorkList.size(); workIdx++)
		{
			BP_ZONE("PWL_ProcessMethod");			

			mSystem->CheckLockYield();
			// Don't allow canceling out of the first pass - otherwise we'll just keep reprocessing the 
			//  head of the file over and over
			if ((resolveParser == NULL) && (mCompiler->mCanceling))
				break;
			auto workItemRef = mInlineMethodWorkList[workIdx];
			if (workItemRef == NULL)
			{
				workIdx = mInlineMethodWorkList.RemoveAt(workIdx);
				continue;
			}

			auto workItem = *workItemRef;
			auto owner = workItem.mMethodInstance->mMethodInstanceGroup->mOwner;
			auto module = workItem.mFromModule;
			auto methodInstance = workItem.mMethodInstance;

			workIdx = mInlineMethodWorkList.RemoveAt(workIdx);

			BfLogSysM("Module %p inlining method %p into func:%p\n", module, methodInstance, workItem.mFunc);

			BfMethodInstance dupMethodInstance = *methodInstance;
			if (dupMethodInstance.mMethodInfoEx != NULL)
			{
				dupMethodInstance.mMethodInfoEx = new BfMethodInfoEx();
				*dupMethodInstance.mMethodInfoEx = *(methodInstance->mMethodInfoEx);
				for (auto genericParam : dupMethodInstance.mMethodInfoEx->mGenericParams)
					genericParam->AddRef();				
				dupMethodInstance.mMethodInfoEx->mMethodCustomAttributes = NULL;
			}
			dupMethodInstance.mHasBeenProcessed = false;			
			dupMethodInstance.mIRFunction = workItem.mFunc;
			dupMethodInstance.mMethodProcessRequest = NULL;
			dupMethodInstance.mIsReified = true;			
			dupMethodInstance.mHotMethod = NULL;
			BF_ASSERT(module->mIsReified); // We should only bother inlining in reified modules

			{
				// These errors SHOULD be duplicates, but if we have no other errors at all then we don't ignoreErrors, which
				//  may help unveil some kinds of compiler bugs
				SetAndRestoreValue<bool> prevIgnoreErrors(module->mIgnoreErrors, mCompiler->mPassInstance->HasFailed());
				module->ProcessMethod(&dupMethodInstance, true);
			}

			static int sMethodIdx = 0;
			module->mBfIRBuilder->Func_SetLinkage(workItem.mFunc, BfIRLinkageType_Internal);
			
			BF_ASSERT(module->mContext == this);
			BF_ASSERT(module->mIsModuleMutable);			
			if (module->WantsFinishModule())
			{
				BfLogSysM("Module finished: %s (from inlining)\n", module->mModuleName.c_str());
				QueueFinishModule(module);				
			}

			didWork = true;
		}

		if (!didWork)
		{
			if ((mPopulateTypeWorkList.size() == 0) && (resolveParser == NULL))
			{
				BP_ZONE("PWL_CheckIncompleteGenerics");				

				for (auto type : mResolvedTypes)
				{					
					if ((type->IsIncomplete()) && (type->HasBeenReferenced()))
					{
						// The only reason a type instance wouldn't have already been in the work list is
						//  because it's a generic specialization that was eligible for deletion,
						//  but it has been referenced now so we need to complete it, OR
						//  if this is from a newly-reified module

// 						if (false)
// 						{
// 							// For debugging
// 							mScratchModule->PopulateType(type, BfPopulateType_Full);
// 						}

						BfTypeProcessRequest* typeProcessRequest = mPopulateTypeWorkList.Alloc();
						typeProcessRequest->mType = type;						
						mCompiler->mStats.mTypesQueued++;
						mCompiler->UpdateCompletion();
						didWork = true;
					}
				}
			}
		}

		if (!didWork)
			break;
		didAnyWork = true;
	}	

	return didAnyWork;
}

void BfContext::HandleChangedTypeDef(BfTypeDef* typeDef, bool isAutoCompleteTempType)
{
	if ((mCompiler->mResolvePassData == NULL) || (!typeDef->HasSource(mCompiler->mResolvePassData->mParser)))
		return;

	if (typeDef->mDefState != BfTypeDef::DefState_Defined)
	{		
		if (mCompiler->mResolvePassData->mSourceClassifier != NULL)
		{							
			auto _CheckSource = [&](BfTypeDef* checkTypeDef)
			{
				auto typeDecl = checkTypeDef->mTypeDeclaration;
				if (checkTypeDef->mNextRevision != NULL)
					typeDecl = checkTypeDef->mNextRevision->mTypeDeclaration;
				if (typeDecl == NULL)
					return;

				if (typeDecl->GetSourceData() == mCompiler->mResolvePassData->mParser->mSourceData)
				{
					SetAndRestoreValue<bool> prevSkipTypeDeclaration(mCompiler->mResolvePassData->mSourceClassifier->mSkipTypeDeclarations, true);
					auto classifier = mCompiler->mResolvePassData->mSourceClassifier;
					classifier->mSkipMethodInternals = isAutoCompleteTempType;
					classifier->Handle(typeDecl);
				}
			};

			if (typeDef->mIsCombinedPartial)
			{
				for (auto partial : typeDef->mPartials)
					_CheckSource(partial);
			}
			else
			{
				_CheckSource(typeDef);
			}			
		}		
	}

	if (!typeDef->mIsPartial)
	{
		if ((typeDef->mDefState == BfTypeDef::DefState_New) ||
			(typeDef->mDefState == BfTypeDef::DefState_Deleted) ||
			(typeDef->mDefState == BfTypeDef::DefState_Signature_Changed))
		{
			mCompiler->mPassInstance->mHadSignatureChanges = true;
		}
	}
}

BfType * BfContext::FindTypeById(int typeId)
{
	for (auto type : mResolvedTypes)
	{		
		if (type->mTypeId == typeId)
			return type;
	}

	return NULL;
}

void BfContext::AddTypeToWorkList(BfType* type)
{
	if ((type->mRebuildFlags & BfTypeRebuildFlag_AddedToWorkList) == 0)
	{
		type->mRebuildFlags = (BfTypeRebuildFlags)(type->mRebuildFlags | BfTypeRebuildFlag_AddedToWorkList);

		BfTypeProcessRequest* typeProcessRequest = mPopulateTypeWorkList.Alloc();
		typeProcessRequest->mType = type;
		mCompiler->mStats.mTypesQueued++;
		mCompiler->UpdateCompletion();
	}
}

void BfContext::ValidateDependencies()
{
#if _DEBUG
// 	BP_ZONE("BfContext::ValidateDependencies");
// 	BfLogSysM("ValidateDependencies\n");
// 
// 	bool deletedNewTypes = false;
// 	auto itr = mResolvedTypes.begin();
// 	while (itr != mResolvedTypes.end())
// 	{
// 		auto type = itr.mCurEntry->mValue;		
// 		if ((type->IsGenericTypeInstance()) && (type->mDefineState > BfTypeDefineState_Undefined))
// 		{
// 			// We can't contain deleted generic arguments without being deleted ourselves
// 			BfGenericTypeInstance* genericType = (BfGenericTypeInstance*)type;
// 
// 			for (auto genericTypeArg : genericType->mTypeGenericArguments)
// 			{
// 				auto depType = genericTypeArg->ToDependedType();
// 				if (depType != NULL)
// 				{
// 					BF_ASSERT(depType->mDependencyMap.mTypeSet.ContainsKey(type));					
// 				}
// 			}
// 		}
// 		++itr;
// 	}
#endif
}

void BfContext::RebuildType(BfType* type, bool deleteOnDemandTypes, bool rebuildModule, bool placeSpecializiedInPurgatory)
{
	BfTypeInstance* typeInst = type->ToTypeInstance();		
		
	if (type->IsDeleting())
	{
		return;
	}

	type->mDirty = true;
		
	if (typeInst == NULL)
	{	
		type->mDefineState = BfTypeDefineState_Undefined;

		BfTypeProcessRequest* typeProcessRequest = mPopulateTypeWorkList.Alloc();
		typeProcessRequest->mType = type;		
		mCompiler->mStats.mTypesQueued++;
		mCompiler->UpdateCompletion();
		
		return;
	}

	// We need to verify lookups before we rebuild the type, because a type lookup change needs to count as a TypeDataChanged
	VerifyTypeLookups(typeInst);

	if (typeInst->mRevision != mCompiler->mRevision)
	{
		BfLogSysM("Setting revision.  Type: %p  Revision: %d\n", typeInst, mCompiler->mRevision);
		typeInst->mRevision = mCompiler->mRevision;
	}

	if ((typeInst->IsTypeAlias()) != (typeInst->mTypeDef->mTypeCode == BfTypeCode_TypeAlias))
	{		
		BfLogSysM("TypeAlias %p status changed - deleting\n", typeInst);
		DeleteType(type);
		return;		
	}
	
	if (mSystem->mWorkspaceConfigChanged)
	{
		typeInst->mTypeOptionsIdx = -2;
	}

	if (typeInst->mTypeFailed)
	{
		// The type definition failed, so we need to rebuild everyone that was depending on us
		RebuildDependentTypes(typeInst);
	}

	if ((typeInst->IsOnDemand()) && (deleteOnDemandTypes))
	{
		BfLogSysM("On-demand type %p attempted rebuild - deleting\n", typeInst);
		DeleteType(type);
		RebuildDependentTypes(typeInst);		
		return;
	}
	
	if (typeInst->mTypeDef->mDefState == BfTypeDef::DefState_Deleted)
		return;	
		
	if (typeInst->mDefineState == BfTypeDefineState_Undefined)
	{
		// If we haven't added this type the worklist yet then we reprocess the type rebuilding
		if ((typeInst->mRebuildFlags & BfTypeRebuildFlag_AddedToWorkList) != 0)
			return;	
	}	
	
	typeInst->mRebuildFlags = (BfTypeRebuildFlags)(typeInst->mRebuildFlags & ~BfTypeRebuildFlag_AddedToWorkList);

	bool addToWorkList = true;
	if ((typeInst->IsGenericTypeInstance()) && (!typeInst->IsUnspecializedType()) && (placeSpecializiedInPurgatory))
	{
		mCompiler->mGenericInstancePurgatory.push_back(typeInst);
		addToWorkList = false;
	}
	
	String typeName = mScratchModule->TypeToString(typeInst, BfTypeNameFlags_None);
	BfLogSysM("%p Rebuild Type: %p %s deleted:%d\n", this, typeInst, typeName.c_str(), typeInst->IsDeleting());
	if (addToWorkList)
	{
		AddTypeToWorkList(typeInst);		
	}

	// Why did we need to do this?  This caused all struct types to be rebuilt when we needed to rebuild ValueType due to
	//  ValueType.Equals<T> needing to rebuild -- which happens if any structs that have been compared have a signature change.
	/*for (auto depItr : typeInst->mDependencyMap)
	{
		auto dependentType = depItr.first;
		auto dependencyFlags = depItr.second.mFlags;
		if (dependencyFlags & BfDependencyMap::DependencyFlag_DerivedFrom)
		{
			//BfLogSysM("Setting BaseTypeMayBeIncomplete on %p from %p\n", dependentType, typeInst);
			//dependentType->mBaseTypeMayBeIncomplete = true;
			if (!dependentType->IsIncomplete())
				RebuildType(dependentType);
		}
	}*/
	
	if ((mCompiler->IsHotCompile()) && (!typeInst->IsTypeAlias()))
	{
		BF_ASSERT(typeInst->mHotTypeData != NULL);
		if (typeInst->mHotTypeData != NULL)
		{
			auto hotLatestVersionHead = typeInst->mHotTypeData->GetLatestVersionHead();
			if (!hotLatestVersionHead->mPopulatedInterfaceMapping)
			{
				typeInst->CalcHotVirtualData(&hotLatestVersionHead->mInterfaceMapping);
				hotLatestVersionHead->mPopulatedInterfaceMapping = true;
			}
			PopulateHotTypeDataVTable(typeInst);
		}
	}
	else
	{
		if (typeInst->mHotTypeData != NULL)
		{
			delete typeInst->mHotTypeData;
			typeInst->mHotTypeData = NULL;
		}
	}

	auto typeDef = typeInst->mTypeDef;	

	// Process deps before clearing mMethodInstanceGroups, to make sure we delete any methodrefs pointing to us before
	// deleting those methods
	for (auto& dep : typeInst->mDependencyMap)
	{
		auto depType = dep.mKey;
		auto depFlags = dep.mValue.mFlags;

		// If a MethodRef depends ON US, that means it's a local method that we own. MethodRefs directly point to
		// methodInstances, so these will be invalid now.
		if (depType->IsMethodRef())
		{
			auto methodRefType = (BfMethodRefType*)depType;
			BF_ASSERT(methodRefType->mOwner == typeInst);
			DeleteType(methodRefType);
		}

		if ((depFlags & BfDependencyMap::DependencyFlag_UnspecializedType) != 0)
		{
			if ((depType->mDefineState != BfTypeDefineState_Undefined) && (depType->mRevision != mCompiler->mRevision))
			{
				// Rebuild undefined type.  This isn't necessary when we modify the typeDef, but when we change configurations then
				//  the specialized types will rebuild
				
				//TODO: WE just added "no rebuild module" to this. I'm not sure what this is all about anyway...
				RebuildType(depType, true, false);
			}
		}
	}

	if ((!mCompiler->mIsResolveOnly) && (addToWorkList))
	{
		if (typeDef->mDefState == BfTypeDef::DefState_Signature_Changed)
		{
			typeInst->mSignatureRevision = mCompiler->mRevision;
		}
		else
		{
			bool needMethodCallsRebuild = false;
			for (auto& methodInstGroup : typeInst->mMethodInstanceGroups)
			{
				if (methodInstGroup.mMethodSpecializationMap != NULL)
				{
					for (auto& methodSpecializationItr : *methodInstGroup.mMethodSpecializationMap)
					{
						auto methodInstance = methodSpecializationItr.mValue;
						if ((!methodInstance->mIsUnspecialized) && (methodInstance->mHasFailed))
						{
							// A specialized generic method has failed, but the unspecialized version did not.  This 
							//  can only happen for 'var' constrained methods, and we need to cause all referring
							//  types to rebuild to ensure we're really specializing only the correct methods
							needMethodCallsRebuild = true;
						}
					}
				}
			}
			if (needMethodCallsRebuild)
			{				
				TypeMethodSignaturesChanged(typeInst);				
			}
		}
	}

	type->mDefineState = BfTypeDefineState_Undefined;	
	typeInst->mSpecializedMethodReferences.Clear();
	typeInst->mLookupResults.Clear();
	typeInst->mIsTypedPrimitive = false;
	typeInst->mMergedFieldDataCount = 0;
	typeInst->mTypeIncomplete = true;
	typeInst->mNeedsMethodProcessing = false;
	typeInst->mHasBeenInstantiated = false;
	typeInst->mHasParameterizedBase = false;
	typeInst->mTypeFailed = false;
	typeInst->mTypeWarned = false;		
	typeInst->mIsSplattable = false;
	typeInst->mHasPackingHoles = false;
	typeInst->mWantsGCMarking = false;	
	if (typeInst->mTypeInfoEx != NULL)
	{
		delete typeInst->mTypeInfoEx;
		typeInst->mTypeInfoEx = NULL;
	}

	if (typeInst->IsGenericTypeInstance())
	{
		auto genericTypeInstance = (BfGenericTypeInstance*)typeInst;
		genericTypeInstance->mTypeGenericArgumentRefs.Clear();
		for (auto genericParam : genericTypeInstance->mGenericParams)
			genericParam->Release();
		genericTypeInstance->mGenericParams.Clear();				
		genericTypeInstance->mValidatedGenericConstraints = false;
		genericTypeInstance->mHadValidateErrors = false;
		delete genericTypeInstance->mGenericExtensionInfo;
		genericTypeInstance->mGenericExtensionInfo = NULL;
		genericTypeInstance->mProjectsReferenced.Clear();
	}	

	typeInst->mStaticSearchMap.Clear();
	typeInst->mInterfaces.Clear();
	typeInst->mInterfaceMethodTable.Clear();
	typeInst->mMethodInstanceGroups.Clear();
	typeInst->mFieldInstances.Clear();
	for (auto methodInst : typeInst->mInternalMethods)
		delete methodInst;
	typeInst->mInternalMethods.Clear();
	typeInst->mHasStaticInitMethod = false;
	typeInst->mHasStaticMarkMethod = false;
	typeInst->mHasStaticDtorMethod = false;
	typeInst->mHasTLSFindMethod = false;
	typeInst->mBaseType = NULL;
	delete typeInst->mCustomAttributes;
	typeInst->mCustomAttributes = NULL;
	delete typeInst->mAttributeData;
	typeInst->mAttributeData = NULL;	
	typeInst->mVirtualMethodTableSize = 0;
	typeInst->mVirtualMethodTable.Clear();		
	typeInst->mSize = -1;
	typeInst->mAlign = -1;
	typeInst->mInstSize = -1;
	typeInst->mInstAlign = -1;
	typeInst->mInheritDepth = 0;
	delete typeInst->mConstHolder;
	typeInst->mConstHolder = NULL;

	if ((typeInst->mModule != NULL) && (rebuildModule))
	{
		typeInst->mModule->StartNewRevision();	
		typeInst->mRevision = mCompiler->mRevision;
	}	
}

void BfContext::RebuildDependentTypes(BfDependedType* dType)
{
	TypeDataChanged(dType, true);
	auto typeInst = dType->ToTypeInstance();
	if (typeInst != NULL)
		TypeMethodSignaturesChanged(typeInst);
}

// Dependencies cascade as such:
//  DerivedFrom / StructMemberData: these change the layout of memory for the dependent classes,
//   so not only do the dependent classes need to be rebuild, but any other classes relying on those derived classes
//   (either by derivation, containment, or field reading) need to have their code recompiled as well.
//  ReadFields: when ClassB depends on the data layout of ClassA, and ClassC reads a field from
//   ClassB, it means that ClassC code needs to be recompiled if ClassA data layout changes, but performing a ReadField
//   (obviously) doesn't change the data layout of ClassC
//  Calls: non-cascading dependency, since it's independent of data layout ConstValue: non-cascading data change
void BfContext::TypeDataChanged(BfDependedType* dType, bool isNonStaticDataChange)
{	
	BfLogSysM("TypeDataChanged %p\n", dType);

	auto rebuildFlag = isNonStaticDataChange ? BfTypeRebuildFlag_NonStaticChange : BfTypeRebuildFlag_StaticChange;
	if ((dType->mRebuildFlags & rebuildFlag) != 0) // Already did this change?
		return;
	dType->mRebuildFlags = (BfTypeRebuildFlags)(dType->mRebuildFlags | rebuildFlag);
	
	// We need to rebuild all other types that rely on our data layout
	for (auto& depItr : dType->mDependencyMap)
	{
		auto dependentType = depItr.mKey;
		auto dependencyFlags = depItr.mValue.mFlags;
		
		auto dependentDType = dependentType->ToDependedType();
		if (dependentDType != NULL)
		{
			auto dependentTypeInstance = dependentType->ToTypeInstance();
			if (isNonStaticDataChange)
			{
				bool hadChange = false;

				if ((dependencyFlags & BfDependencyMap::DependencyFlag_DerivedFrom) ||
					(dependencyFlags & BfDependencyMap::DependencyFlag_ValueTypeMemberData) ||
					(dependencyFlags & BfDependencyMap::DependencyFlag_NameReference))
				{
					hadChange = true;					
				}

				// This case is for when we were declared as a class on a previous compilation, 
				//  but then we were changed to a struct
				if ((dType->IsValueType()) &&
					(dependencyFlags & BfDependencyMap::DependencyFlag_PtrMemberData))
				{
					hadChange = true;
				}

				if (mCompiler->IsHotCompile())
				{
					// VData layout may be changing if there's a data change...
					if (dependencyFlags & BfDependencyMap::DependencyFlag_VirtualCall)
					{
						hadChange = true;
					}
				}

				if (hadChange)
					TypeDataChanged(dependentDType, true);
			}

			if (dependencyFlags & BfDependencyMap::DependencyFlag_ConstValue)
			{
				TypeDataChanged(dependentDType, false);

				
				// The ConstValue dependency may be that dependentType used one of our consts as
				//  a default value to a method param, so assume callsites need rebuilding
				if (dependentTypeInstance != NULL)
					TypeMethodSignaturesChanged(dependentTypeInstance);
			}

			// We need to include DependencyFlag_ParamOrReturnValue because it could be a struct that changes its splatting ability
			//  We can't ONLY check against structs, though, because a type could change from a class to a struct
			if (dependencyFlags & 
				(BfDependencyMap::DependencyFlag_ReadFields | BfDependencyMap::DependencyFlag_ParamOrReturnValue | 
				 BfDependencyMap::DependencyFlag_LocalUsage | BfDependencyMap::DependencyFlag_MethodGenericArg | 
				 BfDependencyMap::DependencyFlag_Allocates))
			{
				RebuildType(dependentType);
			}
		}
		else
		{
			// Not a type instance, probably something like a sized array
			RebuildType(dependentType);
		}
	}
		
	RebuildType(dType);	
}

void BfContext::TypeMethodSignaturesChanged(BfTypeInstance* typeInst)
{	
	if (typeInst->mRebuildFlags & BfTypeRebuildFlag_MethodSignatureChange) // Already did change?
		return;
	typeInst->mRebuildFlags = (BfTypeRebuildFlags) (typeInst->mRebuildFlags | BfTypeRebuildFlag_MethodSignatureChange);

	BfLogSysM("TypeMethodSignaturesChanged %p\n", typeInst);

	// These don't happen in TypeDataChanged because we don't need to cascade
	for (auto& depItr : typeInst->mDependencyMap)
	{
		auto dependentType = depItr.mKey;
		auto dependencyFlags = depItr.mValue.mFlags;

		// We don't need to cascade rebuilding for method-based usage - just rebuild the type directly (unlike TypeDataChanged, which cascades)
		if ((dependencyFlags & BfDependencyMap::DependencyFlag_Calls) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_VirtualCall) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_InlinedCall) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_MethodGenericArg) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_CustomAttribute) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_DerivedFrom) ||
			(dependencyFlags & BfDependencyMap::DependencyFlag_ImplementsInterface))
		{
			RebuildType(dependentType);
		}
	}
}

void BfContext::TypeInlineMethodInternalsChanged(BfTypeInstance* typeInst)
{
	if (typeInst->mRebuildFlags & BfTypeRebuildFlag_MethodInlineInternalsChange) // Already did change?
		return;
	typeInst->mRebuildFlags = (BfTypeRebuildFlags)(typeInst->mRebuildFlags | BfTypeRebuildFlag_MethodInlineInternalsChange);

	// These don't happen in TypeDataChanged because we don't need to cascade
	for (auto& depItr : typeInst->mDependencyMap)
	{
		auto dependentType = depItr.mKey;
		auto dependencyFlags = depItr.mValue.mFlags;

		// We don't need to cascade rebuilding for method-based usage - just rebuild the type directly (unlike TypeDataChanged, which cascades)
		if (dependencyFlags & BfDependencyMap::DependencyFlag_InlinedCall)
		{
			RebuildType(dependentType);
		}
	}
}

void BfContext::PopulateHotTypeDataVTable(BfTypeInstance* typeInstance)
{
	BP_ZONE("BfContext::PopulateHotTypeDataVTable");

	if (typeInstance->IsTypeAlias())
		return;

	// The hot virtual table only holds our new entries, not the vtable entries inherited from our base classes
	auto hotTypeData = typeInstance->mHotTypeData;
	if (hotTypeData == NULL)
		return;
	
	if (typeInstance->IsIncomplete())
	{
		if (mCompiler->mHotState->mCommittedHotCompileIdx > 0)
		{
			// Should already be populated
			BF_ASSERT(hotTypeData->mVTableOrigLength != -1);
		}
		return;
	}

	if (hotTypeData->mVTableOrigLength == -1)
	{		
		auto committedHotTypeVersion = typeInstance->mHotTypeData->GetTypeVersion(mCompiler->mHotState->mCommittedHotCompileIdx);
		if (committedHotTypeVersion != NULL)
		{
			hotTypeData->mVTableOrigLength = typeInstance->mVirtualMethodTableSize;
			hotTypeData->mOrigInterfaceMethodsLength = typeInstance->GetIFaceVMethodSize();
		}
		BfLogSysM("PopulateHotTypeDataVTable set %p HotDataType->mVTableOrigLength To %d\n", typeInstance, hotTypeData->mVTableOrigLength);
	}	

	int vTableStart = -1;
	int primaryVTableSize = 0;

	if (typeInstance->IsInterface())
	{
		// Interfaces don't have vext markers
		vTableStart = 0;

#ifdef _DEBUG
		for (int vIdx = 0; vIdx < (int)typeInstance->mVirtualMethodTable.size(); vIdx++)
		{
			auto& methodRef = typeInstance->mVirtualMethodTable[vIdx].mDeclaringMethod;
			if (methodRef.mMethodNum == -1)
			{				
				BF_DBG_FATAL("Shouldn't have vext marker");				
			}
		}
#endif
	}
	else
	{
		for (int vIdx = 0; vIdx < (int)typeInstance->mVirtualMethodTable.size(); vIdx++)
		{
			auto& methodRef = typeInstance->mVirtualMethodTable[vIdx].mDeclaringMethod;
			if (methodRef.mMethodNum == -1)
			{
				if (methodRef.mTypeInstance == typeInstance)
				{
					vTableStart = vIdx;
				}
				else if (vTableStart != -1)
				{
					BF_DBG_FATAL("Shouldn't have another vext marker");
					break;
				}
			}
		}
	}
	primaryVTableSize = (int)typeInstance->mVirtualMethodTable.size() - vTableStart;

	BF_ASSERT(vTableStart != -1);
	
	if (primaryVTableSize > (int)hotTypeData->mVTableEntries.size())
		hotTypeData->mVTableEntries.Resize(primaryVTableSize);

	int methodIdx = -1;	
	for (int vIdx = 0; vIdx < primaryVTableSize; vIdx++)
	{
		auto& methodRef = typeInstance->mVirtualMethodTable[vTableStart + vIdx].mDeclaringMethod;
		methodIdx++;
		auto methodInstance = (BfMethodInstance*)methodRef;
		if (methodInstance == NULL)
			continue;
		
		BF_ASSERT(methodRef.mTypeInstance == typeInstance);
		BF_ASSERT(methodInstance->mVirtualTableIdx != -1);
		BF_ASSERT(!methodInstance->mMethodDef->mIsOverride);

		// Find the original non-override method
		/*while (methodInstance->mMethodDef->mIsOverride)
		{
			BfTypeInstance* parent = methodInstance->GetOwner()->mBaseType;
			auto parentVirtualMethod = parent->mVirtualMethodTable[methodInstance->mVirtualTableIdx];
			BF_ASSERT(parentVirtualMethod->mVirtualTableIdx != -1);
			methodInstance = parentVirtualMethod;
		}*/

		BF_ASSERT(!methodInstance->mMethodInfoEx->mMangledName.IsEmpty());

		auto& entry = hotTypeData->mVTableEntries[vIdx];
		if (entry.mFuncName.empty())
		{			
			entry.mFuncName = methodInstance->mMethodInfoEx->mMangledName;
		}
		else
		{
			// Make sure its the same still
			BF_ASSERT(entry.mFuncName == methodInstance->mMethodInfoEx->mMangledName);
		}
	}
}

void BfContext::SaveDeletingType(BfType* type)
{
	if (mCompiler->mIsResolveOnly)
		return;

	if ((type->mRebuildFlags) && ((type->mRebuildFlags & BfTypeRebuildFlag_TypeDataSaved) != 0))
		return;
	type->mRebuildFlags = (BfTypeRebuildFlags)(type->mRebuildFlags | BfTypeRebuildFlag_TypeDataSaved);

	String mangledName = BfSafeMangler::Mangle(type, mUnreifiedModule);
	BfLogSysM("Saving deleted type: %p %s\n", type, mangledName.c_str());

	BfSavedTypeData** savedTypeDataPtr;
	BfSavedTypeData* savedTypeData;
	if (mSavedTypeDataMap.TryAdd(mangledName, NULL, &savedTypeDataPtr))
	{
		savedTypeData = new BfSavedTypeData();
		*savedTypeDataPtr = savedTypeData;
	}
	else
	{
		// This can happen if we have a conflicting type definition
		savedTypeData = *savedTypeDataPtr;		
	}
	savedTypeData->mTypeId = type->mTypeId;
	while ((int)mSavedTypeData.size() <= savedTypeData->mTypeId)
		mSavedTypeData.Add(NULL);
	mSavedTypeData[savedTypeData->mTypeId] = savedTypeData;

	auto typeInst = type->ToTypeInstance();
	if (typeInst != NULL)
	{		
		delete savedTypeData->mHotTypeData;
		if (mCompiler->IsHotCompile())
			savedTypeData->mHotTypeData = typeInst->mHotTypeData;
		else
			delete typeInst->mHotTypeData;
		typeInst->mHotTypeData = NULL;
	}
}

BfType* BfContext::FindType(const StringImpl& fullTypeName)
{
	int genericArgCount = 0;
	String typeName = fullTypeName;
	if (typeName.EndsWith('>'))
	{
		// Generic
	}

	BfTypeDef* typeDef = mSystem->FindTypeDef(typeName, genericArgCount);
	if (typeDef == NULL)
		return NULL;

	return mUnreifiedModule->ResolveTypeDef(typeDef);
}

String BfContext::TypeIdToString(int typeId)
{
	auto type = mTypes[typeId];
	if (type != NULL)
		return mScratchModule->TypeToString(type);
	if (mCompiler->mHotState != NULL)
	{
		for (auto& kv : mCompiler->mHotState->mDeletedTypeNameMap)
		{
			if (kv.mValue == typeId)
				return kv.mKey;
		}
	}
	return StrFormat("#%d", typeId);
}

BfHotTypeData* BfContext::GetHotTypeData(int typeId)
{
	auto type = mTypes[typeId];
	if (type != NULL)
	{
		auto typeInst = type->ToTypeInstance();
		if (typeInst != NULL)
			return typeInst->mHotTypeData;
	}

	if (typeId < (int)mSavedTypeData.size())
	{
		auto savedTypeData = mSavedTypeData[typeId];
		if (savedTypeData != NULL)
			return savedTypeData->mHotTypeData;
	}

	return NULL;
}

void BfContext::DeleteType(BfType* type, bool deferDepRebuilds)
{
	if (type == mBfObjectType)
		mBfObjectType = NULL;	
	if (type == mBfTypeType)	
		mBfObjectType = NULL;

	if (type->mRebuildFlags & BfTypeRebuildFlag_Deleted)
		return;	
	
	mCompiler->mStats.mTypesDeleted++;

	BfDependedType* dType = type->ToDependedType();
	BfTypeInstance* typeInst = type->ToTypeInstance();
	if (typeInst != NULL)
	{
// 		if (typeInst->mTypeDef->IsGlobalsContainer())
// 		{
// 			bool worked = mSystem->mGlobalsMap.Remove(typeInst->mTypeDef->mNamespace);
// 			BF_ASSERT(worked);
// 		}

		if (mCompiler->mHotState != NULL)
		{
			if ((typeInst->mHotTypeData != NULL) && (typeInst->mHotTypeData->mPendingDataChange))
				mCompiler->mHotState->RemovePendingChanges(typeInst);
			String typeName = mScratchModule->TypeToString(typeInst);
			mCompiler->mHotState->mDeletedTypeNameMap[typeName] = typeInst->mTypeId;
		}

		auto module = typeInst->mModule;
		// Don't remove the mModule pointer in typeInst -- if the type ends up being a zombie then we still need
		//  to generate the VData from the type
		if (module != NULL)
		{
			if (module->mIsScratchModule)
			{
				BF_ASSERT(module->mOwnedTypeInstances.size() == 0);
			}
			else
			{
				auto itr = std::find(module->mOwnedTypeInstances.begin(), module->mOwnedTypeInstances.end(), typeInst);
				module->mOwnedTypeInstances.erase(itr);

				if ((module->mOwnedTypeInstances.size() == 0) && (module != mScratchModule))
				{					
					// This module is no longer needed
					module->RemoveModuleData();
					module->mIsDeleting = true;
					auto itr = std::find(mModules.begin(), mModules.end(), module);
					mModules.erase(itr);

					// This was only needed for 'zombie modules', which we don't need anymore?
					//  To avoid linking errors.  Used instead of directly removing from mModules.
					mDeletingModules.push_back(module);
				}
			}
		}
	}
	
	type->mRebuildFlags = (BfTypeRebuildFlags)((type->mRebuildFlags | BfTypeRebuildFlag_Deleted) & ~BfTypeRebuildFlag_DeleteQueued);
	SaveDeletingType(type);
	
	mTypes[type->mTypeId] = NULL;

 	BfLogSysM("Deleting Type: %p %s\n", type, mScratchModule->TypeToString(type).c_str());
	
	// All dependencies cause rebuilds when we delete types	
	if (dType != NULL)
	{
		//TODO: Do PopulateHotTypeDataVTable then store the HotTypeDataData
		
		if (dType->IsUnspecializedType())
		{				
			/*auto itr = mScratchModule->mClassVDataRefs.find(typeInst);
			if (itr != mScratchModule->mClassVDataRefs.end())
				mScratchModule->mClassVDataRefs.erase(itr);*/
			
			mScratchModule->mClassVDataRefs.Remove(typeInst);
		}		
		
		//UH - I think this is not true.
		//  If A derives from B, and C derives from B, if we delete 'A' then it's true that
		//   'C' won't rebuild otherwise, BUT 'B' would fail to build but it would do a TypeDataChanged once it WAS able to built.  Right?
		// Even though we do rebuilds on all types below, we specifically need to call
		//  TypeDataChanged here for cascading data dependencies		
		/*if (!deferDepRebuilds)
			TypeDataChanged(typeInst, true);*/
		
		Array<BfType*> rebuildTypeQueue;

		for (auto& depItr : dType->mDependencyMap)
		{
			//bool rebuildType = false;

			auto dependentType = depItr.mKey;
			auto dependentTypeInst = dependentType->ToTypeInstance();
			auto dependencyEntry = depItr.mValue;
			if ((dependencyEntry.mFlags & (BfDependencyMap::DependencyFlag_MethodGenericArg)) != 0)
			{
				if (!dependentType->IsDeleting())
				{					
					if ((deferDepRebuilds) && (dependentTypeInst != NULL))
						mQueuedSpecializedMethodRebuildTypes.Add(dependentTypeInst);
				}
			}

			if ((dependencyEntry.mFlags & (BfDependencyMap::DependencyFlag_TypeGenericArg)) != 0)
			{
				// This type can't exist anymore
				DeleteType(dependentType, deferDepRebuilds);
				continue;
			}
			
			if (dependentTypeInst == NULL)
			{
				// This was something like a sized array
				DeleteType(dependentType, deferDepRebuilds);
				continue;
			}

			if ((dependencyEntry.mFlags & ~(BfDependencyMap::DependencyFlag_UnspecializedType)) == 0)
				continue; // Not a cause for rebuilding

			if ((deferDepRebuilds) && (dependentTypeInst != NULL))
				mFailTypes.Add(dependentTypeInst);
			else
			{
// 				BfTypeProcessRequest* typeProcessRequest = mPopulateTypeWorkList.Alloc();
// 				typeProcessRequest->mType = dependentType;
// 				typeProcessRequest->mRebuildType = true;
// 				mCompiler->mStats.mTypesQueued++;
// 				mCompiler->UpdateCompletion();
				rebuildTypeQueue.Add(dependentType);
			}
		}			

 		if (type->IsMethodRef())
 		{	
 			// Detach
 			auto methodRefType = (BfMethodRefType*)type;
 			BfMethodInstance* methodInstance = methodRefType->mMethodRef;
 			BF_ASSERT(methodInstance->mMethodInstanceGroup->mRefCount > 0);
 			methodInstance->mMethodInstanceGroup->mRefCount--;
 			methodRefType->mMethodRef = NULL;
 			methodInstance->mHasMethodRefType = false;
 		}

 		for (auto dependentType : rebuildTypeQueue)
 			RebuildType(dependentType);
	}
}

void BfContext::UpdateAfterDeletingTypes()
{
	BP_ZONE("BfContext::UpdateAfterDeletingTypes");
	BfLogSysM("UpdateAfterDeletingTypes\n");
	
	int graveyardStart = (int)mTypeGraveyard.size();	

	while (true)
	{
		bool deletedNewTypes = false;
		auto itr = mResolvedTypes.begin();
		while (itr != mResolvedTypes.end())
		{
			auto type = itr.mCurEntry->mValue;

			bool doDelete = false;
			//BfLogSysM("Removing entry\n");
			bool isDeleting = type->IsDeleting();
			if ((!isDeleting) && (type->IsDependentOnUnderlyingType()))
			{
				auto underlyingType = type->GetUnderlyingType();
				if ((underlyingType != NULL) && (underlyingType->IsDeleting()))
				{
					deletedNewTypes = true;
					isDeleting = true;
					DeleteType(type);
				}				
			}

			if (isDeleting)
			{				
				doDelete = true;
			}
			else
			{
#if _DEBUG
				if (type->IsGenericTypeInstance())
				{
					// We can't contain deleted generic arguments without being deleted ourselves
					BfGenericTypeInstance* genericType = (BfGenericTypeInstance*)type;

					for (auto genericTypeArg : genericType->mTypeGenericArguments)
					{
						BF_ASSERT((!genericTypeArg->IsDeleting()));
					}
				}
#endif
			}

			if (doDelete)
			{
				BF_ASSERT((type->mRebuildFlags & BfTypeRebuildFlag_Deleted) == BfTypeRebuildFlag_Deleted);
				itr = mResolvedTypes.Erase(itr);
				mTypeGraveyard.push_back(type);
			}
			else
				++itr;
		}

		if (!deletedNewTypes)
			break;
	}

#if _DEBUG
// 	auto itr = mResolvedTypes.begin();
// 	while (itr != mResolvedTypes.end())
// 	{
// 		auto type = itr.mCurEntry->mType;
// 		BF_ASSERT((type->mRebuildFlags & ~(BfTypeRebuildFlag_Deleted)) == 0);
// 		++itr;
// 	}
#endif
	
	if (!mCompiler->mIsResolveOnly)
	{
		BP_ZONE("BfContext::UpdateAfterDeletingTypes saving typeData");
		for (int graveyardIdx = graveyardStart; graveyardIdx < (int)mTypeGraveyard.size(); graveyardIdx++)
		{
			auto type = mTypeGraveyard[graveyardIdx];			
			SaveDeletingType(type);			
		}
	}
}

// This happens before the old defs have been injected
void BfContext::PreUpdateRevisedTypes()
{
// 	if (mCompiler->IsHotCompile())
// 	{
// 		for (auto typeEntry : mResolvedTypes)
// 		{
// 			auto type = typeEntry->mType;
// 			auto typeInst = type->ToTypeInstance();
// 			if (typeInst == NULL)
// 				continue;
// 
// 			auto typeDef = typeInst->mTypeDef;
// 			if ((typeDef->mDefState != BfTypeDef::DefState_New) && (typeDef->mDefState != BfTypeDef::DefState_Defined))
// 			{
// 				if (typeInst->mHotTypeData == NULL)
// 				{
// 					typeInst->mHotTypeData = new BfHotTypeData();					
// 					typeInst->CalcHotVirtualData(&typeInst->mHotTypeData->mInterfaceMapping);					
// 				}
// 				PopulateHotTypeDataVTable(typeInst);
// 			}
// 		}		
// 	}
}


// Note that this method can also cause modules to be build in other contexts.
//  That's why we do our UpdateAfterDeletingTypes after all the contexts' UpdateRevisedTypes
void BfContext::UpdateRevisedTypes()
{
	BP_ZONE("BfContext::UpdateRevisedTypes");

	int wantPtrSize;
	if ((mCompiler->mOptions.mMachineType == BfMachineType_x86) |
		(mCompiler->mOptions.mMachineType == BfMachineType_ARM))
		wantPtrSize = 4;
	else
		wantPtrSize = 8;
	if (wantPtrSize != mSystem->mPtrSize)
	{
		BfLogSysM("Changing pointer size to: %d\n", wantPtrSize);
		mSystem->mPtrSize = wantPtrSize;
		auto intPtrType = mScratchModule->GetPrimitiveType(BfTypeCode_IntPtr);
		auto uintPtrType = mScratchModule->GetPrimitiveType(BfTypeCode_UIntPtr);
		if (intPtrType != NULL)
			RebuildType(intPtrType);		
		if (uintPtrType != NULL)
			RebuildType(uintPtrType);

		// Rebuild all types
		for (auto type : mResolvedTypes)
		{			
			RebuildType(type);
		}
	}

	// Temporarily store failTypes - we may need to re-insert into them after another failure
	auto failTypes = mFailTypes;
	mFailTypes.Clear();	

	bool wantsDebugInfo = (mCompiler->mOptions.mEmitDebugInfo);
	
	Array<BfTypeInstance*> defStateChangedQueue;

	// Do primary 'rebuild' scan
	for (auto type : mResolvedTypes)
	{		
		auto typeInst = type->ToTypeInstance();
		if (type == NULL)
		{
			BF_FATAL("We shouldn't have NULLs");
			continue;
		}

		/*if ((!mCompiler->mIsResolveOnly) && (!type->IsNull()) && (!type->IsUnspecializedType()))
		{
			// We need to completely rebuild all types if we switch from having debug info to not having debug info			
			if ((typeInst != NULL) && (typeInst->mModule != NULL) && (typeInst->mModule->mHasDebugInfo != wantsDebugInfo))			
			{
				RebuildType(type);
			}
		}*/
					
		if (typeInst == NULL)
			continue;
		
		if (typeInst->IsDeleting())
			continue;

		auto typeDef = typeInst->mTypeDef;		
		if (typeDef->mProject->mDisabled)
		{
			DeleteType(type);
			continue;
		}

		typeInst->mRebuildFlags = BfTypeRebuildFlag_None;
						
		if (typeDef->mIsPartial)
		{
			// This was a type that wasn't marked as partial before but now it is, so it doesn't need its own typedef
			//  since we will have a separate type instance for the combined partials
			DeleteType(type);
			continue;
		}

		if ((typeInst->mHotTypeData != NULL) && (!mCompiler->IsHotCompile()))
		{
			if (typeInst->mHotTypeData->GetLatestVersion()->mDeclHotCompileIdx != 0)
			{
				// Type was rebuilt with hot changes - rebuild back to normal
				delete typeInst->mHotTypeData;
				typeInst->mHotTypeData = NULL;
				RebuildType(typeInst);
			}
		}

		if (typeDef->mDefState == BfTypeDef::DefState_Defined)
		{
			BF_ASSERT(typeDef->mNextRevision == NULL);
			continue;
		}

// 		if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mParser != NULL) && (!typeInst->IsSpecializedType()))
// 		{
// 			if (typeDef->HasSource(mCompiler->mResolvePassData->mParser))
// 			{
// 				HandleChangedTypeDef(typeDef);				
// 			}
// 		}

		if (typeDef->mDefState != BfTypeDef::DefState_New)
		{
			defStateChangedQueue.Add(typeInst);									
		}						
	}

	for (auto typeInst : defStateChangedQueue)
	{
		BP_ZONE("BfContext::UpdateRevisedTypes defStateChangedQueue");

		auto typeDef = typeInst->mTypeDef;

		bool isTypeDefinedInContext = true;		
		
		if (typeDef->mDefState == BfTypeDef::DefState_Deleted)
		{			
			HandleChangedTypeDef(typeDef);
			DeleteType(typeInst);
			continue;
		}

		if (typeDef->mDefState == BfTypeDef::DefState_InlinedInternals_Changed)
		{
			TypeInlineMethodInternalsChanged(typeInst);
		}

		bool isSignatureChange = typeDef->mDefState == BfTypeDef::DefState_Signature_Changed;
		if (((typeDef->mDefState == BfTypeDef::DefState_Internals_Changed) || (typeDef->mDefState == BfTypeDef::DefState_InlinedInternals_Changed)) &&
			(typeInst->IsInterface()))
		{
			isSignatureChange = true;
		}

		if (isSignatureChange)
		{
			TypeDataChanged(typeInst, true);
			TypeMethodSignaturesChanged(typeInst);			
		}

		/*if (!mCompiler->mIsResolveOnly)
		{
			OutputDebugStrF("TypeDef: %s %d %p\n", typeDef->mName.c_str(), typeDef->mDefState, &typeDef->mDefState);
		}*/

		RebuildType(typeInst);
	}
	
	for (auto typeInst : failTypes)
	{
		if (!typeInst->IsDeleting())
		{
			if (!typeInst->mTypeDef->mProject->mDisabled)
			{	
				RebuildType(typeInst);
			}
		}
	}

	//
	{
		AutoCrit autoCrit(mSystem->mDataLock);

		auto options = &mCompiler->mOptions;
		HashContext workspaceConfigHashCtx;												

		workspaceConfigHashCtx.MixinStr(options->mTargetTriple);
		workspaceConfigHashCtx.Mixin(options->mForceRebuildIdx);

		workspaceConfigHashCtx.Mixin(options->mMachineType);
		workspaceConfigHashCtx.Mixin(options->mToolsetType);
		workspaceConfigHashCtx.Mixin(options->mSIMDSetting);		

		workspaceConfigHashCtx.Mixin(options->mEmitDebugInfo);
		workspaceConfigHashCtx.Mixin(options->mEmitLineInfo);		

		workspaceConfigHashCtx.Mixin(options->mNoFramePointerElim);
		workspaceConfigHashCtx.Mixin(options->mInitLocalVariables);
		workspaceConfigHashCtx.Mixin(options->mRuntimeChecks);
		workspaceConfigHashCtx.Mixin(options->mAllowStructByVal);	
		workspaceConfigHashCtx.Mixin(options->mEmitDynamicCastCheck);

		workspaceConfigHashCtx.Mixin(options->mAllowHotSwapping);
		workspaceConfigHashCtx.Mixin(options->mObjectHasDebugFlags);
		workspaceConfigHashCtx.Mixin(options->mEnableRealtimeLeakCheck);
		workspaceConfigHashCtx.Mixin(options->mEmitObjectAccessCheck);
		workspaceConfigHashCtx.Mixin(options->mEnableCustodian);
		workspaceConfigHashCtx.Mixin(options->mEnableSideStack);
		workspaceConfigHashCtx.Mixin(options->mHasVDataExtender);
		workspaceConfigHashCtx.Mixin(options->mDebugAlloc);
		workspaceConfigHashCtx.Mixin(options->mOmitDebugHelpers);

		workspaceConfigHashCtx.Mixin(options->mUseDebugBackingParams);

		workspaceConfigHashCtx.Mixin(options->mWriteIR);
		workspaceConfigHashCtx.Mixin(options->mGenerateObj);

		workspaceConfigHashCtx.Mixin(options->mAllocStackCount);
		workspaceConfigHashCtx.Mixin(options->mExtraResolveChecks);
		workspaceConfigHashCtx.Mixin(options->mMaxSplatRegs);
		workspaceConfigHashCtx.MixinStr(options->mMallocLinkName);
		workspaceConfigHashCtx.MixinStr(options->mFreeLinkName);

		for (auto& typeOptions : mSystem->mTypeOptions)
		{
			workspaceConfigHashCtx.Mixin(typeOptions.mTypeFilters.size());
			for (auto& filter : typeOptions.mTypeFilters)
				workspaceConfigHashCtx.MixinStr(filter);
			workspaceConfigHashCtx.Mixin(typeOptions.mAttributeFilters.size());
			for (auto& filter : typeOptions.mAttributeFilters)
				workspaceConfigHashCtx.MixinStr(filter);
			workspaceConfigHashCtx.Mixin(typeOptions.mSIMDSetting);
			workspaceConfigHashCtx.Mixin(typeOptions.mOptimizationLevel);
			workspaceConfigHashCtx.Mixin(typeOptions.mEmitDebugInfo);
			workspaceConfigHashCtx.Mixin(typeOptions.mRuntimeChecks);
			workspaceConfigHashCtx.Mixin(typeOptions.mInitLocalVariables);
			workspaceConfigHashCtx.Mixin(typeOptions.mEmitDynamicCastCheck);
			workspaceConfigHashCtx.Mixin(typeOptions.mEmitObjectAccessCheck);
			workspaceConfigHashCtx.Mixin(typeOptions.mAllocStackTraceDepth);
		}

// 		for (auto project : mSystem->mProjects)
// 		{
// 			workspaceConfigHashCtx.MixinStr(project->mName);
// 		}

		Val128 workspaceConfigHash = workspaceConfigHashCtx.Finish128();

		mSystem->mWorkspaceConfigChanged = mSystem->mWorkspaceConfigHash != workspaceConfigHash;
		if (mSystem->mWorkspaceConfigChanged)
		{
			// If the type options have changed, we know we will rebuild all types and thus
			//  remap their mTypeOptionsIdx
			mSystem->mMergedTypeOptions.Clear();
			mSystem->mWorkspaceConfigHash = workspaceConfigHash;
		}
		
		for (auto project : mSystem->mProjects)
		{
			HashContext buildConfigHashCtx;
			buildConfigHashCtx.Mixin(workspaceConfigHash);
			
			if (!mCompiler->mIsResolveOnly)
			{
				auto& codeGenOptions = project->mCodeGenOptions;								
				
				buildConfigHashCtx.Mixin(project->mAlwaysIncludeAll);
				buildConfigHashCtx.Mixin(project->mSingleModule);

				bool isTestConfig = project->mTargetType == BfTargetType_BeefTest;
				buildConfigHashCtx.Mixin(isTestConfig);
				
				buildConfigHashCtx.Mixin(codeGenOptions.mOptLevel);
				buildConfigHashCtx.Mixin(codeGenOptions.mSizeLevel);
				buildConfigHashCtx.Mixin(codeGenOptions.mUseCFLAA);
				buildConfigHashCtx.Mixin(codeGenOptions.mUseNewSROA);

				buildConfigHashCtx.Mixin(codeGenOptions.mDisableTailCalls);
				buildConfigHashCtx.Mixin(codeGenOptions.mDisableUnitAtATime);
				buildConfigHashCtx.Mixin(codeGenOptions.mDisableUnrollLoops);
				buildConfigHashCtx.Mixin(codeGenOptions.mBBVectorize);
				buildConfigHashCtx.Mixin(codeGenOptions.mSLPVectorize);
				buildConfigHashCtx.Mixin(codeGenOptions.mLoopVectorize);
				buildConfigHashCtx.Mixin(codeGenOptions.mRerollLoops);
				buildConfigHashCtx.Mixin(codeGenOptions.mLoadCombine);
				buildConfigHashCtx.Mixin(codeGenOptions.mDisableGVNLoadPRE);
				buildConfigHashCtx.Mixin(codeGenOptions.mVerifyInput);
				buildConfigHashCtx.Mixin(codeGenOptions.mVerifyOutput);
				buildConfigHashCtx.Mixin(codeGenOptions.mStripDebug);
				buildConfigHashCtx.Mixin(codeGenOptions.mMergeFunctions);
				buildConfigHashCtx.Mixin(codeGenOptions.mEnableMLSM);
				buildConfigHashCtx.Mixin(codeGenOptions.mRunSLPAfterLoopVectorization);
				buildConfigHashCtx.Mixin(codeGenOptions.mUseGVNAfterVectorization);
			}
			buildConfigHashCtx.Mixin(project->mDisabled);
			buildConfigHashCtx.Mixin(project->mTargetType);

			for (auto dep : project->mDependencies)
			{
				String depName = dep->mName;
				buildConfigHashCtx.MixinStr(depName);
			}

			Val128 buildConfigHash = buildConfigHashCtx.Finish128();

			HashContext vDataConfigHashCtx;
			vDataConfigHashCtx.Mixin(buildConfigHash);
			vDataConfigHashCtx.MixinStr(project->mStartupObject);
			vDataConfigHashCtx.Mixin(project->mTargetType);

			//Val128 vDataConfigHash = buildConfigHash;
			//vDataConfigHash = Hash128(project->mStartupObject.c_str(), (int)project->mStartupObject.length() + 1, vDataConfigHash);
			//vDataConfigHash = Hash128(&project->mTargetType, sizeof(project->mTargetType), vDataConfigHash);

			auto vDataConfigHash = vDataConfigHashCtx.Finish128();

			project->mBuildConfigChanged = buildConfigHash != project->mBuildConfigHash;
			project->mBuildConfigHash = buildConfigHash;			
			project->mVDataConfigHash = vDataConfigHash;
		}
	}		
	
	Array<BfModule*> moduleRebuildList;

	for (int moduleIdx = 0; moduleIdx < (int)mModules.size(); moduleIdx++)
	{
		//mCompiler->mOutputDirectory
		auto module = mModules[moduleIdx];
		
		// This logic needs to run on both us and our mOptModule
		//for (int subModuleIdx = 0; subModuleIdx < 2; subModuleIdx++)
		auto subModule = module;
		while (subModule != NULL)
		{
			//auto subModule = module;
			//if (subModuleIdx == -1)
				//subModule = module->mOptModule;

			// If we canceled the last build, we could have specialized method modules referring to projects that have
			//  since been deleted or disabled - so remove those
			for (auto methodModuleItr = subModule->mSpecializedMethodModules.begin(); methodModuleItr != subModule->mSpecializedMethodModules.end(); )
			{
				auto& projectList = methodModuleItr->mKey;
				auto specModule = methodModuleItr->mValue;
				bool hasDisabledProject = false;

				for (auto checkProject : projectList)
					hasDisabledProject |= checkProject->mDisabled;

				if (hasDisabledProject)
				{
					delete specModule;
					methodModuleItr = subModule->mSpecializedMethodModules.Remove(methodModuleItr);
				}
				else
					++methodModuleItr;
			}

			subModule = subModule->mNextAltModule;
		}
		
		if ((module->mProject != NULL) && (module->mProject->mDisabled))
		{
			continue;
		}

		// Module previously had error so we have to rebuild the whole thing
		bool needsModuleRebuild = module->mHadBuildError;
		if ((module->mHadHotObjectWrites) && (!mCompiler->IsHotCompile()))
		{
			module->mHadHotObjectWrites = false; // Handled, can reset now
			needsModuleRebuild = true;
		}
		if (module->mProject != NULL)
		{			
			if ((module->mIsHotModule) && (mCompiler->mOptions.mHotProject == NULL))
				needsModuleRebuild = true;
			if (module->mProject->mBuildConfigChanged)
				needsModuleRebuild = true;
		}

		if (mCompiler->mInterfaceSlotCountChanged)
		{
			if ((module->mUsedSlotCount >= 0) && (module->mUsedSlotCount != mCompiler->mMaxInterfaceSlots))
				needsModuleRebuild = true;
		}

		if (needsModuleRebuild)
			moduleRebuildList.push_back(module);		

		if (module->mIsSpecialModule) // vdata, external, data
			continue;
		
		bool wantMethodSpecializations = !mCompiler->mIsResolveOnly;

		// We don't really need this on for resolveOnly passes, but this is useful to force on for debugging.
		//  The following block is fairly useful for detecting dependency errors.
		wantMethodSpecializations = true;
	}

	mCompiler->mInterfaceSlotCountChanged = false;
	for (auto module : moduleRebuildList)
	{
		if (!module->mIsDeleting)
			module->StartNewRevision();
	}

	// Ensure even unspecialized types an interfaces get rebuilt
	//  In particular, this is needed if we build a non-hotswap config and then
	//  build a hotswap config-- we need to make sure all those methods have
	//  HotMethodData
	if (mSystem->mWorkspaceConfigChanged)
	{
		for (auto type : mResolvedTypes)
		{			
			RebuildType(type);
		}
	}
}

void BfContext::VerifyTypeLookups(BfTypeInstance* typeInst)
{
	for (auto& lookupEntryPair : typeInst->mLookupResults)
	{
		BfTypeLookupEntry& lookupEntry = lookupEntryPair.mKey;
		bool isDirty = false;
		if (lookupEntry.mName.IsEmpty())		
		{
			// If the name lookup failed before, thats because we didn't have the right atoms.  Are there new atoms now?
			if (lookupEntry.mAtomUpdateIdx != mSystem->mAtomUpdateIdx)
				isDirty = true;
		}
		else
		{
			// If any atoms have been placed in the graveyard, typesHash will be zero and thus cause a rebuild
			uint32 atomUpdateIdx = lookupEntry.mName.GetAtomUpdateIdx();
			
			// Sanity check, mostly checking that useTypeDef wasn't deleted
			BF_ASSERT((lookupEntry.mUseTypeDef->mName->mAtomUpdateIdx >= 1) && (lookupEntry.mUseTypeDef->mName->mAtomUpdateIdx <= mSystem->mAtomUpdateIdx));

			// Only do the actual lookup if types were added or removed whose name is contained in one of the name parts referenced
			if (atomUpdateIdx != lookupEntry.mAtomUpdateIdx)
			{
				// NOTE: we purposely don't use mNextRevision here. If the the was NOT rebuilt then that means we didn't actually rebuild
				//  so the mNextRevision will be ignored
				auto useTypeDef = lookupEntry.mUseTypeDef;
				BfTypeDef* ambiguousTypeDef = NULL;
				BfTypeDef* result = mSystem->FindTypeDef(lookupEntry.mName, lookupEntry.mNumGenericParams, useTypeDef->mProject, useTypeDef->mNamespaceSearch, &ambiguousTypeDef);
				if (result != lookupEntryPair.mValue.mTypeDef)
					isDirty = true;
				else
					lookupEntry.mAtomUpdateIdx = atomUpdateIdx;
			}
		}		

		if (isDirty)
		{						
			// Clear lookup results to avoid infinite recursion
			typeInst->mLookupResults.Clear();

			// We need to treat this lookup as if it changed the whole type signature
			TypeDataChanged(typeInst, true);
			TypeMethodSignaturesChanged(typeInst);
			RebuildType(typeInst);
			break;
		}
	}
}

void BfContext::GenerateModuleName_TypeInst(BfTypeInstance* typeInst, String& name)
{
	auto resolveModule = typeInst->mIsReified ? mScratchModule : mUnreifiedModule;
	auto outerType = resolveModule->GetOuterType(typeInst);
	int startGenericIdx = 0;
	if (outerType != NULL)
	{
		startGenericIdx = (int)outerType->mTypeDef->mGenericParamDefs.size();
		GenerateModuleName_Type(outerType, name);

		/*if ((!name.empty()) && (name[name.length() - 1] != '_'))
			name += '_';*/
	}
	else
	{		
		for (int i = 0; i < typeInst->mTypeDef->mNamespace.mSize; i++)
		{
			auto atom = typeInst->mTypeDef->mNamespace.mParts[i];
			if ((!name.empty()) && (name[name.length() - 1] != '_'))
				name += '_';
			name += atom->mString;
		}
	}

	if ((!name.empty()) && (name[name.length() - 1] != '_'))
		name += '_';

	if (typeInst->mTypeDef->IsGlobalsContainer())
		name += "GLOBALS_";
	else
		name += typeInst->mTypeDef->mName->mString;

	if (typeInst->IsClosure())
	{
		auto closureType = (BfClosureType*)typeInst;		
		name += closureType->mNameAdd;
		return;
	}

	for (int genericIdx = startGenericIdx; genericIdx < (int)typeInst->mTypeDef->mGenericParamDefs.size(); genericIdx++)
	{
		auto genericType = (BfGenericTypeInstance*)typeInst;
		
		auto type = genericType->mTypeGenericArguments[genericIdx];
		GenerateModuleName_Type(type, name);
	}
}

void BfContext::GenerateModuleName_Type(BfType* type, String& name)
{
	if ((!name.empty()) && (name[name.length() - 1] != '_'))
		name += '_';

	if (type->IsBoxed())
	{
		auto boxedType = (BfBoxedType*)type;
		if (boxedType->IsBoxedStructPtr())
			name += "BOXPTR_";
		else
			name += "BOX_";
		GenerateModuleName_Type(boxedType->mElementType, name);
		return;
	}

	if (type->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)type;
		name += primType->mTypeDef->mName->mString;
		return;
	}

	if (type->IsPointer())
	{
		auto ptrType = (BfPointerType*)type;
		name += "PTR_";
		GenerateModuleName_Type(ptrType->mElementType, name);		
		return;
	}

	if (type->IsTuple())
	{
		auto tupleType = (BfTupleType*)type;
		name += "TUPLE_";		
		for (int fieldIdx = 0; fieldIdx < (int)tupleType->mFieldInstances.size(); fieldIdx++)
		{
			BfFieldInstance* fieldInstance = &tupleType->mFieldInstances[fieldIdx];
			BfFieldDef* fieldDef = fieldInstance->GetFieldDef();
			String fieldName = fieldDef->mName;
			if ((fieldName[0] < '0') || (fieldName[0] > '9'))
				name += StrFormat("U%d@%s", fieldName.length() + 1, fieldName.c_str());
			GenerateModuleName_Type(fieldInstance->mResolvedType, name);
		}
		return;
	}

	if (type->IsDelegateFromTypeRef() || type->IsFunctionFromTypeRef())
	{
		auto delegateType = (BfDelegateType*)type;

		auto methodDef = delegateType->mTypeDef->mMethods[0];		

		if (type->IsDelegateFromTypeRef())
			name += "DELEGATE_";
		else
			name += "FUNCTION_";
		GenerateModuleName_Type(mScratchModule->ResolveTypeRef(methodDef->mReturnTypeRef), name);
		name += "_";
		for (int paramIdx = 0; paramIdx < methodDef->mParams.size(); paramIdx++)
		{
			if (paramIdx > 0)
				name += "_";
			auto paramDef = methodDef->mParams[paramIdx];
			GenerateModuleName_Type(mScratchModule->ResolveTypeRef(paramDef->mTypeRef), name);			
			name += "_";
			name += paramDef->mName;
		}
		return;
	}

	if (type->IsMethodRef())
	{
		auto methodRefType = (BfMethodRefType*)type;
		BfMethodInstance* methodInstance = methodRefType->mMethodRef;
		name += "METHOD_";
		GenerateModuleName_Type(methodInstance->GetOwner(), name);
		name += "_";
		String addName = methodInstance->mMethodDef->mName;
		for (auto&& c : addName)
		{
			if ((c == '$') || (c == '@'))
				c = '_';
		}
		name += addName;
	}

	if (type->IsConstExprValue())
	{
		auto constExprType = (BfConstExprValueType*)type;
		if (BfIRConstHolder::IsInt(constExprType->mValue.mTypeCode))
		{
			if (constExprType->mValue.mInt64 < 0)
				name += StrFormat("_%ld", -constExprType->mValue.mInt64);
			else
				name += StrFormat("%ld", constExprType->mValue.mInt64);
			return;
		}		
	}

	auto typeInst = type->ToTypeInstance();
	if (typeInst != NULL)
	{
		GenerateModuleName_TypeInst(typeInst, name);
		return;
	}	
}

String BfContext::GenerateModuleName(BfTypeInstance* typeInst)
{	
	String name;	
	GenerateModuleName_Type(typeInst, name);

	int maxChars = 80;
	if (name.length() > 80)
	{
		name.RemoveToEnd(80);
		name += "__";
	}
	for (int i = 0; i < (int)name.length(); i++)
	{
		char c = name[i];
		if (c == '@')
			name[i] = '_';
	}

	String tryName = name;
	for (int i = 2; true; i++)
	{
		if (!mUsedModuleNames.Contains(ToUpper(tryName)))
			return tryName;
		tryName = name + StrFormat("_%d", i);
	}

	return name;
}

bool BfContext::IsSentinelMethod(BfMethodInstance* methodInstance)
{
	return (methodInstance != NULL) && ((uintptr)(methodInstance) <= 1);
}

void BfContext::VerifyTypeLookups()
{
	BP_ZONE("BfContext::VerifyTypeLookups");	

	for (auto type : mResolvedTypes)
	{		
		auto typeInst = type->ToTypeInstance();
		if ((typeInst != NULL) && (!typeInst->IsDeleting()) && (!typeInst->IsIncomplete()))
		{
			VerifyTypeLookups(typeInst);			
		}
	}
}

// When we are rebuilding 'typeInst' and we want to make sure that we rebuild all the methods that were 
//  actively referenced previously, this method will generate BfMethodSpecializationRequest for all used
//  methods from previously-built modules
void BfContext::QueueMethodSpecializations(BfTypeInstance* typeInst, bool checkSpecializedMethodRebuildFlag)
{
	BF_ASSERT(!typeInst->IsDeleting());
	
	BP_ZONE("BfContext::QueueMethodSpecializations");
	BfLogSysM("QueueMethodSpecializations %p\n", typeInst);

	auto module = typeInst->mModule;
	if (module == NULL)
		return;

	BfLogSysM("QueueMethodSpecializations methodInst %p module %p\n", typeInst, module);

	if (!checkSpecializedMethodRebuildFlag)
	{
		// Modules that have already rebuilt have already explicitly added their method specialization requests.
		//  This pass is just for handling rebuilding old specialization requests
		if (module->mRevision == mCompiler->mRevision)
			return;
	}

	// Find any method specialization requests for types that are rebuilding, but from
	//  modules that are NOT rebuilding to be sure we generate those.  Failure to do this
	//  will cause a link error from an old module
	for (auto& methodRefKV : typeInst->mSpecializedMethodReferences)
	{			
		auto& methodRef = methodRefKV.mKey;
		auto& specializedMethodRefInfo = methodRefKV.mValue;
				
		if (checkSpecializedMethodRebuildFlag)
		{
			if ((methodRef.mTypeInstance->mRebuildFlags & BfTypeRebuildFlag_SpecializedMethodRebuild) == 0)
				continue;
		}
		else
		{
			if ((methodRef.mTypeInstance->mModule == NULL) ||
				(methodRef.mTypeInstance->mModule->mRevision != mCompiler->mRevision))
				continue;
		}		

		bool allowMismatch = false;
		if ((methodRef.mTypeInstance->mTypeDef == mCompiler->mInternalTypeDef) || (methodRef.mTypeInstance->mTypeDef == mCompiler->mGCTypeDef))
			allowMismatch = true;

		// The signature hash better not have changed, because if it did then we should have rebuilding 'module'
		//  because of dependencies!  This infers a dependency error.
		int newSignatureHash = (int)methodRef.mTypeInstance->mTypeDef->mSignatureHash;
		BF_ASSERT((newSignatureHash == methodRef.mSignatureHash) || (allowMismatch));

		auto methodDef = methodRef.mTypeInstance->mTypeDef->mMethods[methodRef.mMethodNum];

		auto targetContext = methodRef.mTypeInstance->mContext;
		BfMethodSpecializationRequest* specializationRequest = targetContext->mMethodSpecializationWorkList.Alloc();
		if (specializedMethodRefInfo.mHasReifiedRef)
			specializationRequest->mFromModule = typeInst->mModule;
		else
			specializationRequest->mFromModule = mUnreifiedModule;
		specializationRequest->mFromModuleRevision = typeInst->mModule->mRevision;
		specializationRequest->mMethodDef = methodRef.mTypeInstance->mTypeDef->mMethods[methodRef.mMethodNum];
		specializationRequest->mMethodGenericArguments = methodRef.mMethodGenericArguments;
		specializationRequest->mType = methodRef.mTypeInstance;		

		BfLogSysM("QueueMethodSpecializations typeInst %p specializationRequest %p methodDef %p fromModule %p\n", typeInst, specializationRequest, methodDef, specializationRequest->mFromModule);
	}
}

void BfContext::MarkAsReferenced(BfDependedType* depType)
{
	BF_ASSERT((depType->mRebuildFlags & BfTypeRebuildFlag_AwaitingReference) != 0);
	depType->mRebuildFlags = (BfTypeRebuildFlags)(depType->mRebuildFlags & ~BfTypeRebuildFlag_AwaitingReference);

// 	bool madeFullPass = true;
// 	if (mCompiler->mCanceling)
// 		madeFullPass = false;
// 	if ((mCompiler->mResolvePassData != NULL) && (mCompiler->mResolvePassData->mParser != NULL))
// 		madeFullPass = false;

	// Having this in caused errors when we canceled and then compiled again
	auto typeInst = depType->ToTypeInstance();

	if (typeInst != NULL)
	{
		QueueMethodSpecializations(typeInst, false);
	}
}

static int gCheckIdx = 0;

template <typename T>
void ReportRemovedItem(T workItem)
{
}

template <>
void ReportRemovedItem<BfMethodProcessRequest*>(BfMethodProcessRequest* workItem)
{
	if (workItem->mMethodInstance != NULL)
		BfLogSys(workItem->mFromModule->mSystem, "DoRemoveInvalidWorkItems MethodInstance:%p\n", workItem->mMethodInstance);
}

template <typename T>
void DoRemoveInvalidWorkItems(BfContext* bfContext, WorkQueue<T>& workList, bool requireValidType)
{	
	//auto itr = workList.begin();
	//while (itr != workList.end())

	for (int workIdx = 0; workIdx < (int)workList.size(); workIdx++)
	{
		gCheckIdx++;

		//auto& workItem = *itr;
		auto workItem = workList[workIdx];
		if (workItem == NULL)
			continue;
		
		auto typeInst = workItem->mType->ToTypeInstance();

		if ((workItem->mType->IsDeleting()) ||
			(workItem->mType->mRebuildFlags & BfTypeRebuildFlag_Deleted) ||			
			((workItem->mRevision != -1) && (typeInst != NULL) && (workItem->mRevision != typeInst->mRevision)) ||
			((workItem->mSignatureRevision != -1) && (typeInst != NULL) && (workItem->mSignatureRevision != typeInst->mSignatureRevision)) ||
			((workItem->mFromModuleRevision != -1) && (workItem->mFromModuleRevision != workItem->mFromModule->mRevision)) ||
			((workItem->mFromModule != NULL) && (workItem->mFromModule->mIsDeleting)) ||
			((requireValidType) && (workItem->mType->mDefineState == BfTypeDefineState_Undefined)))
		{
			if (typeInst != NULL)
			{
				BF_ASSERT(
					(workItem->mRevision < typeInst->mRevision) ||
					((workItem->mFromModule != NULL) && (workItem->mFromModuleRebuildIdx != -1) && (workItem->mFromModule->mRebuildIdx != workItem->mFromModuleRebuildIdx)) ||
					((workItem->mRevision == typeInst->mRevision) && (workItem->mType->mRebuildFlags & BfTypeRebuildFlag_Deleted)) ||
					(workItem->mType->mDefineState == BfTypeDefineState_Undefined));
			}
			BfLogSys(bfContext->mSystem, "Removing work item: %p ReqId:%d\n", workItem, workItem->mReqId);
			ReportRemovedItem(workItem);
			workIdx = workList.RemoveAt(workIdx);
			//itr = workList.erase(itr);					
		}
		//else
			//++itr;		
	}
}

void BfContext::RemoveInvalidFailTypes()
{	
	for (auto itr = mFailTypes.begin(); itr != mFailTypes.end(); )
	{
		auto typeInst = *itr;
		BfLogSysM("Checking FailType: %p\n", typeInst);
		if ((typeInst->IsDeleting()) || (typeInst->mRebuildFlags & BfTypeRebuildFlag_Deleted))
		{
			BfLogSysM("Removing Invalid FailType: %p\n", typeInst);
			itr = mFailTypes.Remove(itr);
		}
		else
			itr++;
	}	
}

// These work items are left over from a previous canceled run, OR from explicit method
//  specializations being rebuilt when the type is rebuilt
void BfContext::RemoveInvalidWorkItems()
{	
	BfLogSysM("RemoveInvalidWorkItems %p\n", this);

	// Delete any request that include deleted types.
	//  For the list items referring to methods we check the LLVMType because that lets us know
	//  whether or not the type has been reset since these work items were requested

	DoRemoveInvalidWorkItems<BfMethodProcessRequest>(this, mMethodWorkList, true);
	DoRemoveInvalidWorkItems<BfInlineMethodRequest>(this, mInlineMethodWorkList, true);	
	//TODO: We used to pass true into requireValidType, but this gets populated from UpdateRevisedTypes right before RemoveInvalidWorkItems,
	//  so we're passing false in here now.  Don't just switch it back and forth - find why 'false' was causing an issue.
	//  Same with mMethodSpecializationWorkList	
	DoRemoveInvalidWorkItems<BfTypeProcessRequest>(this, mPopulateTypeWorkList, false);	
	DoRemoveInvalidWorkItems<BfMethodSpecializationRequest>(this, mMethodSpecializationWorkList, false/*true*/);

	
#ifdef _DEBUG
	for (auto& workItem : mMethodWorkList)
	{
		//BF_ASSERT(workItem.mMethodInstance->mDeclModule != NULL);		
	}

	for (auto workItem : mMethodSpecializationWorkList)
	{
		if (workItem == NULL)
			continue;
		for (auto genericArg : workItem->mMethodGenericArguments)
		{
			BF_ASSERT((genericArg->mRebuildFlags & BfTypeRebuildFlag_Deleted) == 0);
			BF_ASSERT(!genericArg->IsDeleting());
		}
	}
#endif

	if (mCompiler->mRevision == mScratchModule->mRevision)
	{
		// We have deleted the old module so we need to recreate unspecialized LLVMFunctions
		for (auto workListItem : mMethodWorkList)
		{
			if ((workListItem != NULL) && (workListItem->mType->IsUnspecializedType()))
			{				
				workListItem->mMethodInstance->mIRFunction = BfIRFunction();												
			}			
		}
	}		
	
	RemoveInvalidFailTypes();
}

void BfContext::RemapObject()
{
	
	// There are several types that get their LLVM type mapped to Object, so make sure to remap that 
	//  for when Object itself gets recreated
		
	auto objectType = mScratchModule->ResolveTypeDef(mCompiler->mBfObjectTypeDef, BfPopulateType_Declaration);
	auto objectTypeInst = objectType->ToTypeInstance();
	if (objectTypeInst->mRevision == mMappedObjectRevision)
		return;
	mMappedObjectRevision = objectTypeInst->mRevision;
	
	for (int paramKind = 0; paramKind < 2; paramKind++)
	{
		for (int paramIdx = 0; paramIdx < (int)mGenericParamTypes[paramKind].size(); paramIdx++)
		{
			auto genericParam = mGenericParamTypes[paramKind][paramIdx];			
			genericParam->mSize = objectType->mSize;
			genericParam->mAlign = objectType->mAlign;
		}
	}

	auto varType = mScratchModule->GetPrimitiveType(BfTypeCode_Var);	
	varType->mSize = objectType->mSize;
	varType->mAlign = objectType->mAlign;
}

void BfContext::CheckSpecializedErrorData()
{
	//TODO: Unecessary now?
	/*for (auto& specializedErrorData : mSpecializedErrorData)
	{
		bool ignoreError = false;
		if (specializedErrorData.mRefType->IsDeleting())
			ignoreError = true;
		if (specializedErrorData.mMethodInstance != NULL)
		{
			for (auto genericArg : specializedErrorData.mMethodInstance->mMethodGenericArguments)
				if (genericArg->IsDeleting())
					ignoreError = true;
		}
		if (ignoreError)
		{
			if (specializedErrorData.mMethodInstance->mIRFunction != NULL)
			{
				specializedErrorData.mMethodInstance->mIRFunction->eraseFromParent();
				specializedErrorData.mMethodInstance->mIRFunction = NULL;
			}
			specializedErrorData.mError->mIgnore = true;
		}
		else
		{
			specializedErrorData.mModule->mHadBuildError = true;
			mFailTypes.insert(specializedErrorData.mRefType);
		}
	}*/
}

void BfContext::TryUnreifyModules()
{
	BP_ZONE("BfContext::TryUnreifyModules");

	for (auto module : mModules)
	{		
		if (module->mIsSpecialModule)
			continue;

		if (!module->mIsReified)
			continue;

		if (module->mLastUsedRevision == mCompiler->mRevision)
			continue;

		bool isRequired = false;
		for (auto typeInst : module->mOwnedTypeInstances)
		{
			if ((typeInst->mTypeDef->mIsAlwaysInclude) || (typeInst->mTypeDef->IsGlobalsContainer()))
				isRequired = true;
		}

		if (isRequired)
			continue;

		module->UnreifyModule();
	}
}

void BfContext::MarkUsedModules(BfProject* project, BfModule* module)
{
	BP_ZONE("BfContext::MarkUsedModules");

	if (module->mIsScratchModule)
		return;

	if (project->mUsedModules.Contains(module))	
		return;	

	if (!mCompiler->IsModuleAccessible(module, project))
		return;

	project->mUsedModules.Add(module);

	for (auto& typeDataKV : module->mTypeDataRefs)
		project->mReferencedTypeData.Add(typeDataKV.mKey);	

	for (auto& slotKV : module->mInterfaceSlotRefs)
	{
		auto typeInstance = slotKV.mKey;
		if ((typeInstance->mSlotNum < 0) && (mCompiler->mHotState != NULL))
			mCompiler->mHotState->mHasNewInterfaceTypes = true;
		mReferencedIFaceSlots.Add(typeInstance);
	}
	
	for (auto& kv : module->mStaticFieldRefs)
	{
		auto& fieldRef = kv.mKey;
		auto typeInst = fieldRef.mTypeInstance;
		BF_ASSERT(!typeInst->IsDataIncomplete());
		BF_ASSERT(fieldRef.mFieldIdx < typeInst->mFieldInstances.size());
		if (fieldRef.mFieldIdx < typeInst->mFieldInstances.size())
			typeInst->mFieldInstances[fieldRef.mFieldIdx].mLastRevisionReferenced = mCompiler->mRevision;
	}

	module->mLastUsedRevision = mCompiler->mRevision;
	for (auto usedModule : module->mModuleRefs)
	{		
		MarkUsedModules(project, usedModule);
	}

	for (auto& kv : module->mSpecializedMethodModules)
	{
		MarkUsedModules(project, kv.mValue);
	}
}

void BfContext::Finish()
{		
	
}

void BfContext::Cleanup()
{
	// Can't clean up LLVM types, they are allocated with a bump allocator
	RemoveInvalidFailTypes();	

	for (auto module : mDeletingModules)
	{
// 		auto itr = std::find(mFinishedModuleWorkList.begin(), mFinishedModuleWorkList.end(), module);
// 		if (itr != mFinishedModuleWorkList.end())
// 			mFinishedModuleWorkList.erase(itr);

		int idx = (int)mFinishedModuleWorkList.IndexOf(module);
		if (idx != -1)		
			mFinishedModuleWorkList.RemoveAt(idx);		

		idx = (int)mFinishedSlotAwaitModuleWorkList.IndexOf(module);
		if (idx != -1)
			mFinishedSlotAwaitModuleWorkList.RemoveAt(idx);

		delete module;
	}
	mDeletingModules.Clear();	

	// Clean up deleted BfTypes
	for (auto type : mTypeGraveyard)
	{		
		BF_ASSERT(type->mRebuildFlags & BfTypeRebuildFlag_Deleted);
		delete type;
	}
	mTypeGraveyard.Clear();
	for (auto typeDef : mTypeDefGraveyard)
		delete typeDef;
	mTypeDefGraveyard.Clear();

	// We can't remove the local methods if they still may be referenced by a BfMethodRefType used to specialize a method
	if (mMethodWorkList.empty())
	{
		Array<BfLocalMethod*> survivingLocalMethods;

		for (auto localMethod : mLocalMethodGraveyard)
		{
			if ((localMethod->mMethodInstanceGroup != NULL) && (localMethod->mMethodInstanceGroup->mRefCount > 0))
				survivingLocalMethods.push_back(localMethod);			
			else
				delete localMethod;
		}
		mLocalMethodGraveyard = survivingLocalMethods;
	}
		
	mScratchModule->Cleanup();
	mUnreifiedModule->Cleanup();
	for (auto module : mModules)		
		module->Cleanup();
}

